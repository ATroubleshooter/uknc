#!/bin/sh
# Такты на одну операцию: два способа делить, три цикла.
# Корень репозитория, абсолютным путём: ниже есть cd в своё место.
U=$(cd "$(dirname "$0")/../.." && pwd)
export PATH=$U/gcc/bin:$PATH
port=3100
echo "вариант op ticks"
for v in std asm; do
  X=""; [ $v = asm ] && X=fisasm.o
  for op in OP_NONE OP_UMODSI OP_UDIVHI; do
    pdp11-uknc-rt11-gcc -std=gnu23 -O2 -g -DOP=$op -o iop_$v.sav idivop.c $X 2>/dev/null
    pdp11-uknc-rt11-gcc -std=gnu23 -O2 -g -DOP=$op -Wl,-m,pdp11rt11 -o iop_$v.elf idivop.c $X 2>/dev/null
    port=$((port+1))
    t=$(./measure.sh iop_$v $port 2>&1 | grep "profiling is off" | head -1 | sed -E 's/.*off, ([0-9]+) ticks.*/\1/')
    echo "$v $op ${t:-ОШИБКА}"
  done
done
