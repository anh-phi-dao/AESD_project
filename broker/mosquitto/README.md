# Mosquitto tự host (tùy chọn)

Dùng khi không muốn phụ thuộc broker cloud. Topic và phân quyền xem `docs/mqtt_protocol.md`.

1. Tạo chứng chỉ vào `certs/` (`ca.crt`, `server.crt`, `server.key`). CN/SAN của `server.crt`
   phải khớp tên miền hoặc IP mà thiết bị và trình duyệt dùng để kết nối.
   **Không commit khóa riêng.**
2. `cp acl.example acl`, sửa theo mã thiết bị.
3. Tạo tài khoản:
   ```sh
   docker run --rm -it -v "$PWD:/mosquitto/config" eclipse-mosquitto \
     sh -c "mosquitto_passwd -c /mosquitto/config/passwd door01 && mosquitto_passwd /mosquitto/config/passwd owner_door01"
   ```
4. Chạy broker:
   ```sh
   docker run -d --name smartlock-broker -p 8883:8883 -p 8884:8884 \
     -v "$PWD:/mosquitto/config" eclipse-mosquitto
   ```
5. Firmware: `p_broker_uri = "mqtts://<host>:8883"`, `p_ca_cert_pem` = nội dung `ca.crt`.
   Web: `wss://<host>:8884`.

Nếu dùng chứng chỉ tự ký, trình duyệt sẽ từ chối `wss://` cho tới khi bạn mở
`https://<host>:8884` một lần và chấp nhận chứng chỉ, hoặc cài CA vào máy.
