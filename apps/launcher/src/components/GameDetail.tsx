import { useEffect, useRef, useState } from 'react';
import { open } from '@tauri-apps/plugin-dialog';
import { api, formatSize, GameEntry, STAGES } from '../api';
import type { BuildState } from '../App';
import Cover from './Cover';
import GameSettingsPanel from './GameSettingsPanel';
import { overallProgress } from './GameCard';

interface Props {
  game: GameEntry;
  build?: BuildState;
  active: boolean;
  queued: boolean;
  onClose: () => void;
  onBuild: () => void;
  onCancel: () => void;
  onPlay: () => void;
  onCover: (image: string | null) => void;
}

function elapsed(ms: number): string {
  const s = Math.floor(ms / 1000);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
}

export default function GameDetail({ game, build, active, queued, onClose, onBuild, onCancel, onPlay, onCover }: Props) {
  const [logOpen, setLogOpen] = useState(false);
  const [oldLog, setOldLog] = useState<string>('');
  const [, tick] = useState(0);
  const logRef = useRef<HTMLPreElement>(null);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && onClose();
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);
  useEffect(() => {
    if (!active) return;
    const t = setInterval(() => tick((x) => x + 1), 1000);
    return () => clearInterval(t);
  }, [active]);
  useEffect(() => {
    if (logOpen && !build) api.readBuildLog(game.profile).then(setOldLog);
  }, [logOpen, build, game.profile]);
  useEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight;
  }, [build?.log.length, logOpen]);

  const failed = build?.result && !build.result.ok;
  const succeeded = build?.result?.ok;
  const showSteps = active || queued || build?.result;
  const pickCover = async () => {
    const img = await open({ multiple: false, filters: [{ name: 'Image', extensions: ['png', 'jpg', 'jpeg', 'webp'] }] });
    if (typeof img === 'string') onCover(img);
  };

  return (
    <div className="detail-backdrop" onClick={onClose}>
      <aside className="detail" onClick={(e) => e.stopPropagation()}>
        <button className="close" onClick={onClose} aria-label="Close">✕</button>
        <div className="detail-head">
          <div className="detail-cover">
            <Cover game={game} large />
            <div className="cover-actions">
              <button className="btn ghost small" onClick={pickCover}>Set cover…</button>
              {game.cover && <button className="btn ghost small" onClick={() => onCover(null)}>Reset</button>}
            </div>
          </div>
          <div className="detail-info">
            <h1>{game.title}</h1>
            <div className="meta">
              {game.version && <span>Version {game.version}</span>}
              {!game.title_id.startsWith('bad:') && <span>Title ID {game.title_id}</span>}
              <span>Profile {game.profile}</span>
            </div>
            <p className={game.support === 'supported' ? 'support ok' : 'support exp'}>
              {game.support === 'supported'
                ? 'Supported: a tested, hand-made profile. Builds and plays.'
                : game.support === 'experimental'
                  ? 'Experimental: the profile is generated automatically. The game should build; whether it plays depends on how much of it the runtime already covers.'
                  : `This file cannot be read: ${game.nup_error}`}
            </p>
            {game.nup_path && (
              <div className="path-row">
                <span className="label">Package</span>
                <span className="path" title={game.nup_path}>{game.nup_path}</span>
                <span className="dim">{formatSize(game.nup_size)}{game.nup_external ? ' · external drive (copied to the SSD to build)' : ''}</span>
              </div>
            )}
            {game.built && (
              <div className="path-row">
                <span className="label">Installed</span>
                <span className="path" title={game.built.game_dir}>{game.built.game_dir}</span>
                <button className="btn ghost small" onClick={() => api.openPath(game.built!.game_dir)}>Open folder</button>
              </div>
            )}
            <div className="detail-actions">
              {game.built && <button className="btn play big" onClick={onPlay}>▶ Play</button>}
              {active ? (
                <button className="btn danger" onClick={onCancel}>Cancel build</button>
              ) : queued ? (
                <button className="btn ghost" onClick={onCancel}>Remove from queue</button>
              ) : (
                game.nup_path && !game.nup_error && (
                  <button className={game.built ? 'btn ghost' : 'btn big'} onClick={onBuild}>{game.built ? 'Rebuild' : 'Recompile'}</button>
                )
              )}
            </div>
          </div>
        </div>

        {showSteps && (
          <section className="build">
            <div className="build-head">
              <h3>{active ? build?.label ?? 'Starting' : queued ? 'Waiting for the current build' : succeeded ? 'Ready to play' : failed ? 'Build failed' : 'Build'}</h3>
              {active && build && <span className="dim">{elapsed(Date.now() - build.started)}</span>}
            </div>
            <div className="bar big-bar">
              <div className={failed ? 'fill fail' : 'fill'} style={{ width: `${succeeded ? 100 : overallProgress(build) * 100}%` }} />
            </div>
            <ol className="steps">
              {STAGES.map((s, i) => {
                const idx = build?.index ?? -1;
                const state = succeeded || i < idx ? 'done' : i === idx ? (failed ? 'fail' : active ? 'now' : 'done') : 'todo';
                return (
                  <li key={s.id} className={'step ' + state}>
                    <span className="dot" />
                    <span>{s.label}</span>
                    {state === 'now' && build && build.total > 0 && (
                      <span className="dim"> {build.done.toLocaleString()} / {build.total.toLocaleString()}</span>
                    )}
                  </li>
                );
              })}
            </ol>
            {failed && <div className="error-box">{build?.result?.error}</div>}
          </section>
        )}

        {game.built && !active && <GameSettingsPanel gameDir={game.built.game_dir} />}

        <section className="log-drawer">
          <button className="log-toggle" onClick={() => setLogOpen((o) => !o)}>
            {logOpen ? '▾' : '▸'} Build log {build ? `(${build.log.length} lines)` : ''}
          </button>
          {logOpen && <pre className="log" ref={logRef}>{build ? build.log.join('\n') : oldLog || 'No build log yet.'}</pre>}
        </section>
      </aside>
    </div>
  );
}
