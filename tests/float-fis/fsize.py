import re, subprocess, sys

def sizes(obj):
    out = subprocess.run(['pdp11-uknc-rt11-objdump','-d',obj],
                         capture_output=True, text=True).stdout
    funcs, cur, last = {}, None, None
    for line in out.splitlines():
        m = re.match(r'^([0-9a-f]+) <([^>]+)>:', line)
        if m:
            if cur is not None:
                funcs[cur] = last - start
            cur, start = m.group(2), int(m.group(1), 16)
            last = start
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+((?:[0-9a-f]{4} )+)', line)
        if m and cur is not None:
            at = int(m.group(1), 16)
            last = at + len(m.group(2).split()) * 2
    if cur is not None:
        funcs[cur] = last - start
    return funcs

for obj in sys.argv[1:]:
    f = sizes(obj)
    print(obj)
    for name in sorted(f):
        print('   %-18s %5d' % (name, f[name]))
