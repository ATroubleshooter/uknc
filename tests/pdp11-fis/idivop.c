/* One integer division per iteration, so the per-operation cost can be
   read off the tick counter.  OP picks which; OP_NONE measures the loop
   itself, to subtract.  */
#include <unistd.h>

#define OP_NONE   0
#define OP_UMODSI 1
#define OP_UDIVHI 2

#ifndef OP
#define OP OP_NONE
#endif
#ifndef ITERS
#define ITERS 1000
#endif

__attribute__((noinline)) void bench_begin(void) { __asm__ volatile (""); }
__attribute__((noinline)) void bench_end(void) { __asm__ volatile (""); }

static volatile unsigned long sink_l;
static volatile unsigned int sink_i;

__attribute__((noinline)) static void work(unsigned n)
{
    volatile unsigned long a = 0xffffffffUL, b = 7UL;
    volatile unsigned int c = 60000u, d = 7u;
    unsigned i;

    for (i = 0; i < n; i++) {
#if OP == OP_UMODSI
        sink_l = a % b;
#elif OP == OP_UDIVHI
        sink_i = c / d;
#else
        sink_l = a;
#endif
    }
}

int main(void)
{
    bench_begin();
    work(ITERS);
    bench_end();
    write(STDOUT_FILENO, "done\r\n", 6);
    return 0;
}
