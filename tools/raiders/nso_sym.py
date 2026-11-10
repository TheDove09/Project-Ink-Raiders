"""Name addresses in an NSO module from its .dynsym exports (read-only).
  python nso_sym.py <recomp dir> <nso file> <module base hex> <addr hex> [<addr hex> ...]
Prints, per address, the exported function that contains it (or the nearest export below it, marked '~')."""
import bisect, struct, sys
sys.path.insert(0, sys.argv[1])
from nxrecomp.nso import nso_to_elf

d = open(sys.argv[2], 'rb').read()
base = int(sys.argv[3], 16)
elf, meta = nso_to_elf(d)
mem = elf[0x1000:]
syms = []
for i in range(meta['nsym']):
    nm, info, other, shn, val, size = struct.unpack_from('<IBBHQQ', mem, meta['dynsym'] + i * 24)
    if val and (info & 0xf) == 2:
        end = mem.index(b'\0', meta['dynstr'] + nm)
        syms.append((val, size, mem[meta['dynstr'] + nm:end].decode('utf8', 'replace')))
syms.sort()
starts = [s[0] for s in syms]
for a in sys.argv[4:]:
    off = int(a, 16) - base
    k = bisect.bisect_right(starts, off) - 1
    if k < 0:
        print(a, '?'); continue
    v, sz, n = syms[k]
    mark = '' if off < v + max(sz, 1) else '~'
    print(f'{a} {mark}{n}+0x{off - v:x}')
