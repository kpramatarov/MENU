#!/bin/sh
# Компилира двата фърмуера като споделени библиотеки и изпълнява сценариите
# на симулацията. Резултатите (JSON, CSV) са в rezultati/, подробните записи по
# UART на двата възела - в build/.
set -e
cd "$(dirname "$0")"
B=build
R=rezultati
mkdir -p $B $R
FLAGS="-std=gnu++17 -O2 -fPIC -shared -fvisibility=hidden -fno-gnu-unique -Wall -Wextra \
       -Wno-unused-parameter -Wno-stringop-truncation -I ../test_host/mock_include"
g++ $FLAGS -DNODE_SLAVE  sim_node.cpp -o $B/slave.so
g++ $FLAGS -DNODE_MASTER sim_node.cpp -o $B/master.so
g++ -std=gnu++17 -O2 -Wall -Wextra sim_main.cpp -o $B/sim -ldl
for s in ${@:-rtt rtt_bg fer0 fer30 fer50 fer70 fer90 buttons wet wet_clean long}; do
  ./$B/sim "$s" $B $B
  cp $B/"$s".json $R/
  [ -f $B/"$s"_rtt.csv ] && cp $B/"$s"_rtt.csv $R/ || true
  [ -f $B/"$s"_react.csv ] && cp $B/"$s"_react.csv $R/ || true
done
echo "Готово: резултатите са в $R/"
