#!/usr/bin/env python3

"""Turn `monitor prof save` output into a .cpuprofile VS Code can open.

The emulator's profiler counts ticks per address and `monitor prof save`
writes the lot -- one line of "octal address, ticks" per address that was
ever executing.  That is the whole truth about where a program's time
went, and nothing reads it.

VS Code does read .cpuprofile files: the Chrome DevTools / V8 profile
format, for which the editor ships a viewer of its own (a bottom-up
table ordered by self time; the flame chart is one extension away, and
the editor offers to install it).  Nothing in the format is specific to
JavaScript -- it is a tree of call frames plus samples -- so our ticks
fit it, and the viewer costs us nothing to maintain.

    monitor prof reset
    monitor prof on
    ... run the program ...
    monitor prof off
    monitor prof save hello.prof

    prof2cpuprofile.py -e hello.elf hello.prof

and open hello.cpuprofile.

What comes out is a flat profile: one frame per source line (or per
function, or per address -- see --by), all of them children of a single
root.  The profiler records the address of the instruction executing on
each tick and knows nothing of call stacks, so there are none here
either; the editor's table view, which orders by self time, is the one
that suits this.

Addresses are named through addr2line, which needs the ELF the program
was linked as -- not the .sav, which has no symbols.  A PPU module is
linked from zero and loaded wherever ppuc_load_code() put it, so its
profile needs that load address subtracted first: --base, the number
ppu.gdb prints ("PPU symbols from gfppu.ppu.elf at 023666").

Numbers are written to the file as microseconds, which is the format's
unit, and by default one tick becomes one microsecond: the times the
viewer shows are then ticks under another name, proportional to each
other and wrong only in their label.  --hz scales them into real time --
8000000 for the central processor, 6250000 for the peripheral one, which
is the 160000 and 125000 ticks a frame the profiler counts in, fifty
frames a second.
"""

import argparse
import json
import os
import subprocess
import sys


def parse_number(text):
    """A number the way this project writes them: octal unless told.

    Addresses here come out of the profiler, out of gdb and out of
    ppu.gdb, all of which print octal, and copying one of those over
    should mean what it looked like.  0x marks hex, and a trailing dot
    marks decimal, as it does in MACRO-11.
    """
    text = text.strip()
    try:
        if text[:2].lower() in ('0x', '0o', '0b'):
            return int(text, 0)
        if text.endswith('.'):
            return int(text[:-1], 10)
        return int(text, 8)
    except ValueError:
        raise argparse.ArgumentTypeError(
            '%s is not a number (octal, or 0x... hex, or 123. decimal)' % text)


def read_profile(path):
    """The emulator's own format: "%06o %u" per line, one line per address."""
    try:
        lines = sys.stdin if path == '-' else open(path)
    except OSError as error:
        sys.exit('cannot read %s: %s' % (path, error.strerror))
    counts = []
    with lines as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            fields = line.split()
            if len(fields) != 2:
                sys.exit('%s:%d: expected "address ticks", found %r'
                         % (path, number, line))
            try:
                counts.append((int(fields[0], 8), int(fields[1], 10)))
            except ValueError:
                sys.exit('%s:%d: expected "address ticks", found %r'
                         % (path, number, line))
    return counts


def symbolize(addresses, elf, base, addr2line):
    """addr2line for every address at once, as {address: (function, file, line)}.

    One call with the addresses on its standard input rather than one
    call per address: there can be thousands of them, and a process each
    would take longer than the run being profiled.
    """
    if elf is None:
        return {}

    stdin = ''.join('0x%x\n' % (address - base) for address in addresses)
    try:
        out = subprocess.run([addr2line, '-f', '-e', elf],
                             input=stdin, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
    except OSError as error:
        sys.exit('cannot run %s: %s' % (addr2line, error))
    if out.returncode != 0:
        sys.exit('%s failed: %s' % (addr2line, out.stderr.strip()))

    # Two lines per address: the function, then "file:line".
    fields = out.stdout.splitlines()
    if len(fields) != 2 * len(addresses):
        sys.exit('%s answered %d lines for %d addresses'
                 % (addr2line, len(fields), len(addresses)))

    where = {}
    elf_dir = os.path.dirname(os.path.abspath(elf))
    for i, address in enumerate(addresses):
        function = fields[2 * i].strip()
        source = fields[2 * i + 1].strip()
        path, _, line = source.rpartition(':')
        # addr2line writes "??" for what it cannot name, and can put a
        # discriminator after the line number.
        line = line.split(' ')[0]
        try:
            line = int(line)
        except ValueError:
            line = 0
        if function == '??':
            function = ''
        if path in ('??', ''):
            path = ''
        elif not os.path.isabs(path):
            # Relative to where the ELF is, which is where the build ran
            # for everything this project builds.
            beside = os.path.join(elf_dir, path)
            path = beside if os.path.exists(beside) else path
        where[address] = (function, path, line)
    return where


def group(counts, where, by):
    """Sum ticks per frame, which is what --by picks.

    Returns [(ticks, functionName, url, line)], heaviest first.
    """
    totals = {}
    for address, ticks in counts:
        function, path, line = where.get(address, ('', '', 0))
        if by == 'address' or not (function or path):
            # One frame per address, either because that is what was
            # asked for or because nothing could name this one.  An
            # address outside the ELF -- the monitor, a driver, the
            # firmware -- has nothing to be grouped by anyway, and
            # throwing all of them together under one name would hide
            # exactly what the profile is being read for.
            key = (address,)
        elif by == 'function':
            key = (function, path)
        else:
            key = (function, path, line)

        if key in totals:
            totals[key][0] += ticks
            totals[key][1] = min(totals[key][1], address)
        else:
            totals[key] = [ticks, address]

    frames = []
    for ticks, lowest in totals.values():
        function, path, line = where.get(lowest, ('', '', 0))
        if by == 'address' or not function:
            # The address itself is then the only name the frame can
            # have, and the one to look it up by hand with.
            name = '0%06o %s' % (lowest, function) if function \
                else '0%06o' % lowest
        else:
            name = function
        frames.append((ticks, name, path, line))

    frames.sort(key=lambda frame: (-frame[0], frame[1]))
    return frames


def build(frames, hz):
    """The .cpuprofile itself.

    The viewer gives sample i the delta that follows it -- timeDeltas[i+1]
    -- and the last sample whatever is left of endTime - startTime (see
    buildModel in vscode-js-profile-core, and Chrome's own reader, which
    reads the format the same way).  So one sample per frame, with the
    deltas shifted up by one and the total as the duration, attributes
    every frame exactly its own ticks.
    """
    def microseconds(ticks):
        return ticks if hz is None else int(round(ticks * 1e6 / hz))

    root = {
        'id': 1,
        'callFrame': {
            'functionName': '(root)',
            'scriptId': '0',
            'url': '',
            'lineNumber': -1,
            'columnNumber': -1,
        },
        'hitCount': 0,
        'children': list(range(2, 2 + len(frames))),
    }

    nodes = [root]
    samples = []
    times = []
    for i, (ticks, name, path, line) in enumerate(frames):
        nodes.append({
            'id': 2 + i,
            'callFrame': {
                'functionName': name,
                'scriptId': '0',
                'url': 'file://' + path if path else '',
                # The format counts lines from zero.
                'lineNumber': line - 1 if line > 0 else -1,
                'columnNumber': 0,
            },
            'hitCount': ticks,
            'children': [],
        })
        samples.append(2 + i)
        times.append(microseconds(ticks))

    return {
        'nodes': nodes,
        'startTime': 0,
        'endTime': sum(times),
        'samples': samples,
        'timeDeltas': [0] + times[:-1],
    }


def main():
    parser = argparse.ArgumentParser(
        description='Turn "monitor prof save" output into a .cpuprofile.')
    parser.add_argument('profile', metavar='FILE',
                        help='what "monitor prof save" wrote; - for stdin')
    parser.add_argument('-e', '--elf',
                        help='the ELF the program was linked as, for names')
    parser.add_argument('-b', '--base', type=parse_number, default=0,
                        help='load address to subtract first, octal unless '
                             '0x or a trailing dot says otherwise -- for a '
                             'PPU module, what ppu.gdb prints')
    parser.add_argument('-o', '--out',
                        help='where to write it (default: FILE.cpuprofile)')
    parser.add_argument('--by', choices=('line', 'function', 'address'),
                        default='line',
                        help='one frame per source line (default), per '
                             'function, or per address')
    parser.add_argument('--addr2line', default='pdp11-uknc-rt11-addr2line',
                        help='the addr2line to name addresses with')
    parser.add_argument('--hz', type=float,
                        help='ticks per second, to write real time instead '
                             'of one microsecond per tick: 8000000 for the '
                             'CPU, 6250000 for the PPU')
    args = parser.parse_args()

    counts = read_profile(args.profile)
    if not counts:
        sys.exit('%s has no counted addresses in it' % args.profile)

    out = args.out
    if out is None:
        if args.profile == '-':
            sys.exit('reading standard input needs -o')
        out = os.path.splitext(args.profile)[0] + '.cpuprofile'

    where = symbolize([address for address, _ in counts],
                      args.elf, args.base, args.addr2line)
    frames = group(counts, where, args.by)
    with open(out, 'w') as f:
        json.dump(build(frames, args.hz), f)

    ticks = sum(count for _, count in counts)
    named = sum(1 for frame in frames if frame[2])
    print('written: %s, %d ticks over %d addresses in %d frames%s'
          % (out, ticks, len(counts), len(frames),
             '' if args.elf is None
             else ', %d of them with a source line' % named))
    if args.hz is None:
        print('times in it are ticks, written as microseconds -- '
              'pass --hz to scale them into real time')


if __name__ == '__main__':
    main()
