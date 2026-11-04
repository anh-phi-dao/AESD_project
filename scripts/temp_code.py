#!/usr/bin/env python3
"""
temp_code.py

Reference of the temporary guest codes (docs/temp_code.md). The firmware (app/temp_code) and the web page
(web/js/temp-code.js) compute the same codes; this script is the one to compare them with.

    python scripts/temp_code.py <secret>               # the code valid from now, and until when
    python scripts/temp_code.py <secret> --at 1790000000   # the code made at that Unix time
    python scripts/temp_code.py --new-secret           # a fresh secret for config/device_config.csv

The secret is 32 hex characters (16 bytes), the "tmp_secret" row of the device CSV.
"""

import argparse
import datetime
import hashlib
import hmac
import re
import secrets
import sys
import time

LABEL = b"LOCKTMP1"
DIGITS = 8
VALID_MIN = 30


def parse_secret(text):
    if not re.fullmatch(r"[0-9a-fA-F]{32}", text or ""):
        raise ValueError("the secret must be 32 hex characters")
    return bytes.fromhex(text)


def code_at(key, slot):
    """The code of one minute slot (Unix time // 60), as 8 digits."""
    mac = hmac.new(key, LABEL + slot.to_bytes(4, "big"), hashlib.sha256).digest()
    offset = mac[-1] & 0x0F
    value = int.from_bytes(mac[offset : offset + 4], "big") & 0x7FFFFFFF
    return f"{value % 10 ** DIGITS:0{DIGITS}d}"


def main():
    parser = argparse.ArgumentParser(description="Temporary guest codes of the smart lock.")
    parser.add_argument("secret", nargs="?", help="32 hex characters (tmp_secret)")
    parser.add_argument("--at", type=int, help="Unix time the code is made at (default: now)")
    parser.add_argument("--new-secret", action="store_true", help="Print a new random secret")
    args = parser.parse_args()

    if args.new_secret:
        print(secrets.token_hex(16))
        return
    try:
        key = parse_secret(args.secret)
    except ValueError as err:
        sys.exit(f"[!] {err}")

    now = args.at if args.at is not None else int(time.time())
    slot = now // 60
    until = datetime.datetime.fromtimestamp((slot + VALID_MIN) * 60).strftime("%H:%M %d/%m/%Y")
    print(f"{code_at(key, slot)}  (valid until {until}, local time)")


if __name__ == "__main__":
    main()
