# OBS Virtual Audio

A Windows OBS Studio plugin that forwards an OBS audio mix to a selected playback endpoint such as VB-Audio Virtual Cable, so applications like TikTok Studio can consume the routed audio from the corresponding virtual recording endpoint.

## MVP goal

- Windows x64
- OBS Studio plugin
- Capture OBS audio mix/track 1
- Forward PCM audio to a WASAPI playback device
- Start with VB-CABLE as the primary target
- Keep audio I/O off the OBS audio callback thread via buffering/worker processing

## Planned signal flow

```text
OBS sources
   -> OBS audio mixer (Track 1)
   -> raw audio callback
   -> ring buffer
   -> WASAPI render worker
   -> CABLE Input (VB-Audio Virtual Cable)
   -> CABLE Output
   -> TikTok Studio
```

## Current status

The plugin currently captures OBS mix 1 as 48 kHz stereo float audio and writes it into a lock-free SPSC ring buffer. WASAPI output has not been implemented yet.

## Windows build

The repository includes `scripts/build-windows.ps1`. It uses the official OBS plugin template build infrastructure in a temporary `.build` workspace, so you do not need to manually prepare a libobs SDK.

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 with **Desktop development with C++**
- CMake available in `PATH`
- Git available in `PATH`
- PowerShell 7.2+

From PowerShell 7 at the repository root:

```powershell
pwsh -File .\scripts\build-windows.ps1
```

For a Release build:

```powershell
pwsh -File .\scripts\build-windows.ps1 -Configuration Release
```

The first build downloads the official OBS plugin template and its build dependencies. Build output is copied to:

```text
release/RelWithDebInfo/
```

The plugin DLL should be under:

```text
release/RelWithDebInfo/obs-plugins/64bit/obs-virtual-audio.dll
```

## Smoke test in OBS

After building, copy the generated plugin layout into your OBS Studio installation (or copy the DLL and data folders to their matching OBS plugin locations), then start OBS Studio.

Open **Help -> Log Files -> View Current Log** and search for:

```text
[obs-virtual-audio] loaded (version 0.1.0)
[obs-virtual-audio] audio capture started: mix=0, 48000 Hz, stereo float
```

If those messages appear and OBS remains stable while audio sources are active, the capture-stage smoke test is successful.

## Next milestone

Implement a WASAPI render worker that reads the ring buffer and sends the captured audio to a playback endpoint such as `CABLE Input (VB-Audio Virtual Cable)`.
