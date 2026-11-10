#!/usr/bin/env node
// Extracts one package's ExeFS or RomFS from an unencrypted NUP container.
// Uses only Node's built-in modules; the game payload stays in the selected local output folder.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import zlib from 'node:zlib';

function fail(message) { throw new Error(message); }
function u16(b, o) { return b.readUInt16LE(o); }
function u32(b, o) { return b.readUInt32LE(o); }
function u64(b, o) {
  const n = b.readBigUInt64LE(o);
  if (n > BigInt(Number.MAX_SAFE_INTEGER)) fail(`NUP offset ${n} exceeds the safe integer range.`);
  return Number(n);
}
function checkedRange(offset, length, total, label) {
  if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(length) || offset < 0 || length < 0 || offset + length > total) {
    fail(`Invalid ${label} bounds.`);
  }
}

function readFully(fd, buffer, bufferOffset, length, fileOffset, label, chunkSize = 1024 * 1024) {
  let done = 0;
  while (done < length) {
    const request = Math.min(chunkSize, length - done);
    let got;
    try {
      got = fs.readSync(fd, buffer, bufferOffset + done, request, fileOffset + done);
    } catch (e) {
      fail(`${label} read failed at NUP offset ${fileOffset + done}: ${e.message}`);
    }
    if (!got) fail(`${label} short read at NUP offset ${fileOffset + done}: received ${done} of ${length} bytes.`);
    done += got;
    if (label === 'NUP TOC' && done % (8 * 1024 * 1024) < got) console.log(`NUP TOC read: ${done.toLocaleString()} / ${length.toLocaleString()} bytes`);
  }
}

function readNup(file) {
  const fd = fs.openSync(file, 'r');
  try {
    const stat = fs.fstatSync(fd);
    if (stat.size < 0x80) fail('File is too small to be a NUP container.');
    const header = Buffer.alloc(0x80);
    readFully(fd, header, 0, header.length, 0, 'NUP header', header.length);
    if (!header.subarray(0, 4).equals(Buffer.from([0x4e, 0x55, 0x50, 1]))) fail('NUP v1 magic was not found.');
    const headerSize = u16(header, 4);
    const headerVersion = u16(header, 6);
    const tocOffset = u64(header, 8);
    const tocSize = u64(header, 16);
    const blobOffset = u64(header, 0x38);
    const declaredSize = u64(header, 0x40);
    const packageCount = u32(header, 0x48);
    if (headerSize < 0x80 || headerVersion < 1 || tocOffset !== headerSize) fail('NUP header fields are invalid.');
    if (declaredSize !== stat.size) fail('NUP declared file size differs from its actual size.');
    checkedRange(tocOffset, tocSize, stat.size, 'TOC');
    if (tocSize < 0x30 || tocSize > 1024 * 1024 * 1024 || blobOffset < tocOffset + tocSize || blobOffset % 16 !== 0) fail('NUP TOC/blob bounds are invalid.');
    if (packageCount < 1 || packageCount > 10000) fail('NUP package count is outside supported bounds.');

    const toc = Buffer.alloc(tocSize);
    console.log(`Reading NUP TOC (${toc.length.toLocaleString()} bytes).`);
    readFully(fd, toc, 0, toc.length, tocOffset, 'NUP TOC', 1024 * 1024);
    const digest = crypto.createHash('sha256').update(toc).digest();
    if (!crypto.timingSafeEqual(digest, header.subarray(0x18, 0x38))) fail('NUP TOC SHA-256 check failed.');
    if (u32(toc, 0) !== 0x01434f54 || u32(toc, 4) !== packageCount) fail('NUP TOC magic or package count is invalid.');

    const dirCount = u64(toc, 8);
    const fileCount = u64(toc, 16);
    const poolOffset = u64(toc, 0x18);
    const poolSize = u64(toc, 0x20);
    if (dirCount > 5_000_000 || fileCount > 5_000_000) fail('NUP directory/file count exceeds supported bounds.');
    const packagesOffset = 0x30;
    const dirsOffset = packagesOffset + packageCount * 0x40;
    const filesOffset = dirsOffset + dirCount * 0x18;
    const childrenOffset = filesOffset + fileCount * 0x50;
    checkedRange(0, childrenOffset, toc.length, 'TOC tables');
    if (poolOffset < childrenOffset || poolOffset + poolSize > toc.length || (poolOffset - childrenOffset) % 4 !== 0) fail('NUP string pool or child table bounds are invalid.');
    const childrenCount = (poolOffset - childrenOffset) / 4;
    const dirs = [];
    for (let i = 0; i < dirCount; i++) {
      const o = dirsOffset + i * 0x18;
      const first = u32(toc, o + 8), count = u32(toc, o + 12);
      if (first + count > childrenCount) fail(`NUP directory ${i} has an invalid child range.`);
      dirs.push({ nameOffset: u32(toc, o), nameLength: u32(toc, o + 4), first, count });
    }
    const packages = [];
    for (let i = 0; i < packageCount; i++) {
      const o = packagesOffset + i * 0x40;
      packages.push({
        titleId: toc.readBigUInt64LE(o), exefsDir: u32(toc, o + 0x0c), romfsDir: u32(toc, o + 0x10),
        version: u32(toc, o + 0x14), kind: toc[o + 0x18],
      });
    }
    return { fd, stat, toc, tocOffset, blobOffset, packageCount, dirCount, fileCount, poolOffset, poolSize, dirsOffset, filesOffset, childrenOffset, childrenCount, dirs, packages };
  } catch (e) {
    fs.closeSync(fd);
    throw e;
  }
}

function stringAt(n, off, len) {
  if (off + len > n.poolSize) fail('NUP name is outside the string pool.');
  const value = n.toc.toString('utf8', n.poolOffset + off, n.poolOffset + off + len);
  if (value.includes('\0')) fail('NUP path contains a NUL byte.');
  return value;
}

function hashFileSync(file, expectedHash) {
  const fd = fs.openSync(file, 'r');
  try {
    const hash = crypto.createHash('sha256');
    const chunk = Buffer.allocUnsafe(1024 * 1024);
    let offset = 0;
    for (;;) {
      const got = fs.readSync(fd, chunk, 0, chunk.length, offset);
      if (!got) break;
      hash.update(chunk.subarray(0, got));
      offset += got;
    }
    return hash.digest().equals(expectedHash);
  } finally {
    fs.closeSync(fd);
  }
}

function extract(n, pkg, destination, section, resume) {
  const rootDir = pkg[`${section}Dir`];
  if (rootDir === 0xffffffff || rootDir >= n.dirCount) fail(`Selected package does not have a valid ${section.toUpperCase()} directory.`);
  const root = path.resolve(destination);
  fs.mkdirSync(root, { recursive: true });
  const seen = new Set();
  const pending = [{ dir: rootDir, parts: [] }];
  let extracted = 0;
  let visitedFiles = 0;
  while (pending.length) {
    const { dir: dirIndex, parts } = pending.pop();
    if (seen.has(dirIndex)) fail('NUP directory graph contains a cycle or repeated directory.');
    seen.add(dirIndex);
    const dir = n.dirs[dirIndex];
    for (let i = 0; i < dir.count; i++) {
      const childId = u32(n.toc, n.childrenOffset + (dir.first + i) * 4);
      if (childId < n.dirCount) {
        const childName = stringAt(n, n.dirs[childId].nameOffset, n.dirs[childId].nameLength);
        if (!childName || childName === '.' || childName === '..' || /[\\/]/.test(childName)) fail('Unsafe directory name in NUP.');
        pending.push({ dir: childId, parts: [...parts, childName] });
        continue;
      }
      const fileIndex = childId - n.dirCount;
      if (fileIndex >= n.fileCount) fail('NUP child references a file outside the file table.');
      const fo = n.filesOffset + fileIndex * 0x50;
      const fileName = stringAt(n, u32(n.toc, fo), u32(n.toc, fo + 4));
      if (!fileName || fileName === '.' || fileName === '..' || /[\\/]/.test(fileName)) fail('Unsafe filename in NUP.');
      const relative = [...parts, fileName].join(path.sep);
      visitedFiles++;
      if (resume && visitedFiles % 100 === 0) console.log(`Resume progress: checked ${visitedFiles} ${section.toUpperCase()} entries; ${extracted} verified or extracted.`);
      const output = path.resolve(root, relative);
      if (output !== root && !output.startsWith(root + path.sep)) fail('NUP path escapes output directory.');
      const size = u64(n.toc, fo + 8);
      const extentOffset = u64(n.toc, fo + 0x10);
      const extentSize = u64(n.toc, fo + 0x18);
      const expectedHash = n.toc.subarray(fo + 0x20, fo + 0x40);
      if (fs.existsSync(output)) {
        if (!resume) fail(`Duplicate ${section.toUpperCase()} path: ${relative}`);
        const existing = fs.statSync(output);
        if (existing.isFile() && existing.size === size && hashFileSync(output, expectedHash)) {
          extracted++;
          continue;
        }
        fs.rmSync(output, { force: true });
      }
      checkedRange(extentOffset, extentSize, n.stat.size, 'NUP file extent');
      if (extentOffset < n.blobOffset) fail('NUP extent is outside the blob region.');
      const stored = Buffer.alloc(extentSize);
      let read = 0;
      while (read < extentSize) {
        const request = Math.min(1024 * 1024, extentSize - read);
        let got;
        try {
          got = fs.readSync(n.fd, stored, read, request, extentOffset + read);
        } catch (e) {
          fail(`Read failed for ${relative} at NUP offset ${extentOffset + read}: ${e.message}`);
        }
        if (!got) fail(`Short read for ${relative} at NUP offset ${extentOffset + read}: received ${read} of ${extentSize} bytes.`);
        read += got;
      }
      let content;
      switch (n.toc[fo + 0x40]) {
        case 0:
          if (extentSize !== size) fail(`Raw extent size mismatch for ${relative}.`);
          content = stored;
          break;
        case 1:
          content = zlib.zstdDecompressSync(stored, { maxOutputLength: size });
          if (content.length !== size) fail(`Zstandard output size mismatch for ${relative}.`);
          break;
        default:
          fail(`Unsupported NUP extent codec ${n.toc[fo + 0x40]} for ${relative}.`);
      }
      const fileHash = crypto.createHash('sha256').update(content).digest();
      if (!crypto.timingSafeEqual(fileHash, expectedHash)) fail(`SHA-256 check failed for ${relative}.`);
      fs.mkdirSync(path.dirname(output), { recursive: true });
      fs.writeFileSync(output, content, { flag: 'wx' });
      extracted++;
      console.log(`${relative}  ${size.toLocaleString()} bytes`);
    }
  }
  console.log(`Extracted ${extracted} verified ${section.toUpperCase()} files to ${root}`);
}

function usage() {
  console.error('Usage: node tools/raiders/extract-exefs.mjs <game.nup> <output-directory> [base|update] [exefs|romfs] [fresh|resume]');
  process.exitCode = 2;
}

const [input, output, wantedKind = 'update', section = 'exefs', mode = 'fresh'] = process.argv.slice(2);
if (!input || !output || !['base', 'update'].includes(wantedKind) || !['exefs', 'romfs'].includes(section) || !['fresh', 'resume'].includes(mode)) usage();
else {
  let n;
  try {
    n = readNup(path.resolve(input));
    const kind = wantedKind === 'base' ? 0 : 1;
    const candidates = n.packages.map((p, i) => ({ ...p, index: i })).filter(p => p.kind === kind);
    if (!candidates.length) fail(`NUP has no ${wantedKind} package.`);
    candidates.sort((a, b) => a.version - b.version);
    const selected = candidates.at(-1);
    console.log(`Selected ${wantedKind} package ${selected.index}, title ID ${selected.titleId.toString(16).toUpperCase().padStart(16, '0')}, version ${selected.version}; extracting ${section.toUpperCase()}.`);
    extract(n, selected, output, section, mode === 'resume');
  } catch (e) {
    console.error(`NUP extraction failed: ${e.message}`);
    process.exitCode = 1;
  } finally {
    if (n?.fd !== undefined) fs.closeSync(n.fd);
  }
}
