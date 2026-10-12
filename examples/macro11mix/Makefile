# A program half in C and half in MACRO-11: see macro11mix.c for what
# the two halves are and mixasm.mac for the convention between them.
#
# What is different from the other examples here is the assembler.
# macro11 writes an RT-11 object module, which ld reads like any other
# input, and -elf writes a second file beside it holding the module's
# line numbers and symbols as DWARF -- the object module format has
# nowhere to keep them.  Both files go on the link line, and then every
# address of the program, in whichever half, has a name and a source
# line behind it.
OBJS:=	macro11mix.o mixasm.obj mixasm.dbg
CC:=pdp11-uknc-rt11-gcc
M11:=macro11
OPT_FLAG:=-O2
# -g is what puts DWARF in the C objects; -elf below does the same for
# the MACRO-11 one.
GCC_FLAGS:=-std=gnu23 -fomit-frame-pointer $(OPT_FLAG) -g

# No -ffunction-sections/-fdata-sections and no --gc-sections here,
# which every other example in this directory does use.  Collecting
# what nothing reaches works by following relocations, and it follows
# them only through ELF objects, which has two consequences for a
# program with a MACRO-11 half in it:
#
#   - the call this one's EACHWD makes to SHOWWD, a C function, is in
#     an RT-11 object module, so that walk never sees it.  Nothing else
#     calls SHOWWD, its section would go, and what is left is a JSR into
#     whatever ended up at that address instead.  -Wl,-u,SHOWWD would
#     answer that one, and every other C name only the MACRO-11 half
#     uses would need the same;
#   - mixasm.dbg would go entirely.  The linker keeps an input file's
#     debug sections only when some section of that same file is kept,
#     and a file that is nothing but debug information has none to keep
#     -- so the MACRO-11 half's line numbers, the point of this
#     example, would be collected and no one would say so.
#
# Both are worth knowing about before turning it on elsewhere.
LD_FLAGS:=

# The ELF with it, not only the disk image: the symbols are the point
# of this example, and the .sav has nowhere to keep them.
all:	macro11mix.dsk macro11mix.elf

%.o:	%.c
	${CC} ${GCC_FLAGS} -c -o $@ $<

# One run of the assembler writes both files, so mixasm.dbg has the
# object module as its only prerequisite and nothing of its own to do.
mixasm.obj:	mixasm.mac
	${M11} -o $@ -elf mixasm.dbg -l mixasm.lst $<

mixasm.dbg:	mixasm.obj ;

# MIX.SAV, not macro11mix.sav: an RT-11 filename is six characters and
# three, so the name on the disk -- what you type at the KMON prompt to
# run it -- is shortened.  The source filenames are not.
mix.sav:	$(OBJS)
	${CC} ${GCC_FLAGS} ${LD_FLAGS} -Wl,-Map=$@.map -o $@ $(OBJS)

# The same program linked as ELF instead of a flat image: same objects,
# same addresses, but with the symbols and DWARF that a .sav has
# nowhere to keep.  This is what gdb reads -- of both halves.
macro11mix.elf:	$(OBJS)
	${CC} ${GCC_FLAGS} ${LD_FLAGS} -Wl,-m,pdp11rt11 -o $@ $(OBJS)

macro11mix.dsk:	mix.sav
	rm -f macro11mix.dsk
	cp ../../resources/rt11os.dsk macro11mix.dsk
	rt11dsk a macro11mix.dsk mix.sav

clean:
	@-rm -f *.o *.obj *.dbg *.lst *.dsk *.sav.map *.sav *.elf
