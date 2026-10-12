/* One float operation per iteration, so the per-operation cost can be
   read off the tick counter.  OP picks which; OP_NONE measures the loop
   itself, to subtract.  */
#include <unistd.h>

#define OP_NONE 0
#define OP_ADD  1
#define OP_MUL  2
#define OP_DIV  3

#ifndef OP
#define OP OP_NONE
#endif
#ifndef ITERS
#define ITERS 1000
#endif

__attribute__((noinline)) void bench_begin(void) { __asm__ volatile (""); }
__attribute__((noinline)) void bench_end(void) { __asm__ volatile (""); }

static volatile float sink;

__attribute__((noinline)) static void work(unsigned n)
{
    volatile float a = 3.0f, b = 1.0009765625f;
    unsigned i;

    for (i = 0; i < n; i++) {
#if OP == OP_ADD
        sink = a + b;
#elif OP == OP_MUL
        sink = a * b;
#elif OP == OP_DIV
        sink = a / b;
#else
        sink = a;
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
