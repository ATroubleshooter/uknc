#!/usr/bin/env python3
"""Пишет opbench.c -- по блоку на каждую подпрограмму из pdp11-fis.s.

Каждая вызывается по своему ассемблерному имени, поэтому измеряется
именно она, а не то, что компилятор решил выпустить для оператора.
Запускать, если список операций меняется; сам opbench.c лежит в
репозитории как обычный исходник.
"""

# имя, тип результата, типы аргументов, значения аргументов, слов в результате
OPS = [
    ("addsf3",     "float",         ["float"],              ["3.5f", "1.25f"],       2),
    ("subsf3",     "float",         ["float"],              ["3.5f", "1.25f"],       2),
    ("mulsf3",     "float",         ["float"],              ["3.5f", "1.25f"],       2),
    ("divsf3",     "float",         ["float"],              ["3.5f", "1.25f"],       2),
    ("negsf2",     "float",         ["float"],              ["3.5f"],                2),
    ("adddf3",     "double",        ["double"],             ["3.5", "1.25"],         4),
    ("subdf3",     "double",        ["double"],             ["3.5", "1.25"],         4),
    ("muldf3",     "double",        ["double"],             ["3.5", "1.25"],         4),
    ("divdf3",     "double",        ["double"],             ["3.5", "1.25"],         4),
    ("extendsfdf2", "double",       ["float"],              ["3.5f"],                4),
    ("truncdfsf2", "float",         ["double"],             ["3.5"],                 2),
    ("fixsfsi",    "long",          ["float"],              ["12345.75f"],           2),
    ("fixdfsi",    "long",          ["double"],             ["12345.75"],            2),
    ("fixunssfsi", "unsigned long", ["float"],              ["70000.5f"],            2),
    ("floatsisf",  "float",         ["long"],               ["123456L"],             2),
    ("floatunsisf", "float",        ["unsigned long"],      ["4000000000UL"],        2),
    ("floatdisf",  "float",         ["long long"],          ["1234567890123LL"],     2),
    ("ltsf2",      "int",           ["float"],              ["3.5f", "1.25f"],       1),
    ("lesf2",      "int",           ["float"],              ["3.5f", "1.25f"],       1),
    ("gtsf2",      "int",           ["float"],              ["3.5f", "1.25f"],       1),
    ("gesf2",      "int",           ["float"],              ["3.5f", "1.25f"],       1),
    ("eqsf2",      "int",           ["float"],              ["3.5f", "1.25f"],       1),
    ("nesf2",      "int",           ["float"],              ["3.5f", "1.25f"],       1),
    ("cmpsf2",     "int",           ["float"],              ["3.5f", "1.25f"],       1),
    ("divsi3",     "long",          ["long"],               ["1000000L", "7L"],      2),
    ("modsi3",     "long",          ["long"],               ["1000000L", "7L"],      2),
    ("udivsi3",    "unsigned long", ["unsigned long"],      ["0xffffffffUL", "7UL"], 2),
    ("umodsi3",    "unsigned long", ["unsigned long"],      ["0xffffffffUL", "7UL"], 2),
    ("udivhi3",    "unsigned int",  ["unsigned int"],       ["60000u", "7u"],        1),
    ("umodhi3",    "unsigned int",  ["unsigned int"],       ["60000u", "7u"],        1),
]

head = '''/* opbench.c -- по одной подпрограмме libgcc на сборку, чтобы снять её
   цену в тактах.  Написан генератором gen_opbench.py рядом.

   Каждая подпрограмма вызывается по ассемблерному имени: так измеряется
   именно она, а не то, во что компилятор превратил оператор (`float a +
   b' с включённым FIS вообще не вызов).  Операнды -- volatile, иначе
   вычисление ушло бы из цикла.  OP_none меряет сам цикл, его вычитают.

   Результат печатается словами, как он лежит в памяти: два варианта
   сборки должны напечатать одно и то же, иначе сравнивать их скорость
   незачем.  */

#include <string.h>
#include <unistd.h>

#ifndef ITERS
#define ITERS 1000
#endif

__attribute__((noinline)) void bench_begin(void) { __asm__ volatile (""); }
__attribute__((noinline)) void bench_end(void) { __asm__ volatile (""); }

static void put(const char *s)
{
    unsigned len = 0;

    while (s[len] != 0)
        len++;
    write(STDOUT_FILENO, s, len);
}

static void putnum(unsigned long v)
{
    char buf[12];
    int i = 11;

    buf[i] = 0;
    if (v == 0)
        buf[--i] = '0';
    while (v > 0) {
        buf[--i] = (char) ('0' + (unsigned) (v % 10));
        v /= 10;
    }
    put(buf + i);
}

/* Результат как он лежит в памяти, слово за словом.  */
static void show(const void *p, unsigned words)
{
    unsigned short w;
    unsigned i;

    for (i = 0; i < words; i++) {
        memcpy(&w, (const char *) p + i * 2, sizeof w);
        put(" ");
        putnum(w);
    }
    put("\\r\\n");
}

#define OP_none 0
'''

lines = [head]
for n, (name, _, _, _, _) in enumerate(OPS, 1):
    lines.append("#define OP_%-12s %d\n" % (name, n))
lines.append("\n#ifndef OP\n#define OP OP_none\n#endif\n\n")

# объявления
lines.append("/* Имена с тремя подчёркиваниями -- это префикс цели плюс имя libgcc.  */\n")
for name, ret, args, _, _ in OPS:
    argl = ", ".join(args * (2 if len(args) == 1 and name not in
                             ("negsf2", "extendsfdf2", "truncdfsf2", "fixsfsi",
                              "fixdfsi", "fixunssfsi", "floatsisf",
                              "floatunsisf", "floatdisf") else 1))
    lines.append('extern %s r_%s(%s) __asm__("___%s");\n' % (ret, name, argl, name))

lines.append("\nstatic volatile int sink_int;\nstatic volatile long sink_long;\n"
             "static volatile unsigned long sink_ulong;\n"
             "static volatile unsigned int sink_uint;\n"
             "static volatile float sink_float;\n"
             "static volatile double sink_double;\n\n")

sinks = {"int": "sink_int", "long": "sink_long", "unsigned long": "sink_ulong",
         "unsigned int": "sink_uint", "float": "sink_float",
         "double": "sink_double"}

lines.append("__attribute__((noinline)) static void work(unsigned n)\n{\n")
lines.append("    unsigned i;\n\n")
lines.append("#if OP == OP_none\n    for (i = 0; i < n; i++)\n        sink_int = (int) i;\n")
for name, ret, args, vals, _ in OPS:
    decl = "    volatile %s a = %s" % (args[0], vals[0])
    if len(vals) == 2:
        decl += ", b = %s" % vals[1]
    decl += ";\n"
    call = "r_%s(a%s)" % (name, ", b" if len(vals) == 2 else "")
    inline = {"addsf3": "a + b", "subsf3": "a - b", "mulsf3": "a * b",
              "divsf3": "a / b", "negsf2": "-a"}.get(name)
    body = ("#elif OP == OP_%s\n%s\n    for (i = 0; i < n; i++)\n        %s = %s;\n"
            % (name, decl, sinks[ret], call))
    if inline is not None:
        # INLINE: то же самое оператором, то есть командой FIS без вызова.
        body = ("#elif OP == OP_%s\n%s\n    for (i = 0; i < n; i++)\n"
                "#ifdef INLINE\n        %s = %s;\n#else\n        %s = %s;\n#endif\n"
                % (name, decl, sinks[ret], inline, sinks[ret], call))
    lines.append(body)
lines.append("#endif\n}\n\n")

lines.append("""int main(void)
{
    bench_begin();
    work(ITERS);
    bench_end();

#if OP == OP_none
    put("none\\r\\n");
""")
for name, ret, _, _, words in OPS:
    lines.append('#elif OP == OP_%s\n    show((const void *) &%s, %d);\n'
                 % (name, sinks[ret], words))
lines.append("#endif\n    return 0;\n}\n")

open("opbench.c", "w").write("".join(lines))
print("opbench.c: %d операций" % len(OPS))
