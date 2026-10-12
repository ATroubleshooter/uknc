/* Float benchmark: a fixed amount of add/sub/mul/div, comparisons and
   conversions, between two markers the profiler is switched on and off
   at.  Output through write() and a hand-rolled itoa, so that nothing
   but the loop itself differs between the builds being compared.  */

#include <unistd.h>

#ifndef ITERS
#define ITERS 2000
#endif

__attribute__((noinline)) void bench_begin(void) { __asm__ volatile (""); }
__attribute__((noinline)) void bench_end(void) { __asm__ volatile (""); }

__attribute__((noinline)) static long work(unsigned n)
{
    float x = 0.1f, y = 0.2f, s = 0.0f;
    unsigned i;

    for (i = 0; i < n; i++) {
        s = s + x * y;                  /* multiply and add */
        x = x + 0.001953125f;           /* add */
        if (x > 1.0f)                   /* compare */
            x = 0.1f;
        y = y / 1.0009765625f;          /* divide */
        if (y < 0.001f)
            y = 0.2f;
    }

    return (long) (s * 1000.0f);        /* multiply and float -> int */
}

static void put(const char *s)
{
    unsigned len = 0;

    while (s[len] != 0)
        len++;
    write(STDOUT_FILENO, s, len);
}

static void putnum(long v)
{
    char buf[12];
    int i = 11;

    buf[i] = 0;
    if (v == 0)
        buf[--i] = '0';
    while (v > 0) {
        buf[--i] = (char) ('0' + (int) (v % 10));
        v /= 10;
    }
    put(buf + i);
}

int main(void)
{
    long r;

    put("fbench ");
    putnum(ITERS);
    put("\r\n");

    bench_begin();
    r = work(ITERS);
    bench_end();

    put("result ");
    putnum(r);
    put("\r\n");
    return 0;
}
