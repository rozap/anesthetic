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


def select_button(prompt: str):
    """Wait for user to press a button and return the device and button code."""
    print(f"\n{prompt}")
    print("(Axis movements will be ignored, only button presses count)")
    
    devices = get_input_devices()
    if not devices:
        print("No input devices found!", file=sys.stderr)
        sys.exit(1)
    
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
                    print(f"Detected: {button_name} on {device.name}")
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


def get_source_description(source_name: str) -> Optional[str]:
    """Get the description of a PulseAudio source."""
    try:
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
            elif line.startswith('Description:') and current_name == source_name:
                return line.split(':', 1)[1].strip()
        
        return None
    except subprocess.CalledProcessError:
        return None


def set_source_volume(source_name: str, volume_percent: int) -> bool:
    """Set the volume of a PulseAudio source.
    
    Args:
        source_name: Name of the PulseAudio source
        volume_percent: Volume level (0-100)
    
    Returns:
        True if successful, False otherwise
    """
    try:
        volume_percent = max(0, min(100, volume_percent))  # Clamp to 0-100
        subprocess.run(
            ["pactl", "set-source-volume", source_name, f"{volume_percent}%"],
            check=True,
            capture_output=True
        )
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error setting volume: {e}", file=sys.stderr)
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
    gain_up_code = config['gain_up_code']
    gain_up_name = config['gain_up_name']
    gain_down_code = config['gain_down_code']
    gain_down_name = config['gain_down_name']
    source_name = config['source_name']
    transmit_gain = config.get('transmit_gain', 100)  # Default to 100% if not specified
    
    source_description = get_source_description(source_name)
    
    print(f"PTT Configuration:")
    print(f"  Device: {device_name}")
    print(f"  PTT Button: {button_name}")
    print(f"  Gain Up Button: {gain_up_name}")
    print(f"  Gain Down Button: {gain_down_name}")
    print(f"  Audio Source: {source_name}")
    if source_description:
        print(f"  Description: {source_description}")
    print(f"  Transmit Gain: {transmit_gain}%")
    print(f"\nStarting PTT... (Press Ctrl+C to exit)")
    
    device = None
    last_ptt_state = False
    last_gain_up_state = False
    last_gain_down_state = False
    reconnect_delay = 1.0
    last_reconnect_attempt = 0
    
    # Start with mic at 0% volume (will set to transmit_gain on button press)
    # But safe state on errors/exit is 100% for race car communication
    current_volume = 0
    set_source_volume(source_name, 0)
    print("Mic muted (0% gain - press PTT button to talk)")
    
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
                        last_ptt_state = False
                        last_gain_up_state = False
                        last_gain_down_state = False
                        if current_volume != 0:
                            set_source_volume(source_name, 0)
                            current_volume = 0
                            print("Mic muted (0% gain - device reconnected)")
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
                        if event.type == evdev.ecodes.EV_KEY:
                            # Process PTT button
                            if event.code == button_code:
                                button_pressed = (event.value == 1)
                                
                                # Only act on state changes
                                if button_pressed != last_ptt_state:
                                    last_ptt_state = button_pressed
                                    
                                    # Button pressed = transmit at configured gain, released = 0%
                                    # (But safe state on errors is 100% for race car communication)
                                    target_volume = transmit_gain if button_pressed else 0
                                    
                                    if target_volume != current_volume:
                                        if set_source_volume(source_name, target_volume):
                                            current_volume = target_volume
                                            if target_volume == 0:
                                                print(f"Mic muted (0% gain)")
                                            else:
                                                print(f"Mic TRANSMITTING ({target_volume}% gain)")
                                        else:
                                            print("Warning: Failed to change volume (source may have disconnected)")
                            
                            # Process gain up button
                            elif event.code == gain_up_code:
                                button_pressed = (event.value == 1)
                                
                                if button_pressed and not last_gain_up_state:
                                    transmit_gain = min(100, transmit_gain + 5)
                                    config['transmit_gain'] = transmit_gain
                                    save_config(config)
                                    print(f"Transmit gain increased to {transmit_gain}%")
                                    
                                    # If currently transmitting, update volume immediately
                                    if last_ptt_state:
                                        if set_source_volume(source_name, transmit_gain):
                                            current_volume = transmit_gain
                                            print(f"Mic TRANSMITTING ({transmit_gain}% gain)")
                                
                                last_gain_up_state = button_pressed
                            
                            # Process gain down button
                            elif event.code == gain_down_code:
                                button_pressed = (event.value == 1)
                                
                                if button_pressed and not last_gain_down_state:
                                    transmit_gain = max(0, transmit_gain - 5)
                                    config['transmit_gain'] = transmit_gain
                                    save_config(config)
                                    print(f"Transmit gain decreased to {transmit_gain}%")
                                    
                                    # If currently transmitting, update volume immediately
                                    if last_ptt_state:
                                        if set_source_volume(source_name, transmit_gain):
                                            current_volume = transmit_gain
                                            print(f"Mic TRANSMITTING ({transmit_gain}% gain)")
                                
                                last_gain_down_state = button_pressed
            
            except OSError as e:
                # Device disconnected
                print(f"Device disconnected: {e}")
                device = None
                # Set to 100% on disconnect for safety (race car communication)
                if current_volume != 100:
                    set_source_volume(source_name, 100)
                    current_volume = 100
                    print("Mic set to 100% gain (device disconnected - safe state)")
                continue
        
        except KeyboardInterrupt:
            print("\nExiting...")
            # Set to 100% on exit for safety (race car communication)
            set_source_volume(source_name, 100)
            print("Mic set to 100% gain")
            break


def setup():
    """Run setup wizard."""
    print("=== PTT Setup ===\n")
    
    print("Available input devices:")
    devices = get_input_devices()
    
    if not devices:
        print("No input devices found!", file=sys.stderr)
        sys.exit(1)
    
    for i, device in enumerate(devices):
        print(f"  [{i}] {device.name} ({device.path})")
    
    # Select PTT button
    device_path, device_name, button_code, button_name = select_button(
        "Press the button you want to use for PTT (Push-to-Talk)..."
    )
    
    # Select gain up button
    _, _, gain_up_code, gain_up_name = select_button(
        "Press the button you want to use to INCREASE transmit gain..."
    )
    
    # Select gain down button
    _, _, gain_down_code, gain_down_name = select_button(
        "Press the button you want to use to DECREASE transmit gain..."
    )
    
    # Select audio source
    source_name = select_audio_source()
    
    # Save configuration
    config = {
        'device_path': device_path,
        'device_name': device_name,
        'button_code': button_code,
        'button_name': button_name,
        'gain_up_code': gain_up_code,
        'gain_up_name': gain_up_name,
        'gain_down_code': gain_down_code,
        'gain_down_name': gain_down_name,
        'source_name': source_name,
        'transmit_gain': 100  # Default to 100%
    }
    
    save_config(config)
    
    print("\nSetup complete! Run without --setup to start PTT.")
    print(f"Default transmit gain is set to 100%. Use {gain_up_name}/{gain_down_name} to adjust during operation.")


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
