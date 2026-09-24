# Changelog

## Unreleased

- **Frames through a pipe instead of files, where the game allows it.** The program offers the game's `startmovie` a Windows named pipe opened for each frame number (a FIFO on Linux) instead of a TGA/JPEG file in a temporary folder, so frames could go straight into the encoder with nothing written to disk; order, quality and frame rate stay the same (the fake game gives bit-identical video both ways), and the game audio takes the same route into the usual WAV. Nothing is injected into the game. The pipe is named `\??\pipe\…`: the first name, `\\?\pipe\…`, did not work in the current GMod (regular and RTX), because the Source file system reads a name that starts with two slashes as its own `//PATHID/file` syntax ("Couldn't write movie snapshot to file \\?\pipe\…" in the game console). The new name is not yet confirmed with the real game. If the game does not write into the pipe, the render restarts the game from the same point with files. The log then shows this launch's game console lines about the movie and whether the game opened the pipe at all, and the program remembers the result for this game and this pipe name (for 30 days or until the game updates). Game page → **Frame transfer**; console: `--frame-transport auto|pipe|files`.
- **RTX: no black frames while Remix compiles shaders.** During the render Remix compiles its shaders before drawing a frame (`rtx.shader.enableAsyncCompilation = False`) instead of in the background, when it draws nothing. The game may then freeze for a while at the start (minutes without a shader cache); the program tells a busy game from a hung one by its CPU use and waits (up to 20 min with RTX, 5 min without), and the demo loading timeout is longer with RTX.
- Fixed: a render to AVI with FLAC audio failed at the very end ("Could not finish the file" with no reason): FLAC's final header-only packet landed on the same timestamp as the last audio packet. Such packets are skipped now, and a muxer error always comes with its reason.
- **Translation and dubbing.** Recognized speech is translated into 25 languages (DeepL, Google, LibreTranslate, or an OpenAI-compatible model, including local Ollama and LM Studio) as `.<lang>.srt` subtitles, and can be dubbed over the game audio by a local engine (OmniVoice, installed by the program on request) or by ElevenLabs. Dubs go into extra tracks with language tags, a separate video per language, or separate audio files. Publishing templates cover YouTube multi-language audio, Shorts/TikTok/Reels, Discord/Telegram and editing. New Translation & dubbing page; console: `--translate`, `--dub`, `--publish`, `translate`, `voice-engine`, `voices`.
- **Players' own voices.** With the players' consent confirmed, a dub can use a clone of each player's voice. A voice library collects clean samples of every player (by SteamID) from each transcribed demo and can be cleared at any time.
- Service API keys are stored encrypted (Windows DPAPI) and are removed from problem reports.
- Chinese, Japanese, Korean and Hindi text is displayed using the system fonts.
- **19 more interface languages**: Russian, Belarusian, Polish, Czech, German, French, Spanish, Italian, Portuguese (Brazil and Portugal), Galician, Lithuanian, Latvian, Estonian, Finnish, Turkish, Esperanto, Hindi and Simplified Chinese. The translations are partial for now (about a third to 40% of the strings); anything missing is shown in English. The language list shows every language by its own name, and the system language is picked automatically.
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
