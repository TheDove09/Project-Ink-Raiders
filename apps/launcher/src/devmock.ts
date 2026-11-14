// Development only: when the UI runs in a plain browser (npm run dev, no Tauri), this stands in for the Rust backend with sample
// games and a simulated build, so the interface can be designed and checked without the app. Never loaded inside the app.
type Handler = (payload: unknown) => void;

export function installMock() {
  const listeners: Record<string, Map<number, Handler>> = {};
  let nextId = 1;
  const callbacks = new Map<number, (v: unknown) => void>();
  const emit = (event: string, payload: unknown) => listeners[event]?.forEach((h) => h({ event, payload, id: 0 }));

  const games = [
    { title_id: '0400C3F00006E000', title: 'Mario Kart World', version: '1.8.0', nup_path: 'C:\\Users\\you\\Downloads\\Mario Kart World + Update 1.8.0.nup',
      nup_size: 23182528023, nup_external: false, nup_error: null, profile: 'x0400c3f00006e000', support: 'experimental', built: null, cover: null },
    { title_id: '0400380020056000', title: 'Splatoon Raiders', version: '1.1.0', nup_path: 'F:\\Switch2\\Splatoon Raiders + Update 1.1.0.nup',
      nup_size: 13897347051, nup_external: true, nup_error: null, profile: 'sra', support: 'supported',
      built: { game_dir: 'D:\\Games\\Recompiled\\InkRaiders', exe: 'D:\\Games\\Recompiled\\InkRaiders\\InkRaiders.exe', built: '2026-11-14T00:49:35', version: '1.1.0' }, cover: null },
  ];
  const settings = { scan_dirs: ['C:\\Users\\you\\Downloads', 'F:\\Switch2'], extra_nups: [], work_dir: 'C:\\RecompWork', out_dir: 'D:\\Games\\Recompiled',
    stage_mode: 'auto', keep_temp: false, jobs: 12, repo_dir: 'C:\\src\\recomp', python: 'C:\\tools\\python.exe', covers: {} };
  const jobs = { current: null as null | { title_id: string; nup_path: string }, queue: [] as unknown[] };

  const simulate = (title_id: string) => {
    const stages = [['stage', 'Copying to SSD', 0], ['exefs', 'Unpacking code', 6], ['profile', 'Creating game profile', 0], ['lift', 'Translating ARM64', 238],
      ['link', 'Linking native exe', 470], ['romfs', 'Unpacking assets', 206202], ['package', 'Finishing', 0]] as const;
    let t = 0;
    stages.slice(0, 5).forEach(([stage, label, total], index) => {   // the simulated build fails while linking
      setTimeout(() => emit('pipeline', { title_id, event: { event: 'stage', stage, label, index, count: 7 } }), (t += 600));
      for (let k = 1; k <= 8 && total; k++) {
        setTimeout(() => {
          emit('pipeline', { title_id, event: { event: 'progress', stage, done: Math.round((total * k) / 8), total } });
          emit('pipeline', { title_id, event: { event: 'log', stage, line: `[${Math.round((total * k) / 8)}/${total}] ${label.toLowerCase()} ...` } });
        }, (t += 250));
      }
    });
    setTimeout(() => {
      emit('pipeline', { title_id, event: { event: 'done', ok: false, stage: 'link', error: 'exit code 1: lld-link: error: undefined symbol: nn::example (simulated failure)' } });
      jobs.current = null;
      emit('jobs', jobs);
    }, (t += 600));
  };

  const commands: Record<string, (a: Record<string, unknown>) => unknown> = {
    get_settings: () => settings,
    save_settings: (a) => Object.assign(settings, a.new),
    scan_library: () => games,
    add_paths: () => settings,
    check_tools: () => [{ name: 'Pipeline', ok: true, detail: 'C:\\src\\recomp\\tools\\pipeline\\recomp_pipeline.py' }, { name: 'Node.js', ok: true, detail: 'node.exe' }],
    start_build: (a) => {
      jobs.current = { title_id: a.titleId as string, nup_path: a.nupPath as string };
      setTimeout(() => simulate(a.titleId as string), 100);
      return jobs;
    },
    cancel_build: () => ((jobs.current = null), jobs),
    job_status: () => jobs,
    play: () => null,
    open_path: () => null,
    set_cover: () => settings,
    read_build_log: () => 'No build log yet.',
    get_game_settings: () => ({ window_mode: 'windowed', vsync: false, fps_limit: '60', render_scale: '100', operation_mode: 'handheld',
      save_dir: '', save_dir_default: 'C:\\Users\\you\\AppData\\Roaming\\SplatoonRaidersNative\\save' }),
    save_game_settings: (a) => ({ ...(a.settings as object) }),
    'plugin:event|listen': (a) => {
      const id = nextId++;
      (listeners[a.event as string] ??= new Map()).set(id, callbacks.get(a.handler as number) as Handler);
      return id;
    },
    'plugin:event|unlisten': (a) => listeners[a.event as string]?.delete(a.eventId as number),
  };

  (window as unknown as Record<string, unknown>).__TAURI_EVENT_PLUGIN_INTERNALS__ = {
    unregisterListener: (event: string, id: number) => listeners[event]?.delete(id),
  };
  (window as unknown as Record<string, unknown>).__TAURI_INTERNALS__ = {
    invoke: async (cmd: string, args: Record<string, unknown> = {}) => {
      const f = commands[cmd];
      if (!f) return null;
      return f(args);
    },
    transformCallback: (cb: (v: unknown) => void) => {
      const id = nextId++;
      callbacks.set(id, cb);
      return id;
    },
    convertFileSrc: (p: string) => p,
    metadata: { currentWindow: { label: 'main' }, currentWebview: { windowLabel: 'main', label: 'main' } },
  };
}
