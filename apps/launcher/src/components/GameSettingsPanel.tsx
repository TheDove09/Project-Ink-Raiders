import { useEffect, useState } from 'react';
import { open } from '@tauri-apps/plugin-dialog';
import { api, GameSettings } from '../api';

// Per-game settings, saved where the game reads them (controls.ini [display], <profile>.ini [am] / [fs]). Applied the next time the
// game starts; the in-game settings menu (F1) changes the display ones live as well.
export default function GameSettingsPanel({ gameDir }: { gameDir: string }) {
  const [saved, setSaved] = useState<GameSettings | null>(null);
  const [s, setS] = useState<GameSettings | null>(null);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  useEffect(() => {
    api.getGameSettings(gameDir).then(
      (g) => {
        setSaved(g);
        setS(g);
      },
      (e) => setMsg({ ok: false, text: String(e) }),
    );
  }, [gameDir]);

  if (!s || !saved) return msg ? <div className="error-box">{msg.text}</div> : null;
  const dirty = JSON.stringify(s) !== JSON.stringify(saved);
  const scale = s.render_scale === 'auto' ? 100 : Number(s.render_scale);

  return (
    <section className="game-settings">
      <h3>Game settings</h3>
      <div className="gs-grid">
        <label className="field">
          <span>Display</span>
          <select value={s.window_mode} onChange={(e) => setS({ ...s, window_mode: e.target.value })}>
            <option value="windowed">Window</option>
            <option value="borderless">Fullscreen (borderless)</option>
            <option value="exclusive">Fullscreen (exclusive)</option>
          </select>
        </label>
        <label className="field">
          <span>Console mode</span>
          <select value={s.operation_mode} onChange={(e) => setS({ ...s, operation_mode: e.target.value })}>
            <option value="handheld">Handheld (lighter, steadier frame rate)</option>
            <option value="docked">Docked (TV resolution, needs a strong GPU)</option>
          </select>
        </label>
        <label className="field">
          <span>Frame cap</span>
          <select value={s.fps_limit} onChange={(e) => setS({ ...s, fps_limit: e.target.value })}>
            <option value="30">30 fps</option>
            <option value="60">60 fps</option>
            <option value="120">120 fps</option>
            <option value="0">No cap</option>
            <option value="auto">Display refresh rate</option>
            {!['30', '60', '120', '0', 'auto'].includes(s.fps_limit) && <option value={s.fps_limit}>{s.fps_limit} fps</option>}
          </select>
        </label>
        <label className="check">
          <input type="checkbox" checked={s.vsync} onChange={(e) => setS({ ...s, vsync: e.target.checked })} />
          VSync
        </label>
        <label className="field span2">
          <span>Resolution scale: {s.render_scale === 'auto' ? 'automatic' : `${scale}%`}</span>
          <div className="row">
            <input type="range" min={50} max={200} step={25} value={scale} disabled={s.render_scale === 'auto'}
              onChange={(e) => setS({ ...s, render_scale: e.target.value })} />
            <label className="check inline">
              <input type="checkbox" checked={s.render_scale === 'auto'}
                onChange={(e) => setS({ ...s, render_scale: e.target.checked ? 'auto' : '100' })} />
              Automatic
            </label>
          </div>
        </label>
        <label className="field span2">
          <span>Save data folder</span>
          <div className="row">
            <input placeholder={s.save_dir_default} value={s.save_dir} onChange={(e) => setS({ ...s, save_dir: e.target.value })} />
            <button className="btn ghost small" onClick={async () => {
              const d = await open({ directory: true, multiple: false, title: 'Save data folder' });
              if (typeof d === 'string') setS({ ...s, save_dir: d });
            }}>Browse…</button>
            <button className="btn ghost small" onClick={() => api.openPath(s.save_dir || s.save_dir_default)}>Open</button>
          </div>
          <small className="dim">Empty: the default folder. Moving it does not copy existing saves; copy them yourself first.</small>
        </label>
      </div>
      <div className="gs-actions">
        {msg && <span className={msg.ok ? 'ok-text' : 'err-text'}>{msg.text}</span>}
        <button className="btn ghost" disabled={!dirty} onClick={() => setS(saved)}>Undo</button>
        <button className="btn" disabled={!dirty} onClick={async () => {
          try {
            const g = await api.saveGameSettings(gameDir, s);
            setSaved(g);
            setS(g);
            setMsg({ ok: true, text: 'Saved. Applies the next time the game starts.' });
          } catch (e) {
            setMsg({ ok: false, text: String(e) });
          }
        }}>Save</button>
      </div>
    </section>
  );
}
