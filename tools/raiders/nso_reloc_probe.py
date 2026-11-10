"""Which relocation fills main's GOT slot at main+0x4b648, and what symbol (if any) does it name?"""
import struct, sys
sys.path.insert(0, sys.argv[1])  # recomp dir
from nxrecomp.nso import nso_info

EXEFS = sys.argv[2]
SLOT = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x4c4b648
TARGET = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x4e143c8

def load(name):
    d = open(f'{EXEFS}/{name}', 'rb').read()
    info = nso_info(d)
    segs = info['segments']
    end = max(s['vaddr'] + s['size'] for s in segs)
    mem = bytearray(end)
    for s in segs:
        mem[s['vaddr']:s['vaddr'] + s['size']] = s['data']
    mod = struct.unpack_from('<I', mem, 4)[0]
    dyn_off, bss_s, bss_e = struct.unpack_from('<iii', mem, mod + 4)
    dyn = mod + dyn_off
    tags = {}
    n = 0
    while True:
        t, v = struct.unpack_from('<qQ', mem, dyn + n * 16); n += 1
        if t == 0: break
        tags.setdefault(t, []).append(v)
    g = lambda k: tags.get(k, [0])[0]
    symtab, strtab = g(6), g(5)
    nsym = struct.unpack_from('<I', mem, g(4) + 4)[0] if 4 in tags else (strtab - symtab) // 24
    def cstr(o):
        e = mem.index(0, strtab + o); return mem[strtab + o:e].decode('latin1')
    syms = []
    for i in range(nsym):
        nm, info_, oth, shn, val, sz = struct.unpack_from('<IBBHQQ', mem, symtab + i * 24)
        syms.append(dict(name=cstr(nm), bind=info_ >> 4, type=info_ & 15, vis=oth & 3, shndx=shn, value=val, size=sz))
    relas = []
    for (a, s) in ((g(7), g(8)), (g(23), g(2))):
        for o in range(0, s, 24):
            off, inf, add = struct.unpack_from('<QQq', mem, a + o)
            relas.append((off, inf & 0xffffffff, inf >> 32, add))
    relr = []
    if g(36):
        a, s = g(36), g(35); where = 0
        for o in range(0, s, 8):
            e = struct.unpack_from('<Q', mem, a + o)[0]
            if e & 1 == 0: relr.append(e); where = e + 8
            else:
                for b in range(63):
                    if e >> (b + 1) & 1: relr.append(where + b * 8)
                where += 63 * 8
    return dict(name=name, mem=mem, syms=syms, relas=relas, relr=relr, bss=(bss_s, bss_e), tags=tags)

mods = {n: load(n) for n in ('main', 'subsdk0', 'sdk', 'rtld')}
m = mods['main']
slot = SLOT - 0x100000 * 0 # offsets are module-relative already
slot_off = SLOT & 0xffffffff if SLOT < 0x7100000000 else SLOT - 0x7100000000
tgt_off = TARGET if TARGET < 0x7100000000 else TARGET - 0x7100000000
print('main bss', [hex(x) for x in m['bss']], 'slot', hex(slot_off), 'target', hex(tgt_off))
print('raw image qword at slot:', hex(struct.unpack_from('<Q', m['mem'], slot_off)[0]))
print('in RELR list:', slot_off in set(m['relr']))
TN = {1027: 'RELATIVE', 1025: 'GLOB_DAT', 1026: 'JUMP_SLOT', 257: 'ABS64'}
for off, typ, si, add in m['relas']:
    if off == slot_off:
        s = m['syms'][si] if si else None
        print('RELA', TN.get(typ, typ), 'sym', si, s, 'addend', hex(add))
        if s and s['name']:
            for nm, mm in mods.items():
                for t in mm['syms']:
                    if t['name'] == s['name']:
                        print(f'   {nm}: def={t["shndx"]!=0} bind={t["bind"]} type={t["type"]} vis={t["vis"]} value={hex(t["value"])} size={t["size"]}')
# Symbols in main covering the target address
for i, s in enumerate(m['syms']):
    if s['shndx'] and s['value'] <= tgt_off < s['value'] + max(s['size'], 1):
        print('main symbol covering target:', i, s)
# Other relocations that point at the target object (anything resolving to it)
for off, typ, si, add in m['relas']:
    if typ == 1027 and add == tgt_off:
        print('RELATIVE reloc to target at slot', hex(off))
    if si and m['syms'][si]['shndx'] and m['syms'][si]['value'] == tgt_off:
        print('symbolic reloc to target at slot', hex(off), TN.get(typ, typ))
print('reloc counts:', {TN.get(k, k): sum(1 for r in m['relas'] if r[1] == k) for k in set(r[1] for r in m['relas'])}, 'relr', len(m['relr']))
