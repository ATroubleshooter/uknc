# __umodsi3 and __udivhi3 -- libgcc's own against the hand-written
# routines in ../../stdlib/float/pdp11-fis.s, which defines the whole
# set of division helpers beside its floating point.  README.md has the
# numbers.
#
# `make' builds both variants, `make measure' times them on the machine
# and prints what each printed (the correctness pass), `make perop'
# prints what one operation costs in each.
CC:=pdp11-uknc-rt11-gcc
AS:=pdp11-uknc-rt11-as
SIZE:=pdp11-uknc-rt11-size
OPT_FLAG:=-O2
GCC_FLAGS:=-std=gnu23 -fomit-frame-pointer $(OPT_FLAG) -g

FISASM:=../../stdlib/float/pdp11-fis.s

# std -- what the compiler links by itself: libgcc's __udivmodsi4 and
#        __udivhi3
# asm -- the hand-written routines instead: the object defines the same
#        names, so the linker takes them and leaves libgcc's members
#        unread
VARIANTS:=std asm

all:	$(VARIANTS:%=id_%.sav) $(VARIANTS:%=id_%.elf)

fisasm.o:	$(FISASM)
	${AS} -o $@ $<

id_std.sav:	idivbench.c
	${CC} ${GCC_FLAGS} -o $@ $<
id_asm.sav:	idivbench.c fisasm.o
	${CC} ${GCC_FLAGS} -o $@ idivbench.c fisasm.o

id_std.elf:	idivbench.c
	${CC} ${GCC_FLAGS} -Wl,-m,pdp11rt11 -o $@ $<
id_asm.elf:	idivbench.c fisasm.o
	${CC} ${GCC_FLAGS} -Wl,-m,pdp11rt11 -o $@ idivbench.c fisasm.o

measure:	all
	@printf '%-5s %12s %7s %8s\n' вариант такты кадры text
	@port=3000; for v in $(VARIANTS); do \
	    port=$$((port + 1)); \
	    line=$$(./measure.sh id_$$v $$port | grep -m1 "profiling is off"); \
	    t=$$(echo "$$line" | sed -E 's/.*off, ([0-9]+) ticks.*/\1/'); \
	    f=$$(echo "$$line" | sed -E 's/.*\(([0-9.]+) frames.*/\1/'); \
	    z=$$(${SIZE} id_$$v.elf | tail -1 | awk '{print $$1}'); \
	    printf '%-5s %12s %7s %8s\n' "$$v" "$$t" "$$f" "$$z"; \
	done
	@echo "--- что они напечатали (должно совпадать)"
	@for v in $(VARIANTS); do \
	    printf '%-5s ' $$v; \
	    grep -hE "^(umodsi3|udivhi3|sum|result)" out_id_$$v.txt | tr '\n' ' '; \
	    echo; \
	done

# Цена каждой подпрограммы из pdp11-fis.s, двумя реализациями.
sweep:	fisasm.o
	./sweep.sh

perop:	fisasm.o
	./perop.sh

clean:
	@-rm -f *.o *.sav *.elf *.sav.map out_*.txt perop.txt sweep.txt
