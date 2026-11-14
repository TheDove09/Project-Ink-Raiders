// SPDX-License-Identifier: GPL-3.0-or-later
//! The library: .nup files found in the scan folders (and added by hand) merged with the games already built in the output folder,
//! one entry per title id.

use crate::nup;
use crate::settings::Settings;
use serde::Serialize;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

#[derive(Serialize, Clone, Debug)]
pub struct BuiltGame {
    pub game_dir: String,
    pub exe: String,
    pub built: String,
    pub version: String,
}

#[derive(Serialize, Clone, Debug)]
pub struct GameEntry {
    pub title_id: String,
    pub title: String,
    pub version: Option<String>,
    pub nup_path: Option<String>,
    pub nup_size: u64,
    /// The .nup is on another drive than the work folder (staged to the SSD first when staging is "auto").
    pub nup_external: bool,
    pub nup_error: Option<String>,
    /// "sra" for a built-in profile, "x<title id>" for a generated one.
    pub profile: String,
    /// "supported" (built-in, tested profile) or "experimental" (generated).
    pub support: String,
    pub built: Option<BuiltGame>,
    pub cover: Option<String>,
}

/// {title id: profile id} of the hand-kept profiles in recomp/games (generated x* ones are not "supported").
pub fn builtin_profiles(repo: &Path) -> BTreeMap<String, String> {
    let mut out = BTreeMap::new();
    let Ok(rd) = std::fs::read_dir(repo.join("recomp").join("games")) else { return out };
    for e in rd.flatten() {
        let id = e.file_name().to_string_lossy().into_owned();
        if id.starts_with('x') || !repo.join("runtime").join("games").join(&id).is_dir() {
            continue;
        }
        if let Ok(t) = std::fs::read_to_string(e.path().join("game.toml")) {
            for line in t.lines() {
                let l = line.trim();
                if let Some(v) = l.strip_prefix("title_id") {
                    let v = v.trim_start_matches([' ', '=']).trim().trim_matches('"').to_uppercase();
                    out.insert(v, id.clone());
                    break;
                }
            }
        }
    }
    out
}

fn find_nups(dir: &Path, depth: u32, out: &mut Vec<PathBuf>) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    for e in rd.flatten() {
        let p = e.path();
        let Ok(ft) = e.file_type() else { continue };
        if ft.is_dir() && depth > 0 {
            let name = e.file_name().to_string_lossy().to_lowercase();
            // never descend into our own output (romfs folders hold 200,000 files) or system folders
            if name.starts_with('$') || name == "romfs" || name == "exefs" || name == "node_modules" || name == "windows" {
                continue;
            }
            find_nups(&p, depth - 1, out);
        } else if ft.is_file() && p.extension().map(|x| x.eq_ignore_ascii_case("nup")).unwrap_or(false) {
            out.push(p);
        }
    }
}

fn drive_of(p: &Path) -> String {
    p.to_string_lossy().chars().take(2).collect::<String>().to_lowercase()
}

pub fn scan(s: &Settings) -> Vec<GameEntry> {
    let repo = PathBuf::from(&s.repo_dir);
    let builtin = builtin_profiles(&repo);
    let mut by_id: BTreeMap<String, GameEntry> = BTreeMap::new();
    let entry = |tid: &str, title: String| GameEntry {
        title_id: tid.to_string(),
        title,
        version: None,
        nup_path: None,
        nup_size: 0,
        nup_external: false,
        nup_error: None,
        profile: builtin.get(tid).cloned().unwrap_or_else(|| format!("x{}", tid.to_lowercase())),
        support: if builtin.contains_key(tid) { "supported".into() } else { "experimental".into() },
        built: None,
        cover: s.covers.get(tid).cloned(),
    };

    // built games: <out>/<name>/game.json, written by the pipeline's package stage
    if let Ok(rd) = std::fs::read_dir(&s.out_dir) {
        for e in rd.flatten() {
            let gj = e.path().join("game.json");
            let Ok(text) = std::fs::read_to_string(&gj) else { continue };
            let Ok(v) = serde_json::from_str::<serde_json::Value>(&text) else { continue };
            let tid = v["title_id"].as_str().unwrap_or("").to_uppercase();
            let exe = e.path().join(v["exe"].as_str().unwrap_or(""));
            if tid.is_empty() || !exe.is_file() {
                continue;
            }
            let title = v["title"].as_str().unwrap_or(&tid).to_string();
            let g = by_id.entry(tid.clone()).or_insert_with(|| entry(&tid, title));
            g.built = Some(BuiltGame {
                game_dir: e.path().to_string_lossy().into_owned(),
                exe: exe.to_string_lossy().into_owned(),
                built: v["built"].as_str().unwrap_or("").to_string(),
                version: v["version"].as_str().unwrap_or("").to_string(),
            });
            g.version = Some(v["version"].as_str().unwrap_or("").to_string());
        }
    }

    // .nup files: scan folders (4 levels deep) and the ones added by hand
    let mut nups = Vec::new();
    for d in &s.scan_dirs {
        find_nups(Path::new(d), 4, &mut nups);
    }
    nups.extend(s.extra_nups.iter().map(PathBuf::from).filter(|p| p.is_file()));
    nups.sort();
    nups.dedup();
    let work_drive = drive_of(Path::new(&s.work_dir));
    for p in nups {
        let (title, version) = nup::title_from_filename(&p);
        match nup::read(&p) {
            Ok(info) => {
                let Some(sel) = info.selected() else { continue };
                let tid = format!("{:016X}", sel.title_id);
                let g = by_id.entry(tid.clone()).or_insert_with(|| entry(&tid, title.clone()));
                if g.nup_path.is_none() {
                    g.nup_path = Some(p.to_string_lossy().into_owned());
                    g.nup_size = info.size;
                    g.nup_external = drive_of(&p) != work_drive;
                    if g.built.is_none() {
                        g.title = title;
                        g.version = version;
                    }
                }
            }
            Err(err) => {
                // shown as a card with the problem (an incomplete download, a different format)
                let key = format!("bad:{}", p.to_string_lossy());
                let mut g = entry("", title);
                g.title_id = key.clone();
                g.nup_path = Some(p.to_string_lossy().into_owned());
                g.nup_error = Some(err);
                g.support = "unreadable".into();
                by_id.insert(key, g);
            }
        }
    }
    let mut v: Vec<GameEntry> = by_id.into_values().collect();
    v.sort_by(|a, b| a.title.to_lowercase().cmp(&b.title.to_lowercase()));
    v
}
