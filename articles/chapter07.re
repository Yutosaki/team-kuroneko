= 第7章 プランナーによる実行計画の選択

== 第6章までの課題：常に全件走査するSELECT

第6章で実装したSELECT文は、FROM句のテーブルに対して@<code>{Table.scan()}を呼び出し、全行をディスクから読み出してからWHERE条件で絞り込んでいます。この方式では、条件指定の有無や内容に関わらず常に全件走査（フルスキャン）となります。

第5章ではB-Treeによる検索の高速化を実装しましたが、第6章のテーブル構造には組み込まれていません。全件走査の処理時間はレコードの件数に比例するため、データ量が増加するとパフォーマンスが悪化します。

インデックスによる検索を適切に行うには、以下の判断が必要です。
 * WHERE句が指定されているか
 * 条件の左辺に対応するカラムが存在するか
 * そのカラムにインデックスが設定されているか
 * 比較演算子がインデックスで処理可能か

これらを評価し、インデックス検索と全件走査のどちらで実行するかを決定する役割が@<b>{プランナー（Planner）}です。本章では、SQLの解析結果（Statement）とテーブル定義（Schema）から@<b>{実行計画（Execution Plan）}を生成し、最適な実行方法を選択する仕組みを実装します。

== 実行計画とプランナーの導入

方針として、実行方法を決定する「プランナー」と、実際のデータ読み書きを担う「エクゼキュータ」を分離します。検索方法が増加した際のシステムの複雑化を防ぐためです。

本章では、全件走査を表す@<code>{SeqScanPlan}と、インデックス検索を表す@<code>{IndexScanPlan}の2種類の実行計画（プラン）を用意します。システム全体の流れは以下のようになります。

//cmd{
Statement.Select ＋ Schema
        │
        ▼
Planner.createPlan()
        │
   ┌────┴────┐
   ▼         ▼
IndexScanPlan  SeqScanPlan
        │
   └────┬────┘
        ▼
Plan.execute(context, statement)
        │
        ▼
QueryExecutor
        │
        ▼
   結果を表示
//}

第6章では@<code>{QueryExecutor.executeSelect()}が直接テーブルを走査していましたが、本章からはプランナーにプランを作成させ、そのプランを実行する流れに変更します。

//emlist[プランを作って実行するexecuteSelect][java]{
private void executeSelect(Statement.Select statement) throws IOException {
  Schema schema = catalog.requireSchema(statement.tableName());
  Plan plan = planner.createPlan(statement, schema);
  plan.execute(this, statement);
}
//}

== カラムへのインデックス定義とカタログ保存

プランナーがインデックス検索を選択できるように、テーブル定義にインデックスの有無を含めます。

@<code>{CREATE TABLE}文のカラム定義に@<code>{INDEX}キーワードを追加可能にし、Parserの構文解析処理を拡張します。

//emlist[INDEXキーワードを読み取る処理][java]{
int length = 0;
boolean indexed = false;

// STRING型の場合は括弧内の長さを読み取る（第6章と同じ）
if (index < tokens.size() && tokens.get(index).equals("(")) {
  // ...
}

// データ型の後ろにINDEXがあれば対象とする
if (index < tokens.size() && equalsIgnoreCase(tokens.get(index), "index")) {
  indexed = true;
  index++;
}

columns.add(new Schema.Column(columnName, type, length, indexed));
//}

これに合わせて、@<code>{Schema.Column}に@<code>{indexed}フラグを追加します。インデックスの有無は検索方法の決定に使用されるため、ディスク上のレコードサイズには影響しません。

テーブル定義を永続化する@<code>{catalog.txt}の保存フォーマットも変更します。末尾にインデックスの有無（1または0）を追加した4項目とします。

//cmd{
users|id:INTEGER:0:1,name:STRING:20:0,age:INTEGER:0:0
//}

== インデックスの構築と同期処理

インデックスは、キーから行を検索するためのB-Treeをラップした@<code>{Index}クラスとして実装し、@<code>{Table}クラスごとに保持します。同一キーの複数行に対応するため、1つのキーに対して@<code>{List<Row>}を保持する構造とします。

//emlist[Indexが公開する操作][java]{
public class Index {
  private final BTree btree;

  public void add(Object keyObj, Row row) { /* キーと行を登録する */ }
  public List<Row> search(Object keyObj) { /* キーに一致する行を返す */ }
  public void remove(Object keyObj, Row row) { /* 登録を取り消す */ }
}
//}

インデックスのデータはディスクに保存せず、テーブルの起動時に実データを走査してメモリ上に再構築します。

//emlist[起動時にIndexを作り、既存行を登録する][java]{
private final Map<String, Index> indexes = new HashMap<>();

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

また、INSERT、UPDATE、DELETEによるデータ変更時には、ディスクへの書き込みと同時に対応する@<code>{Index}の更新処理を実行し、実データとの整合性を保ちます。

== Plannerによる実行方法の選択

プランナー（@<code>{Planner}）は、Statementとテーブル定義に基づき、@<code>{SeqScanPlan}か@<code>{IndexScanPlan}のどちらを実行するかを決定します。

//emlist[条件を確認してプランを選ぶcreatePlan][java]{
public Plan createPlan(Statement.Select statement, Schema schema) {
  Statement.Condition condition = statement.whereCondition();

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

  return new SeqScanPlan(statement.tableName(), condition);
}
//}

現在の実装では、@<code>{IndexScanPlan}が選択される条件は以下のすべてを満たす場合のみです。
 * 対象カラムが@<code>{INTEGER}型である
 * 対象カラムに@<code>{INDEX}が指定されている
 * 比較演算子が@<code>{=}である

範囲検索（@<code>{>}など）を含め、条件を満たさない場合はすべて全件走査（@<code>{SeqScanPlan}）を選択します。

== 実行処理の分離

プランには決定された実行方法の情報のみを保持させ、実際の処理は@<code>{ExecutionContext}（本プログラムでは@<code>{QueryExecutor}）に委譲します。

//emlist[PlanとExecutionContextのインターフェース][java]{
public interface Plan {
  void execute(ExecutionContext context, Statement.Select statement) throws IOException;
}

public interface ExecutionContext {
  void executeSeqScan(Statement.Select statement) throws IOException;
  void executeIndexScan(Statement.Select statement) throws IOException;
}
//}

@<code>{QueryExecutor}側の@<code>{executeSeqScan()}は、従来通りテーブル全行を読み込みます。@<code>{executeIndexScan()}は、@<code>{table.searchByIndex()}を呼び出してインデックスから該当レコードのみを読み込みます。

//emlist[executeIndexScan（要点）][java]{
Statement.Condition condition = statement.whereCondition();
Schema.Column column = schema.getColumn(condition.left());

Object value = condition.right();
if (column != null) {
  value = parseValue(condition.right(), column);
}

List<Row> rows = table.searchByIndex(condition.left(), value);
// 取得した行に対してJOIN・WHEREを適用する
//}

== 実行例とプランの確認

idカラムにインデックスを設定したテーブルでの実行例を示します。

//cmd{
db > CREATE TABLE users (id INTEGER INDEX, name STRING(20), age INTEGER);
Table created: users

db > INSERT INTO users (id, name, age) VALUES (1, 'Taro', 20);
Inserted into users: {id=1, name=Taro, age=20}
db > INSERT INTO users (id, name, age) VALUES (2, 'Hanako', 18);
Inserted into users: {id=2, name=Hanako, age=18}
//}

idを完全一致で検索すると、インデックス検索が選択されます。

//cmd{
db > SELECT * FROM users WHERE id = 2;
IndexScan : users
{id=2, name=Hanako, age=18}
//}

インデックスのないカラムや、範囲検索を指定した場合は全件走査が選択されます。

//cmd{
db > SELECT * FROM users WHERE age = 18;
SeqScan : users
{id=2, name=Hanako, age=18}

db > SELECT * FROM users WHERE id > 1;
SeqScan : users
{id=2, name=Hanako, age=18}
//}

== まとめと次章への課題

本章では以下の機能を実装しました。
 * @<b>{INDEX}定義のカタログへの永続化。
 * テーブルごとのB-Treeインデックス管理とデータ同期。
 * @<b>{Planner}によるStatementとSchemaに基づく実行計画の決定。
 * プラン（@<code>{Plan}）と処理（@<code>{ExecutionContext}）の分離。

現在の実装では、抽出したレコードをすべて@<code>{List<Row>}に保持してから絞り込みや結合を行っています。データ量が増加するとメモリを過大に消費し、メモリ枯渇（Out Of Memory）が発生する課題が残っています。

次章（第8章）では、処理単位を@<b>{演算子（Operator）}として分離し、1行ずつデータを処理するイテレータモデルを導入します。これにより、メモリ消費を抑えた効率的なクエリ実行アーキテクチャへと改修します。
