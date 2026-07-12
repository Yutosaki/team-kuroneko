= ページとスロットによるデータ配置の改善

== 第3章までの課題：ファイル全体の書き直し

第3章では、プログラムを終了してもデータが消えないように、レコードをファイルへ保存する仕組みを作りました。

一方で、insert, update, delete によりレコードを1件でも変更したら「ファイル全体を書き直している」という点が非効率です。10000件のレコードがある状態で1件を更新すると、変更のなかった9999件も含めてすべてを書き出すことになります。書き込む量はレコードの総数に比例して増えていくため、データが増えるほど1回の更新が重くなっていきます。

これは保存の単位がファイル全体になっているためです。データベースが扱うデータは、ファイルの先頭から末尾まで1本のテキストとして並んでいるだけなので、途中の1件だけを差し替える手段がありません。

== 本章の方針：ページ単位での読み書き

この課題を解決するには、保存の単位をファイル全体よりも小さくし、変更したい部分だけを読み書きできるようにする必要があります。

そこで本章では@<b>{ページ}を導入し、更新のたびにファイル全体を書き直すのではなく、@<b>{変更のあった部分だけを読み書きする}実装へと作り替えます。具体的には、次の順序で進めます。

 1. @<b>{ページ単位の導入}: ファイルを一定サイズの固定長の単位に区切り、目的の場所へ直接アクセスできるようにします。
 2. @<b>{スロットによるレコード設計}: 1ページの中を「スロット」という固定長の区画に分割し、レコードを確実な位置へ格納できるようにします。
 3. @<b>{CRUDの実装}: ページ単位の読み書きを用いて、必要な分だけのディスクI/OでCRUD操作を完結させます。

この作り替えにより、レコードを1件変更するときにファイルへ書き込む量は、データの総数に関わらず「1ページ分」で一定になります。次節ではまず、ページという単位の導入から見ていきます。

== ページの導入と固定長スロットによるレコードの設計

ページとは、ファイルを一定の大きさで区切った固定長の単位のことです。本書では1ページを4096バイト（4KB）とし、ファイルの先頭から4096バイトずつ「ページ0」「ページ1」「ページ2」……と区切って管理します。

固定長であることの最大のメリットは、計算によって位置を即座に特定できることです。あるページのファイルの先頭からのバイト位置（オフセット）は「ページ番号 @<m>{\times 4096}」で計算できます。そのため、Javaの @<code>{RandomAccessFile} の @<code>{seek()} メソッドを使えば、目的のページへ直接移動し、そのページだけを読み書きできるようになります。

//image[chapter04_image01][ページとスロットの構成]{
//}

さらに、1ページ（4096バイト）の中を、64バイトずつの区画に細かく分割します。この1区画を@<b>{スロット}と呼び、1つのスロットに1件のレコードを格納します。1ページは @<m>{4096 / 64 = 64} 個のスロットを持つことになります。

ページが固定長であることと同様に、スロットも固定長であることで、s番目のスロットの位置は「s @<m>{\times 64}」で即座に計算できます。スロットの64バイトの内訳を以下に示します。

//image[chapter04_image02][スロットの内訳]{
//}

先頭の1バイトは@<b>{有効フラグ}です。そのスロットが使用中か空きかを表し、レコードを読み書きするときはまずこのフラグを確認します。続く4バイトにキー（整数）、次の4バイトに値のバイト数、残りの55バイトに値の本体を格納します。@<code>{1 + 4 + 4 + 55} でちょうど64バイトになります。

Javaでこのバイト列を組み立てるときは、スロットの開始位置（@<m>{スロット番号 \times スロットサイズ}）を先頭として、フラグ・キー・長さ・本体の順にデータを書き込んでいきます。

//emlist[スロットへ1件のレコードを書き込む][java]{
int offset = targetSlot * SLOT_SIZE;
ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
bb.put((byte) 1); // 有効フラグをセット
bb.putInt(id); // キーをセット

byte[] valBytes = value.getBytes(StandardCharsets.UTF_8);
int len = Math.min(valBytes.length, 55); // 最大55バイトに制限
bb.putInt(len);
bb.put(valBytes, 0, len);
//}

値は @<code>{Math.min(valBytes.length, 55)} で最大55バイトに切り詰めています。読み出すときは、保存しておいた「値の長さ」の分だけバイトを取り出して文字列に戻します。

== ページ単位での読み書きの実装

スロットの設計が決まったので、CRUD操作をページ単位の読み書きで実装していきます。4つのコマンドはどれも、@<b>{ページを読み込む → スロットを調べる（または書き換える）→ 必要ならページを書き戻す}という共通の流れで動きます。書き込みが必要なときでも、ファイルへ書き戻すのは変更のあった1ページだけです。

以降、コマンドごとに処理の流れを順を追って見ていきます。

==== insert

 1. 現在のファイルサイズから、ページ数を求める。
 2. すべてのページ・スロットを調べ、同じキーがすでに存在しないか確認し、最初に見つかった空きスロットの位置を覚えておく。
 3. 空きスロットが1つも無ければ、新しいページを1枚増やしてその先頭スロットを使う。
 4. 書き込み先のページを読み込み、スロットにレコードを書き込む。
 5. そのページだけをファイルへ書き戻す。
//emlist[重複チェックと空きスロットの探索][java]{

int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

int targetPage = -1;
int targetSlot = -1;

for (int p = 0; p < numPages; p++) {
  byte[] buf = new byte[PAGE_SIZE];
  file.seek((long) p * PAGE_SIZE);
  file.read(buf);

  for (int s = 0; s < MAX_SLOTS; s++) {
    int offset = s * SLOT_SIZE;
    if (buf[offset] == 1) { // 使用中スロット
      ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
      bb.get(); // 有効フラグをスキップ
      if (bb.getInt() == id) {
        System.out.println("Key already exists. Use update command to modify.");
        return;
      }
    } else if (buf[offset] == 0 && targetPage == -1) { // 最初の空きスロットを記録
      targetPage = p;
      targetSlot = s;
    }
  }
}

// 空きがない場合は新しいページを用意する
if (targetPage == -1) {
  targetPage = numPages;
  targetSlot = 0;
}
//}

書き込み先が決まったら、そのページを読み込み、スロットにレコードを書き込みます。1件を書き込む処理は前述の通り、有効フラグ・キー・値の長さ・値の本体の順に @<code>{ByteBuffer} で並べるだけです。最後に、変更したページだけをファイルへ書き戻します。

//emlist[対象ページを読み込み、書き込み、書き戻す][java]{
// 対象のページを読み込む（新規ページなら読み込みは不要）
byte[] buf = new byte[PAGE_SIZE];
if (targetPage < numPages) {
  file.seek((long) targetPage * PAGE_SIZE);
  file.read(buf);
}

// スロットにレコードを書き込む
int offset = targetSlot * SLOT_SIZE;
ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
bb.put((byte) 1); // 有効フラグ
bb.putInt(id); // キー
byte[] valBytes = value.getBytes(StandardCharsets.UTF_8);
int len = Math.min(valBytes.length, 55);
bb.putInt(len); // 値の長さ
bb.put(valBytes, 0, len); // 値の本体

// 対象ページだけをファイルに上書き保存
file.seek((long) targetPage * PAGE_SIZE);
file.write(buf);
//}

第3章では更新のたびに全レコードを書き出していましたが、ここで最後にファイルへ書き込んでいるのは4096バイト、つまり1ページ分だけです。データベース全体に何万件のレコードがあっても、1回の insert で書き込む量は常に1ページで一定になります。ファイル全体の書き直しという無駄は、これで解消できました。

==== select

フラグが0のスロットは空きなので飛ばし、使用中のスロットだけを表示します。

//emlist[全件を表示する select][java]{
for (int p = 0; p < numPages; p++) {
  byte[] buf = new byte[PAGE_SIZE];
  file.seek((long) p * PAGE_SIZE);
  file.read(buf);

  for (int s = 0; s < MAX_SLOTS; s++) {
    int offset = s * SLOT_SIZE;
    if (buf[offset] == 1) { // 使用中スロットのみ読み取る
      ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
      bb.get(); // フラグスキップ

      int key = bb.getInt();
      int valLen = bb.getInt();
      byte[] valBytes = new byte[valLen];
      bb.get(valBytes);
      String value = new String(valBytes, StandardCharsets.UTF_8);
      System.out.println("(" + key + "," + value + ")");
    }
  }
}
//}

キーを指定した検索も、ページとスロットを順に調べていく流れは同じです。

//emlist[キーを指定した検索][java]{
for (int p = 0; p < numPages; p++) {
  byte[] buf = new byte[PAGE_SIZE];
  file.seek((long) p * PAGE_SIZE);
  file.read(buf);

  for (int s = 0; s < MAX_SLOTS; s++) {
    int offset = s * SLOT_SIZE;
    if (buf[offset] == 1) {
      ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
      bb.get(); // フラグスキップ
      int recordKey = bb.getInt();

      if (recordKey == id) {
        int valLen = bb.getInt();
        byte[] valBytes = new byte[valLen];
        bb.get(valBytes);
        String value = new String(valBytes, StandardCharsets.UTF_8);
        System.out.println(value);
        return;
      }
    }
  }
}
System.out.println("Record not found.");
//}

使用中スロットのキーが指定したキーと一致すれば、値を読み出して表示し、すぐに @<code>{return} で探索を打ち切ります。最後まで調べても見つからなければ @<code>{Record not found.} と表示します。ここで注目したいのは、@<b>{目的のレコードが後ろのページにあるほど、それまでのページをすべて読み込んでから見つかる}という点です。この性質が、本章の最後で述べる課題につながります。

==== update

 update操作は、対象を見つけたら同じスロットに新しい値を上書きし、そのページだけをファイルへ書き戻します。レコードの位置は変わらないため、他のスロットに影響を与えません。

//emlist[レコードの値を上書きする][java]{
if (recordKey == id) {
  // 対象を見つけたら同じスロットに上書き
  byte[] valBytes = value.getBytes(StandardCharsets.UTF_8);
  int len = Math.min(valBytes.length, 55);

  bb.putInt(len);
  bb.put(valBytes, 0, len);

  // 対象ページだけを保存
  file.seek((long) p * PAGE_SIZE);
  file.write(buf);

  System.out.println("Updated correctly");
  return;
}
//}

==== delete

スロットの中身を実際に消したり、後ろのレコードを前に詰めたりはしません。その代わり、@<b>{スロット先頭の有効フラグを0にするだけ}で削除とみなします。これを@<b>{論理削除}と呼びます。

//emlist[有効フラグを0にする論理削除][java]{
if (recordKey == id) {
  // 対象を見つけたら、先頭フラグを0にして論理削除
  buf[offset] = 0;

  file.seek((long) p * PAGE_SIZE);
  file.write(buf);

  System.out.println("Deleted and saved to disk.");
  return;
}
//}

== 処理時間の計測とパフォーマンス評価

insert, update, delete のたびにレコード全体をファイルに書き出していた前章と、ページとスロットを導入した本章の処理時間を比較してみましょう。

//table[performance_comparison][実行時間の比較（1万件のデータが存在する状態）]{
コマンド	前章（全件書き直し）	本章（ページ管理）
-------------------------------------------------------------
@<code>{select 5000}	0.315 ms	9.468 ms
@<code>{insert 10001 user10001}	16.114 ms	4.622 ms
@<code>{update 5000 test}	8.025 ms	3.751 ms
@<code>{delete 5000}	9.833 ms	2.378 ms
//}

前章と比較し、insert, update, delete の処理時間が短くなっていることが確認できます。ファイル全体ではなく、1ページ（4KB）だけの読み書きで済むようになった効果が明確に表れています。

一方で、@<code>{select} の処理時間は大幅に増加しています。これは、前章までは「起動時に全てのデータをメモリに載せていた」のに対し、本章からは「毎回ディスクからページを読み込んで探すようになった」ためです。ディスクからのI/Oアクセスが毎回発生するようになったことが、検索が遅くなった最大の理由です。

== まとめと次章への課題

本章では、データベースのデータ配置を次のように改善しました。

 * ファイルを4096バイトの@<b>{ページ}という固定長の単位に区切り、ページの位置を「ページ番号 @<m>{\times 4096}」で即座に計算可能とした。
 * 1ページを64バイトの@<b>{固定長スロット}に分割し、s番目のスロットの位置を「s @<m>{\times 64}」で計算可能とした。
 * 挿入・更新・削除で書き込むのは、変更のあった@<b>{1ページ分だけ}になり、データ更新のたびにファイル全体を書き直す無駄が解消された。

一方で、検索（select）には致命的な課題が残っています。ファイルの先頭から順に目的のキーが見つかるまですべてのページをディスクから読み込んでスロットを調べており、これを@<b>{線形探索（Linear Search）}と呼びます。レコード件数が1万件、100万件と増えるごとに、1件を探すために読み込むディスクI/O回数が激増してしまい、実用的な速度ではなくなってしまいます。

次章（第5章）では、この検索速度の問題を根本から解決するために、データベースの心臓部とも言える@<b>{B-Tree（B木）}インデックスを導入します。ファイルの先頭から順に探すのではなく、「探しているキーがファイルのどこ（どのページとスロット）にあるのか」を一足飛びに知る仕組みを組み込み、再び圧倒的な検索スピードを取り戻します。

