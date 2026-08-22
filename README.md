# OBS Virtual Audio

OBS Virtual Audio is a Windows OBS Studio plugin that sends OBS audio Track 1
to a playback device such as VB-CABLE. Applications such as TikTok LIVE Studio
can then receive the complete OBS mix through the corresponding virtual
recording device.

```text
OBS audio sources
   -> OBS Track 1
   -> OBS Virtual Audio
   -> CABLE Input (playback device)
   -> VB-CABLE
   -> CABLE Output (recording device)
   -> TikTok LIVE Studio
```

## Requirements

These are the only requirements for using the prebuilt plugin:

- Windows 10 or Windows 11, x64
- OBS Studio 32.2.2
- [VB-CABLE](https://vb-audio.com/Cable/) or another active Windows playback
  endpoint

Visual Studio, CMake, the Windows SDK, Git, and PowerShell are only required
when building the plugin from source.

## Install the plugin

1. Close OBS Studio.
2. Download `obs-virtual-audio-0.1.0-windows-x64.zip` from the
   [latest release](https://github.com/Jiksi/obs-virtual-audio/releases/latest).
3. Extract the downloaded ZIP once. It contains an `obs-virtual-audio` folder.
4. Copy that complete folder into:

   ```text
   C:\ProgramData\obs-studio\plugins\
   ```

5. Confirm that the resulting DLL path is:

   ```text
   C:\ProgramData\obs-studio\plugins\obs-virtual-audio\bin\64bit\obs-virtual-audio.dll
   ```

6. Start OBS Studio.

Copying files into `C:\ProgramData` may require administrator permission. If
Windows blocks the downloaded file, right-click the ZIP before extracting it,
open **Properties**, select **Unblock**, and extract it again.

## Configure OBS and VB-CABLE

1. Open **Settings -> Audio** in OBS and set **Sample Rate** to **48 kHz**.
2. Open **Advanced Audio Properties** from the OBS Audio Mixer.
3. Enable **Track 1** for every audio source that should be sent to TikTok.
4. Open **Tools -> OBS Virtual Audio**.
5. Select **CABLE Input (VB-Audio Virtual Cable)** as the playback device.
6. Confirm that the dialog reports **Status: Connected**.

The device names are intentionally opposite from the application's point of
view:

- **CABLE Input** receives audio from OBS Virtual Audio.
- **CABLE Output** exposes that audio to TikTok as a microphone.

For predictable audio timing, configure both **CABLE Input** and
**CABLE Output** as 48,000 Hz stereo devices in Windows Sound settings.

## Configure TikTok LIVE Studio

1. Select **CABLE Output (VB-Audio Virtual Cable)** as the microphone.
2. Disable the direct physical microphone in TikTok to avoid duplicate audio.
   Add the physical microphone to OBS instead, so it is included in Track 1.
3. If video is coming from OBS, start **OBS Virtual Camera** and select it as
   the camera in TikTok LIVE Studio.
4. Make a short TikTok recording and perform a visible hand clap to verify A/V
   synchronization.
5. Turn off TikTok audio monitoring after testing. Monitor playback can have
   additional latency and is not a reliable reference for the recorded or live
   output.

Judge synchronization from a TikTok recording or viewer-side output:

- If audio occurs before the matching video, add **Sync Offset** to the OBS
  audio sources in **Advanced Audio Properties**.
- If video occurs before the matching audio, add a **Render Delay** filter to
  the video source.
- Start with the measured difference and adjust in 25-50 ms steps.
- If only TikTok monitoring is delayed but the recording is synchronized, do
  not add either delay.

## Verify the installation

Open **Help -> Log Files -> View Current Log** in OBS and search for messages
similar to:

```text
[obs-virtual-audio] loaded (version 0.1.0)
[obs-virtual-audio] audio capture started: mix=0, 48000 Hz, stereo float
[obs-virtual-audio] WASAPI output started: CABLE Input ..., 48000 Hz, stereo float
```

Play audio in OBS and verify that meters move in this order:

1. The source and master meters in OBS
2. The microphone meter for `CABLE Output` in TikTok LIVE Studio

## Troubleshooting

### OBS Virtual Audio is missing from the Tools menu

- Confirm the DLL path matches the installation path shown above.
- Confirm that OBS Studio is the x64 build and is version 32.2.2.
- Check the current OBS log for `[obs-virtual-audio]` or module load errors.
- If the ZIP came from another computer, unblock it and reinstall the folder.

### No playback devices are listed

- Install or reinstall VB-CABLE, then restart Windows if its installer requests
  it.
- Confirm that **CABLE Input** is enabled under Windows playback devices.
- Click **Refresh** in **Tools -> OBS Virtual Audio**.

### Status is disconnected or reconnecting

- Confirm that the selected playback device is enabled and connected.
- Reopen **Tools -> OBS Virtual Audio**, select the device again, and apply it.
- Avoid changing the selected device's format while OBS is running; restart OBS
  after changing Windows audio formats.

### TikTok receives no audio

- Confirm that the required OBS sources are assigned to Track 1.
- Confirm that OBS Virtual Audio reports **Status: Connected**.
- Confirm that TikTok uses **CABLE Output**, not **CABLE Input**.
- Keep OBS, CABLE Input, and CABLE Output at 48 kHz.

### Audio is doubled or echoes

- Do not enable both the physical microphone and CABLE Output in TikTok.
- Do not capture TikTok's monitor playback back into OBS Desktop Audio.
- Turn off TikTok audio monitoring after calibration.

## Uninstall

1. Close OBS Studio.
2. Delete this folder:

   ```text
   C:\ProgramData\obs-studio\plugins\obs-virtual-audio\
   ```

VB-CABLE is installed separately and can remain installed or be removed using
its own installer.

## Build from source

The repository uses the official OBS plugin template infrastructure in a
temporary `.build` workspace. Local build requirements are:

- Windows 10 or Windows 11, x64
- Visual Studio 2026 with **Desktop development with C++**
- MSVC x64 build tools
- Windows 11 SDK 10.0.26100.0
- CMake available in `PATH`
- Git available in `PATH`
- PowerShell 7.2 or newer

From PowerShell 7 at the repository root, build the default configuration with:

```powershell
pwsh -File .\scripts\build-windows.ps1
```

Build and create a distributable Release ZIP with:

```powershell
pwsh -File .\scripts\package-windows.ps1 -Configuration Release
```

The resulting archive is written to:

```text
dist\obs-virtual-audio-<version>-windows-x64.zip
```

Pass `-SkipBuild` to `package-windows.ps1` to package an existing build of the
selected configuration. The scripts download verified OBS and Qt dependencies,
run the automated tests, validate the package, and prepare the OBS plugin folder
structure.

## Current implementation

The plugin captures OBS Track 1 as 48 kHz stereo float audio. Audio is buffered
outside the OBS callback thread and rendered through WASAPI. The queue is capped
at 50 ms to prevent clock drift from accumulating latency. If the selected
device disappears or WASAPI fails, the plugin reconnects automatically with
retry delays from 1 to 10 seconds.

## Next milestone

Add automated tests for renderer state transitions and device-loss recovery.
