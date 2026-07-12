= 最小のデータベース

前章では、データベースを段階的に作り上げていくロードマップを示しました。
本章では、その第一段階として、まずは対話型のプログラム（REPL）を作り、メモリ上だけで動く最小のデータベースを作ります。


== 対話型プログラム（REPL）の作成

データベースは、一度起動したらすぐに終了するプログラムではありません。
ユーザーからの入力を待ち続け、入力された命令を実行し、その結果を表示することを繰り返します。
例えば、多くのデータベースでは次のような対話形式で操作を行います。

//emlist{
> SELECT * FROM users;
1|Alice|20
2|Bob|18
//}

このようなプログラムは@<b>{REPL（Read-Eval-Print Loop）}と呼ばれます。
REPLとは、以下の4つの処理から構成されます。

 * @<b>{Read}：ユーザーから入力を受け取る
 * @<b>{Eval}：入力された命令を実行する
 * @<b>{Print}：実行結果を表示する
 * @<b>{Loop}：これらを繰り返す

本書で作成するデータベースも、このREPL形式を採用します。

==== 最初のデータベース

まずはデータベース本体となるnekoDBクラスを作成します。
コンストラクタでは、データを保持するためのHashMap、キーボード入力を受け取るScanner、入力を解析するためのSimpleParserを初期化します。
//emlist{
public nekoDB() {
    db = new HashMap<>();
    scanner = new Scanner(System.in);
    parser = new SimpleParser();
    System.out.println("Welcome to nekoDB!");
}
//}
現時点では、HashMapがデータベースそのものになります。
プログラムを起動すると、以下のメッセージが表示され、データベースの開始を知らせます。
//emlist{
Welcome to nekoDB!
//}

==== REPLの実装

データベースが起動したら、ユーザーが終了を指示するまで入力を受け付け続けます。
そのために、nekoDBクラスでは無限ループを用意しています。

//emlist{
public void start() {
    while (true) {
        System.out.print("db > ");

        if (!scanner.hasNextLine()) {
            break;
        }

        String input = scanner.nextLine();

        // 後続の処理（中略）
    }
}
//}

このコードを実行すると、画面には次のように表示され、ユーザーの入力待ち状態になります。
ここでユーザーがコマンドを入力すると、入力された文字列が1行分読み込まれます。

//emlist{
db >
//}


== 入力とコマンドの扱い

前節ではユーザーからの入力を受け付けるREPLを作成しました。
しかし、この時点では入力された文字列は単なる文字列であり、プログラムはその意味を理解できません。
例えば、@<code>{insert 1 Alice}という入力があったとしても、プログラムにとっては@<code>{"insert 1 Alice"}という一つの文字列に過ぎません。
本節では、この文字列を解釈するための処理を実装していきます。

データベースとして動作させるためには、REPLにて入力を受け付けた文字列を

 * 実行するコマンド
 * 操作対象のデータ

に分けて解釈する必要があります。
本書では、この役割を@<b>{SimpleParser}クラスが担当します。
まず、入力された文字列を解析する処理をSimpleParserクラスに実装します。

//emlist{
public String[] parse(String sql) {
    String[] tokens = sql.trim().split("\\s+");
    tokens[0] = tokens[0].toLowerCase();
    return tokens;
}
//}

入力の解析結果は次のようになり、コマンドと引数を個別に扱えるようになります。

//emlist{
| 入力         | 分割結果              |
| ------------ | -------------------- |
|insert 1 Alice|["insert","1","Alice"]|
|select        |["select"]            |
|delete 5      |["delete","5"]        |
//}


データベースが最初に知りたいのは、「どの操作を実行するのか」です。
そこで、分割した配列の最初の要素をコマンドとして取り出します。

//emlist{
public String getCommand(String[] tokens) {
    return tokens.length > 0 ? tokens[0] : "";
}
//}

例えば、@<code>{["insert", "1", "Alice"]}であれば、@<code>{"insert"}が返されます。
これにより、以降の処理では入力全体を調べる必要がなくなり、コマンド名だけを見て処理を分岐できるようになります。

nekoDBクラスで、取得したコマンド名をもとに処理を切り替えるよう実装します。

//emlist{
public void start() {
    while (true) {
      // ユーザーの入力待ち（中略）

      String[] tokens = parser.parse(scanner.nextLine());
      String command = parser.getCommand(tokens);

      // コマンドが空の場合はスキップ
      if (command.isEmpty()) {
        continue;
      } else if (command.equals("insert") && tokens.length == 3) {
        insert(tokens[1], tokens[2]);
      } else if (command.equals("select")) {
        if (tokens.length == 1) select();
        else if (tokens.length == 2) select(tokens[1]);
      } else if (command.equals("update") && tokens.length == 3) {
        update(tokens[1], tokens[2]);
      } else if (command.equals("delete") && tokens.length == 2) {
        delete(tokens[1]);
      } else if (command.equals("exit")) {
        System.out.println("Bye!");
        break;
      } else {
        System.out.println("Unknown command");
      }
    }
}
//}

上記の分岐処理によって、例えば@<code>{insert 1 Alice}と入力すると、

//emlist{
command = "insert"
tokens[1] = "1"
tokens[2] = "Alice"
//}

と解析され、@<code>{insert("1", "Alice");}が呼び出されます。
このように、入力された文字列を解析し、適切なメソッドへ処理を振り分けることで、データベースはさまざまな命令を実行できるようになります。


== CRUD操作の実装

前節までで、ユーザーが入力したコマンドを解析し、実行する処理を決定できるようになりました。
しかし、この時点ではデータベースとしての機能はまだありません。
そこで本節では、データベースの基本操作である@<b>{CRUD}を実装します。
CRUDとは、データを扱うための4つの基本操作の頭文字を取ったものです。

//emlist{
| 操作   | コマンド | 説明            |
| ------ | ------- | -------------- |
| Create | insert  | データを登録する |
| Read   | select  | データを取得する |
| Update | update  | データを更新する |
| Delete | delete  | データを削除する |
//}

多くのデータベースは、これら4つの操作を基本として構成されています。本書でも、まずはこの最小限の機能を実装していきます。


==== insert
新しいデータを登録するには、@<b>{insert}コマンドを使用します。
例えば、

//emlist{
db > insert 1 Alice
//}

と入力すると、REPLではコマンド名と引数の数を確認し、insert()メソッドを呼び出します。

//emlist{
if (db.putIfAbsent(id, value) != null) {
    System.out.println("Key already exists.");
    return;
}
//}

insert()メソッドではキーの重複チェックを行い、HashMapに以下のようなキーと値の組み合わせを追加します。

//emlist{
キー：1
値：Alice
//}


==== select

登録したデータを確認するには、@<b>{select}コマンドを使用します。
selectコマンドには2つの使い方があります。

1つ目の使い方は、キーを指定せず、登録されているすべてのデータを取得します。

//emlist{
db > select
(1,Alice)
(2,Bob)
//}

実装では、HashMapのすべての要素を順番に取り出して表示します。

//emlist{
db.forEach((id, name) ->
    System.out.println("(" + id + "," + name + ")"));
//}

2つ目の使い方は、キーを指定し、そのデータだけを取得します。

//emlist{
db > select 2
Bob
//}

実装では、指定されたキーの存在を確認し、HashMapから取り出して表示します。

//emlist{
if (!db.containsKey(id)) {
      System.out.println("Record not found.");
    } else {
      System.out.println(db.get(id));
    }
//}



==== update

登録済みのデータを変更するには、@<b>{update}コマンドを使用します。
例えば、

//emlist{
db > update 2 Robert
//}

を実行すると、ID=2に対応する値がBobからRobertへ変更されます。
updateでは、まず対象のキーが存在するかを確認し、キーが存在する場合のみ、対応する値を上書きします。

//emlist{
if (!db.containsKey(id)) {
    System.out.println("Record not found.");
    return;
}
db.put(id, value);
//}


==== delete 

不要になったデータは、@<b>{delete}コマンドで削除します。
例えば、

//emlist{
db > delete 1
//}

を実行すると、
ID=1と対応する値の組み合わせがデータベースから削除されます。
deleteでは、updateと同様に、まず対象のキーが存在するか確認します。
そして、キーが存在する場合のみ対象のキーと値の組み合わせを削除します。

//emlist{
db.remove(id);
//}



== Mapによるデータ管理

前節では、insert、select、update、deleteの4つの基本操作を実装しました。
これらの処理では、データを保存するための共通の仕組みとして@<b>{Map}を利用しています。
本節では、Mapがどのようなデータ構造であり、データベースの内部でどのような役割を果たしているのかを見ていきます。


==== Mapとは

JavaのMapは、「キー」と「値」の組を管理するデータ構造です。
例えば、

//emlist{
| キー(ID) | 値(Name) |
| ------- | -------- |
| 1       |  Alice   |
| 2       |  Bob     |
| 3       |  Carol   |
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
public class nekoDB {
    // データベース本体（キーはID、値は文字列）
    private Map<Integer, String> db;
    public nekoDB() {
        db = new HashMap<>();
        // その他の初期化処理（中略）
    }
    // その他のメソッド（中略）
}
//}


==== なぜHashMapを使うのか

Mapにはさまざまな実装がありますが、本書ではHashMapを採用しています。
その理由は、指定したキーに対応する値を高速に検索できるためです。
例えば、

//emlist{
db > select 2
//}

を実行すると、

//emlist{
db.get(2);
//}

によってID=2に対応する値を取得できます。
データ数が増えても、キーを指定した検索は効率的に行えるため、小規模なデータベースの実装には適しています。
なお、本章ではMapの内部構造までは扱いません。重要なのは、「キーを指定すると対応する値を取得できる」という特徴です。

そして、前節で実装した4つの操作は、すべてMapの基本操作に対応しています。

//emlist{
| DB操作 | Mapの操作    |
| ------ | ----------- |
| insert |putIfAbsent()|
| select |get()        |
| update |put()        |
| delete |remove()     |
//}







== まとめと次章への課題

本章では、JavaのHashMapを利用して、データベースの最小構成を実装しました。
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

この構成はシンプルで理解しやすく、小規模なデータベースとして十分に機能します。
しかし、データがメモリ上にしか存在しないため、プログラムを終了するとすべて失われてしまいます。
実際のデータベースでは、サーバーを停止したりプログラムを終了したりしても、登録したデータは保持されなければなりません。
そのため、次章ではこの問題を解決するために、データをファイルへ保存する@<b>{「永続化」}の仕組みを実装します。
これにより、プログラムを再起動してもデータを保持できる、本格的なデータベースへと一歩近づいていきます。
