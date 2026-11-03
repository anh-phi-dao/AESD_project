# Telegram bot

Chương trình nhỏ chạy trên một máy luôn bật (PC, Raspberry Pi, VPS). Nó nghe broker MQTT bằng một tài khoản chỉ đọc
rồi gửi sang Telegram:

| Từ khóa | Tin Telegram |
|---|---|
| `alert` mức `warning` / `critical` | `🚨 Cảnh báo nghiêm trọng: Thử sai nhiều lần` + nội dung + giờ |
| `event` mở cửa thành công | `🔓 Dat đã mở cửa bằng vân tay` + giờ |
| `voice/out` (tin thoại của khách) | File âm thanh, kèm độ dài |

```
ESP32 --MQTT--> EMQX --MQTT--> telegram_bridge.py --HTTPS--> Telegram
```

Firmware không đổi gì và không tốn thêm RAM trên ESP32.

**Bot chỉ gửi, không nhận lệnh.** Nó không đọc tin nhắn ai gửi cho bot, và tài khoản broker của nó không được
publish, nên không có cách nào mở cửa qua Telegram (quy tắc ở `docs/mqtt_protocol.md` mục 1).

## 1. Tạo bot

1. Trong Telegram, nhắn [@BotFather](https://t.me/BotFather): `/newbot`, đặt tên, nhận **token** dạng
   `123456789:AA...`. Ai có token là điều khiển được bot: không đưa lên git, không gửi cho ai.
2. Mở bot vừa tạo, nhấn **Start** (hoặc thêm bot vào nhóm gia đình rồi nhắn một tin trong nhóm).

## 2. Tài khoản broker cho bot

Nên dùng tài khoản riêng, chỉ đọc. EMQX Cloud → Access Control:

- Authentication: thêm `bridge_door01` với mật khẩu riêng.
- Authorization: `bridge_door01`, topic `lock/door01/#`, **Subscribe**, Allow. Không cho Publish.

Tự host: dùng khối `bridge_door01` trong `broker/mosquitto/acl.example`.

## 3. Cài và chạy

Python 3.9 trở lên.

```sh
cd telegram
python -m venv .venv
.venv\Scripts\activate          # Linux / macOS: source .venv/bin/activate
pip install -r requirements.txt
copy config.example.json config.json   # Linux / macOS: cp
```

Sửa `config.json`: `broker_url`, `username`, `password`, `device_id`, `bot_token`. Rồi lấy `chat_id`:

```sh
python telegram_bridge.py --find-chat   # in ra chat_id của những ai vừa nhắn cho bot
```

Điền vào `chat_ids` (nhiều người hoặc một nhóm đều được: `[111, -1002223334]`), rồi:

```sh
python telegram_bridge.py --test        # bot gửi "✅ Bot của khóa door01 đã kết nối."
python telegram_bridge.py               # chạy thật
```

Thử mà không gửi Telegram: `python telegram_bridge.py --dry-run` in ra tin sẽ gửi (không cần `bot_token`).

**Tin thoại:** có [ffmpeg](https://ffmpeg.org/) trong PATH (hoặc ghi đường dẫn vào `"ffmpeg"` trong `config.json`) thì
gửi thành tin voice nghe ngay trong chat. Không có thì gửi file WAV.

## 4. Không mất tin

- Bridge dùng phiên MQTT lưu lâu (client id cố định, giữ 24 giờ). Bridge tắt hoặc mất mạng thì broker giữ lại tin
  QoS 1, bật lại là gửi tiếp.
- `state.json` nhớ id đã gửi, nên khóa gửi lại một tin (ví dụ web xin lại tin thoại) thì Telegram không bị trùng.
- Gửi Telegram lỗi thì thử lại 4 lần (1, 2, 4, 8 s).

## 5. Chạy nền

- Windows: Task Scheduler → Create Task → Trigger "At startup" → Action
  `telegram\.venv\Scripts\pythonw.exe telegram_bridge.py`, "Start in" là thư mục `telegram`.
- Linux (systemd), `/etc/systemd/system/lock-telegram.service`:

  ```ini
  [Unit]
  Description=Smart lock Telegram bridge
  After=network-online.target

  [Service]
  WorkingDirectory=/opt/AESD_project/telegram
  ExecStart=/opt/AESD_project/telegram/.venv/bin/python telegram_bridge.py
  Restart=always

  [Install]
  WantedBy=multi-user.target
  ```

  `sudo systemctl enable --now lock-telegram`, xem log: `journalctl -u lock-telegram -f`.

## 6. Kiểm tra

```sh
python -m unittest test_telegram_bridge.py
```

Kiểm tra cách đọc chunk thoại, ghép tin, nội dung tin nhắn và việc chống gửi trùng, không cần broker hay Telegram.
