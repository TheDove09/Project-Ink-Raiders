// SPDX-License-Identifier: GPL-3.0-or-later
//! Recomp Launcher: a library of Switch 2 .nup packages and the games built from them, with one-click recompiling
//! (tools/pipeline/recomp_pipeline.py) and playing.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod jobs;
mod library;
mod nup;
mod settings;

use std::os::windows::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::{Arc, Mutex};
use tauri::State;

struct AppState {
    settings: Mutex<settings::Settings>,
    jobs: jobs::Shared,
}

#[tauri::command]
fn get_settings(state: State<AppState>) -> settings::Settings {
    state.settings.lock().unwrap().clone()
}

#[tauri::command]
fn save_settings(state: State<AppState>, new: settings::Settings) -> Result<settings::Settings, String> {
    settings::save(&new)?;
    *state.settings.lock().unwrap() = new.clone();
    Ok(new)
}

#[tauri::command]
async fn scan_library(state: State<'_, AppState>) -> Result<Vec<library::GameEntry>, String> {
    let s = state.settings.lock().unwrap().clone();
    tauri::async_runtime::spawn_blocking(move || library::scan(&s)).await.map_err(|e| e.to_string())
}

/// Files dropped on the window or picked: .nup files are remembered (unless a scan folder already covers them), folders become
/// scan folders.
#[tauri::command]
fn add_paths(state: State<AppState>, paths: Vec<String>) -> Result<settings::Settings, String> {
    let mut s = state.settings.lock().unwrap();
    for p in paths {
        let pb = PathBuf::from(&p);
        if pb.is_dir() {
            if !s.scan_dirs.iter().any(|d| d.eq_ignore_ascii_case(&p)) {
                s.scan_dirs.push(p);
            }
        } else if pb.extension().map(|x| x.eq_ignore_ascii_case("nup")).unwrap_or(false) {
            let covered = s.scan_dirs.iter().any(|d| pb.parent().map(|par| par.starts_with(d)).unwrap_or(false));
            if !covered && !s.extra_nups.iter().any(|x| x.eq_ignore_ascii_case(&p)) {
                s.extra_nups.push(p);
            }
        }
    }
    settings::save(&s)?;
    Ok(s.clone())
}

#[derive(serde::Serialize)]
struct ToolCheck {
    name: String,
    ok: bool,
    detail: String,
}

#[tauri::command]
fn check_tools(state: State<AppState>) -> Vec<ToolCheck> {
    let s = state.settings.lock().unwrap().clone();
    let repo = Path::new(&s.repo_dir);
    let workspace = repo.ancestors().nth(2).unwrap_or(repo);
    let pf86 = std::env::var("ProgramFiles(x86)").unwrap_or_else(|_| "C:\\Program Files (x86)".into());
    let vswhere = Path::new(&pf86).join("Microsoft Visual Studio").join("Installer").join("vswhere.exe");
    let node = workspace.join("work").join("release").join("node").join("node.exe");
    let checks = [
        ("Pipeline", repo.join("tools").join("pipeline").join("recomp_pipeline.py")),
        ("Python 3.13", PathBuf::from(&s.python)),
        ("Node.js", node),
        ("Visual Studio Build Tools", vswhere),
        ("Shader translator", workspace.join("work").join("raiders").join("build").join("native-x64-v2").join("tools").join("spirv_harness").join("spirv_harness.exe")),
        ("ASTC texture library", workspace.join("work").join("raiders").join("build").join("astc-textures").join("astc2bc7.lib")),
    ];
    checks
        .into_iter()
        .map(|(n, p)| ToolCheck { name: n.into(), ok: p.exists(), detail: p.to_string_lossy().into_owned() })
        .collect()
}

#[tauri::command]
fn start_build(app: tauri::AppHandle, state: State<AppState>, title_id: String, nup_path: String) -> Result<jobs::JobsView, String> {
    let s = state.settings.lock().unwrap().clone();
    jobs::enqueue(app, state.jobs.clone(), s, jobs::Job { title_id, nup_path })
}

#[tauri::command]
fn cancel_build(state: State<AppState>, title_id: String) -> jobs::JobsView {
    jobs::cancel(&state.jobs, &title_id)
}

#[tauri::command]
fn job_status(state: State<AppState>) -> jobs::JobsView {
    jobs::view(&state.jobs.lock().unwrap())
}

#[tauri::command]
fn play(exe: String, game_dir: String) -> Result<(), String> {
    Command::new(&exe).current_dir(&game_dir).spawn().map(|_| ()).map_err(|e| format!("could not start {exe}: {e}"))
}

#[tauri::command]
fn open_path(path: String) -> Result<(), String> {
    Command::new("explorer").arg(&path).creation_flags(0x0800_0000).spawn().map(|_| ()).map_err(|e| e.to_string())
}

/// A custom cover: the image is copied into the launcher's data folder (covers\<title id>.<ext>).
#[tauri::command]
fn set_cover(state: State<AppState>, title_id: String, image: Option<String>) -> Result<settings::Settings, String> {
    let mut s = state.settings.lock().unwrap();
    match image {
        Some(img) => {
            let src = PathBuf::from(&img);
            let ext = src.extension().map(|e| e.to_string_lossy().to_lowercase()).unwrap_or_else(|| "png".into());
            let dir = settings::data_dir().join("covers");
            std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
            let dst = dir.join(format!("{title_id}.{ext}"));
            std::fs::copy(&src, &dst).map_err(|e| e.to_string())?;
            s.covers.insert(title_id, dst.to_string_lossy().into_owned());
        }
        None => {
            s.covers.remove(&title_id);
        }
    }
    settings::save(&s)?;
    Ok(s.clone())
}

/// The last lines of a game's pipeline log (work\<profile>\pipeline.log), for the log drawer after a restart.
#[tauri::command]
fn read_build_log(state: State<AppState>, profile: String) -> String {
    let s = state.settings.lock().unwrap().clone();
    let p = Path::new(&s.work_dir).join(profile).join("pipeline.log");
    let text = std::fs::read_to_string(p).unwrap_or_default();
    let lines: Vec<&str> = text.lines().collect();
    lines[lines.len().saturating_sub(400)..].join("\n")
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(AppState { settings: Mutex::new(settings::load()), jobs: Arc::new(Mutex::new(jobs::Jobs::default())) })
        .invoke_handler(tauri::generate_handler![
            get_settings, save_settings, scan_library, add_paths, check_tools, start_build, cancel_build, job_status, play,
            open_path, set_cover, read_build_log
        ])
        .run(tauri::generate_context!())
        .expect("error while running the launcher");
}
