// SPDX-License-Identifier: GPL-3.0-or-later
//! Fast read-only NUP identification for the library scan: the header and the package table only (title id, version, kind,
//! update/base). The full reader and the extractor live in tools/pipeline (nup.py) and tools/raiders/extract-exefs.mjs.

use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

#[derive(Debug, Clone)]
pub struct NupPackage {
    pub title_id: u64,
    pub version: u32,
    pub kind: u8, // 0 base, 1 update
}

#[derive(Debug, Clone)]
pub struct NupInfo {
    pub size: u64,
    pub packages: Vec<NupPackage>,
}

impl NupInfo {
    /// The package the pipeline builds: the newest update, else the newest base.
    pub fn selected(&self) -> Option<&NupPackage> {
        let pick = |k: u8| self.packages.iter().filter(|p| p.kind == k).max_by_key(|p| p.version);
        pick(1).or_else(|| pick(0))
    }
}

fn u16le(b: &[u8], o: usize) -> u16 {
    u16::from_le_bytes([b[o], b[o + 1]])
}
fn u32le(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes(b[o..o + 4].try_into().unwrap())
}
fn u64le(b: &[u8], o: usize) -> u64 {
    u64::from_le_bytes(b[o..o + 8].try_into().unwrap())
}

pub fn read(path: &Path) -> Result<NupInfo, String> {
    let mut f = File::open(path).map_err(|e| e.to_string())?;
    let size = f.metadata().map_err(|e| e.to_string())?.len();
    if size < 0x80 {
        return Err("too small to be a NUP".into());
    }
    let mut h = [0u8; 0x80];
    f.read_exact(&mut h).map_err(|e| e.to_string())?;
    if &h[..4] != b"NUP\x01" {
        return Err("not a NUP v1 container".into());
    }
    let header_size = u16le(&h, 4) as u64;
    let toc_offset = u64le(&h, 8);
    let toc_size = u64le(&h, 16);
    let declared = u64le(&h, 0x40);
    let count = u32le(&h, 0x48) as u64;
    if header_size < 0x80 || toc_offset != header_size || !(1..=10000).contains(&count) {
        return Err("NUP header fields are invalid".into());
    }
    if declared != size {
        return Err(format!("incomplete file ({} of {} bytes)", size, declared));
    }
    let table = 0x30 + count * 0x40;
    if table > toc_size {
        return Err("NUP package table is out of bounds".into());
    }
    let mut toc = vec![0u8; table as usize];
    f.seek(SeekFrom::Start(toc_offset)).map_err(|e| e.to_string())?;
    f.read_exact(&mut toc).map_err(|e| e.to_string())?;
    if u32le(&toc, 0) != 0x0143_4F54 || u32le(&toc, 4) as u64 != count {
        return Err("NUP table of contents is invalid".into());
    }
    let packages = (0..count as usize)
        .map(|i| {
            let o = 0x30 + i * 0x40;
            NupPackage { title_id: u64le(&toc, o), version: u32le(&toc, o + 0x14), kind: toc[o + 0x18] }
        })
        .collect();
    Ok(NupInfo { size, packages })
}

/// ("Mario Kart World", "1.8.0") from "Mario Kart World + Update 1.8.0.nup" (NUPs carry no name or display version).
pub fn title_from_filename(path: &Path) -> (String, Option<String>) {
    let stem = path.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
    let lower = stem.to_lowercase();
    if let Some(i) = lower.find("+ update") {
        let title = stem[..i].trim().to_string();
        let ver: String = stem[i + 8..].trim().chars().take_while(|c| c.is_ascii_digit() || *c == '.').collect();
        return (title, if ver.is_empty() { None } else { Some(ver) });
    }
    (stem.trim().to_string(), None)
}
