import { RefObject, useEffect } from 'react';

// Controller and arrow-key navigation over the game grid: D-pad / left stick / arrows move the focus between cards, A / Enter opens
// the focused card, B / Escape closes the open panel. Uses the browser Gamepad API (WebView2), so every XInput pad works.
export function useGamepadNav(grid: RefObject<HTMLDivElement>, enabled: boolean, onBack: () => void) {
  useEffect(() => {
    const cards = () => Array.from(grid.current?.querySelectorAll<HTMLElement>('[data-nav="card"]') ?? []);
    const columns = () => {
      const c = cards();
      if (c.length < 2) return 1;
      const top = c[0].offsetTop;
      const n = c.findIndex((el) => el.offsetTop !== top);
      return n === -1 ? c.length : n;
    };
    const move = (dx: number, dy: number) => {
      const c = cards();
      if (!c.length) return;
      const i = c.indexOf(document.activeElement as HTMLElement);
      const next = i < 0 ? 0 : Math.max(0, Math.min(c.length - 1, i + dx + dy * columns()));
      c[next].focus();
      c[next].scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    };

    const onKey = (e: KeyboardEvent) => {
      if (!enabled || (e.target as HTMLElement)?.tagName === 'INPUT') return;
      const k: Record<string, [number, number]> = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };
      if (k[e.key]) {
        e.preventDefault();
        move(...k[e.key]);
      }
    };
    window.addEventListener('keydown', onKey);

    let raf = 0;
    let prev: boolean[] = [];
    let held = 0;
    const poll = () => {
      const pad = navigator.getGamepads().find((p) => p);
      if (pad) {
        const b = pad.buttons.map((x) => x.pressed);
        const pressed = (i: number) => b[i] && !prev[i];
        const ax = pad.axes[0] ?? 0;
        const ay = pad.axes[1] ?? 0;
        const dir: [number, number] = [b[14] || ax < -0.6 ? -1 : b[15] || ax > 0.6 ? 1 : 0, b[12] || ay < -0.6 ? -1 : b[13] || ay > 0.6 ? 1 : 0];
        const now = performance.now();
        if ((dir[0] || dir[1]) && enabled && now > held) {
          move(dir[0], dir[1]);
          held = now + (held === 0 ? 350 : 140);
        } else if (!dir[0] && !dir[1]) held = 0;
        if (pressed(0)) (document.activeElement as HTMLElement | null)?.click();
        if (pressed(1)) onBack();
        prev = b;
      }
      raf = requestAnimationFrame(poll);
    };
    raf = requestAnimationFrame(poll);
    return () => {
      window.removeEventListener('keydown', onKey);
      cancelAnimationFrame(raf);
    };
  }, [grid, enabled, onBack]);
}
