# Đưa web lên mạng

Web chỉ là file tĩnh trong `web/`, không có server riêng. Trình duyệt kết nối thẳng tới broker qua `wss://`:

```
Điện thoại / máy tính ──HTTPS──> GitHub Pages            chỉ tải HTML/JS/CSS
        └──────────wss://:8884──> HiveMQ Cloud <──mqtts://:8883── ESP32-S3
```

Host tĩnh không thấy mật khẩu hay dữ liệu. Web và MQTT không có lệnh mở cửa, nên lộ tài khoản web cũng không mở được
cửa.

## 1. Trang demo

https://project-smartlock.github.io/doorlock-remote/

Trang này là bản copy tay của `web/`, nằm ở repo `project-smartlock/doorlock-remote` (nhánh `main`). Merge vào `master`
của repo này không tự cập nhật trang demo. Cách cập nhật:

1. Thử trên máy: `cd web`, `python -m http.server 8000`, mở `http://localhost:8000`.
2. Copy nội dung `web/` sang repo `doorlock-remote`, giữ lại `README.md` và `.nojekyll` của repo đó.
3. Push lên `main`. Sau 1 đến 2 phút trang cập nhật (Ctrl+F5 nếu vẫn thấy bản cũ).

Workflow `.github/workflows/deploy-web.yml` của repo này deploy lên một trang khác (`https://<owner>.github.io/<repo>/`),
chỉ chạy khi bật Settings → Pages → Source: GitHub Actions.

Host nào cũng phải có HTTPS. Mở bằng `http://` (trừ `localhost`) thì trình duyệt chặn `wss://`, chặn micro và không cho
cài app.

## 2. Điền sẵn broker

Sửa `web/config.js` trước khi deploy:

```js
export default {
  brokerUrl: 'wss://xxxx.s1.eu.hivemq.cloud:8884/mqtt',
  deviceId: 'door01',
};
```

Ô nào đã điền ở đây thì bị ẩn khỏi form đăng nhập. Để trống `deviceId` nếu một trang dùng cho nhiều khóa. Ai cũng tải
được file này, nên không ghi mật khẩu vào đây.

## 3. Tài khoản người dùng

Mỗi người (hoặc mỗi gia đình) một tài khoản MQTT riêng trên HiveMQ Cloud (Access Management), khác tài khoản của thiết
bị. Mất điện thoại thì chỉ cần xóa tài khoản đó.

| Topic `lock/<id>/...` | Quyền của web |
|---|---|
| `status`, `event`, `alert`, `recent`, `history/resp`, `voice/out` | Subscribe |
| `history/req`, `voice/in` | Publish |

Gói miễn phí của HiveMQ có thể không phân quyền được theo topic. Khi đó web có thể publish sự kiện giả, nhưng vẫn không
mở được cửa. Cần phân quyền đúng thì dùng Mosquitto tự host với `broker/mosquitto/acl.example`.

## 4. Cài như app

Android (Chrome): menu ⋮ → Cài đặt ứng dụng. iPhone (Safari): Chia sẻ → Thêm vào MH chính (ghi âm cần iOS 16.4 trở lên).

## 5. Giới hạn

Mạng 4G chặn cổng 8884: trang tải được nhưng đăng nhập báo "Broker không phản hồi". Khi demo dùng WiFi hoặc hotspot nhà
mạng khác. Muốn chạy mọi mạng thì cần broker hoặc proxy nhận WebSocket ở cổng 443.

Web chỉ nhận cảnh báo khi app đang mở. Mở lại thì trang báo các cảnh báo đã lỡ. Thông báo nền sẽ đi qua Telegram bot
hoặc ntfy (việc tiếp theo).

## 6. Bảo mật

CSP trong `index.html` chỉ cho chạy script của trang và `cdn.jsdelivr.net`. Dữ liệu từ broker luôn hiển thị bằng
`textContent`.

mqtt.js được ghim phiên bản và có `integrity` (SRI). Khi nâng phiên bản: sửa URL ở `index.html` và `sw.js`, tính lại
`integrity` bằng `curl -s <url> | openssl dgst -sha384 -binary | openssl base64 -A`, rồi tăng `CACHE` trong `sw.js`.

Trình duyệt không lưu mật khẩu, chỉ nhớ tên đăng nhập nếu người dùng chọn.
