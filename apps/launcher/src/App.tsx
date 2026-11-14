import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { listen } from '@tauri-apps/api/event';
import { getCurrentWebview } from '@tauri-apps/api/webview';
import { open } from '@tauri-apps/plugin-dialog';
import { api, GameEntry, JobsView, PipelineEvent, Settings, STAGES, ToolCheck } from './api';
import GameCard from './components/GameCard';
import GameDetail from './components/GameDetail';
import SettingsView from './components/SettingsView';
import { useGamepadNav } from './useGamepadNav';

export interface BuildState {
  stage: string | null;
  label: string;
  index: number;
  done: number;
  total: number;
  log: string[];
  result: { ok: boolean; error?: string } | null;
  started: number;
}

const MAX_LOG = 2000;

export default function App() {
  const [settings, setSettings] = useState<Settings | null>(null);
  const [games, setGames] = useState<GameEntry[]>([]);
  const [scanning, setScanning] = useState(false);
  const [jobs, setJobs] = useState<JobsView>({ current: null, queue: [] });
  const [builds, setBuilds] = useState<Record<string, BuildState>>({});
  const [view, setView] = useState<'library' | 'settings'>('library');
  const [selected, setSelected] = useState<string | null>(null);
  const [dragging, setDragging] = useState(false);
  const [tools, setTools] = useState<ToolCheck[]>([]);
  const [filter, setFilter] = useState('');
  const gridRef = useRef<HTMLDivElement>(null);

  const rescan = useCallback(async () => {
    setScanning(true);
    try {
      setGames(await api.scan());
    } finally {
      setScanning(false);
    }
  }, []);

  useEffect(() => {
    api.getSettings().then(setSettings);
    api.jobStatus().then(setJobs);
    api.checkTools().then(setTools);
    rescan();
  }, [rescan]);

  // pipeline events -> per-game build state
  useEffect(() => {
    const un1 = listen<{ title_id: string; event: PipelineEvent }>('pipeline', ({ payload }) => {
      const { title_id, event: e } = payload;
      setBuilds((all) => {
        const cur: BuildState = all[title_id] ?? { stage: null, label: 'Starting', index: 0, done: 0, total: 0, log: [], result: null, started: Date.now() };
        const next = { ...cur };
        if (e.event === 'stage') {
          Object.assign(next, { stage: e.stage, label: e.label, index: e.index, done: 0, total: 0 });
        } else if (e.event === 'progress') {
          Object.assign(next, { done: e.done, total: e.total });
        } else if (e.event === 'log') {
          next.log = cur.log.length >= MAX_LOG ? [...cur.log.slice(-MAX_LOG / 2), e.line] : [...cur.log, e.line];
        } else if (e.event === 'done') {
          next.result = { ok: e.ok, error: e.error };
          const failedAt = STAGES.findIndex((s) => s.id === e.stage);
          if (!e.ok && failedAt >= 0) next.index = failedAt;
          if (e.error) next.log = [...cur.log, 'ERROR: ' + e.error];
        }
        return { ...all, [title_id]: next };
      });
      if (payload.event.event === 'done' && payload.event.ok) rescan();
    });
    const un2 = listen<JobsView>('jobs', ({ payload }) => setJobs(payload));
    return () => {
      un1.then((f) => f());
      un2.then((f) => f());
    };
  }, [rescan]);

  // drag and drop: .nup files and folders
  useEffect(() => {
    const un = getCurrentWebview().onDragDropEvent(async (ev) => {
      const t = ev.payload.type;
      if (t === 'enter' || t === 'over') setDragging(true);
      else if (t === 'leave') setDragging(false);
      else if (t === 'drop') {
        setDragging(false);
        const s = await api.addPaths(ev.payload.paths);
        setSettings(s);
        rescan();
      }
    });
    return () => {
      un.then((f) => f());
    };
  }, [rescan]);

  const importFiles = async () => {
    const picked = await open({ multiple: true, filters: [{ name: 'Switch 2 package', extensions: ['nup'] }] });
    if (!picked) return;
    const paths = Array.isArray(picked) ? picked : [picked];
    setSettings(await api.addPaths(paths));
    rescan();
  };

  const startBuild = async (g: GameEntry) => {
    if (!g.nup_path) return;
    setBuilds((all) => ({ ...all, [g.title_id]: { stage: null, label: 'Queued', index: 0, done: 0, total: 0, log: [], result: null, started: Date.now() } }));
    setJobs(await api.startBuild(g.title_id, g.nup_path));
  };

  const shown = useMemo(
    () => games.filter((g) => !filter || g.title.toLowerCase().includes(filter.toLowerCase())),
    [games, filter],
  );
  const selectedGame = games.find((g) => g.title_id === selected) ?? null;
  const missingTools = tools.filter((t) => !t.ok);

  useGamepadNav(gridRef, view === 'library' && !selectedGame, () => setSelected(null));

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <span className="brand-dot" />
          Recomp Launcher
        </div>
        <nav className="tabs">
          <button className={view === 'library' ? 'tab active' : 'tab'} onClick={() => setView('library')}>Library</button>
          <button className={view === 'settings' ? 'tab active' : 'tab'} onClick={() => setView('settings')}>Settings</button>
        </nav>
        <div className="topbar-right">
          {view === 'library' && (
            <>
              <input className="search" placeholder="Search" value={filter} onChange={(e) => setFilter(e.target.value)} />
              <button className="btn ghost" onClick={rescan} disabled={scanning}>{scanning ? 'Scanning…' : 'Rescan'}</button>
              <button className="btn" onClick={importFiles}>Add .nup</button>
            </>
          )}
        </div>
      </header>

      {missingTools.length > 0 && (
        <div className="banner warn">
          Missing build tools: {missingTools.map((t) => t.name).join(', ')}. Games already built still play; see Settings.
        </div>
      )}

      {view === 'library' ? (
        <main className="library">
          {games.length === 0 && !scanning ? (
            <div className="empty">
              <div className="drop-hint">
                <div className="drop-icon">⤓</div>
                <h2>Drop a .nup file here</h2>
                <p>or add a folder to scan in Settings. Only your own dumps: nothing is downloaded.</p>
                <button className="btn" onClick={importFiles}>Choose a .nup file</button>
              </div>
            </div>
          ) : (
            <div className="grid" ref={gridRef}>
              {shown.map((g) => (
                <GameCard
                  key={g.title_id}
                  game={g}
                  build={builds[g.title_id]}
                  active={jobs.current?.title_id === g.title_id}
                  queued={jobs.queue.some((j) => j.title_id === g.title_id)}
                  onOpen={() => setSelected(g.title_id)}
                  onBuild={() => startBuild(g)}
                  onPlay={() => g.built && api.play(g.built.exe, g.built.game_dir)}
                />
              ))}
            </div>
          )}
        </main>
      ) : (
        settings && (
          <SettingsView
            settings={settings}
            tools={tools}
            onSave={async (s) => {
              setSettings(await api.saveSettings(s));
              api.checkTools().then(setTools);
              rescan();
            }}
          />
        )
      )}

      {selectedGame && (
        <GameDetail
          game={selectedGame}
          build={builds[selectedGame.title_id]}
          active={jobs.current?.title_id === selectedGame.title_id}
          queued={jobs.queue.some((j) => j.title_id === selectedGame.title_id)}
          onClose={() => setSelected(null)}
          onBuild={() => startBuild(selectedGame)}
          onCancel={async () => setJobs(await api.cancelBuild(selectedGame.title_id))}
          onPlay={() => selectedGame.built && api.play(selectedGame.built.exe, selectedGame.built.game_dir)}
          onCover={async (img) => {
            setSettings(await api.setCover(selectedGame.title_id, img));
            rescan();
          }}
        />
      )}

      {dragging && (
        <div className="drop-overlay">
          <div className="drop-card">
            <div className="drop-icon">⤓</div>
            <h2>Drop to add</h2>
            <p>.nup files join the library; folders become scan folders.</p>
          </div>
        </div>
      )}
    </div>
  );
}
