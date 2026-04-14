#!/usr/bin/env python3
"""
Simple Push-to-Talk (PTT) application for Linux.
Mutes/unmutes a PulseAudio input based on gamepad button state.
"""

import argparse
import json
import os
import sys
import time
import subprocess
import select
from pathlib import Path
from typing import Optional, Dict, Any

try:
    import evdev
except ImportError:
    print("Error: evdev library not found. Install with: pip install evdev", file=sys.stderr)
    sys.exit(1)


CONFIG_DIR = Path.home() / ".config" / "ptt"
CONFIG_FILE = CONFIG_DIR / "config.json"


def get_input_devices():
    """Get all available input devices."""
    devices = [evdev.InputDevice(path) for path in evdev.list_devices()]
    return devices


def select_button():
    """Wait for user to press a button and return the device and button code."""
    print("Available input devices:")
    devices = get_input_devices()
    
    if not devices:
        print("No input devices found!", file=sys.stderr)
        sys.exit(1)
    
    for i, device in enumerate(devices):
        print(f"  [{i}] {device.name} ({device.path})")
    
    print("\nPress any button on the device you want to use for PTT...")
    print("(Axis movements will be ignored, only button presses count)")
    
    # Monitor all devices for button press
    devices_dict = {dev.fd: dev for dev in devices}
    
    while True:
        r, w, x = select.select(devices_dict.keys(), [], [], 1)
        for fd in r:
            device = devices_dict[fd]
            for event in device.read():
                # Only care about key/button events (type 1)
                if event.type == evdev.ecodes.EV_KEY and event.value == 1:  # Button press
                    button_name = evdev.ecodes.KEY[event.code] if event.code in evdev.ecodes.KEY else f"BTN_{event.code}"
                    print(f"\nDetected: {button_name} on {device.name}")
                    return device.path, device.name, event.code, button_name


def get_pulseaudio_sources():
    """Get list of PulseAudio input sources."""
    try:
        result = subprocess.run(
            ["pactl", "list", "sources", "short"],
            capture_output=True,
            text=True,
            check=True
        )
        sources = []
        for line in result.stdout.strip().split('\n'):
            if line:
                parts = line.split('\t')
                if len(parts) >= 2:
                    sources.append({
                        'id': parts[0],
                        'name': parts[1],
                        'description': parts[1]
                    })
        
        # Try to get better descriptions
        result = subprocess.run(
            ["pactl", "list", "sources"],
            capture_output=True,
            text=True,
            check=True
        )
        
        current_name = None
        for line in result.stdout.split('\n'):
            line = line.strip()
            if line.startswith('Name:'):
                current_name = line.split(':', 1)[1].strip()
            elif line.startswith('Description:') and current_name:
                description = line.split(':', 1)[1].strip()
                for source in sources:
                    if source['name'] == current_name:
                        source['description'] = description
                        break
        
        return sources
    except subprocess.CalledProcessError as e:
        print(f"Error getting PulseAudio sources: {e}", file=sys.stderr)
        return []


def select_audio_source():
    """Let user select a PulseAudio input source."""
    sources = get_pulseaudio_sources()
    
    if not sources:
        print("No PulseAudio sources found!", file=sys.stderr)
        sys.exit(1)
    
    print("\nAvailable audio input sources:")
    for i, source in enumerate(sources):
        print(f"  [{i}] {source['description']}")
    
    while True:
        try:
            choice = input("\nSelect source number: ").strip()
            idx = int(choice)
            if 0 <= idx < len(sources):
                return sources[idx]['name']
            else:
                print(f"Please enter a number between 0 and {len(sources) - 1}")
        except (ValueError, KeyboardInterrupt):
            print("\nInvalid input")
            sys.exit(1)


def save_config(config: Dict[str, Any]):
    """Save configuration to file."""
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    with open(CONFIG_FILE, 'w') as f:
        json.dump(config, f, indent=2)
    print(f"\nConfiguration saved to {CONFIG_FILE}")


def load_config() -> Optional[Dict[str, Any]]:
    """Load configuration from file."""
    if not CONFIG_FILE.exists():
        return None
    
    try:
        with open(CONFIG_FILE, 'r') as f:
            return json.load(f)
    except (json.JSONDecodeError, IOError) as e:
        print(f"Error loading config: {e}", file=sys.stderr)
        return None


def set_source_mute(source_name: str, mute: bool) -> bool:
    """Mute or unmute a PulseAudio source."""
    try:
        mute_value = "1" if mute else "0"
        subprocess.run(
            ["pactl", "set-source-mute", source_name, mute_value],
            check=True,
            capture_output=True
        )
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error setting mute state: {e}", file=sys.stderr)
        return False


def find_device_by_name(device_name: str) -> Optional[evdev.InputDevice]:
    """Find an input device by name."""
    devices = get_input_devices()
    for device in devices:
        if device.name == device_name:
            return device
    return None


def run_ptt(config: Dict[str, Any]):
    """Run the PTT application."""
    device_name = config['device_name']
    button_code = config['button_code']
    button_name = config['button_name']
    source_name = config['source_name']
    
    print(f"PTT Configuration:")
    print(f"  Device: {device_name}")
    print(f"  Button: {button_name}")
    print(f"  Audio Source: {source_name}")
    print(f"\nStarting PTT... (Press Ctrl+C to exit)")
    
    device = None
    last_button_state = False
    reconnect_delay = 1.0
    last_reconnect_attempt = 0
    
    # Start with mic muted (will unmute on button press)
    # But safe state on errors/exit is UNMUTED for race car communication
    current_mute_state = True
    set_source_mute(source_name, True)
    print("Mic muted (press button to talk)")
    
    while True:
        try:
            # Try to connect/reconnect to device
            if device is None:
                current_time = time.time()
                if current_time - last_reconnect_attempt >= reconnect_delay:
                    device = find_device_by_name(device_name)
                    last_reconnect_attempt = current_time
                    
                    if device:
                        print(f"Connected to {device_name}")
                        # Reset state on reconnect and ensure mic is muted
                        last_button_state = False
                        if not current_mute_state:
                            set_source_mute(source_name, True)
                            current_mute_state = True
                            print("Mic muted (device reconnected)")
                    else:
                        if reconnect_delay == 1.0:  # Only print once
                            print(f"Waiting for device '{device_name}' to connect...")
                        reconnect_delay = min(reconnect_delay * 1.5, 5.0)  # Exponential backoff
                        time.sleep(0.1)
                        continue
                else:
                    time.sleep(0.1)
                    continue
            
            # Reset reconnect delay on successful connection
            reconnect_delay = 1.0
            
            # Read events with timeout
            try:
                r, w, x = select.select([device.fd], [], [], 0.1)
                
                if r:
                    for event in device.read():
                        # Only process our specific button
                        if event.type == evdev.ecodes.EV_KEY and event.code == button_code:
                            button_pressed = (event.value == 1)
                            
                            # Only act on state changes
                            if button_pressed != last_button_state:
                                last_button_state = button_pressed
                                
                                # Button pressed = UNMUTE, button released = MUTE
                                # (But safe state on errors is UNMUTED for race car communication)
                                should_mute = not button_pressed
                                
                                if should_mute != current_mute_state:
                                    if set_source_mute(source_name, should_mute):
                                        current_mute_state = should_mute
                                        state_str = "muted" if should_mute else "UNMUTED"
                                        print(f"Mic {state_str}")
                                    else:
                                        print("Warning: Failed to change mute state (source may have disconnected)")
            
            except OSError as e:
                # Device disconnected
                print(f"Device disconnected: {e}")
                device = None
                # UNMUTE on disconnect for safety (race car communication)
                if current_mute_state:
                    set_source_mute(source_name, False)
                    current_mute_state = False
                    print("Mic UNMUTED (device disconnected - safe state)")
                continue
        
        except KeyboardInterrupt:
            print("\nExiting...")
            # UNMUTE on exit for safety (race car communication)
            set_source_mute(source_name, False)
            print("Mic UNMUTED")
            break


def setup():
    """Run setup wizard."""
    print("=== PTT Setup ===\n")
    
    # Select button
    device_path, device_name, button_code, button_name = select_button()
    
    # Select audio source
    source_name = select_audio_source()
    
    # Save configuration
    config = {
        'device_path': device_path,
        'device_name': device_name,
        'button_code': button_code,
        'button_name': button_name,
        'source_name': source_name
    }
    
    save_config(config)
    
    print("\nSetup complete! Run without --setup to start PTT.")


def main():
    parser = argparse.ArgumentParser(
        description="Simple Push-to-Talk for Linux",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --setup    Run setup wizard
  %(prog)s            Start PTT with saved configuration
        """
    )
    parser.add_argument(
        '--setup',
        action='store_true',
        help='Run setup wizard to configure PTT'
    )
    
    args = parser.parse_args()
    
    if args.setup:
        setup()
    else:
        config = load_config()
        if config is None:
            print("No configuration found. Run with --setup first.", file=sys.stderr)
            sys.exit(1)
        
        run_ptt(config)


if __name__ == '__main__':
    main()
