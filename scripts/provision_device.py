#!/usr/bin/env python3
"""
provision_device.py

Writes per-device credentials (Wi-Fi, MQTT broker account, device id) into the
'devcfg' NVS partition of a flashed ESP32-S3. Only that partition is written, so
user data in the main 'nvs' partition is kept.

Run it from an ESP-IDF shell (it uses nvs_partition_gen.py and parttool.py):

    python scripts/provision_device.py                    # config/device_config.csv, port from scripts/config.json
    python scripts/provision_device.py -p COM7 -c config/door02.csv
    python scripts/provision_device.py --no-flash -o devcfg.bin   # factory: build the image only

See docs/provisioning.md.
"""

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PARTITION_NAME = "devcfg"
NAMESPACE = "device_cfg"
DEFAULT_CSV = os.path.join(ROOT, "config", "device_config.csv")
SERIAL_CONFIG = os.path.join(ROOT, "scripts", "config.json")
PARTITIONS_CSV = os.path.join(ROOT, "partitions.csv")

# Limits mirror app/device_config/device_config_config.h (buffer size minus the terminator).
REQUIRED = {
    "wifi_ssid": (1, 32),
    "wifi_pass": (8, 64),
    "device_id": (1, 32),
    "mqtt_uri": (1, 127),
    "mqtt_user": (1, 64),
    "mqtt_pass": (1, 128),
}
OPTIONAL_FILES = {"mqtt_ca": 3999}
# Values from config/device_config.csv.example that must be replaced before flashing.
PLACEHOLDERS = {"YourWifiName", "YourWifiPassword", "device-broker-password"}
PLACEHOLDER_HOST = "example.com"


def fail(msg):
    print(f"[!] {msg}")
    sys.exit(1)


def idf_tool(*parts):
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        fail("IDF_PATH is not set. Run this script from an ESP-IDF shell.")
    path = os.path.join(idf_path, *parts)
    if not os.path.isfile(path):
        fail(f"ESP-IDF tool not found: {path}")
    return path


def partition_size():
    with open(PARTITIONS_CSV, encoding="utf-8") as f:
        for row in csv.reader(line for line in f if not line.lstrip().startswith("#")):
            cells = [c.strip() for c in row]
            if cells and cells[0] == PARTITION_NAME:
                return cells[4]
    fail(f"Partition '{PARTITION_NAME}' not found in {PARTITIONS_CSV}")


def default_port():
    try:
        with open(SERIAL_CONFIG, encoding="utf-8") as f:
            return json.load(f).get("serial", {}).get("port")
    except (OSError, ValueError):
        return None


def load_and_check(csv_path):
    """Validates the CSV and returns rows with file paths made absolute."""
    if not os.path.isfile(csv_path):
        fail(f"'{csv_path}' not found. Copy config/device_config.csv.example and fill it in.")

    with open(csv_path, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    base = os.path.dirname(os.path.abspath(csv_path))
    values, out = {}, []
    for row in rows:
        key, typ = (row.get("key") or "").strip(), (row.get("type") or "").strip()
        value = row.get("value") or ""
        if typ == "namespace":
            if key != NAMESPACE:
                fail(f"Namespace must be '{NAMESPACE}', got '{key}'")
        elif key in REQUIRED and typ == "data":
            lo, hi = REQUIRED[key]
            if not lo <= len(value.encode("utf-8")) <= hi:
                fail(f"'{key}' must be {lo}..{hi} bytes")
            values[key] = value
        elif key in OPTIONAL_FILES and typ == "file":
            path = value if os.path.isabs(value) else os.path.join(base, value)
            if not os.path.isfile(path):
                fail(f"'{key}' file not found: {path}")
            if os.path.getsize(path) > OPTIONAL_FILES[key]:
                fail(f"'{key}' file is larger than {OPTIONAL_FILES[key]} bytes")
            row["value"] = path
        else:
            fail(f"Unexpected row: key='{key}' type='{typ}'")
        out.append(row)

    missing = sorted(set(REQUIRED) - set(values))
    if missing:
        fail(f"Missing keys: {', '.join(missing)}")
    left = sorted(k for k, v in values.items() if v in PLACEHOLDERS or PLACEHOLDER_HOST in v)
    if left:
        fail(f"Still using example values for: {', '.join(left)}. Edit {csv_path} first.")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", values["device_id"]):
        fail("device_id may only contain letters, digits, '-' and '_'")
    if not values["mqtt_uri"].startswith("mqtts://"):
        print("[!] Warning: mqtt_uri is not mqtts://; credentials will travel in plain text.")

    print(f"[*] Device '{values['device_id']}', Wi-Fi '{values['wifi_ssid']}', broker {values['mqtt_uri']}")
    return out


def main():
    parser = argparse.ArgumentParser(description="Write device credentials to the devcfg NVS partition.")
    parser.add_argument("-c", "--csv", default=DEFAULT_CSV, help="Credentials CSV (default: config/device_config.csv)")
    parser.add_argument("-p", "--port", help="Serial port (default: scripts/config.json)")
    parser.add_argument("-o", "--output", help="Also keep the generated image at this path (contains secrets)")
    parser.add_argument("--no-flash", action="store_true", help="Only generate the image")
    args = parser.parse_args()

    if args.no_flash and not args.output:
        fail("--no-flash needs --output")

    rows = load_and_check(args.csv)
    size = partition_size()

    with tempfile.TemporaryDirectory() as tmp:
        tmp_csv = os.path.join(tmp, "devcfg.csv")
        with open(tmp_csv, "w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=["key", "type", "encoding", "value"], extrasaction="ignore")
            writer.writeheader()
            writer.writerows(rows)

        image = os.path.abspath(args.output) if args.output else os.path.join(tmp, "devcfg.bin")
        gen = idf_tool("components", "nvs_flash", "nvs_partition_generator", "nvs_partition_gen.py")
        subprocess.run([sys.executable, gen, "generate", tmp_csv, image, size], check=True, stdout=subprocess.DEVNULL)
        print(f"[+] Generated {PARTITION_NAME} image ({size} bytes)")

        if args.no_flash:
            print(f"[+] Saved to {image}. It contains passwords: do not commit or share it.")
            return

        port = args.port or default_port()
        if not port:
            fail("No serial port. Pass -p COMx or run scripts/generate_config.py.")

        parttool = idf_tool("components", "partition_table", "parttool.py")
        print(f"[*] Writing partition '{PARTITION_NAME}' on {port}...")
        result = subprocess.run(
            [sys.executable, parttool, "--port", port, "write_partition",
             "--partition-name", PARTITION_NAME, "--input", image],
        )
        if result.returncode != 0:
            fail(
                "Writing failed. Common causes:\n"
                "    - 'Partition does not exist': the board runs an old partition table. Flash the firmware first:\n"
                f"      idf.py -p {port} erase-flash flash\n"
                "    - 'Wrong boot mode' / cannot connect: close idf.py monitor, or hold BOOT, press RESET, release BOOT"
            )
        print("[+] Done. Reset the board; the log shows 'Loaded device ...' when it reads the new credentials.")


if __name__ == "__main__":
    main()
