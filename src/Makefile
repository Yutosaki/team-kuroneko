# 実行する章のデフォルト値（コマンドラインから上書き可能）
CHAP ?= chapter01

# 開発環境の起動
up:
	docker compose up -d

# 開発環境の停止
down:
	docker compose down

# コンテナの再構築（キャッシュ無視）
build:
	docker compose down
	docker compose build --no-cache
	docker compose up -d

# コンテナのシェルに入る
shell:
	docker compose exec dev bash

# プログラムの実行 (例: make run または make run CHAP=chapter02)
run:
	docker compose exec dev gradle :$(CHAP):run --console=plain

# コードの自動整形
fmt:
	docker compose exec dev gradle spotlessApply --no-configuration-cache

# フォーマットのチェック (CI用)
check:
	docker compose exec dev gradle spotlessCheck --no-configuration-cache

# 10000件のテストデータを一括投入する (例: make insert または make insert CHAP=chapter03)
insert:
	./insert_data.sh $(CHAP)

.PHONY: up down build shell run fmt check insert
