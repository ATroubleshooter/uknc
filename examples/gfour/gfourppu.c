// gfourppu.c -- PPU-side payload for the gfour example
//
// Waits for gfour.c to say where its screen buffer is (one 2-byte
// message: the buffer's plane offset -- see gfour.c's header comment
// for why a CPU heap buffer is video memory), builds a video display
// tag list in plane 0 pointing every visible scanline at it, then
// stays resident -- its own keyboard interrupt handler (see kbd_init()
// below) needs to stay installed for as long as gfour.c's own animation
// loop is still running -- but, unlike a program meant to stay resident
// for a whole game session, this program is only ever meant to run for
// the lifetime of one gfour demo: the moment its own keyboard ISR sees
// an actual key press (not
// just a release -- same criterion as gfour.c's own kbd_recv()), it
// forwards that event to the CPU as usual, then hands the *video*
// console back (restore_tag0() below -- see its own comment for why
// one saved tag is all that takes), restores vector 0300 to whatever
// the PPU-resident monitor had there before (kbd_shutdown() below),
// drops its channel-2 receiver, tells the CPU so (PPU_MSG_BYE), and
// returns from ppu_main(), letting ppus_start.c's own shim free this
// program's PPU memory block and hand control back to the resident
// monitor (see ppu_server.h's own ppus_exit() comment) -- exactly the
// same reasoning gfour.c's own vsync_init()/vsync_shutdown() pair
// already follows on the CPU side. Without the vector restore, an earlier
// version of this file left vector 0300 permanently pointing at this
// program's own (about-to-be-stale) kbd_recv_byte and never gave
// control back to the resident monitor at all: gfour.c's own animation
// loop still noticed the keypress fine (forwarding worked), but the
// whole system was left with no PPU program servicing the
// keyboard/console for RT-11 itself -- confirmed directly: the CPU
// program's own exit never reached a real RT-11 prompt, just stopped
// animating in place forever. And without the tag-0 restore, even
// after that fix the *display* stayed stuck on this program's own tag
// chain/palette forever, showing whatever it last drew -- nothing else
// ever points TAG_BASE back at RT-11's own chain on its own. Video
// itself needs nothing further while this program *is* running: the
// generator re-reads planes 0/1/2 every real frame on its own
// regardless of what PPU code is or isn't running.
//
// Keyboard input is PPU-only hardware (0177700 status/0177702 data, PPU
// interrupt vector 0300 -- confirmed against ukncbtl-qt's own
// emulator/emubase/Board.cpp, ChanRxStateGetCPU()/ChanReadByCPU()
// notwithstanding: those are channel 0, a CPU<->PPU *messaging* path
// RT-11's own .TTYIN happens to be plumbed through while its
// PPU-resident monitor is still driving the console -- not the
// keyboard controller itself, and no longer serviced once this
// program's own tag list has taken over the monitor's video state, the
// same way console output stops working then too -- see gfour.c's own
// header comment. This file's own earlier version tried polling
// channel 0 straight from the CPU for exactly this reason and it
// silently never saw a keypress once the PPU monitor stopped feeding
// it). The real, always-live path is 0177700/0177702, reachable only
// from the PPU -- confirmed against a real UKNC game's own PPU-side
// keyboard ISR (blairecas/descent, descnt_ppu.mac), and independently
// against the "УКНЦ Техническое описание" hardware manual. This file's
// own ISR (kbd_init()/kbd_recv_byte) mirrors descent's, forwarding raw
// scancodes to the CPU over libppu's own channel-1 PPU->CPU messaging
// (ppus_send()/ppuc_recv_init(), see ppu_server.h/ppu_client.h) instead
// of descent's own shared-CPU-RAM approach, since libppu already
// provides exactly this and gfour.c has no per-key state of its own to
// update directly.
//
// Tag list format (reverse-engineered from ukncbtl-qt's own decoder,
// emulator/Emulator.cpp:Emulator_PrepareScreenRGB32 -- not from any
// official datasheet, since no ROM/EMT-level graphics API or hardware
// manual for this turned up anywhere in this project): the video
// generator processes one 2- or 4-word "tag" per scanline, 307
// tags/frame (19 invisible + 288 visible), always starting with a
// fixed first tag at plane-0 byte address 0000270. A 2-word tag is
// {addressBits, tagB}; a 4-word tag prepends {word1, word2} (meaning
// depends on type). tagB's low bits control the *next* tag: bit 0
// toggles the cursor (unused here -- never set, so the cursor never
// appears), bit 1 says the next tag is 4-word, bit 2 (only meaningful
// if bit 1 is set) says that 4-word tag sets the palette (1) vs
// params (0); the rest of tagB, with those low bits masked off, is
// the next tag's own byte address (8-byte aligned if the next tag is
// 4-word, 4-byte aligned if 2-word). The very first tag of the frame
// is always treated as 2-word, regardless of its own content -- so
// tag 0, fixed at 0000270, only ever redirects into the real list
// below, at a separate fixed address (TAG_LIST_BASE).
//
// Where the pixels themselves live: in gfour.c's own malloc()ed buffer,
// at plane offset (CPU address / 2), received from the CPU at startup
// (see ppu_main()) -- the visible tags simply walk that buffer 40
// bytes per line. Plane 0 at those same offsets is *this* processor's
// own RAM (the monitor, this program, the tag list), so it is never
// touched: the palette below gives every odd slot the color of the
// even slot under it, which makes plane 0's bit invisible instead.
//
// This is the second address scheme this file has used. The first
// version pointed the tags at RT-11's own console text screen, plane
// offset 0100000 (found by decoding the live default tag list at boot,
// and confirmed by 3 real UKNC games that run under RT-11 --
// nzeemin/uknc-highwayencounter, uknc-loderunner, uknc-desolate -- all
// drawing there), which both processors can only reach through their
// window-register ports (CPU: 0176640/0176642, PPU: 0177010/0177012 --
// plane offsets >= 0100000 are outside both direct address ranges,
// confirmed against ukncbtl-qt/emulator/emubase/Memory.cpp's
// TranslateAddress logic), two I/O accesses per word. The CPU's own
// RAM being planes 1/2 below 0157777 made the buffer approach possible
// (Digger did it first; bolder and soft_scroll_test on GitHub did it
// years earlier); its one cost is the palette trick above, since the
// buffer's plane-0 counterpart can't be zeroed the way 0100000's could.
// A side benefit: RT-11's console memory is never written at all now,
// so its text screen is exactly as it was when this program exits.

#include "ppu_server.h"
#include "pdp11_irq.h"

#define TAG_BASE 0000270     /* fixed by hardware -- see file header comment */
#define LINE_STRIDE_BYTES 40 /* 320px / 8px-per-byte, per plane, per line */
#define INVISIBLE_LINES 19   /* yy=0..18 are never drawn to the screen */
#define VISIBLE_LINES 288    /* yy=19..306 */

// Sent to the CPU once the console is RT-11's again -- see ppu_main().
// Must match gfour.c's own definition.
#define PPU_MSG_BYE 0377

// Fixed, compile-time base for the real tag list (tag 0 at TAG_BASE
// above just redirects here -- see file header comment): a literal
// address rather than a C array, since this toolchain's a.out backend
// puts every global into .data regardless of whether it's ever
// initialized, and an array big enough for all 306 remaining tags
// (616 words) pushes a PPU module's linked .data size past an
// undiagnosed limit in this project's own `ld -r -m pdp11rt11rel`
// pass (confirmed directly: it fails with "cannot read section
// `.text': file truncated" once combined .text+.data crosses roughly
// 1340 bytes, well under what a real PPU program's free memory could
// otherwise hold) -- picked well above where a small compiled program
// starting at the usual load address (01000, see ppu_server.h) could
// ever reach, and well under plane 0's own direct-access ceiling
// (077777).
#define TAG_LIST_BASE 0060000

#define ALIGN4(x) (((x) + 3) & ~3)
#define ALIGN8(x) (((x) + 7) & ~7)

// Direct plane-0 access -- valid only for addr < 0100000 (see file
// header comment). Used for the tag list itself, which always lives
// well below that.
static void poke(unsigned int addr, unsigned int value) {
  *(volatile unsigned short *)addr = (unsigned short)value;
}

static unsigned short peek(unsigned int addr) {
  return *(volatile unsigned short *)addr;
}

// tag 0's original contents, saved by build_tags() before it
// overwrites them -- see restore_tag0()'s own comment for why putting
// them back matters.
static unsigned short saved_tag0_ab, saved_tag0_tagb;

// Builds the full 307-tag list (tag 0 at the fixed hardware address,
// the other 306 starting at TAG_LIST_BASE), the visible ones walking
// the CPU's screen buffer from plane offset screen_base, then loops it
// forever (the last visible-line tag points back to TAG_BASE).
static void build_tags(unsigned int screen_base) {
  unsigned int pos, next, addr_bits, i;

  pos = TAG_LIST_BASE;

  // Tag 0 (yy=0, fixed at TAG_BASE, always treated as 2-word): not
  // visible, only used to chain into the first real (4-word, params)
  // tag at TAG_LIST_BASE. Its *previous* contents are saved first --
  // this is the only word this program ever touches anywhere in
  // RT-11's own original, otherwise still fully intact tag chain
  // (everything from TAG_BASE+4 up to well below TAG_LIST_BASE is
  // never written by this file at all) -- see restore_tag0().
  saved_tag0_ab = peek(TAG_BASE);
  saved_tag0_tagb = peek(TAG_BASE + 2);
  poke(TAG_BASE, 0);           /* addressBits, unused (invisible) */
  poke(TAG_BASE + 2, pos | 2); /* next tag is 4-word, type=params */

  // Tag 1 (yy=1, 4-word params): scale=2 (320 visible px across the
  // full 640-dot raster), no brightness modifier (tag2's low 3 bits
  // set to 7 -> pbpgpr = (7-7)<<4 = 0, the "full brightness" palette
  // group), cursor left off (its bit in tagB is never set anywhere in
  // this list, so it never toggles on).
  next = ALIGN8(pos + 8);
  poke(pos, 0);                /* w1: cursor fields, all unused */
  poke(pos + 2, (1 << 4) | 7); /* w2: scale_raw=1 (-> scale=2), pbpgpr=0 */
  poke(pos + 4, 0);            /* addressBits, unused (invisible) */
  poke(pos + 6, next | 2 | 4); /* next tag is 4-word, type=palette */
  pos = next;

  // Tag 2 (yy=2, 4-word palette): a slot is indexed by plane0 |
  // plane1<<1 | plane2<<2. gfour.c only ever draws planes 1/2 (slots
  // 0/2/4/6); plane 0 under its buffer is this processor's own RAM,
  // not something to zero (see file header comment), so each odd slot
  // gets the even slot's color and plane 0's bit changes nothing.
  // Nibble values are this hardware's YRGB encoding (confirmed against
  // ukncbtl-qt's own color table, group 0 = pbpgpr's "full brightness"
  // row): 0=black, 0xA=bright green, 0xC=bright red, 0xF=white.
  next = ALIGN4(pos + 8);
  poke(pos, 0xAA00);     /* w1: slots 0/1=black, slots 2/3=green */
  poke(pos + 2, 0xFFCC); /* w2: slots 4/5=red, slots 6/7=white */
  poke(pos + 4, 0);      /* addressBits, unused (invisible) */
  poke(pos + 6, next);   /* next tag is 2-word (bit1=0) */
  pos = next;

  // Remaining invisible lines (yy=3..18): plain 2-word filler tags --
  // content doesn't matter, nothing is ever drawn from them.
  for (i = 3; i < INVISIBLE_LINES; i++) {
    next = ALIGN4(pos + 4);
    poke(pos, 0);
    poke(pos + 2, next);
    pos = next;
  }

  // Visible lines (yy=19..306): addressBits walks the CPU's screen
  // buffer, one scanline at a time, from screen_base.
  addr_bits = screen_base;
  for (i = 0; i < VISIBLE_LINES; i++) {
    int last = (i == VISIBLE_LINES - 1);

    next = last ? TAG_BASE : ALIGN4(pos + 4);
    poke(pos, addr_bits);
    poke(pos + 2, next);
    pos = next;
    addr_bits += LINE_STRIDE_BYTES;
  }
}

// Puts tag 0 back to whatever build_tags() found there, so the video
// generator resumes walking RT-11's own original tag chain (still
// fully intact -- see build_tags()'s own comment) instead of this
// program's TAG_LIST_BASE chain, which nothing else will ever redirect
// away from otherwise (ppus_exit() only frees this program's own PPU
// memory *allocation*; TAG_LIST_BASE was deliberately placed outside
// that allocated range in the first place -- see its own comment -- so
// freeing never touches it, and TAG_BASE itself would otherwise be
// left pointing there forever). Called from ppu_main() right before it
// returns, alongside kbd_shutdown(). RT-11's own screen memory was
// never written, so what comes back is the console exactly as the
// user left it.
static void restore_tag0(void) {
  poke(TAG_BASE, saved_tag0_ab);
  poke(TAG_BASE + 2, saved_tag0_tagb);
}

// Keyboard controller registers (PPU-only -- see file header comment).
// Status bit 7 (0200) = a scancode is waiting; bit 6 (0100) = interrupt
// enable, the only writable bit. Data is a 7-bit scancode: a *press*
// carries the full code (bits 0-3 = row, bits 4-6 = column, bit 7
// clear); a *release* carries only the row (bits 0-3), bit 7 set, and
// is only ever sent once a whole row's worth of keys has gone up --
// gfour.c only cares about "some press happened", so it just checks
// bit 7 itself rather than this file trying to track row/column state.
#define KBD_CSR (*(volatile unsigned char *)0177700)
#define KBD_DATA (*(volatile unsigned char *)0177702)

// Set by kbd_recv_byte, drained by ppu_main()'s own loop -- a single
// slot, not a queue: a press arriving while the previous one is still
// unsent overwrites it. Fine for gfour's own "did any key go down"
// use -- see gfour.c's own header comment for why a real queue isn't
// needed here (unlike descent's own per-key state array, which has no
// such gap since it updates persistent state directly instead of
// forwarding discrete events).
static volatile unsigned char kbd_pending;
static volatile unsigned char kbd_event;

// The interrupt entry point (kbd_recv_byte itself, declared by this
// same macro -- see its own comment in pdp11_irq.h -- so kbd_init()
// below can just take its address directly) and its callback body (a
// separate, generated kbd_recv_byte_impl) are both produced by this
// one definition. b is the one scancode
// byte the trampoline read from @$0177702 (exactly once -- re-reading
// it would consume/re-latch whatever the *next* scancode is, not
// re-read this one, confirmed against descnt_ppu.mac's own handler,
// which carries an explicit comment for the same reason). Heavier than
// the hand-written version this replaces (that one saved only r0 and
// stored the byte directly with two MOVBs, no call at all) -- saving
// and restoring all 6 registers around a real jsr/rts costs real
// cycles/words this ISR never needed, but reuses the exact same
// trampoline shape as libppu's ppuc_recv_isr/ppus_recv_isr instead of a
// second hand-written copy.
PDP11_IRQ_RECV_BYTE(kbd_recv_byte, 0177702) {
  kbd_event = b;
  kbd_pending = 1;
}

// Saved by kbd_init(), restored by kbd_shutdown() -- same save/restore
// discipline as gfour.c's own vsync_init()/vsync_shutdown(), just on
// the PPU side and for vector 0300 instead of 0100.
static struct pdp11_vector saved_kbd_vec;

// Installs kbd_recv_byte over vector 0000300 (saving whatever was
// there before, for kbd_shutdown() to put back, via
// pdp11_irq_vector_swap() -- see pdp11_irq.h -- for the same reason
// ppu_main()'s own comment explains this program restores it instead
// of leaving it permanently overwritten the way an earlier version
// did) and enables the keyboard's own interrupt (KBD_CSR's bit 0100).
// pdp11_irq_vector_swap() itself masks interrupts for the swap, so an
// already-pending keystroke can't fire mid-install and land on a
// half-written vector.
static void kbd_init(void) {
  struct pdp11_vector v;

  v.pc = (unsigned short)(unsigned int)kbd_recv_byte;
  v.psw = 0200;
  saved_kbd_vec = pdp11_irq_vector_swap(0300, v);
  KBD_CSR = 0100;
}

// Puts vector 0000300 back to whatever kbd_init() found there -- called
// right before ppu_main() returns (see its own comment), the same way
// gfour.c's own vsync_shutdown() runs right before that program's
// main() returns.
static void kbd_shutdown(void) {
  pdp11_irq_vector_set(0300, saved_kbd_vec);
}

// gfour.c's one message: its screen buffer's plane offset. Set from
// ppus_recv_init()'s interrupt-context callback, read by ppu_main()
// outside it; 0 is never a valid offset (that would be CPU address 0,
// the interrupt vectors), so it doubles as "not arrived yet".
static volatile unsigned int screen_base_msg;

static void on_receive(const void *buf, unsigned int size) {
  if (size >= 2) {
    screen_base_msg = *(const unsigned short *)buf;
  }
}

void ppu_main(void) {
  unsigned char bye = PPU_MSG_BYE;

  ppus_recv_init(on_receive);
  while (screen_base_msg == 0) {
  }
  build_tags(screen_base_msg);
  kbd_init();

  // Stays resident until an actual key press (not a release) comes
  // through -- see the file header comment for why this can no longer
  // just build the tag list and return the way an even earlier version
  // did, and for why it also can't stay resident forever the way
  // diggerppu.c does. ppus_send() itself is an ordinary blocking call
  // (see ppu_server.h), safe to make from this normal foreground loop
  // even though it would not be safe to make directly from
  // kbd_recv_byte's own callback body (running in interrupt context)
  // -- exactly why it only ever sets kbd_pending/kbd_event and leaves
  // the actual send to here.
  for (;;) {
    if (kbd_pending) {
      unsigned char event = kbd_event;

      kbd_pending = 0;
      ppus_send(&event, 1);
      if ((event & 0200) == 0) {
        // A press, same criterion as gfour.c's own kbd_recv() -- stop
        // servicing the keyboard here and hand it back to the
        // resident monitor (see kbd_shutdown() and the file header
        // comment).
        break;
      }
    }
  }

  // Reverse of the startup, then the bye -- which must come *after*
  // ppus_recv_shutdown(): gfour.c's .EXIT follows the bye almost at
  // once, RT-11 talks to the resident monitor over the same channel 2,
  // and a request landing while this program's receiver was still
  // armed would be swallowed by on_receive() -- RMON would then wait
  // for an answer forever. ppus_recv_shutdown() also puts the channel's
  // RX interrupt enable back the way the monitor had it (see its own
  // comment in libppu) -- it is how that request reaches the monitor.
  kbd_shutdown();
  restore_tag0();
  ppus_recv_shutdown();
  ppus_send(&bye, 1);
}
