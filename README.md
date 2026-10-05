# Smart Lock Door (ESP32-S3)

Khóa cửa điện tử dựa trên ESP32-S3: mở cửa tại chỗ bằng keypad/NFC, còn trang web cho chủ nhà
xem trạng thái, lịch sử ra vào, cảnh báo và tin nhắn thoại qua MQTT.

> **Nguyên tắc an toàn quan trọng nhất: không có cách nào mở cửa qua mạng.**
> Điều này áp dụng cho cả web lẫn MQTT, trong LAN hay từ xa. Mở cửa chỉ bằng keypad/NFC tại chỗ.
> Đừng thêm topic, API hay nút "mở cửa" nào. Chi tiết ở mục [Quy tắc khi viết code](#quy-tắc-khi-viết-code).

---

## Trạng thái hiện tại

| Phần | Trạng thái |
| WiFi + đồng bộ giờ SNTP (`app/network_manager`) | Xong, em đã chạy thử trên module |
| MQTT qua TLS (`middleware/mqtt_client`) | Xong, đã kết nối HiveMQ Cloud |
| Đọc credential từ NVS (`app/device_config`) | Xong |
| Trang web chỉ xem (`web/`) | Xong: trạng thái online/offline, lịch sử, cảnh báo (banner + âm báo, báo cả cảnh báo lỡ khi đóng trang), tin nhắn thoại |
| Lịch sử gần nhất (`app/event_log`) | Xong: 20 sự kiện + 10 cảnh báo, lưu NVS, giữ qua khởi động lại. Log đầy đủ chờ thẻ SD |
| Cầu nối MQTT (`app/remote_service`) | Xong: `event`, `alert`, `recent` (retained, xem được khi khóa offline), trả lời `history/req` |
| Giả lập phần cứng (`app/door_sim`) | Xong, **chỉ cho board dev, tắt mặc định**: nút BOOT + lệnh `sim` trên serial |
| Telegram bot | Chưa làm |
| Lưu log ra vào lên thẻ SD | Chưa làm |
| Tin nhắn thoại trên firmware (INMP441, loa) | Chưa làm; web đã sẵn sàng |
| Solenoid, NFC (PN532), màn hình, audio | Chưa làm |

---

## Cấu trúc project

```
AESD_project/
├── main/   Điểm vào: khởi tạo NVS, task mạng, keypad (đang test)
├── app/
│   ├── device_config/ NEW Đọc credential từ partition "devcfg"
│   ├── network_manager/  NEW  WiFi STA, tự kết nối lại, SNTP, chờ "có IP + đúng giờ"
│   ├── event_log/       NEW  Gán id, giữ sự kiện/cảnh báo gần nhất trong NVS, mã phiên gen
│   ├── remote_service/  NEW  Một task publish event/alert/recent, trả lời history/req
│   ├── door_sim/        NEW  Giả lập keypad/NFC/cảm biến cho board dev (nút BOOT, lệnh "sim")
│   ├── lock_service/ ...     (chưa có code)
├── device/                   Driver phần cứng (pcf8574, keypad, ...)
├── middleware/
│   ├── mqtt_client/   NEW  Bọc esp-mqtt: TLS, Last Will, allowlist topic, ghép payload lớn
│   └── ...
├── util/                     app_err_t, err_map
├── third_party/   NEW  Thư viện ngoài (hiện trống, giữ bằng .gitkeep)
├── web/   NEW  Trang web tĩnh: HTML + mqtt.js
│   └── js/
│       ├── lock-client.js         Thư viện MQTT chỉ xem (không đụng DOM)
│       ├── voice.js               Đóng gói/ghép chunk thoại, ghi âm → PCM 16 kHz
│       └── app.js                 Giao diện
├── broker/mosquitto/   NEW  Cấu hình Mosquitto tự host + ACL mẫu
├── config/    NEW  Mẫu credential; file thật bị gitignore
├── docs/
│   ├── mqtt_protocol.md     NEW  Topic, payload JSON, định dạng chunk thoại (hợp đồng firmware ↔ web)
│   └── provisioning.md      NEW  Nạp credential, xử lý lỗi, chuẩn bị sản phẩm
├── scripts/
│   ├── provision_device.py  NEW  Ghi credential vào board
│   └── ...                        format.sh, generate_config.py (có từ trước)
├── partitions.csv      NEW  Bảng partition 4 MB
├── sdkconfig.defaults  NEW  Target esp32s3, flash 4 MB, partition tùy chỉnh
└── .gitignore          NEW
```

---

## Những thay đổi cần biết khi pull về

1. **Target đổi từ `esp32` sang `esp32s3`.** Cấu hình gốc giờ nằm ở `sdkconfig.defaults`.
   Muốn đổi cấu hình lâu dài thì sửa file này, xóa `sdkconfig`, rồi build lại.
2. **Bảng partition mới** (`partitions.csv`):

   | Partition | Offset | Kích thước | Nội dung |
   |---|---|---|---|
   | `nvs` | 0x9000 | 24 KB | Dữ liệu WiFi, dữ liệu người dùng sau này (PIN, thẻ) |
   | `phy_init` | 0xF000 | 4 KB | Hiệu chuẩn RF |
   | `devcfg` | 0x10000 | 24 KB | Credential thiết bị (WiFi, broker, mã thiết bị) |
   | `factory` | 0x20000 | 3 MB | Firmware (hiện dùng khoảng 1 MB) |

   Lần đầu nạp firmware sau khi pull phải chạy `idf.py -p COMx erase-flash flash`.
3. **Thư mục `build/` không còn trong git.** Nếu build báo
   `CMakeCache.txt directory ... is different`, xóa thư mục `build/` rồi build lại.
4. **Credential không nằm trong code, `sdkconfig` hay git.** Mỗi người tự tạo
   `config/device_config.csv` từ file mẫu rồi nạp vào board (xem [Demo nhanh](#demo-nhanh)).
5. **Thư mục `third_party/`** được giữ bằng `.gitkeep`. Nếu thiếu thư mục này, CMake báo lỗi ngay.

---

## Board dev và board sản phẩm

Firmware được cấu hình cho **board sản phẩm: ESP32-S3, flash 4 MB, không giả định có PSRAM**.
Board dev ESP32-S3 **N16R8** chạy nguyên cấu hình này mà không cần sửa gì.

- **Không bật PSRAM hay tăng flash size trên board dev.** Nếu bật, code chạy tốt trên dev nhưng
  thiếu RAM/flash trên sản phẩm.
- Với N16R8: GPIO 26–32 dành cho flash, GPIO 33–37 dành cho PSRAM, GPIO 19/20 là USB.
  Không nối thiết bị vào các chân này.

---

## Demo nhanh

### Cần chuẩn bị
- ESP-IDF **v5.5.5** (mở bằng ESP-IDF PowerShell), Python 3, board ESP32-S3.
- WiFi **2.4 GHz**, bảo mật WPA2/WPA3, mật khẩu từ 8 ký tự. Hotspot iPhone cần bật "Maximize Compatibility".
- Một broker MQTT. Nhanh nhất là HiveMQ Cloud bản miễn phí:
  - Tạo cluster, rồi tạo **2 tài khoản**: một cho thiết bị (ví dụ `door01`), một cho web (ví dụ `owner`).
  - Thiết bị dùng `mqtts://<host>:8883`, web dùng `wss://<host>:8884/mqtt`. Kiểm tra lại cổng và đường
    dẫn trong trang quản lý của broker.
  - Muốn tự host thì xem [broker/mosquitto/README.md](broker/mosquitto/README.md).

### Các bước

**1. Build và nạp firmware** (tại thư mục gốc repo):
```sh
idf.py build
idf.py -p COM5 erase-flash flash     # erase chỉ cần ở lần đầu; các lần sau chỉ "flash"
```
Nếu gặp `Wrong boot mode`: giữ nút BOOT, bấm RESET, thả BOOT, rồi chạy lại.

**2. Nạp credential** (đóng `idf.py monitor` trước, vì nó chiếm cổng COM):
```sh
copy config\device_config.csv.example config\device_config.csv
# Sửa: wifi_ssid, wifi_pass, device_id, mqtt_uri, mqtt_user, mqtt_pass
python scripts\provision_device.py -p COM5
```
`mqtt_user`/`mqtt_pass` là **tài khoản của thiết bị**, khác tài khoản đăng nhập web.
Script từ chối nếu CSV còn giá trị mẫu hoặc sai độ dài.

> **Cảnh báo:** sau khi nạp credential, **đừng chạy `erase-flash` nữa**, vì lệnh đó xóa luôn credential.
> Khi cập nhật firmware chỉ dùng `idf.py -p COM5 flash`.

**3. Xem log:**
```sh
idf.py -p COM5 monitor
```
Khi chạy đúng, log hiện lần lượt:
```
...
MQTT_CONN: Connected
```

**4. Mở web:**
```sh
cd web
python -m http.server 8000
```
Vào `http://localhost:8000` rồi đăng nhập:
- **Broker:** `wss://<host>:8884/mqtt`
- **Mã thiết bị:** `door01`
- **Tài khoản:** tài khoản web

### Kịch bản trình diễn

Board dev không có keypad/NFC thì dùng **bộ giả lập** (`app/door_sim`). Bộ giả lập **tắt mặc định** để firmware
chính không có nó. Bật trên board dev:
```sh
idf.py menuconfig      # Door simulator (dev board) -> bật "Simulate lock activity..."
idf.py -p COM5 flash monitor
```
`menuconfig` ghi vào `sdkconfig`: **đừng commit `sdkconfig` khi đang bật giả lập**.
Nó ghi vào cùng `event_log` mà `lock_service` sẽ dùng, nên luồng MQTT và web chạy y như khi có phần cứng.
Bộ giả lập chỉ nhận lệnh tại chỗ, **không có lệnh nào qua mạng**.

| Thao tác | Kết quả |
|---|---|
| Nhấn nhanh nút **BOOT** | Một lần mở cửa / từ chối ngẫu nhiên |
| Giữ nút **BOOT** ≥ 1 giây | Cảnh báo `tamper` (critical) |
| Gõ `sim` trong `idf.py monitor` | Danh sách lệnh: `sim open [pin\|nfc\|button\|key] [tên]`, `sim deny`, `sim alert [code] [level]`, `sim seed [n]`, `sim clear` |

Tên gõ trong console phải **không dấu**: console của ESP-IDF bỏ mọi ký tự ngoài ASCII.

1. Trang hiện **"Khóa: trực tuyến"** và **"Đã kết nối"**.
2. Gõ `sim seed 10`: lịch sử và "Hoạt động mới" cập nhật ngay.
3. Giữ nút BOOT 1 giây: trang hiện **banner đỏ, kêu bíp, rung, nháy tiêu đề tab**. Bấm "Đã xem".
4. **Rút điện board:** khoảng 45 giây sau chuyển sang **"Khóa: ngoại tuyến"** (Last Will). Bấm "Tải lại":
   trang vẫn hiện các sự kiện gần nhất từ topic retained `recent`. Cắm lại thì quay về trực tuyến.
5. Đóng trang, giữ nút BOOT, mở lại trang: banner **"Trong lúc bạn vắng mặt: …"**.
6. Gõ `sim clear`: lịch sử và cảnh báo trên mọi trang đang mở đều xóa theo (mã phiên `gen` đổi).

nút "Ghi âm gửi tới cửa" (firmware chưa nhận, chưa có loa).

Khi demo ở chỗ có WiFi khác: sửa CSV, chạy lại bước 2. Không cần build lại.

### Lỗi thường gặp

| Build: `CMakeCache.txt directory ... is different` -> Xóa thư mục `build/`, build lại |
| Build: `EXTRA_COMPONENT_DIRS doesn't exist ... third_party` -> Tạo thư mục `third_party/` |
| Nạp credential: `Partition does not exist` -> Chưa nạp firmware mới: `idf.py -p COM5 erase-flash flash` |
| `access point not found` -> Sai SSID, hoặc WiFi 5 GHz |
| Có `Got IP` nhưng lặp `Still waiting for SNTP time sync` -> Mạng chặn NTP: đổi sang hotspot điện thoại |
| `Broker refused the connection` -> Sai `mqtt_user`/`mqtt_pass` hoặc ACL |
| Web: "Hết thời gian chờ" / "Broker không phản hồi" -> Sai URL `wss://`, cổng hoặc đường dẫn `/mqtt` |
| Web: "Ghi âm cần mở trang qua HTTPS" -> Chỉ ghi âm được trên `localhost` hoặc HTTPS |

Xem thêm ở [docs/provisioning.md](docs/provisioning.md).
