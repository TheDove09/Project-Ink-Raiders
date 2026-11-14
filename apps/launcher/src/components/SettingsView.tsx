import { useState } from 'react';
import { open } from '@tauri-apps/plugin-dialog';
import { Settings, ToolCheck } from '../api';

interface Props {
  settings: Settings;
  tools: ToolCheck[];
  onSave: (s: Settings) => void;
}

export default function SettingsView({ settings, tools, onSave }: Props) {
  const [s, setS] = useState<Settings>(settings);
  const dirty = JSON.stringify(s) !== JSON.stringify(settings);
  const pickDir = async (title: string): Promise<string | null> => {
    const d = await open({ directory: true, multiple: false, title });
    return typeof d === 'string' ? d : null;
  };

  return (
    <main className="settings">
      <section>
        <h2>Scan folders</h2>
        <p className="dim">.nup files in these folders (and up to four folders deep) appear in the library automatically.</p>
        <ul className="dir-list">
          {s.scan_dirs.map((d) => (
            <li key={d}>
              <span className="path">{d}</span>
              <button className="btn ghost small" onClick={() => setS({ ...s, scan_dirs: s.scan_dirs.filter((x) => x !== d) })}>Remove</button>
            </li>
          ))}
          {s.scan_dirs.length === 0 && <li className="dim">No scan folders.</li>}
        </ul>
        <button
          className="btn ghost"
          onClick={async () => {
            const d = await pickDir('Add a folder to scan');
            if (d && !s.scan_dirs.includes(d)) setS({ ...s, scan_dirs: [...s.scan_dirs, d] });
          }}
        >
          Add folder…
        </button>
        {s.extra_nups.length > 0 && (
          <>
            <h3>Added files</h3>
            <ul className="dir-list">
              {s.extra_nups.map((d) => (
                <li key={d}>
                  <span className="path">{d}</span>
                  <button className="btn ghost small" onClick={() => setS({ ...s, extra_nups: s.extra_nups.filter((x) => x !== d) })}>Remove</button>
                </li>
              ))}
            </ul>
          </>
        )}
      </section>

      <section>
        <h2>Storage</h2>
        <label className="field">
          <span>Work folder (fast SSD)</span>
          <div className="row">
            <input value={s.work_dir} onChange={(e) => setS({ ...s, work_dir: e.target.value })} />
            <button className="btn ghost small" onClick={async () => { const d = await pickDir('Work folder'); if (d) setS({ ...s, work_dir: d }); }}>Browse…</button>
          </div>
          <small className="dim">Code is unpacked and compiled here. Temporary files are removed after each build.</small>
        </label>
        <label className="field">
          <span>Games folder</span>
          <div className="row">
            <input value={s.out_dir} onChange={(e) => setS({ ...s, out_dir: e.target.value })} />
            <button className="btn ghost small" onClick={async () => { const d = await pickDir('Games folder'); if (d) setS({ ...s, out_dir: d }); }}>Browse…</button>
          </div>
          <small className="dim">Each finished game gets its own folder here (about the size of its .nup).</small>
        </label>
        <label className="field">
          <span>Copy .nup files from other drives to the work folder first</span>
          <select value={s.stage_mode} onChange={(e) => setS({ ...s, stage_mode: e.target.value as Settings['stage_mode'] })}>
            <option value="auto">Automatically (when the .nup is on another drive)</option>
            <option value="always">Always</option>
            <option value="never">Never</option>
          </select>
        </label>
        <label className="check">
          <input type="checkbox" checked={s.keep_temp} onChange={(e) => setS({ ...s, keep_temp: e.target.checked })} />
          Keep temporary build files (faster rebuilds, several GB per game)
        </label>
        <label className="field">
          <span>Compile processes</span>
          <input type="number" min={1} max={32} value={s.jobs} onChange={(e) => setS({ ...s, jobs: Math.max(1, Number(e.target.value) || 1) })} />
        </label>
      </section>

      <section>
        <h2>Build tools</h2>
        <ul className="tools">
          {tools.map((t) => (
            <li key={t.name} className={t.ok ? 'ok' : 'bad'}>
              <span className="dot" />
              <span>{t.name}</span>
              <span className="path dim" title={t.detail}>{t.ok ? t.detail : 'not found: ' + t.detail}</span>
            </li>
          ))}
        </ul>
        <label className="field">
          <span>Recompiler folder (repository)</span>
          <input value={s.repo_dir} onChange={(e) => setS({ ...s, repo_dir: e.target.value })} />
        </label>
        <label className="field">
          <span>Python 3.13</span>
          <input value={s.python} onChange={(e) => setS({ ...s, python: e.target.value })} />
        </label>
      </section>

      <div className="save-bar">
        <button className="btn ghost" disabled={!dirty} onClick={() => setS(settings)}>Undo changes</button>
        <button className="btn" disabled={!dirty} onClick={() => onSave(s)}>Save</button>
      </div>
    </main>
  );
}
