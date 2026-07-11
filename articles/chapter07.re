= プランナーによる実行計画の選択

== 第6章までの課題：常に全件走査するSELECT

第6章「テーブル定義とSQLの実行」では、SQL文字列をStatementへ変換し、テーブル定義に従って実行する経路を作りました。SELECTは、FROM句のテーブルに対して@<code>{Table.scan()}を呼び出し、すべての行を取り出してからWHERE条件で絞り込みます。この処理は、条件に指定されたカラムが何であっても変わりません。

一方、第5章「B-Treeによる検索の高速化と領域管理」では、整数のidをB-Treeへ登録し、キーからRecordIdを一足飛びに検索する仕組みを扱いました。第6章では、テーブルごとに任意のカラムを扱えるよう構造を作り直したため、このB-Treeを検索へ組み込んでいません。その結果、第6章のSELECTは、idを条件にした場合も、それ以外のカラムを条件にした場合も、すべて全件走査になります。

先頭から1件ずつ順番に調べていく全件走査（線形探索）でかかる時間は、レコードの件数に比例します。この性質と、B-Treeによって検索を高速化できることは、第5章「B-Treeを用いたCRUDの高速化」で確認しました。件数が増えるほど、idのような特定のキーを指定した検索でも全件走査のコストが大きくなります。

インデックスを再び利用するには、次の判断が必要です。

 * WHERE句があるか
 * 条件の左辺に対応するカラムがあるか
 * そのカラムにインデックスがあるか
 * 比較演算子がインデックスで処理できるか

これらを確認し、インデックス検索と全件走査のどちらで実行するかを決める役割が@<b>{プランナー}です。本章では、Statementとテーブル定義から@<b>{実行計画}を作り、条件に応じて実行方法を選択する仕組みを実装します。

== 実行計画とプランナー

@<b>{実行計画}（execution plan、以降プランと呼びます）とは、あるSQLをどの方法で実行するかを表したオブジェクトです。本章では、全件走査を表す@<code>{SeqScanPlan}と、インデックス検索を表す@<code>{IndexScanPlan}の2種類を用意します。

プランナー（@<code>{Planner}）は、Statementとテーブル定義（@<code>{Schema}）を受け取り、どちらのプランを使うかを判断してプランを返します。QueryExecutorは、返されたプランを実行します。

//cmd{
Statement.Select ＋ Schema
        │
        ▼
Planner.createPlan()
条件とスキーマを確認
        │
   ┌────┴────┐
   ▼         ▼
IndexScanPlan  SeqScanPlan
インデックス   全件走査
        │
   └────┬────┘
        ▼
Plan.execute(context, statement)
        │
        ▼
QueryExecutor
executeIndexScan / executeSeqScan
        │
        ▼
   結果を表示
//}

第6章では、@<code>{QueryExecutor.executeSelect()}が直接@<code>{Table.scan()}を呼び出していました。本章では、その間にPlannerとPlanを挟みます。executeSelectは、Schemaを取得し、Plannerにプランを作らせ、そのプランを実行するだけになります。

//emlist[プランを作って実行するexecuteSelect][java]{
private void executeSelect(Statement.Select statement) throws IOException {
  Schema schema = catalog.requireSchema(statement.tableName());
  Plan plan = planner.createPlan(statement, schema);
  plan.execute(this, statement);
}
//}

この分離により、「どの方法で実行するかを決める処理」（Planner）と、「実際にテーブルを読み書きする処理」（QueryExecutor）を、別々に扱えます。

なお、本章でプランナーを経由するのはSELECTだけです。INSERT、UPDATE、DELETE、CREATE TABLEは第6章と同じ経路で実行します。これらの操作でも、インデックスを持つテーブルではインデックスの更新が必要になりますが、その処理はTableの内部で行います（「インデックスの更新」で説明します）。

== カラムへのインデックスの定義

インデックスを使うには、まずどのカラムにインデックスを張るかを指定する必要があります。本章では、CREATE TABLEのカラム定義に@<code>{INDEX}キーワードを追加できるようにします。

=== インデックスを指定するCREATE TABLE

カラムのデータ型の後ろに@<code>{INDEX}と書くと、そのカラムにインデックスを作成します。

//emlist[idカラムにインデックスを張るCREATE TABLE][sql]{
CREATE TABLE users (id INTEGER INDEX, name STRING(20), age INTEGER);
//}

この例では、idにインデックスを張り、nameとageには張りません。第6章で説明したParserの構文解析では、カラム名とデータ型（STRINGの場合は括弧内の長さ）を読み取っていました。本章では、その後ろに@<code>{INDEX}が続くかどうかを追加で確認します。

//emlist[INDEXキーワードを読み取る処理][java]{
int length = 0;
boolean indexed = false;

// STRING型の場合は括弧内の長さを読み取る（第6章と同じ）
if (index < tokens.size() && tokens.get(index).equals("(")) {
  // ...
}

// データ型（と長さ）の後ろにINDEXがあればインデックス対象にする
if (index < tokens.size()
    && equalsIgnoreCase(tokens.get(index), "index")) {
  indexed = true;
  index++;
}

columns.add(new Schema.Column(columnName, type, length, indexed));
//}

@<code>{INDEX}があれば@<code>{indexed}をtrueにし、そのカラムをインデックス対象として@<code>{Schema.Column}へ渡します。Parserが行うのはここまでで、実際にインデックスを構築するのはCatalogとTableです。ParserとStatement、Schemaの役割分担は第6章「ParserによるSQLの構文解析」で説明したとおりです。

=== Schemaへのindexedフラグの追加

第6章の@<code>{Schema.Column}は、カラム名、データ型、文字列の最大長の3つを保持していました。本章では、そのカラムがインデックス対象かどうかを表す@<code>{indexed}を追加します。

//emlist[indexedフラグを持つColumn][java]{
public record Column(String name, DataType type, int length, boolean indexed) {
  public int size() {
    return switch (type) {
      case INTEGER -> 4;
      case FLOAT -> 4;
      case DOUBLE -> 8;
      case STRING -> 4 + length;
    };
  }

  public boolean isIndexed() {
    return indexed;
  }
}
//}

@<code>{indexed}はレコードのバイト数（@<code>{size()}）には影響しません。インデックスの有無は、ディスク上のレコード形式ではなく、テーブルをどう検索するかに関わる情報だからです。レコードの直列化については第6章「Table：テーブルファイルの操作」と、固定長レコードの設計を説明した第4章「固定長スロットによるレコードの設計」を参照してください。

=== カタログへのインデックス定義の保存

どのカラムにインデックスを張るかは、テーブル定義の一部です。第6章では、テーブル定義を@<code>{catalog.txt}へ保存し、起動時に読み込んでSchemaを復元していました。本章では、この定義に@<code>{indexed}フラグを追加します。

第6章のcatalog.txtでは、1カラムを「名前:型:長さ」の3項目で表していました。本章では、末尾にインデックスの有無（1または0）を加えた4項目にします。

//cmd{
users|id:INTEGER:0:1,name:STRING:20:0,age:INTEGER:0:0
//}

この行は、idがインデックス対象（末尾が1）で、nameとageは対象外（末尾が0）であることを表します。Catalogは書き出し時にこの4項目を出力し、読み込み時に4項目目を@<code>{indexed}として解釈します。

//emlist[インデックスの有無を含めて1カラムを書き出す][java]{
String columnText =
    column.name()
        + ":"
        + column.type().name()
        + ":"
        + column.length()
        + ":"
        + (column.isIndexed() ? "1" : "0");
//}

読み込み側では、4項目目が@<code>{1}のときだけインデックス対象とみなします。第6章の3項目形式との互換のため、項目が3つの行も受け付け、その場合はインデックスなしとして扱います。

//emlist[カタログの行からindexedを復元する][java]{
boolean indexed = columnParts.length == 4 && columnParts[3].equals("1");
columns.add(new Schema.Column(columnName, type, length, indexed));
//}

これで、テーブルを再作成しなくても、起動のたびに「どのカラムにインデックスを張るか」が復元されます。インデックスそのもの（B-Treeの中身）はカタログには保存しません。次節で述べるとおり、起動時にテーブルを走査して作り直します。

== インデックスの構築と保持

インデックスの実体は、キーから行を検索するためのB-Treeです。本章では、この構造を@<code>{Index}クラスとして用意し、Tableがカラムごとに保持します。

@<code>{Index}は、第5章で実装したB-Treeを移植したものです。B-Treeのノード構造、検索、挿入時のノード分割、削除時のバランス調整（BorrowとMerge）は、第5章「B-Treeの実装」で説明しています。本章ではこれらを再説明せず、Tableからインデックスをどう使うかに注目します。

第5章のB-Treeがキーに対してRecordIdを1つ保持していたのに対し、本章の@<code>{Index}はキーに対して行（Row）の一覧を保持します。同じキーを持つ行が複数あっても対応できるよう、値を@<code>{List<Row>}にしています。

//emlist[Indexが公開する操作][java]{
public class Index {
  private final BTree btree;

  public void add(Object keyObj, Row row) { /* キーと行を登録する */ }

  public List<Row> search(Object keyObj) { /* キーに一致する行を返す */ }

  public void remove(Object keyObj, Row row) { /* 登録を取り消す */ }
}
//}

現在の@<code>{Index}が扱うキーは整数だけです。@<code>{add()}と@<code>{search()}は、渡された値を整数へ変換できる場合のみ処理し、変換できない場合は登録や検索を行いません。この制限は、後述するプランナーがインデックス検索を選ぶ条件（INTEGER型のカラム）と対応しています。

//emlist[整数キーだけを扱うadd][java]{
public void add(Object keyObj, Row row) {
  if (keyObj == null) return;

  int key;
  if (keyObj instanceof Number number) {
    key = number.intValue();
  } else {
    try {
      key = Integer.parseInt(keyObj.toString());
    } catch (NumberFormatException e) {
      return; // 現状は整数キーのみサポート
    }
  }

  btree.insert(key, row);
}
//}

=== TableによるIndexの保持

第6章のTableは、Schemaと@<code>{RandomAccessFile}を保持し、テーブルファイルを読み書きしていました。本章のTableは、これに加えてカラム名ごとのIndexを保持します。

//emlist[Tableが持つインデックスのマップ][java]{
private final Map<String, Index> indexes = new HashMap<>();
//}

キーはインデックスを張ったカラム名、値はそのカラムのIndexです。前述の@<code>{users}テーブルであれば、@<code>{indexes}はidに対応するIndexを1つ持ちます。

=== 起動時のインデックス構築

インデックスの中身はカタログに保存しないため、Tableを開くたびに作り直します。Tableのコンストラクタは、Schemaを確認して@<code>{indexed}なカラムのIndexを用意し、既存の行をすべて走査して登録します。

//emlist[インデックス対象のカラムに空のIndexを作り、既存行を登録する][java]{
private void buildIndexes() throws IOException {
  for (Schema.Column column : schema.getColumns()) {
    if (column.isIndexed()) {
      indexes.put(column.name(), new Index());
    }
  }

  if (indexes.isEmpty()) return;

  for (Row row : scan()) {
    for (String col : indexes.keySet()) {
      Index idx = indexes.get(col);
      idx.add(row.get(col), row);
    }
  }
}
//}

インデックスを張ったカラムがなければ、@<code>{indexes}は空のままで、走査も行いません。この、起動時に保存済みのデータからB-Treeを組み立て直す方針は、第5章「起動時のB-Tree構築と空き領域管理」と「B-Treeの再構築（buildTree）」で扱ったものと同じです。本章では、テーブルごとに、インデックスを張ったカラムだけを対象にこの構築を行います。

=== インデックスの更新

インデックスは、テーブルの行と一致していなければ検索に使えません。そのため、行が変化する操作では、ディスクへの書き込みと合わせてインデックスも更新します。

たとえばINSERTでは、行をページへ書き込んだ後に、各インデックスへその行を登録します。

//emlist[insert時にインデックスへ行を登録する][java]{
// レコードをページへ書き込む（第4章・第6章と同じ）
file.seek((long) targetPage * PAGE_SIZE);
file.write(page);

// インデックスに追加
for (Map.Entry<String, Index> e : indexes.entrySet()) {
  String col = e.getKey();
  Index idx = e.getValue();
  idx.add(row.get(col), row);
}
//}

UPDATEでは、更新前の行をインデックスから取り除き、更新後の行を登録します。DELETEでは、削除する行をインデックスから取り除きます。ページへの書き込み（同じスロットへの上書きや、使用フラグによる論理削除）は、第4章「更新（update）」「削除（delete）」で説明した方式のままです。本章では、その前後でインデックスの登録内容を行の状態に合わせます。

インデックスを張っていないテーブルでは@<code>{indexes}が空なので、これらのループは何もしません。したがって、INSERT・UPDATE・DELETE自体の処理は第6章から変わりません。

=== インデックスによる検索

インデックスからキーで行を取り出すには、@<code>{searchByIndex()}を使います。カラム名でIndexを引き、そのIndexに対してキーで検索します。

//emlist[カラム名でIndexを引いて検索する][java]{
public List<Row> searchByIndex(String column, Object value) {
  Index idx = indexes.get(column);

  if (idx == null) {
    throw new IllegalStateException("No index found for column: " + column);
  }

  return idx.search(value);
}
//}

指定したカラムにインデックスがなければ例外になります。後述するプランナーは、インデックスを持つカラムに対してのみインデックス検索を選ぶため、通常この経路では例外になりません。

== Plannerによる実行計画の作成

@<code>{Planner.createPlan()}は、Statementとテーブル定義を受け取り、@<code>{SeqScanPlan}または@<code>{IndexScanPlan}を返します。判断の材料は、WHERE条件と、条件の左辺が指すカラムの定義です。

//emlist[条件を確認してプランを選ぶcreatePlan][java]{
public Plan createPlan(Statement.Select statement, Schema schema) {
  Statement.Condition condition = statement.whereCondition();

  // WHERE句がない場合は全件走査
  if (condition == null) {
    return new SeqScanPlan(statement.tableName(), null);
  }

  Schema.Column column = schema.getColumn(condition.left());

  if (column != null
      && column.isIndexed()
      && column.type() == Schema.DataType.INTEGER
      && condition.operator().equals("=")) {
    return new IndexScanPlan(statement.tableName(), condition);
  }

  // それ以外は全件走査
  return new SeqScanPlan(statement.tableName(), condition);
}
//}

@<code>{IndexScanPlan}を選ぶのは、次の条件をすべて満たすときだけです。

 * WHERE句がある
 * 条件の左辺が、テーブルに存在するカラムである
 * そのカラムにインデックスが張られている
 * そのカラムのデータ型がINTEGERである
 * 比較演算子が@<code>{=}である

1つでも満たさない場合は@<code>{SeqScanPlan}を選びます。たとえば、WHERE句がない、インデックスのないカラムを条件にした、@<code>{>}や@<code>{<}などの範囲を指定した、といった場合です。データ型と演算子を限定しているのは、本章のIndexが整数キーの完全一致だけを扱うためです。範囲検索へインデックスを使う拡張は本章では行いません。

プランナーは条件とスキーマを見るだけで、実際にテーブルを読みません。この時点では「どちらの方法で実行するか」を決めるだけであり、行の取得はプランの実行時に行います。

== 実行計画の実行

=== PlanとExecutionContextの分離

@<code>{SeqScanPlan}と@<code>{IndexScanPlan}は、共通のインターフェース@<code>{Plan}を実装します。Planは、実行を要求する@<code>{execute()}を1つ持ちます。

//emlist[Planインターフェース][java]{
public interface Plan {
  void execute(ExecutionContext context, Statement.Select statement)
      throws IOException;
}
//}

@<code>{execute()}は、渡された@<code>{ExecutionContext}に対して、自分に対応する処理を呼び出します。ExecutionContextは、全件走査とインデックス検索の2つの操作を持つインターフェースです。

//emlist[ExecutionContextインターフェース][java]{
public interface ExecutionContext {
  void executeSeqScan(Statement.Select statement) throws IOException;

  void executeIndexScan(Statement.Select statement) throws IOException;
}
//}

@<code>{SeqScanPlan}は@<code>{executeSeqScan()}を、@<code>{IndexScanPlan}は@<code>{executeIndexScan()}を呼び出します。

//emlist[SeqScanPlanのexecute][java]{
@Override
public void execute(ExecutionContext context, Statement.Select statement)
    throws IOException {
  System.out.println("SeqScan : " + getTableName());
  context.executeSeqScan(statement);
}
//}

//emlist[IndexScanPlanのexecute][java]{
@Override
public void execute(ExecutionContext context, Statement.Select statement)
    throws IOException {
  System.out.println("IndexScan : " + getTableName());
  context.executeIndexScan(statement);
}
//}

先頭の@<code>{System.out.println()}は、どちらのプランで実行したかを画面に表示します。この出力によって、プランナーの選択を実行結果から確認できます。

@<code>{ExecutionContext}を実装するのは@<code>{QueryExecutor}です。プランは実行方法の種類だけを表し、テーブルの読み書きを伴う具体的な処理はQueryExecutorが持ちます。この分け方には次の利点があります。

 * プランは、どの検索方法を選んだかという情報だけを持つため、単純に保てます。
 * 検索方法を増やすときは、Planの実装とExecutionContextの操作を追加します。
 * QueryExecutorは、Statementの種類ごとの実行手順（第6章「QueryExecutorによるStatementの実行」）に、プラン単位の実行手順を加えるだけで済みます。

=== executeSeqScanとexecuteIndexScan

@<code>{executeSeqScan()}は、第6章のSELECTと同じ処理です。テーブルを全件走査し、JOINがあれば結合し、WHERE条件で絞り込み、SELECT句のカラムを射影して表示します。JOINやWHERE、射影の詳細は第6章「SELECTとJOIN」を参照してください。

//emlist[executeSeqScan（要点）][java]{
Table leftTable = catalog.requireTable(statement.tableName());

List<Row> rows = new ArrayList<>();
for (Row row : leftTable.scan()) {
  // JOINがある場合はテーブル名を付与する
  // ...
  rows.add(row);
}

// JOIN・WHEREを適用して表示する
//}

@<code>{executeIndexScan()}は、全件走査の代わりに@<code>{searchByIndex()}でキーに一致する行だけを取り出します。まず、条件の右辺をカラムの型に合わせて変換し、左辺のカラムに対してインデックス検索を行います。

//emlist[executeIndexScan（要点）][java]{
Statement.Condition condition = statement.whereCondition();

Schema.Column column = schema.getColumn(condition.left());
Object value = condition.right();

if (column != null) {
  value = parseValue(condition.right(), column);
}

List<Row> rows = table.searchByIndex(condition.left(), value);

// 取り出した行に対して、JOIN・WHEREを適用して表示する
//}

インデックス検索で取り出した後も、全件走査と同じ経路でJOINとWHERE条件の評価を行い、結果を表示します。取り出した行はキーに一致するものだけですが、出力処理を両者で共通化するため、同じ絞り込みと射影を通します。

両者の違いは、@<b>{最初に何件の行を読み込むか}です。全件走査はテーブルのすべての行を読み込みますが、インデックス検索はキーに一致する行だけを読み込みます。件数が多いテーブルでidのような一意なキーを指定した場合、この差が実行時間に現れます。

== 実行例

ここでは、インデックスを張ったテーブルを作り、プランナーがどちらのプランを選ぶかを確認します。実行時間は環境によって異なるため、以下の出力例では@<code>{(Executed in ... ms)}を省略します。

=== インデックスを定義したテーブルの作成

idにインデックスを張った@<code>{users}テーブルを作成し、3件の行を追加します。

//cmd{
db > CREATE TABLE users (id INTEGER INDEX, name STRING(20), age INTEGER);
Table created: users

db > INSERT INTO users (id, name, age) VALUES (1, 'Taro', 20);
Inserted into users: {id=1, name=Taro, age=20}

db > INSERT INTO users (id, name, age) VALUES (2, 'Hanako', 18);
Inserted into users: {id=2, name=Hanako, age=18}

db > INSERT INTO users (id, name, age) VALUES (3, 'Jiro', 25);
Inserted into users: {id=3, name=Jiro, age=25}
//}

INSERTの表示は第6章と同じです。行がページへ保存されると同時に、idのインデックスへも登録されますが、この登録は画面には表示されません。

=== 実行計画の確認

idを@<code>{=}で指定してSELECTを実行します。idはINTEGER型でインデックスが張られており、演算子も@<code>{=}なので、プランナーはインデックス検索を選びます。プランの実行時に表示される@<code>{IndexScan : users}が、その選択を示します。

//cmd{
db > SELECT * FROM users WHERE id = 2;
IndexScan : users
{id=2, name=Hanako, age=18}
//}

取得列を絞っても、検索方法の選択は変わりません。SELECT句のカラムは、行を取り出した後の射影で決まるためです。

//cmd{
db > SELECT name, age FROM users WHERE id = 2;
IndexScan : users
{name=Hanako, age=18}
//}

=== 全件走査との比較

インデックスのないageを条件にすると、プランナーは全件走査を選びます。表示は@<code>{SeqScan : users}に変わります。

//cmd{
db > SELECT * FROM users WHERE age = 25;
SeqScan : users
{id=3, name=Jiro, age=25}
//}

同じidが条件でも、演算子が@<code>{=}でなければインデックス検索は選ばれません。次の例は範囲を指定しているため、全件走査になります。

//cmd{
db > SELECT * FROM users WHERE id > 1;
SeqScan : users
{id=2, name=Hanako, age=18}
{id=3, name=Jiro, age=25}
//}

WHERE句のないSELECTも全件走査です。

//cmd{
db > SELECT * FROM users;
SeqScan : users
{id=1, name=Taro, age=20}
{id=2, name=Hanako, age=18}
{id=3, name=Jiro, age=25}
//}

これらの出力から、同じSELECTでも条件によって実行方法が切り替わることを確認できます。インデックスの中身はカタログに保存しないため、データベースを再起動した後でも、起動時にテーブルを走査してインデックスが作り直され、@<code>{id = 2}のような検索は再びインデックス検索になります。

第5章「実行速度の比較：どれくらい速くなったのか」で確認したように、インデックス検索と全件走査の差は件数が増えるほど大きくなります。本章で追加したプランの表示と、第4章「処理時間の計測」で導入した実行時間の表示を使えば、件数を増やしながら、どちらのプランがどれだけの時間で実行されたかを確認できます。

== まとめと次章への課題

本章では、SELECTの実行方法を条件に応じて選ぶ仕組みを追加しました。

 * カラム定義に@<b>{INDEX}を指定できるようにし、その有無をカタログへ保存するようにした。
 * テーブルごとに、インデックスを張ったカラムのB-Treeを起動時に構築し、INSERT・UPDATE・DELETEに合わせて更新するようにした。
 * @<b>{Planner}がStatementとSchemaから@<b>{実行計画}を作り、インデックス検索と全件走査を選び分けるようにした。
 * 実行計画（@<code>{Plan}）と、実際の処理（@<code>{QueryExecutor}）を@<code>{ExecutionContext}で分離した。

一方で、本章のSELECTには実行方法の選択とは別の課題が残っています。@<code>{executeSeqScan()}と@<code>{executeIndexScan()}は、取り出した行をいったんすべて@<code>{List<Row>}へ集め、そのリストを結合し、さらに絞り込んだ結果を別のリストへ移しています。JOINも、左右の行をすべて展開してから突き合わせるため、途中の行がまとめてメモリ上に並びます。行数が多い場合、この方式では処理の途中で大量の行を同時に保持することになります。

また、本章のプランは全件走査かインデックス検索かという1回の選択にとどまり、WHEREによる絞り込みやSELECT句の射影は、選ばれた後の共通処理として固定されています。より複雑なSQLを組み立てるには、走査・絞り込み・射影・結合といった処理を、それぞれ独立した部品として組み合わせられるようにしたいところです。

次章では、これらの処理を@<b>{演算子（Operator）}という単位に分け、1行ずつ順に取り出しながら処理を進める反復モデルを導入します。演算子を積み重ねて実行計画を組み立てることで、行をまとめて保持せずにSQLを実行できるようにしていきます。
