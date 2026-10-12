# Three ways of doing single-precision floats on this machine, measured
# against each other: the FIS instructions the compiler emits itself,
# the hand-written FIS routines in ../../stdlib/float/pdp11-fis.s, and
# libgcc's own software floating point.  README.md has the numbers and
# what they mean.
#
# `make' builds the four variants of fbench.c, `make measure' times them
# on the machine, `make perop' prints what one operation costs in each.
CC:=pdp11-uknc-rt11-gcc
AS:=pdp11-uknc-rt11-as
SIZE:=pdp11-uknc-rt11-size
OPT_FLAG:=-O2
GCC_FLAGS:=-std=gnu23 -fomit-frame-pointer $(OPT_FLAG) -g

# The hand-written routines, assembled from where they live.  They are
# call-compatible with what the compiler emits for soft float: argument
# words on the stack, a hidden pointer to the result ahead of them.
FISASM:=../../stdlib/float/pdp11-fis.s

VARIANTS:=fis soft asm both

all:	$(VARIANTS:%=fb_%.sav) $(VARIANTS:%=fb_%.elf)

fisasm.o:	$(FISASM)
	${AS} -o $@ $<

# fis   -- FADD/FSUB/FMUL/FDIV emitted inline (this toolchain's default)
# soft  -- no FIS at all: libgcc's software floating point
# asm   -- no FIS from the compiler; the hand-written routines instead
# both  -- inline FIS, and the hand-written helpers for what FIS has no
#          instruction for (comparisons, integer conversions)
fb_fis.sav:	fbench.c
	${CC} ${GCC_FLAGS} -o $@ $<
fb_soft.sav:	fbench.c
	${CC} ${GCC_FLAGS} -mno-fis -o $@ $<
fb_asm.sav:	fbench.c fisasm.o
	${CC} ${GCC_FLAGS} -mno-fis -o $@ fbench.c fisasm.o
fb_both.sav:	fbench.c fisasm.o
	${CC} ${GCC_FLAGS} -o $@ fbench.c fisasm.o

# The same objects linked as ELF: same addresses, but with the symbols
# gdb needs to put a breakpoint on the markers.
fb_fis.elf:	fbench.c
	${CC} ${GCC_FLAGS} -Wl,-m,pdp11rt11 -o $@ $<
fb_soft.elf:	fbench.c
	${CC} ${GCC_FLAGS} -mno-fis -Wl,-m,pdp11rt11 -o $@ $<
fb_asm.elf:	fbench.c fisasm.o
	${CC} ${GCC_FLAGS} -mno-fis -Wl,-m,pdp11rt11 -o $@ fbench.c fisasm.o
fb_both.elf:	fbench.c fisasm.o
	${CC} ${GCC_FLAGS} -Wl,-m,pdp11rt11 -o $@ fbench.c fisasm.o

measure:	all
	@printf '%-6s %12s %7s %8s\n' вариант такты кадры text
	@port=2900; for v in $(VARIANTS); do \
	    port=$$((port + 1)); \
	    line=$$(./measure.sh fb_$$v $$port | grep -m1 "profiling is off"); \
	    t=$$(echo "$$line" | sed -E 's/.*off, ([0-9]+) ticks.*/\1/'); \
	    f=$$(echo "$$line" | sed -E 's/.*\(([0-9.]+) frames.*/\1/'); \
	    z=$$(${SIZE} fb_$$v.elf | tail -1 | awk '{print $$1}'); \
	    printf '%-6s %12s %7s %8s\n' "$$v" "$$t" "$$f" "$$z"; \
	done

perop:	fisasm.o
	./perop.sh

clean:
	@-rm -f *.o *.sav *.elf *.sav.map out_*.txt perop.txt
