**Quy tắc để hai export ghép được vào một firmware:**

1. cùng phiên bản SquareLine (hiện là 1.6.2). Firmware dùng chung `ui_helpers.c` của `ui/`.
2. Mọi màn hình admin bật Temporary.
3. Tên đối tượng, animation và hàm sự kiện bên admin bắt đầu bằng `Admin`.
4. Font hoặc ảnh dùng ở cả hai project phải cùng tên và cùng nội dung.
5. Không include `ui/ui.h` và `ui_admin/ui.h` cùng một file `.c`.
6. Thanh trạng thái, màu, font có ở cả hai project: sửa bên này thì sửa cả bên kia.


## Kiến trúc

```
SmartLock_UI.spj      --Export--> app/ui_service/ui/ 
SmartLock_adminUI.spj --Export--> app/ui_service/ui_admin/
app/ui_service               Nạp màn hình, đổi phím keypad → phím LVGL, chuyển giữa hai project
middleware/lvgl_port         LVGL 8.3: tick, buffer vẽ, task "lvgl", mutex, keypad
app/common/ui_task.c         Khởi tạo GPIO + SPI, nối driver với lvgl_port, chuyển phím từ keypad_task
device/ili9341_tft_display   Driver ILI9341 (DEV_ILI9341_*), luôn ở hướng ngang xoay 180° (320 × 240)
```

Chân cắm, xung SPI và số dòng buffer chỉnh trong menuconfig → TFT display (ILI9341) and UI.

Nếu màn hình lỗi, firmware chỉ ghi log rồi chạy tiếp, khóa vẫn hoạt động bình thường.

## Hai project SquareLine

| Project | Màn hình | UI Files Export Path |
|---|---|---|
| `design/squareline/SmartLock_UI.spj` | Home, Pin, Granted, Denied, Voice, VoiceDone, Alert | `app/ui_service/ui` |
| `design/squareline_admin/SmartLock_adminUI.spj` | AdminLogin, AdminMenu, AdminPin, AdminFinger, AdminCard, AdminInput, AdminScan, AdminDone, AdminFail, AdminAlert | `app/ui_service/ui_admin` |

Thông số phải khớp với firmware: **LVGL 8.3.x, 320 × 240, 16 bit swap**.
Tạo thư mục export trước, và **không bao giờ chọn `app/ui_service`**, vì SquareLine sẽ ghi đè `CMakeLists.txt`.

## Chuyển màn hình

Màn hình không cảm ứng, nên firmware chuyển màn hình (trừ luồng ghi âm bên dưới):

```c
ui_service_show_admin(&s_ui_ctrl); /* Sang project admin: mở AdminLogin. */
ui_service_show_home(&s_ui_ctrl);  /* Về Home, ví dụ khi thoát admin. */

/* Trong cùng một project, khi đang giữ lock: */
_ui_screen_change(&ui_AdminMenu, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_AdminMenu_screen_init);
```

Màn hình admin là màn hình tạm: chỉ được tạo khi mở và tự giải phóng khi rời đi. Khi không hiển thị, các biến
`ui_Admin*` bằng `NULL`, nên phải kiểm tra trước khi cập nhật.

## Dữ liệu trên màn hình

Đối tượng nào firmware cập nhật, dữ liệu gì, khi nào, và phím trên từng màn hình: xem [ui_data.md](ui_data.md). Đổi
tên đối tượng trong SquareLine thì sửa file đó và code firmware theo.

## Keypad, font, RAM

- **Keypad:** `0`–`9`, `#` = `LV_KEY_ENTER`, `*` = `LV_KEY_BACKSPACE` (đổi trong `ui_service_config.h`). Không có phím mũi tên, nên mỗi màn hình chỉ nên có một ô nhận phím.
- **Font:** firmware bật Montserrat **14, 20, 28**. Dùng cỡ khác thì bật thêm trong menuconfig → LVGL → Font usage.
- **RAM:** heap LVGL 32 KB, 2 buffer vẽ 25 KB, driver thêm 25 KB để đổi byte. Ảnh nằm trong flash. Nếu log báo `lv_mem` hết bộ nhớ, tăng
  `LV_MEM_SIZE` hoặc bật Temporary cho màn hình ít dùng.

## Sửa lỗi thường gặp

| Hiện tượng | Cách xử lý |
|---|---|
| `LV_COLOR_16_SWAP should be 1` | Chọn 16 bit swap trong SquareLine, export lại |
| `undefined reference to lv_font_montserrat_XX` | Bật cỡ font đó trong menuconfig |
| `multiple definition of 'ui_...'` | Trùng tên giữa hai project (quy tắc 3), hoặc còn file màn hình cũ |
| `redefinition of 'struct _ui_anim_user_data_t'` | Vi phạm quy tắc 5 |
| Màu sai hoàn toàn | Kiểm tra `CONFIG_LV_COLOR_16_SWAP=y`; driver gửi byte thấp trước |
| Màn hình trắng, log không lỗi | Kiểm tra dây DC, RESET, CS; driver không báo lỗi SPI |
