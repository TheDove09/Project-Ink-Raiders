import { convertFileSrc } from '@tauri-apps/api/core';
import { GameEntry } from '../api';

// A stable colour pair per title: the placeholder cover until cover art is set (automatic art comes in a later phase).
function hue(s: string): number {
  let h = 0;
  for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
  return h % 360;
}

export default function Cover({ game, large }: { game: GameEntry; large?: boolean }) {
  if (game.cover) {
    return <img className={large ? 'cover cover-large' : 'cover'} src={convertFileSrc(game.cover)} alt="" draggable={false} />;
  }
  const h = hue(game.title_id + game.title);
  const words = game.title.split(/\s+/).filter(Boolean);
  const initials = words.slice(0, 3).map((w) => w[0]).join('').toUpperCase();
  return (
    <div
      className={large ? 'cover cover-large placeholder' : 'cover placeholder'}
      style={{ background: `linear-gradient(145deg, hsl(${h} 55% 32%), hsl(${(h + 50) % 360} 60% 16%))` }}
    >
      <span className="cover-initials">{initials}</span>
      <span className="cover-title">{game.title}</span>
    </div>
  );
}
