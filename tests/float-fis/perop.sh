#!/bin/sh
# Ticks per float operation: three ways of doing floats, four loops.
# Корень репозитория, абсолютным путём: ниже есть cd в своё место.
U=$(cd "$(dirname "$0")/../.." && pwd)
export PATH=$U/gcc/bin:$PATH
port=2600
echo "вариант op ticks"
for v in fis soft asm; do
  case $v in fis) F="";; *) F="-mno-fis";; esac
  X=""; [ $v = asm ] && X=fisasm.o
  for op in OP_NONE OP_ADD OP_MUL OP_DIV; do
    pdp11-uknc-rt11-gcc -std=gnu23 -O2 -g $F -DOP=$op -o fop_$v.sav fop.c $X 2>/dev/null
    pdp11-uknc-rt11-gcc -std=gnu23 -O2 -g $F -DOP=$op -Wl,-m,pdp11rt11 -o fop_$v.elf fop.c $X 2>/dev/null
    port=$((port+1))
    t=$(./measure.sh fop_$v $port 2>&1 | grep "profiling is off" | head -1 | sed -E 's/.*off, ([0-9]+) ticks.*/\1/')
    echo "$v $op ${t:-ОШИБКА}"
  done
done
