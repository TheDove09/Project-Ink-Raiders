// SPDX-License-Identifier: GPL-3.0-or-later
//! Launcher settings: %LOCALAPPDATA%\RecompLauncher\settings.json (scan folders, work / output folders, staging, the pipeline's location)
//! and per-game data (custom covers) in the same folder.

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(default)]
pub struct Settings {
    /// Folders scanned for .nup files (Downloads, an emulation drive, ...).
    pub scan_dirs: Vec<String>,
    /// .nup files added by hand (drag and drop, file picker) outside the scan folders.
    pub extra_nups: Vec<String>,
    /// Fast local scratch folder for builds (an SSD).
    pub work_dir: String,
    /// Where finished games go, one folder each.
    pub out_dir: String,
    /// "auto" (copy a .nup to the work drive first when it is on another drive), "always", "never".
    pub stage_mode: String,
    /// Keep generated C++ and objects after a build (faster rebuilds of the same game, several GB).
    pub keep_temp: bool,
    /// Compile processes.
    pub jobs: u32,
    /// The repository with tools/pipeline (the recompiler, runtime and build files).
    pub repo_dir: String,
    /// Python 3.13 that runs the pipeline.
    pub python: String,
    /// Custom cover images: title id -> image path (copied into the launcher's data folder).
    pub covers: BTreeMap<String, String>,
}

/// The repository this launcher was built from (apps/launcher/src-tauri -> repo root); the workspace is two folders above it.
fn default_repo() -> PathBuf {
    let here = Path::new(env!("CARGO_MANIFEST_DIR"));
    here.ancestors().nth(3).map(Path::to_path_buf).unwrap_or_else(|| here.to_path_buf())
}

impl Default for Settings {
    fn default() -> Self {
        let repo = default_repo();
        let workspace = repo.ancestors().nth(2).map(Path::to_path_buf).unwrap_or_else(|| repo.clone());
        let downloads = std::env::var("USERPROFILE").map(|h| format!("{h}\\Downloads")).unwrap_or_default();
        let jobs = std::thread::available_parallelism().map(|n| n.get() as u32).unwrap_or(8).clamp(1, 12);
        Settings {
            scan_dirs: if downloads.is_empty() { vec![] } else { vec![downloads] },
            extra_nups: vec![],
            work_dir: workspace.join("work").join("pipeline").to_string_lossy().into_owned(),
            out_dir: workspace.join("work").join("library").to_string_lossy().into_owned(),
            stage_mode: "auto".into(),
            keep_temp: false,
            jobs,
            repo_dir: repo.to_string_lossy().into_owned(),
            python: workspace.join("work").join("toolchain").join("python313-embed").join("python.exe").to_string_lossy().into_owned(),
            covers: BTreeMap::new(),
        }
    }
}

pub fn data_dir() -> PathBuf {
    let base = std::env::var("LOCALAPPDATA").map(PathBuf::from).unwrap_or_else(|_| std::env::temp_dir());
    let d = base.join("RecompLauncher");
    let _ = std::fs::create_dir_all(&d);
    d
}

fn path() -> PathBuf {
    data_dir().join("settings.json")
}

pub fn load() -> Settings {
    std::fs::read_to_string(path()).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

pub fn save(s: &Settings) -> Result<(), String> {
    let text = serde_json::to_string_pretty(s).map_err(|e| e.to_string())?;
    let tmp = path().with_extension("json.tmp");
    std::fs::write(&tmp, text).map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, path()).map_err(|e| e.to_string())
}
