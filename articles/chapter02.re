= 最小のデータベース

== 対話型プログラム（REPL）の作成

データベースは、一度起動したらすぐに終了するプログラムではありません。ユーザーからの入力を待ち続け、入力された命令を実行し、その結果を表示することを繰り返します。
例えば、多くのデータベースでは次のような対話形式で操作を行います。
//emlist{
> SELECT * FROM users;
1|Alice|20
2|Bob|18
//}
このようなプログラムは@<b>{REPL（Read-Eval-Print Loop）}と呼ばれます。
REPLとは、

 * @<b>{Read}：ユーザーから入力を受け取る
 * @<b>{Eval}：入力された命令を実行する
 * @<b>{Print}：実行結果を表示する
 * @<b>{Loop}：これらを繰り返す

という4つの処理から構成されます。
本書で作成するデータベースも、このREPL形式を採用します。

=== 最初のデータベース

まずはデータベース本体となる `nekoDB` クラスを作成します。
コンストラクタでは、データを保持するための `HashMap`、キーボード入力を受け取る `Scanner`、入力を解析するための `SimpleParser` を初期化します。
//emlist{
public nekoDB() {
    db = new HashMap<>();
    scanner = new Scanner(System.in);
    parser = new SimpleParser();
    System.out.println("Welcome to nekoDB!");
}
//}
現時点では、`HashMap` がデータベースそのものになります。
プログラムを起動すると、
//emlist{
Welcome to nekoDB!
//}
というメッセージが表示され、データベースの開始を知らせます。

=== REPLの実装

データベースが起動したら、ユーザーが終了を指示するまで入力を受け付け続けます。
そのために、`start()` メソッドでは無限ループを用意しています。
//emlist{
while (true) {
    System.out.print("db > ");

    if (!scanner.hasNextLine()) {
        break;
    }

    String[] tokens = parser.parse(scanner.nextLine());

    // コマンドを実行
}
//}
このコードを実行すると、画面には次のように表示されます。
//emlist{
db >
//}
ここでユーザーはコマンドを入力します。
入力された文字列は1行分読み込まれ、
//emlist{
parser.parse(...)
//}
によって入力を解析し、後続の処理へ渡します。



== 入力とコマンドの扱い

ユーザーからの入力を受け付けるREPLを作成しましたが、この時点では入力された文字列は単なる文字列であり、プログラムはその意味を理解できません。
例えば、

//emlist{
db > insert 1 Alice
//}

という入力があったとしても、プログラムにとっては

//emlist{
"insert 1 Alice"
//}

という一つの文字列に過ぎません。
データベースとして動作させるには、この文字列を

 * 実行するコマンド
 * 操作対象のデータ

に分けて解釈する必要があります。
本書では、この役割を@<b>{SimpleParser}クラスが担当します。


=== コマンドの分割

最初に、入力された文字列を空白で分割します。
例えば、

//emlist{
insert 1 Alice
//}

という入力は、

//emlist{
["insert", "1", "Alice"]
//}

という3つの要素に分割されます。
この処理は `SimpleParser` クラスの `parse()` メソッドで行っています。

//emlist{
public String[] parse(String sql) {
    String[] tokens = sql.trim().split("\\s+");
    tokens[0] = tokens[0].toLowerCase();
    return tokens;
}
//}

入力の解析結果は次のようになり、コマンドと引数を個別に扱えるようになります。

//emlist{
| 入力               | 分割結果                     |
| ---------------- | ------------------------ |
| `insert 1 Alice` | `["insert","1","Alice"]` |
| `select`         | `["select"]`             |
| `delete 5`       | `["delete","5"]`         |
//}



=== コマンド名を取得する

データベースが最初に知りたいのは、「どの操作を実行するのか」です。
そこで、分割した配列の最初の要素をコマンドとして取り出します。

//emlist{
public String getCommand(String[] tokens) {
    return tokens.length > 0 ? tokens[0] : "";
}
//}

例えば、

//emlist{
["insert", "1", "Alice"]
//}

であれば、

//emlist{
insert
//}

が返されます。
これにより、以降の処理では入力全体を調べる必要がなくなり、コマンド名だけを見て処理を分岐できるようになります。


=== コマンドに応じた処理の分岐

`start()` メソッドでは、取得したコマンド名をもとに処理を切り替えています。

//emlist{
String[] tokens = parser.parse(scanner.nextLine());
String command = parser.getCommand(tokens);
//}

取得したコマンドに対して、

//emlist{
if (command.equals("insert") && tokens.length == 3) {
    insert(tokens[1], tokens[2]);
}
//}

のように判定を行います。
例えば、

//emlist{
insert 1 Alice
//}

と入力すると、

//emlist{
command = "insert"

tokens[1] = "1"

tokens[2] = "Alice"
//}

となり、

//emlist{
insert("1", "Alice");
//}

が呼び出されます。
このように、入力された文字列を解析し、適切なメソッドへ処理を振り分けることで、データベースはさまざまな命令を実行できるようになります。


== CRUD操作の実装

ユーザーが入力したコマンドを解析し、実行する処理を決定できるようになりましたが、この時点ではデータベースとしての機能はまだありません。
そこで本節では、データベースの基本操作である@<b>{CRUD}を実装します。
CRUDとは、データを扱うための4つの基本操作の頭文字を取ったものです。

//emlist{
| 操作     | コマンド     | 説明       |
| ------ | -------- | -------- |
| Create | `insert` | データを登録する |
| Read   | `select` | データを取得する |
| Update | `update` | データを更新する |
| Delete | `delete` | データを削除する |
//}

多くのデータベースは、これら4つの操作を基本として構成されています。本書でも、まずはこの最小限の機能を実装していきます。


=== insert ― データを登録する

新しいデータを登録するには、`insert` コマンドを使用します。
例えば、

//emlist{
db > insert 1 Alice
//}

と入力すると、

//emlist{
キー：1
値：Alice
//}

というデータをデータベースへ登録します。
REPLでは、コマンド名と引数の数を確認し、`insert()` メソッドを呼び出します。
insert()ではキーの重複チェックを行い、`HashMap`にキーと値の組み合わせを追加します。

//emlist{
if (db.putIfAbsent(id, value) != null) {
    System.out.println("Key already exists.");
    return;
}
//}



=== select ― データを取得する

登録したデータを確認するには、`select` コマンドを使用します。
`select` には2つの使い方があります。

1つ目の使い方は、キーを指定せず、登録されているすべてのデータを取得します。

//emlist{
db > select
(1,Alice)
(2,Bob)
//}

実装では、`HashMap` のすべての要素を順番に取り出して表示します。

//emlist{
db.forEach((id, name) ->
    System.out.println("(" + id + "," + name + ")"));
//}

2つ目の使い方は、キーを指定し、そのデータだけを取得します。

//emlist{
db > select 2
Bob
//}

実装では、指定されたキーの存在を確認し、`HashMap`から取り出して表示します。

//emlist{
if (!db.containsKey(id)) {
      System.out.println("Record not found.");
    } else {
      System.out.println(db.get(id));
    }
//}


=== update ― データを更新する

登録済みのデータを変更するには、`update` コマンドを使用します。
例えば、

//emlist{
db > update 2 Robert
//}

を実行すると、ID=2に対応する値が

//emlist{
Bob
//}

から

//emlist{
Robert
//}

へ変更されます。
`update()`メソッドでは、まず対象のキーが存在するかを確認します。

//emlist{
if (!db.containsKey(id)) {
    System.out.println("Record not found.");
    return;
}
//}

存在する場合は、

//emlist{
db.put(id, value);
//}

によって値を上書きします。



=== delete ― データを削除する

不要になったデータは、`delete` コマンドで削除します。
例えば、

//emlist{
db > delete 1
//}

を実行すると、
ID=1のレコードがデータベースから削除されます。
`delete()`では、`update()`メソッドと同様に、まず対象のキーが存在するか確認します。
存在する場合は、

//emlist{
db.remove(id);
//}

によってレコードを削除します。



=== 動作確認

ここまで実装すると、データベースはCRUD操作を一通り実行できるようになります。

//emlist{
Welcome to nekoDB!

db > insert 1 Alice
Inserted.

db > insert 2 Bob
Inserted.

db > select
(1,Alice)
(2,Bob)

db > update 2 Robert
Updated.

db > select 2
Robert

db > delete 1
Deleted.

db > select
(2,Robert)
//}

登録したデータを取得し、更新し、不要になれば削除できることが確認できます。



== Mapによるデータ管理

前節では、`insert`、`select`、`update`、`delete` の4つの基本操作を実装しました。
これらの処理では、データを保存するための共通の仕組みとして@<b>{Map}を利用しています。
本節では、Mapがどのようなデータ構造であり、データベースの内部でどのような役割を果たしているのかを見ていきます。


=== Mapとは

Javaの `Map` は、「キー」と「値」の組を管理するデータ構造です。
例えば、

//emlist{
| キー(ID) | 値(Name) |
| ------ | ------- |
| 1      | Alice   |
| 2      | Bob     |
| 3      | Carol   |
//}

というデータは、Mapでは次のように表現できます。

//emlist{
1 → Alice
2 → Bob
3 → Carol
//}

本書では、このキーをレコードのID、値をレコードの内容として扱います。
プログラムでは、データベース本体を次のように宣言しています。

//emlist{
private Map<Integer, String> db;
//}

そして、コンストラクタで

//emlist{
db = new HashMap<>();
//}

を実行することで、空のデータベースを作成しています。


=== なぜHashMapを使うのか

Mapにはさまざまな実装がありますが、本書では `HashMap` を採用しています。
その理由は、指定したキーに対応するデータを高速に検索できるためです。
例えば、

//emlist{
db > select 2
//}

を実行すると、

//emlist{
db.get(2);
//}

によってIDが2のデータを取得できます。
データ数が増えても、キーを指定した検索は効率的に行えるため、小規模なデータベースの実装には適しています。
なお、本章ではMapの内部構造までは扱いません。重要なのは、「キーを指定すると対応する値を取得できる」という特徴です。

そして、前節で実装した4つの操作は、すべてMapの基本操作に対応しています。

//emlist{
| データベース操作 | Mapの操作          |
| -------- | --------------- |
| insert   | `putIfAbsent()` |
| select   | `get()`         |
| update   | `put()`         |
| delete   | `remove()`      |
//}




== 1.5 最小DBの動作確認

ここまでで、データベースとして最低限の機能が完成しました。
実装した機能は次の4つです。

//emlist{
| コマンド     | 機能       |
| -------- | -------- |
| `insert` | データを登録する |
| `select` | データを取得する |
| `update` | データを更新する |
| `delete` | データを削除する |
//}

また、これらのデータは `HashMap` を利用してメモリ上に管理されています。
本節では、実際にデータベースを操作しながら、期待どおりに動作することを確認します。


=== データを登録する

まずは、`insert` コマンドを使ってデータを登録します。

//emlist{
Welcome to nekoDB!

db > insert 1 Alice
Inserted.

db > insert 2 Bob
Inserted.

db > insert 3 Carol
Inserted.
//}

`Inserted.` と表示されれば、データは正常に `HashMap` へ登録されています。


=== データを取得する

登録したデータを確認するために、`select` コマンドを実行します。
引数を指定しない場合は、すべてのデータが表示されます。

//emlist{
db > select

(1,Alice)
(2,Bob)
(3,Carol)
//}

特定のデータだけを取得することもできます。

//emlist{
db > select 2

Bob
//}

キーを指定すると、そのキーに対応するデータだけが表示されます。
存在しないキーを指定した場合は、

//emlist{
db > select 10

Record not found.
//}

となります。


=== データを更新する

続いて、登録済みのデータを更新します。

//emlist{
db > update 2 Robert

Updated.
//}

再び検索すると、

//emlist{
db > select 2

Robert
//}

となり、値が更新されていることが確認できます。
存在しないキーを更新しようとすると、

//emlist{
db > update 10 David

Record not found.
//}

と表示されます。


=== データを削除する

最後に、不要になったデータを削除してみます。

//emlist{
db > delete 1

Deleted.
//}

その後、すべてのデータを表示すると、

//emlist{
db > select

(2,Robert)
(3,Carol)
//}

となり、IDが1のデータだけが削除されていることが確認できます。
削除対象が存在しない場合は、

//emlist{
db > delete 5

Record not found.
//}

と表示されます。


=== エラー入力の確認

これまでの例では、正しいコマンドだけを入力してきました。
しかし、実際には誤った入力が行われることもあります。
例えば、存在しないコマンドを入力すると、

//emlist{
db > hello

Unknown command
//}

と表示されます。
また、

//emlist{
db > insert abc Alice

Error: Key must be an integer.
//}

のように、キーへ整数以外を指定した場合も、エラーメッセージを表示して処理を終了します。
このように入力内容を検査することで、不正なデータが登録されないようになっています。


=== 現在のデータベースの問題点

ここまでで、CRUD操作を行えるデータベースが完成しました。
しかし、このデータベースには大きな問題があります。
例えば、データを登録したあとにプログラムを終了します。

//emlist{
db > insert 1 Alice

Inserted.

db > exit
//}

再びプログラムを起動すると、新しい `HashMap` が生成されます。
そのため、

//emlist{
db > select
//}

を実行しても、

//emlist{
(何も表示されない)
//}

となり、先ほど登録したデータはすべて失われています。
これは、現在のデータベースがデータを@<b>{メモリ上にしか保持していない}ためです。
プログラムが終了すると、メモリ上のデータも同時に破棄されてしまいます。
実際のデータベースでは、サーバーを停止したりプログラムを終了したりしても、登録したデータは保持されなければなりません。
そのためには、メモリだけでなく、@<b>{ディスク上のファイルへデータを保存する仕組み}が必要になります。


=== この章のまとめ

本章では、Javaの `HashMap` を利用して、データベースの最小構成を実装しました。
ユーザーからの入力をREPLで受け付け、コマンドを解析し、CRUD操作を実行することで、「データを登録・取得・更新・削除する」というデータベースの基本機能を実現しました。
ここまでの処理の流れをまとめると、次のようになります。

//emlist{
          ユーザー
             │
             ▼
         REPLで入力
             │
             ▼
     SimpleParserで解析
             │
             ▼
      CRUDメソッドを実行
             │
             ▼
          HashMap
             │
             ▼
        メモリ上に保存
//}

この構成はシンプルで理解しやすく、小規模なデータベースとして十分に機能します。しかし、データがメモリ上にしか存在しないため、プログラムを終了するとすべて失われてしまいます。
@<b>{次章では、この問題を解決するために、データをファイルへ保存する「永続化」の仕組みを実装します。}これにより、プログラムを再起動してもデータを保持できる、本格的なデータベースへと一歩近づいていきます。
