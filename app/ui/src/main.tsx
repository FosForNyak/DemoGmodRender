// Order matters: React must be on the window before the Anvil bundle runs (ADR-004).
import './setup-react';
import './vendor/anvil/bundle.js';
import './vendor/anvil/tokens.css';
import './vendor/anvil/bundle.css';
import './app.css';

import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
