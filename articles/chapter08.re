= オペレータによる実行処理の分離

== 第7章までの課題：実行処理とストレージの密結合、メモリ枯渇

第7章では、SQLの条件に応じて全件走査とインデックス走査を選択するプランナ（Planner）を実装しました。しかし、WHEREによる絞り込み、SELECT列の取り出し、JOINなどの処理は依然としてクエリエグゼキュータ（QueryExecutor）内に残っており、機能を増やすたびに分岐が肥大化してしまう密結合な構造でした。

さらに深刻なのが、パフォーマンスとメモリの課題です。現在の実装では、取得した行を @<code>{List<Row>} に一気に保持してから次の処理（絞り込みなど）へ渡すため、処理の途中に巨大な中間結果が作られます。データが増えるとメモリを過大に消費し、いずれメモリ枯渇（Out Of Memory）を引き起こす危険性があります。

== 本章の方針：実行処理の抽象化とパイプライン化

これらの課題を解決するため、第1章のロードマップで示した通り、本章では「構造の分離」を行います。実行処理を@<b>{オペレータ（Operator）}という小さな単位へ分け、それらをパイプラインのようにつなぎ合わせるアーキテクチャを導入します。

具体的には、以下の順序で実装を進めます。

 1. @<b>{イテレータモデルの導入}: すべての処理を @<code>{open()}、@<code>{next()}、@<code>{close()} という共通のインターフェースで扱えるように基盤を整えます。
 2. @<b>{走査と加工のオペレータ化}: データの読み出し（走査）、WHEREによる絞り込み（Filter）、SELECT列の抽出（Project）、結合（JOIN）をそれぞれ独立したオペレータとして実装します。
 3. @<b>{更新系のオペレータ化}: INSERT、UPDATE、DELETEも同じ仕組みで動くようにオペレータ化し、実行方法を完全に統一します。
 4. @<b>{プランナによる実行計画ツリーの構築}: プランナがオペレータを組み合わせて「実行計画ツリー」を作り、クエリエグゼキュータが内部構造を意識せずに実行する仕組みへと改修します。

本章の目的は、単にクラスを増やすことではありません。行を1行ずつバケツリレーのように渡すことで巨大な中間結果をなくし、今後SQLの機能を追加しても既存処理への影響を最小限に抑えられる、拡張性と堅牢性を備えた実行基盤を作ることです。

== オペレータとイテレータ（Volcano）モデルの導入

検索、絞り込み、射影、結合を一つのメソッドに書くと、処理順序を変更するたびにメソッド全体を修正しなければなりません。そこで、それぞれの役割をオペレータとして独立させ、上位のオペレータが下位のオペレータへ「次の1行をちょうだい」と要求するアーキテクチャを導入します。
これはデータベースエンジンの世界で@<b>{イテレータモデル（またはVolcanoモデル）}と呼ばれる標準的な手法です。

たとえば、次のSELECT文を考えます。

//emlist[絞り込みと射影を行うSELECT文][sql]{
SELECT name FROM users WHERE age >= 20;
//}

このSQLは、全件走査、条件による絞り込み、カラムの取り出しというオペレータをつないだ「実行計画ツリー」で表せます。

//cmd{
ProjectOperator
  SELECT name
       │ next()
       ▼
FilterOperator
  age >= 20
       │ next()
       ▼
SeqScanOperator
  users
//}

クエリエグゼキュータが根（トップ）の @<code>{ProjectOperator} へ行を要求すると、要求は @<code>{FilterOperator}、@<code>{SeqScanOperator} の順に下へ伝わります。一番下の @<code>{SeqScanOperator} が返した行を @<code>{FilterOperator} が判定し、条件を満たした行だけを @<code>{ProjectOperator} が @<code>{{name=Taro}} のような結果へ変換して上に返します。

すべてのオペレータを同じ方法で扱うため、次のインターフェースを定義します。

//emlist[Operatorインターフェース][java]{
public interface Operator {
  void open() throws IOException;

  Row next() throws IOException;

  void close() throws IOException;
}
//}

@<code>{open()} は処理の準備、@<code>{next()} は次の1行の取得、@<code>{close()} は終了処理を担当します。返せる行がなくなると @<code>{next()} は @<code>{null} を返します。

この約束を守れば、全件走査でもインデックス走査でも、呼び出し側から見た実行方法は変わりません。本章では、行を読み出す @<code>{SeqScanOperator} と @<code>{IndexScanOperator}、行を加工する @<code>{FilterOperator}、@<code>{ProjectOperator}、@<code>{NestedLoopJoinOperator}、データを変更する @<code>{InsertOperator}、@<code>{UpdateOperator}、@<code>{DeleteOperator} を順に実装していきます。

== 走査オペレータの実装

全件走査とインデックス走査を共通のオペレータとして扱えると、上位の絞り込みや射影処理を変更することなく、プランナが検索方法だけを差し替えることができます。本節では、実行計画ツリーの「葉（一番下）」にあたり、行を供給する二つのオペレータを作ります。

==== SeqScanOperator

@<code>{SeqScanOperator} は、指定されたテーブルの行を先頭から順に返します。@<code>{open()} でイテレータを準備し、@<code>{next()} が呼ばれるたびに次のRowを返します。

//emlist[SeqScanOperatorの主要な処理][java]{
public void open() throws IOException {
  List<Row> rows = table.scan();
  this.iterator = rows.iterator();
}

public Row next() throws IOException {
  if (iterator != null && iterator.hasNext()) {
    Row rawRow = iterator.next();
    return qualifyRow(tableName, schema, rawRow);
  }
  return null;
}

public void close() throws IOException {
  this.iterator = null;
}
//}

@<code>{qualifyRow()} は、カラム名（id）をテーブル名付き（users.id）に変換してRowへ登録する処理です。これにより、単一テーブルでは短いカラム名を使いつつ、JOIN時には同名カラムをテーブル名で区別できるようになります。

==== IndexScanOperator

@<code>{IndexScanOperator} は、第7章で実装した @<code>{Table.searchByIndex()} を利用して、インデックスに一致する行だけを取得します。@<code>{SeqScanOperator} と異なるのは、主に @<code>{open()} で取得対象を絞り込む点です。

//emlist[IndexScanOperatorのopen][java]{
public void open() throws IOException {
  Schema.Column column = schema.getColumn(condition.left());
  Object value = condition.right();

  String columnName =
      column != null ? column.name() : condition.left();
  if (column != null) {
    value = parseValue(condition.right(), column);
  }

  List<Row> rows = table.searchByIndex(columnName, value);
  this.iterator = rows.iterator();
}
//}

@<code>{next()} と @<code>{close()} は @<code>{SeqScanOperator} と同じ形です。そのため、上位のオペレータはどちらの走査方法が使われているかを一切意識しません。

== 行を加工するオペレータの実装

WHERE、SELECT、JOINといった処理を独立したオペレータへ分けることで、条件評価やカラム選択を複数の実行計画で再利用できるようになります。

==== FilterOperator

@<code>{FilterOperator} は、子オペレータ（下位）から受け取った行を評価し、WHERE条件を満たす行だけを上に返します。

//emlist[条件に一致するまで行を取得する処理][java]{
public Row next() throws IOException {
  while (true) {
    Row row = child.next();
    if (row == null) {
      return null; // 子からのデータが尽きた
    }
    if (ConditionEvaluator.matches(row, condition)) {
      return row; // 条件に一致した行だけを返す
    }
  }
}
//}

条件評価は @<code>{ConditionEvaluator} という別クラスへ分離します。第6章ではクエリエグゼキュータ内にあった比較処理を独立させることで、@<code>{FilterOperator} だけでなく後述の更新系オペレータ等からも同じ判定ロジックを利用できます。

==== ProjectOperator

@<code>{ProjectOperator} は、子オペレータが返したRowを、SELECT句で指定された形（特定のカラムのみ）へと変換（射影）します。

//emlist[1行を射影する処理][java]{
public Row next() throws IOException {
  Row row = child.next();
  if (row == null) {
    return null;
  }
  return projectRow(row, selectColumns, hasJoin);
}
//}

SELECT句が @<code>{*} なら全カラムを返し、カラム名が指定されていれば該当する値だけを抽出して返します。これを @<code>{FilterOperator} の上に配置することで、「WHEREで絞り込んでからSELECT列を取り出す」という論理的な順序が実行計画ツリー上で表現されます。

==== NestedLoopJoinOperator

@<code>{NestedLoopJoinOperator} は左右の二つの子オペレータから行を受け取り、ON条件を満たす組み合わせを返します。

//cmd{
左の1行を取得
      │
      ▼
右の行を先頭から取得
      │
      ▼
ON条件を評価 ── 不一致 ──▶ 右の次の行
      │
     一致
      ▼
結合した1行を返す
//}

右側を最後まで調べたら、右のオペレータを @<code>{close()} して @<code>{open()} し直し、左の次の行との比較を始めます。専用のオペレータにすることで、将来的に「Hash Join」などのより高速な結合アルゴリズムを追加する場合も、同じインターフェースで簡単に差し替えられます。

== 更新系オペレータの実装

SELECTだけをオペレータ化しても、INSERT、UPDATE、DELETEの個別処理がクエリエグゼキュータに残っていては、アーキテクチャが統一されません。更新処理もオペレータにすることで、すべてのCRUDを @<code>{open}、@<code>{next}、@<code>{close} で実行し、処理件数の数え方なども共通化できます。

==== InsertOperator

@<code>{InsertOperator} は、Statementの値をSchemaに従ってRowへ変換し、テーブルへ挿入します。INSERTは1回だけ実行すればよいため、@<code>{executed} フラグで二重実行を防ぎます。

//emlist[一度だけ行を挿入するnext][java]{
public Row next() throws IOException {
  if (executed) {
    return null;
  }
  Row row = buildRow(schema, statement);
  table.insert(row);
  executed = true;
  return row;
}
//}

最初の @<code>{next()} は挿入したRowを返し、次の @<code>{next()} は @<code>{null} を返します。

==== UpdateOperatorとDeleteOperator

更新や削除を行うためには、データの中身（Row）だけでなく、「物理的にディスクのどこにあるのか（RecordId）」という情報が必要です。そのため、子オペレータからRowを受け取る通常のイテレータモデルとは異なり、内部で直接 @<code>{Table.scanRecords()}（RowとRecordIdのペアを返す）を利用して処理を行います。

//emlist[条件に一致した1行を更新する処理（UpdateOperator）][java]{
while (iterator != null && iterator.hasNext()) {
  Table.Record record = iterator.next();
  Row row = record.row();

  if (ConditionEvaluator.matches(
      row, statement.whereCondition())) {
    Object newValue =
        parseValue(statement.value(), targetColumn);
    row.put(targetColumn.name(), newValue);

    // RecordIdを指定して上書き
    table.update(record.recordId(), row);
    return row;
  }
}

return null;
//}

一致する行が複数あれば、@<code>{next()} が呼ばれるたびに1行ずつ処理して結果を返します。テーブルオブジェクトへ更新を委譲するため、第7章で実装したインデックスの同期処理もそのまま利用されます。

== プランナによる実行計画ツリーの構築

オペレータという「部品」が揃いました。次はプランナ（Planner）がStatementとSchemaからこれらを組み立て、実行計画ツリーを作ります。

==== 単一テーブルの実行計画ツリー

プランナは、最初に木の葉（一番下）となる走査方法を選びます。インデックスを利用できる場合は @<code>{IndexScanOperator}、それ以外は @<code>{SeqScanOperator} です。

//emlist[走査方法を選択する処理][java]{
Operator plan;

if (condition != null && isIndexable(schema, condition)) {
  plan = new IndexScanOperator(
      table, schema, statement.tableName(), condition);
} else {
  plan = new SeqScanOperator(table, schema, statement.tableName());
}
//}

WHERE条件があればその上に @<code>{FilterOperator} を重ね、最後に @<code>{ProjectOperator} を重ねます。たとえば @<code>{SELECT name FROM users WHERE age >= 20} は、次の実行計画ツリーになります。

//cmd{
ProjectOperator [name]
        │
FilterOperator [age >= 20]
        │
SeqScanOperator [users]
//}

プランナがクエリエグゼキュータに返すのは、根（トップ）の @<code>{ProjectOperator} だけです。根に @<code>{next()} を呼べば自動的に下へ処理が伝わるため、実行側は内部構造を一切気にする必要がありません。

==== JOINと条件のプッシュダウン

JOINでは、WHERE条件を「ツリーのどこに置くか」によってパフォーマンスが激変します。条件が左テーブルだけを参照する場合、@<code>{NestedLoopJoinOperator} よりも「下」に @<code>{FilterOperator} を置きます。これを@<b>{条件のプッシュダウン}と呼びます。

//cmd{
ProjectOperator
        │
NestedLoopJoinOperator
        ├────────────────┐
        │                │
FilterOperator       SeqScanOperator [右テーブル]
        │
SeqScanOperator [左テーブル]
//}

結合（JOIN）の前に左側の行を減らしておくことで、比較する組み合わせの数が激減します。
この実装は限定的ですが、オペレータの配置順序（実行計画）を変えるだけで、結果を変えずに処理量を劇的に減らせることが体験できます。

== クエリエグゼキュータによるオペレータの実行

プランナが実行計画ツリーを作り、すべての処理がオペレータに抽象化されたことで、クエリエグゼキュータはSQLごとの検索方法や処理順序を管理する必要がなくなりました。

CREATE TABLE以外のStatementはすべてプランナへ渡し、根のオペレータを取得します。

//emlist[プランナから根のオペレータを受け取る処理][java]{
public void execute(Statement statement) throws IOException {
  if (statement instanceof Statement.CreateTable createTableStmt) {
    executeCreateTable(createTableStmt);
  } else {
    Operator rootPlan = planner.createPlan(statement);
    executePlan(rootPlan, statement);
  }
}
//}

実行時は @<code>{open()} を呼び、@<code>{next()} が @<code>{null} を返すまでループでRowを受け取り続けます。最後に必ず @<code>{close()} を呼べるように、終了処理をfinallyブロックへ置きます。

//emlist[オペレータを実行する共通ループ][java]{
private void executePlan(
    Operator rootPlan, Statement statement) throws IOException {
  rootPlan.open();

  try {
    int count = 0;
    while (true) {
      Row row = rootPlan.next();
      if (row == null) {
        break;
      }
      if (statement instanceof Statement.Select) {
        System.out.println(row);
      }
      count++;
    }

    // Statementに応じて空の結果や更新件数を表示する
  } finally {
    rootPlan.close();
  }
}
//}

SELECTではRowを表示し、UPDATEとDELETEでは @<code>{next()} が返した回数を処理件数として表示します。クエリエグゼキュータが「今何のオペレータを動かしているのか」を一切意識しないことが、イテレータモデルの最大の利点です。

== 実行結果の確認

本章の変更はデータベース内部のアーキテクチャを整理するものであり、利用者が入力するSQLやその結果は第7章から変わりません。

//cmd{
db > SELECT name FROM users WHERE age >= 20;
{name=Taro}

db > UPDATE users SET age = 21 WHERE id = 1;
Updated 1 row(s).

db > DELETE FROM users WHERE id = 2;
Deleted 1 row(s).
//}

SELECTは走査、絞り込み、射影をつないだ実行計画ツリーで処理され、UPDATEとDELETEは共通の実行ループで件数がカウントされます。外部のインターフェースを保ったまま内部構造を劇的に変更できたことは、コンポーネントの責務が美しく分離されている証拠です。

== まとめと次章への課題

第1章では、データベース全体をフロントエンド層、実行処理層、ストレージ層に分けました。本章のオペレータ導入により、実装上のクラスが見事にこの役割へと対応しました。

//cmd{
ユーザーのSQL
      │
      ▼
フロントエンド層
Tokenizer・SimpleParser・Statement
      │
      ▼
実行処理層
Planner・Operator・QueryExecutor
      │
      ▼
ストレージ層
Catalog・Schema・Table・Index
      │
      ▼
.tblファイル・catalog.txt
//}

構文解析がSQLをASTへ変換し、プランナがオペレータを組み合わせます。クエリエグゼキュータは実行計画ツリーを共通の方法で動かし、テーブルとインデックスは行の保存と検索に集中します。

一方、本章の冒頭で提起した「メモリ枯渇問題」は、実はまだ完全に解決していません。
現在の @<code>{SeqScanOperator} の実装を振り返ると、@<code>{open()} の中で @<code>{Table.scan()} を呼び出し、その戻り値である巨大な @<code>{List<Row>} をイテレータに変換して使用しています。上位のオペレータ間では1行ずつ受け渡す美しいバケツリレーが完成しましたが、「一番底のストレージからの読み込み」は一括読み込みのままなのです。

これを完全に1行ずつ処理（ストリーミング化）し、真の意味でメモリ消費を抑えるには、Table側にページとスロットを順に1件ずつ読み進める「Cursor（カーソル）」という仕組みを実装する必要があります。

//cmd{
現在
Table.scan() ──(一括)──▶ List<Row> ──▶ SeqScanOperator.next()

真のストリーミング化（今後の課題）
Table Cursor ──(1行ずつ)──▶ SeqScanOperator.next()
//}

また、本章のプランナが行う最適化は、インデックス走査の選択と限定的なプッシュダウンだけです。実際の商用データベースでは、統計情報を使ったコスト推定（CBO）、JOIN順序の動的変更、SortやAggregateなど、さらに多くの複雑な処理が行われています。

本章では、それらを新たなオペレータとして追加し、自由に組み合わせるための「強力な土台」を完成させました。
次章（第9章）では、最小のデータベースからここまでに追加してきた仕組み全体を振り返り、本物の実用的なデータベースシステムに到達するために残された機能と、今後の発展について整理します。
