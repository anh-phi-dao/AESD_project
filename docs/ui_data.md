# Dữ liệu trên màn hình TFT

Đối tượng tạo trong `app/ui_service/ui/screens/ui_<màn hình>.c`, khai báo trong file `.h` cùng tên. Mọi màn hình
được tạo lúc khởi động và giữ suốt, nên biến `ui_*` không bao giờ `NULL` (trừ `ui_Overlay`).

Cập nhật từ task khác phải giữ `ui_service_lock` / `ui_service_unlock`.

## 1. Đối tượng firmware dùng

| Màn hình | Đối tượng | Chữ mẫu trong SquareLine | Firmware đổi thành |
|---|---|---|---|
| `Home` | `ui_HomeState` | `LOCKED` | `UNLOCKED` |
| `Home` | `ui_HomeMessages` | `No new messages` | `1 new message`, `2 new messages` |
| `Home` | `ui_HomeHintText` | `Hold:  * Record    # Play    0 Admin` | |
| `Input` | `ui_InputTitle` | `ENTER PIN` | `ADD PIN`, `DELETE CARD` … |
| `Input` | `ui_InputPrompt` | `Enter PIN or scan card / fingerprint` | `Enter user ID (1-20)`, `Enter new PIN`, `Enter PIN again` |
| `Input` | `ui_InputField` | (ô nhập, che ký tự, tối đa 8 số) | `ui_service_show()` tự xóa; đọc khi `#` qua `on_submit`; bật/tắt che ký tự bằng `lv_textarea_set_password_mode` |
| `Input` | `ui_InputKeys` | `#  OK          *  Delete` | |
| `Input` | `ui_InputHintText` | `Returns to home after 10s` | |
| `Voice` | `ui_VoiceTitle` | `RECORDING` | `SENDING` |
| `Voice` | `ui_VoiceFill` | Rộng 0 px | Rộng 0–240 px = thời gian đã ghi / thời gian tối đa (thanh nền `ui_VoiceTrack` rộng 240 px), hoặc gọi `RecordProgress_Animation(ui_VoiceFill, 0)` (15 s) |
| `Voice` | `ui_VoiceTime` | `Up to 15 s` | `00:04 / 00:15` |
| `Voice` | `ui_VoiceHintText` | `Hold * for 3s to cancel and return Home` | |
| `AdminLogin` | `ui_AdminLoginId` | (ô nhập, placeholder `Admin ID`) | `ui_service_show()` tự xóa; đọc khi `#` qua `on_submit` |
| `AdminLogin` | `ui_AdminLoginPass` | (ô nhập che ký tự, placeholder `Password`) | `ui_service_show()` tự xóa; đọc khi `#` qua `on_submit` |
| `AdminLogin` | `ui_AdminLoginScan` | `Scan Card / Fingerprint` | |
| `AdminLogin` | `ui_AdminLoginHintText` | `#: Next / OK      *: Delete / Back` | |
| `Menu` | `ui_MenuTitle` | `ADMIN MENU` | `PIN CODES`, `FINGERPRINTS`, `RFID CARDS` |
| `Menu` | `ui_MenuCount` | `Admin mode` | `Stored: 3 / 20` |
| `Menu` | `ui_MenuRow1` … `ui_MenuRow4` | `1    PIN codes` … `0    Exit admin` | `1    Add`, `2    Delete`, `0    Back`, dòng 4 để `""` |
| `Menu` | `ui_MenuHint` | `Press a number` | |
| `Status` | `ui_StatusIcon` | `LV_SYMBOL_WARNING` | `LV_SYMBOL_EYE_OPEN` khi quét … |
| `Status` | `ui_StatusTitle` | `SECURITY ALERT` | `ADD FINGERPRINT`, `DOOR LOCKED` |
| `Status` | `ui_StatusDetail` | `Too many failed attempts` | `Place finger on the sensor`, `Lift your finger`, `Tap card on the reader` |
| `Status` | `ui_StatusFill` | Rộng 96 px | Rộng 0–240 px = tiến độ (thanh nền `ui_StatusTrack` rộng 240 px) |
| `Status` | `ui_StatusValue` | `Door locked for 60 s` | `59 s` … `1 s`, `Scan 2 / 3` |
| `Status` | `ui_StatusHintText` | `Hold * to cancel` | `Use admin or contact security` |
| `Overlay` → TopBar | `ui_TopBarText` | `SMART LOCK` | `ADMIN MODE` khi đang ở admin |
| `Overlay` → TopBar | `ui_TopBarWifiOff` | Dấu X đỏ | `LV_SYMBOL_CLOSE` khi mất WiFi, `""` khi có WiFi (nguồn: `network_manager`) |
| `Overlay` → TopBar | `ui_TopBarBattery` | `LV_SYMBOL_BATTERY_FULL` | `_3` / `_2` / `_1` / `_EMPTY` (chưa có phần đo pin) |
| `Overlay` → TopBar | `ui_TopBarBatteryPct` | `100%` | `<n>%` |
| `Overlay` → Popup | `ui_Popup`, `ui_PopupIcon`, `ui_PopupTitle`, `ui_PopupDetail` | `Authentication failed` / `3 tries left` | Chỉ đổi qua `ui_service_popup()` |

Để ẩn thanh tiến độ hoặc dòng đếm khi không dùng: `lv_obj_add_flag(ui_StatusTrack, LV_OBJ_FLAG_HIDDEN)` (và
`ui_StatusFill`, `ui_MenuCount`).

## 2. Luồng màn hình

| Sự kiện | Màn hình | Popup |
|---|---|---|
| Ở `Home` nhấn phím số | `Input` (`ENTER PIN`) | |
| PIN / thẻ / vân tay đúng | `Home`, `ui_HomeState` = `UNLOCKED` | SUCCESS `Welcome` / `Door unlocked` |
| PIN / thẻ / vân tay sai | `ui_service_show(INPUT)` lần nữa (xóa ô) | ERROR `Authentication failed` / `3 tries left` |
| Sai quá số lần cho phép | `Status` (`SECURITY ALERT`, đếm ngược ở `ui_StatusValue`) | |
| Hết thời gian khóa | `Home` | |
| `Input` 10 s không có phím | `Home` | |
| Giữ `*` 3 s ở `Home` | `Voice` (`ui_VoiceFill`, `ui_VoiceTime` chạy theo thời gian) | |
| Hết thời gian ghi tối đa | `Home` | SUCCESS `Message saved` |
| Ghi âm xong / lỗi | `Home` | SUCCESS `Message saved` / ERROR `Record failed` |
| Giữ `*` 3 s khi đang ghi (hủy) | `Home` | INFO `Recording cancelled` |
| Giữ `0` 3 s ở `Home` | `AdminLogin` (keypad ở ô ID; `#` sang ô mật khẩu). Lần nhấn `0` đầu đã mở `Input`: firmware chuyển tiếp sang `AdminLogin` khi `0` giữ đủ 3 s | |
| `#` ở ô mật khẩu (`on_submit`), hoặc quét thẻ / vân tay admin | Firmware kiểm tra | |
| Đăng nhập admin sai | `ui_service_show(ADMIN_LOGIN)` lần nữa (xóa 2 ô, về ô ID) | ERROR `Wrong ID or password` |
| Đăng nhập admin sai quá số lần | `Status` (`Admin locked for 60 s`) | |
| Đăng nhập admin đúng | `Menu` (`ADMIN MENU`) | |
| Ở `Menu` chọn 1 / 2 / 3 | `Menu`, đổi chữ (`PIN CODES`, `Stored: 3 / 20`, `1 Add / 2 Delete / 0 Back`) | |
| Add / Delete PIN, Delete vân tay / thẻ | `Input` (`Enter user ID (1-20)`, rồi `Enter new PIN`, `Enter PIN again`). Mỗi bước đều gọi `on_submit(INPUT, …)`: firmware tự nhớ đang ở bước nào | |
| Add vân tay / thẻ | `Status` (`Place finger on the sensor`, `Scan 1 / 3`) | |
| Lưu / xóa xong | `Menu` của mục đó | SUCCESS `Saved` / `User 4 added`, ERROR `Failed` |
| Nhấn `0` ở `ADMIN MENU` | `Home` | |

Mọi lần chuyển màn hình do firmware làm, bằng `ui_service_show()`. Không còn màn hình nào SquareLine tự chuyển.

## 3. Firmware chạy luồng này: `app/common/ui_flow_task.c`

Task `ui_flow` nhận phím từ `keypad_task` (nhấn / nhả, để đo thời gian giữ) và `on_submit` từ `ui_service`, rồi
chuyển màn hình theo bảng trên. Thời gian, số lần thử, PIN và tài khoản admin mặc định nằm trong `ui_flow_task.h`.

| Phím | Ở đâu | Tác dụng |
|---|---|---|
| Số | `Home` | Mở `Input` (`ENTER PIN`), giữ lại số vừa bấm |
| `#` | `Input` / `AdminLogin` | Gửi (LVGL gọi `on_submit`) |
| Giữ `*` 3 s | `Home` | `Voice` |
| Giữ `#` 3 s | `Home` | Popup `No new messages` |
| Giữ `0` 3 s | `Home` | `AdminLogin` |
| Giữ `*` 3 s | `Input`, `AdminLogin` | Về `Home` |
| Giữ `*` 3 s | Bước admin (`Input`, `Status` quét) | Về menu của mục đó |
| `#` / giữ `*` 3 s | `Voice` | Dừng / hủy ghi |
| `1` `2` `3` / `0` | `ADMIN MENU` | Mục PIN / vân tay / thẻ, hoặc thoát admin |
| `1` / `2` / `0` | Menu PIN / vân tay / thẻ | Thêm / xóa / quay lại |

Thời gian chờ: `Input` 10 s, `AdminLogin` 20 s, admin 30 s không bấm phím thì về `Home`. Sai PIN 5 lần, hoặc sai đăng
nhập admin 3 lần, thì khóa 60 s. PIN đúng/sai được ghi vào `event_log` (web thấy được). Khi bị khóa thì ghi thêm cảnh
báo `pin_bruteforce`.

**Chưa có phần cứng nên tạm thời:** `UNLOCKED` chỉ đổi chữ trong 5 s, chưa đóng mở solenoid. Ghi âm mới chỉ chạy
thanh tiến độ, chưa ghi tiếng. Thêm vân tay / thẻ sẽ hết giờ sau 15 s và báo `Reader not connected yet`. PIN thêm từ
menu admin chỉ nằm trong RAM, khởi động lại là mất. Các chỗ cần nối phần cứng có đánh dấu `TODO(...)` trong code.

