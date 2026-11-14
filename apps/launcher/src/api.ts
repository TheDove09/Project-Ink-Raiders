import { invoke } from '@tauri-apps/api/core';

export interface BuiltGame { game_dir: string; exe: string; built: string; version: string }
export interface GameEntry {
  title_id: string; title: string; version: string | null;
  nup_path: string | null; nup_size: number; nup_external: boolean; nup_error: string | null;
  profile: string; support: 'supported' | 'experimental' | 'unreadable';
  built: BuiltGame | null; cover: string | null;
}
export interface Settings {
  scan_dirs: string[]; extra_nups: string[]; work_dir: string; out_dir: string;
  stage_mode: 'auto' | 'always' | 'never'; keep_temp: boolean; jobs: number;
  repo_dir: string; python: string; covers: Record<string, string>;
}
export interface Job { title_id: string; nup_path: string }
export interface JobsView { current: Job | null; queue: Job[] }
export interface ToolCheck { name: string; ok: boolean; detail: string }
export interface GameSettings {
  window_mode: string; vsync: boolean; fps_limit: string; render_scale: string;
  operation_mode: string; save_dir: string; save_dir_default: string;
}

export type PipelineEvent =
  | { event: 'stage'; stage: string; label: string; index: number; count: number }
  | { event: 'progress'; stage: string; done: number; total: number }
  | { event: 'log'; stage: string | null; line: string }
  | { event: 'profile'; profile: Record<string, unknown> }
  | { event: 'done'; ok: boolean; stage?: string | null; error?: string; game_dir?: string; exe?: string; cancelled?: boolean };

export const STAGES: { id: string; label: string }[] = [
  { id: 'stage', label: 'Copying to SSD' },
  { id: 'exefs', label: 'Unpacking code' },
  { id: 'profile', label: 'Game profile' },
  { id: 'lift', label: 'Translating ARM64' },
  { id: 'link', label: 'Linking native exe' },
  { id: 'romfs', label: 'Unpacking assets' },
  { id: 'package', label: 'Finishing' },
];

export const api = {
  getSettings: () => invoke<Settings>('get_settings'),
  saveSettings: (s: Settings) => invoke<Settings>('save_settings', { new: s }),
  scan: () => invoke<GameEntry[]>('scan_library'),
  addPaths: (paths: string[]) => invoke<Settings>('add_paths', { paths }),
  checkTools: () => invoke<ToolCheck[]>('check_tools'),
  startBuild: (titleId: string, nupPath: string) => invoke<JobsView>('start_build', { titleId, nupPath }),
  cancelBuild: (titleId: string) => invoke<JobsView>('cancel_build', { titleId }),
  jobStatus: () => invoke<JobsView>('job_status'),
  play: (exe: string, gameDir: string) => invoke<void>('play', { exe, gameDir }),
  openPath: (path: string) => invoke<void>('open_path', { path }),
  setCover: (titleId: string, image: string | null) => invoke<Settings>('set_cover', { titleId, image }),
  readBuildLog: (profile: string) => invoke<string>('read_build_log', { profile }),
  getGameSettings: (gameDir: string) => invoke<GameSettings>('get_game_settings', { gameDir }),
  saveGameSettings: (gameDir: string, settings: GameSettings) => invoke<GameSettings>('save_game_settings', { gameDir, settings }),
};

export function formatSize(bytes: number): string {
  if (bytes >= 1 << 30) return (bytes / (1 << 30)).toFixed(1) + ' GB';
  if (bytes >= 1 << 20) return (bytes / (1 << 20)).toFixed(0) + ' MB';
  return bytes + ' B';
}
