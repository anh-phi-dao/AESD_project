# Màn hình TFT

## Kiến trúc

```
SmartLock_UI.spj  --Export-->  app/ui_service/ui/
app/ui_service                 Nạp màn hình, chuyển màn hình, popup, đổi phím keypad → phím LVGL
middleware/lvgl_port           LVGL 8.3: tick, buffer vẽ, task "lvgl", mutex, keypad
app/common/ui_task.c           Khởi tạo GPIO + SPI, nối driver với lvgl_port, chuyển phím từ keypad_task
device/ili9341_tft_display     Driver ILI9341 (DEV_ILI9341_*), luôn ở hướng ngang xoay 180° (320 × 240)
```

Chân cắm, xung SPI và số dòng buffer chỉnh trong menuconfig → TFT display (ILI9341) and UI.

Nếu màn hình lỗi, firmware chỉ ghi log rồi chạy tiếp, khóa vẫn hoạt động bình thường.

## Project SquareLine

Một project duy nhất: `design/squareline/SmartLock_UI.spj`, export vào `app/ui_service/ui`. Admin không còn project
riêng: ngoài màn hình đăng nhập, admin dùng lại Input, Menu, Status với chữ khác.

| Màn hình | Dùng cho |
|---|---|
| `Home` | Trạng thái khóa, số tin nhắn mới, gợi ý phím giữ |
| `Input` | Một ô nhập: PIN, ID người dùng, PIN mới |
| `Voice` | Ghi âm tin nhắn thoại: icon mic, thanh tiến độ, thời gian |
| `AdminLogin` | Ô ID và ô mật khẩu admin, hoặc quét thẻ / vân tay admin |
| `Menu` | Tiêu đề, dòng đếm, 4 dòng `1 … / 2 … / 3 … / 0 …`: menu admin, menu PIN / vân tay / thẻ |
| `Status` | Icon, tiêu đề, chi tiết, thanh tiến độ: khóa tạm, quét vân tay / thẻ |
| `Overlay` | **Không bao giờ hiện.** Chứa `TopBar` (WiFi, pin) và `Popup` để vẽ trong SquareLine |

Khi khởi động, firmware chuyển `TopBar` và `Popup` sang `lv_layer_top()`: chúng nằm đè lên mọi màn hình, chỉ có một
bản để cập nhật. Vì vậy trên các màn hình khác **chừa trống 26 px phía trên** cho TopBar.

Thông số phải khớp với firmware: **SquareLine 1.6.2, LVGL 8.3.x, 320 × 240, 16 bit swap**.
Tạo thư mục export trước, và **không bao giờ chọn `app/ui_service`**, vì SquareLine sẽ ghi đè `CMakeLists.txt`.

**Khi sửa trong SquareLine:**

- Giữ tên các đối tượng firmware dùng (bảng trong [ui_data.md](ui_data.md)). Đối tượng trang trí đặt tên tùy ý.
- Không thêm event chuyển màn hình: firmware tự chuyển.
- Xóa hay đổi tên màn hình thì xóa file cũ trong `app/ui_service/ui/screens/` sau khi export (SquareLine không tự xóa).
- Popup đang có màu đỏ để dễ nhìn khi vẽ. Firmware đổi màu theo loại popup, và ẩn nó lúc khởi động.

## Gọi từ firmware

```c
ui_service_instance_ctrl_t *p_ui = ui_task_get_ui();     /* NULL khi không có màn hình: bỏ qua mọi lệnh UI */

ui_service_show(p_ui, UI_SERVICE_SCREEN_INPUT);          /* HOME, INPUT, VOICE, ADMIN_LOGIN, MENU, STATUS */

ui_service_popup(p_ui, UI_SERVICE_POPUP_ERROR, "Authentication failed", "3 tries left", 2000);
ui_service_popup_hide(p_ui);                             /* duration 0 = hiện đến khi gọi hide */

/* Nhận PIN / đăng nhập admin khi người dùng nhấn #. Chạy trong task LVGL: chép chuỗi, gửi sang task của bạn, return. */
static void on_submit(ui_service_screen_t screen, const char *p_first, const char *p_second, void *p_context);
ui_service_set_submit_cb(p_ui, on_submit, NULL);

/* Đổi chữ trên màn hình: giữ lock */
if (APP_SUCCESS == ui_service_lock(p_ui, 100))
{
    lv_label_set_text(ui_InputTitle, "ADD PIN");
    lv_label_set_text(ui_InputPrompt, "Enter user ID (1-20)");
    lv_textarea_set_password_mode(ui_InputField, false);
    ui_service_unlock(p_ui);
}
```

Loại popup: `INFO` (nền tối, chuông), `SUCCESS` (xanh lá, dấu tích), `ERROR` (đỏ, dấu X). Màu chỉnh trong
`ui_service_config.h`.

`ui_service_show()` xóa các ô nhập và chỉ cho keypad vào ô của màn hình vừa hiện:

- `Input`: `ui_InputField`. `#` gọi `on_submit(INPUT, pin, NULL)` (ô trống thì bỏ qua).
- `AdminLogin`: `ui_AdminLoginId`. `#` ở ô ID sang ô mật khẩu, `*` khi ô mật khẩu trống quay lại ô ID, `#` ở ô mật
  khẩu gọi `on_submit(ADMIN_LOGIN, id, password)`.
- `Home`: phím số vào `ui_InputField` (đang ẩn), nên `Home` → `Input` giữ lại chữ số đầu tiên. `#` ở `Home` không gọi
  `on_submit`.
- `Voice`, `Menu`, `Status`: LVGL bỏ qua phím. Firmware đọc phím từ keypad (chọn menu, giữ `*` để hủy...).
- Gọi lại `ui_service_show()` với màn hình đang hiện để xóa ô nhập, ví dụ sau khi nhập sai PIN.

UI không kiểm tra PIN hay mật khẩu. Firmware kiểm tra cục bộ, không bao giờ qua MQTT.

Mọi màn hình được tạo một lần lúc khởi động và không bị xóa, nên các biến `ui_*` luôn hợp lệ (trừ `ui_Overlay`, bằng
`NULL` sau khi khởi động).

## Dữ liệu trên màn hình

Đối tượng nào firmware cập nhật, dữ liệu gì, khi nào: xem [ui_data.md](ui_data.md). Đổi tên đối tượng trong SquareLine
thì sửa file đó và code firmware theo.

## Keypad, font, RAM

- **Keypad:** `0`–`9`, `#` = `LV_KEY_ENTER`, `*` = `LV_KEY_BACKSPACE` (đổi trong `ui_service_config.h`). Không có phím mũi tên, nên mỗi màn hình chỉ nên có một ô nhận phím (riêng `AdminLogin` có 2 ô, `ui_service` chuyển bằng `#` / `*`).
- **Font:** firmware bật Montserrat **14, 20, 28**. Dùng cỡ khác thì bật thêm trong menuconfig → LVGL → Font usage.
- **RAM:** heap LVGL 32 KB, 2 buffer vẽ 25 KB, driver thêm 25 KB để đổi byte. Ảnh nằm trong flash. Nếu log báo `lv_mem` hết bộ nhớ, tăng
  `LV_MEM_SIZE`.

## Sửa lỗi thường gặp

| Hiện tượng | Cách xử lý |
|---|---|
| `LV_COLOR_16_SWAP should be 1` | Chọn 16 bit swap trong SquareLine, export lại |
| `undefined reference to lv_font_montserrat_XX` | Bật cỡ font đó trong menuconfig |
| `multiple definition of 'ui_...'` | Còn file màn hình cũ trong `ui/screens/` |
| `'ui_TopBar' undeclared` hoặc `'ui_Popup' undeclared` | Đã đổi tên hoặc xóa đối tượng trên `Overlay`; đặt lại tên cũ |
| Màu sai hoàn toàn | Kiểm tra `CONFIG_LV_COLOR_16_SWAP=y`; driver gửi byte thấp trước |
| Màn hình trắng, log không lỗi | Kiểm tra dây DC, RESET, CS; driver không báo lỗi SPI |
