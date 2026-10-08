# Nạp credential cho thiết bị

Thông tin WiFi, tài khoản broker và mã thiết bị **không nằm trong code, `sdkconfig` hay git**.
Chúng được ghi vào partition NVS riêng tên `devcfg` bằng `scripts/provision_device.py`.

| Partition | Nội dung | Bị ghi đè khi nạp credential? |
|---|---|---|
| `nvs` | Dữ liệu WiFi driver, dữ liệu người dùng (PIN, thẻ…) | Không |
| `devcfg` | Credential của thiết bị | Có (ghi lại toàn bộ) |

Nếu thiếu credential, firmware vẫn chạy: khóa vẫn mở được bằng keypad/NFC, chỉ phần mạng bị tắt
(log: `Network disabled: no device credentials`).

## Board dev (N16R8) và board sản phẩm (4 MB)

Firmware được cấu hình cho **board sản phẩm: ESP32-S3 flash 4 MB, không giả định có PSRAM**. Board dev
ESP32-S3 N16R8 chạy nguyên cấu hình này mà không cần sửa gì: firmware chỉ dùng 4 MB đầu của flash
16 MB, còn PSRAM 8 MB được bỏ qua. **Không bật PSRAM hay tăng flash size trên board dev**, nếu không
lỗi thiếu RAM/flash chỉ lộ ra trên board sản phẩm.

Lưu ý chân GPIO trên N16R8: GPIO 26–32 dành cho flash, GPIO 33–37 dành cho PSRAM octal; không nối
thiết bị vào các chân này. GPIO 19/20 là USB D-/D+.

## Demo nhanh (5 bước)

Tất cả lệnh chạy trong ESP-IDF PowerShell, tại thư mục gốc repo.

1. **Lần đầu sau khi đổi sang bảng partition mới**, xóa flash để bỏ firmware cũ:
   ```sh
   idf.py -p COM7 erase-flash flash
   ```
   Các lần sau chỉ cần `idf.py -p COM7 flash monitor`.

2. Tạo file credential (file này đã được gitignore):
   ```sh
   copy config\device_config.csv.example config\device_config.csv
   ```
   Sửa `wifi_ssid`, `wifi_pass` (WiFi 2.4 GHz, WPA2/WPA3), `device_id`, `mqtt_uri`, `mqtt_user`, `mqtt_pass`.
   `mqtt_user`/`mqtt_pass` là **tài khoản của thiết bị** trên broker, khác tài khoản đăng nhập web.

3. Ghi vào board (cổng lấy từ `scripts/config.json`, hoặc thêm `-p COM7`):
   ```sh
   python scripts\provision_device.py
   ```

4. Reset board và xem log `idf.py -p COM7 monitor`. Thứ tự log khi thành công:
   ```
   DEV_CFG:   Loaded device "door01" (Wi-Fi "...", broker mqtts://..., CA bundle)
   NET_MGR:   Got IP 192.168.x.x
   NET_MGR:   Time synced: ...
   MQTT_CONN: Connected
   ```

5. Mở trang web:
   ```sh
   cd web
   python -m http.server 8000
   ```
   Vào `http://localhost:8000`, đăng nhập bằng tài khoản web với URL `wss://` của broker.
   Trạng thái hiện **Khóa: trực tuyến**; rút nguồn board thì sau khoảng 45 giây chuyển sang **ngoại tuyến**
   (Last Will). Lịch sử trống cho đến khi có sự kiện: trên board dev, bật bộ giả lập rồi nhấn nút BOOT hoặc gõ `sim seed 10`
   trong monitor (xem mục "Kịch bản trình diễn" trong README).
   Để dùng từ xa qua Internet, xem [deploy_web.md](deploy_web.md).

Đổi WiFi khi đi demo ở nơi khác: sửa CSV rồi chạy lại bước 3. Không cần build hay flash lại firmware.

## Broker tự host với CA riêng

Thêm một dòng vào CSV, đường dẫn tính từ thư mục chứa CSV:

```
mqtt_ca,file,string,certs/ca.crt
```

Không có dòng này thì thiết bị xác thực broker bằng bộ chứng chỉ gốc của ESP-IDF (dùng cho broker cloud).

## Sản xuất hàng loạt

- Mỗi thiết bị một file CSV với `device_id` và tài khoản broker riêng. Không dùng chung tài khoản:
  lộ một thiết bị không được làm lộ các thiết bị khác, và ACL của broker gắn theo từng tài khoản.
- Tạo image mà không cần board:
  ```sh
  python scripts\provision_device.py -c door02.csv --no-flash -o door02_devcfg.bin
  ```
  Rồi ghi image ở offset `0x10000` cùng lúc nạp firmware. Image chứa mật khẩu: xóa sau khi dùng.

## Trước khi giao sản phẩm thật

Credential trong `devcfg` hiện **chưa được mã hóa**: ai có board và cổng USB đều đọc được flash.
Cần bật thêm:

1. **Flash encryption** và **NVS encryption** (ESP32-S3 hỗ trợ khóa dựa trên HMAC trong eFuse).
2. **Secure boot v2**, để không ai nạp được firmware lạ đọc credential ra.

Các bước này **ghi eFuse, không thể đảo ngược**. Chỉ làm trên board sản phẩm, sau khi firmware đã ổn định,
và không làm trên board dev của nhóm.

Việc người dùng cuối tự đổi WiFi (không cần máy tính) để giai đoạn sau, ví dụ BLE provisioning qua app
ESP BLE Prov. Khi đó chỉ cần sửa `device_config` để đọc WiFi từ nguồn mới; `network_manager` không đổi.

## Xử lý lỗi thường gặp

| Log | Nguyên nhân | Cách xử lý |
|---|---|---|
| Build: `CMakeCache.txt directory ... is different` | Thư mục `build/` được tạo trên máy khác | Xóa thư mục `build/` rồi build lại |
| Nạp credential: `Partition does not exist` | Board chưa có firmware với bảng partition mới | `idf.py -p COMx erase-flash flash` trước |
| `Cannot open partition "devcfg"` | Còn dữ liệu cũ ở vùng devcfg | Chạy `provision_device.py` (hoặc `erase-flash` lần đầu) |
| `Device is not provisioned` | Chưa ghi credential | Bước 2–3 |
| `Missing key "..."` / `longer than` | CSV thiếu hoặc sai độ dài | Sửa CSV, chạy lại bước 3 |
| `access point not found` | Sai SSID hoặc WiFi 5 GHz | ESP32-S3 chỉ hỗ trợ 2.4 GHz |
| `authentication failed` | Sai mật khẩu WiFi | Sửa `wifi_pass` |
| Có IP nhưng không thấy `Time synced` | Mạng chặn NTP (UDP 123) | Đổi mạng hoặc dùng NTP server nội bộ |
| `Broker refused the connection` | Sai tài khoản broker hoặc ACL | Kiểm tra `mqtt_user`/`mqtt_pass` và ACL |
