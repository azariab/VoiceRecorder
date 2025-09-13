# VoiceRecorder Audio Settings and AFE Investigation

## Summary
- Final recording path: 16 kHz, 16‑bit PCM; mono downmix (L+R)/2; duplicated to stereo in WAV for player compatibility.
- AFE (VC/SR) disabled for recording due to audible artifacts on this hardware/stack.
- Rationale: raw path is clean; downmix yields better speech consistency than raw stereo with closely spaced mics.

## Hardware/stack
- Board: ESP32‑S3‑BOX‑3 (ES7210 ADC, ES8311 DAC, GT911 touch, ILI9341 LCD)
- Framework: ESP‑IDF 5.5.1; components via managed_components
- Storage: SD card at /sdcard

## Findings (chronological, condensed)
1. SD card: mount and filename quirks resolved; FAT short names (REC###.WAV) reliable.
2. Raw recorder: clean stereo capture at 16 kHz; WAV header/stop race fixed; stable.
3. Media Player: refreshed file index to /sdcard; plays WAVs.
4. AFE attempts:
   - VC AFE (SE/AGC, AEC off): frequent fetch ret=-1; files with partial/garbled content.
   - SR AFE (SE only): aligned feed/fetch (160/480), some frames written, still audible corruption.
   - 2‑mic feed, 3‑ch with zero ref, and mono feed variants tried; corruption persisted.
   - Conclusion: with this build set and hardware, AFE introduces artifacts; raw I2S path is clean.
5. Mapping tests: added RAW modes (ST/L/R/M). All clean; ST and M preferred subjectively; M slightly better for speech.
6. UI: settings moved to right column; Back at top‑left unobstructed.

## Final recording settings
- Sample rate: 16000 Hz
- Bit depth: 16‑bit PCM
- Input: two ES7210 mics, interleaved L/R
- Mix: mono downmix m = (L+R)/2
- File: WAV header written as stereo (2 ch); mono duplicated to L/R for player compatibility
- AFE: disabled (wakenet/vad/aec/agc off); VC/SR not used
- Player/I2S guard: media player disabled while recording to keep I2S fixed at 16 kHz

## Why mono downmix?
- With very close mic spacing, stereo adds little localization and can introduce phase differences.
- Downmix improves SNR consistency and intelligibility for speech while remaining simple and predictable.

## Next optional improvements (non‑AFE)
- Add fixed high‑pass (~100 Hz) to remove DC/rumble
- Gentle RMS AGC (slow release) targeting ~‑20 dBFS
- Filename timestamping once RTC/time is available

## Known logs and cues
- I2S reconfig lines (16 kHz) expected at record start
- AFE tags should be absent in final path (AFE disabled)

## Troubleshooting notes
- If future AFE is revisited: ensure exact feed alignment, avoid zero reference with AEC, update esp‑sr, and verify mic slot order before processing.
