"""Offline tests: chunk parsing, voice assembly, message texts and the sent-id state. No broker, no Telegram.

    python -m unittest test_telegram_bridge.py
"""

import os
import struct
import tempfile
import unittest
import wave
import io

import telegram_bridge as tb


def chunk(msg_id, seq, total, data=b"\x01\x00" * 4, rate=16000, magic=b"VM", version=1, codec=0):
    return struct.pack("<2sBBIHHI", magic, version, codec, msg_id, seq, total, rate) + data


class VoiceChunkTest(unittest.TestCase):
    def test_valid_chunk(self):
        c = tb.parse_voice_chunk(chunk(0xDEADBEEF, 1, 3))
        self.assertEqual((c["msg_id"], c["seq"], c["total"], c["rate"]), (0xDEADBEEF, 1, 3, 16000))
        self.assertEqual(c["data"], b"\x01\x00" * 4)

    def test_rejects_bad_chunks(self):
        for bad in (
            chunk(1, 0, 1, magic=b"XX"),
            chunk(1, 0, 1, version=2),
            chunk(1, 0, 1, codec=1),
            chunk(1, 3, 3),  # seq >= total
            chunk(1, 0, 0),
            chunk(1, 0, 161),
            chunk(1, 0, 1, rate=4000),
            chunk(1, 0, 1, data=b"\x01\x02\x03"),  # odd length
            chunk(1, 0, 1, data=b"\x00" * 4098),  # too long
            chunk(1, 0, 1, data=b""),  # header only
        ):
            self.assertIsNone(tb.parse_voice_chunk(bad))


class AssemblerTest(unittest.TestCase):
    def test_out_of_order_and_duplicates(self):
        a = tb.VoiceAssembler()
        parts = [tb.parse_voice_chunk(chunk(7, i, 3, data=bytes([i, 0]) * 2)) for i in range(3)]
        self.assertIsNone(a.push(parts[2]))
        self.assertIsNone(a.push(parts[0]))
        self.assertIsNone(a.push(parts[0]))  # The same chunk twice counts once.
        msg_id, pcm, rate = a.push(parts[1])
        self.assertEqual((msg_id, rate), (7, 16000))
        self.assertEqual(pcm, b"\x00\x00\x00\x00\x01\x00\x01\x00\x02\x00\x02\x00")

    def test_mismatch_drops_message(self):
        a = tb.VoiceAssembler()
        self.assertIsNone(a.push(tb.parse_voice_chunk(chunk(9, 0, 2))))
        self.assertIsNone(a.push(tb.parse_voice_chunk(chunk(9, 1, 3))))  # Other total: dropped.
        self.assertIsNone(a.push(tb.parse_voice_chunk(chunk(9, 1, 2))))  # Starts over, still incomplete.

    def test_timeout(self):
        now = [0.0]
        a = tb.VoiceAssembler(timeout_s=30, clock=lambda: now[0])
        a.push(tb.parse_voice_chunk(chunk(5, 0, 2)))
        now[0] = 31.0
        self.assertIsNone(a.push(tb.parse_voice_chunk(chunk(5, 1, 2))))  # Chunk 0 expired.

    def test_wav(self):
        wav = tb.pcm_to_wav(b"\x00\x01" * 160, 16000)
        with wave.open(io.BytesIO(wav)) as w:
            self.assertEqual((w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()), (1, 2, 16000, 160))


class TextTest(unittest.TestCase):
    TS = 1790000000  # 2026-09-21 14:13:20 UTC, 21:13 in Vietnam (UTC+7)

    def test_alert(self):
        text = tb.format_alert({"id": 1, "ts": self.TS, "level": "critical", "code": "pin_bruteforce",
                                "msg": "Too many wrong tries: PIN locked until an admin login"}, "door01", 7)
        self.assertEqual(text, "🚨 Cảnh báo nghiêm trọng: Thử sai nhiều lần\n"
                               "Too many wrong tries: PIN locked until an admin login\n"
                               "21:13 21/09/2026 · door01")

    def test_alert_info_skipped(self):
        self.assertIsNone(tb.format_alert({"id": 1, "ts": self.TS, "level": "info", "code": "tamper"}, "door01", 7))

    def test_access(self):
        text = tb.format_access({"id": 3, "ts": self.TS, "method": "fingerprint", "result": "granted", "user": "Dat"},
                                "door01", 7)
        self.assertEqual(text, "🔓 Dat đã mở cửa bằng vân tay\n21:13 21/09/2026 · door01")
        self.assertIsNone(tb.format_access({"id": 4, "ts": self.TS, "method": "pin", "result": "denied"}, "d", 7))
        self.assertTrue(tb.format_access({"id": 5, "ts": self.TS, "method": "nfc", "result": "granted"}, "d", 7)
                        .startswith("🔓 Ai đó đã mở cửa bằng thẻ NFC"))
        self.assertTrue(tb.format_access({"id": 6, "ts": self.TS, "method": "pin", "result": "granted",
                                          "user": "Temp code"}, "d", 7)
                        .startswith("🔓 Khách (mã tạm) đã mở cửa bằng mã PIN"))


class StateTest(unittest.TestCase):
    def test_persists(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "state.json")
            s = tb.SentState(path)
            s.mark("alert", 12)
            s.mark("voice", 0xDEADBEEF)
            again = tb.SentState(path)
            self.assertTrue(again.seen("alert", 12))
            self.assertTrue(again.seen("voice", 0xDEADBEEF))
            self.assertFalse(again.seen("event", 12))


class BridgeTest(unittest.TestCase):
    def test_routes_and_dedupes(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = {"device_id": "door01", "chat_ids": [1]}
            state = tb.SentState(os.path.join(tmp, "state.json"))
            bridge = tb.Bridge(cfg, None, state)
            bridge.on_message("lock/door01/alert", b'{"id":2,"ts":1,"level":"warning","code":"unknown_card"}')
            bridge.on_message("lock/door01/event", b'{"id":3,"ts":1,"method":"pin","result":"granted","user":"Phi"}')
            bridge.on_message("lock/door01/event", b'{"id":4,"ts":1,"method":"pin","result":"denied"}')
            bridge.on_message("lock/door02/alert", b'{"id":9,"ts":1,"level":"critical","code":"tamper"}')
            bridge.on_message("lock/door01/alert", b"not json")
            bridge.on_message("lock/door01/voice/out", chunk(42, 0, 1))
            jobs = [bridge.jobs.get_nowait() for _ in range(bridge.jobs.qsize())]
            self.assertEqual([(j[1], j[2]) for j in jobs], [("alert", 2), ("event", 3), ("voice", 42)])

            state.mark("alert", 2)
            bridge.on_message("lock/door01/alert", b'{"id":2,"ts":1,"level":"warning","code":"unknown_card"}')
            self.assertTrue(bridge.jobs.empty())

    def test_retry_only_failed_chats(self):
        class FlakyTelegram:
            def __init__(self):
                self.sent = []
                self.failures = {222: 1}  # The second chat fails once.

            def call(self, method, fields, file=None):
                chat = fields["chat_id"]
                if self.failures.get(chat, 0) > 0:
                    self.failures[chat] -= 1
                    raise RuntimeError("Telegram sendMessage: 502 Bad Gateway")
                self.sent.append(chat)

        with tempfile.TemporaryDirectory() as tmp:
            telegram = FlakyTelegram()
            state = tb.SentState(os.path.join(tmp, "state.json"))
            bridge = tb.Bridge({"device_id": "door01", "chat_ids": [111, 222]}, telegram, state)
            self.assertTrue(bridge.send_job(("text", "event", 7, "hello"), sleep=lambda s: None))
            self.assertEqual(telegram.sent, [111, 222])  # 111 once, not again on the retry.
            self.assertTrue(state.seen("event", 7))

            telegram.failures = {111: 99}
            self.assertFalse(bridge.send_job(("text", "event", 8, "hi"), retries=2, sleep=lambda s: None))
            self.assertFalse(state.seen("event", 8))  # Not marked: sent again if it comes back.


if __name__ == "__main__":
    unittest.main()
