# Changelog

## Unreleased

- **Translation and dubbing.** Recognized speech is translated into 25 languages (DeepL, Google, LibreTranslate, or an OpenAI-compatible model, including local Ollama and LM Studio) as `.<lang>.srt` subtitles, and can be dubbed over the game audio by a local engine (OmniVoice, installed by the program on request) or by ElevenLabs. Dubs go into extra tracks with language tags, a separate video per language, or separate audio files. Publishing templates cover YouTube multi-language audio, Shorts/TikTok/Reels, Discord/Telegram and editing. New Translation & dubbing page; console: `--translate`, `--dub`, `--publish`, `translate`, `voice-engine`, `voices`.
- **Players' own voices.** With the players' consent confirmed, a dub can use a clone of each player's voice. A voice library collects clean samples of every player (by SteamID) from each transcribed demo and can be cleared at any time.
- Service API keys are stored encrypted (Windows DPAPI) and are removed from problem reports.
- Chinese, Japanese, Korean and Hindi text is displayed using the system fonts.
- Fixed: a normal render no longer logs "Test run: extra versions are not encoded".
- **New interface.** Side navigation with pages instead of Adobe-style panels: Overview shows everything about the demo and the future render on one screen (duration, players, voice chat, server, protocol, warnings, output file, versions, subtitles). Settings are grouped into cards on the Video, Audio & voices and Game pages; the monitor and the timeline sit next to them and can be hidden.
- **Standard and Advanced modes.** Standard shows only the main settings; Advanced adds every codec, game and audio option and the Fragment & markers, Chat & speech and Log pages.
- **Own look**: a new logo, dark and light themes (or same as Windows), seven accent colors, 80–200% interface scale and a compact density — on the new Settings page. The window frame follows the theme on Windows 11.
- The demo name in the top bar opens recent demos; Ctrl+1…9 switch pages, Ctrl+B collapses the sidebar, Ctrl+, opens Settings.
- The Game tab has a **Standard / RTX** switch. Each mode keeps its own game folder: the regular game is found through Steam, the RTX copy through RTXLauncher. A GMod RTX folder entered as the regular game folder is moved to RTX mode automatically. Console: `--rtx-dir FOLDER`.

## 1.0.0 — 2026-09-24

The first public release. It brings together everything built during Beta 1 and Beta 2 and adds a new interface.

**Download** `GModDemoRender-1.0.0-win64.zip`, unpack it anywhere and run `gmdr.exe`. It needs Windows 10/11 x64, Garry's Mod from Steam, and the Visual C++ 2015–2022 Redistributable (x64), which most Steam games already install.

### Highlights

- **New Premiere Pro-style workspace**:
  - export settings, a Program monitor with a live preview, and voice/library/chat/queue/log panels;
  - a timeline with a track per player, mute/solo, a playhead and markers;
  - panels resize by dragging the gutters between them;
  - I / O / M set the fragment and markers, as in Premiere.
- **Rendering**:
  - any resolution and frame rate, real motion blur, 8/10/12-bit output;
  - CPU codecs and GPU encoding (NVENC, AMF, Quick Sync), presets for YouTube, Discord, editing and archiving;
  - slow motion and fast forward;
  - extra versions from one render: Discord, 480p, vertical 9:16, a ProRes master, a thumbnail, GIF and WebP;
  - parallel rendering with 2–4 game instances;
  - GMod RTX support.
- **Audio and voice**:
  - players' voices decoded from the demo, with per-player volume, mute/solo and preview;
  - voice leveling, RNNoise noise suppression, ducking and EBU R128 loudness;
  - your own microphone track;
  - an editing package of separate WAVs and an XML project for Premiere Pro and DaVinci Resolve.
- **Subtitles and text**: "who is speaking" subtitles and on-screen labels, chat subtitles, and offline speech recognition with whisper.cpp.
- **Workflow**:
  - chat and events from the demo;
  - markers that become video chapters;
  - watching the demo in the game with hotkeys that set the fragment;
  - a render queue that launches the game once;
  - a demo library;
  - a 3-second test run with a time and size estimate;
  - notifications, tray, and "then shut down / sleep".
- **Reliability**:
  - the game renders in the background;
  - renders survive game crashes and hangs, and a render cut short by an app or PC crash can be finished later;
  - crash-safe MP4, and the output is verified after the render;
  - a problem report ZIP for bug reports.
- English and Ukrainian interface. The update check uses GitHub Releases and runs only when you ask.
