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
# -ffunction-sections/-fdata-sections put every function and every
# variable in a section of its own, so that -Wl,--gc-sections below can
# drop the ones nothing reaches.
SECTION_FLAGS:=-ffunction-sections -fdata-sections
# -g is what puts DWARF in the C objects; -elf below does the same for
# the MACRO-11 one.
GCC_FLAGS:=-std=gnu23 -fomit-frame-pointer $(OPT_FLAG) $(SECTION_FLAGS) -g

# Collecting what nothing reaches wanted two things of the linker before
# a program built like this one could use it, and both are in
# patches/binutils/0022.  The collection follows relocations, and it
# follows them only through ELF objects: the call this program's EACHWD
# makes to SHOWWD is in an RT-11 object module, nothing else calls that
# function, and so its section went -- leaving a JSR into whatever took
# its place, with no diagnostic.  And an input file's debug sections are
# kept only when some section of that same file is kept, which a file
# that is nothing but debug information never has, so mixasm.dbg went
# whole and the MACRO-11 half's line numbers with it.  Now a symbol a
# module in another format leaves undefined is a root, and a file with
# no allocatable section keeps its DWARF.
LD_FLAGS:=-Wl,--gc-sections

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
