# GMod Demo Render

[![build](https://github.com/FosForNyak/DemoGmodRender/actions/workflows/build.yml/badge.svg)](https://github.com/FosForNyak/DemoGmodRender/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/FosForNyak/DemoGmodRender)](https://github.com/FosForNyak/DemoGmodRender/releases/latest)
[![license](https://img.shields.io/github/license/FosForNyak/DemoGmodRender)](LICENSE)

Turn **Garry's Mod** demo recordings (`.dem`) into video files. Pick any resolution, frame rate,
bit depth and codec. The video carries the game audio, the players' voice chat decoded straight
from the demo and, if you want, your own microphone. Encoding runs on every CPU core or on the
GPU (NVIDIA NVENC, AMD AMF, Intel Quick Sync).

![Main window](docs/screenshot.png)

## Download

Get the latest `GModDemoRender-<version>-win64.zip` from
[Releases](https://github.com/FosForNyak/DemoGmodRender/releases/latest), unpack it anywhere and
run `gmdr.exe`. Nothing needs to be installed.

**You need:**

- Windows 10 or 11, 64-bit;
- Garry's Mod installed through Steam (the regular or the x86-64 branch), and Steam running;
- Microsoft Visual C++ 2015–2022 Redistributable (x64). Most Steam games already install it.

The interface is in English or Ukrainian. By default it follows your Windows language, and you
can switch it in **Tools → Мова / Language**.

## Features

**Rendering**

- Any resolution (480p up to 8K, vertical formats too) and any frame rate, even fractional
  (`59.94`, `60000/1001`). The game advances by a fixed time step, so a slow PC renders 4K at
  240 fps correctly. It just takes longer.
- Real **motion blur**. The game renders N sub-frames per video frame and the program blends
  them with a configurable shutter angle. The blending keeps 16 bits of precision.
- 8/10/12-bit output, 4:2:0 / 4:2:2 / 4:4:4 chroma.
- CPU codecs: H.264, H.265, AV1 (SVT-AV1, libaom), VP9, ProRes, DNxHR, FFV1, UT Video and PNG.
  GPU codecs: H.264, HEVC and AV1 via NVENC, AMF, Quick Sync, Vulkan and Direct3D 12. At
  startup the program tests which GPU encoders actually work on your card and shows only those.
- Output formats: MP4, MKV, MOV, WebM, AVI and NUT, or PNG/TIFF/BMP/JPEG image sequences.
- One-click presets: YouTube 1080p60 and 4K60 (10-bit), ProRes 422 HQ for editing, Discord
  10/50/500 MB (the bitrate is fitted to the file size) and lossless archive.
- **Slow motion and fast forward**, ×0.25 to ×8. Slow motion is honest: the game renders
  every in-between frame, nothing is interpolated. Audio is stretched without changing pitch.
- **Several versions from one render.** The same frames are also encoded as a Discord copy
  (≤ 10 MB, 720p), a light 480p copy, a vertical 9:16 video for Shorts/TikTok/Reels and a
  ProRes master. After the render you can also get a thumbnail, a GIF and an animated WebP.
- **Parallel rendering.** Two to four game instances render parts of the fragment at the same
  time. The parts are joined without re-encoding, frame-exact.
- **GMod RTX** support through [RTXLauncher](https://github.com/Xenthio/RTXLauncher). During
  the render the program sets Remix to video-friendly settings (DLAA, no frame generation) and
  restores your config afterwards.

**Audio and voice**

- Players' voices are decoded from the demo (Steam Voice / Opus) and placed tick-accurately.
  Pick whose voices go in, set each player's volume, mute or solo them, and preview a voice
  right in the app.
- Audio processing:
  - level every player to -18 LUFS;
  - RNNoise neural noise suppression with a gate between phrases;
  - duck the game audio under voices;
  - final loudness per EBU R128 (-14 LUFS for YouTube).
- Your own microphone from a separate recording (OBS, Audacity, Discord), with an offset.
- Subtitles: "who is speaking" `.srt`, the game chat as subtitles, and **speech recognition**.
  Voice chat becomes text locally with whisper.cpp, offline.
- "Who is speaking" labels drawn right on the video, like the in-game voice indicator.
- **Editing package**: separate 24-bit WAVs (game, each player, microphone) and an XML
  project that Premiere Pro and DaVinci Resolve import with every track and marker in place.

**Workflow**

- A Premiere Pro-style workspace:
  - export settings;
  - a Program monitor with a live preview while rendering;
  - a timeline with a track per player, mute/solo, playhead and markers;
  - voice, library, chat, queue and log panels.
- **Fragments and markers** with I / O / M, like in Premiere. Markers become chapters in
  MP4/MOV/MKV and a `.chapters.txt` with timestamps for a YouTube description.
- **Chat and events** from the demo: messages, joins and leaves (with kick/ban reasons),
  searchable and shown on the timeline. Chat addons that send text through net messages
  (e.g. `customchat`) are understood too.
- **Watch in game**: play the demo in GMod from any point in real time. In the game, F9 sets
  the fragment start, F11 the end and F6 adds a marker. The changes show up in the program
  right away.
- **Render queue**: queue demos with their own settings. The game launches only once for the
  whole queue.
- **Demo library**: every demo in the game folder and your own folders, with map, length,
  date and size, searchable and sortable.
- **Test run** ("Test 3 s"): three seconds with a step-by-step check (game started, driver
  answers, demo plays, frames and audio arrive, encoder works) and an estimate of the time and
  file size of the full render.
- Windows notifications, minimize to tray, and "then shut down / sleep" for overnight renders.

**Reliability**

- The game runs in the background, off screen and without focus, so you can keep working.
  Windows power throttling and the unfocused FPS cap are turned off for it.
- If GMod crashes or hangs, the program restarts it from the same point and keeps writing the
  same file, seamlessly.
- If the program or the PC crashes, the next start offers to finish the interrupted render.
  At most a few seconds are lost.
- MP4/MOV is written in fragments, so the file stays playable after any crash. The game pauses
  when the encoder falls behind or disk space runs low. After the render the output is verified.
- **Help → Collect a problem report** makes one ZIP with the log, settings, system info, the
  GMod console and a crash dump. Your profile path is masked, and nothing is sent anywhere.

## Quick start

1. Make sure Steam is running and Garry's Mod is closed.
2. Run `gmdr.exe` and drop a `.dem` file onto the window, or click **Open demo...**.
   Demos recorded with `record` are in `…\steamapps\common\GarrysMod\garrysmod\` or its
   `demos` subfolder. The **Library** tab lists them all.
3. Choose a preset, or set the resolution, FPS and codec on the **Video** tab.
4. Optionally mark a fragment on the timeline: click the ruler, then press **I** and **O**.
5. Click **Test 3 s** to check every step and see how long the render will take.
6. Click **Start render**. The program finds GMod, launches it in the background and records
   the demo.

The video appears next to the demo. The **File** line under the settings shows the path: click
the file name to pick another one, or the pencil to type a path.

## The window

| Panel | What it holds |
|---|---|
| Top left | Export settings: **Video**, **Audio & voice**, **Game** and **Fragment** tabs, with the output file below |
| Top right | **Program** monitor: before a render it shows the demo's map, server, recorder and length; during a render, a live preview and the checks. Transport buttons sit under it |
| Bottom left | **Voices** (volume, preview), **Library**, **Chat**, **Queue**, **Log** |
| Bottom right | **Timeline**: a track per player with M (mute) and S (solo), a chat track, the ruler, the playhead and markers |

Drag the gutters between panels to resize them. **Window → Reset panel layout** restores the
default. Blue values such as the resolution, speed or volume can be dragged left and right, or
clicked to type a number.

| Key | Action |
|---|---|
| **I** / **O** | Set the fragment start / end at the playhead |
| **M** | Add a marker at the playhead (or Ctrl+click the timeline) |
| **Shift+I** / **Shift+O** | Go to the fragment start / end |
| **Home** / **End** | Go to the start / end of the demo |
| **Ctrl+O** | Open a demo |

## How it works

A `.dem` file contains no pictures. It is a recording of the game's network packets: where
entities are, which sounds played, what players said on voice chat. Only the GMod engine, with
your maps, models, addons and Lua, can draw that correctly. So the program does not re-draw
the game; it drives it.

1. **Demo analysis.** The program parses the `HL2DEMO` format itself (protocol 24 plus
   GMod-specific messages, including GMod 2026 servers). It reads the map, length, tick rate
   and player names, and decodes the voice chat.
2. **Fixed time step.** GMod starts with `host_framerate` set to FPS × sub-frames. Every
   game frame is then exactly 1/FPS of demo time, however fast or slow the PC is.
3. **Driver in the GMod menu.** A small Lua script starts the demo and waits until it really
   plays. It turns `startmovie` on at the right tick, off at the end, and closes the game.
4. **Frame pipeline.** The game writes each frame (TGA or JPEG) and the audio (WAV) to a
   temporary folder. The program picks the frames up and decodes them in parallel. Then it
   blends sub-frames for motion blur, scales and converts color (BT.709) on all cores, and
   encodes with FFmpeg on its own thread. Finished files are deleted.
5. **Audio.** The game audio, the decoded voices and your microphone are mixed and encoded in
   sync with the video.

The internals are described in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (in Ukrainian).

## Voices and your microphone

Other players' voices are stored in the demo, and the program decodes them cleanly and exactly
in time. While rendering, in-game voice is muted (`voice_scale 0`) so voices are not doubled.
**Save voices to files...** exports each player's voice for the fragment or the whole demo. All
files start at the same moment, so they line up at the start of the tracks in an editor.

**Your own voice** is not sent back to you by the server, so a normal demo does not have it.
There are two ways around that:

- type `voice_loopback 1` in the GMod console before recording the demo. Your voice ends up in
  the demo and is marked "(you)";
- or record your microphone separately and add the file under **Audio & voice → Own
  microphone**. The offset sets the second of video where the file starts; it can be negative.

## Command line

`gmdr-cli.exe` does the same work without a window, which suits batch jobs. Its messages are in
Ukrainian by default; add `--lang en` or set `GMDR_LANG=en` for English.

```
gmdr-cli info demo.dem [--chat]
gmdr-cli render demo.dem -o video.mp4 --size 2560x1440 --fps 60 --codec hevc_nvenc --bit-depth 10
gmdr-cli render demo.dem -o film.mov --codec prores_ks --motion-blur 16 --shutter 180 --acodec pcm_s24le
gmdr-cli render demo.dem -o clip.mp4 --start 30 --end 75 --hide-hud --mic mic.wav --mic-offset -1.5
gmdr-cli render demo.dem --start 10:00 --end 11:00 --test-run
gmdr-cli render demo.dem -o discord.mp4 --start 1:00 --end 1:30 --size 1280x720 --target-size 10
gmdr-cli render demo.dem -o yt.mp4 --level-voices --denoise --duck-game --loudness -14
gmdr-cli render demo.dem -o clip.mkv --markers "5:30=Fight; 7:10=Final" --chat-srt
gmdr-cli render demo.dem -o fight.mp4 --also discord,vertical,thumb,gif
gmdr-cli render demo.dem -o slowmo.mp4 --start 2:10 --end 2:14 --speed 0.25
gmdr-cli render demo.dem -o long.mp4 --start 1:00:00 --end 1:20:00 --motion-blur 8 --parallel 2
gmdr-cli render a.dem b.dem c.dem -o videos\
gmdr-cli queue overnight.txt --then shutdown
gmdr-cli transcribe demo.dem --language en -o speech.srt
gmdr-cli watch demo.dem --from 12:30
gmdr-cli voice demo.dem -o voices\ --start 1:00:00 --end 1:05:00
gmdr-cli resume
gmdr-cli encoders --test
gmdr-cli report -o report.zip
```

A queue file has one demo per line with its own options. Empty lines and lines starting with
`#` are skipped:

```
"C:\demos\match.dem" --start 5:00 --end 7:30 -o "D:\video\fight.mp4"
"C:\demos\match.dem" --start 12:00 --end 13:00 --hide-hud
C:\demos\other.dem --size 2560x1440
```

Run `gmdr-cli --help` for every option.

## What the program changes

- It adds `garrysmod\lua\menu\gmdr_driver.lua` and one line at the end of
  `garrysmod\lua\menu\menu.lua`. The script does nothing until the program hands it a job. A
  backup is kept as `menu.lua.gmdr_backup`. Remove it with **Tools → Remove the driver from
  GMod**, or verify the game files in Steam.
- During a render or a watch session it creates temporary files in `garrysmod\cfg\gmdr`,
  `garrysmod\data\gmdr` and `garrysmod\gmdr_tmp`. They are deleted afterwards.
- Console variables changed for the render (`fps_max`, `mat_vsync`, `voice_scale`,
  `cl_drawhud`...) are restored. `config.cfg` is restored from a backup even if the game was
  closed mid-render.
- Garry's Mod is muted in the Windows volume mixer during the render (the video still has
  sound). Windows is kept awake until the render ends.
- The `.dem` file association is registered only if you turn on **Tools → Open .dem files with
  a double click**. It goes under `HKCU` for your account only, and the same menu item removes
  it.

The program injects nothing into the game process and does not touch anti-cheat. It uses only
standard engine features (`startmovie`, `host_framerate`, the menu Lua state) and ordinary
Windows window management.

Settings are saved in `gmdr_settings.json` next to the program, and the log in `gmdr_log.txt`.
If the program folder is not writable (for example `Program Files`), they go to
`%LOCALAPPDATA%\GModDemoRender` instead.

## Troubleshooting

If the cause of a problem is unclear, use **Help → Collect a problem report...** and attach the
ZIP. You can look inside before sending it.

- **"Garry's Mod is already running".** Close the game; the program launches it itself with
  the right options.
- **The game closes at once, or "the driver does not respond".** Make sure Steam is running.
  If GMod was just updated and replaced `menu.lua`, use **Tools → Install the driver into GMod**.
- **"The demo did not start".** The demo was recorded by another GMod version, or the server
  used maps or addons you don't have. Check that the demo plays in the game itself
  (`playdemo demos/name`). Joining that server once usually downloads the content.
- **A step of the test run fails.** The step shows a hint and the last lines of the game
  console.
- **The background game stops producing frames.** The program moves the window back on screen,
  behind other windows, and logs it. If it happens every time, choose **Game → Behind other
  windows**. Don't minimize the game: a minimized game does not draw.
- **The game menu flashes into a frame.** Demos also record the player pressing Esc. The
  program hides the menu at once, but a single frame can sometimes slip into the video.
- **The video size differs from the settings.** The game cannot open a window larger than the
  monitor; frames are then scaled to the requested size, with a warning in the log.
- **You hear sped-up audio from the speakers.** That's normal: the game plays sound at render
  pace. The audio in the video is correct.
- **Garry's Mod stays muted.** This happens if the game or the PC crashed mid-render. Unmute
  it in the Windows volume mixer, or just start the next render.
- **Low disk space.** Below 1 GiB free the game pauses until space is freed. Lower **Frame
  queue on disk** on the Game tab or switch the frame format to JPEG.
- **The program crashed.** A `gmdr_crash_<date>.dmp` appears next to it. Please attach it
  together with `gmdr_log.txt` to your bug report.

## Building from source

You need **Visual Studio 2022 (17.8+) or 2026** with the *Desktop development with C++*
workload, which already includes CMake.

The simplest way is to run `build.bat`. It finds CMake, downloads the FFmpeg development
libraries into `third_party\ffmpeg` if they are missing, and builds `build\Release\gmdr.exe`
and `gmdr-cli.exe`. The FFmpeg DLLs are copied next to them.

In Visual Studio, use **File → Open → Folder...** and pick the repository. Choose the *Windows
x64 Release* configuration and the `gmdr.exe` target. If `third_party\ffmpeg` is missing, fetch
it first:

```
powershell -ExecutionPolicy Bypass -File scripts\get_ffmpeg.ps1
```

`scripts\get_whisper.ps1` puts `whisper-cli` into `third_party\whisper` for speech recognition.
The recognition model itself is downloaded by the program when you first need it.

Linux builds are for development: install `libavcodec-dev libavformat-dev libavfilter-dev
libswscale-dev libswresample-dev libglfw3-dev`, then run `cmake --preset linux-release &&
cmake --build --preset linux-release`.

**Tests:**

- Configure with `-DGMDR_BUILD_TESTS=ON`.
- Generate synthetic demos with `python tests/tools/make_test_demo.py out.dem truth.json`
  (needs `ffmpeg` with libopus), then run `gmdr-tests <folder with demos>`.
- Sanitizers: `-DGMDR_SANITIZE=address`, or `address,undefined` with GCC/Clang.
- Fuzzer: `-DGMDR_BUILD_FUZZERS=ON`.

CI builds and tests every push on Windows (MSVC, with end-to-end renders through a fake game)
and Linux (GCC, ASan/UBSan), and fuzzes the demo parser.

UI strings are Ukrainian in the source, with English translations in
`src/core/util/i18n_en.inc`. After adding strings, run `python scripts/i18n.py check`.

```
src/core/demo/     .dem parser, GMod network messages, string tables, chat and events
src/core/voice/    Steam Voice packets, Opus decoding, voice timeline
src/core/audio/    WAV, mixer, FFmpeg filters, loudness, gate, voice preview
src/core/frames/   frame buffers, TGA/JPEG, live frame sequence reader, motion blur
src/core/media/    FFmpeg video/audio encoders and muxer
src/core/game/     GMod discovery, Lua driver, process and window control, mixer mute
src/core/speech/   speech recognition via whisper.cpp
src/core/render/   encode pipeline, background jobs (analysis, render, queue, resume), subtitles
src/core/util/     logging, JSON, VDF, thread pool, i18n, crash reports, update check
src/gui/           Dear ImGui interface (Win32 + Direct3D 11; GLFW on Linux)
src/cli/           command-line version
tests/             unit tests, synthetic demo generator, fake game, fuzzer, A/V comparison
```

## License

GMod Demo Render is released under the [MIT License](LICENSE).

Third-party components:

- [Dear ImGui](https://github.com/ocornut/imgui) (MIT).
- [FFmpeg](https://ffmpeg.org). Releases ship the BtbN "gpl" build, which includes x264 and
  x265, so the FFmpeg DLLs in the release archive are covered by the GPL. Their license is
  included in the archive.
- [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (MIT).
- The RNNoise model "leavened-quisling" from
  [rnnoise-models](https://github.com/GregorR/rnnoise-models). Its author states it is not
  subject to copyright; see `third_party/rnnoise-models/README.md`.

Garry's Mod is a trademark of Facepunch Studios. This project is not affiliated with Facepunch
Studios or Valve.
