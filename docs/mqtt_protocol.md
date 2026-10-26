# Giao thức MQTT

Thỏa thuận chung giữa firmware, web (`web/`) và broker. Đổi topic hoặc payload thì sửa file này trước.

## 1. An toàn

Không có cách nào mở cửa qua mạng. Chỉ mở tại chỗ bằng keypad / thẻ / vân tay. Ràng buộc này giữ ở 3 nơi:

1. Firmware: không có topic mở cửa. Component dùng `mqtt_conn` không được `REQUIRES lock_service`.
2. Broker: tài khoản người dùng chỉ được publish `history/req` và `voice/in`.
3. Web: `LockClient` không có hàm mở cửa.

Thiết bị bỏ qua message retained gửi tới, chỉ subscribe topic trong allowlist, bỏ payload quá cỡ. Thiết bị và người
dùng dùng hai tài khoản broker khác nhau.

## 2. Topic

Gốc: `lock/<device_id>/`, ví dụ `lock/door01/`. Tất cả QoS 1. `ts` là Unix time (giây, UTC), thiết bị phải đồng bộ
SNTP trước khi ghi log.

| Topic | Hướng | Retain | Payload |
|---|---|---|---|
| `status` | thiết bị → web | có | `online` / `offline` (Last Will) |
| `event` | thiết bị → web | không | JSON sự kiện ra vào |
| `alert` | thiết bị → web | không | JSON cảnh báo |
| `recent` | thiết bị → web | có | JSON sự kiện và cảnh báo gần nhất |
| `history/req` | web → thiết bị | không | JSON yêu cầu lịch sử, tối đa 256 byte |
| `history/resp` | thiết bị → web | không | JSON một trang lịch sử |
| `voice/out` | thiết bị → web | không | Tin nhắn thoại của khách tại cửa |
| `voice/in` | web → thiết bị | không | Tin nhắn thoại chủ nhà gửi tới cửa (cần loa) |

## 3. Payload

`event`

```json
{"id": 1024, "ts": 1790000000, "method": "pin", "result": "granted", "user": "Dat"}
```

`method`: `pin`, `nfc`, `fingerprint`, `button` (nút bên trong), `key` (chìa cơ). `result`: `granted`, `denied`. `user` bỏ đi nếu
không biết. `id` tăng dần, dùng để phân trang.

`alert`

```json
{"id": 88, "ts": 1790000123, "level": "critical", "code": "pin_bruteforce", "msg": "Sai PIN 5 lần liên tiếp"}
```

`level`: `info`, `warning`, `critical`. `code`: `pin_bruteforce`, `unknown_card`, `door_ajar`, `tamper`, `low_battery`,
`power_lost`. `msg` tùy chọn. Cảnh báo `warning` / `critical` gửi thêm qua Telegram bot.

`recent`

```json
{"gen": 3554416255, "events": [/* mới nhất trước */], "alerts": [/* mới nhất trước */]}
```

Retained, nên web mở ra là có ngay dữ liệu, kể cả khi khóa đang ngoại tuyến. Thiết bị publish lại sau mỗi sự kiện,
mỗi lần kết nối broker và khi xóa lịch sử. Tối đa 20 sự kiện và 10 cảnh báo.

`gen` chỉ đổi khi lịch sử trên khóa bị xóa. Web thấy `gen` khác thì xóa lịch sử đang hiện. `id` của sự kiện và cảnh
báo là hai dãy riêng, không bao giờ dùng lại (lưu trong NVS).

`history/req` và `history/resp`

```json
{"req_id": "k3j9x2", "before_id": 1000, "limit": 20}
{"req_id": "k3j9x2", "items": [/* event, mới nhất trước */], "more": true}
```

`before_id`: lấy sự kiện có `id` nhỏ hơn, bỏ trống để lấy trang mới nhất. `limit`: 1 đến 50, mặc định 20. `req_id`:
1 đến 16 ký tự `[A-Za-z0-9_-]`, được trả lại nguyên vẹn. Request sai định dạng hoặc quá 4 request/giây bị bỏ qua.

## 4. Tin nhắn thoại

Mỗi tin chia thành nhiều chunk. Mỗi chunk có header 16 byte (little-endian) rồi tới dữ liệu âm thanh:

| Offset | Byte | Trường | Giá trị |
|---|---|---|---|
| 0 | 2 | magic | `'V' 'M'` |
| 2 | 1 | version | `1` |
| 3 | 1 | codec | `0` = PCM 16-bit mono |
| 4 | 4 | msg_id | Ngẫu nhiên, giống nhau cho mọi chunk của một tin |
| 8 | 2 | seq | Bắt đầu từ 0 |
| 10 | 2 | total | Tổng số chunk, 1 đến 160 |
| 12 | 4 | sample_rate | 16000 |
| 16 | tối đa 4096 | data | Độ dài chẵn |

Tin dài tối đa khoảng 20 s, web giới hạn 15 s. Sau 30 s chưa nhận đủ chunk thì bỏ tin. Chưa có loa thì firmware chưa
cần subscribe `voice/in`.

## 5. Broker

Cloud (mặc định): thiết bị kết nối `mqtts://<host>:8883`, xác thực bằng certificate bundle của ESP-IDF. Web kết nối
`wss://<host>:<port>/mqtt`. Nên chọn broker phân quyền được theo topic cho từng tài khoản.

Tự host: xem `broker/mosquitto/`. Thiết bị truyền CA của broker qua `p_ca_cert_pem`.
