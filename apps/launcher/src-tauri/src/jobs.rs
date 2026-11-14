// SPDX-License-Identifier: GPL-3.0-or-later
//! Build jobs: runs tools/pipeline/recomp_pipeline.py build --json for one .nup at a time (a queue for the rest) and forwards each
//! JSON progress line to the window as a "pipeline" event.

use crate::settings::Settings;
use serde::Serialize;
use std::collections::VecDeque;
use std::io::{BufRead, BufReader};
use std::os::windows::process::CommandExt;
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex};
use tauri::{AppHandle, Emitter};

const CREATE_NO_WINDOW: u32 = 0x0800_0000;

#[derive(Serialize, Clone, Debug)]
pub struct Job {
    pub title_id: String,
    pub nup_path: String,
}

#[derive(Default)]
pub struct Jobs {
    pub current: Option<Job>,
    pub queue: VecDeque<Job>,
    child: Option<Arc<Mutex<Child>>>,
    cancelled: bool,
}

#[derive(Serialize, Clone)]
pub struct JobsView {
    pub current: Option<Job>,
    pub queue: Vec<Job>,
}

#[derive(Serialize, Clone)]
struct PipelineEvent {
    title_id: String,
    event: serde_json::Value,
}

pub type Shared = Arc<Mutex<Jobs>>;

pub fn view(j: &Jobs) -> JobsView {
    JobsView { current: j.current.clone(), queue: j.queue.iter().cloned().collect() }
}

pub fn enqueue(app: AppHandle, shared: Shared, settings: Settings, job: Job) -> Result<JobsView, String> {
    {
        let mut j = shared.lock().unwrap();
        let busy = |x: &Job| x.title_id == job.title_id;
        if j.current.as_ref().map(busy).unwrap_or(false) || j.queue.iter().any(busy) {
            return Ok(view(&j));
        }
        j.queue.push_back(job);
    }
    start_next(app, shared.clone(), settings);
    let j = shared.lock().unwrap();
    Ok(view(&j))
}

pub fn cancel(shared: &Shared, title_id: &str) -> JobsView {
    let mut j = shared.lock().unwrap();
    j.queue.retain(|x| x.title_id != title_id);
    if j.current.as_ref().map(|c| c.title_id == title_id).unwrap_or(false) {
        j.cancelled = true;
        if let Some(c) = &j.child {
            let pid = c.lock().unwrap().id();
            // the pipeline runs cmd, cmake, ninja and compilers below it: end the whole tree
            let _ = Command::new("taskkill").args(["/PID", &pid.to_string(), "/T", "/F"]).creation_flags(CREATE_NO_WINDOW).status();
        }
    }
    view(&j)
}

fn start_next(app: AppHandle, shared: Shared, settings: Settings) {
    let job = {
        let mut j = shared.lock().unwrap();
        if j.current.is_some() {
            return;
        }
        let Some(job) = j.queue.pop_front() else { return };
        j.current = Some(job.clone());
        j.cancelled = false;
        job
    };
    let _ = app.emit("jobs", view(&shared.lock().unwrap()));
    let emit = {
        let app = app.clone();
        let tid = job.title_id.clone();
        move |event: serde_json::Value| {
            let _ = app.emit("pipeline", PipelineEvent { title_id: tid.clone(), event });
        }
    };

    let script = PathBuf::from(&settings.repo_dir).join("tools").join("pipeline").join("recomp_pipeline.py");
    let mut cmd = Command::new(&settings.python);
    cmd.arg(&script)
        .args(["build", &job.nup_path, "--json", "--work", &settings.work_dir, "--out", &settings.out_dir])
        .args(["--stage", &settings.stage_mode, "--jobs", &settings.jobs.to_string()])
        .current_dir(&settings.repo_dir)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .creation_flags(CREATE_NO_WINDOW);
    if settings.keep_temp {
        cmd.arg("--keep-temp");
    }
    let spawned = cmd.spawn();
    let mut child = match spawned {
        Ok(c) => c,
        Err(e) => {
            emit(serde_json::json!({"event": "done", "ok": false, "stage": null, "error": format!("could not start the pipeline ({}): {}", settings.python, e)}));
            finish(app, shared, settings);
            return;
        }
    };
    let stdout = child.stdout.take().unwrap();
    let stderr = child.stderr.take().unwrap();
    let child = Arc::new(Mutex::new(child));
    shared.lock().unwrap().child = Some(child.clone());

    // stderr: Python tracebacks and tool noise become log lines
    {
        let emit = emit.clone();
        std::thread::spawn(move || {
            for line in BufReader::new(stderr).lines().map_while(Result::ok) {
                emit(serde_json::json!({"event": "log", "stage": null, "line": line}));
            }
        });
    }
    std::thread::spawn(move || {
        let mut saw_done = false;
        for line in BufReader::new(stdout).lines().map_while(Result::ok) {
            match serde_json::from_str::<serde_json::Value>(&line) {
                Ok(v) => {
                    if v["event"] == "done" {
                        saw_done = true;
                    }
                    emit(v)
                }
                Err(_) => emit(serde_json::json!({"event": "log", "stage": null, "line": line})),
            }
        }
        let code = child.lock().unwrap().wait().ok().and_then(|s| s.code());
        let cancelled = shared.lock().unwrap().cancelled;
        if !saw_done {
            let msg = if cancelled { "cancelled".to_string() } else { format!("the pipeline stopped unexpectedly (exit code {:?})", code) };
            emit(serde_json::json!({"event": "done", "ok": false, "stage": null, "error": msg, "cancelled": cancelled}));
        }
        finish(app, shared, settings);
    });
}

fn finish(app: AppHandle, shared: Shared, settings: Settings) {
    {
        let mut j = shared.lock().unwrap();
        j.current = None;
        j.child = None;
    }
    let _ = app.emit("jobs", view(&shared.lock().unwrap()));
    start_next(app, shared, settings);
}
