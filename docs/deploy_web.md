# Đưa web lên mạng

Web chỉ là file tĩnh trong `web/`, không có server riêng. Trình duyệt kết nối thẳng tới broker qua `wss://`:

```
Điện thoại / máy tính ──HTTPS──> GitHub Pages            chỉ tải HTML/JS/CSS
        └──────────wss://:8084──> EMQX Cloud Serverless <──mqtts://:8883── ESP32-S3
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
  brokerUrl: 'wss://<address>.emqxsl.com:8084/mqtt',
  deviceId: 'door01',
};
```

Ô nào đã điền ở đây thì bị ẩn khỏi form đăng nhập. Để trống `deviceId` nếu một trang dùng cho nhiều khóa. Ai cũng tải
được file này, nên không ghi mật khẩu vào đây.

## 3. Tài khoản người dùng

Broker: EMQX Cloud Serverless (gói miễn phí, 1 GB dữ liệu mỗi tháng; một tin thoại 15 s tốn khoảng 1 MB). Trang
Overview của deployment cho địa chỉ, cổng `8883` (MQTT qua TLS, cho thiết bị) và `8084` (WebSocket qua TLS, cho web).
Không cần tải CA: ESP-IDF và trình duyệt đã tin chứng chỉ của EMQX.

Access Control → Authentication: mỗi người (hoặc mỗi gia đình) một tài khoản riêng, ví dụ `owner_door01`, khác tài
khoản của thiết bị (`door01`). Mất điện thoại thì chỉ cần xóa tài khoản đó.

Access Control → Authorization, cho từng username:

| Username | Topic | Action | Permission |
|---|---|---|---|
| `door01` | `lock/door01/#` | Publish | Allow |
| `door01` | `lock/door01/history/req`, `lock/door01/voice/in`, `lock/door01/voice/req` | Subscribe | Allow |
| `owner_door01` | `lock/door01/#` | Subscribe | Allow |
| `owner_door01` | `lock/door01/history/req`, `lock/door01/voice/in`, `lock/door01/voice/req` | Publish | Allow |
| `bridge_door01` | `lock/door01/#` | Subscribe | Allow (bot Telegram, chỉ đọc: [telegram/README.md](../telegram/README.md)) |

EMQX xét quy tắc từ trên xuống, gặp quy tắc khớp đầu tiên là dừng. Một quy tắc `#` Deny đặt **trên** các quy tắc Allow
sẽ chặn hết. Muốn chặn phần còn lại thì đặt nó ở cuối, rồi thử lại web và thiết bị. Thiếu quyền thì web vẫn không mở
được cửa (không có lệnh mở cửa), chỉ có thể gửi dữ liệu giả. Tự host: `broker/mosquitto/acl.example`.

## 4. Cài như app

Android (Chrome): menu ⋮ → Cài đặt ứng dụng. iPhone (Safari): Chia sẻ → Thêm vào MH chính (ghi âm cần iOS 16.4 trở lên).

## 5. Giới hạn

Mạng 4G có thể chặn cổng 8084: trang tải được nhưng đăng nhập báo "Broker không phản hồi". Khi demo dùng WiFi hoặc hotspot nhà
mạng khác. Muốn chạy mọi mạng thì cần broker hoặc proxy nhận WebSocket ở cổng 443.

Web chỉ nhận cảnh báo khi app đang mở. Mở lại thì trang báo các cảnh báo đã lỡ. Thông báo nền đi qua bot Telegram
([telegram/README.md](../telegram/README.md)).

## 6. Bảo mật

CSP trong `index.html` chỉ cho chạy script của trang và `cdn.jsdelivr.net`. Dữ liệu từ broker luôn hiển thị bằng
`textContent`.

mqtt.js được ghim phiên bản và có `integrity` (SRI). Khi nâng phiên bản: sửa URL ở `index.html` và `sw.js`, tính lại
`integrity` bằng `curl -s <url> | openssl dgst -sha384 -binary | openssl base64 -A`, rồi tăng `CACHE` trong `sw.js`.

Trình duyệt không lưu mật khẩu, chỉ nhớ tên đăng nhập nếu người dùng chọn.
