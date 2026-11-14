# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only reader of unencrypted Switch 2 NUP containers: header, packages, and per-section file counts and sizes.

The layout follows tools/raiders/extract-exefs.mjs (which does the actual extraction, with per-file SHA-256 checks). This module
only reads the header and the table of contents, so inspecting a 20 GB package takes well under a second and never writes anything.
"""
from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass, field

KIND_NAMES = {0: 'base', 1: 'update'}
NO_DIR = 0xFFFFFFFF


class NupError(Exception):
    pass


@dataclass
class Section:
    files: int = 0
    bytes: int = 0
    names: list[str] = field(default_factory=list)   # top-level file names (the ExeFS module list)


@dataclass
class Package:
    index: int
    title_id: int
    version: int
    kind: int
    exefs_dir: int
    romfs_dir: int
    exefs: Section | None = None
    romfs: Section | None = None

    @property
    def kind_name(self) -> str:
        return KIND_NAMES.get(self.kind, f'kind{self.kind}')

    @property
    def title_id_hex(self) -> str:
        return f'{self.title_id:016X}'


@dataclass
class NupInfo:
    path: str
    size: int
    packages: list[Package]

    def pick(self, kind: str = 'update') -> Package:
        """The newest package of a kind ('update' falls back to 'base' when the NUP has no update), as the extractor picks it."""
        want = 0 if kind == 'base' else 1
        cands = [p for p in self.packages if p.kind == want] or ([p for p in self.packages if p.kind == 0] if want == 1 else [])
        if not cands:
            raise NupError(f'the NUP has no {kind} package')
        return max(cands, key=lambda p: p.version)


def _u32(b: bytes, o: int) -> int:
    return struct.unpack_from('<I', b, o)[0]


def _u64(b: bytes, o: int) -> int:
    return struct.unpack_from('<Q', b, o)[0]


def read(path: str, verify_toc: bool = True) -> NupInfo:
    with open(path, 'rb') as f:
        f.seek(0, 2)
        size = f.tell()
        if size < 0x80:
            raise NupError('file is too small to be a NUP container')
        f.seek(0)
        header = f.read(0x80)
        if header[:4] != b'NUP\x01':
            raise NupError('not a NUP v1 container (magic missing)')
        header_size = struct.unpack_from('<H', header, 4)[0]
        toc_offset, toc_size = _u64(header, 8), _u64(header, 16)
        blob_offset, declared = _u64(header, 0x38), _u64(header, 0x40)
        package_count = _u32(header, 0x48)
        if header_size < 0x80 or toc_offset != header_size:
            raise NupError('NUP header fields are invalid')
        if declared != size:
            raise NupError(f'NUP declares {declared} bytes but the file has {size} (incomplete download?)')
        if not (0x30 <= toc_size <= 1 << 30) or toc_offset + toc_size > size or blob_offset < toc_offset + toc_size:
            raise NupError('NUP table of contents bounds are invalid')
        if not 1 <= package_count <= 10000:
            raise NupError('NUP package count is out of range')
        f.seek(toc_offset)
        toc = f.read(toc_size)
    if len(toc) != toc_size:
        raise NupError('short read of the NUP table of contents')
    if verify_toc and hashlib.sha256(toc).digest() != header[0x18:0x38]:
        raise NupError('NUP table of contents SHA-256 check failed (damaged file)')
    if _u32(toc, 0) != 0x01434F54 or _u32(toc, 4) != package_count:
        raise NupError('NUP table of contents magic is invalid')

    dir_count, file_count = _u64(toc, 8), _u64(toc, 16)
    pool_offset, pool_size = _u64(toc, 0x18), _u64(toc, 0x20)
    if dir_count > 5_000_000 or file_count > 5_000_000:
        raise NupError('NUP directory/file count out of range')
    packages_offset = 0x30
    dirs_offset = packages_offset + package_count * 0x40
    files_offset = dirs_offset + dir_count * 0x18
    children_offset = files_offset + file_count * 0x50
    if children_offset > len(toc) or pool_offset < children_offset or pool_offset + pool_size > len(toc):
        raise NupError('NUP table bounds are invalid')
    children_count = (pool_offset - children_offset) // 4

    def name_at(off: int, ln: int) -> str:
        if off + ln > pool_size:
            raise NupError('NUP name outside the string pool')
        return toc[pool_offset + off:pool_offset + off + ln].decode('utf-8')

    def section(root: int) -> Section | None:
        if root == NO_DIR or root >= dir_count:
            return None
        s = Section()
        seen: set[int] = set()
        pending = [(root, True)]
        while pending:
            d, top = pending.pop()
            if d in seen:
                raise NupError('NUP directory graph has a cycle')
            seen.add(d)
            o = dirs_offset + d * 0x18
            first, count = _u32(toc, o + 8), _u32(toc, o + 12)
            if first + count > children_count:
                raise NupError('NUP directory child range is invalid')
            for i in range(count):
                child = _u32(toc, children_offset + (first + i) * 4)
                if child < dir_count:
                    pending.append((child, False))
                    continue
                fi = child - dir_count
                if fi >= file_count:
                    raise NupError('NUP child outside the file table')
                fo = files_offset + fi * 0x50
                s.files += 1
                s.bytes += _u64(toc, fo + 8)
                if top:
                    s.names.append(name_at(_u32(toc, fo), _u32(toc, fo + 4)))
        s.names.sort()
        return s

    packages = []
    for i in range(package_count):
        o = packages_offset + i * 0x40
        p = Package(index=i, title_id=_u64(toc, o), exefs_dir=_u32(toc, o + 0x0C), romfs_dir=_u32(toc, o + 0x10),
                    version=_u32(toc, o + 0x14), kind=toc[o + 0x18])
        p.exefs, p.romfs = section(p.exefs_dir), section(p.romfs_dir)
        packages.append(p)
    return NupInfo(path=path, size=size, packages=packages)


def describe(info: NupInfo) -> dict:
    """A JSON-friendly summary (the launcher's library scan and `recomp_pipeline.py inspect`)."""
    out = {'path': info.path, 'size': info.size, 'packages': []}
    for p in info.packages:
        out['packages'].append({
            'index': p.index, 'kind': p.kind_name, 'title_id': p.title_id_hex, 'version': p.version,
            'exefs': None if not p.exefs else {'files': p.exefs.files, 'bytes': p.exefs.bytes, 'names': p.exefs.names},
            'romfs': None if not p.romfs else {'files': p.romfs.files, 'bytes': p.romfs.bytes},
        })
    try:
        sel = info.pick('update')
        # The version is the package's internal number (Raiders 1.1.0 = 65536); the display version lives in the control data, which
        # NUPs do not carry, so the launcher shows the name's version or the profile's.
        out['selected'] = {'index': sel.index, 'kind': sel.kind_name, 'title_id': sel.title_id_hex, 'version': sel.version}
    except NupError as e:
        out['selected'] = None
        out['error'] = str(e)
    return out
