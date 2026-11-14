// SPDX-License-Identifier: GPL-3.0-or-later
//! Per-game settings, kept where the game itself reads them:
//!   controls.ini [display]  window_mode (windowed | borderless | exclusive), vsync, fps_limit (0 = none, auto), render_scale (25-400, auto)
//!   <profile>.ini [am] operation_mode (docked | handheld), [fs] save_dir (empty = the game's default under %APPDATA%)
//! Only these keys are touched; every other line of both files is kept as it is.

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct GameSettings {
    pub window_mode: String,
    pub vsync: bool,
    pub fps_limit: String,
    pub render_scale: String,
    pub operation_mode: String,
    pub save_dir: String,
    /// Read only: where saves go when save_dir is empty.
    pub save_dir_default: String,
}

fn get(text: &str, section: &str, key: &str) -> Option<String> {
    let mut in_sec = false;
    for line in text.lines() {
        let t = line.trim();
        if t.starts_with('[') {
            in_sec = t.trim_matches(|c| c == '[' || c == ']').eq_ignore_ascii_case(section);
            continue;
        }
        if !in_sec || t.starts_with('#') || t.starts_with(';') {
            continue;
        }
        if let Some((k, v)) = t.split_once('=') {
            if k.trim().eq_ignore_ascii_case(key) {
                return Some(v.trim().trim_matches('"').to_string());
            }
        }
    }
    None
}

/// Sets (or with None removes) section.key, keeping the rest of the file; adds the section at the end when it is missing.
fn set(text: &str, section: &str, key: &str, value: Option<&str>) -> String {
    let mut out: Vec<String> = Vec::new();
    let mut in_sec = false;
    let mut found_sec = false;
    let mut done = false;
    let lines: Vec<&str> = text.lines().collect();
    for (i, line) in lines.iter().enumerate() {
        let t = line.trim();
        if t.starts_with('[') {
            if in_sec && !done {
                if let Some(v) = value {
                    // insert before the trailing blank lines of the section
                    let mut at = out.len();
                    while at > 0 && out[at - 1].trim().is_empty() {
                        at -= 1;
                    }
                    out.insert(at, format!("{key} = {v}"));
                }
                done = true;
            }
            in_sec = t.trim_matches(|c| c == '[' || c == ']').eq_ignore_ascii_case(section);
            found_sec |= in_sec;
            out.push(line.to_string());
            continue;
        }
        if in_sec && !done {
            if let Some((k, _)) = t.split_once('=') {
                if k.trim().eq_ignore_ascii_case(key) && !t.starts_with('#') {
                    if let Some(v) = value {
                        out.push(format!("{key} = {v}"));
                    }
                    done = true;
                    continue;
                }
            }
        }
        out.push(line.to_string());
        if i + 1 == lines.len() && in_sec && !done {
            if let Some(v) = value {
                out.push(format!("{key} = {v}"));
            }
            done = true;
        }
    }
    if !found_sec {
        if let Some(v) = value {
            if out.last().map(|l| !l.trim().is_empty()).unwrap_or(false) {
                out.push(String::new());
            }
            out.push(format!("[{section}]"));
            out.push(format!("{key} = {v}"));
        }
    }
    let mut s = out.join("\r\n");
    s.push_str("\r\n");
    s
}

fn game_ini(game_dir: &Path) -> Result<PathBuf, String> {
    let gj = std::fs::read_to_string(game_dir.join("game.json")).map_err(|e| format!("game.json: {e}"))?;
    let v: serde_json::Value = serde_json::from_str(&gj).map_err(|e| e.to_string())?;
    let profile = v["profile"].as_str().ok_or("game.json has no profile")?;
    Ok(game_dir.join(format!("{profile}.ini")))
}

fn default_save_dir(game_dir: &Path) -> String {
    // the save folder name is in the game's profile (game.toml [files] save_folder); show the AppData location it resolves to
    let appdata = std::env::var("APPDATA").unwrap_or_default();
    let name = std::fs::read_to_string(game_dir.join("game.json"))
        .ok()
        .and_then(|t| serde_json::from_str::<serde_json::Value>(&t).ok())
        .and_then(|v| v["save_folder"].as_str().map(str::to_string));
    match name {
        Some(n) => format!("{appdata}\\{n}\\save"),
        None => format!("{appdata}\\<game>\\save"),
    }
}

pub fn load(game_dir: &str) -> Result<GameSettings, String> {
    let dir = Path::new(game_dir);
    let controls = std::fs::read_to_string(dir.join("controls.ini")).unwrap_or_default();
    let ini = std::fs::read_to_string(game_ini(dir)?).unwrap_or_default();
    let d = |k: &str, def: &str| get(&controls, "display", k).unwrap_or_else(|| def.to_string());
    Ok(GameSettings {
        window_mode: d("window_mode", "windowed"),
        vsync: matches!(d("vsync", "0").as_str(), "1" | "true" | "on" | "yes"),
        fps_limit: d("fps_limit", "60"),
        render_scale: d("render_scale", "100"),
        operation_mode: get(&ini, "am", "operation_mode").unwrap_or_else(|| "docked".into()),
        save_dir: get(&ini, "fs", "save_dir").unwrap_or_default(),
        save_dir_default: default_save_dir(dir),
    })
}

pub fn save(game_dir: &str, s: &GameSettings) -> Result<(), String> {
    let dir = Path::new(game_dir);
    if !matches!(s.window_mode.as_str(), "windowed" | "borderless" | "exclusive") {
        return Err(format!("unknown window mode {}", s.window_mode));
    }
    if !matches!(s.operation_mode.as_str(), "docked" | "handheld") {
        return Err(format!("unknown console mode {}", s.operation_mode));
    }
    let num_or_auto = |v: &str, lo: i64, hi: i64| v == "auto" || v.parse::<i64>().map(|n| (lo..=hi).contains(&n)).unwrap_or(false);
    if !num_or_auto(&s.fps_limit, 0, 1000) || !num_or_auto(&s.render_scale, 25, 400) {
        return Err("frame cap or resolution scale out of range".into());
    }
    let cpath = dir.join("controls.ini");
    let mut c = std::fs::read_to_string(&cpath).unwrap_or_default();
    c = set(&c, "display", "window_mode", Some(&s.window_mode));
    c = set(&c, "display", "vsync", Some(if s.vsync { "1" } else { "0" }));
    c = set(&c, "display", "fps_limit", Some(&s.fps_limit));
    c = set(&c, "display", "render_scale", Some(&s.render_scale));
    std::fs::write(&cpath, c).map_err(|e| format!("controls.ini: {e}"))?;

    let ipath = game_ini(dir)?;
    let mut i = std::fs::read_to_string(&ipath).unwrap_or_default();
    i = set(&i, "am", "operation_mode", Some(&s.operation_mode));
    let sd = s.save_dir.trim();
    let quoted = format!("\"{}\"", sd.replace('\\', "/"));
    i = set(&i, "fs", "save_dir", if sd.is_empty() { None } else { Some(&quoted) });
    std::fs::write(&ipath, i).map_err(|e| format!("{}: {e}", ipath.display()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn save_and_load_a_game_folder() {
        let dir = std::env::temp_dir().join(format!("rl-gs-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("game.json"), r#"{"profile":"sra","save_folder":"SplatoonRaidersNative"}"#).unwrap();
        std::fs::write(dir.join("sra.ini"), "[boot]\nrun_module_init = true\n\n[am]\n# comment\noperation_mode = handheld\n").unwrap();
        std::fs::write(dir.join("controls.ini"), "[mouse]\nyaw = 1\n\n[display]\nwindow_mode = windowed\nvsync = 0\nfps_limit = 60\n").unwrap();
        let d = dir.to_string_lossy().into_owned();
        let mut s = load(&d).unwrap();
        assert_eq!((s.window_mode.as_str(), s.operation_mode.as_str(), s.fps_limit.as_str()), ("windowed", "handheld", "60"));
        assert!(s.save_dir_default.ends_with("SplatoonRaidersNative\\save"));
        s.window_mode = "borderless".into();
        s.fps_limit = "120".into();
        s.render_scale = "150".into();
        s.operation_mode = "docked".into();
        s.save_dir = "D:\\Saves\\Raiders".into();
        save(&d, &s).unwrap();
        let t = load(&d).unwrap();
        assert_eq!((t.window_mode.as_str(), t.fps_limit.as_str(), t.render_scale.as_str()), ("borderless", "120", "150"));
        assert_eq!((t.operation_mode.as_str(), t.save_dir.as_str()), ("docked", "D:/Saves/Raiders"));
        let ini = std::fs::read_to_string(dir.join("sra.ini")).unwrap();
        assert!(ini.contains("run_module_init = true") && ini.contains("# comment"));
        assert!(std::fs::read_to_string(dir.join("controls.ini")).unwrap().contains("yaw = 1"));
        assert!(save(&d, &GameSettings { window_mode: "huge".into(), ..t.clone() }).is_err());
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn set_keeps_other_lines() {
        let t = "# c\n[display]\nwidth = 1280\nvsync = 0\n\n[audio]\nvolume = 1\n";
        let u = set(t, "display", "vsync", Some("1"));
        assert!(u.contains("vsync = 1") && u.contains("width = 1280") && u.contains("volume = 1") && u.contains("# c"));
        let u = set(&u, "display", "render_scale", Some("150"));
        assert_eq!(get(&u, "display", "render_scale").as_deref(), Some("150"));
        assert!(u.find("render_scale").unwrap() < u.find("[audio]").unwrap());
        let u = set(&u, "fs", "save_dir", Some("\"D:/s\""));
        assert_eq!(get(&u, "fs", "save_dir").as_deref(), Some("D:/s"));
        let u = set(&u, "fs", "save_dir", None);
        assert_eq!(get(&u, "fs", "save_dir"), None);
        let u = set("[am]\noperation_mode = docked", "am", "operation_mode", Some("handheld"));
        assert_eq!(get(&u, "am", "operation_mode").as_deref(), Some("handheld"));
        assert_eq!(u.matches("operation_mode").count(), 1);
    }
}
