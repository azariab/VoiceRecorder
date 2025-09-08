## ESP32-S3-BOX VoiceRecorder

A PlatformIO/Arduino project for ESP32-S3-BOX to record stereo audio from the two onboard microphones, save uncompressed BWF files to SD-Card with timestamped filenames, and provide a simple LCD UI to record, play, and manage recordings.

### Status
- Scaffolded project with PlatformIO config for `esp32s3box`.
- Core features (audio I/O, BWF writing, LCD UI) are planned per Product Requirements (`prd.md`).

### Product Requirements (from `prd.md`)
- Record from two microphones; combine into stereo and save to SD-Card in a dedicated folder.
- Store audio in uncompressed BWF format (for timestamps/metadata).
- Filename must reflect exact creation date/time.
- LCD UI should support:
  - Scroll recordings (arrows)
  - Play / Fast play (long press)
  - Pause
  - Delete selected file
  - Record new file / Stop recording
  - Option to switch between recording and playback screens due to limited space
- Show battery charge level on LCD
- Debug screen with logs and a way to go back

### Hardware and References
- Hardware: ESP32-S3-BOX (using SENSOR dock for peripherals)
- Peripherals: LCD, SD-Card, battery manager, integrated buttons, Bluetooth, Wi‑Fi, speaker
- Schematics:
  - Main board: https://github.com/espressif/esp-box/blob/master/hardware/SCH_ESP32-S3-BOX-3_V1.0/SCH_ESP32-S3-BOX-3-MB_V1.1_20230808.pdf
  - SENSOR dock: https://github.com/espressif/esp-box/blob/master/hardware/SCH_ESP32-S3-BOX-3_V1.0/SCH_ESP32-S3-BOX-3-SENSOR-01_V1.1_20230922.pdf
- Espressif repo: https://github.com/espressif/esp-box
- Examples: https://github.com/espressif/esp-box/tree/master/examples

### Project Layout
- `src/main.cpp`: Arduino entry point (currently a minimal template)
- `platformio.ini`: PlatformIO environment configuration
- `prd.md`: Product requirements and reference links
- `include/`, `lib/`, `test/`: Standard PlatformIO folders

### Build and Flash
Requires VS Code with the PlatformIO extension, or PlatformIO Core (CLI).

PlatformIO environment is named `esp32s3box`:
```ini
[env:esp32s3box]
platform = espressif32
board = esp32s3box
framework = arduino
```

Using VS Code + PlatformIO:
- Open the workspace folder.
- Use the PlatformIO sidebar to Build and Upload the `esp32s3box` environment.

Using CLI:
```bash
# Build
pio run -e esp32s3box

# Flash firmware to the board
pio run -e esp32s3box -t upload

# Optional: serial monitor (adjust baud if needed)
pio device monitor -b 115200
```

### Recording and File Storage (planned)
- Folder on SD-Card: `/recordings`
- Filename format: `YYYYMMDD_HHMMSS.wav` (BWF is stored in a `.wav` container with a BEXT chunk)
- Audio format: stereo, uncompressed PCM (exact sample rate/bit-depth TBD)
- BWF metadata: include timestamp and optional description/originator

### LCD UI (planned)
- Recording screen: record/stop, level or simple indicator, elapsed time
- Playback screen: file list with arrows to scroll, play/pause, fast play on long press, delete
- Battery indicator visible on both screens
- Debug screen accessible via a button; shows logs and a back action

### Roadmap
1) Audio pipeline: microphone capture → stereo mixing → WAV/BWF writer
2) SD-Card I/O: folder creation, timestamped filenames, storage management
3) LCD UI: simple two-screen navigation + buttons handling
4) Playback engine: file list, play/pause, fast play, delete
5) Battery meter integration
6) Debug screen and logging view
7) Optional: Bluetooth/Wi‑Fi features

### Development Notes
- This repository is configured for Arduino framework on `esp32s3box` via PlatformIO.
- Use SSH for git operations: `git@github.com:azariab/VoiceRecorder.git`.

### License
TBD


