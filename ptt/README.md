
# PTT - Simple Push-to-Talk for Linux

A lightweight, dependency-minimal push-to-talk application for Linux that uses gamepad buttons to control microphone muting via PulseAudio.

This was vibed because Discord does not support gamepad PTT (only keyboard PTT) on Linux...?????

## Features

- **Simple setup**: Interactive wizard to configure your button and microphone
- **Robust**: Automatically recovers when devices disconnect and reconnect
- **Lightweight**: Minimal dependencies (only `evdev` Python library)
- **Clear feedback**: Logs all state changes to stdout

## Requirements

- Linux with PulseAudio
- Python 3.6+
- `evdev` Python library
- `pactl` command (usually pre-installed with PulseAudio)

## Installation

1. Install the required Python library:
   ```bash
   pip install evdev
   ```

2. Make the script executable:
   ```bash
   chmod +x ptt.py
   ```

3. (Optional) Create a symlink for easier access:
   ```bash
   sudo ln -s $(pwd)/ptt.py /usr/local/bin/ptt
   ```

## Usage

### First-time Setup

Run the setup wizard to configure your PTT button and microphone:

```bash
./ptt.py --setup
```

The wizard will:
1. Show all available input devices
2. Wait for you to press the PTT (Push-to-Talk) button on your gamepad/controller
3. Wait for you to press the gain up button
4. Wait for you to press the gain down button
5. Show all available PulseAudio input sources
6. Let you select your microphone
7. Save the configuration to `~/.config/ptt/config.json`

### Running PTT

After setup, simply run:

```bash
./ptt.py
```

The application will:
- Start with your microphone at **0% gain** (muted)
- Set to **configured transmit gain** when you press and hold the PTT button
- Return to **0% gain** when you release the PTT button
- Allow you to adjust transmit gain (in 5% increments) using the gain up/down buttons
- Save the transmit gain setting automatically when changed
- Log all state changes to stdout
- Automatically reconnect if your gamepad or microphone disconnects
- Set microphone to **100% gain** on exit or errors (safe state for race car communication)

Press `Ctrl+C` to exit (microphone will be set to 100% gain for safety).

## How It Works

- **Button Detection**: Uses the Linux `evdev` interface to read raw input events from your gamepad
- **Audio Control**: Uses `pactl` (PulseAudio command-line tool) to control microphone volume/gain
- **Gain Control**: Adjustable transmit gain (0-100%) using dedicated buttons, saved to config
- **Device Recovery**: Polls for device reconnection with exponential backoff if devices disconnect
- **Safe Defaults**: On errors or exit, microphone is set to 100% gain for race car communication safety

## Troubleshooting

### Permission Denied on Input Devices

If you get permission errors accessing `/dev/input/eventX`, you may need to add your user to the `input` group:

```bash
sudo usermod -a -G input $USER
```

Then log out and log back in.

### No Input Devices Found

Make sure your gamepad/controller is connected and recognized by the system:

```bash
ls -l /dev/input/event*
```

### PulseAudio Source Not Found

List your PulseAudio sources:

```bash
pactl list sources short
```

If your microphone isn't listed, check your audio settings.

### Device Names Changed After Reconnect

The script uses device names (not paths) to handle reconnection. If your device name changes, you'll need to run `--setup` again.

## Configuration File

Configuration is stored in `~/.config/ptt/config.json`:

```json
{
  "device_path": "/dev/input/event5",
  "device_name": "Xbox Wireless Controller",
  "button_code": 307,
  "button_name": "BTN_WEST",
  "gain_up_code": 310,
  "gain_up_name": "BTN_NORTH",
  "gain_down_code": 308,
  "gain_down_name": "BTN_SOUTH",
  "source_name": "alsa_input.usb-Blue_Microphones_Yeti_Stereo_Microphone_REV8-00.analog-stereo",
  "transmit_gain": 85
}
```

The `transmit_gain` value (0-100) is automatically updated when you adjust gain during operation.

You can manually edit this file if needed, but it's easier to run `--setup` again.

## Wayland / KDE Notes

This application works on both X11 and Wayland since it:
- Uses low-level input device access (evdev) rather than X11/Wayland protocols
- Uses PulseAudio's command-line interface rather than GUI integration

No special configuration needed for Wayland or KDE.

## License

This is free and unencumbered software released into the public domain.
