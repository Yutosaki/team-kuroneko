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

現時点では、このHashMapがデータベースそのものになります。
プログラムを起動すると、以下のメッセージが表示され、データベースの開始を知らせます。

//emlist{
Welcome to nekoDB!
//}

==== REPLの実装

データベースが起動したら、ユーザーが終了を指示するまで入力を受け付け続けます。
そのために、nekoDBクラスのstartメソッドに無限ループを用意しています。

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

データベースとして動作させるためには、REPLにて入力を受け付けた文字列を、

 * 実行するコマンド
 * 操作対象のデータ（引数）

に分けて解釈する必要があります。
本書では、この役割を@<b>{SimpleParser}クラスが担当します。
まず、入力された文字列を空白で分割する処理をSimpleParserクラスに実装します。

//emlist{
public String[] parse(String sql) {
    String[] tokens = sql.trim().split("\\s+");
    tokens[0] = tokens[0].toLowerCase();
    return tokens;
}
//}

入力の解析結果は次のようになり、コマンドと引数を個別に扱えるようになります。

//emlist{
| 入力         | 分割結果             |
| ------------ | -------------------- |
|insert 1 Alice|["insert","1","Alice"]|
|select        |["select"]            |
|delete 5      |["delete","5"]        |
//}

データベースが最初に知りたいのは、「どの操作を実行するのか」です。
そこで、分割した配列の最初の要素をコマンド名として取り出します。

//emlist{
public String getCommand(String[] tokens) {
    return tokens.length > 0 ? tokens[0] : "";
}
//}

例えば、@<code>{["insert", "1", "Alice"]}であれば、@<code>{"insert"}が返されます。
これにより、以降の処理では入力全体を調べる必要がなくなり、コマンド名だけを見て処理を分岐できるようになります。

nekoDBクラスに戻り、取得したコマンド名をもとに処理を切り替えるよう実装します。
IDは数値として扱うため、引数（文字列）を @<code>{Integer.parseInt()} で整数型に変換して各メソッドに渡します。

//emlist{
public void start() {
    while (true) {
        // ユーザーの入力待ち（中略）

        String[] tokens = parser.parse(scanner.nextLine());
        String command = parser.getCommand(tokens);

        // コマンドが空の場合はスキップ
        if (command.isEmpty()) {
            continue;
        }

        try {
            if (command.equals("insert") && tokens.length == 3) {
                int id = Integer.parseInt(tokens[1]);
                insert(id, tokens[2]);
            } else if (command.equals("select")) {
                if (tokens.length == 1) select();
                else if (tokens.length == 2) {
                    int id = Integer.parseInt(tokens[1]);
                    select(id);
                }
            } else if (command.equals("update") && tokens.length == 3) {
                int id = Integer.parseInt(tokens[1]);
                update(id, tokens[2]);
            } else if (command.equals("delete") && tokens.length == 2) {
                int id = Integer.parseInt(tokens[1]);
                delete(id);
            } else if (command.equals("exit")) {
                System.out.println("Bye!");
                break;
            } else {
                System.out.println("Unknown command");
            }
        } catch (NumberFormatException e) {
            System.out.println("Error: ID must be a number.");
        }
    }
}
//}

この分岐処理によって、例えば@<code>{insert 1 Alice}と入力すると、@<code>{tokens[1]}の"1"が数値の1に変換され、@<code>{insert(1, "Alice");}が呼び出されます（数値に変換できない文字が入力された場合はエラーを返します）。
このように、入力された文字列を解析し、適切なメソッドへ処理を振り分けることで、データベースはさまざまな命令を実行できるようになります。

== CRUD操作の実装

前節までで、ユーザーが入力したコマンドを解析し、実行する処理を決定できるようになりました。
しかし、この時点ではデータベースの実体としての機能はまだありません。
そこで本節では、データベースの基本操作である@<b>{CRUD}を実装します。
CRUDとは、データを扱うための4つの基本操作の頭文字を取ったものです。

//emlist{
| 操作   | コマンド | 説明           |
| ------ | ------- | -------------- |
| Create | insert  | データを登録する |
| Read   | select  | データを取得する |
| Update | update  | データを更新する |
| Delete | delete  | データを削除する |
//}

多くのデータベースは、これら4つの操作を基本として構成されています。本書でも、まずはこの最小限の機能を実装していきます。

==== insert

新しいデータを登録するには、@<b>{insert}コマンドを使用します。

//emlist{
db > insert 1 Alice
//}

insertメソッドでは、キー（ID）の重複チェックを行い、HashMapにキーと値の組み合わせを追加します。

//emlist{
public void insert(int id, String name) {
    if (db.putIfAbsent(id, name) != null) {
        System.out.println("Key already exists.");
    } else {
        System.out.println("Inserted.");
    }
}
//}

==== select

登録したデータを確認するには、@<b>{select}コマンドを使用します。
selectコマンドには2つの使い方（引数なし、引数あり）があるため、メソッドをオーバーロード（同名で引数が違うメソッドを定義）して実装します。

1つ目の使い方は、キーを指定せず、登録されているすべてのデータを取得します。

//emlist{
db > select
(1,Alice)
(2,Bob)
//}

実装では、HashMapのすべての要素を順番に取り出して表示します。

//emlist{
public void select() {
    db.forEach((id, name) ->
        System.out.println("(" + id + "," + name + ")"));
}
//}

2つ目の使い方は、キー（ID）を指定し、そのデータだけを取得します。

//emlist{
db > select 2
Bob
//}

実装では、指定されたキーの存在を確認し、HashMapから取り出して表示します。

//emlist{
public void select(int id) {
    if (!db.containsKey(id)) {
        System.out.println("Record not found.");
    } else {
        System.out.println(db.get(id));
    }
}
//}

==== update

登録済みのデータを変更するには、@<b>{update}コマンドを使用します。

//emlist{
db > update 2 Robert
//}

updateメソッドでは、まず対象のキーが存在するかを確認し、存在する場合のみ対応する値を上書きします。

//emlist{
public void update(int id, String name) {
    if (!db.containsKey(id)) {
        System.out.println("Record not found.");
        return;
    }
    db.put(id, name);
    System.out.println("Updated.");
}
//}

==== delete 

不要になったデータは、@<b>{delete}コマンドで削除します。

//emlist{
db > delete 1
//}

deleteメソッドでも同様に、対象のキーが存在するか確認してから削除処理を行います。

//emlist{
public void delete(int id) {
    if (db.remove(id) == null) {
        System.out.println("Record not found.");
    } else {
        System.out.println("Deleted.");
    }
}
//}

== ハッシュテーブルによるオンメモリ管理

前節では、insert、select、update、deleteの4つの基本操作を実装しました。
これらの処理では、データを保存するための共通の仕組みとしてJavaの@<b>{Map}を利用しています。
本節では、これがどのようなデータ構造であり、データベースの内部でどのような役割を果たしているのかを見ていきます。

==== ハッシュテーブル（Map）とは

JavaのMapは、コンピュータサイエンスにおける「連想配列」や「ハッシュテーブル」と呼ばれる、キーと値のペアを管理するデータ構造の実装です。
例えば、次のようなデータがあるとします。

//emlist{
| キー(ID) | 値(Name) |
| ------- | -------- |
| 1       |  Alice   |
| 2       |  Bob     |
| 3       |  Carol   |
//}

Mapを使うと、この表のような対応関係をそのままプログラム上で表現できます。
本書では、このキーを「レコードのID」、値を「レコードの内容」として扱います。
プログラムでは、データベース本体を次のように宣言しています。

//emlist{
public class nekoDB {
    // データベース本体（キーは数値型のID、値は文字列）
    private Map<Integer, String> db;

    public nekoDB() {
        db = new HashMap<>();
        // その他の初期化処理（中略）
    }
    // その他のメソッド（中略）
}
//}

==== なぜHashMapを使うのか

Mapにはいくつかの実装種類がありますが、本書ではハッシュテーブルを利用した @<code>{HashMap} を採用しています。
その最大の理由は、指定したキーに対応する値を「極めて高速に検索できる」ためです。

//emlist{
db > select 2
//}

を実行すると、内部では @<code>{db.get(2);} が呼ばれます。
HashMapは内部的にハッシュ関数という仕組みを使ってデータの保存場所を一瞬で特定するため、データ数が数件でも数十万件に増えても、キーを指定した検索にかかる時間はほとんど変わりません（計算量 @<m>{O(1)}）。

前節で実装した4つの操作は、すべてこのMapの基本メソッドに1対1で対応しています。

//emlist{
| DB操作 | Mapの操作    |
| ------ | ----------- |
| insert |putIfAbsent()|
| select |get()        |
| update |put()        |
| delete |remove()     |
//}

キーによる検索が圧倒的に速いHashMapは、このようにオンメモリ（メモリ上）で動作する小規模なデータベースの基盤として非常に適しているのです。

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
