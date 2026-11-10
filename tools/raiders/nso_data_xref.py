"""Static xrefs to a main-module data address: adrp+add/ldr/str/stp forms, plus via a GOT slot that holds it.
Linear-sweep register tracking (reset on branches); good enough to locate candidate writers."""
import struct, sys
sys.path.insert(0, sys.argv[1])
from nxrecomp.nso import nso_info

d = open(sys.argv[2], 'rb').read()
TGT = int(sys.argv[3], 16)        # module offset of the object
GOT = int(sys.argv[4], 16) if len(sys.argv) > 4 else None   # module offset of a slot holding &object
BASE = 0x7100000000
segs = nso_info(d)['segments']
text = segs[0]['data']; tv = segs[0]['vaddr']

def sx(v, b): return v - (1 << b) if v >> (b - 1) & 1 else v

hits = []
regs = {}      # reg -> ('addr', value) known constant; ('obj', 0) means reg == &object
for i in range(0, len(text) - 3, 4):
    pc = tv + i
    w = struct.unpack_from('<I', text, i)[0]
    if (w & 0x9F000000) == 0x90000000:              # ADRP
        rd = w & 31
        imm = ((w >> 29) & 3) | (((w >> 5) & 0x7FFFF) << 2)
        regs[rd] = ('addr', (pc & ~0xFFF) + (sx(imm, 21) << 12))
        continue
    if (w & 0xFF800000) == 0x91000000:              # ADD Xd, Xn, #imm (sh=0)
        rd, rn, imm = w & 31, (w >> 5) & 31, (w >> 10) & 0xFFF
        if rn in regs and regs[rn][0] == 'addr':
            v = regs[rn][1] + imm
            regs[rd] = ('obj', 0) if v == TGT else ('addr', v)
            if v == TGT: hits.append((pc, 'add -> &obj', w))
        else: regs.pop(rd, None)
        continue
    if (w & 0xFFC00000) == 0xF9400000:              # LDR Xt, [Xn, #imm*8]
        rt, rn, imm = w & 31, (w >> 5) & 31, ((w >> 10) & 0xFFF) * 8
        if rn in regs and regs[rn][0] == 'addr':
            a = regs[rn][1] + imm
            if a == TGT: hits.append((pc, 'ldr  obj (read pointer)', w))
            if GOT is not None and a == GOT:
                hits.append((pc, 'ldr GOT -> &obj', w)); regs[rt] = ('obj', 0); continue
        regs.pop(rt, None)
        continue
    if (w & 0xFFC00000) == 0xF9000000:              # STR Xt, [Xn, #imm*8]
        rt, rn, imm = w & 31, (w >> 5) & 31, ((w >> 10) & 0xFFF) * 8
        if rn in regs:
            k, v = regs[rn]
            if (k == 'addr' and v + imm == TGT) or (k == 'obj' and imm == 0):
                hits.append((pc, f'STR  x{rt} -> obj', w))
        continue
    if (w & 0x7FC00000) == 0x29000000 or (w & 0xFFC00000) == 0xA9000000:   # STP (signed offset)
        rt, rt2, rn = w & 31, (w >> 10) & 31, (w >> 5) & 31
        imm = sx((w >> 15) & 0x7F, 7) * (8 if w >> 31 else 4)
        if rn in regs:
            k, v = regs[rn]
            base = v if k == 'addr' else (TGT if k == 'obj' else None)
            if base is not None and base + imm <= TGT < base + imm + 16:
                hits.append((pc, f'STP x{rt},x{rt2} -> obj', w))
        continue
    if (w & 0x7C000000) == 0x14000000 or (w & 0xFE000000) == 0xD6000000 or (w & 0xFF000000) == 0x54000000:
        if (w & 0xFC000000) != 0x94000000:   # not BL (calls keep callee-saved; still reset caller-saved)
            regs.clear()
        else:
            for r in list(regs):
                if r < 19: regs.pop(r)
        continue
    # any other instruction: conservatively drop rd for data-processing writes to w&31
    if (w >> 26) & 0b111 in (0b100, 0b101) or (w & 0x0A000000) == 0x0A000000:
        regs.pop(w & 31, None)

kinds = {}
for pc, k, w in hits: kinds.setdefault(k.split()[0] + ' ' + k.split()[1], []).append(pc)
for pc, k, w in hits:
    if 'STR' in k or 'STP' in k or 'add' in k or 'GOT' in k:
        print(hex(BASE + pc), k)
print({k: len(v) for k, v in kinds.items()})
