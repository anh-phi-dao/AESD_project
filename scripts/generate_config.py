#!/usr/bin/env python3
"""
generate_config.py

Generates local 'config.json' from 'config.template.json'.
This file is kept ignored by Git to allow local environment customizations
(such as serial COM ports, toolchain paths) without polluting version control.
"""

import os
import sys
import json
import shutil
import argparse

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TEMPLATE_FILE = os.path.join(SCRIPT_DIR, "config.template.json")
CONFIG_FILE = os.path.join(SCRIPT_DIR, "config.json")


def list_available_com_ports():
    """List available serial COM ports if pyserial is installed."""
    try:
        import serial.tools.list_ports
        ports = list(serial.tools.list_ports.comports())
        return [p.device for p in ports]
    except ImportError:
        return []


def generate_config(port=None, baudrate=None, force=False):
    """Create or update local config.json from template."""
    if not os.path.exists(TEMPLATE_FILE):
        print(f"[!] Error: Template file not found at: {TEMPLATE_FILE}")
        sys.exit(1)

    if os.path.exists(CONFIG_FILE) and not force:
        print(f"[*] '{CONFIG_FILE}' already exists.")
        print("    Use '--force' to overwrite it with clean template defaults.")
        return

    print(f"[*] Copying template '{TEMPLATE_FILE}' -> '{CONFIG_FILE}'...")
    shutil.copyfile(TEMPLATE_FILE, CONFIG_FILE)

    with open(CONFIG_FILE, "r", encoding="utf-8") as f:
        config = json.load(f)

    modified = False

    # Auto-detect COM port if not explicitly specified and current is default
    if not port:
        available = list_available_com_ports()
        if available:
            print(f"[*] Detected available serial ports: {available}")
            # If default port is not in available, prompt/suggest the first detected port
            if config.get("serial", {}).get("port") not in available:
                suggested = available[0]
                print(f"[*] Setting detected port '{suggested}' in config.json")
                config.setdefault("serial", {})["port"] = suggested
                modified = True
    else:
        config.setdefault("serial", {})["port"] = port
        modified = True

    if baudrate:
        config.setdefault("serial", {})["baudrate"] = int(baudrate)
        modified = True

    if modified:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=4)
        print(f"[*] Updated '{CONFIG_FILE}' with user configurations.")

    print(f"[+] Configuration ready in '{CONFIG_FILE}'.")
    print("    Remember: 'config.json' is ignored by Git and should NOT be pushed.")


def main():
    parser = argparse.ArgumentParser(
        description="Generate local config.json from template for local development."
    )
    parser.add_argument("-p", "--port", help="Serial port to set in config.json (e.g., COM3, /dev/ttyUSB0)")
    parser.add_argument("-b", "--baudrate", type=int, help="Serial baudrate (default: 115200)")
    parser.add_argument("-f", "--force", action="store_true", help="Force overwrite existing config.json")

    args = parser.parse_args()
    generate_config(port=args.port, baudrate=args.baudrate, force=args.force)


if __name__ == "__main__":
    main()

