# Nạp credential cho thiết bị

WiFi, tài khoản broker và mã thiết bị không nằm trong code, `sdkconfig` hay git. Chúng được ghi vào partition NVS
`devcfg` bằng `scripts/provision_device.py`.

| Partition | Nội dung | Bị ghi đè khi nạp credential |
|---|---|---|
| `nvs` | Dữ liệu WiFi driver, PIN, thẻ... | Không |
| `devcfg` | Credential của thiết bị | Có |

Thiếu credential thì firmware vẫn chạy, khóa vẫn mở bằng keypad / thẻ, chỉ tắt phần mạng.

## 1. Board

Firmware cấu hình cho board sản phẩm: ESP32-S3 flash 4 MB, không PSRAM. Board dev N16R8 chạy nguyên cấu hình này.
Không bật PSRAM hay tăng flash size trên board dev, nếu không lỗi thiếu RAM chỉ lộ ra trên board sản phẩm.

Trên N16R8 không nối thiết bị vào GPIO 26 đến 37 (flash và PSRAM). GPIO 19 / 20 là USB.

## 2. Nạp và chạy thử

Chạy trong ESP-IDF PowerShell, tại thư mục gốc repo.

1. Lần đầu dùng bảng partition mới: `idf.py -p COM7 erase-flash flash`. Các lần sau: `idf.py -p COM7 flash monitor`.
2. `copy config\device_config.csv.example config\device_config.csv`, rồi sửa `wifi_ssid`, `wifi_pass` (WiFi 2.4 GHz),
   `device_id`, `mqtt_uri`, `mqtt_user`, `mqtt_pass`. `mqtt_user` / `mqtt_pass` là tài khoản của thiết bị, khác tài
   khoản web. File này đã được gitignore.
3. `python scripts\provision_device.py` (cổng lấy từ `scripts/config.json`, hoặc thêm `-p COM7`).
4. Reset board, xem `idf.py -p COM7 monitor`. Thành công khi thấy lần lượt:
   ```
   DEV_CFG:   Loaded device "door01" (...)
   NET_MGR:   Got IP 192.168.x.x
   NET_MGR:   Time synced: ...
   MQTT_CONN: Connected
   ```
5. `cd web`, `python -m http.server 8000`, mở `http://localhost:8000`, đăng nhập bằng tài khoản web. Trạng thái hiện
   trực tuyến. Rút nguồn board thì khoảng 45 s sau chuyển sang ngoại tuyến. Muốn có dữ liệu lịch sử thì bật bộ giả lập
   rồi nhấn BOOT hoặc gõ `sim seed 10` trong monitor (xem README).

Đổi WiFi: sửa CSV rồi chạy lại bước 3, không cần build lại. Dùng từ xa: xem [deploy_web.md](deploy_web.md).

## 3. Broker tự host

Thêm dòng sau vào CSV (đường dẫn tính từ thư mục chứa CSV):

```
mqtt_ca,file,string,certs/ca.crt
```

Không có dòng này thì thiết bị dùng bộ chứng chỉ gốc của ESP-IDF (cho broker cloud).

## 4. Sản xuất hàng loạt

Mỗi thiết bị một CSV, `device_id` và tài khoản broker riêng, không dùng chung. Tạo image không cần board:

```sh
python scripts\provision_device.py -c door02.csv --no-flash -o door02_devcfg.bin
```

Ghi image ở offset `0x10000` cùng lúc nạp firmware. Image chứa mật khẩu, xóa sau khi dùng.

## 5. Trước khi giao sản phẩm

`devcfg` hiện chưa mã hóa, ai có board là đọc được. Cần bật flash encryption, NVS encryption và secure boot v2. Các
bước này ghi eFuse, không đảo ngược được: chỉ làm trên board sản phẩm khi firmware đã ổn định, không làm trên board dev.

## 6. Lỗi thường gặp

| Log | Cách xử lý |
|---|---|
| `CMakeCache.txt directory ... is different` | Xóa thư mục `build/` rồi build lại |
| `Partition does not exist` | `idf.py -p COMx erase-flash flash` trước |
| `Cannot open partition "devcfg"` | Chạy `provision_device.py` (lần đầu thì `erase-flash`) |
| `Device is not provisioned` | Làm bước 2 và 3 |
| `Missing key "..."` / `longer than` | CSV thiếu hoặc sai độ dài, sửa rồi chạy lại bước 3 |
| `access point not found` | Sai SSID, hoặc WiFi 5 GHz (chỉ hỗ trợ 2.4 GHz) |
| `authentication failed` | Sai `wifi_pass` |
| Có IP nhưng không có `Time synced` | Mạng chặn NTP (UDP 123), đổi mạng |
| `Broker refused the connection` | Kiểm tra `mqtt_user` / `mqtt_pass` và ACL |
