# Giao thức MQTT

Thỏa thuận chung giữa firmware, web (`web/`) và broker. Đổi topic hoặc payload thì sửa file này trước.

## 1. An toàn

Không có cách nào mở cửa qua mạng. Chỉ mở tại chỗ bằng keypad / thẻ / vân tay. Ràng buộc này giữ ở 3 nơi:

1. Firmware: không có topic mở cửa. Component dùng `mqtt_conn` không được `REQUIRES lock_service`.
2. Broker: tài khoản người dùng chỉ được publish `history/req`, `voice/in` và `voice/req`.
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
| `voice/index` | thiết bị → web | có | JSON danh sách tin của khách còn trên khóa |
| `voice/req` | web → thiết bị | không | JSON xin gửi lại / xóa một tin của khách, tối đa 64 byte |
| `voice/in` | web → thiết bị | không | Tin nhắn thoại chủ nhà gửi cho khách, tối đa 4112 byte mỗi chunk |
| `battery` | thiết bị → web | có | JSON dung lượng pin, điện áp, dòng điện |

## 3. Payload

`event`

```json
{"id": 1024, "ts": 1790000000, "method": "pin", "result": "granted", "user": "Dat"}
```

`method`: `pin`, `nfc`, `fingerprint`, `button` (nút bên trong), `key` (chìa cơ). `result`: `granted`, `denied`. `user` bỏ đi nếu
không biết. `id` tăng dần, dùng để phân trang.

`alert`

```json
{"id": 88, "ts": 1790000123, "level": "warning", "code": "pin_bruteforce", "msg": "Too many wrong tries: PIN locked for 30 s, the other methods still work"}
```

`level`: `info`, `warning`, `critical`. `code`: `pin_bruteforce` (sai quá nhiều lần với PIN, thẻ, vân tay hoặc
đăng nhập admin; `msg` nói cái nào bị khóa và bao lâu, `critical` khi khóa hẳn), `unknown_card`, `door_ajar`, `tamper`, `low_battery`,
`power_lost`. `msg` tùy chọn. Cảnh báo `warning` / `critical` gửi thêm qua bot Telegram (`telegram/`, đọc `alert`,
`event` và `voice/out` bằng tài khoản chỉ đọc).

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

`battery` (retained)

```json
{"ts": 1790000000, "level": "normal", "pct": 82, "mv": 11850, "ma": 120, "charging": false}
{"ts": 1790000000, "level": "unknown"}
```

Đo bằng INA219 (shunt 0,1 Ω) trên pack 3S 2600 mAh. `pct`: 0 đến 100, đếm coulomb, chỉnh lại theo điện áp khi pack
nghỉ. `mv`: điện áp pack. `ma`: dương khi xả, âm khi sạc. `charging`: `ma` dưới -80. `level`: `normal`, `low` (dưới
20 %), `critical` (dưới 10 %), về `normal` khi lên 25 %. `unknown`: khóa không đọc được INA219, không có số liệu.

Thiết bị publish khi `pct`, `level` hoặc `charging` đổi, 5 phút một lần, và mỗi lần kết nối broker. Retained, nên web
vẫn thấy lần đo cuối khi khóa offline; xem `ts` để biết số liệu cũ hay mới. Pin yếu và sắp mất nguồn còn được báo qua
`alert` (`low_battery`, `power_lost`).

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

Tin dài tối đa khoảng 20 s, web giới hạn 15 s. Sau 30 s chưa nhận đủ chunk thì bỏ tin. Firmware chỉ nhận
`sample_rate` 16000.

Hai chiều, hai loại tin, không trộn lẫn:

- **Tin của khách → chủ nhà.** Khách giữ `*` ở Home để ghi (tối đa 15 s), nghe lại, rồi `#` để gửi. Khóa lưu vào thẻ SD (10 tin gần nhất, tin cũ
  nhất bị thay), gửi ngay lên `voice/out` với `msg_id` = `id` của tin, rồi publish lại `voice/index`. Khóa không bao
  giờ phát tin của khách, nên khách sau không nghe được tin của khách trước.
- **Tin của chủ nhà → khách.** Gửi từ web qua `voice/in`, hoặc admin ghi tại cửa (menu admin, mục 4). Khóa giữ 5 tin
  (tin từ web thay tin cũ nhất khi đầy). Khách giữ `#` ở Home: 1 tin thì phát luôn, nhiều tin thì hiện danh sách.

`voice/index` (retained), mới nhất trước:

```json
{"items": [{"id": 3735928559, "ts": 1790000000, "ms": 8400}]}
```

`id`: số ngẫu nhiên 32 bit, không lặp lại sau khi khởi động lại. `ts`: 0 nếu khóa chưa đồng bộ giờ. `ms`: độ dài.

`voice/req`:

```json
{"op": "get", "id": 3735928559}
{"op": "del", "id": 3735928559}
```

`get`: khóa gửi lại tin đó trên `voice/out`. `del`: khóa xóa tin rồi publish lại `voice/index`. Web lưu tin đã nhận
trong trình duyệt (IndexedDB), nên tin gửi lúc trang đóng vẫn tải về được khi mở lại, và vẫn còn sau khi khóa đã thay
tin đó. Khóa gửi từng chunk và chờ broker xác nhận, nên một tin 15 s mất vài giây; khách bấm phím thì khóa dừng gửi,
web tự xin lại sau.

## 5. Broker

Cloud (mặc định): EMQX Cloud Serverless. Thiết bị kết nối `mqtts://<address>:8883`, xác thực bằng certificate bundle
của ESP-IDF. Web kết nối `wss://<address>:8084/mqtt`. Quyền theo topic cho từng tài khoản: `docs/deploy_web.md` mục 3.
Giới hạn của broker: retained, Last Will và QoS 1 đều có; message tối đa 1 MB (chunk thoại 4112 byte).

Tự host: xem `broker/mosquitto/`. Thiết bị truyền CA của broker qua `p_ca_cert_pem`.
