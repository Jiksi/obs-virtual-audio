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

## Development baseline

The project will follow the official OBS plugin template conventions for CMake and Windows builds. The initial Windows development target is Visual Studio 2022 with CMake.

## Status

Bootstrap repository initialized. Plugin source and WASAPI implementation will be added next.
