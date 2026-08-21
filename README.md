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

The plugin captures OBS mix 1 as 48 kHz stereo float audio and sends it to a configurable Windows playback device. On first run it automatically selects the first active device whose name contains `CABLE Input`. The WASAPI worker writes silence when capture data is temporarily unavailable and automatically reconnects to the selected endpoint after device removal or other WASAPI failures. Retry delays increase from 1 second to a maximum of 10 seconds.

## Windows build

The repository includes `scripts/build-windows.ps1`. It uses the official OBS plugin template build infrastructure in a temporary `.build` workspace, so you do not need to manually prepare a libobs SDK.

Requirements:

- Windows 10/11 x64
- Visual Studio 2026 with **Desktop development with C++**
- Windows 11 SDK 10.0.26100.0
- CMake available in `PATH`
- Git available in `PATH`
- PowerShell 7.2+
- VB-CABLE installed with an active playback endpoint containing `CABLE Input`

From PowerShell 7 at the repository root:

```powershell
pwsh -File .\scripts\build-windows.ps1
```

For a Release build:

```powershell
pwsh -File .\scripts\build-windows.ps1 -Configuration Release
```

To build, validate, and create a distributable ZIP:

```powershell
pwsh -File .\scripts\package-windows.ps1
```

The archive is written to `dist/obs-virtual-audio-<version>-windows-x64.zip`. Pass `-SkipBuild` to package an existing build of the selected configuration.

The first build downloads the official OBS plugin template and its build dependencies. The build also runs the audio ring buffer unit tests. Build output is copied to:

```text
release/RelWithDebInfo/
```

The plugin package should be under:

```text
release/RelWithDebInfo/obs-virtual-audio/
```

## Smoke test in OBS

After building, copy the generated plugin directory to the Windows third-party plugin location:

```text
C:\ProgramData\obs-studio\plugins\obs-virtual-audio\
```

The installed DLL should therefore be at:

```text
C:\ProgramData\obs-studio\plugins\obs-virtual-audio\bin\64bit\obs-virtual-audio.dll
```

Then start OBS Studio.

Use **Tools -> OBS Virtual Audio** to select or change the target playback device. The selection is stored by device ID and restored the next time OBS starts. The dialog reports the live output state as connecting, connected, reconnecting, or disconnected.

Open **Help -> Log Files -> View Current Log** and search for:

```text
[obs-virtual-audio] loaded (version 0.1.0)
[obs-virtual-audio] audio capture started: mix=0, 48000 Hz, stereo float
[obs-virtual-audio] WASAPI output started: CABLE Input ..., 48000 Hz, stereo float
```

If those messages appear and OBS remains stable while audio sources are active, select the corresponding `CABLE Output` recording endpoint in the destination application and verify that its meter receives the OBS mix.

## Next milestone

Add automated tests for renderer state transitions and device-loss recovery.
