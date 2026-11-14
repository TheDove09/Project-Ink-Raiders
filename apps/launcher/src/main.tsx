import React from 'react';
import ReactDOM from 'react-dom/client';
import './styles.css';

async function start() {
  // In a plain browser (npm run dev without the app) the backend is simulated: src/devmock.ts.
  if (import.meta.env.DEV && !('__TAURI_INTERNALS__' in window)) {
    (await import('./devmock')).installMock();
  }
  const { default: App } = await import('./App');
  ReactDOM.createRoot(document.getElementById('root')!).render(
    <React.StrictMode>
      <App />
    </React.StrictMode>,
  );
}
start();
