# Audio formats, rates and bit depths

## What plays

| Format | Decoder | Notes |
|---|---|---|
| MP3 | dr_mp3 | streams while downloading |
| FLAC (up to 8 ch, 32 bit, 655 kHz) | dr_flac | streams while downloading |
| WAV / RF64 / W64 | dr_wav | PCM 8..32 bit, float32/64, A-law, mu-law, IMA/MS ADPCM, extensible multichannel; streams |
| AIFF / AIFC | own reader | PCM 8..32 bit big-endian, `sowt`, `fl32`/`fl64`; compressed AIFC is not read |
| Ogg Vorbis (.ogg/.oga) | libvorbisfile | up to 8 channels |
| FLAC in Ogg (.oga/.ogg) | dr_flac (Ogg container) | same as FLAC |
| Opus (.opus / Ogg Opus) | libopus + own Ogg reader | 48 kHz, pre-skip, gain, multistream (mapping family 1), exact seek |
| AAC-LC, HE-AAC v1/v2 (.m4a/.mp4/.m4b, raw .aac ADTS) | fdk-aac + own MP4 reader | edit list (encoder delay) honoured |
| ALAC (.m4a) | Apple's open-source decoder | 16/20/24/32 bit |

Not decoded natively (WMA, APE, WavPack, DSD, ...): while online the server transcodes them to MP3 (`format=mp3`), and downloads of
them are fetched as MP3 (320 kbps unless a lower quality is chosen), so they stay playable offline.

Everything the decoders return is interleaved **float** at the file's own rate; `Decoder::bits()` reports the source depth for the Now Playing panel.
Multichannel (up to 8) is folded to stereo in `Player` (ITU-style, normalised); channel order is converted to the WAVE order inside each decoder.

## Rates and bit depth

- The player resamples to the device rate with a Kaiser-windowed sinc (16 zero crossings, cutoff lowered when downsampling), so 96/192 kHz material
  does not fold ultrasonic content into the audible band. Equal rates pass through bit-exact.
- The mix stays float to the device: 24/32-bit sources are not truncated to 16 bit inside the app.
- Android output is AAudio called directly (`src/audio/aaudio_out.cpp`, float, data callback). SDL 2.30's AAudio backend writes with a 1 ms timeout
  and discards what does not fit, so it is only the fallback now (16-bit, TPDF-dithered from the float mix).
- Downloads: lossy or bitrate-capped downloads go through `stream.view` as MP3; the `download` endpoint ignores the bitrate. Files are identified by their
  header (`sniff_audio_kind`), never by the song's suffix.

## Tests

`ctest -R decoders` decodes the fixtures in `tests/fixtures/audio` (made with ffmpeg: 8-bit/8 kHz up to 24-bit/192 kHz, 5.1, mono/stereo, every format
above) and checks rate, channels, bit depth, length, level and pitch (Goertzel), centre-channel position for 5.1, seeking and lossless bit-exactness.
`dsp_test` covers the resampler (unity gain in band, alias rejection for 96k/192k to 48k).

## Third-party code added for this

libopus (BSD-3), libvorbis + libogg (BSD-3), fdk-aac (FDK AAC license), Apple ALAC (Apache-2.0, vendored in `third_party/alac`), dr_wav (public
domain / MIT-0). All are listed in Settings > Licenses. fdk-aac needs the stub `third_party/fdk_android_stub/log/log.h` on Android (AOSP-only header).

## Device note (Oilsky G88)

When a debug session force-stops the app many times, the device's `audioserver`/MediaTek HAL degrades and every app's audio runs at a fraction of
real time (position advanced 0.14-0.6x, `Frames written` in `dumpsys media.audio_flinger` crawled, HAL `audio_pool_buf_copy_from_linear fail!!` in logcat).
`adb shell setprop ctl.restart audioserver` (or a reboot) restores real-time playback. Measure with the saved play position:
`playstate_<acct>.json` `pos` advances ~10 s per 10 s of wall time when healthy.

## Porting to iOS and macOS (not done yet, notes only)

Nothing here is Android-specific except `aaudio_out.*` (guarded by `__ANDROID__`) and the fdk-aac log stub. To port:

1. Configure with the existing Apple flows (`build-ios.sh`, `build-macos.sh`, docs/APPLE.md). The new dependencies (opus, fdk-aac, ogg/vorbis, ALAC) are
   fetched/built by the root `CMakeLists.txt`; check that opus finds NEON (arm64) and that `-O2` on fdk-aac/opus applies in Debug.
2. Audio output goes through SDL's CoreAudio backend with `AUDIO_F32SYS` (no code change); verify the callback size and that 96/192 kHz material
   opens at the device rate (`set_preferred_device_rate` is currently Android-only, desktop opens at the track rate).
3. iOS: add `UIBackgroundModes=audio` handling for the new decoders is unchanged, but run `decoder_test` on a device build (the tests target is off for iOS)
   and play one file of each format from the library, including a 24-bit/96 kHz FLAC and an m4a.
4. macOS: run `ctest` (decoders, dsp) and play the same set; the unit tests need no audio device.
5. Review the fdk-aac license text for App Store distribution (it forbids patent grants; some stores want a note) and ship the licenses with the app.
