# ゼロから作るリレーショナルデータベース (Java版)

本書「ゼロから作るリレーショナルデータベース」の公式サポートリポジトリです。
データベースの仕組みを理解するために、Javaを使ってREPL、永続化、B木インデックス、SQLパーサ、クエリ実行エンジンを段階的に自作していきます。

---

## 🛠 1. 開発環境の起動と停止（Docker）

本プロジェクトはDockerコンテナ内でJavaを動かします。ホストマシン（ご自身のPC）にJavaやGradleをインストールする必要はありません。

### コンテナの起動

プロジェクトのルートディレクトリで以下のコマンドを実行します。初回はコンテナの構築が行われます。
※本READMEでは Docker Compose v2（`docker compose`）を使用します。`docker-compose`（v1）を利用している場合は Compose プラグインをインストールしてください。

```bash
docker compose up -d
```

コンテナの停止
開発を終了する場合は、以下のコマンドでコンテナを停止します。

```Bash
docker compose down
```

コンテナの中に入る（シェルを起動する）
コンテナの中で直接色々なコマンド（gradleなど）を実行したい場合は、以下のコマンドでコンテナ内のシェルに入ることができます。

```Bash
docker compose exec dev bash
```

(※以降のコマンドは、コンテナの外から実行する想定のコマンドを記載しています)

🚀 2. 各章のプログラムを実行する（REPLの起動など）
各章のメインプログラム（REPLなど）を起動するには、コンテナ経由でGradleの run タスクを呼び出します。

第1章を実行する場合

```Bash
docker compose exec dev gradle :chapter01:run --console=plain
```

ポイント: --console=plain を付与することで、対話型プログラム（REPL）の入力プロンプトが崩れず、綺麗に表示・入力できるようになります。

将来、他の章を実行する場合
settings.gradle に章を追加した後は、同様に章のフォルダ名を指定するだけで実行可能です。

🧪 3. テストの実行方法
バグがないか、自動テストを一斉に実行することができます。

すべての章のテストをまとめて実行する

```Bash
docker compose exec dev gradle test
```

特定の章（例: 第1章）のテストだけを実行する

```Bash
docker compose exec dev gradle :chapter01:test
```

テスト結果のレポートは、各章の build/reports/tests/test/index.html に自動生成されます。

✨ 4. コードフォーマット（自動整形）の使い方
本書のコードは、表記揺れを防ぎ美しく保つために Google Java Format を採用しています。
コードを書き換えたら、コミットやPull Request（PR）を作成する前に必ず以下のコマンドを実行してフォーマットを適用してください。

コードを自動整形する

```Bash
docker compose exec dev gradle spotlessApply
```

このコマンドを実行すると、すべてのJavaファイルのインデントや改行が正しいルールに自動で修正されます。

フォーマットが守られているかチェックだけする

```Bash
docker compose exec dev gradle spotlessCheck
```

※GitHub上のCI（自動ビルド）でもこのチェックが走るようになっており、フォーマットが崩れている場合はエラーになります。

📂 目次と対応コード

第1章 最小のデータベース

第2章 データの永続化

第3章 データ配置の改善

第4章 検索の高速化

第5章 SQLを扱う

第6章 実行方法の決定

第7章 実行処理の構築

第8章 構造の分離

第9章 パフォーマンスの改善

第10章 設計の整理
