"""Forwards what the lock publishes on MQTT to Telegram: alerts, who opened the door, and guests' voice messages.

Send-only by design: the bot reads no commands, and nothing it receives reaches the lock (docs/mqtt_protocol.md,
section 1). It listens with a read-only broker account, so it cannot publish to the lock either.

    python telegram_bridge.py                 # run with config.json next to this file
    python telegram_bridge.py --find-chat     # list the chats that wrote to the bot, to fill chat_ids
    python telegram_bridge.py --test          # send one test message to every chat_id
    python telegram_bridge.py --dry-run       # print the messages instead of sending them (no bot token needed)

See README.md for the setup.
"""

import argparse
import datetime
import io
import json
import logging
import os
import queue
import shutil
import struct
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CONFIG = os.path.join(HERE, "config.json")
DEFAULT_STATE = os.path.join(HERE, "state.json")

LOG = logging.getLogger("telegram_bridge")

# Voice chunk format of docs/mqtt_protocol.md section 4 (little-endian 16-byte header, then PCM 16-bit mono).
VOICE_HEADER = struct.Struct("<2sBBIHHI")
VOICE_MAGIC = b"VM"
VOICE_VERSION = 1
VOICE_CODEC_PCM16 = 0
VOICE_CHUNK_DATA_MAX = 4096
VOICE_MAX_CHUNKS = 160
VOICE_ASSEMBLY_TIMEOUT_S = 30.0

SESSION_EXPIRY_S = 86400  # The broker keeps QoS 1 messages this long while the bridge is down.
STATE_KEEP = 200  # Ids remembered per kind, so a message sent again is not forwarded twice.
SEND_RETRIES = 4

# Same wording as the web page (web/js/app.js).
METHOD_LABEL = {
    "pin": "mã PIN",
    "nfc": "thẻ NFC",
    "fingerprint": "vân tay",
    "button": "nút bên trong",
    "key": "chìa cơ",
}
ALERT_LABEL = {
    "pin_bruteforce": "Thử sai nhiều lần",  # Any method now: the msg says which one is locked.
    "unknown_card": "Quẹt thẻ lạ",
    "door_ajar": "Cửa mở quá lâu",
    "tamper": "Phát hiện cạy phá",
    "low_battery": "Pin yếu",
    "power_lost": "Mất nguồn",
}
ALERT_ICON = {"critical": "🚨", "warning": "⚠️"}


# ------------------------------------------------------------------------------------------------ payloads


def parse_voice_chunk(data):
    """The chunk as a dict, or None when it is not a valid version-1 PCM chunk."""
    if len(data) <= VOICE_HEADER.size:
        return None
    magic, version, codec, msg_id, seq, total, rate = VOICE_HEADER.unpack_from(data)
    payload = bytes(data[VOICE_HEADER.size :])
    valid = (
        magic == VOICE_MAGIC
        and version == VOICE_VERSION
        and codec == VOICE_CODEC_PCM16
        and 1 <= total <= VOICE_MAX_CHUNKS
        and seq < total
        and 8000 <= rate <= 48000
        and len(payload) <= VOICE_CHUNK_DATA_MAX
        and len(payload) % 2 == 0
    )
    if not valid:
        return None
    return {"msg_id": msg_id, "seq": seq, "total": total, "rate": rate, "data": payload}


class VoiceAssembler:
    """Collects chunks per msg_id. push() returns (msg_id, pcm, rate) once every chunk of a message arrived."""

    def __init__(self, timeout_s=VOICE_ASSEMBLY_TIMEOUT_S, clock=time.monotonic):
        self._pending = {}
        self._timeout_s = timeout_s
        self._clock = clock

    def push(self, chunk):
        now = self._clock()
        for msg_id in [m for m, p in self._pending.items() if now - p["first"] > self._timeout_s]:
            LOG.warning("Voice message %08x incomplete after %.0f s, dropped", msg_id, self._timeout_s)
            del self._pending[msg_id]

        msg = self._pending.get(chunk["msg_id"])
        if msg is None:
            msg = {"total": chunk["total"], "rate": chunk["rate"], "parts": {}, "first": now}
            self._pending[chunk["msg_id"]] = msg
        if chunk["total"] != msg["total"] or chunk["rate"] != msg["rate"]:
            del self._pending[chunk["msg_id"]]  # Two messages with one id: neither can be trusted.
            return None
        msg["parts"][chunk["seq"]] = chunk["data"]
        if len(msg["parts"]) < msg["total"]:
            return None
        del self._pending[chunk["msg_id"]]
        pcm = b"".join(msg["parts"][i] for i in range(msg["total"]))
        return chunk["msg_id"], pcm, msg["rate"]


def pcm_to_wav(pcm, rate):
    out = io.BytesIO()
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    return out.getvalue()


def wav_to_ogg(wav, ffmpeg):
    """OGG/Opus for a Telegram voice bubble, or None when ffmpeg fails."""
    try:
        done = subprocess.run(
            [ffmpeg, "-hide_banner", "-loglevel", "error", "-f", "wav", "-i", "pipe:0",
             "-c:a", "libopus", "-b:a", "24k", "-f", "ogg", "pipe:1"],
            input=wav, capture_output=True, timeout=30, check=True,
        )
        return done.stdout or None
    except (OSError, subprocess.SubprocessError) as err:
        LOG.warning("ffmpeg failed (%s), sending WAV instead", err)
        return None


def fmt_time(ts, utc_offset_hours):
    if not isinstance(ts, int) or ts <= 0:
        ts = int(time.time())
    tz = datetime.timezone(datetime.timedelta(hours=utc_offset_hours))
    return datetime.datetime.fromtimestamp(ts, tz).strftime("%H:%M %d/%m/%Y")


def format_alert(alert, device_id, utc_offset_hours):
    """Text for a warning / critical alert, None for anything else (info alerts stay on the web page)."""
    level = alert.get("level")
    if level not in ALERT_ICON:
        return None
    code = alert.get("code") if isinstance(alert.get("code"), str) else "unknown"
    lines = [f"{ALERT_ICON[level]} {'Cảnh báo nghiêm trọng' if level == 'critical' else 'Cảnh báo'}: "
             f"{ALERT_LABEL.get(code, code)}"]
    if isinstance(alert.get("msg"), str) and alert["msg"]:
        lines.append(alert["msg"][:200])
    lines.append(f"{fmt_time(alert.get('ts'), utc_offset_hours)} · {device_id}")
    return "\n".join(lines)


def format_access(event, device_id, utc_offset_hours):
    """Text for a granted access, None for a denied one (repeated failures arrive as alerts)."""
    if event.get("result") != "granted":
        return None
    user = event.get("user") if isinstance(event.get("user"), str) and event.get("user") else "Ai đó"
    method = event.get("method") if isinstance(event.get("method"), str) else "unknown"
    return (f"🔓 {user[:64]} đã mở cửa bằng {METHOD_LABEL.get(method, method)}\n"
            f"{fmt_time(event.get('ts'), utc_offset_hours)} · {device_id}")


# ------------------------------------------------------------------------------------------------ state


class SentState:
    """Ids already forwarded, per kind, kept in state.json so a restart does not send them again."""

    def __init__(self, path):
        self._path = path
        self._lock = threading.Lock()
        self._ids = {"alert": [], "event": [], "voice": []}
        try:
            with open(path, encoding="utf-8") as f:
                saved = json.load(f)
            for kind in self._ids:
                self._ids[kind] = [i for i in saved.get(kind, []) if isinstance(i, int)][-STATE_KEEP:]
        except FileNotFoundError:
            pass
        except (OSError, ValueError) as err:
            LOG.warning("Ignoring unreadable %s: %s", path, err)

    def seen(self, kind, item_id):
        with self._lock:
            return item_id in self._ids[kind]

    def mark(self, kind, item_id):
        with self._lock:
            if item_id in self._ids[kind]:
                return
            self._ids[kind] = (self._ids[kind] + [item_id])[-STATE_KEEP:]
            tmp = self._path + ".tmp"
            try:
                with open(tmp, "w", encoding="utf-8") as f:
                    json.dump(self._ids, f)
                os.replace(tmp, self._path)
            except OSError as err:
                LOG.warning("Cannot save %s: %s", self._path, err)


# ------------------------------------------------------------------------------------------------ Telegram


class Telegram:
    """sendMessage / sendVoice / sendDocument with the standard library only."""

    def __init__(self, token, dry_run=False):
        self._base = f"https://api.telegram.org/bot{token}/"
        self._dry_run = dry_run

    def call(self, method, fields=None, file=None):
        """file: (field, filename, content_type, bytes). Returns the "result" of the answer."""
        if self._dry_run:
            size = f", {file[1]} {len(file[3])} bytes" if file else ""
            print(f"[dry-run] {method} {json.dumps(fields or {}, ensure_ascii=False)}{size}")
            return {}
        fields = {k: str(v) for k, v in (fields or {}).items()}
        if file is None:
            body = urllib.parse.urlencode(fields).encode()
            headers = {"Content-Type": "application/x-www-form-urlencoded"}
        else:
            boundary = uuid.uuid4().hex
            parts = []
            for key, value in fields.items():
                parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode())
            field, filename, content_type, data = file
            parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{field}"; filename="{filename}"\r\n'
                         f"Content-Type: {content_type}\r\n\r\n".encode() + data + b"\r\n")
            parts.append(f"--{boundary}--\r\n".encode())
            body = b"".join(parts)
            headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}

        request = urllib.request.Request(self._base + method, data=body, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                answer = json.load(response)
        except urllib.error.HTTPError as err:
            answer = json.loads(err.read() or b"{}")
            # Never log the URL: it holds the bot token.
            raise RuntimeError(f"Telegram {method}: {err.code} {answer.get('description', '')}") from None
        if not answer.get("ok"):
            raise RuntimeError(f"Telegram {method}: {answer.get('description', 'not ok')}")
        return answer.get("result")


# ------------------------------------------------------------------------------------------------ bridge


class Bridge:
    def __init__(self, cfg, telegram, state, ffmpeg=None):
        self.cfg = cfg
        self.telegram = telegram
        self.state = state
        self.ffmpeg = ffmpeg
        self.root = f"{cfg.get('topic_prefix', 'lock')}/{cfg['device_id']}/"
        self.voice = VoiceAssembler()
        self.jobs = queue.Queue()  # Sending runs in its own thread, so a slow upload never stalls the MQTT keepalive.

    # ---- MQTT side (paho network thread)

    def on_message(self, topic, payload):
        if not topic.startswith(self.root):
            return
        suffix = topic[len(self.root) :]
        offset = float(self.cfg.get("utc_offset_hours", 7))
        if suffix == "alert" and self.cfg.get("send_alerts", True):
            alert = _json(payload)
            if alert and isinstance(alert.get("id"), int) and not self.state.seen("alert", alert["id"]):
                text = format_alert(alert, self.cfg["device_id"], offset)
                if text:
                    self.jobs.put(("text", "alert", alert["id"], text))
        elif suffix == "event" and self.cfg.get("send_access", True):
            event = _json(payload)
            if event and isinstance(event.get("id"), int) and not self.state.seen("event", event["id"]):
                text = format_access(event, self.cfg["device_id"], offset)
                if text:
                    self.jobs.put(("text", "event", event["id"], text))
        elif suffix == "voice/out" and self.cfg.get("send_voice", True):
            chunk = parse_voice_chunk(payload)
            done = chunk and self.voice.push(chunk)
            if done and not self.state.seen("voice", done[0]):
                self.jobs.put(("voice", "voice", done[0], done[1:]))

    # ---- Telegram side (worker thread)

    def run_worker(self):
        while True:
            self.send_job(self.jobs.get())

    def send_job(self, job, retries=SEND_RETRIES, sleep=time.sleep):
        """Every chat gets the job once: a retry goes only to the chats that did not get it yet."""
        kind, state_kind, item_id, content = job
        send = self._text_sender(content) if kind == "text" else self._voice_sender(*content)
        pending = list(self.cfg["chat_ids"])
        for attempt in range(retries):
            for chat in list(pending):
                try:
                    send(chat)
                    pending.remove(chat)
                except (OSError, RuntimeError, ValueError) as err:
                    LOG.warning("Sending %s %s to %s failed (%s)", state_kind, item_id, chat, err)
            if not pending:
                self.state.mark(state_kind, item_id)
                LOG.info("Forwarded %s %s", state_kind, item_id)
                return True
            if attempt + 1 < retries:
                sleep(2 ** attempt)
        LOG.error("Gave up on %s %s for %s", state_kind, item_id, pending)
        return False

    def _text_sender(self, text):
        return lambda chat: self.telegram.call("sendMessage", {"chat_id": chat, "text": text})

    def _voice_sender(self, pcm, rate):
        seconds = len(pcm) / 2 / rate
        caption = (f"🎙️ Khách để lại lời nhắn tại cửa ({seconds:.0f} s)\n"
                   f"{fmt_time(None, float(self.cfg.get('utc_offset_hours', 7)))} · {self.cfg['device_id']}")
        wav = pcm_to_wav(pcm, rate)
        ogg = wav_to_ogg(wav, self.ffmpeg) if self.ffmpeg else None  # Encoded once for every chat.
        if ogg:
            return lambda chat: self.telegram.call(
                "sendVoice", {"chat_id": chat, "caption": caption, "duration": round(seconds)},
                ("voice", "message.ogg", "audio/ogg", ogg))
        return lambda chat: self.telegram.call(
            "sendDocument", {"chat_id": chat, "caption": caption}, ("document", "loi-nhan.wav", "audio/wav", wav))


def _json(payload):
    try:
        value = json.loads(payload)
    except (ValueError, UnicodeDecodeError):
        return None
    return value if isinstance(value, dict) else None


# ------------------------------------------------------------------------------------------------ main


def load_config(path, need_mqtt=True, need_bot=True):
    with open(path, encoding="utf-8") as f:
        cfg = json.load(f)
    needed = []
    if need_mqtt:
        needed += ["broker_url", "username", "password", "device_id"]
    if need_bot:
        needed += ["bot_token"]
    missing = [k for k in needed if not cfg.get(k)]
    if need_bot and need_mqtt and not cfg.get("chat_ids"):
        missing.append("chat_ids")
    if missing:
        sys.exit(f"{path}: missing {', '.join(missing)} (see config.example.json)")
    return cfg


def find_chat(cfg):
    """Chats that wrote to the bot lately: their ids go into chat_ids."""
    updates = Telegram(cfg["bot_token"]).call("getUpdates", {"timeout": 0})
    chats = {}
    for update in updates or []:
        message = update.get("message") or update.get("channel_post") or {}
        chat = message.get("chat")
        if chat:
            chats[chat["id"]] = chat.get("title") or " ".join(
                filter(None, [chat.get("first_name"), chat.get("last_name")])) or chat.get("username", "")
    if not chats:
        print("Chưa có chat nào. Mở bot trong Telegram, gửi /start (hoặc thêm bot vào nhóm và nhắn một tin), rồi chạy lại.")
    for chat_id, name in chats.items():
        print(f"chat_id {chat_id}  {name}")


def run(cfg, dry_run):
    import paho.mqtt.client as mqtt
    from paho.mqtt.packettypes import PacketTypes
    from paho.mqtt.properties import Properties

    url = urllib.parse.urlparse(cfg["broker_url"])
    if url.scheme not in ("mqtt", "mqtts") or not url.hostname:
        sys.exit("broker_url must look like mqtts://<address>:8883")
    port = url.port or (8883 if url.scheme == "mqtts" else 1883)

    if dry_run and not cfg.get("chat_ids"):
        cfg["chat_ids"] = ["dry-run"]
    ffmpeg = cfg.get("ffmpeg") or shutil.which("ffmpeg")
    LOG.info("Voice messages as %s", "Telegram voice (ffmpeg)" if ffmpeg else "WAV files (no ffmpeg found)")

    telegram = Telegram(cfg.get("bot_token", ""), dry_run=dry_run)
    bridge = Bridge(cfg, telegram, SentState(cfg.get("state_file", DEFAULT_STATE)), ffmpeg)
    threading.Thread(target=bridge.run_worker, name="telegram", daemon=True).start()

    # A fixed client id with a persistent session: what the lock sends while the bridge is down waits on the broker.
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"tg-bridge-{cfg['device_id']}",
                         protocol=mqtt.MQTTv5)
    client.username_pw_set(cfg["username"], cfg["password"])
    if url.scheme == "mqtts":
        client.tls_set(ca_certs=cfg.get("ca_file") or None)
    client.reconnect_delay_set(min_delay=1, max_delay=60)

    topics = [(bridge.root + t, 1) for t in ("alert", "event", "voice/out")]

    def on_connect(c, _userdata, _flags, reason_code, _props):
        if reason_code.is_failure:
            LOG.error("Broker refused the connection: %s", reason_code)
            return
        c.subscribe(topics)
        LOG.info("Connected to %s, listening on %s#", url.hostname, bridge.root)

    def on_disconnect(_c, _userdata, _flags, reason_code, _props):
        LOG.warning("Disconnected from the broker (%s), reconnecting", reason_code)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = lambda _c, _u, msg: bridge.on_message(msg.topic, msg.payload)

    props = Properties(PacketTypes.CONNECT)
    props.SessionExpiryInterval = SESSION_EXPIRY_S
    client.connect(url.hostname, port, keepalive=60, clean_start=False, properties=props)
    client.loop_forever(retry_first_connection=True)


def main():
    parser = argparse.ArgumentParser(description="Smart lock -> Telegram bridge (send-only).")
    parser.add_argument("-c", "--config", default=DEFAULT_CONFIG, help="Config file (default: config.json here)")
    parser.add_argument("--find-chat", action="store_true", help="List chats that wrote to the bot")
    parser.add_argument("--test", action="store_true", help="Send a test message to every chat_id")
    parser.add_argument("--dry-run", action="store_true", help="Print messages instead of sending them")
    args = parser.parse_args()

    # Windows consoles and redirected output default to a code page without emoji or Vietnamese.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    if args.find_chat:
        find_chat(load_config(args.config, need_mqtt=False))
    elif args.test:
        cfg = load_config(args.config)
        for chat in cfg["chat_ids"]:
            Telegram(cfg["bot_token"]).call(
                "sendMessage", {"chat_id": chat, "text": f"✅ Bot của khóa {cfg['device_id']} đã kết nối."})
        print("Đã gửi tin thử.")
    else:
        run(load_config(args.config, need_bot=not args.dry_run), args.dry_run)


if __name__ == "__main__":
    main()
