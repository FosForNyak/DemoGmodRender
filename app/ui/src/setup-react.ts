// The Anvil bundle is a classic script that reads React from the window (ADR-004). This module is imported
// first, so the global exists before the bundle runs.
import React from 'react';

(window as unknown as { React: typeof React }).React = React;
