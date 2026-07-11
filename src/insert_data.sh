#!/bin/bash

# 引数から実行する章（CHAP）を取得（デフォルトは chapter01）
CHAP=${1:-chapter01}

echo "10000件のデータを生成中..."

# 10000回分の insert コマンドを一時ファイルに書き出す
# （例: insert 1 user1）
for i in $(seq 1 10000)
do
    echo "insert $i user$i"
done > data.txt

echo "exit" >> data.txt

echo "Dockerコンテナ内のプログラムにデータを投入します ($(CHAP))..."

# Dockerコンテナのプログラムに対して、生成したテキストファイルを標準入力として流し込む
docker compose exec -T dev gradle :"$CHAP":run --console=plain < data.txt

rm data.txt

echo "データ投入が完了しました。"
