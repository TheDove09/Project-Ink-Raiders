import { GameEntry } from '../api';
import type { BuildState } from '../App';
import Cover from './Cover';

interface Props {
  game: GameEntry;
  build?: BuildState;
  active: boolean;
  queued: boolean;
  onOpen: () => void;
  onBuild: () => void;
  onPlay: () => void;
}

export function overallProgress(b?: BuildState): number {
  if (!b) return 0;
  if (b.result) return b.result.ok ? 1 : b.index / 7;     // a failed build keeps the bar where it stopped
  const per = b.total > 0 ? b.done / b.total : 0;
  return Math.min(1, (b.index + per) / 7);
}

export default function GameCard({ game, build, active, queued, onOpen, onBuild, onPlay }: Props) {
  const failed = build?.result && !build.result.ok && !active;
  return (
    <div className="card" tabIndex={0} onClick={onOpen} onKeyDown={(e) => e.key === 'Enter' && onOpen()} data-nav="card">
      <div className="card-cover">
        <Cover game={game} />
        <div className="chips">
          {game.support === 'supported' && <span className="chip ok">Supported</span>}
          {game.support === 'experimental' && <span className="chip exp">Experimental</span>}
          {game.support === 'unreadable' && <span className="chip bad">Unreadable</span>}
          {game.nup_external && !game.built && <span className="chip dim">External drive</span>}
        </div>
        {active && (
          <div className="card-progress">
            <div className="bar"><div className="fill" style={{ width: `${overallProgress(build) * 100}%` }} /></div>
            <span>{build?.label ?? 'Starting'}{build && build.total > 0 ? ` · ${Math.round((build.done / build.total) * 100)}%` : ''}</span>
          </div>
        )}
      </div>
      <div className="card-body">
        <div className="card-title" title={game.title}>{game.title}</div>
        <div className="card-sub">
          {game.version ? `v${game.version}` : game.title_id.startsWith('bad:') ? 'Cannot read file' : game.title_id}
          {failed && <span className="err"> · build failed</span>}
        </div>
        <div className="card-actions" onClick={(e) => e.stopPropagation()}>
          {game.built ? (
            <button className="btn play" onClick={onPlay}>▶ Play</button>
          ) : active ? (
            <button className="btn ghost" onClick={onOpen}>Building…</button>
          ) : queued ? (
            <button className="btn ghost" disabled>Queued</button>
          ) : (
            <button className="btn" onClick={onBuild} disabled={!game.nup_path || !!game.nup_error}>Recompile</button>
          )}
        </div>
      </div>
    </div>
  );
}
