# Dữ liệu trên màn hình TFT

Đối tượng tạo trong `app/ui_service/<thư mục>/screens/ui_<màn hình>.c`, khai báo trong file `.h` cùng tên.

Màn hình thường ở `ui/`, màn hình admin ở `ui_admin/`.

Code cập nhật màn hình thường viết trong `app/ui_service/ui_service.c`, màn hình admin trong
`app/ui_service/ui_service_admin.c` (một file `.c` không include được cả hai `ui.h`). 

## Chữ và số thay đổi trên màn hình

| Màn hình | Đối tượng | Đang vẽ | Đổi thành |
|---|---|---|---|
| `Home` | `ui_HomeState` | `LOCKED` | `UNLOCKED` |

| `Home` | `ui_HomeMessages` | `No new messages` | `1 new message`, `2 new messages` |


| `Denied` | `ui_DeniedReason` | `Authentication Failed` | `Wrong PIN`, `Unknown card` |

| `Denied` | `ui_DeniedTries` | `4 tries left` | `3 tries left`, `2 tries left`, `1 try left` |

| `Alert` | `ui_AlertHint` | `Door locked for 60 s` | `59 s`, `58 s` ... `1 s` |

| `VoiceDone` | `ui_VoiceDoneTitle` | `RECORD SUCCESS` | `RECORD FAILED` |

| `AdminLogin` | `ui_AdminLoginError` | (trống) | `Wrong ID or password` |

| `AdminAlert` | `ui_AdminAlertHint` | `Admin locked for 60 s` | `59 s`, `58 s` ... `1 s` |

| `AdminPin` / `Finger` / `Card` | `ui_Admin<X>Count` | `Stored: 3 / 20` | `Stored: 4 / 20`, `Stored: 2 / 20` |

| `AdminInput` | `ui_AdminInputTitle` | `ADD PIN` | `DELETE PIN`, `ADD CARD` ... |

| `AdminInput` | `ui_AdminInputPrompt` | `Enter user ID (1-20)` | `Enter new PIN`, `Enter PIN again` |

| `AdminScan` | `ui_AdminScanPrompt` | `Place finger on the sensor` | `Lift your finger`, `Tap card on the reader` |

| `AdminScan` | `ui_AdminScanStep` | `Scan 1 / 3` | `Scan 2 / 3`, `Scan 3 / 3` |

| `AdminDone` | `ui_AdminDoneTitle` | `SAVED` | `DELETED`, `FAILED` |

| `AdminDone` | `ui_AdminDoneDetail` | `User 4 added` | `User 5 added`, `User 4 deleted` |



## 1. Quy tắc chung

Cập nhật UI từ task khác phải giữ `ui_service_lock` / `ui_service_unlock`.

Màn hình admin là màn hình tạm: khi không hiện, `ui_Admin*` bằng `NULL`


## 2. Chuyển màn hình

| Sự kiện | Sang màn hình | Ai chuyển |
|---|---|---|
| Ở `Home` nhấn phím số | `Pin` | Firmware |
| PIN / thẻ / vân tay đúng | `Granted` | Firmware, theo `auth_manager` |
| PIN / thẻ / vân tay sai | `Denied` | Firmware, theo `auth_manager` |
| Sai quá số lần cho phép | `Alert` | Firmware |
| `Pin` 10 s không có phím | `Home` | Firmware |
| `Granted` / `Denied` hiện được 3 s | `Home` | Firmware |
| `Alert` hết thời gian khóa | `Home` | Firmware |
| Giữ `*` 3 s ở `Home` | `Voice` | Firmware |
| `Voice` 16 s, rồi `VoiceDone` 2 s | `Home` | SquareLine tự chuyển |
| Giữ `*` 3 s ở `Voice` (hủy) | `Home` | Firmware |
| Giữ `0` 3 s ở `Home` | `AdminLogin` | `ui_service_show_admin` |
| Đăng nhập admin đúng | `AdminMenu` | Firmware |
| Đăng nhập admin sai quá số lần | `AdminAlert` | Firmware |
| `AdminAlert` hết thời gian khóa | `AdminLogin` | Firmware |
| Nhấn `0` ở `AdminMenu` | `Home` | `ui_service_show_home` |


## 3. Thanh trạng thái

Tên đối tượng: `ui_<màn hình>Wifi`, `WifiOff`, `Battery`, `BatteryPct`. Chỉ cần cập nhật màn hình đang hiện, và cập
nhật lại khi chuyển màn hình.

| Đối tượng | Dữ liệu | Nguồn |
|---|---|---|
| `…WifiOff` | Dấu X đỏ: `LV_SYMBOL_CLOSE` khi mất WiFi (mặc định lúc khởi động), `""` khi có WiFi | `network_manager` |
| `…Battery` | `LV_SYMBOL_BATTERY_FULL` / `_3` / `_2` / `_1` / `_EMPTY` | Chưa có phần đo pin |
| `…BatteryPct` | `<n>%`. Chưa đo pin thì để `100%` | Chưa có phần đo pin |
