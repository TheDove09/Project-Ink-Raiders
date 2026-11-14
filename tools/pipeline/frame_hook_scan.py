# SPDX-License-Identifier: GPL-3.0-or-later
"""Finds a game's per-frame method automatically (the play-mode frame hook, `entryhook` in <id>.sites / GamePlay::frame_update).

Many Nintendo titles run on sead, whose GameFramework run loop is `for (;;) this->vtable[0xE0](this)`: the method in slot 0xE0 runs
once per main-loop iteration on the main thread, which is where the runtime pumps window events and samples input. Raiders' was found
by hand (0x71031b8c60). This scan does the same statically:
  1. find run loops: `ldr xA, [xN]; ldr xB, [xA, #0xE0]; (mov x0, xN;) blr xB; b <back>` in main's .text;
  2. find the vtables that hold the loop's function (a RELATIVE relocation pointing at its entry);
  3. read slot 0xE0 of those vtables (Itanium layout: offset-to-top, typeinfo, then the slots).
Only sead titles have this loop; anything else reports no candidate and the profile is generated without a frame hook.
"""
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'recomp'))
from nxrecomp.image import Image  # noqa: E402

MAIN_BASE = 0x7100000000
SLOT = 0xE0


def _ldr64_uimm(w):
    """(rt, rn, byte offset) of `ldr Xt, [Xn, #imm]` (unsigned offset), else None."""
    if w & 0xFFC00000 != 0xF9400000:
        return None
    return w & 31, (w >> 5) & 31, ((w >> 10) & 0xFFF) * 8


def _blr(w):
    return (w >> 5) & 31 if w & 0xFFFFFC1F == 0xD63F0000 else None


def _b_target(w, pc):
    if w & 0xFC000000 != 0x14000000:
        return None
    imm = w & 0x3FFFFFF
    if imm & 0x2000000:
        imm -= 0x4000000
    return pc + imm * 4


def find_run_loops(img: Image):
    """Addresses of the `blr` of every vtable[0xE0] call that sits in a tight backward loop."""
    words = img.words()
    t0 = img.base + img.text_start
    out = []
    for i, w in enumerate(words):
        r = _ldr64_uimm(w)
        if not r or r[2] != SLOT:
            continue
        rt, ra, _ = r
        # the vtable load ldr xA, [xN] within the 3 words before
        obj = None
        for j in range(max(0, i - 3), i):
            q = _ldr64_uimm(words[j])
            if q and q[0] == ra and q[2] == 0:
                obj = q[1]
        if obj is None:
            continue
        # blr xT within 3 words after, then a backward branch to at or before the vtable load within 2 words
        for k in range(i + 1, min(len(words), i + 4)):
            if _blr(words[k]) == rt:
                for m in range(k + 1, min(len(words), k + 3)):
                    tgt = _b_target(words[m], t0 + m * 4)
                    if tgt is not None and t0 + (i - 4) * 4 <= tgt <= t0 + i * 4:
                        out.append(t0 + k * 4)
                break
    return out


def scan(main_path: str, base: int = MAIN_BASE) -> dict:
    with open(main_path, 'rb') as f:
        img = Image.from_nso(f.read(), base, 'main')
    text_lo, text_hi = img.base + img.text_start, img.base + img.text_start + img.text_size
    rel = {}                                    # offset -> absolute target (RELATIVE relocations)
    for o, info, a in img.relocs():
        if (info & 0xFFFFFFFF) == 0x403:
            rel[o] = img.base + a
    by_target: dict[int, list[int]] = {}
    for o, t in rel.items():
        if text_lo <= t < text_hi:
            by_target.setdefault(t, []).append(o)
    fn_starts = sorted(by_target)

    import bisect
    loops = find_run_loops(img)
    candidates = []
    for site in loops:
        i = bisect.bisect_right(fn_starts, site) - 1
        if i < 0 or site - fn_starts[i] > 0x100:
            continue
        fn = fn_starts[i]
        for o in by_target[fn]:
            # walk back over function pointers to the vtable's first slot (the word before it is the typeinfo pointer, a data address)
            v = o
            while v - 8 in rel and text_lo <= rel[v - 8] < text_hi:
                v -= 8
            slot_off = v + SLOT
            tgt = rel.get(slot_off)
            if tgt is None or not (text_lo <= tgt < text_hi):
                continue
            slots = []
            k = 0
            while v + 8 * k in rel and text_lo <= rel[v + 8 * k] < text_hi:
                slots.append(rel[v + 8 * k])
                k += 1
            candidates.append({'loop_site': site, 'loop_function': fn, 'vtable': img.base + v, 'frame_method': tgt, 'slots': slots})
    # Several framework classes share the run loop; the base classes' slot 0xE0 is one implementation found in many vtables, the class
    # the game actually runs overrides it (Raiders: 0x71031b8c60 in one vtable, the base 0x7103df0270 in three). Prefer the method
    # referenced by the fewest vtables; the others stay as fallbacks in the profile.
    refs: dict[int, int] = {}
    for t in rel.values():
        refs[t] = refs.get(t, 0) + 1
    methods = sorted({c['frame_method'] for c in candidates}, key=lambda m: (refs.get(m, 0), m))
    best, confidence = None, 'none'
    if len(methods) == 1:
        best, confidence = methods[0], 'single'
    elif len(methods) > 1 and refs.get(methods[0], 0) < refs.get(methods[1], 0):
        best, confidence = methods[0], 'override'
    elif len(methods) > 1:
        # Tie (MKW strips RTTI and each class's method sits in one vtable): the class the game runs implements more of its own
        # methods, so its vtable has the most slots no other vtable shares (Raiders: 12 against 4-5 for the base classes).
        for c in candidates:
            c['unique_slots'] = sum(1 for s in c['slots'] if refs.get(s, 0) == 1)
        ranked = sorted(candidates, key=lambda c: -c['unique_slots'])
        if len(ranked) > 1 and ranked[0]['unique_slots'] > ranked[1]['unique_slots']:
            best, confidence = ranked[0]['frame_method'], 'likely'
            methods = [best] + [m for m in methods if m != best]
    for c in candidates:
        c.pop('slots', None)
    return {'loops': loops, 'candidates': candidates, 'methods': methods, 'frame_method': best, 'confidence': confidence}


if __name__ == '__main__':
    r = scan(sys.argv[1])
    print('run loops:', ', '.join(hex(x) for x in r['loops']) or 'none')
    for c in r['candidates']:
        print('  loop fn %x  vtable %x  slot 0xE0 -> %x' % (c['loop_function'], c['vtable'], c['frame_method']))
    print('frame method:', hex(r['frame_method']) if r['frame_method'] else 'ambiguous or none: ' + ', '.join(hex(m) for m in r['methods']))
