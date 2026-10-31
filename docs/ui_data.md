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
| Ở `Home` nhấn (không giữ) phím số | `Input` (`ENTER PIN`), có sẵn số vừa nhấn | |
| PIN / thẻ đúng | `Home`, `ui_HomeState` = `UNLOCKED` trong 5 s | SUCCESS `Welcome` / `Door unlocked` |
| PIN sai | `Input`, ô trống | ERROR `Authentication failed` / `4 tries left` |
| Thẻ / vân tay lạ (ở `Home` hoặc `Input`) | Giữ nguyên | ERROR `Authentication failed` / `Unknown card, 3 tries left` |
| Sai 5 lần, tính chung PIN + thẻ + vân tay | `Status` (`SECURITY ALERT`, `Door locked for 60 s`) | |
| Thẻ / vân tay đã đăng ký khi đang bị khóa | `Home`, mở cửa, hết khóa | SUCCESS `Welcome` / `Door unlocked` |
| Hết thời gian khóa | `Home`, số lần thử được tính lại từ đầu | |
| Mở cửa đúng (bất kỳ cách nào) | | Số lần sai về 0 |
| Bị khóa 10 lần liên tiếp (không có lần mở đúng xen giữa) | `Status`, khóa đến khi khởi động lại | |
| `Input` 10 s không có phím | `Home` | |
| Giữ `*` 3 s ở `Home` | `Voice` (`ui_VoiceFill`, `ui_VoiceTime` chạy theo thời gian) | |
| Nhấn `#` khi đang ghi, hoặc hết 15 s | `Home` | INFO `Recording stopped` |
| Giữ `*` 3 s khi đang ghi (hủy) | `Home` | INFO `Recording cancelled` |
| Giữ `#` 3 s ở `Home` | Giữ nguyên, phát lời nhắn mới nhất của chủ nhà | INFO `Playing message`, hoặc `No new messages` |
| Giữ `0` 3 s ở `Home` hoặc `Input` | `AdminLogin` (keypad ở ô ID; `#` sang ô mật khẩu) | |
| Đăng nhập admin sai | `AdminLogin`, xóa 2 ô, về ô ID | ERROR `Wrong ID or password` / `2 tries left` |
| Đăng nhập admin sai 3 lần | `Status` (`Admin locked for 60 s`) | |
| Đăng nhập admin đúng | `Menu` (`ADMIN MENU`) | |
| Ở `Menu` chọn 1 / 2 / 3 | `Menu`, đổi chữ (`PIN CODES`, `Stored: 3 / 20`, `1 Add / 2 Delete / 0 Back`) | |
| Add PIN | `Input`: `Enter user ID (1-20)`, rồi `Enter new PIN`, rồi `Enter PIN again` | |
| Delete PIN / vân tay / thẻ | `Input` (`Enter user ID (1-20)`) | |
| Add thẻ / vân tay | `Status` (`Tap card on the reader`, `User 3    Scan 1 / 3`), cho ID trống đầu tiên | |
| Quét đúng lần 1, 2 | `Status` (`Lift, then tap the same card`, `Scan 2 / 3`, thanh tiến độ 1/3, 2/3) | |
| Quét lần 3 | `Menu` của mục đó | SUCCESS `Saved` / `User 3 added` |
| Quét thẻ khác thẻ lần đầu | `Status`, đếm lại từ `Scan 2 / 3` với thẻ mới | ERROR `Cards do not match` |
| Thẻ đã thuộc user khác | `Menu` của mục đó | ERROR `Already added` / `Card of user 2` |
| 15 s không quét (mỗi lần quét đủ 15 s) | `Menu` của mục đó | ERROR `Failed` / `No card detected` |
| Lưu / xóa xong | `Menu` của mục đó | SUCCESS `Saved` / `User 4 added`, `Deleted` / `User 4`, ERROR `Failed` |
| Admin 30 s không có phím | `Home` | INFO `Admin mode ended` |
| Nhấn `0` ở `ADMIN MENU` | `Home` | |

## 3. Phím

Nhấn rồi nhả là một phím, tác dụng xảy ra lúc nhả. Giữ 3 s là lệnh giữ, tác dụng xảy ra ngay khi đủ 3 s, và lần nhả
sau đó không tính là một phím. Nhờ vậy một phím vừa gõ được vừa giữ được (ví dụ `0` là chữ số, giữ `0` là vào admin).
Giữ một phím ở chỗ phím đó không có lệnh giữ (bảng dưới) thì vẫn tính là nhấn, để số không bị mất khi bấm hơi lâu.

| Phím | Ở đâu | Tác dụng |
|---|---|---|
| Số | `Home` | Mở `Input` (`ENTER PIN`), giữ lại số vừa nhấn |
| Số | `Input`, `AdminLogin` | Gõ vào ô (tối đa 8 số) |
| `*` | `Input`, `AdminLogin` | Xóa số cuối. Ở ô mật khẩu đang trống thì quay về ô ID |
| `#` | `Input` | Gửi |
| `#` | `AdminLogin` | Ô ID: sang ô mật khẩu. Ô mật khẩu: đăng nhập |
| Giữ `*` | `Home` | `Voice` |
| Giữ `#` | `Home` | Phát lời nhắn của chủ nhà |
| Giữ `0` | `Home`, `Input` | `AdminLogin` |
| Giữ `*` | `Input`, `AdminLogin` | Về `Home` |
| Giữ `*` | Bước admin (`Input`, `Status` quét) | Về menu của mục đó |
| `#` / giữ `*` | `Voice` | Dừng / hủy ghi |
| `1` `2` `3` / `0` | `ADMIN MENU` | Mục PIN / vân tay / thẻ, hoặc thoát admin |
| `1` / `2` / `0` | Menu PIN / vân tay / thẻ | Thêm / xóa / quay lại |
| | `Status` khóa | Không phím nào có tác dụng |

## 4. Firmware

| File | Việc |
|---|---|
| `app/common/keypad_task.c` | Quét keypad, phân biệt nhấn và giữ 3 s, gọi `system_control_key(key, hold)` |
| `app/common/rfid_task.c` | Đọc thẻ PN532, gọi `system_control_card(uid, len)` |
| `app/common/fingerprint_task.c` | Vân tay AS608 (UART1, TX IO9, RX IO8). Ở `Home` / `Input` / khóa: nhận dạng, gọi `system_control_finger(user)`. Ở `ADD FINGERPRINT`: quét 3 lần (lần 1 và 2 ghép thành mẫu lưu ở trang = user ID, lần 3 phải khớp mẫu đó), gọi `system_control_finger_scanned()` |
| `app/system_controller/system_control.c` | Toàn bộ logic: màn hình đang ở đâu, phím làm gì, kiểm tra PIN / thẻ / admin, khóa, thời gian chờ |
| `app/common/system_task.c` | Gọi `system_control_tick()` mỗi 100 ms; nối kết quả vào event log (web thấy được), cửa và âm thanh |
| `app/common/ui_flow_task.c` | Chỉ hiển thị: đọc `system_control_get_view()` mỗi 100 ms rồi vẽ màn hình và popup |

Thời gian, số lần thử, PIN và tài khoản admin mặc định nằm trong `system_control.h`: PIN của User 1 là `1234567`, admin
là ID `1` / mật khẩu `1234`. PIN được kiểm tra ngay trên thiết bị. Lên mạng chỉ có kết quả đúng/sai, không có mã.

**Chưa có phần cứng / chưa làm:**
- Solenoid: `system_on_door()` mới chỉ ghi log.
- Ghi âm và phát: `system_on_voice()` và `system_on_play()` mới chỉ ghi log. Code âm thanh cần gọi
  `system_control_set_message_count()` để `Home` hiện số lời nhắn.
- Vân tay: mẫu nằm trong flash của AS608, còn danh sách user có vân tay chỉ nằm trong RAM. Khởi động lại thì phải thêm lại, mẫu cũ bị ghi đè.
- Lưu trữ: PIN và thẻ chỉ nằm trong RAM, khởi động lại là mất.

Các chỗ này có đánh dấu `TODO(...)` trong code.
