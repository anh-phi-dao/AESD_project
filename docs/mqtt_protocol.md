# Giao thức MQTT – Smart Lock Door

Tài liệu này là hợp đồng chung giữa firmware (ESP32-S3), trang web (`web/`) và broker.
Khi đổi topic hoặc định dạng payload, sửa tài liệu này trước.

## Nguyên tắc an toàn

**Không có cách nào mở cửa qua mạng**, dù ở trong LAN hay từ xa. Người dùng chỉ mở cửa tại chỗ
(keypad / NFC). Ràng buộc này được giữ ở 3 tầng:

1. **Firmware:** không có topic lệnh mở cửa. Component nào dùng `mqtt_conn` thì **không được**
   `REQUIRES lock_service` trong CMake. `lock_service` chỉ phát sự kiện ra `system_controller`.
2. **Broker ACL:** tài khoản người dùng chỉ được publish vào `history/req` và `voice/in`.
3. **Web:** thư viện `LockClient` không có hàm mở cửa.

Ngoài ra:

- Thiết bị **bỏ qua mọi message retained gửi tới** (tránh việc request bị phát lại mỗi lần reconnect).
- Thiết bị chỉ subscribe các topic trong allowlist và bỏ các payload vượt giới hạn kích thước.
- Tài khoản broker của thiết bị và của người dùng là **hai tài khoản khác nhau**.

## Topic

Gốc topic: `lock/<device_id>/` (ví dụ `lock/door01/`).

| Topic | Hướng | QoS | Retain | Payload |
|---|---|---|---|---|
| `status` | thiết bị → web | 1 | có | `online` / `offline` (offline là Last Will) |
| `event` | thiết bị → web | 1 | không | JSON sự kiện ra/vào |
| `alert` | thiết bị → web | 1 | không | JSON cảnh báo |
| `history/req` | web → thiết bị | 1 | không | JSON yêu cầu lịch sử, tối đa 256 byte |
| `history/resp` | thiết bị → web | 1 | không | JSON một trang lịch sử |
| `voice/out` | thiết bị → web | 1 | không | chunk nhị phân, tin nhắn thoại của khách tại cửa |
| `voice/in` | web → thiết bị | 1 | không | chunk nhị phân, tin nhắn thoại chủ nhà gửi tới cửa (cần loa) |

Thời gian (`ts`) là Unix time tính bằng giây (UTC), nên thiết bị phải đồng bộ SNTP trước khi ghi log.

### `event`

```json
{"id": 1024, "ts": 1790000000, "method": "pin", "result": "granted", "user": "Dat"}
```

| Trường | Giá trị |
|---|---|
| `id` | số tăng dần, duy nhất trên thiết bị (dùng để phân trang lịch sử) |
| `method` | `pin`, `nfc`, `button` (nút mở từ bên trong), `key` (chìa cơ) |
| `result` | `granted`, `denied` |
| `user` | tên người dùng nếu xác định được, không có thì bỏ trường |

### `alert`

```json
{"id": 88, "ts": 1790000123, "level": "critical", "code": "pin_bruteforce", "msg": "Sai PIN 5 lần liên tiếp"}
```

| Trường | Giá trị |
|---|---|
| `level` | `info`, `warning`, `critical` |
| `code` | `pin_bruteforce`, `unknown_card`, `door_ajar`, `tamper`, `low_battery`, `power_lost` |
| `msg` | mô tả ngắn cho người đọc (tùy chọn) |

Mọi `alert` mức `warning`/`critical` cũng được gửi qua Telegram bot, vì trang web chỉ nhận
MQTT khi đang mở.

### `history/req` và `history/resp`

```json
{"req_id": "k3j9x2", "before_id": 1000, "limit": 20}
```

- `before_id`: lấy các sự kiện có `id < before_id`; bỏ trường này để lấy trang mới nhất.
- `limit`: 1..50. Thiết bị tự giới hạn lại và nên bỏ qua request nếu nhận quá 1 request/giây.

```json
{"req_id": "k3j9x2", "items": [ /* các object event, mới nhất trước */ ], "more": true}
```

`req_id` được trả lại nguyên vẹn để web ghép đúng response với request.

### `voice/out` và `voice/in`

Mỗi tin nhắn thoại được chia thành nhiều chunk. Mỗi chunk gồm header 16 byte (little-endian)
và phần dữ liệu âm thanh:

| Offset | Kích thước | Trường | Ghi chú |
|---|---|---|---|
| 0 | 2 | magic | `'V' 'M'` (0x56 0x4D) |
| 2 | 1 | version | `1` |
| 3 | 1 | codec | `0` = PCM 16-bit LE mono |
| 4 | 4 | msg_id | uint32, ngẫu nhiên, giống nhau cho mọi chunk của một tin |
| 8 | 2 | seq | uint16, bắt đầu từ 0 |
| 10 | 2 | total | uint16, tổng số chunk, 1..160 |
| 12 | 4 | sample_rate | uint32, khuyến nghị 16000 |
| 16 | ≤ 4096 | data | độ dài chẵn |

- Ở 16 kHz, 160 chunk × 4096 byte tương đương khoảng 20 giây. Web giới hạn tin gửi đi còn 15 giây.
- Bên nhận bỏ tin nếu sau 30 giây chưa nhận đủ chunk.
- INMP441 chỉ là micro, nên chiều `voice/out` làm được ngay. Chiều `voice/in` cần thêm loa
  (ví dụ MAX98357A qua I2S); khi chưa có loa, firmware chưa cần subscribe `voice/in`.

## Broker

### Cloud (mặc định)

- Chọn broker cho phép **phân quyền theo topic cho từng tài khoản** (ACL). Kiểm tra lại trong gói
  miễn phí trước khi chọn, vì không phải gói nào cũng hỗ trợ.
- Thiết bị kết nối `mqtts://<host>:8883`, xác thực chứng chỉ bằng certificate bundle của ESP-IDF
  (`p_ca_cert_pem = NULL`).
- Web kết nối `wss://<host>:<port>/mqtt` (đường dẫn WebSocket tùy từng broker).

### Tự host Mosquitto (tùy chọn)

Xem `broker/mosquitto/`. Thiết bị truyền CA của broker qua `p_ca_cert_pem`.
