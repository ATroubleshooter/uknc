// gfour.c -- animates 3 colored squares bouncing around a black
// 320x288 screen until any key is pressed (or NUM_FRAMES elapses with
// none, as a safety cap for an unattended run), then exits.
//
// The first real graphics output in this project: the video
// generator's plane 0 (tag list, palette, horizontal scale) is
// entirely PPU-owned (see gfourppu.c). Pixel data lives in an ordinary
// malloc()ed buffer of this program's own (`screen` below), written
// through a plain pointer: the CPU's whole RAM *is* video planes 1/2
// interleaved (CPU byte 2N = plane1[N], 2N+1 = plane2[N] -- confirmed
// against ukncbtl's emubase/Memory.cpp, ADDRTYPE_RAM12 for every
// address below 0160000), and the PPU's tag list can point a scanline
// at any plane offset at all, so the buffer's address / 2 is all the
// PPU needs to display it -- sent over once, right after ppuc_run()
// (see main()). Two shipped UKNC games do exactly this
// (blairecas/bolder, aberranthacker/soft_scroll_test), and so does
// this project's own Digger port, where it replaced the version of
// this scheme this file used first: pixels in RT-11's own console
// screen memory at plane offset 0100000 -- above the CPU's directly
// addressable range -- reached one word at a time through the
// address/data window ports 0176640/0176642, two I/O writes (and, for
// a read-modify-write, a read) per word. The ports are gone from this
// file now; gfourppu.c's header comment keeps the account of that
// address.
//
// Digger has to split its screen (HUD through the ports, playfield in
// a heap buffer) and move its stack out of the way; this program is
// small enough that the full 288-line buffer (23040 bytes) fits between
// its own _end and the stack RT-11 gave it, so none of that is needed
// here.
//
// Keypresses reach this side via libppu's own channel-1 PPU->CPU
// messaging (ppuc_recv_init(), see ppu_client.h) instead of any kind
// of polling: gfourppu.c's own keyboard interrupt handler (see its
// header comment) forwards raw scancodes as they happen, and
// kbd_recv() below just watches for one that looks like a press. This
// replaces an earlier version of this file that instead polled
// channel 0's CPU-side status/data registers (0177560/0177562) --
// the same ones EMT 0340/.TTYIN uses -- directly, hoping to read the
// keyboard without RT-11's own blocking read(); that never saw a
// keypress at all, since channel 0 only carries whatever the
// PPU-resident monitor's own console driver is still choosing to send
// over it, and this program's tag list (see gfourppu.c) has already
// taken over the monitor's video/keyboard state the same way it
// breaks console *output* too (see the comment right before
// ppuc_run() below).
//
// Console messages go through write() rather than printf(): mixing
// buffered stdio with real file I/O has a known, unresolved GCC
// pdp11-backend codegen bug on this toolchain (see examples/ppurun's
// own header comment). They only ever happen before ppuc_run() --
// console I/O only works again once the PPU side has actually handed
// control back to its own resident monitor (see examples/ppupong's
// own header comment, and gfourppu.c's own header comment for when
// gfourppu.c itself does that), and every write()/read() attempted
// between ppuc_run() and that point has been observed to block,
// possibly forever -- this program never attempts one, since it has
// no way to know exactly when gfourppu.c has handed control back
// without asking first, and its own kbd_recv()/any_key_pressed
// mechanism below already covers everything it needs from the
// keyboard -- exactly why that has its own, non-console channel.

#include <stdlib.h>
#include <unistd.h>

#include "ppu_client.h"
#include "pdp11_irq.h"

#define SCREEN_W 320
#define SCREEN_H 288
#define LINE_WORDS                                                             \
  (SCREEN_W / 8) /* one word = 8px: low byte plane 1, high byte plane 2 */

// The byte gfourppu.c sends back once it has handed the console over
// to RT-11 again (see its ppu_main()) -- distinguishable from any
// scancode, press (bit 7 clear) or release (row number, bit 7 set).
// Must match gfourppu.c's own definition.
#define PPU_MSG_BYE 0377

#define SQ_WIDTH 24 /* square width, in pixels */
#define SQ_ROWS 24  /* square height, in pixels */
#define NUM_SQUARES 3
#define NUM_FRAMES                                                             \
  3000 /* safety cap: stop even if no key ever comes (see kbd_recv()) */

// Incremented by vsync_tick (see vsync_init() below) on every
// EVNT trap -- vector 0000100, this hardware's line-clock interrupt,
// confirmed earlier this session (against ukncbtl-qt's own Board.cpp
// timing model) to fire twice per 40ms video frame, the second
// landing right after the last visible scanline. wait_vsync() below
// waits for this to advance by 2 (a full ~40ms video frame, not just
// one ~20ms half of one) before returning.
//
// Just one tick wasn't enough headroom back when every screen word
// went through the window ports: fill_rect_px()'s per-pixel
// horizontal positioning needs a read-modify-write for every
// partially-covered edge byte (see its own comment), which made a
// single square's erase-move-draw noticeably heavier than the
// original always-byte-aligned version -- confirmed directly, this
// example visibly flickered again once movement went per-pixel, the
// same symptom as the original no-vsync-at-all version, meaning
// drawing was again finishing mid-scan on at least some frames rather
// than safely inside the blanking gap. Waiting a full frame instead
// of a half doubles that gap. The plain-pointer buffer is far cheaper
// per word; the full-frame wait stays because it's the right pacing
// anyway.
static volatile unsigned int vsync_count;

// The interrupt entry point (vsync_tick itself, declared by this same
// macro -- see its own comment in pdp11_irq.h -- so vsync_init() below
// can just take its address directly) and its callback body (a
// separate, generated vsync_tick_impl) are both produced by this one
// definition. Heavier than the hand-written version this replaces (that
// one was a bare `INC
// _vsync_count; RTI` -- INC operates directly on the memory operand,
// so it never touched a register at all, nothing to save/restore):
// saving and restoring all 6 registers around a real jsr/rts, twice
// per video frame, costs real cycles this ISR never needed, but reuses
// the same trampoline shape as gfourppu.c's own kbd_recv_byte instead
// of a second hand-written one-off.
PDP11_IRQ_HANDLER(vsync_tick) { vsync_count++; }

static struct pdp11_vector saved_evnt;

// Installs vsync_tick over vector 0000100 (saving whatever was
// there before, for vsync_shutdown() to put back, via
// pdp11_irq_vector_swap() -- see pdp11_irq.h) so it can actually fire.
// RT-11 itself may depend on this same vector for its own
// clock/scheduling, hence the save/restore discipline -- matching
// libppu's own ppus_recv_init() precedent for a different vector (see
// ../../libs/libppu/ppus_recv.c). pdp11_irq_vector_swap() itself masks
// interrupts for the swap, so an already-pending EVNT can't fire
// mid-install and land on a half-written vector.
static void vsync_init(void) {
  struct pdp11_vector v;

  v.pc = (unsigned short)(unsigned int)vsync_tick;
  v.psw = 0200;
  saved_evnt = pdp11_irq_vector_swap(0100, v);
}

// Puts vector 0000100 back to whatever vsync_init() found there.
// Must be called before this program exits -- RT-11's own resident
// use of this vector (if any) must not end up pointing at memory this
// program no longer owns once it's gone.
static void vsync_shutdown(void) {
  pdp11_irq_vector_set(0100, saved_evnt);
}

// Busy-waits for a full video frame (2 EVNT traps) to elapse -- called
// once per animation frame, right before that frame's drawing.
// Unsigned wraparound-safe: works even if vsync_count wraps past
// 65535 while this is waiting.
static void wait_vsync(void) {
  unsigned int start = vsync_count;

  while (vsync_count - start < 2) {
  }
}

// Set by kbd_recv() (see main()'s own ppuc_recv_init() call), read by
// the animation loop below -- volatile since it's written from inside
// an interrupt handler (channel 1's own, vector 0460, installed by
// ppuc_recv_init() itself) and read from ordinary foreground code.
static volatile int any_key_pressed;

// Set by kbd_recv() on PPU_MSG_BYE: gfourppu.c has restored RT-11's
// tag list, keyboard vector and channel-2 receiver, so RT-11 can be
// talked to (i.e. this program can exit) again -- see main().
static volatile int ppu_bye;

// gfourppu.c's own keyboard ISR forwards every scancode, press and
// release alike (see its own header comment for the format: a press
// carries the full 7-bit code with bit 7 clear, a release carries
// only a row number with bit 7 set) -- this only cares that *some*
// press happened, so a release (bit 7 set) is simply ignored.
//
// Runs inside ppuc_recv_init()'s own interrupt handler (see
// ppu_client.h's own contract: keep it short, no ppuc_*-family calls
// that themselves wait on an interrupt) -- setting one volatile flag
// is all it does.
static void kbd_recv(const void *buf, unsigned int size) {
  const unsigned char *p = (const unsigned char *)buf;

  if (size < 1) {
    return;
  }
  if (p[0] == PPU_MSG_BYE) {
    ppu_bye = 1;
  } else if ((p[0] & 0200) == 0) {
    any_key_pressed = 1;
  }
}

static void msg(const char *s) {
  unsigned int len = 0;

  while (s[len] != 0) {
    len++;
  }
  write(STDOUT_FILENO, s, len);
}

// Small 16-bit LCG (classic constants) -- avoids pulling in newlib's
// rand()/srand() state for something this cosmetic. Low bits of a
// plain LCG have a very short period (bit 0 <=2, bit 1 <=4, ... --
// confirmed directly earlier in this example's own history: masking
// the raw low bits produced perfectly regular, non-random output), so
// every caller here reads bits 6 and up instead.
static unsigned int rng_state = 1;
static unsigned int next_rand(void) {
  rng_state = rng_state * 25173u + 13849u;
  return (rng_state >> 6);
}

// The screen: SCREEN_H lines of LINE_WORDS words (8 pixels each: low
// byte plane 1, high byte plane 2), malloc()ed by main() -- see the
// file header comment for why an ordinary heap buffer *is* video
// memory here, and how the PPU learns where it is. volatile: the video
// generator reads it behind the compiler's back, so every store must
// really happen, in order.
static volatile unsigned short *screen;

static volatile unsigned short *screen_line(unsigned int row) {
  return screen + row * LINE_WORDS;
}

// color: 0-3 (bit 0 -> plane 1, bit 1 -> plane 2); 0 is black, used
// here to erase a square's old position as well as to paint one.
static unsigned short color_word(unsigned int color) {
  return (unsigned short)((color & 1) ? 0x00ff : 0) |
         (unsigned short)((color & 2) ? 0xff00 : 0);
}

// Zeroes the whole screen buffer -- called once at the very start:
// fresh from malloc() it holds whatever the heap held before (mostly
// the .PPU file ppuc_load_code() just read and freed). Nothing needs
// clearing on the way out any more: RT-11's own console memory is
// never written by this program now, so its screen comes back exactly
// as it was left -- gfourppu.c only ever redirected the tag list away
// from it and back.
static void clear_screen(void) {
  unsigned int n = (unsigned int)SCREEN_H * LINE_WORDS;
  volatile unsigned short *p = screen;

  while (n-- != 0) {
    *p++ = 0;
  }
}

// Fills a SQ_WIDTH x SQ_ROWS block with its top-left corner at pixel
// (x, row0) -- x need not be a multiple of 8: a screen word covers 8
// pixels, one bit each, the same bit position in both planes' bytes,
// so a byte the square only partially overlaps (its left or right
// edge) is read, has just the covered bits replaced, and written
// back; a byte it fully covers skips straight to a plain write, the
// same fast path fill_rect() used before this could only ever move in
// whole 8-pixel steps.
static void fill_rect_px(int x, int row0, unsigned int color) {
  unsigned short word = color_word(color);
  int byte_lo = x >> 3;
  int byte_hi = (x + SQ_WIDTH - 1) >> 3;
  int r, bc;

  for (r = 0; r < SQ_ROWS; r++) {
    volatile unsigned short *line = screen_line((unsigned int)(row0 + r));

    for (bc = byte_lo; bc <= byte_hi; bc++) {
      int bit_start = (bc == byte_lo) ? (x & 7) : 0;
      int bit_end = (bc == byte_hi) ? ((x + SQ_WIDTH - 1) & 7) : 7;

      if (bit_start == 0 && bit_end == 7) {
        line[bc] = word;
      } else {
        unsigned short bitmask =
            (unsigned short)(((2 << bit_end) - 1) & ~((1 << bit_start) - 1));
        unsigned short fullmask = (unsigned short)(bitmask | (bitmask << 8));

        line[bc] = (unsigned short)((line[bc] & ~fullmask) | (word & fullmask));
      }
    }
  }
}

// One bouncing square: x/row track its current top-left corner, both
// in pixels; dx/drow are its current per-frame velocity, reversed on
// hitting an edge. Horizontal and vertical motion are identical in
// kind (both per-pixel, both magnitude 1-4px/frame) -- fill_rect_px()
// above is what makes arbitrary (not just 8-pixel-aligned) horizontal
// positions possible at all.
struct square {
  int x, row;
  int dx, drow;
  unsigned int color;
};

static int random_speed(void) {
  int speed = (int)(1 + (next_rand() & 3)); /* 1..4 px/frame */

  return (next_rand() & 1) ? speed : -speed;
}

static void init_square(struct square *s, unsigned int color) {
  s->x = (int)(next_rand() % (unsigned int)(SCREEN_W - SQ_WIDTH));
  s->row = (int)(next_rand() % (unsigned int)(SCREEN_H - SQ_ROWS));
  s->dx = random_speed();
  s->drow = random_speed();
  s->color = color;
}

// Moves one square by its current velocity, bouncing (reversing the
// relevant velocity component and clamping back inside the screen)
// off any edge it would otherwise cross.
static void step_square(struct square *s) {
  int x = s->x + s->dx;
  int row = s->row + s->drow;

  if (x < 0) {
    x = 0;
    s->dx = -s->dx;
  } else if (x > SCREEN_W - SQ_WIDTH) {
    x = SCREEN_W - SQ_WIDTH;
    s->dx = -s->dx;
  }

  if (row < 0) {
    row = 0;
    s->drow = -s->drow;
  } else if (row > SCREEN_H - SQ_ROWS) {
    row = SCREEN_H - SQ_ROWS;
    s->drow = -s->drow;
  }

  s->x = x;
  s->row = row;
}

int main(void) {
  long ppu_addr;
  struct square sq[NUM_SQUARES];
  unsigned int i, frame;
  unsigned short screen_base;

  msg("gfour: loading GFPPU.PPU...\r\n");
  ppu_addr = ppuc_load_code("GFPPU.PPU");
  if (ppu_addr < 0) {
    msg("gfour: ppuc_load_code failed\r\n");
    return 1;
  }

  // The screen buffer -- after ppuc_load_code() (the only other
  // malloc() user, whose freed file buffer this then reuses) and
  // before ppuc_run() (while the console still works, so a failure can
  // still say so). malloc() returns even addresses, so the plane
  // offset is exactly address / 2 -- see the file header comment.
  screen = malloc((unsigned int)SCREEN_H * LINE_WORDS * 2);
  if (screen == NULL) {
    msg("gfour: no memory for the screen buffer\r\n");
    return 1;
  }
  screen_base = (unsigned short)((unsigned int)screen >> 1);

  /* 1/2/3 -> palette slots 2/4/6 (see gfourppu.c): green, red, white. */
  init_square(&sq[0], 1);
  init_square(&sq[1], 2);
  init_square(&sq[2], 3);

  msg("gfour: starting PPU...\r\n");
  if (ppuc_run((unsigned short)ppu_addr) < 0) {
    msg("gfour: ppuc_run failed\r\n");
    return 1;
  }

  // Tell the PPU where the screen is; it can't build its tag list
  // before it knows. This first ppuc_send() also blocks until
  // gfourppu.c's own ppus_recv_init() has taken over from the resident
  // monitor (the handshake ppu_client.h describes) -- which is exactly
  // why it has to come *before* ppuc_recv_init() below: that same
  // handshake byte arrives on the channel ppuc_recv_init() would
  // otherwise start reading, and would be taken for a scancode.
  ppuc_send(&screen_base, sizeof screen_base);

  // Arms kbd_recv() for every scancode gfourppu.c's own keyboard ISR
  // forwards from here on (see that file's header comment).
  ppuc_recv_init(kbd_recv);

  // No console output from here until gfourppu.c has handed the
  // console back (ppu_bye below): the PPU-resident monitor that
  // services RT-11's console requests isn't running while gfourppu.c
  // is -- every write() attempted in between has been observed to
  // block, possibly forever (see the file header comment).

  clear_screen();

  for (i = 0; i < NUM_SQUARES; i++) {
    fill_rect_px(sq[i].x, sq[i].row, sq[i].color);
  }

  vsync_init();
  for (frame = 0; frame < NUM_FRAMES; frame++) {
    wait_vsync();

    if (any_key_pressed)
      break; /* any key -- stop animating and exit */

    for (i = 0; i < NUM_SQUARES; i++) {
      fill_rect_px(sq[i].x, sq[i].row, 0); /* erase */
      step_square(&sq[i]);
      fill_rect_px(sq[i].x, sq[i].row, sq[i].color);
    }
  }

  // gfourppu.c saw the same key press and is handing the console back
  // on its own (see its ppu_main()); wait for its bye before touching
  // RT-11 again -- an .EXIT that raced ahead of it would have its
  // first request to the PPU swallowed by gfourppu.c's still-armed
  // channel-2 receiver, and RMON would wait for the answer forever
  // (seen exactly so in the Digger port). On the NUM_FRAMES safety
  // path no key was pressed, so gfourppu.c is still running: nothing
  // will ever say bye, and this hangs -- the same "no orderly way out
  // without a keypress" this example has always had, just moved here.
  while (!ppu_bye) {
  }
  vsync_shutdown();
  ppuc_recv_shutdown();

  return 0;
}
