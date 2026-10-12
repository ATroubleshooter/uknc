/* opbench.c -- по одной подпрограмме libgcc на сборку, чтобы снять её
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
    put("\r\n");
}

#define OP_none 0
#define OP_addsf3       1
#define OP_subsf3       2
#define OP_mulsf3       3
#define OP_divsf3       4
#define OP_negsf2       5
#define OP_adddf3       6
#define OP_subdf3       7
#define OP_muldf3       8
#define OP_divdf3       9
#define OP_extendsfdf2  10
#define OP_truncdfsf2   11
#define OP_fixsfsi      12
#define OP_fixdfsi      13
#define OP_fixunssfsi   14
#define OP_floatsisf    15
#define OP_floatunsisf  16
#define OP_floatdisf    17
#define OP_ltsf2        18
#define OP_lesf2        19
#define OP_gtsf2        20
#define OP_gesf2        21
#define OP_eqsf2        22
#define OP_nesf2        23
#define OP_cmpsf2       24
#define OP_divsi3       25
#define OP_modsi3       26
#define OP_udivsi3      27
#define OP_umodsi3      28
#define OP_udivhi3      29
#define OP_umodhi3      30

#ifndef OP
#define OP OP_none
#endif

/* Имена с тремя подчёркиваниями -- это префикс цели плюс имя libgcc.  */
extern float r_addsf3(float, float) __asm__("___addsf3");
extern float r_subsf3(float, float) __asm__("___subsf3");
extern float r_mulsf3(float, float) __asm__("___mulsf3");
extern float r_divsf3(float, float) __asm__("___divsf3");
extern float r_negsf2(float) __asm__("___negsf2");
extern double r_adddf3(double, double) __asm__("___adddf3");
extern double r_subdf3(double, double) __asm__("___subdf3");
extern double r_muldf3(double, double) __asm__("___muldf3");
extern double r_divdf3(double, double) __asm__("___divdf3");
extern double r_extendsfdf2(float) __asm__("___extendsfdf2");
extern float r_truncdfsf2(double) __asm__("___truncdfsf2");
extern long r_fixsfsi(float) __asm__("___fixsfsi");
extern long r_fixdfsi(double) __asm__("___fixdfsi");
extern unsigned long r_fixunssfsi(float) __asm__("___fixunssfsi");
extern float r_floatsisf(long) __asm__("___floatsisf");
extern float r_floatunsisf(unsigned long) __asm__("___floatunsisf");
extern float r_floatdisf(long long) __asm__("___floatdisf");
extern int r_ltsf2(float, float) __asm__("___ltsf2");
extern int r_lesf2(float, float) __asm__("___lesf2");
extern int r_gtsf2(float, float) __asm__("___gtsf2");
extern int r_gesf2(float, float) __asm__("___gesf2");
extern int r_eqsf2(float, float) __asm__("___eqsf2");
extern int r_nesf2(float, float) __asm__("___nesf2");
extern int r_cmpsf2(float, float) __asm__("___cmpsf2");
extern long r_divsi3(long, long) __asm__("___divsi3");
extern long r_modsi3(long, long) __asm__("___modsi3");
extern unsigned long r_udivsi3(unsigned long, unsigned long) __asm__("___udivsi3");
extern unsigned long r_umodsi3(unsigned long, unsigned long) __asm__("___umodsi3");
extern unsigned int r_udivhi3(unsigned int, unsigned int) __asm__("___udivhi3");
extern unsigned int r_umodhi3(unsigned int, unsigned int) __asm__("___umodhi3");

static volatile int sink_int;
static volatile long sink_long;
static volatile unsigned long sink_ulong;
static volatile unsigned int sink_uint;
static volatile float sink_float;
static volatile double sink_double;

__attribute__((noinline)) static void work(unsigned n)
{
    unsigned i;

#if OP == OP_none
    for (i = 0; i < n; i++)
        sink_int = (int) i;
#elif OP == OP_addsf3
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
#ifdef INLINE
        sink_float = a + b;
#else
        sink_float = r_addsf3(a, b);
#endif
#elif OP == OP_subsf3
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
#ifdef INLINE
        sink_float = a - b;
#else
        sink_float = r_subsf3(a, b);
#endif
#elif OP == OP_mulsf3
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
#ifdef INLINE
        sink_float = a * b;
#else
        sink_float = r_mulsf3(a, b);
#endif
#elif OP == OP_divsf3
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
#ifdef INLINE
        sink_float = a / b;
#else
        sink_float = r_divsf3(a, b);
#endif
#elif OP == OP_negsf2
    volatile float a = 3.5f;

    for (i = 0; i < n; i++)
#ifdef INLINE
        sink_float = -a;
#else
        sink_float = r_negsf2(a);
#endif
#elif OP == OP_adddf3
    volatile double a = 3.5, b = 1.25;

    for (i = 0; i < n; i++)
        sink_double = r_adddf3(a, b);
#elif OP == OP_subdf3
    volatile double a = 3.5, b = 1.25;

    for (i = 0; i < n; i++)
        sink_double = r_subdf3(a, b);
#elif OP == OP_muldf3
    volatile double a = 3.5, b = 1.25;

    for (i = 0; i < n; i++)
        sink_double = r_muldf3(a, b);
#elif OP == OP_divdf3
    volatile double a = 3.5, b = 1.25;

    for (i = 0; i < n; i++)
        sink_double = r_divdf3(a, b);
#elif OP == OP_extendsfdf2
    volatile float a = 3.5f;

    for (i = 0; i < n; i++)
        sink_double = r_extendsfdf2(a);
#elif OP == OP_truncdfsf2
    volatile double a = 3.5;

    for (i = 0; i < n; i++)
        sink_float = r_truncdfsf2(a);
#elif OP == OP_fixsfsi
    volatile float a = 12345.75f;

    for (i = 0; i < n; i++)
        sink_long = r_fixsfsi(a);
#elif OP == OP_fixdfsi
    volatile double a = 12345.75;

    for (i = 0; i < n; i++)
        sink_long = r_fixdfsi(a);
#elif OP == OP_fixunssfsi
    volatile float a = 70000.5f;

    for (i = 0; i < n; i++)
        sink_ulong = r_fixunssfsi(a);
#elif OP == OP_floatsisf
    volatile long a = 123456L;

    for (i = 0; i < n; i++)
        sink_float = r_floatsisf(a);
#elif OP == OP_floatunsisf
    volatile unsigned long a = 4000000000UL;

    for (i = 0; i < n; i++)
        sink_float = r_floatunsisf(a);
#elif OP == OP_floatdisf
    volatile long long a = 1234567890123LL;

    for (i = 0; i < n; i++)
        sink_float = r_floatdisf(a);
#elif OP == OP_ltsf2
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
        sink_int = r_ltsf2(a, b);
#elif OP == OP_lesf2
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
        sink_int = r_lesf2(a, b);
#elif OP == OP_gtsf2
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
        sink_int = r_gtsf2(a, b);
#elif OP == OP_gesf2
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
        sink_int = r_gesf2(a, b);
#elif OP == OP_eqsf2
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
        sink_int = r_eqsf2(a, b);
#elif OP == OP_nesf2
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
        sink_int = r_nesf2(a, b);
#elif OP == OP_cmpsf2
    volatile float a = 3.5f, b = 1.25f;

    for (i = 0; i < n; i++)
        sink_int = r_cmpsf2(a, b);
#elif OP == OP_divsi3
    volatile long a = 1000000L, b = 7L;

    for (i = 0; i < n; i++)
        sink_long = r_divsi3(a, b);
#elif OP == OP_modsi3
    volatile long a = 1000000L, b = 7L;

    for (i = 0; i < n; i++)
        sink_long = r_modsi3(a, b);
#elif OP == OP_udivsi3
    volatile unsigned long a = 0xffffffffUL, b = 7UL;

    for (i = 0; i < n; i++)
        sink_ulong = r_udivsi3(a, b);
#elif OP == OP_umodsi3
    volatile unsigned long a = 0xffffffffUL, b = 7UL;

    for (i = 0; i < n; i++)
        sink_ulong = r_umodsi3(a, b);
#elif OP == OP_udivhi3
    volatile unsigned int a = 60000u, b = 7u;

    for (i = 0; i < n; i++)
        sink_uint = r_udivhi3(a, b);
#elif OP == OP_umodhi3
    volatile unsigned int a = 60000u, b = 7u;

    for (i = 0; i < n; i++)
        sink_uint = r_umodhi3(a, b);
#endif
}

int main(void)
{
    bench_begin();
    work(ITERS);
    bench_end();

#if OP == OP_none
    put("none\r\n");
#elif OP == OP_addsf3
    show((const void *) &sink_float, 2);
#elif OP == OP_subsf3
    show((const void *) &sink_float, 2);
#elif OP == OP_mulsf3
    show((const void *) &sink_float, 2);
#elif OP == OP_divsf3
    show((const void *) &sink_float, 2);
#elif OP == OP_negsf2
    show((const void *) &sink_float, 2);
#elif OP == OP_adddf3
    show((const void *) &sink_double, 4);
#elif OP == OP_subdf3
    show((const void *) &sink_double, 4);
#elif OP == OP_muldf3
    show((const void *) &sink_double, 4);
#elif OP == OP_divdf3
    show((const void *) &sink_double, 4);
#elif OP == OP_extendsfdf2
    show((const void *) &sink_double, 4);
#elif OP == OP_truncdfsf2
    show((const void *) &sink_float, 2);
#elif OP == OP_fixsfsi
    show((const void *) &sink_long, 2);
#elif OP == OP_fixdfsi
    show((const void *) &sink_long, 2);
#elif OP == OP_fixunssfsi
    show((const void *) &sink_ulong, 2);
#elif OP == OP_floatsisf
    show((const void *) &sink_float, 2);
#elif OP == OP_floatunsisf
    show((const void *) &sink_float, 2);
#elif OP == OP_floatdisf
    show((const void *) &sink_float, 2);
#elif OP == OP_ltsf2
    show((const void *) &sink_int, 1);
#elif OP == OP_lesf2
    show((const void *) &sink_int, 1);
#elif OP == OP_gtsf2
    show((const void *) &sink_int, 1);
#elif OP == OP_gesf2
    show((const void *) &sink_int, 1);
#elif OP == OP_eqsf2
    show((const void *) &sink_int, 1);
#elif OP == OP_nesf2
    show((const void *) &sink_int, 1);
#elif OP == OP_cmpsf2
    show((const void *) &sink_int, 1);
#elif OP == OP_divsi3
    show((const void *) &sink_long, 2);
#elif OP == OP_modsi3
    show((const void *) &sink_long, 2);
#elif OP == OP_udivsi3
    show((const void *) &sink_ulong, 2);
#elif OP == OP_umodsi3
    show((const void *) &sink_ulong, 2);
#elif OP == OP_udivhi3
    show((const void *) &sink_uint, 1);
#elif OP == OP_umodhi3
    show((const void *) &sink_uint, 1);
#endif
    return 0;
}
