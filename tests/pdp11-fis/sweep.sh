#!/bin/sh
# sweep.sh -- цена каждой подпрограммы из pdp11-fis.s, двумя реализациями.
#
# Для каждой операции собираются две программы: без fisasm.o (подпрограмму
# даёт libgcc) и с ним (её даёт pdp11-fis.s), каждая прогоняется на машине,
# и печатается строка «операция такты_штатной такты_рукописной результат».
# OP_none -- пустой цикл, его стоимость вычитается.
U=$(cd "$(dirname "$0")/../.." && pwd)
export PATH=$U/gcc/bin:$PATH
FLAGS="-std=gnu23 -O2 -g -mno-fis"
port=3200

ops=$(sed -n 's/^#define OP_\([a-z0-9]*\) *[0-9]*$/\1/p' opbench.c)

run() {                 # run ВАРИАНТ ОПЕРАЦИЯ -> такты
  v=$1; op=$2
  x=""; [ "$v" = asm ] && x=fisasm.o
  pdp11-uknc-rt11-gcc $FLAGS -DOP=OP_$op -o ob_$v.sav opbench.c $x 2> /dev/null
  pdp11-uknc-rt11-gcc $FLAGS -DOP=OP_$op -Wl,-m,pdp11rt11 -o ob_$v.elf \
      opbench.c $x 2> /dev/null
  port=$((port + 1))
  ./measure.sh ob_$v $port 2>&1 > /tmp/sweep_$v.txt
  sed -n 's/.*profiling is off, \([0-9]*\) ticks.*/\1/p' /tmp/sweep_$v.txt | head -1
}

result() {              # то, что напечатала программа
  grep -vE "^ob_|Breakpoint|Thread|profiling|^$|^[0-7]+ +[0-9]+|warning|Loading|Start address|Transfer|inferior|determining|\?\?" \
      /tmp/sweep_$1.txt | tr -d '\r' | tr '\n' ' ' | sed 's/  */ /g'
}

printf '%-12s %10s %10s %6s  %s\n' операция штатная рукописная раз результат
for op in $ops; do
  s=$(run std "$op"); a=$(run asm "$op")
  rs=$(result std); ra=$(result asm)
  same="="; [ "$rs" != "$ra" ] && same="РАЗНО"
  ratio=""
  if [ -n "$s" ] && [ -n "$a" ] && [ "$a" -gt 0 ] 2>/dev/null; then
    ratio=$(awk "BEGIN{printf \"%.2f\", $s/$a}")
  fi
  printf '%-12s %10s %10s %6s  %s %s\n' "$op" "${s:-?}" "${a:-?}" "$ratio" "$same" "$rs"
done
