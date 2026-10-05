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

## Cách 1: GitHub Pages (tự deploy khi merge vào master)

Workflow `.github/workflows/deploy-web.yml` publish thư mục `web/` mỗi khi `web/**` thay đổi trên `master`.

1. Chủ repo vào **Settings → Pages → Build and deployment → Source: GitHub Actions** (làm một lần).
2. Merge PR vào `master`. Tab **Actions** sẽ chạy "Deploy web".
3. Web có địa chỉ `https://<owner>.github.io/<repo>/`. Link cũng hiện trong kết quả job.

Chạy lại thủ công: **Actions → Deploy web → Run workflow**.

## Cách 2: demo ngay, không cần quyền admin repo

Kéo thả thư mục `web/` vào [Netlify Drop](https://app.netlify.com/drop) hoặc Cloudflare Pages (Direct Upload).
Sau vài giây sẽ có link `https://...`. Mỗi lần sửa web thì phải kéo thả lại.

Bắt buộc dùng host có **HTTPS**. Nếu mở qua `http://` (trừ `localhost`), trình duyệt sẽ:
- chặn ghi âm micro;
- không cho cài app;
- chặn kết nối `wss://`.

## Điền sẵn broker cho người dùng

Sửa `web/config.js` trước khi deploy:

```js
export default {
  brokerUrl: 'wss://xxxx.s1.eu.hivemq.cloud:8884/mqtt',
  deviceId: 'door01',
};
```

Người dùng khi đó chỉ cần nhập tài khoản và mật khẩu. File này ai cũng tải về được, nên
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

## Cài như app trên điện thoại

Web là PWA (`manifest.webmanifest` + `sw.js`):
- **Android (Chrome):** menu ⋮ → **Cài đặt ứng dụng** / **Thêm vào màn hình chính**.
- **iPhone (Safari):** nút Chia sẻ → **Thêm vào MH chính**. Cần iOS 16.4 trở lên để ghi âm trong chế độ app.

App mở toàn màn hình, có icon riêng và mở được giao diện kể cả khi mạng chập chờn. Dữ liệu vẫn cần kết nối
tới broker.

## Giới hạn: thông báo khi đóng app

MQTT qua WebSocket **chỉ chạy khi app đang mở**. Khi đóng app hoặc khóa màn hình điện thoại, web không nhận
cảnh báo nữa. Thông báo nền (trộm cạy cửa, nhập sai PIN...) sẽ đi qua **Telegram bot** (việc tiếp theo). Web Push
thật thì cần thêm một server, để giai đoạn sau.

## Bảo mật của trang public

- **CSP** trong `index.html`: chỉ cho chạy script của trang và `cdn.jsdelivr.net`, không có inline script.
  Dữ liệu từ broker luôn hiển thị bằng `textContent`.
- **mqtt.js được ghim phiên bản và có SRI** (`integrity`). Nếu CDN bị sửa file, trình duyệt sẽ từ chối chạy.
  Khi nâng phiên bản:
  1. sửa URL ở `index.html` và `sw.js`;
  2. tính lại mã `integrity`: `curl -s <url> | openssl dgst -sha384 -binary | openssl base64 -A`;
  3. tăng `CACHE` trong `sw.js` (`smartlock-v2`...).
- Không lưu mật khẩu trong trình duyệt. Chỉ URL, mã thiết bị và tên đăng nhập được nhớ nếu người dùng chọn.
