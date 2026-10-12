#!/bin/sh
# measure.sh ИМЯ ПОРТ -- такты между bench_begin и bench_end.
#
# ИМЯ -- основа имени собранной программы: берутся ИМЯ.sav и ИМЯ.elf.
#
# Программа кладётся на копию системного диска под именем T.SAV, машина
# поднимается, gdb командой run просит сервер её запустить (тот сбросит
# машину, дождётся RT-11 и наберёт "R T"), и профилировщик включается на
# первом маркере, выключается на втором. Символы gdb берёт из .elf --
# это те же объекты и те же адреса, что в .sav.
set -e
# Корень репозитория, абсолютным путём: ниже есть cd в своё место.
U=$(cd "$(dirname "$0")/../.." && pwd)
prog=$1
port=$2

tmp=$(mktemp -d /tmp/fbench.XXXXXX)
trap 'pkill -f "ukncbtldebug.*--port $port" 2>/dev/null; rm -rf "$tmp"' EXIT
cp "$U/resources/rt11os.dsk" "$tmp/t.dsk"
cp "$prog.sav" "$tmp/T.SAV"
(cd "$tmp" && "$U/gcc/bin/rt11dsk" a t.dsk T.SAV > /dev/null)

("$U/debugger/bin/ukncbtldebug" --disk1 "$tmp/t.dsk" \
    --rom "$U/rom/uknc_rom_autoboot.bin" --port "$port" --frames 100000 \
    > "$tmp/emu.log" 2>&1 &)
i=0
while [ $i -lt 30 ]; do
  grep -q "Listening" "$tmp/emu.log" 2> /dev/null && break
  i=$((i + 1)); sleep 1
done

"$U/gcc/bin/pdp11-uknc-rt11-gdb" -batch -nx \
  -ex "set confirm off" \
  -ex "target extended-remote :$port" \
  -ex "set remote exec-file T" \
  -ex "break bench_begin" \
  -ex "break bench_end" \
  -ex "run" \
  -ex "monitor prof reset" \
  -ex "monitor prof on" \
  -ex "continue" \
  -ex "monitor prof off" \
  -ex "monitor prof top 12" \
  "$prog.elf" > "out_$prog.txt" 2>&1 || true
sed "s/^/$prog: /" "out_$prog.txt"
