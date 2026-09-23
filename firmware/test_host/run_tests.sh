#!/bin/sh
# Компилира и изпълнява тестовете на персонален компютър (g++, python3, node).
set -e
cd "$(dirname "$0")"
OUT="${1:-build}"
mkdir -p "$OUT/out"
FLAGS="-std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -I mock_include"
g++ $FLAGS test_slave.cpp  -o "$OUT/test_slave"
g++ $FLAGS test_master.cpp -o "$OUT/test_master"
"$OUT/test_slave"
"$OUT/test_master" "$OUT/out"
python3 check_json.py "$OUT/out"
node test_ui.js "$OUT/out"
echo "Всички тестове са успешни."
