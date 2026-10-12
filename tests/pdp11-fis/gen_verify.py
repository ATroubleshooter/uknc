#!/usr/bin/env python3
"""Пишет verify.c -- прогон значений через подпрограммы без FIS.

Собирается дважды (со штатными и с рукописными) и прогоняется на машине:
если вывод совпал, реализации взаимозаменяемы на этих значениях.
"""

F = ["0.0f", "1.0f", "-1.0f", "0.5f", "-0.5f", "2.5f", "-2.5f",
     "12345.75f", "-12345.75f", "0.1f", "65536.0f", "1e30f", "1e-30f"]
D = ["0.0", "1.0", "-1.0", "0.5", "2.5", "-2.5", "12345.75",
     "1.2345678901234", "-1.2345678901234", "1e30", "1e-30"]
SI = ["0L", "1L", "-1L", "32767L", "-32768L", "65535L", "123456L",
      "-123456L", "2147483647L", "(-2147483647L - 1L)",
      # округление: мантисса float -- 24 бита, дальше надо округлять
      "16777215L", "16777216L", "16777217L", "16777219L", "33554433L",
      "-16777217L", "-33554433L", "2147483646L", "1073741825L"]
USI = ["0UL", "1UL", "65535UL", "65536UL", "4000000000UL", "4294967295UL",
       "16777215UL", "16777216UL", "16777217UL", "16777219UL", "33554433UL",
       "2147483649UL", "4294967294UL"]
DI = ["0LL", "1LL", "-1LL", "1234567890123LL", "-1234567890123LL"]
FF = ["{0.0f, 0.0f}", "{1.0f, -1.0f}", "{-1.0f, 1.0f}", "{1.0f, 1.0f}",
      "{0.5f, 0.25f}", "{-0.5f, -0.25f}", "{12345.75f, 12345.75f}",
      "{1e30f, 1e-30f}", "{0.0f, -0.0f}",
      # знак и порядок: формат DEC -- знак-величина, целочисленным
      # сравнением отрицательные так просто не упорядочить
      "{-1e30f, -1e-30f}", "{-1e-30f, -1e30f}", "{-12345.75f, 12345.75f}",
      "{12345.75f, -12345.75f}", "{1e-30f, 0.0f}", "{0.0f, 1e-30f}",
      "{-1e-30f, 0.0f}", "{0.0f, -1e-30f}", "{-0.0f, 0.0f}"]
SS = ["{1000000L, 7L}", "{-1000000L, 7L}", "{1000000L, -7L}",
      "{-1000000L, -7L}", "{7L, 1000000L}", "{0L, 5L}",
      "{2147483647L, 1L}", "{(-2147483647L - 1L), -1L}",
      # у делимого бит 15 младшего слова стоит, а знак числа другой:
      # знак остатка берётся от старшего слова, не от младшего
      "{98304L, 7L}", "{-98304L, 7L}", "{98304L, -7L}", "{-98304L, -7L}",
      "{2147450880L, 3L}", "{-2147450880L, 3L}", "{40960L, 7L}",
      "{-40960L, 7L}"]
UU = ["{4294967295UL, 3UL}", "{1000UL, 7UL}", "{7UL, 1000UL}",
      "{2147483648UL, 2UL}", "{1UL, 1UL}", "{0UL, 5UL}"]
HH = ["{65535u, 1u}", "{65535u, 3u}", "{32768u, 2u}", "{1000u, 7u}",
      "{7u, 9u}", "{0u, 9u}", "{40000u, 40001u}"]

# имя, тип результата, слов в результате, тип аргумента(ов), значения, пара?
CASES = [
    ("negsf2",      "float",         2, "float",         F,   False),
    ("extendsfdf2", "double",        4, "float",         F,   False),
    ("truncdfsf2",  "float",         2, "double",        D,   False),
    ("fixsfsi",     "long",          2, "float",         F,   False),
    ("fixunssfsi",  "unsigned long", 2, "float",         F,   False),
    ("fixdfsi",     "long",          2, "double",        D,   False),
    ("floatsisf",   "float",         2, "long",          SI,  False),
    ("floatunsisf", "float",         2, "unsigned long", USI, False),
    ("floatdisf",   "float",         2, "long long",     DI,  False),
    ("ltsf2",       "int",           1, "float",         FF,  True),
    ("lesf2",       "int",           1, "float",         FF,  True),
    ("gtsf2",       "int",           1, "float",         FF,  True),
    ("gesf2",       "int",           1, "float",         FF,  True),
    ("eqsf2",       "int",           1, "float",         FF,  True),
    ("nesf2",       "int",           1, "float",         FF,  True),
    ("cmpsf2",      "int",           1, "float",         FF,  True),
    ("divsi3",      "long",          2, "long",          SS,  True),
    ("modsi3",      "long",          2, "long",          SS,  True),
    ("udivsi3",     "unsigned long", 2, "unsigned long", UU,  True),
    ("umodsi3",     "unsigned long", 2, "unsigned long", UU,  True),
    ("udivhi3",     "unsigned int",  1, "unsigned int",  HH,  True),
    ("umodhi3",     "unsigned int",  1, "unsigned int",  HH,  True),
]

out = ['''/* verify.c -- те подпрограммы из pdp11-fis.s, что обходятся без FIS,
   прогнанные по набору значений.  Написан генератором gen_verify.py.

   Собирается дважды: без fisasm.o подпрограммы даёт libgcc, с ним --
   pdp11-fis.s.  Вывод сравнивается: строка на вызов, в ней имя, номер
   случая и результат словами, как он лежит в памяти.  Деления на ноль
   здесь нет -- в C это неопределённое поведение.  */

#include <string.h>
#include <unistd.h>

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

/* У сравнений от результата нужен только знак: его и печатаем, иначе
   сравнивались бы величины, которых никто не обещал.  */
static void line_sign(const char *name, unsigned n, int v)
{
    put(name);
    put(" ");
    putnum(n);
    put(v < 0 ? " -1" : (v > 0 ? " 1" : " 0"));
    put("\\r\\n");
}

static void line(const char *name, unsigned n, const void *p, unsigned words)
{
    unsigned short w;
    unsigned i;

    put(name);
    put(" ");
    putnum(n);
    for (i = 0; i < words; i++) {
        memcpy(&w, (const char *) p + i * 2, sizeof w);
        put(" ");
        putnum(w);
    }
    put("\\r\\n");
}

''']

for name, ret, words, arg, vals, pair in CASES:
    argl = "%s, %s" % (arg, arg) if pair else arg
    out.append('extern %s r_%s(%s) __asm__("___%s");\n' % (ret, name, argl, name))
out.append("\n")

for name, ret, words, arg, vals, pair in CASES:
    if pair:
        out.append("static volatile %s in_%s[][2] = {\n    %s\n};\n"
                   % (arg, name, ",\n    ".join(vals)))
    else:
        out.append("static volatile %s in_%s[] = {\n    %s\n};\n"
                   % (arg, name, ", ".join(vals)))
out.append("\n")

out.append("int main(void)\n{\n    unsigned i;\n\n")
for name, ret, words, arg, vals, pair in CASES:
    call = ("r_%s(in_%s[i][0], in_%s[i][1])" % (name, name, name)) if pair \
        else ("r_%s(in_%s[i])" % (name, name))
    CMP = ("ltsf2", "lesf2", "gtsf2", "gesf2", "eqsf2", "nesf2", "cmpsf2")
    show = ('line_sign("%s", i, (int) r);' % name) if name in CMP else \
        ('line("%s", i, (const void *) &r, %d);' % (name, words))
    out.append("    for (i = 0; i < sizeof in_%s / sizeof in_%s[0]; i++) {\n"
               "        %s r = %s;\n\n"
               "        %s\n    }\n\n"
               % (name, name, ret, call, show))
out.append("    return 0;\n}\n")

open("verify.c", "w").write("".join(out))
print("verify.c: %d подпрограмм" % len(CASES))
