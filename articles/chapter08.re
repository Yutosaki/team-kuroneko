= Operatorによる実行処理の分離

== 第7章までの課題：実行処理の複雑化と中間結果

第7章では、SQLの条件に応じて全件走査とインデックス走査を選択するPlannerを実装しました。しかし、WHEREによる絞り込み、SELECT列の取り出し、JOINなどはQueryExecutorに残っており、機能を増やすたびにQueryExecutorの分岐も増える構造でした。

また、取得した行を@<code>{List<Row>}に保持してから次の処理へ渡すため、処理の途中に大きな中間結果が作られます。データが増えると、QueryExecutorの複雑さとメモリ使用量の両方が増加します。

第1章のロードマップでは、第8章を「構造の分離」を行う章と位置付けました。本章では、実行処理を@<b>{Operator}という小さな単位へ分け、必要な順序に組み合わせます。目指す効果は次のとおりです。

 * 走査、絞り込み、射影、結合をOperatorという単位で分けることで、各処理を単独で変更・再利用できます。
 * 共通の@<code>{open()}、@<code>{next()}、@<code>{close()}を使うことで、異なる処理を同じ方法で実行できます。
 * 行を1行ずつ渡すことで、処理ごとに中間結果のListを作る必要がなくなります。
 * Operatorを木構造にすることで、SQLの処理順序を組み替えられます。
 * Plannerが実行木を作ることで、QueryExecutorはOperatorの種類を判断せずに済み、同じ実行処理を再利用できます。

つまり、本章の目的はクラスを増やすことではありません。SQLの機能を追加しても既存処理への影響を小さくできる、拡張しやすい実行基盤を作ることです。

== Operatorとイテレータモデルの導入

検索、絞り込み、射影、結合を一つのメソッドに書くと、処理順序を変更するたびにメソッド全体を修正しなければなりません。そこで、それぞれをOperatorとして分け、上位のOperatorが下位のOperatorへ1行ずつ要求する@<b>{イテレータモデル}を導入します。

たとえば、次のSELECT文を考えます。

//emlist[絞り込みと射影を行うSELECT文][sql]{
SELECT name FROM users WHERE age >= 20;
//}

このSQLは、全件走査、条件による絞り込み、カラムの取り出しをつないだ実行木で表せます。

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

QueryExecutorが根のProjectOperatorへ行を要求すると、要求はFilterOperator、SeqScanOperatorの順に伝わります。SeqScanOperatorが返した行をFilterOperatorが判定し、条件を満たした行だけをProjectOperatorが{name=Taro}のような結果へ変換します。

すべてのOperatorを同じ方法で扱うため、次のインターフェースを定義します。

//emlist[Operatorインターフェース][java]{
public interface Operator {
  void open() throws IOException;

  Row next() throws IOException;

  void close() throws IOException;
}
//}

@<code>{open()}は処理の準備、@<code>{next()}は次の1行の取得、@<code>{close()}は終了処理を担当します。返せる行がなくなるとnextはnullを返します。

この約束を守れば、全件走査でもインデックス走査でも、QueryExecutorから見た実行方法は変わりません。また、各Operatorは子Operatorの種類を知らずにnextを呼べるため、走査方法や処理順序を差し替えられます。

本章では、行を読み出すSeqScanOperatorとIndexScanOperator、行を加工するFilterOperator、ProjectOperator、NestedLoopJoinOperator、データを変更するInsertOperator、UpdateOperator、DeleteOperatorを実装します。

== 走査Operatorの実装

全件走査とインデックス走査を共通のOperatorとして扱えると、上位の絞り込みや射影を変更せずに検索方法だけを選択できます。本節では、実行木の葉から行を供給する二つのOperatorを作ります。

==== SeqScanOperator

@<code>{SeqScanOperator}は、指定されたTableの行を先頭から順に返します。openでIteratorを準備し、nextが呼ばれるたびに次のRowを返します。

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

@<code>{qualifyRow()}は、idに加えてusers.idのようなテーブル名付きのカラムをRowへ登録します。これにより、単一テーブルでは短いカラム名を使い、JOINでは同名カラムをテーブル名で区別できます。

==== IndexScanOperator

@<code>{IndexScanOperator}は、第7章で実装した@<code>{Table.searchByIndex()}から一致する行を取得します。SeqScanOperatorと異なるのは、主にopenで行を取得する方法です。

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

nextとcloseはSeqScanOperatorと同じ形です。そのため、上位のOperatorはどちらの走査方法が使われているかを意識しません。本章では、第7章と同じく、インデックス付きINTEGER型カラムに対する@<code>{=}条件でIndexScanOperatorを使用します。

== 行を加工するOperatorの実装

WHERE、SELECT、JOINを独立したOperatorへ分けると、条件評価やカラム選択を複数の実行計画から再利用できます。また、Operatorの接続順序によってSQLの処理順序を表せます。

==== FilterOperator

@<code>{FilterOperator}は、子Operatorから受け取った行を評価し、WHERE条件を満たす行だけを返します。

//emlist[条件に一致するまで行を取得する処理][java]{
public Row next() throws IOException {
  while (true) {
    Row row = child.next();
    if (row == null) {
      return null;
    }
    if (ConditionEvaluator.matches(row, condition)) {
      return row;
    }
  }
}
//}

条件評価は@<code>{ConditionEvaluator}へ分離します。第6章ではQueryExecutor内にあった比較処理を独立させることで、FilterOperatorだけでなくJOIN、UPDATE、DELETEからも同じ判定を利用できます。

==== ProjectOperator

@<code>{ProjectOperator}は、子Operatorが返したRowをSELECT句に対応するRowへ変換します。

//emlist[1行を射影する処理][java]{
public Row next() throws IOException {
  Row row = child.next();
  if (row == null) {
    return null;
  }
  return projectRow(row, selectColumns, hasJoin);
}
//}

SELECT句が@<code>{*}なら全カラムを返し、カラム名が指定されていれば該当する値だけを返します。FilterOperatorの上に配置することで、「WHEREで絞り込んでからSELECT列を取り出す」という順序が実行木に表れます。

==== NestedLoopJoinOperator

@<code>{NestedLoopJoinOperator}は左右の子Operatorから行を受け取り、ON条件を満たす組み合わせを返します。

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

右側を最後まで調べたら、右のOperatorを開き直し、左の次の行との比較を始めます。第6章から使っているNested Loop Joinの考え方は変わりませんが、専用のOperatorにすることでQueryExecutorから結合処理を取り除けます。将来Hash Joinを追加する場合も、同じインターフェースで置き換えられます。

== Plannerによる実行木の構築

Operatorを分けるだけでは、どのOperatorをどの順番で接続するかは決まりません。PlannerがStatementとSchemaから実行木を作ることで、SQLの結果を保ったまま検索方法や処理順序を選択できます。

==== 単一テーブルの実行木

Plannerは、最初に実行木の葉となる走査方法を選びます。インデックスを利用できる場合はIndexScanOperator、それ以外はSeqScanOperatorです。

//emlist[走査方法を選択する処理][java]{
Operator plan;

if (condition != null && isIndexable(schema, condition)) {
  plan = new IndexScanOperator(
      table, schema, statement.tableName(), condition);
} else {
  plan = new SeqScanOperator(table, schema, statement.tableName());
}
//}

WHERE条件があればFilterOperatorを重ね、最後にProjectOperatorを重ねます。たとえば@<code>{SELECT name FROM users WHERE age >= 20}は、次の実行木になります。

//cmd{
ProjectOperator [name]
        │
FilterOperator [age >= 20]
        │
SeqScanOperator [users]
//}

Plannerが返すのは根のProjectOperatorだけです。根から子へ処理が伝わるため、QueryExecutorは実行木の内部構造を調べる必要がありません。

==== JOINと条件のプッシュダウン

JOINでは、WHERE条件をどこへ置くかによって比較する行数が変わります。条件が左テーブルだけを参照する場合は、NestedLoopJoinOperatorより下にFilterOperatorを置きます。これを@<b>{条件のプッシュダウン}と呼びます。

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

先に左側の行を減らすことで、JOINが比較する組み合わせも減ります。右テーブルのカラムを参照する条件は結合前に評価できないため、NestedLoopJoinOperatorより上にFilterOperatorを置きます。

この実装は単純な一つのConditionだけを対象としますが、Operatorの配置によって結果を変えずに処理量を減らせることを確認できます。

== 更新系Operatorの実装

SELECTだけをOperator化しても、INSERT、UPDATE、DELETEの個別処理がQueryExecutorに残れば、実行方法は統一されません。更新処理もOperatorにすることで、すべてのCRUD文をopen、next、closeで実行し、結果行や処理件数の数え方も共有できます。

==== InsertOperator

@<code>{InsertOperator}は、Statement.Insertの値をSchemaに従ってRowへ変換し、Tableへ挿入します。INSERTは1回だけ実行するため、executedフラグで二重実行を防ぎます。

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

最初のnextは挿入したRowを返し、次のnextはnullを返します。

==== UpdateOperatorとDeleteOperator

UpdateOperatorとDeleteOperatorは、RowとRecordIdの組を順番に取得し、ConditionEvaluatorでWHERE条件を評価します。UpdateOperatorは同じRecordIdへ変更後のRowを書き戻し、DeleteOperatorはRecordIdの使用フラグを変更します。

//emlist[条件に一致した1行を更新する処理][java]{
while (iterator != null && iterator.hasNext()) {
  Table.Record record = iterator.next();
  Row row = record.row();

  if (ConditionEvaluator.matches(
      row, statement.whereCondition())) {
    Object newValue =
        parseValue(statement.value(), targetColumn);
    row.put(targetColumn.name(), newValue);
    table.update(record.recordId(), row);
    return row;
  }
}

return null;
//}

一致する行が複数あれば、nextが呼ばれるたびに1行ずつ処理します。Tableへ更新を委譲するため、第7章で実装したインデックスの同期処理もそのまま利用できます。

== QueryExecutorによるOperatorの実行

Plannerが実行木を作り、すべての処理がOperatorになったことで、QueryExecutorはSQLごとの検索方法や処理順序を持たずに済みます。その結果、新しいOperatorを追加しても共通の実行ループを再利用できます。

CREATE TABLE以外のStatementはPlannerへ渡し、根のOperatorを取得します。

//emlist[Plannerから根のOperatorを受け取る処理][java]{
public void execute(Statement statement) throws IOException {
  if (statement instanceof Statement.CreateTable createTableStmt) {
    executeCreateTable(createTableStmt);
  } else {
    Operator rootPlan = planner.createPlan(statement);
    executePlan(rootPlan, statement);
  }
}
//}

実行時はopenを呼び、nextがnullを返すまでRowを受け取ります。最後に必ずcloseを呼べるように、終了処理をfinallyへ置きます。

//emlist[Operatorを実行する共通ループ][java]{
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

SELECTではRowを表示し、UPDATEとDELETEではnextが返した回数を処理件数として表示します。QueryExecutorがOperatorの種類を判定しないことが、実行処理を再利用できる理由です。

== 実行結果の確認

本章の変更はデータベース内部の構造を整理するものであり、利用者が入力するSQLや結果は第7章から変わりません。

//cmd{
db > SELECT name FROM users WHERE age >= 20;
{name=Taro}

db > UPDATE users SET age = 21 WHERE id = 1;
Updated 1 row(s).

db > DELETE FROM users WHERE id = 2;
Deleted 1 row(s).
//}

SELECTは走査、絞り込み、射影をつないだ実行木で処理され、UPDATEとDELETEは共通の実行ループで件数を数えます。外部のインターフェースを保ったまま内部構造を変更できたことは、Parser、Planner、Operator、Tableの責務が分離されていることを示します。

== 本章のまとめ：構造の分離と残る課題

第1章では、完成時のデータベースをフロントエンド層、実行処理層、ストレージ層に分けました。本章のOperator導入により、実装上のクラスもこの役割へ対応します。

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

ParserはSQLをStatementへ変換し、PlannerはOperatorを組み合わせます。QueryExecutorは実行木の根を共通の方法で動かし、TableとIndexは行の保存と検索に集中します。この分離により、検索方法や結合方法を追加するときも、変更する範囲を対応するOperatorとPlannerへ限定できます。

一方、現在のSeqScanOperatorはopenで@<code>{Table.scan()}を呼び出し、その戻り値であるList<Row>を使用します。UpdateOperatorとDeleteOperatorも@<code>{Table.scanRecords()}が作る一覧を使用します。上位のOperator間では1行ずつ受け渡しますが、ストレージからの読み込みまで完全にストリーミング化されたわけではありません。

完全に1行ずつ処理するには、Table側にもページとスロットを順に読むCursorが必要です。

//cmd{
現在
Table.scan() ──▶ List<Row> ──▶ SeqScanOperator.next()

今後の拡張例
Table Cursor ── 1行ずつ ──▶ SeqScanOperator.next()
//}

また、本章のPlannerが行う最適化は、インデックス走査の選択と限定的な条件のプッシュダウンだけです。実際のデータベースでは、統計情報を使ったコスト推定、JOIN順序の変更、SortやAggregateなど、さらに多くの処理が必要になります。

本章では、それらをOperatorとして追加し、Plannerで組み合わせるための土台を作りました。次章では、最小のデータベースからここまでに追加した仕組みを振り返り、実用的なデータベースとの差と今後の発展を整理します。
