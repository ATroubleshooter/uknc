// gfourppu.c -- PPU-side payload for the gfour example
//
// Waits for gfour.c to say where its screen buffer is (one 2-byte
// message: the buffer's plane offset -- see gfour.c's header comment
// for why a CPU heap buffer is video memory), builds a video display
// tag list in plane 0 pointing every visible scanline at it, then stays
// resident: its own keyboard interrupt handler (see kbd_init() below)
// has to stay installed for as long as gfour.c's animation loop is
// running. It is meant to live exactly as long as one gfour run, so the
// moment its keyboard ISR sees an actual key press (not just a release
// -- the same criterion as gfour.c's own kbd_recv()) it forwards that
// event to the CPU as usual, then hands the *video* console back
// (restore_tag0() below -- see its own comment for why one saved tag is
// all that takes), restores vector 0300 to whatever the PPU-resident
// monitor had there before (kbd_shutdown() below), drops its channel-2
// receiver, tells the CPU so (PPU_MSG_BYE), and returns from
// ppu_main(), letting ppus_start.c's shim free this program's PPU
// memory block and hand control back to the resident monitor (see
// ppu_server.h's own ppus_exit() comment) -- the same discipline
// gfour.c's own vsync_init()/vsync_shutdown() pair follows on the CPU
// side.
//
// Both restores matter, and for different reasons. Vector 0300 left
// pointing into this program's memory after ppus_exit() has freed it
// leaves RT-11 with nothing servicing the keyboard and console at all:
// the next keyboard interrupt lands on whatever is there by then. And
// tag 0 left pointing at this program's own chain leaves the display
// showing whatever it last drew, because nothing else ever points
// TAG_BASE back at RT-11's chain. Video itself needs nothing further
// while this program runs: the generator re-reads planes 0/1/2 every
// frame on its own, whatever PPU code is or is not running.
//
// Keyboard input is PPU-only hardware: status at 0177700, data at
// 0177702, PPU interrupt vector 0300 (the "УКНЦ Техническое описание"
// hardware manual has them). Channel 0 is not that -- it is a
// CPU<->PPU messaging path that RT-11's .TTYIN happens to be plumbed
// through while the PPU-resident monitor is still driving the console,
// and it stops being serviced once this program's tag list has taken
// the monitor's video state over, the same way console output stops
// working then (see gfour.c's header comment). This file's ISR
// (kbd_init()/kbd_recv_byte) forwards raw scancodes to the CPU over
// libppu's channel-1 PPU->CPU messaging (ppus_send()/ppuc_recv_init(),
// see ppu_server.h/ppu_client.h); gfour.c has no per-key state of its
// own to update, so discrete events are all it needs.
//
// Tag list format, as the video generator reads it (no datasheet for
// this: what follows is what the hardware does, worked out from the
// list RT-11 itself leaves at boot and from what comes out on screen).
// The generator processes one 2- or 4-word "tag" per scanline, 307
// tags/frame (19 invisible + 288 visible), always starting with a fixed
// first tag at plane-0 byte address 0000270. A 2-word tag is
// {addressBits, tagB}; a 4-word tag prepends {word1, word2} (meaning
// depends on type). tagB's low bits control the *next* tag: bit 0
// toggles the cursor (unused here -- never set, so the cursor never
// appears), bit 1 says the next tag is 4-word, bit 2 (only meaningful
// if bit 1 is set) says that 4-word tag sets the palette (1) vs params
// (0); the rest of tagB, with those low bits masked off, is the next
// tag's own byte address (8-byte aligned if the next tag is 4-word,
// 4-byte aligned if 2-word). The very first tag of the frame is always
// treated as 2-word, regardless of its own content -- so tag 0, fixed
// at 0000270, only ever redirects into the real list below, at a
// separate fixed address (TAG_LIST_BASE).
//
// Where the pixels themselves live: in gfour.c's own malloc()ed buffer,
// at plane offset (CPU address / 2), received from the CPU at startup
// (see ppu_main()) -- the visible tags simply walk that buffer 40 bytes
// per line. Plane 0 at those same offsets is *this* processor's own RAM
// (the monitor, this program, the tag list), so it is never touched:
// the palette below gives every odd slot the color of the even slot
// under it, which makes plane 0's bit invisible instead. That palette
// trick is what a CPU buffer costs. The alternative is RT-11's own
// console text screen at plane offset 0100000, which both processors
// can reach only through their window-register ports (CPU:
// 0176640/0176642, PPU: 0177010/0177012 -- plane offsets >= 0100000
// are outside both direct address ranges), two I/O accesses per word;
// examples/sin draws there, having no room for a buffer of its own.
// RT-11's console memory is never written here at all, so its text
// screen is exactly as it was when this program exits.

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
// address rather than a C array: an array big enough for all 306
// remaining tags (616 words) pushes a PPU module past a limit in the
// `ld -r -m pdp11rt11rel` pass, which fails with "cannot read section
// `.text': file truncated" once its combined .text and .data cross
// roughly 1340 bytes -- well under what a PPU program's free memory
// could otherwise hold. The address is picked well above where a small
// compiled program
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
  // Nibble values are this hardware's YRGB encoding, in the "full
  // brightness" palette group tag 1 selects: 0=black, 0xA=bright
  // green, 0xC=bright red, 0xF=white.
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
// unsent overwrites it. All gfour.c asks is whether any key went down,
// so a dropped second press costs it nothing; a program that tracked
// each key would want state instead of discrete events.
static volatile unsigned char kbd_pending;
static volatile unsigned char kbd_event;

// The interrupt entry point (kbd_recv_byte itself, declared by this
// same macro -- see its own comment in pdp11_irq.h -- so kbd_init()
// below can just take its address directly) and its callback body (a
// separate, generated kbd_recv_byte_impl) are both produced by this
// one definition. b is the one scancode byte the trampoline read from
// @$0177702, and it is read exactly once: re-reading the data register
// consumes or re-latches whatever the *next* scancode is rather than
// giving this one again. The trampoline saves and restores all six
// registers around a real jsr/rts, which this ISR does not need for a
// byte store; what it buys is the same trampoline shape libppu's own
// ppuc_recv_isr/ppus_recv_isr use, rather than a hand-written copy per
// interrupt.
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
// pdp11_irq_vector_swap() -- see pdp11_irq.h, and the file header
// comment for why the vector has to go back) and enables the
// keyboard's own interrupt (KBD_CSR's bit 0100).
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
  // through: returning as soon as the tag list is built would take the
  // keyboard handler with it, and staying resident for good would
  // never give RT-11 its console back -- see the file header comment.
  // ppus_send() itself is an ordinary blocking call
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
