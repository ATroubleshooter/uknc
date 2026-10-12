/* verify.c -- те подпрограммы из pdp11-fis.s, что обходятся без FIS,
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
    put("\r\n");
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
    put("\r\n");
}

extern float r_negsf2(float) __asm__("___negsf2");
extern double r_extendsfdf2(float) __asm__("___extendsfdf2");
extern float r_truncdfsf2(double) __asm__("___truncdfsf2");
extern long r_fixsfsi(float) __asm__("___fixsfsi");
extern unsigned long r_fixunssfsi(float) __asm__("___fixunssfsi");
extern long r_fixdfsi(double) __asm__("___fixdfsi");
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

static volatile float in_negsf2[] = {
    0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.5f, -2.5f, 12345.75f, -12345.75f, 0.1f, 65536.0f, 1e30f, 1e-30f
};
static volatile float in_extendsfdf2[] = {
    0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.5f, -2.5f, 12345.75f, -12345.75f, 0.1f, 65536.0f, 1e30f, 1e-30f
};
static volatile double in_truncdfsf2[] = {
    0.0, 1.0, -1.0, 0.5, 2.5, -2.5, 12345.75, 1.2345678901234, -1.2345678901234, 1e30, 1e-30
};
static volatile float in_fixsfsi[] = {
    0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.5f, -2.5f, 12345.75f, -12345.75f, 0.1f, 65536.0f, 1e30f, 1e-30f
};
static volatile float in_fixunssfsi[] = {
    0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.5f, -2.5f, 12345.75f, -12345.75f, 0.1f, 65536.0f, 1e30f, 1e-30f
};
static volatile double in_fixdfsi[] = {
    0.0, 1.0, -1.0, 0.5, 2.5, -2.5, 12345.75, 1.2345678901234, -1.2345678901234, 1e30, 1e-30
};
static volatile long in_floatsisf[] = {
    0L, 1L, -1L, 32767L, -32768L, 65535L, 123456L, -123456L, 2147483647L, (-2147483647L - 1L), 16777215L, 16777216L, 16777217L, 16777219L, 33554433L, -16777217L, -33554433L, 2147483646L, 1073741825L
};
static volatile unsigned long in_floatunsisf[] = {
    0UL, 1UL, 65535UL, 65536UL, 4000000000UL, 4294967295UL, 16777215UL, 16777216UL, 16777217UL, 16777219UL, 33554433UL, 2147483649UL, 4294967294UL
};
static volatile long long in_floatdisf[] = {
    0LL, 1LL, -1LL, 1234567890123LL, -1234567890123LL
};
static volatile float in_ltsf2[][2] = {
    {0.0f, 0.0f},
    {1.0f, -1.0f},
    {-1.0f, 1.0f},
    {1.0f, 1.0f},
    {0.5f, 0.25f},
    {-0.5f, -0.25f},
    {12345.75f, 12345.75f},
    {1e30f, 1e-30f},
    {0.0f, -0.0f},
    {-1e30f, -1e-30f},
    {-1e-30f, -1e30f},
    {-12345.75f, 12345.75f},
    {12345.75f, -12345.75f},
    {1e-30f, 0.0f},
    {0.0f, 1e-30f},
    {-1e-30f, 0.0f},
    {0.0f, -1e-30f},
    {-0.0f, 0.0f}
};
static volatile float in_lesf2[][2] = {
    {0.0f, 0.0f},
    {1.0f, -1.0f},
    {-1.0f, 1.0f},
    {1.0f, 1.0f},
    {0.5f, 0.25f},
    {-0.5f, -0.25f},
    {12345.75f, 12345.75f},
    {1e30f, 1e-30f},
    {0.0f, -0.0f},
    {-1e30f, -1e-30f},
    {-1e-30f, -1e30f},
    {-12345.75f, 12345.75f},
    {12345.75f, -12345.75f},
    {1e-30f, 0.0f},
    {0.0f, 1e-30f},
    {-1e-30f, 0.0f},
    {0.0f, -1e-30f},
    {-0.0f, 0.0f}
};
static volatile float in_gtsf2[][2] = {
    {0.0f, 0.0f},
    {1.0f, -1.0f},
    {-1.0f, 1.0f},
    {1.0f, 1.0f},
    {0.5f, 0.25f},
    {-0.5f, -0.25f},
    {12345.75f, 12345.75f},
    {1e30f, 1e-30f},
    {0.0f, -0.0f},
    {-1e30f, -1e-30f},
    {-1e-30f, -1e30f},
    {-12345.75f, 12345.75f},
    {12345.75f, -12345.75f},
    {1e-30f, 0.0f},
    {0.0f, 1e-30f},
    {-1e-30f, 0.0f},
    {0.0f, -1e-30f},
    {-0.0f, 0.0f}
};
static volatile float in_gesf2[][2] = {
    {0.0f, 0.0f},
    {1.0f, -1.0f},
    {-1.0f, 1.0f},
    {1.0f, 1.0f},
    {0.5f, 0.25f},
    {-0.5f, -0.25f},
    {12345.75f, 12345.75f},
    {1e30f, 1e-30f},
    {0.0f, -0.0f},
    {-1e30f, -1e-30f},
    {-1e-30f, -1e30f},
    {-12345.75f, 12345.75f},
    {12345.75f, -12345.75f},
    {1e-30f, 0.0f},
    {0.0f, 1e-30f},
    {-1e-30f, 0.0f},
    {0.0f, -1e-30f},
    {-0.0f, 0.0f}
};
static volatile float in_eqsf2[][2] = {
    {0.0f, 0.0f},
    {1.0f, -1.0f},
    {-1.0f, 1.0f},
    {1.0f, 1.0f},
    {0.5f, 0.25f},
    {-0.5f, -0.25f},
    {12345.75f, 12345.75f},
    {1e30f, 1e-30f},
    {0.0f, -0.0f},
    {-1e30f, -1e-30f},
    {-1e-30f, -1e30f},
    {-12345.75f, 12345.75f},
    {12345.75f, -12345.75f},
    {1e-30f, 0.0f},
    {0.0f, 1e-30f},
    {-1e-30f, 0.0f},
    {0.0f, -1e-30f},
    {-0.0f, 0.0f}
};
static volatile float in_nesf2[][2] = {
    {0.0f, 0.0f},
    {1.0f, -1.0f},
    {-1.0f, 1.0f},
    {1.0f, 1.0f},
    {0.5f, 0.25f},
    {-0.5f, -0.25f},
    {12345.75f, 12345.75f},
    {1e30f, 1e-30f},
    {0.0f, -0.0f},
    {-1e30f, -1e-30f},
    {-1e-30f, -1e30f},
    {-12345.75f, 12345.75f},
    {12345.75f, -12345.75f},
    {1e-30f, 0.0f},
    {0.0f, 1e-30f},
    {-1e-30f, 0.0f},
    {0.0f, -1e-30f},
    {-0.0f, 0.0f}
};
static volatile float in_cmpsf2[][2] = {
    {0.0f, 0.0f},
    {1.0f, -1.0f},
    {-1.0f, 1.0f},
    {1.0f, 1.0f},
    {0.5f, 0.25f},
    {-0.5f, -0.25f},
    {12345.75f, 12345.75f},
    {1e30f, 1e-30f},
    {0.0f, -0.0f},
    {-1e30f, -1e-30f},
    {-1e-30f, -1e30f},
    {-12345.75f, 12345.75f},
    {12345.75f, -12345.75f},
    {1e-30f, 0.0f},
    {0.0f, 1e-30f},
    {-1e-30f, 0.0f},
    {0.0f, -1e-30f},
    {-0.0f, 0.0f}
};
static volatile long in_divsi3[][2] = {
    {1000000L, 7L},
    {-1000000L, 7L},
    {1000000L, -7L},
    {-1000000L, -7L},
    {7L, 1000000L},
    {0L, 5L},
    {2147483647L, 1L},
    {(-2147483647L - 1L), -1L},
    {98304L, 7L},
    {-98304L, 7L},
    {98304L, -7L},
    {-98304L, -7L},
    {2147450880L, 3L},
    {-2147450880L, 3L},
    {40960L, 7L},
    {-40960L, 7L}
};
static volatile long in_modsi3[][2] = {
    {1000000L, 7L},
    {-1000000L, 7L},
    {1000000L, -7L},
    {-1000000L, -7L},
    {7L, 1000000L},
    {0L, 5L},
    {2147483647L, 1L},
    {(-2147483647L - 1L), -1L},
    {98304L, 7L},
    {-98304L, 7L},
    {98304L, -7L},
    {-98304L, -7L},
    {2147450880L, 3L},
    {-2147450880L, 3L},
    {40960L, 7L},
    {-40960L, 7L}
};
static volatile unsigned long in_udivsi3[][2] = {
    {4294967295UL, 3UL},
    {1000UL, 7UL},
    {7UL, 1000UL},
    {2147483648UL, 2UL},
    {1UL, 1UL},
    {0UL, 5UL}
};
static volatile unsigned long in_umodsi3[][2] = {
    {4294967295UL, 3UL},
    {1000UL, 7UL},
    {7UL, 1000UL},
    {2147483648UL, 2UL},
    {1UL, 1UL},
    {0UL, 5UL}
};
static volatile unsigned int in_udivhi3[][2] = {
    {65535u, 1u},
    {65535u, 3u},
    {32768u, 2u},
    {1000u, 7u},
    {7u, 9u},
    {0u, 9u},
    {40000u, 40001u}
};
static volatile unsigned int in_umodhi3[][2] = {
    {65535u, 1u},
    {65535u, 3u},
    {32768u, 2u},
    {1000u, 7u},
    {7u, 9u},
    {0u, 9u},
    {40000u, 40001u}
};

int main(void)
{
    unsigned i;

    for (i = 0; i < sizeof in_negsf2 / sizeof in_negsf2[0]; i++) {
        float r = r_negsf2(in_negsf2[i]);

        line("negsf2", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_extendsfdf2 / sizeof in_extendsfdf2[0]; i++) {
        double r = r_extendsfdf2(in_extendsfdf2[i]);

        line("extendsfdf2", i, (const void *) &r, 4);
    }

    for (i = 0; i < sizeof in_truncdfsf2 / sizeof in_truncdfsf2[0]; i++) {
        float r = r_truncdfsf2(in_truncdfsf2[i]);

        line("truncdfsf2", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_fixsfsi / sizeof in_fixsfsi[0]; i++) {
        long r = r_fixsfsi(in_fixsfsi[i]);

        line("fixsfsi", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_fixunssfsi / sizeof in_fixunssfsi[0]; i++) {
        unsigned long r = r_fixunssfsi(in_fixunssfsi[i]);

        line("fixunssfsi", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_fixdfsi / sizeof in_fixdfsi[0]; i++) {
        long r = r_fixdfsi(in_fixdfsi[i]);

        line("fixdfsi", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_floatsisf / sizeof in_floatsisf[0]; i++) {
        float r = r_floatsisf(in_floatsisf[i]);

        line("floatsisf", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_floatunsisf / sizeof in_floatunsisf[0]; i++) {
        float r = r_floatunsisf(in_floatunsisf[i]);

        line("floatunsisf", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_floatdisf / sizeof in_floatdisf[0]; i++) {
        float r = r_floatdisf(in_floatdisf[i]);

        line("floatdisf", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_ltsf2 / sizeof in_ltsf2[0]; i++) {
        int r = r_ltsf2(in_ltsf2[i][0], in_ltsf2[i][1]);

        line_sign("ltsf2", i, (int) r);
    }

    for (i = 0; i < sizeof in_lesf2 / sizeof in_lesf2[0]; i++) {
        int r = r_lesf2(in_lesf2[i][0], in_lesf2[i][1]);

        line_sign("lesf2", i, (int) r);
    }

    for (i = 0; i < sizeof in_gtsf2 / sizeof in_gtsf2[0]; i++) {
        int r = r_gtsf2(in_gtsf2[i][0], in_gtsf2[i][1]);

        line_sign("gtsf2", i, (int) r);
    }

    for (i = 0; i < sizeof in_gesf2 / sizeof in_gesf2[0]; i++) {
        int r = r_gesf2(in_gesf2[i][0], in_gesf2[i][1]);

        line_sign("gesf2", i, (int) r);
    }

    for (i = 0; i < sizeof in_eqsf2 / sizeof in_eqsf2[0]; i++) {
        int r = r_eqsf2(in_eqsf2[i][0], in_eqsf2[i][1]);

        line_sign("eqsf2", i, (int) r);
    }

    for (i = 0; i < sizeof in_nesf2 / sizeof in_nesf2[0]; i++) {
        int r = r_nesf2(in_nesf2[i][0], in_nesf2[i][1]);

        line_sign("nesf2", i, (int) r);
    }

    for (i = 0; i < sizeof in_cmpsf2 / sizeof in_cmpsf2[0]; i++) {
        int r = r_cmpsf2(in_cmpsf2[i][0], in_cmpsf2[i][1]);

        line_sign("cmpsf2", i, (int) r);
    }

    for (i = 0; i < sizeof in_divsi3 / sizeof in_divsi3[0]; i++) {
        long r = r_divsi3(in_divsi3[i][0], in_divsi3[i][1]);

        line("divsi3", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_modsi3 / sizeof in_modsi3[0]; i++) {
        long r = r_modsi3(in_modsi3[i][0], in_modsi3[i][1]);

        line("modsi3", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_udivsi3 / sizeof in_udivsi3[0]; i++) {
        unsigned long r = r_udivsi3(in_udivsi3[i][0], in_udivsi3[i][1]);

        line("udivsi3", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_umodsi3 / sizeof in_umodsi3[0]; i++) {
        unsigned long r = r_umodsi3(in_umodsi3[i][0], in_umodsi3[i][1]);

        line("umodsi3", i, (const void *) &r, 2);
    }

    for (i = 0; i < sizeof in_udivhi3 / sizeof in_udivhi3[0]; i++) {
        unsigned int r = r_udivhi3(in_udivhi3[i][0], in_udivhi3[i][1]);

        line("udivhi3", i, (const void *) &r, 1);
    }

    for (i = 0; i < sizeof in_umodhi3 / sizeof in_umodhi3[0]; i++) {
        unsigned int r = r_umodhi3(in_umodhi3[i][0], in_umodhi3[i][1]);

        line("umodhi3", i, (const void *) &r, 1);
    }

    return 0;
}
