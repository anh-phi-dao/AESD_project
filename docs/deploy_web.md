# Đưa web lên mạng để dùng từ xa

Web chỉ gồm file tĩnh (`web/`) và **không có server riêng**. Trình duyệt kết nối thẳng tới broker MQTT qua
`wss://`, nên người dùng ở đâu cũng xem được khóa, miễn là có Internet:

```
Điện thoại / máy tính ──HTTPS──> host tĩnh (GitHub Pages...)   chỉ tải HTML/JS/CSS
        │
        └──────────wss://:8884──> HiveMQ Cloud <──mqtts://:8883── ESP32-S3
```

Host tĩnh không bao giờ thấy mật khẩu hay dữ liệu của khóa. Lớp bảo vệ là tài khoản trên broker. Web và MQTT
**không có lệnh mở cửa** (xem `docs/mqtt_protocol.md`), nên kể cả khi lộ tài khoản web thì cũng không mở được cửa.

## Trang demo hiện tại

**https://project-smartlock.github.io/doorlock-remote/**

Trang này là **bản copy tay** của thư mục `web/`, nằm ở repo riêng `project-smartlock/doorlock-remote`
(GitHub Pages, nhánh `main`, thư mục gốc). Merge vào `master` của repo này **không** tự cập nhật trang đó.
Cập nhật trang demo:

1. Sửa và thử `web/` trên máy (`cd web`, `python -m http.server 8000`, mở `http://localhost:8000`).
2. Copy toàn bộ nội dung `web/` sang repo `doorlock-remote` (giữ lại `README.md` và `.nojekyll` của repo đó).
3. Commit và push lên nhánh `main` của `doorlock-remote`. Trang cập nhật sau 1–2 phút, bấm Ctrl+F5 nếu vẫn thấy bản cũ.

Repo này còn có workflow `.github/workflows/deploy-web.yml`: nếu chủ repo bật **Settings → Pages → Source: GitHub
Actions**, mỗi lần `web/**` thay đổi trên `master` sẽ tự deploy lên `https://<owner>.github.io/<repo>/`. Đó là một
trang khác với trang demo ở trên.

Host nào cũng phải có **HTTPS**. Nếu mở qua `http://` (trừ `localhost`), trình duyệt sẽ chặn kết nối `wss://`,
chặn ghi âm micro và không cho cài app.

## Điền sẵn broker cho người dùng

Sửa `web/config.js` trước khi deploy:

```js
export default {
  brokerUrl: 'wss://xxxx.s1.eu.hivemq.cloud:8884/mqtt',
  deviceId: 'door01',
};
```

Giá trị nào đặt ở đây thì ô tương ứng **bị ẩn khỏi form đăng nhập**, người dùng chỉ còn nhập tài khoản và mật khẩu.
Để trống `deviceId` khi một trang dùng cho nhiều khóa (ô mã thiết bị hiện lại). File này ai cũng tải về được, nên
**tuyệt đối không ghi mật khẩu vào đây**.

## Tài khoản cho người dùng

Mỗi người (hoặc mỗi gia đình) có **một MQTT credential riêng** trên HiveMQ Cloud (**Access Management**):
- tách biệt với tài khoản của thiết bị (`mqtt_user` trong CSV);
- khi mất điện thoại chỉ cần xóa đúng tài khoản đó.

Quyền mà web cần:

| Topic | Quyền |
|---|---|
| `lock/<id>/status`, `event`, `alert`, `recent`, `history/resp`, `voice/out` | Subscribe |
| `lock/<id>/history/req`, `voice/in` | Publish |

Gói miễn phí của HiveMQ Cloud có thể không giới hạn được quyền theo từng topic. Nếu vậy, tài khoản web có thể
publish giả sự kiện hoặc cảnh báo, nhưng **vẫn không mở được cửa**. Khi cần phân quyền đúng như bảng trên, dùng
Mosquitto tự host với `broker/mosquitto/acl.example`.

## Giới hạn: mạng 4G chặn cổng 8884

HiveMQ Cloud gói miễn phí chỉ nhận WebSocket ở **cổng 8884**. Một số mạng 4G chặn cổng này: trang vẫn tải được nhưng
đăng nhập báo **"Broker không phản hồi. Kiểm tra cổng và đường dẫn WebSocket"**, trong khi cùng thiết bị dùng WiFi thì
vào được. Khi demo, dùng WiFi hoặc hotspot của nhà mạng khác. Muốn chạy được trên mọi mạng thì cần broker (hoặc
proxy) nhận WebSocket ở cổng 443.

## Cài như app trên điện thoại

Web là PWA (`manifest.webmanifest` + `sw.js`):
- **Android (Chrome):** menu ⋮ → **Cài đặt ứng dụng** / **Thêm vào màn hình chính**.
- **iPhone (Safari):** nút Chia sẻ → **Thêm vào MH chính**. Cần iOS 16.4 trở lên để ghi âm trong chế độ app.

App mở toàn màn hình, có icon riêng và mở được giao diện kể cả khi mạng chập chờn. Dữ liệu vẫn cần kết nối
tới broker.

## Giới hạn: thông báo khi đóng app

MQTT qua WebSocket **chỉ chạy khi app đang mở**. Khi đóng app hoặc khóa màn hình điện thoại, web không nhận
cảnh báo nữa (khi mở lại, trang báo các cảnh báo đã lỡ). Thông báo nền (trộm cạy cửa, nhập sai PIN...) sẽ đi qua
**Telegram bot/ntfy** (việc tiếp theo). Web Push thật thì cần thêm một server, để giai đoạn sau.

## Bảo mật của trang public

- **CSP** trong `index.html`: chỉ cho chạy script của trang và `cdn.jsdelivr.net`, không có inline script.
  Dữ liệu từ broker luôn hiển thị bằng `textContent`.
- **mqtt.js được ghim phiên bản và có SRI** (`integrity`). Nếu CDN bị sửa file, trình duyệt sẽ từ chối chạy.
  Khi nâng phiên bản:
  1. sửa URL ở `index.html` và `sw.js`;
  2. tính lại mã `integrity`: `curl -s <url> | openssl dgst -sha384 -binary | openssl base64 -A`;
  3. tăng `CACHE` trong `sw.js` (`smartlock-v2`...).
- Không lưu mật khẩu trong trình duyệt. Chỉ tên đăng nhập (và URL, mã thiết bị khi `config.js` để trống) được nhớ
  nếu người dùng chọn.
