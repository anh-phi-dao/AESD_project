# Dữ liệu trên màn hình TFT

Đối tượng tạo trong `app/ui_service/ui/screens/ui_<màn hình>.c`, khai báo trong file `.h` cùng tên. Mọi màn hình
được tạo lúc khởi động và giữ suốt, nên biến `ui_*` không bao giờ `NULL` (trừ `ui_Overlay`).

Cập nhật từ task khác phải giữ `ui_service_lock` / `ui_service_unlock`.

## 1. Đối tượng firmware dùng

| Màn hình | Đối tượng | Chữ mẫu trong SquareLine | Firmware đổi thành |
|---|---|---|---|
| `Home` | `ui_HomeState` | `LOCKED` | `UNLOCKED` |
| `Home` | `ui_HomeMessages` | `No new messages` | `No messages`, `1 message - hold # to listen`, `2 messages - hold # to listen` (tin của chủ nhà cho khách) |
| `Home` | `ui_HomeHintText` | `Hold:  * Record    # Play    0 Admin` | |
| `Input` | `ui_InputTitle` | `ENTER PIN` | `ADD PIN`, `DELETE CARD` … |
| `Input` | `ui_InputPrompt` | `Enter PIN or scan card / fingerprint` | `Enter user ID (1-20)`, `Enter new PIN`, `Enter PIN again` |
| `Input` | `ui_InputField` | (ô nhập, che ký tự, tối đa 8 số) | `ui_service_show()` tự xóa; đọc khi `#` qua `on_submit`; bật/tắt che ký tự bằng `lv_textarea_set_password_mode` |
| `Input` | `ui_InputKeys` | `#  OK          *  Delete` | |
| `Input` | `ui_InputHintText` | `Returns to home after 10s` | |
| `Voice` | `ui_VoiceTitle` | `RECORDING` | `MESSAGE FOR GUESTS` (admin ghi), `PLAYING` (đang phát, icon đổi thành loa) |
| `Voice` | `ui_VoiceFill` | Rộng 0 px | Rộng 0–240 px = thời gian đã ghi / thời gian tối đa (thanh nền `ui_VoiceTrack` rộng 240 px), hoặc gọi `RecordProgress_Animation(ui_VoiceFill, 0)` (15 s) |
| `Voice` | `ui_VoiceTime` | `Up to 15 s` | `00:04 / 00:15`; khi phát: `00:03 / 00:12` (độ dài tin) |
| `Voice` | `ui_VoiceHintText` | `Hold * for 3s to cancel and return Home` | |
| `AdminLogin` | `ui_AdminLoginId` | (ô nhập, placeholder `Admin ID`) | `ui_service_show()` tự xóa; đọc khi `#` qua `on_submit` |
| `AdminLogin` | `ui_AdminLoginPass` | (ô nhập che ký tự, placeholder `Password`) | `ui_service_show()` tự xóa; đọc khi `#` qua `on_submit` |
| `AdminLogin` | `ui_AdminLoginScan` | `Scan Card / Fingerprint` | |
| `AdminLogin` | `ui_AdminLoginHintText` | `#: Next / OK      *: Delete / Back` | |
| `Menu` | `ui_MenuTitle` | `ADMIN MENU` | `PIN CODES`, `FINGERPRINTS`, `RFID CARDS`, `MESSAGES FOR GUESTS`, `MESSAGES FROM THE OWNER` |
| `Menu` | `ui_MenuCount` | `Admin mode` | `Stored: 3 / 20` |
| `Menu` | `ui_MenuRow1` … `ui_MenuRow4` | `1    PIN codes` … `0    Exit admin` | `ADMIN MENU`: `1 PIN codes` … `4 Guest messages` (`0 Exit admin` ở dòng gợi ý); `1    Add`, `2    Delete`, `0    Back`, dòng 4 để `""`; danh sách tin: `1. Web   12 s   14:05 02/11` |
| `Menu` | `ui_MenuHint` | `Press a number` | |
| `Status` | `ui_StatusIcon` | `LV_SYMBOL_WARNING` | `LV_SYMBOL_EYE_OPEN` khi quét … |
| `Status` | `ui_StatusTitle` | `SECURITY ALERT` | `ADD FINGERPRINT`, `DOOR LOCKED` |
| `Status` | `ui_StatusDetail` | `Too many failed attempts` | `Place finger on the sensor`, `Lift your finger`, `Tap card on the reader` |
| `Status` | `ui_StatusFill` | Rộng 96 px | Rộng 0–240 px = tiến độ (thanh nền `ui_StatusTrack` rộng 240 px) |
| `Status` | `ui_StatusValue` | `Door locked for 60 s` | `59 s` … `1 s`, `Scan 2 / 3` |
| `Status` | `ui_StatusHintText` | `Hold * to cancel` | `Use admin or contact security` |
| `Overlay` → TopBar | `ui_TopBarText` | `SMART LOCK` | `ADMIN MODE` khi đang ở admin |
| `Overlay` → TopBar | `ui_TopBarWifiOff` | Dấu X đỏ | `LV_SYMBOL_CLOSE` khi mất WiFi, `""` khi có WiFi (nguồn: `network_manager`) |
| `Overlay` → TopBar | `ui_TopBarBattery` | `LV_SYMBOL_BATTERY_FULL` | Theo biến pin (mục 6): `FULL` ≥ 90 %, `_3` ≥ 65 %, `_2` ≥ 40 %, `_1` ≥ 15 %, còn lại hoặc chưa đo `_EMPTY` |
| `Overlay` → TopBar | `ui_TopBarBatteryPct` | `100%` | `<n>%` theo biến pin, `--%` khi chưa đo |
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
| Nhấn `#` khi đang ghi, hoặc hết 15 s | `Status` (`MESSAGE RECORDED`, `1: Listen  2: Record again`, `Length: 7 s`, `#: Send  Hold *: Delete`) | Dưới 1 s: ERROR `Too short`; lỗi mic/SD: ERROR `Record failed` (về `Home`) |
| `1` ở màn nghe lại | `Voice` (`PLAYING`), phát xong hoặc `#`/`*` thì về màn nghe lại | |
| `2` ở màn nghe lại | `Voice` (`RECORDING`), bản mới thay bản cũ | |
| `#` ở màn nghe lại | `Home`; tin lưu vào thẻ SD và gửi lên web | SUCCESS `Message sent` |
| Giữ `*` ở màn nghe lại | `Home`, xóa bản ghi | INFO `Recording cancelled` |
| Màn nghe lại 30 s không có phím | `Home`, tin **vẫn được gửi** (khách bỏ đi thường là muốn gửi) | SUCCESS `Message sent` |
| Giữ `*` 3 s khi đang ghi (hủy) | `Home` | INFO `Recording cancelled` |
| Giữ `*` 3 s ở `Home` khi không có thẻ SD | Giữ nguyên | ERROR `Not available` |
| Giữ `#` 3 s ở `Home`, có 1 tin của chủ nhà | `Voice` (`PLAYING`), phát xong về `Home` | |
| Giữ `#` 3 s ở `Home`, có từ 2 tin | `Menu` dạng danh sách `MESSAGES FROM THE OWNER` | |
| Giữ `#` 3 s ở `Home`, không có tin | Giữ nguyên | INFO `No messages` |
| `#` ở danh sách tin | `Voice` (`PLAYING`), phát xong về danh sách | ERROR `Cannot play` nếu file hỏng |
| `#` hoặc `*` khi đang phát | Dừng, về chỗ cũ | |
| Danh sách tin của khách 20 s không có phím | `Home` | |
| Giữ `0` 3 s ở `Home` hoặc `Input` | `AdminLogin` (keypad ở ô ID; `#` sang ô mật khẩu) | |
| Đăng nhập admin sai | `AdminLogin`, xóa 2 ô, về ô ID | ERROR `Wrong ID or password` / `2 tries left` |
| Đăng nhập admin sai 3 lần | `Status` (`Admin locked for 60 s`) | |
| Đăng nhập admin đúng | `Menu` (`ADMIN MENU`) | |
| Ở `Menu` chọn 1 / 2 / 3 | `Menu`, đổi chữ (`PIN CODES`, `Stored: 3 / 20`, `1 Add / 2 Delete / 0 Back`) | |
| Add (phím `1`) | `Menu` dạng danh sách `For which user?`: Dat, Phi, Tuan, User 4 … (PIN: chỉ người chưa có PIN; thẻ / vân tay: kèm số đang có, `Dat   (2 cards)`) | |
| Chọn user khi Add vân tay | Danh sách `Which finger of Dat?`: `Left thumb` … `Right little` (bỏ các ngón đã có) | |
| Chọn user khi Add PIN | `Input` (`ADD PIN - Dat`): `Enter new PIN`, rồi `Enter PIN again` | |
| Chọn user (thẻ) / ngón (vân tay) | `Status` (`Tap card on the reader`, `Dat - Left index    Scan 1 / 3`) | |
| Quét đúng lần 1, 2 | `Status` (`Lift, then tap the same card`, `Scan 2 / 3`, thanh tiến độ 1/3, 2/3) | |
| Quét lần 3 | `Menu` của mục đó | SUCCESS `Saved` / `Dat - Left index` |
| Quét thẻ khác thẻ lần đầu | `Status`, đếm lại từ `Scan 2 / 3` với thẻ mới | ERROR `Cards do not match` |
| Thẻ đã có người dùng | `Menu` của mục đó | ERROR `Already added` / `Card of Phi` |
| 15 s không quét (mỗi lần quét đủ 15 s) | `Menu` của mục đó | ERROR `Failed` / `No card detected` |
| Delete (phím `2`) | Danh sách mọi mục: `Dat - PIN`, `Dat - card ..A1B2`, `Dat - Left index` | INFO `Nothing to delete` khi trống |
| Chọn mục để xóa | `Status` (`DELETE FINGERPRINT?`, `Dat - Left index`, `#: Delete  *: Cancel`) | |
| `#` ở màn xác nhận | `Menu` của mục đó | SUCCESS `Deleted` / `Dat - Left index` |
| Ở `ADMIN MENU` chọn 4 | `Menu` (`MESSAGES FOR GUESTS`, `Stored: 2 / 5`, `1 Record new / 2 Listen / delete / 0 Back`) | |
| `1` ở `MESSAGES FOR GUESTS` | `Voice` (`MESSAGE FOR GUESTS`); `#` dừng rồi nghe lại như khách (`#: Save`), giữ `*` hủy, về menu | SUCCESS `Message saved`; đủ 5 tin: ERROR `Failed` / `5 messages, delete one first` |
| `2` ở `MESSAGES FOR GUESTS` | Danh sách tin (admin): `#` nghe, `0` xóa, `*` về menu | INFO `Nothing to play` khi trống |
| `0` ở danh sách tin (admin) | `Status` (`DELETE MESSAGE?`, `#: Delete  *: Cancel`) | |
| `#` ở màn xác nhận xóa tin | Danh sách tin, hoặc menu nếu hết tin | SUCCESS `Deleted` / `Message removed` |
| Chủ nhà gửi tin từ web | Giữ nguyên; `Home` hiện thêm 1 tin, danh sách đang mở tự cập nhật | |
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
| Giữ `#` | `Home` | Nghe tin của chủ nhà: 1 tin thì phát luôn, nhiều tin thì mở danh sách |
| Giữ `0` | `Home`, `Input` | `AdminLogin` |
| Giữ `*` | `Input`, `AdminLogin` | Về `Home` |
| Giữ `*` | Bước admin (`Input`, `Status` quét) | Về menu của mục đó |
| `#` / giữ `*` | `Voice` (đang ghi) | Dừng (sang màn nghe lại) / hủy |
| `1` / `2` / `#` / giữ `*` | Màn nghe lại | Nghe / ghi lại / gửi (admin: lưu) / xóa |
| `#` hoặc `*` | `Voice` (đang phát) | Dừng |
| `2` / `8`, `#`, `*` | Danh sách tin | Lên / xuống, nghe, quay lại. Admin có thêm `0`: xóa |
| `1` `2` `3` `4` / `0` | `ADMIN MENU` | Mục PIN / vân tay / thẻ / tin cho khách, hoặc thoát admin |
| `1` / `2` / `0` | `MESSAGES FOR GUESTS` | Ghi tin mới / nghe, xóa / quay lại |
| `1` / `2` / `0` | Menu PIN / vân tay / thẻ | Thêm / xóa / quay lại |
| `2` / `8`, `#`, `*` | Danh sách admin | Lên / xuống (vòng lại), chọn, quay lại. Danh sách dài hơn 4 dòng có thanh cuộn bên phải |
| `#` / `*` | Xác nhận xóa | Xóa / không xóa (về danh sách) |
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
| `app/common/voice_task.c` | Tin nhắn thoại: ghi từ mic INMP441 vào thẻ SD, phát ra loa NS4168, nhận tin của chủ nhà từ web (`voice/in`), gửi tin của khách lên web (`voice/out`, `voice/index`). Xem `docs/mqtt_protocol.md` mục 4 |

Thời gian, số lần thử, PIN và tài khoản admin mặc định nằm trong `system_control.h`: PIN của User 1 là `1234567`, admin
là ID `1` / mật khẩu `1234`. PIN được kiểm tra ngay trên thiết bị. Lên mạng chỉ có kết quả đúng/sai, không có mã.

**Chưa có phần cứng / chưa làm:**
- Solenoid: `system_on_door()` mới chỉ ghi log.
- Tin nhắn thoại cần thẻ SD. Không có thẻ thì ghi âm báo `Not available`, khóa vẫn chạy bình thường.
- Trong lúc ghi âm và phát tin, phím không kêu bíp (loa và mic dùng chung bus I2S).

Solenoid có đánh dấu `TODO(lock_service)` trong code.

## 5. Người dùng và dữ liệu

User 1 là Dat, 2 là Phi, 3 là Tuan, các user khác là `User 4` … `User 20` (`s_user_names` trong
`system_control.c`). Mỗi user có tối đa một PIN, và bao nhiêu thẻ / ngón tay cũng được: 20 ô thẻ, 40 ô vân tay. Vân
tay ghi rõ ngón nào, tay trái hay phải. Mẫu vân tay nằm ở trang = số ô + 1 trong AS608.

`storage_task` lưu vào thẻ SD, file `CREDS.TXT` (bản 2):
```
VERSION,2
PIN,1,1234567
CARD,0,2,04A1B2C3
FINGER,0,1,6        # ô 0, Dat, Right index
```
File bản 1 (một thẻ / một ngón mỗi user) vẫn đọc được: ngón tay cũ hiện là `Finger`. Lần lưu sau sẽ ghi bản 2.

## 6. Biến pin (cho phần đo pin)

Phần đo pin chưa có. Màn hình chỉ hiển thị một biến; ai làm phần đo pin chỉ cần ghi vào biến này, không cần đụng tới LVGL
hay `ui_service_lock`.

| Hàm (`app/common/ui_flow_task.h`) | Ý nghĩa |
|---|---|
| `ui_flow_set_battery(uint8_t percent)` | Đặt % pin, `0`–`100` (lớn hơn 100 tính là 100). `UI_FLOW_BATTERY_UNKNOWN` (`0xFF`): chưa đo được |
| `ui_flow_get_battery()` | Đọc lại giá trị vừa đặt (ví dụ để gửi lên web) |

- Gọi từ task nào cũng được, lúc nào cũng được, không cần khóa. Màn hình vẽ lại trong vòng `UI_FLOW_TICK_MS` (100 ms),
  chỉ khi giá trị đổi.
- Chưa ai gọi thì thanh trên cùng hiện icon pin rỗng và `--%`.
- Giá trị nên được lọc trước khi đặt (trung bình vài lần đọc ADC), để icon không nhảy qua lại ở ngưỡng.
- Pin yếu: ngoài việc đặt %, phần đo pin tự gửi cảnh báo `EVENT_LOG_ALERT_LOW_BATTERY` (`low_battery`, web hiện
  "Pin yếu"), mỗi lần xuống dưới ngưỡng chỉ gửi một lần.

Ví dụ một task đo pin:

```c
#include "ui_flow_task.h"

static void battery_task(void *pvParameter)
{
    while (1)
    {
        const uint8_t percent = battery_read_percent(); /* Phần của người làm đo pin: ADC -> %. */
        ui_flow_set_battery(percent);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
```
