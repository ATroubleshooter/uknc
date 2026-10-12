/* __umodsi3 and __udivhi3 -- libgcc's own against the hand-written
   routines in ../../stdlib/float/pdp11-fis.s.
 *
 * Two parts.  First a correctness pass over operand pairs picked to
 * catch the places where a 32-step and a 16-step shift-subtract loop
 * can differ -- a quotient that does not fit in a word, a divisor
 * larger than the dividend, the top value of each type -- printed as a
 * checksum and a few results in full, so that the two builds can be
 * compared line by line.  Then the loop the profiler measures.
 *
 * Division by zero is not here: it is undefined in C, libgcc and the
 * hand-written routines answer differently (the latter return zero),
 * and nothing in a program should be asking.
 */

#include <unistd.h>

#ifndef ITERS
#define ITERS 2000
#endif

__attribute__((noinline)) void bench_begin(void) { __asm__ volatile (""); }
__attribute__((noinline)) void bench_end(void) { __asm__ volatile (""); }

/* Not const: the compiler would otherwise do the arithmetic itself and
   the routine under test would never be called.  */
static volatile unsigned long wide[][2] = {
    { 0xffffffffUL, 3UL },              /* quotient far past 16 bits */
    { 1000000UL, 7UL },
    { 7UL, 1000000UL },                 /* divisor bigger than dividend */
    { 0x10000UL, 0x10000UL },
    { 123456789UL, 65536UL },
    { 0xfffffffeUL, 0xffffffffUL },
    { 1UL, 1UL },
    { 0UL, 5UL },
};

static volatile unsigned int narrow[][2] = {
    { 65535u, 1u },                     /* quotient 65535: no room for a sign */
    { 65535u, 3u },
    { 65535u, 65535u },
    { 32768u, 2u },
    { 1000u, 7u },
    { 7u, 9u },
    { 0u, 9u },
    { 40000u, 40001u },
};

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

/* % on unsigned long is __umodsi3; / on unsigned int is __udivhi3.  */
__attribute__((noinline)) static unsigned long umodsi(unsigned long a,
                                                      unsigned long b)
{
    return a % b;
}

__attribute__((noinline)) static unsigned int udivhi(unsigned int a,
                                                     unsigned int b)
{
    return a / b;
}

static void check(void)
{
    unsigned long sum = 0;
    unsigned i;

    put("umodsi3:");
    for (i = 0; i < sizeof wide / sizeof wide[0]; i++) {
        unsigned long r = umodsi(wide[i][0], wide[i][1]);

        put(" ");
        putnum(r);
        sum += r;
    }
    put("\r\nudivhi3:");
    for (i = 0; i < sizeof narrow / sizeof narrow[0]; i++) {
        unsigned int r = udivhi(narrow[i][0], narrow[i][1]);

        put(" ");
        putnum(r);
        sum += r;
    }
    put("\r\nsum ");
    putnum(sum);
    put("\r\n");
}

__attribute__((noinline)) static unsigned long work(unsigned n)
{
    unsigned long acc = 0;
    unsigned i;

    for (i = 0; i < n; i++) {
        acc += umodsi(0xffffffffUL - i, 7UL + i);
        acc += udivhi((unsigned int) (60000u - i), (unsigned int) (3u + (i & 7)));
    }

    return acc;
}

int main(void)
{
    unsigned long r;

    check();

    bench_begin();
    r = work(ITERS);
    bench_end();

    put("result ");
    putnum(r);
    put("\r\n");
    return 0;
}
