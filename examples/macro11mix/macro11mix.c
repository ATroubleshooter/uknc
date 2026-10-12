/* A program half in C and half in MACRO-11.
 *
 * The C half is in charge: main() is here, and so is the C library the
 * program prints with.  The MACRO-11 half, mixasm.mac, holds two
 * routines this one calls, a table and the room to copy it into -- and
 * it calls back into one function here, so that the join is crossed in
 * both directions.
 *
 * The .mac is assembled by macro11, which writes an RT-11 object
 * module; ld reads one of those as readily as an ELF object (bfd's
 * obj-pdp11), so the two halves link together with nothing in between.
 *
 * Both halves are debugged the same way, which is the other thing this
 * example is for: -g puts DWARF in the C objects, -elf puts it beside
 * the object module, and the Makefile links an ELF with the lot.  gdb
 * then steps from main() into CSUM16 by MACRO-11 source line, and
 * addr2line names an address in either half:
 *
 *   (gdb) break mixasm.mac:43
 *   Breakpoint 1 at 0x3408: file mixasm.mac, line 43.
 *   (gdb) bt
 *   #0  SHOWWD (word=1) at macro11mix.c:42
 *   #1  EACHWD () at mixasm.mac:61
 *
 * -- a backtrace across the join.  It stops at the MACRO-11 frame and
 * does not reach main(): hand-written code carries no call-frame
 * information, and gdb has nothing to unwind by.
 */

#include <stdio.h>

/* The MACRO-11 half's names, spelt as an object module can hold them.
 * RADIX-50 packs three characters into a word out of an alphabet of
 * forty, and lowercase is not in it -- so a shared name is in capitals
 * and is six characters at most.  __asm__ puts exactly that name on
 * the symbol; left to itself the compiler would ask the linker for
 * _csum16, which RADIX-50 cannot even spell.
 */
extern unsigned short csum16(const unsigned short *words, int count)
    __asm__("CSUM16");
extern void eachwd(const unsigned short *words, int count) __asm__("EACHWD");

extern const unsigned short mixtab[] __asm__("MIXTAB");
extern const unsigned short mixcnt __asm__("MIXCNT");
extern unsigned short scrtch[] __asm__("SCRTCH");

/* And the other direction: EACHWD, in mixasm.mac, calls this.  */
void showwd(unsigned short word) __asm__("SHOWWD");

void showwd(unsigned short word)
{
    printf("  %06o\n", word);
}

int main(void)
{
    unsigned short i;

    printf("MIXTAB holds %u words:\n", mixcnt);
    eachwd(mixtab, mixcnt);
    printf("their checksum is %06o\n", csum16(mixtab, mixcnt));

    /* SCRTCH is the assembler's .BLKW, written from here.  */
    for (i = 0; i < mixcnt; i++)
        scrtch[i] = mixtab[i];
    scrtch[mixcnt - 1] += 1;
    printf("one word more in the copy: %06o\n", csum16(scrtch, mixcnt));

    return 0;
}
