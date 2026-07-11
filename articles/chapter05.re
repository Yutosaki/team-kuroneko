#@# = テーブルが定義され、データが保存されるまで
#@# == SQLがファイルへ保存されるまでの全体像
#@# == テーブル定義とSchemaの必要性
#@# == CREATE TABLEの構文解析
#@# == QueryExecutorによるSchemaの生成
#@# == Catalogへの登録とテーブルファイルの作成
#@# == INSERTにおける型変換とRowの生成
#@# == レコードのバイト列への変換
#@# == ページとスロットへの保存
#@# == SELECTにおけるRowの復元と条件評価
#@# == UPDATEにおけるRecordIdとスロットの上書き
#@# == DELETEにおける使用フラグとスロットの再利用
#@# == Schemaを中心としたテーブルのライフサイクル

= テーブルが定義され、データが保存されるまで

chapter05では、ユーザーが入力したSQLが、直接ファイルへ書き込まれるわけではない。入力されたSQLは、複数の処理を順番に通過し、最終的にテーブルごとのページファイルへ保存される。

この一連の処理を理解するためには、最初に「テーブルがどのように作られるのか」を知る必要がある。INSERTやSELECTといったSQLは、すでにテーブルが存在していることを前提としているからである。

たとえば、次のINSERT文を考えてみよう。

//emlist[INSERT文の例][sql]{
INSERT INTO users (id, name, age)
VALUES (1, 'Taro', 20);
//}

このSQLには、@<code>{users}というテーブル名と、@<code>{id}、@<code>{name}、@<code>{age}という列名が含まれている。しかし、データベースが事前に@<code>{users}テーブルの定義を知らなければ、それぞれの値をどのように保存すればよいか判断できない。

@<code>{1}は整数として保存するのか、それとも文字列として保存するのか。@<code>{Taro}には何バイト分の領域を用意すればよいのか。@<code>{20}はどの列に対応するのか。これらを決めるために必要なのが、テーブル定義である。

chapter05では、テーブル定義を@<code>{Schema}として表現している。Schemaは、テーブルが持つ列の名前、データ型、サイズなどを保持する。言い換えれば、Schemaはテーブルの構造を表す設計情報である。

ただし、Schemaはプログラムの起動時から最初から存在するわけではない。ユーザーが@<code>{CREATE TABLE}文を実行したときに生成される。

次のSQLを例に、テーブルが作られるまでの流れを見ていこう。

//emlist[usersテーブルの定義][sql]{
CREATE TABLE users (
    id INTEGER,
    name STRING(20),
    age INTEGER
);
//}

このSQLは、@<code>{users}という名前のテーブルを作り、そのテーブルに三つの列を定義している。@<code>{id}と@<code>{age}は整数型であり、@<code>{name}は最大20バイトの文字列型である。

ユーザーがこのSQLを入力すると、最初に@<code>{SimpleParser}が呼び出される。ただし、@<code>{SimpleParser}がいきなりテーブルを作るわけではない。Parserの役割は、入力されたSQLを読み取り、その内容をJavaプログラムから扱いやすい形へ変換することである。

その処理の流れを図にすると、次のようになる。

//cmd{
ユーザーが入力したSQL

CREATE TABLE users (
    id INTEGER,
    name STRING(20),
    age INTEGER
);

        │
        ▼
┌─────────────────────┐
│     Tokenizer       │
│ SQLを単語や記号に分割 │
└─────────────────────┘
        │
        ▼
┌─────────────────────┐
│    SimpleParser     │
│ SQLの文法を読み取る   │
└─────────────────────┘
        │
        ▼
┌──────────────────────────┐
│ Statement.CreateTable    │
│ テーブル名と列定義を保持   │
└──────────────────────────┘
//}

TokenizerによってSQLは、@<code>{CREATE}、@<code>{TABLE}、@<code>{users}、@<code>{(}、@<code>{id}、@<code>{INTEGER}といった小さな要素に分けられる。その後、SimpleParserはそれらの並びを読み取り、CREATE TABLE文であることを判断する。

解析が終わると、SQLの内容は@<code>{Statement.CreateTable}として表現される。元のSQL文字列をそのまま保持するのではなく、テーブル名と列定義を整理したデータへ変換するのである。

概念的には、次のような情報が作られる。

//cmd{
Statement.CreateTable

テーブル名
    users

列定義
    id
        型: INTEGER

    name
        型: STRING
        最大長: 20

    age
        型: INTEGER
//}

ここまでの処理では、まだテーブルファイルは作成されていない。ParserはSQLの内容を読み取っただけであり、実際のデータベース操作は行っていない。

解析後の@<code>{Statement.CreateTable}は、@<code>{QueryExecutor}へ渡される。QueryExecutorは、受け取ったStatementの種類を調べ、CREATE TABLE文であればテーブル作成用の処理を実行する。

QueryExecutorは、Statementに保存されている列情報をもとにSchemaを生成する。

//cmd{
Statement.CreateTable
        │
        ▼
┌─────────────────────┐
│   QueryExecutor     │
│ 列情報からSchemaを作る │
└─────────────────────┘
        │
        ▼
┌─────────────────────────┐
│      Schema(users)      │
│ id    : INTEGER         │
│ name  : STRING(20)      │
│ age   : INTEGER         │
└─────────────────────────┘
//}

このSchemaによって、@<code>{users}テーブルがどのような構造を持つかが決まる。Schemaは、単に列名を保存するだけではない。各列がどの型であるか、ファイル上で何バイト使用するか、レコード全体が何バイトになるかを計算するためにも使われる。

Schemaが生成されると、QueryExecutorはそのSchemaをCatalogへ渡す。

Catalogは、データベース内に存在するテーブルを管理するための仕組みである。どのテーブルが存在し、それぞれがどのSchemaを持ち、どのファイルと対応しているかを管理する。

@<code>{users}テーブルを登録するときの流れは、次のようになる。

//cmd{
Schema(users)
        │
        ▼
┌─────────────────────┐
│       Catalog       │
│ usersとSchemaを登録   │
└─────────────────────┘
        │
        ├─────────────────────┐
        ▼                     ▼
┌──────────────────┐   ┌──────────────────┐
│   catalog.txt    │   │    users.tbl     │
│ テーブル定義を保存 │   │ データ保存用ファイル │
└──────────────────┘   └──────────────────┘
//}

ここでは、二種類のファイルが登場する。

@<code>{catalog.txt}には、テーブル名や列名、データ型といった定義情報が保存される。一方、@<code>{users.tbl}には、INSERTによって追加される実際のデータが保存される。

この二つは役割が異なる。

@<code>{catalog.txt}は、データベースの設計情報を保存するファイルである。データベースを再起動したとき、Catalogはこのファイルを読み込み、以前に作成したテーブルのSchemaを復元する。

一方、@<code>{users.tbl}は、@<code>{users}テーブルの行データを保存するためのファイルである。CREATE TABLEを実行した直後は、このファイルにはまだレコードが存在しない。データを書き込むための空のファイルだけが用意された状態である。

したがって、CREATE TABLE文の処理全体は、次のようにまとめられる。

//cmd{
CREATE TABLE users (...)
        │
        ▼
Tokenizer
        │
        ▼
SimpleParser
        │
        ▼
Statement.CreateTable
        │
        ▼
QueryExecutor
        │
        ▼
Schema生成
        │
        ▼
Catalogへ登録
        │
        ├───────────────┐
        ▼               ▼
catalog.txt         users.tbl
定義を保存          空のデータファイル
//}

CREATE TABLEによって作られるのは、データそのものではない。テーブルがどのような列を持ち、どの型で値を保存するのかという設計情報である。

この設計情報が作られて初めて、INSERT、SELECT、UPDATE、DELETEといったSQLを実行できるようになる。

== Schemaは後続の処理で繰り返し使われる

Schemaは、CREATE TABLEのときだけ使用されるものではない。一度作られたSchemaは、その後のデータ操作でも繰り返し参照される。

たとえば、次のINSERT文を実行するとする。

//emlist[Schemaを利用するINSERT文][sql]{
INSERT INTO users (id, name, age)
VALUES (1, 'Taro', 20);
//}

このSQLも、最初はTokenizerとSimpleParserによって解析される。

//cmd{
INSERT INTO users (id, name, age)
VALUES (1, 'Taro', 20);

        │
        ▼
Tokenizer
        │
        ▼
SimpleParser
        │
        ▼
Statement.Insert
//}

解析後の@<code>{Statement.Insert}には、次の情報が保存される。

//cmd{
Statement.Insert

テーブル名
    users

列名
    id
    name
    age

値
    1
    Taro
    20
//}

ただし、この段階では、値の型が完全に決まったわけではない。Parserが読み取った@<code>{1}や@<code>{20}は、SQL中に書かれていた文字列として扱われている。

そこでQueryExecutorは、Catalogから@<code>{users}テーブルを取得し、そのSchemaを参照する。

//cmd{
Statement.Insert
        │
        ▼
QueryExecutor
        │
        ▼
Catalogからusersを取得
        │
        ▼
Schemaを参照

id    : INTEGER
name  : STRING(20)
age   : INTEGER
//}

Schemaを見ることで、QueryExecutorは各値をどの型へ変換すればよいか判断できる。

@<code>{id}列はINTEGERなので、文字列として読み取られた@<code>{"1"}を整数値@<code>{1}へ変換する。@<code>{name}列はSTRINGなので、@<code>{"Taro"}を文字列として扱う。@<code>{age}列はINTEGERなので、@<code>{"20"}を整数値@<code>{20}へ変換する。

この処理を図にすると、次のようになる。

//cmd{
SQLから読み取った値

"1"     "Taro"     "20"
 │         │          │
 ▼         ▼          ▼

Schemaを参照

id        name        age
INTEGER   STRING      INTEGER
 │         │          │
 ▼         ▼          ▼

Java上の値へ変換

1         "Taro"      20
//}

型変換が完了すると、QueryExecutorは一行分のデータを表す@<code>{Row}を生成する。

//cmd{
Row

id    = 1
name  = "Taro"
age   = 20
//}

Rowは、Javaプログラム上で一行分のデータを扱うための形式である。この時点では、まだファイルへ保存されていない。

QueryExecutorは、生成したRowを@<code>{Table.insert()}へ渡す。

//cmd{
Statement.Insert
        │
        ▼
QueryExecutor
        │
        ▼
Schemaを使って型変換
        │
        ▼
Rowを生成
        │
        ▼
Table.insert()
//}

Tableは、Rowをそのままファイルへ書き込むわけではない。Javaのオブジェクトを、ファイルへ保存できるバイト列へ変換する必要がある。

たとえば、@<code>{users}テーブルのSchemaが次のように定義されているとする。

//cmd{
id    INTEGER
name  STRING(20)
age   INTEGER
//}

このとき、一つのレコードは概念的に次の構造になる。

//cmd{
┌──────────┬─────────┬──────────┬──────────────┬─────────┐
│ 使用フラグ │ id      │ 文字列長   │ nameの領域    │ age     │
│ 1バイト    │ 4バイト  │ 4バイト    │ 20バイト      │ 4バイト  │
└──────────┴─────────┴──────────┴──────────────┴─────────┘
//}

先頭の使用フラグは、そのスロットに有効なレコードが保存されているかを表す。@<code>{1}なら使用中、@<code>{0}なら未使用である。

INTEGER型の@<code>{id}と@<code>{age}は、それぞれ4バイトで保存される。STRING型の@<code>{name}は、実際の文字列長を保存する領域と、最大20バイトの文字列領域を使用する。

この場合、レコード全体のサイズは次のようになる。

//cmd{
使用フラグ        1バイト
id                4バイト
nameの文字列長     4バイト
name本体          20バイト
age               4バイト
────────────────────────
合計              33バイト
//}

Schemaは、このレコードサイズの計算にも使われる。

Tableは、RowをSchemaの列順に従ってバイト列へ変換し、4KBのページ内へ保存する。

//cmd{
Row
        │
        ▼
Schemaに従ってバイト列へ変換
        │
        ▼
33バイトのレコード
        │
        ▼
4KBページ内の空きスロットへ保存
        │
        ▼
users.tblへ書き込み
//}

ページとは、データベースがファイルを読み書きする固定サイズの単位である。chapter05では、1ページを4096バイトとして扱う。

//cmd{
users.tbl

┌─────────────────────────────┐
│ ページ0                     │
│ 4096バイト                  │
│                             │
│ ┌──────┬──────┬──────┐      │
│ │slot 0│slot 1│slot 2│ ...  │
│ └──────┴──────┴──────┘      │
└─────────────────────────────┘

┌─────────────────────────────┐
│ ページ1                     │
│ 4096バイト                  │
└─────────────────────────────┘
//}

スロットとは、ページ内に設けられたレコード一件分の固定領域である。

先ほどの例では、一レコードが33バイトであるため、一ページに保存できるスロット数は、4096を33で割って求められる。

//cmd{
4096 ÷ 33 = 124 あまり 4
//}

したがって、一ページには124件分のレコードを保存できる。残りの4バイトは、一レコード分の領域に満たないため使用されない。

Tableは、ページ内のスロットを先頭から調べる。スロットの先頭にある使用フラグが@<code>{0}であれば、そのスロットは空いている。空きスロットが見つかると、Tableはそこへ新しいレコードを書き込む。

//cmd{
ページ内のスロット

slot 0
┌─────────────┐
│ 使用フラグ 1 │ ← 使用中
│ レコード      │
└─────────────┘

slot 1
┌─────────────┐
│ 使用フラグ 0 │ ← 空き
│ 未使用領域    │
└─────────────┘

slot 2
┌─────────────┐
│ 使用フラグ 1 │ ← 使用中
│ レコード      │
└─────────────┘
//}

この場合、新しいRowはslot 1へ保存される。

既存のページに空きスロットがなければ、Tableはファイルの末尾に新しい4KBページを追加する。

以上が、INSERTによって一行のデータが保存されるまでの流れである。

//cmd{
INSERT文
        │
        ▼
Tokenizer
        │
        ▼
SimpleParser
        │
        ▼
Statement.Insert
        │
        ▼
QueryExecutor
        │
        ▼
CatalogからTableとSchemaを取得
        │
        ▼
Schemaに従って値を型変換
        │
        ▼
Rowを生成
        │
        ▼
Table.insert()
        │
        ▼
Rowをバイト列へ変換
        │
        ▼
ページ内の空きスロットを検索
        │
        ▼
users.tblへ保存
//}

== SELECTではSchemaを使ってレコードを読み戻す

次に、保存されたデータをSELECTで取得する流れを考える。

//emlist[SELECT文の例][sql]{
SELECT name
FROM users
WHERE age >= 20;
//}

SELECT文も、TokenizerとSimpleParserによって解析され、@<code>{Statement.Select}へ変換される。

//cmd{
Statement.Select

取得列
    name

対象テーブル
    users

条件
    age >= 20
//}

QueryExecutorは、Catalogから@<code>{users}テーブルを取得し、@<code>{Table.scan()}を呼び出す。

Tableは@<code>{users.tbl}をページ単位で読み込み、各スロットの使用フラグを確認する。使用フラグが@<code>{1}であれば、そのスロットには有効なレコードが保存されている。

ただし、ファイルに保存されているのはRowではなくバイト列である。そのためTableは、Schemaを参照しながらバイト列をRowへ戻す。

//cmd{
users.tblのバイト列
        │
        ▼
Schemaを参照

id    : INTEGER
name  : STRING(20)
age   : INTEGER

        │
        ▼
値を読み取る

4バイト  → id
4バイト  → nameの長さ
20バイト → name
4バイト  → age

        │
        ▼
Rowへ復元
//}

復元されたRowは、QueryExecutorへ返される。

QueryExecutorは、WHERE条件である@<code>{age >= 20}を評価し、条件に一致するRowだけを残す。その後、SELECTで指定された@<code>{name}列だけを取り出し、結果として表示する。

//cmd{
SELECT文
        │
        ▼
Statement.Select
        │
        ▼
Table.scan()
        │
        ▼
バイト列をRowへ復元
        │
        ▼
WHERE条件で絞り込む
        │
        ▼
name列だけ取り出す
        │
        ▼
画面へ表示
//}

ここでもSchemaが重要な役割を持つ。Schemaがなければ、ファイルのどの位置にどの列が保存されているか分からない。

たとえば、レコードの先頭から4バイトを読んだとしても、それが@<code>{id}なのか@<code>{age}なのかは、Schemaを見なければ判断できない。

== UPDATEではRecordIdを使って同じ位置を書き換える

UPDATEでは、条件に一致する行を探し、その行の内容を書き換える。

//emlist[UPDATE文の例][sql]{
UPDATE users
SET age = 30
WHERE id = 1;
//}

Parserは、このSQLを次のような@<code>{Statement.Update}へ変換する。

//cmd{
Statement.Update

対象テーブル
    users

更新する列
    age

新しい値
    30

条件
    id = 1
//}

QueryExecutorは、TableからRowを読み取り、@<code>{id = 1}に一致する行を探す。

ただし、UPDATEではRowの内容だけでなく、そのRowがファイル上のどこに保存されているかも必要になる。ファイル上の位置が分からなければ、どの部分を書き換えればよいか判断できないからである。

そこでTableは、Rowとともに@<code>{RecordId}を返す。

RecordIdは、ページ番号とスロット番号を持つ。

//cmd{
RecordId

page = 0
slot = 5
//}

これは、対象レコードがページ0のスロット5に保存されていることを表す。

//cmd{
users.tbl

ページ0
┌────────┬────────┬────────┬────────┬────────┬────────┐
│ slot 0 │ slot 1 │ slot 2 │ slot 3 │ slot 4 │ slot 5 │
└────────┴────────┴────────┴────────┴────────┴────────┘
                                                  ▲
                                                  │
                                          更新対象のレコード
//}

QueryExecutorは、対象Rowの@<code>{age}を30へ変更する。その後、変更後のRowとRecordIdを@<code>{Table.update()}へ渡す。

TableはRecordIdからファイル内の位置を計算し、該当するスロットだけを新しいバイト列で上書きする。

//cmd{
UPDATE文
        │
        ▼
Statement.Update
        │
        ▼
TableからRowとRecordIdを取得
        │
        ▼
WHERE条件に一致するRowを探す
        │
        ▼
ageを30へ変更
        │
        ▼
Schemaに従って再びバイト列へ変換
        │
        ▼
RecordIdが示すスロットを上書き
//}

UPDATEでは、テーブルファイル全体を書き直す必要はない。RecordIdによって対象の位置が分かるため、そのスロットだけを直接更新できる。

== DELETEではレコードを物理的に消去しない

DELETEでは、条件に一致する行を削除する。

//emlist[DELETE文の例][sql]{
DELETE FROM users
WHERE id = 1;
//}

Parserは、このSQLを次のような@<code>{Statement.Delete}へ変換する。

//cmd{
Statement.Delete

対象テーブル
    users

条件
    id = 1
//}

QueryExecutorはUPDATEと同様に、TableからRowとRecordIdを取得し、条件に一致する行を探す。

対象レコードが見つかると、そのRecordIdを@<code>{Table.delete()}へ渡す。

ただし、chapter05のDELETEは、レコード全体をファイルから物理的に消去するわけではない。また、後ろにあるレコードを前へ移動して空きを詰める処理も行わない。

Tableは、対象スロットの先頭にある使用フラグを@<code>{1}から@<code>{0}へ変更する。

//cmd{
削除前

┌──────────┬──────────────────────────┐
│ フラグ 1  │ id=1, name=Taro, age=20 │
└──────────┴──────────────────────────┘

                │
                ▼

削除後

┌──────────┬──────────────────────────┐
│ フラグ 0  │ id=1, name=Taro, age=20 │
└──────────┴──────────────────────────┘
//}

データ本体のバイト列が残っていたとしても、使用フラグが@<code>{0}であれば、そのスロットは未使用として扱われる。

SELECTでスキャンするときも、使用フラグが@<code>{0}のスロットは読み飛ばされる。

また、次回INSERTを行ったとき、そのスロットを新しいレコードの保存先として再利用できる。

//cmd{
DELETE
        │
        ▼
使用フラグを0にする
        │
        ▼
SELECTでは読み飛ばされる
        │
        ▼
次回INSERTで再利用される
//}

この方式により、DELETEのたびにファイル全体を再構成する必要がなくなる。

== chapter05におけるテーブルのライフサイクル

ここまでの処理をまとめると、chapter05のテーブルは、CREATE TABLEによって定義され、その後のSQLによって利用される。

最初にCREATE TABLEが実行され、Schemaが作られる。SchemaはCatalogへ登録され、@<code>{catalog.txt}へ保存される。同時に、データを保存するための@<code>{.tbl}ファイルが用意される。

その後、INSERTではSchemaを参照しながら値を型変換し、Rowをページへ保存する。

SELECTでは、Schemaを使ってバイト列をRowへ復元し、条件に一致するデータを取り出す。

UPDATEでは、RowとRecordIdを使って対象スロットを上書きする。

DELETEでは、対象スロットの使用フラグを未使用へ変更する。

全体の流れは、次のように表せる。

//cmd{
                    CREATE TABLE
                          │
                          ▼
                    Schemaを生成
                          │
                          ▼
                  Catalogへ登録する
                          │
              ┌───────────┴───────────┐
              ▼                       ▼
        catalog.txt                users.tbl
       テーブル定義を保存           データ保存用
                                      │
                  ┌───────────────────┼───────────────────┐
                  │                   │                   │
                  ▼                   ▼                   ▼
               INSERT              SELECT              UPDATE
                  │                   │                   │
          Schemaで型変換      SchemaでRowへ復元    RowとRecordIdを取得
                  │                   │                   │
              Rowを生成          条件で絞り込む       スロットを上書き
                  │
                  ▼
            ページへ保存
                                      │
                                      ▼
                                    DELETE
                                      │
                               RecordIdを取得
                                      │
                               使用フラグを0へ変更
//}

chapter05では、SQLごとに処理内容は異なるものの、すべてが同じテーブル定義を共有している。

CREATE TABLEによって生まれたSchemaが、その後のINSERT、SELECT、UPDATE、DELETEを支えているのである。

Schemaは単なる列名の一覧ではない。値をどの型へ変換するか、レコードを何バイトで保存するか、一ページに何件入るか、ファイルからどの順番で値を読み戻すかを決定する。

Catalogは、そのSchemaとTableをテーブル名に対応付けて管理する。

QueryExecutorは、Parserが作ったStatementを受け取り、Catalogから必要なSchemaとTableを取り出す。

Tableは、SchemaをもとにRowとバイト列を相互に変換し、ページファイルを操作する。

この役割分担によって、chapter05では、SQLの解析、テーブル定義、データ操作、ファイル保存が明確に分離されている。

ユーザーが入力した一つのSQLは、Parserによって意味を読み取られ、Statementとして整理され、QueryExecutorによって実行内容へ変換される。その後、Catalog、Schema、Table、Rowを経由し、最終的にページファイルへ到達する。

//cmd{
ユーザー入力SQL
        │
        ▼
SimpleParser
        │
        ▼
Statement（AST）
        │
        ▼
QueryExecutor
        │
        ▼
Catalog
        │
        ▼
SchemaとTable
        │
        ▼
Row
        │
        ▼
4KBページ
        │
        ▼
テーブルファイル（.tbl）
//}

chapter05の設計を理解するうえで重要なのは、この流れを個別のクラスの集まりとして見るのではなく、一つの連続した処理として捉えることである。

Parserだけではデータは保存されない。SchemaだけでもSQLは実行できない。Tableだけでは、どのテーブルを操作するか判断できない。

それぞれのクラスが担当する情報を次のクラスへ渡すことで、SQL文字列が最終的なファイル操作へ変換されているのである。