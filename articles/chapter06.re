#@# 執筆方針
#@# - です・ます調で統一します。
#@# - 客観的な視点で説明し、根拠のない評価や誇張表現は使用しません。
#@#
#@# = テーブル定義とSQLの実行
#@# == 第5章までの課題：カラムの制限
#@# == SQL処理の全体像
#@# == テーブルの仕様
#@# === Table：テーブルファイルの操作
#@# === Schema：カラム構成とレコード形式の管理
#@# === Column：カラム名、データ型、サイズの定義
#@# === Row：一行分のデータの表現
#@# === Catalog：テーブル定義とTableの管理
#@# == ParserによるSQLの構文解析
#@# === Tokenizerによる字句解析
#@# === SimpleParserによる構文解析
#@# === StatementによるASTの表現
#@# === CRUD文の解析
#@# === ConditionとJoinClause
#@# == QueryExecutorによるStatementの実行
#@# === CREATE TABLEとINSERT
#@# === SELECTとJOIN
#@# === UPDATEとDELETE
#@# == 実行結果の表示
#@# == 本章に残る課題：実行方法を選択するプランナー

= テーブル定義とSQLの実行

== 第5章までの課題：テーブルの拡張性

第5章「B-Treeを用いたCRUDの高速化」では、ページ内のレコードをB-Treeから検索する方法を扱いました。一方、第5章までのテーブルは、@<code>{id}と@<code>{name}という決められたカラムを前提としていました。別のカラムを追加したり、整数や文字列以外のデータ型を使用したりするには、プログラム自体を変更する必要があります。

一般的なリレーショナルデータベースでは、テーブルごとにカラムの構成を定義します。たとえば、利用者を保存する@<code>{users}テーブルと、注文を保存する@<code>{orders}テーブルでは、必要なカラムが異なります。

//emlist[構成の異なる二つのテーブル][sql]{
CREATE TABLE users (
  id INTEGER,
  name STRING(20),
  age INTEGER
);

CREATE TABLE orders (
  id INTEGER,
  user_id INTEGER,
  amount DOUBLE
);
//}

このSQLを扱うには、次の仕組みが必要です。

 * SQLからテーブル名、カラム名、データ型などを読み取る仕組み
 * 読み取ったテーブル定義をプログラム上で保持する仕組み
 * テーブル定義に従って値をバイト列へ変換する仕組み
 * SQLの種類に応じてテーブルを操作する仕組み

本章では、これらをParser、Statement、Schema、Catalog、Table、QueryExecutorに分けて実装します。これにより、SQL文字列の解析と、ページファイルに対する読み書きを分離します。

== SQL処理の全体像
SQLの解析からデータの読み書きまでを一つの処理にまとめると、各クラスの役割や処理の境界が分かりにくくなります。
そこで本節では、SQLを入力してから結果を表示するまでの流れを示し、後続の節で実装する各要素の役割を整理します。

//cmd{
SQL文字列
    │
    ▼
Tokenizer
文字列をトークンへ分割
    │
    ▼
SimpleParser
トークン列を構文解析
    │
    ▼
Statement（AST）
SQLの内容をJavaの値として保持
    │
    ▼
QueryExecutor
Statementの種類に応じて処理を選択
    │
    ▼
Catalog ── Schema
    │         テーブルの構造を提供
    ▼
Table ─── Row
ページファイルを読み書き
    │
    ▼
実行結果または更新件数を表示
//}

@<code>{nekoDB.start()}は、REPLで一行ずつSQLを受け取ります。REPLの入力ループとコマンド実行の基本構造は、第2章「対話型プログラム（REPL）の作成」と「入力とコマンドの扱い」で説明しています。本章では入力を空白で分割して直接実行するのではなく、@<code>{SimpleParser.parseStatement()}へ渡し、解析結果の@<code>{Statement}を@<code>{QueryExecutor.execute()}へ渡します。

//emlist[SQLを解析して実行する処理][java]{
Statement statement = parser.parseStatement(sql);
executor.execute(statement);
//}

この時点で、SQL文字列を扱う責務はParser側に限定されます。QueryExecutor以降のクラスは、SQLの空白や括弧の位置ではなく、Statementが保持しているテーブル名、カラム名、値、条件を参照します。

== テーブルの仕様
任意のカラムを持つテーブルを扱うには、テーブルの定義と実際の行データを、固定の形式に依存せず表現する必要があります。
この節では、必要な情報をSchema、Column、Row、Table、Catalogに分け、それぞれが一つの役割を担う方針でテーブルの構造を設計します


//cmd{
Catalog
├── テーブル名 → Schema
└── テーブル名 → Table

Schema
└── List<Column>

Table
└── .tblファイル

Row
└── カラム名 → 値
//}

=== Table：テーブルファイルの操作

@<code>{Table}は、Schemaと@<code>{RandomAccessFile}を保持し、テーブルごとの@<code>{.tbl}ファイルを操作します。RandomAccessFileは、ファイル内の任意の位置へ移動して読み書きできるJavaのクラスです。@<fn>{random-access-file}4096バイトのページ、固定長スロット、使用フラグによるレコード管理については、第4章「ページという単位の導入」と「固定長スロットによるレコードの設計」で説明しています。本節では、それらの保存方式を繰り返さず、汎用的なテーブルを扱うためにTableへ追加されたインターフェースを確認します。

//footnote[random-access-file][Oracle「RandomAccessFile」：@<href>{https://docs.oracle.com/javase/jp/8/docs/api/java/io/RandomAccessFile.html}]

Tableが提供する主な操作は次のとおりです。

 * @<code>{insert(Row)}：空きスロットへ行を保存します。
 * @<code>{scan()}：有効なすべての行を返します。
 * @<code>{scanRecords()}：行とRecordIdの組を返します。
 * @<code>{update(RecordId, Row)}：指定されたスロットを上書きします。
 * @<code>{delete(RecordId)}：指定されたスロットの使用フラグを0にします。

RecordIdはページ番号とスロット番号を保持します。この位置情報の目的とB-Treeとの関係は、第5章「B-Treeの実装：RecordIdの導入」で説明しています。本章の変更点は、RowとRecordIdを一緒に返すRecordをTable内に定義し、UPDATEとDELETEから利用できるようにしたことです。

//emlist[Tableが返すRecordIdとRecord][java]{
public record RecordId(int pageNo, int slotNo) {}

public record Record(RecordId recordId, Row row) {}
//}

recordは、値を保持するためのクラスを簡潔に宣言できるJavaの機能です。Java 16から正式に導入されました。@<fn>{java-records}

//footnote[java-records][Oracle「レコード・クラス」：@<href>{https://docs.oracle.com/javase/jp/15/language/records.html}]

レコード先頭の使用フラグと、フラグを使った論理削除については、第4章「削除（delete）」を参照してください。本章のTableも同じ方式を使用します。

=== Schema：カラム構成とレコード形式の管理

@<code>{Schema}は、テーブル名とColumnの一覧を保持します。TableはSchemaを参照することで、カラムの順序と各値のバイト数を判断できます。

//emlist[Schemaの主要なフィールド][java]{
public class Schema {
  private final String tableName;
  private final List<Column> columns;
  private static final int RECORD_HEADER_SIZE = 1;
  // ...
}
//}

固定長レコードからページ内のスロット数を求める考え方は、第4章「固定長スロットによるレコードの設計」で説明しています。第4章ではレコード形式が固定されていましたが、本章ではSchemaがColumnの一覧からレコードサイズを計算します。

//emlist[レコードサイズとスロット数の計算][java]{
public int getRecordSize() {
  int size = RECORD_HEADER_SIZE;

  for (Column column : columns) {
    size += column.size();
  }

  return size;
}

public int getMaxSlots(int pageSize) {
  return pageSize / getRecordSize();
}
//}

=== Column：カラム名、データ型、サイズの定義

ColumnはSchema内のrecordとして定義され、カラム名、データ型、文字列の最大長を保持します。

//emlist[ColumnとDataType][java]{
public record Column(String name, DataType type, int length) {
  public int size() {
    return switch (type) {
      case INTEGER -> 4;
      case FLOAT -> 4;
      case DOUBLE -> 8;
      case STRING -> 4 + length;
    };
  }
}

public enum DataType {
  INTEGER,
  STRING,
  FLOAT,
  DOUBLE
}
//}

STRINGは、実際に保存した文字列の長さを示す4バイトと、定義時に指定した最大長の領域を使用します。たとえば@<code>{STRING(20)}のサイズは24バイトです。

=== Row：一行分のデータの表現

@<code>{Row}は、カラム名と値の対応を@<code>{LinkedHashMap<String, Object>}で保持します。LinkedHashMapは、キーと値の対応を管理しながら、要素が追加された順序も維持するMap実装です。@<fn>{linked-hash-map}この性質により、Rowへ値を追加した順序でカラムを取り出せます。

//footnote[linked-hash-map][Oracle「LinkedHashMap」：@<href>{https://docs.oracle.com/javase/jp/8/docs/api/java/util/LinkedHashMap.html}]

//emlist[Rowへの値の保存と取得][java]{
public void put(String columnName, Object value) {
  values.put(columnName, value);
}

public Object get(String columnName) {
  return values.get(columnName);
}
//}

Rowの値はJava上ではObjectですが、ファイルへ保存するときはSchema.Columnの型に従ってINTEGER、FLOAT、DOUBLE、STRINGのバイト列へ変換されます。Java上の値を保存形式へ変換し、読み込み時に復元する基本的な考え方は、第3章「データ形式の設計」で説明しています。本章では、固定されたキーと値ではなく、Schemaのカラム順とデータ型を使って変換する点が異なります。

=== Catalog：テーブル定義とTableの管理

@<code>{Catalog}は、正規化したテーブル名をキーとしてSchemaとTableを管理します。

//emlist[Catalogが管理するマップ][java]{
private final Map<String, Schema> schemas = new HashMap<>();
private final Map<String, Table> tables = new HashMap<>();
//}

CREATE TABLEを実行すると、Catalogは新しいTableを作成し、SchemaとTableを各マップへ登録します。Schemaの内容は@<code>{catalog.txt}へ保存され、行データはテーブル名に対応する@<code>{.tbl}ファイルへ保存されます。

//cmd{
data/
├── catalog.txt   テーブル定義
├── users.tbl     usersの行データ
└── orders.tbl    ordersの行データ
//}

catalog.txtの一行は、次のような形式です。

//cmd{
users|id:INTEGER:0,name:STRING:20,age:INTEGER:0
//}

Catalogは起動時にcatalog.txtを読み、SchemaとTableを復元します。ファイルへの保存と起動時の復元による永続化については、第3章「なぜ永続化が必要なのか」と「ファイルへの保存」で説明しています。本章で新しく扱うのは、行データに加えてテーブル定義も永続化する点です。

== ParserによるSQLの構文解析
入力されたSQL文字列のままでは、実行処理がテーブル名や条件を安全に参照できません。
本節では、SQLをTokenizerでトークンへ分割し、SimpleParserで構文を読み取り、実行に必要な情報をStatementとして表現する方針を採ります。

=== 字句解析（Tokenizer）

TokenizerはSQL文字列を、キーワード、識別子、リテラル、括弧、カンマ、比較演算子などのトークンへ分割します。SimpleParserがSQLを一文字ずつ扱わずに済むように、意味のある最小単位へ切り分ける処理です。

//emlist[字句解析の対象となるSQL][sql]{
SELECT users.name
FROM users
WHERE users.age >= 20;
//}

このSQLは概念的に次のトークン列へ変換されます。終端のセミコロンはトークンへ追加されません。

//cmd{
[SELECT, users.name, FROM, users, WHERE, users.age, >=, 20]
//}

Tokenizerはシングルクォートの内側を一つの文字列として扱います。そのため、@<code>{'Taro Yamada'}の途中に空白があっても分割されません。また、@<code>{>=}、@<code>{<=}、@<code>{!=}は二文字で一つの演算子になります。閉じていない文字列リテラルはエラーになります。

@<code>{tokenize()}は、入力文字列を先頭から一文字ずつ走査します。@<code>{currentToken}は現在組み立てているトークン、@<code>{inString}は現在位置が文字列リテラルの内側かどうかを表します。

//emlist[Tokenizerの走査処理][java]{
List<String> tokens = new ArrayList<>();
StringBuilder currentToken = new StringBuilder();
boolean inString = false;

for (int i = 0; i < sql.length(); i++) {
  char c = sql.charAt(i);

  if (c == '\'') {
    inString = !inString;
    currentToken.append(c);
    continue;
  }

  if (inString) {
    currentToken.append(c);
    continue;
  }

  // 文字列の外側にある文字を種類ごとに処理する
  // ...
}
//}

シングルクォートを見つけるたびに@<code>{inString}を反転します。inStringがtrueの間は、空白、カンマ、括弧も区切りとして扱わずcurrentTokenへ追加します。たとえば@<code>{'Taro Yamada'}はクォートを含む一つのトークンになります。

文字列の外側では、文字の種類に応じて次のように処理します。

//emlist[区切り文字と演算子の処理][java]{
if (Character.isWhitespace(c)) {
  flush(currentToken, tokens);
} else if (c == '(' || c == ')' || c == ',') {
  flush(currentToken, tokens);
  tokens.add(String.valueOf(c));
} else if (c == ';') {
  flush(currentToken, tokens);
} else if (c == '=' || c == '<' || c == '>' || c == '!') {
  flush(currentToken, tokens);

  if (i + 1 < sql.length() && sql.charAt(i + 1) == '=') {
    tokens.add(String.valueOf(c) + "=");
    i++;
  } else {
    tokens.add(String.valueOf(c));
  }
} else {
  currentToken.append(c);
}
//}

空白または記号へ到達すると、そこまでcurrentTokenへ蓄積した文字を@<code>{flush()}でtokensへ移します。括弧とカンマは構文解析で必要になるため、それ自体もトークンとして追加します。セミコロンはSQLの終端を示すだけなので追加しません。

比較演算子では、現在の文字の次が@<code>{=}かを確認します。次も演算子の一部なら二文字を結合し、走査位置@<code>{i}を一つ進めます。この処理により、@<code>{>}と@<code>{=}ではなく@<code>{>=}という一つのトークンが生成されます。

//emlist[組み立て中のトークンを確定するflush][java]{
private void flush(StringBuilder currentToken, List<String> tokens) {
  if (currentToken.length() > 0) {
    tokens.add(currentToken.toString());
    currentToken.setLength(0);
  }
}
//}

入力の末尾まで走査した後にもflushを呼び、最後のトークンを確定します。最後までinStringがtrueなら、対応する閉じクォートがないため例外を送出します。

たとえば、次のINSERT文を走査するとします。

//emlist[Tokenizerの動作例][sql]{
INSERT INTO users (id, name) VALUES (1, 'Taro Yamada');
//}

主要な位置での状態は次のように変化します。

//cmd{
入力位置             currentToken     確定したtokens
INSERTの後の空白     "INSERT"         [INSERT]
usersの後の空白      "users"          [INSERT, INTO, users]
(                    ""               [..., users, (]
'Taro Yamada'        "'Taro Yamada'"  空白を含めたまま保持
)                    ""               [..., 'Taro Yamada', )]
;                    ""               セミコロンは追加しない
//}

最終的な戻り値は次のとおりです。

//cmd{
[INSERT, INTO, users, (, id, ,, name, ),
 VALUES, (, 1, ,, 'Taro Yamada', )]
//}

=== 構文解析（SimpleParser）

@<code>{SimpleParser.parseStatement()}は、先頭のトークンを調べて解析メソッドを選択します。

//emlist[SQL文の種類を選択する処理][java]{
String command = tokens.get(0).toLowerCase();

return switch (command) {
  case "select" -> parseSelect(tokens);
  case "insert" -> parseInsert(tokens);
  case "create" -> parseCreateTable(tokens);
  case "update" -> parseUpdate(tokens);
  case "delete" -> parseDelete(tokens);
  default -> throw new IllegalArgumentException(
      "Unsupported SQL command: " + command);
};
//}

各解析メソッドは、トークンを参照する位置を@<code>{index}で管理します。値を一つ読み取るたびにindexを進め、@<code>{expect()}を使って必要なキーワードや記号が所定の位置にあるか確認します。

//emlist[必要なトークンを確認するexpect][java]{
private void expect(List<String> tokens, int index, String expected) {
  if (index >= tokens.size()) {
    throw new IllegalArgumentException(
        "Expected '" + expected + "', but reached end of SQL.");
  }

  if (!equalsIgnoreCase(tokens.get(index), expected)) {
    throw new IllegalArgumentException(
        "Expected '" + expected
            + "', but found '" + tokens.get(index) + "'.");
  }
}
//}

キーワードは大文字と小文字を区別せず照合します。想定したトークンがなければ解析を継続せずIllegalArgumentExceptionを送出するため、不完全なStatementがQueryExecutorへ渡ることはありません。

=== ASTの表現（Statement）

抽象構文木（Abstract Syntax Tree、AST）は、プログラムやSQLの構造を木として表したデータです。「抽象」という名前は、元の文字列に含まれる空白、改行、キーワードの大文字・小文字、括弧やカンマなどの表記上の情報を取り除き、実行に必要な意味だけを残すことに由来します。

たとえば、次の二つのSQLは空白と大文字・小文字が異なります。

//emlist[表記は異なるが意味が同じSQL][sql]{
SELECT name FROM users WHERE age >= 20;
select  name  from  users  where  age>=20;
//}

Tokenizerが生成するトークンの表記には差が残りますが、SimpleParserはどちらからも同じ構造のStatement.Selectを生成します。QueryExecutorは元の書き方を意識せず、@<code>{tableName}や@<code>{whereCondition}を参照できます。

一般的なASTでは、文全体が根となり、その下に句や式のノードが接続されます。本章の実装では、Statementを根、ConditionとJoinClauseを子要素とする小さな木として表現できます。

//cmd{
Statement.Select
├── 選択列
├── FROMのテーブル
├── JoinClause
│   ├── 結合先テーブル
│   └── Condition（ON条件）
└── Condition（WHERE条件）
//}

実装では専用のNodeクラスを階層的に作る代わりに、Statementインターフェースを実装するJavaのrecordを使います。recordのフィールド間の包含関係がASTの親子関係に相当します。

//emlist[Statementの定義][java]{
public interface Statement {
  record CreateTable(String tableName, List<Column> columns)
      implements Statement {}

  record Insert(String tableName,
                List<String> columnNames,
                List<String> values)
      implements Statement {}

  record Select(List<String> selectColumns,
                String tableName,
                Condition whereCondition,
                JoinClause joinClause)
      implements Statement {}

  record Update(String tableName,
                String columnName,
                String value,
                Condition whereCondition)
      implements Statement {}

  record Delete(String tableName, Condition whereCondition)
      implements Statement {}
}
//}

たとえば、次のSQLを解析します。

//emlist[Statement.Selectへ変換するSQL][sql]{
SELECT name FROM users WHERE age >= 20;
//}

生成される情報は次のとおりです。

//cmd{
Statement.Select
├── selectColumns: [name]
├── tableName: users
├── whereCondition
│   ├── left: age
│   ├── operator: >=
│   └── right: 20
└── joinClause: null
//}

Statementは元のSQL文字列を保持するのではなく、後続の処理が参照する要素を項目ごとに保持します。この分離には次の利点があります。

 * QueryExecutorがSQL文字列を再解析する必要がありません。
 * 構文エラーを実行前に検出できます。
 * SQLの表記が異なっても同じ実行処理を利用できます。
 * Plannerなどの後続処理が条件やテーブル名を直接調べられます。

=== CRUD文の解析

ここでは、CREATE TABLEに加えて、CRUDに対応するINSERT、SELECT、UPDATE、DELETEがどのStatementへ変換されるかを確認します。CRUDの意味自体は第2章「CRUD操作の実装」を参照してください。

==== CREATE TABLEの解析

CREATE TABLEはCRUDではなく、テーブルを定義するDDLです。@<code>{parseCreateTable()}はCREATE、TABLE、テーブル名、開き括弧の順に読み取ります。その後、閉じ括弧へ到達するまでカラム名とデータ型を繰り返し取得します。

//emlist[カラム定義を読み取る処理][java]{
while (index < tokens.size() && !tokens.get(index).equals(")")) {
  String columnName = tokens.get(index++);
  Schema.DataType type = parseDataType(tokens.get(index++));
  int length = 0;

  if (index < tokens.size() && tokens.get(index).equals("(")) {
    index++;
    length = Integer.parseInt(tokens.get(index++));
    expect(tokens, index, ")");
    index++;
  }

  columns.add(new Schema.Column(columnName, type, length));

  if (index < tokens.size() && tokens.get(index).equals(",")) {
    index++;
  }
}
//}

データ型はSchema.DataTypeへ変換されます。STRINGでは@<code>{STRING(20)}のような括弧内の長さも取得し、Statement.CreateTableへColumnの一覧を保存します。

==== INSERTの解析

INSERTはCreateに当たる操作です。@<code>{parseInsert()}はINSERT INTOの後からテーブル名を取得します。続くトークンが開き括弧ならカラム名一覧を読み、VALUESの括弧内から値一覧を読み取ります。

//emlist[INSERTからカラム名と値を取得する処理][java]{
String tableName = tokens.get(2);
List<String> columnNames = new ArrayList<>();

if (tokens.get(index).equals("(")) {
  index++;
  while (!tokens.get(index).equals(")")) {
    if (!tokens.get(index).equals(",")) {
      columnNames.add(tokens.get(index));
    }
    index++;
  }
  index++;
}

expect(tokens, index, "values");
// valuesの括弧内を同じ要領でvaluesへ追加する
//}

カラム名を省略した場合、columnNamesは空の一覧になります。値とSchemaのカラムを対応付け、型変換する処理はQueryExecutorが担当します。

==== SELECTの解析

SELECTはReadに当たる操作です。@<code>{parseSelect()}はFROMへ到達するまでのトークンを取得列として集め、その直後のトークンをテーブル名として保存します。

//emlist[SELECT列とFROMのテーブルを取得する処理][java]{
while (index < tokens.size()
    && !equalsIgnoreCase(tokens.get(index), "from")) {
  if (!tokens.get(index).equals(",")) {
    selectColumns.add(tokens.get(index));
  }
  index++;
}

expect(tokens, index, "from");
index++;
String tableName = tokens.get(index++);
//}

FROM句の後にJOINがあればJoinClauseを、WHEREがあればConditionを読み取ります。本章の構文では、JOINは一つだけで、JOINの後にWHEREを書く順序に限られます。

==== UPDATEの解析

UPDATEはUpdateに当たる操作です。テーブル名に続いてSETを確認し、更新対象のカラム名、等号、新しい値を順に読み取ります。WHEREがあればConditionも取得します。

//emlist[UPDATEの主要要素を取得する処理][java]{
String tableName = tokens.get(1);
expect(tokens, 2, "set");

String columnName = tokens.get(3);
expect(tokens, 4, "=");
String value = tokens.get(5);

// 後続にWHEREがあればConditionを読み取る
//}

Parserの段階では、新しい値をまだColumnの型へ変換しません。QueryExecutorがSchemaを参照して変換します。

==== DELETEの解析

DELETEはDeleteに当たる操作です。@<code>{parseDelete()}はDELETE FROMに続くテーブル名を取得し、WHEREがあればConditionを読み取ります。

//emlist[DELETEの主要要素を取得する処理][java]{
expect(tokens, 0, "delete");
expect(tokens, 1, "from");
String tableName = tokens.get(2);

if (index < tokens.size()
    && equalsIgnoreCase(tokens.get(index), "where")) {
  index++;
  whereCondition = parseCondition(tokens, index);
}
//}

WHEREを省略したStatement.DeleteではwhereConditionがnullになります。QueryExecutorではnullの条件を常に一致すると判断するため、実行時には全行が削除対象になります。

ParserはSQLの構造を読み取りますが、テーブルやカラムが実在するか、値を指定された型へ変換できるかまでは判断しません。これらはCatalogとSchemaを参照できるQueryExecutorが確認します。

=== ConditionとJoinClause

WHERE句とJOINのON句は、いずれも左辺、演算子、右辺の三要素からなるConditionで表します。

//emlist[ConditionとJoinClause][java]{
record Condition(String left, String operator, String right) {}

record JoinClause(String tableName, Condition onCondition) {}
//}

本章で扱う比較演算子は@<code>{=}、@<code>{!=}、@<code>{>}、@<code>{>=}、@<code>{<}、@<code>{<=}です。一つのConditionは一つの比較だけを表し、ANDやORによる複合条件は扱いません。

JoinClauseは、結合する右側のテーブル名とON条件を保持します。たとえば@<code>{users.id = orders.user_id}では、両辺がカラム名として解決されます。

Conditionは@<code>{parseCondition()}で三つの連続したトークンから生成します。

//emlist[Conditionを生成する処理][java]{
private Statement.Condition parseCondition(
    List<String> tokens, int index) {
  if (index + 2 >= tokens.size()) {
    throw new IllegalArgumentException("Invalid condition.");
  }

  String left = tokens.get(index);
  String operator = tokens.get(index + 1);
  String right = tokens.get(index + 2);

  return new Statement.Condition(left, operator, right);
}
//}

この実装ではConditionの構文を三トークンに限定しています。そのため、括弧を使った条件やAND、OR、NOTは解析できません。この制限により、ParserとQueryExecutorの条件処理を小さく保っています。

== QueryExecutorによるStatementの実行
Parserが生成したStatementだけでは、ページファイルに対する操作は行われません。本節では、Statementの種類ごとに実行処理を選択し、Catalog、Schema、Tableを組み合わせてCRUDへ接続する方針でQueryExecutorを実装します。
@<code>{QueryExecutor.execute()}はStatementの実際の型を調べ、対応する実行メソッドを呼び出します。

データの登録、取得、更新、削除というCRUDの意味と、最小構成での処理手順は、第2章「CRUD操作の実装」で説明しています。本章ではCRUD自体を再説明せず、SQLから作られたStatement、Schema、Catalog、Tableを使って各操作を実行する部分に注目します。

//emlist[Statementに応じた処理の選択][java]{
if (statement instanceof Statement.CreateTable create) {
  executeCreateTable(create);
} else if (statement instanceof Statement.Insert insert) {
  executeInsert(insert);
} else if (statement instanceof Statement.Select select) {
  executeSelect(select);
} else if (statement instanceof Statement.Update update) {
  executeUpdate(update);
} else if (statement instanceof Statement.Delete delete) {
  executeDelete(delete);
}
//}

この分岐により、ParserはSQLの解析、QueryExecutorは実行手順、Tableはファイル操作に集中できます。

=== CREATE TABLEとINSERT

CREATE TABLEでは、Statement.CreateTableが保持するテーブル名とColumnの一覧からSchemaを作り、Catalogへ登録します。Catalogは空のテーブルファイルを作成し、catalog.txtを更新します。

INSERTでは、CatalogからSchemaとTableを取得します。@<code>{buildRow()}はSchemaの各カラムを既定値で初期化した後、Statement.Insertの値を対応するカラムへ設定します。

値はColumnの型に従って変換されます。

//emlist[文字列からカラム型への変換][java]{
return switch (column.type()) {
  case INTEGER -> Integer.parseInt(value);
  case FLOAT -> Float.parseFloat(value);
  case DOUBLE -> Double.parseDouble(value);
  case STRING -> {
    if (value.length() > column.length()) {
      throw new IllegalArgumentException(
          "Value length exceeds column length.");
    }
    yield value;
  }
};
//}

Rowが完成すると、TableはSchemaのカラム順に値を直列化します。空きスロットの探索と、新しいページを追加する手順は、第4章「挿入（insert）」で説明したものと同じです。本章では、その手順へSchemaに基づく可変のレコード形式を組み合わせています。

=== SELECTとJOIN

SELECTでは、FROM句のTableに対して@<code>{scan()}を呼び出します。WHERE条件がある場合は、各Rowに対して@<code>{matches()}を実行し、一致した行だけを残します。その後、SELECT句に指定されたカラムだけを@<code>{projectRow()}で取り出します。

//cmd{
Table.scan()
    │
    ▼
JOINがあれば行を結合
    │
    ▼
WHERE条件を評価
    │
    ▼
SELECT列を射影
    │
    ▼
結果を表示
//}

JOINがある場合は、左側の各行に対して右側のTableを走査するNested Loop Joinを実行します。カラム名の衝突を避けるため、結合中のRowでは@<code>{users.id}のようにテーブル名を付けます。

条件の右辺は、最初にカラム名として解決されます。該当するカラムがなければ、整数、実数、文字列の順にリテラルとして解釈されます。数値同士はdoubleへ変換して比較し、それ以外は文字列として比較します。

=== UPDATEとDELETE

UPDATEとDELETEは@<code>{scanRecords()}を使います。scanRecordsはRowだけでなくRecordIdも返すため、条件に一致した行の保存位置を特定できます。

UPDATEでは対象カラムの存在をSchemaで確認し、新しい値をColumnの型へ変換します。条件に一致したRowを書き換え、同じRecordIdを指定してTable.updateを呼び出します。

DELETEでは条件に一致したRecordIdをTable.deleteへ渡します。同じスロットの上書きと、使用フラグによる論理削除の詳細は、第4章「更新（update）」と「削除（delete）」を参照してください。また、削除したスロットを挿入先として再利用する考え方は、第5章「空き領域リスト（freeList）による挿入の高速化」でも扱っています。本章では、これらの処理をSQLのWHERE条件と接続します。

WHERE句を省略した場合、@<code>{matches(row, null)}はtrueを返します。そのため、UPDATEまたはDELETEの対象は全行になります。

== 実行結果の表示
ここまでに実装したSQL処理が一連の流れとして動作するかを確認するには、各構文の入力と結果を対応付けて検証する必要があります。

本節では、QueryExecutorが処理結果を標準出力へ表示します。ここではusersとordersを作成し、INSERT、SELECT、FROM、WHERE、JOIN、UPDATE、DELETEの順に結果を確認します。

REPLは一行の入力を一つのSQLとして解析するため、実行時には各SQLを一行で入力します。また、実行時間は環境によって異なるため、以下の出力例では@<code>{(Executed in ... ms)}を省略します。

=== 実行例で使用するテーブル

最初にusersとordersを作成します。

//cmd{
db > CREATE TABLE users (id INTEGER, name STRING(20), age INTEGER);
Table created: users

db > CREATE TABLE orders (id INTEGER, user_id INTEGER, amount DOUBLE);
Table created: orders
//}

usersには利用者、ordersには利用者に対応する注文を保存します。JOINでは@<code>{users.id}と@<code>{orders.user_id}を比較します。

=== INSERT文と実行結果

INSERTを実行すると、QueryExecutorは作成したRowを表示します。次の例ではusersへ二行、ordersへ二行を追加します。

//cmd{
db > INSERT INTO users (id, name, age) VALUES (1, 'Taro', 20);
Inserted into users: {id=1, name=Taro, age=20}

db > INSERT INTO users (id, name, age) VALUES (2, 'Hanako', 18);
Inserted into users: {id=2, name=Hanako, age=18}

db > INSERT INTO orders (id, user_id, amount) VALUES (100, 1, 1250.0);
Inserted into orders: {id=100, user_id=1, amount=1250.0}

db > INSERT INTO orders (id, user_id, amount) VALUES (101, 2, 800.0);
Inserted into orders: {id=101, user_id=2, amount=800.0}
//}

INSERTの出力は、Tableへ保存された後のRowです。Statement.Insertが保持していた文字列はSchemaに従って変換されているため、idとageはInteger、amountはDoubleとして表示されます。

=== SELECT文とFROM句の実行結果

FROM句は、読み取り元のテーブルを指定します。@<code>{SELECT * FROM users}では、QueryExecutorがCatalogからusersのTableを取得し、すべてのカラムを表示します。

//cmd{
db > SELECT * FROM users;
{id=1, name=Taro, age=20}
{id=2, name=Hanako, age=18}
//}

アスタリスクの代わりにカラム名を指定すると、@<code>{projectRow()}が該当するカラムだけを結果へ残します。

//cmd{
db > SELECT id, name FROM users;
{id=1, name=Taro}
{id=2, name=Hanako}
//}

FROM句自体が結果を表示するのではなく、SELECTがどのTableを走査するかを決めます。本章のSimpleParserではFROMを省略できません。

=== WHERE句の実行結果

WHERE句は、Tableから読み取ったRowをConditionで絞り込みます。次の例ではageが20以上の行だけが残ります。

//cmd{
db > SELECT name, age FROM users WHERE age >= 20;
{name=Taro, age=20}
//}

条件に一致する行がなければ、@<code>{printRows()}は@<code>{(empty)}を表示します。

//cmd{
db > SELECT name FROM users WHERE age > 100;
(empty)
//}

=== JOIN句の実行結果

JOIN句は、FROMで指定した左側のテーブルと、JOINの直後に指定した右側のテーブルを結合します。ON条件に一致する行だけが結果へ追加されます。

//cmd{
db > SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.user_id;
{users.name=Taro, orders.amount=1250.0}
{users.name=Hanako, orders.amount=800.0}
//}

結合中のRowでは、同名カラムを区別するために@<code>{users.id}や@<code>{orders.id}のようなテーブル名付きのカラム名を使用します。SELECTの取得列にも同じ形式を指定します。

JOINの後にWHEREを書くこともできます。次の例では結合後の行から、注文額が1000以上の行だけを表示します。

//cmd{
db > SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.user_id WHERE orders.amount >= 1000;
{users.name=Taro, orders.amount=1250.0}
//}

=== UPDATE文と実行結果

UPDATEを実行すると、QueryExecutorは更新した行数を表示します。次の例ではidが1の利用者について、ageを21へ変更します。

//cmd{
db > UPDATE users SET age = 21 WHERE id = 1;
Updated 1 row(s).

db > SELECT id, name, age FROM users WHERE id = 1;
{id=1, name=Taro, age=21}
//}

最初の出力は更新件数です。二つ目のSELECTにより、同じRecordIdのスロットが新しいRowで上書きされたことを確認できます。

=== DELETE文と実行結果

DELETEを実行すると、QueryExecutorは削除した行数を表示します。次の例ではidが2の利用者を論理削除します。

//cmd{
db > DELETE FROM users WHERE id = 2;
Deleted 1 row(s).

db > SELECT * FROM users;
{id=1, name=Taro, age=21}
//}

削除後のSELECTにHanakoの行が含まれないことから、使用フラグが0のスロットをTable.scanが読み飛ばしていることを確認できます。

WHERE句を省略したUPDATEとDELETEはすべての行を対象にします。意図しない一括更新や一括削除を避けるため、実行例ではWHERE条件を指定しています。

=== エラーと実行時間の表示

Parser、Catalog、QueryExecutorなどで例外が発生した場合、REPLは例外メッセージの先頭に@<code>{Error:}を付けて表示し、次の入力を受け付けます。

//cmd{
db > SELECT unknown FROM users;
Error: Unknown column: unknown
//}

REPLは成功時とエラー時のどちらでも、解析と実行に要した時間を続けて表示します。実行時間を計測する仕組みは、第4章「処理時間の計測」で説明しています。本章では計測対象にParserとQueryExecutorの処理が加わります。

//cmd{
db > SELECT name FROM users WHERE age >= 20;
{name=Taro}
(Executed in 1.234 ms)
//}

この表示方法は結果を確認するための簡易的な形式です。表形式への整形や、エラーの種類に応じた表示の分離は行っていません。

== 本章に残る課題：実行方法を選択するプランナー

本章では、SQL文字列をStatementへ変換し、テーブル定義に従って実行する経路を構築しました。一方、QueryExecutorはSQLの条件に応じて実行方法を選択しません。

第5章「B-Treeを用いたCRUDの高速化」では、整数のidをB-Treeへ登録し、キーからRecordIdを検索する方法を扱いました。B-Treeの構造、検索、挿入、削除については第5章を参照してください。本章では複数のテーブルと任意のカラムを扱うように構造を変更しており、QueryExecutorのSELECTは常に@<code>{Table.scan()}を呼び出します。したがって、本章のコードではidを条件にした場合も、それ以外のカラムを条件にした場合も全件走査になります。

インデックスを再び利用するには、少なくとも次の判断が必要です。

 * WHERE句があるか
 * 条件の左辺に対応するカラムがあるか
 * そのカラムにインデックスがあるか
 * 比較演算子がインデックスで処理できるか
 * インデックスを使わず全件走査する必要があるか

SQLの解析結果とSchemaを参照し、実行方法を決める役割がプランナーです。たとえば、インデックスが設定された整数カラムに対する等価条件ならインデックス走査を選び、それ以外なら逐次走査を選ぶ、という判断を行います。

//cmd{
Statement.Select + Schema
          │
          ▼
       Planner
       条件を確認
          │
     ┌────┴────┐
     ▼         ▼
Index Scan   Seq Scan
インデックス  全件走査
を利用
//}

本章のQueryExecutorには、このPlannerがまだありません。第7章では、Statementから実行計画を作り、条件に応じてインデックス走査と逐次走査を選択する仕組みを扱います。
