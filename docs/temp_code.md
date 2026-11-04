# Mã tạm cho khách

Chủ nhà không có ở nhà mà cần cho ai đó vào: bấm **"Tạo mã tạm (30 phút)"** trên web, gửi mã 8 số cho khách. Khách
nhập mã trên keypad rồi nhấn `#` như một PIN. Mã dùng được nhiều lần trong 30 phút, rồi tự hết hạn.

**Không có gì đi qua MQTT.** Web và khóa có chung một bí mật. Web tự tính mã, khóa tự tính lại để kiểm tra. Vẫn giữ
quy tắc "không mở cửa qua mạng" (`docs/mqtt_protocol.md` mục 1), và mã vẫn dùng được khi khóa mất Wi-Fi.

## 1. Cài đặt

1. Tạo bí mật và nạp vào khóa (thêm dòng `tmp_secret` vào `config/device_config.csv`, rồi ghi `devcfg`):

   ```sh
   python scripts\provision_device.py --new-temp-secret
   ```

   Script in bí mật ra một lần (32 ký tự hex). Reset board, log có `Temporary guest codes on`.
2. Trên web, thẻ **Mã tạm cho khách**: dán bí mật, nhấn **Lưu trên máy này**. Trình duyệt nhớ nó (theo mã thiết bị),
   chỉ trên máy đó.

Không có `tmp_secret` thì khóa không nhận mã tạm nào. Chạy lại `--new-temp-secret` để đổi bí mật: mọi mã cũ hết dùng
được, và phải dán bí mật mới vào web.

## 2. Trên khóa

| Nhập | Kết quả |
|---|---|
| Mã tạm còn hạn, rồi `#` | Mở cửa, lịch sử ghi người là `Temp code` (web: "Mã tạm", Telegram: "Khách (mã tạm)") |
| Mã hết hạn hoặc sai | Như PIN sai: `Authentication failed`, tính vào số lần sai của PIN (5 lần thì khóa PIN) |
| Mã tạm khi khóa chưa có giờ (chưa lên mạng từ lúc bật) | `Temp code not checked` / `Clock not set yet`, vẫn tính là một lần sai |

PIN thật được kiểm tra trước. Chỉ khi 8 số đó không phải PIN của ai thì khóa mới thử nó như một mã tạm.

## 3. Thuật toán

Firmware (`app/temp_code`), web (`web/js/temp-code.js`) và script (`scripts/temp_code.py`) tính giống hệt nhau:

```
slot = Unix time / 60                       (số phút, UTC)
mac  = HMAC-SHA256(bí_mật_16_byte, "LOCKTMP1" + slot dạng 4 byte big-endian)
off  = mac[31] & 0x0F
mã   = (mac[off..off+3] dạng big-endian & 0x7FFFFFFF) % 100000000, đủ 8 chữ số
```

Web tạo mã với `slot` của phút hiện tại. Khóa chấp nhận `slot` từ 29 phút trước tới 2 phút sau giờ của khóa (32 lần
tính), vì đồng hồ điện thoại có thể nhanh hơn một chút.

Bộ thử (bí mật `000102030405060708090a0b0c0d0e0f`): slot `0` → `80970255`, `29833333` → `55184857`, `29833334` →
`74724184`, `4294967295` → `32480546`. Firmware tự kiểm tra lúc khởi động (log báo lỗi nếu sai), web kiểm tra trước mỗi
lần tạo mã.

Tạo mã từ máy tính, không cần web:

```sh
python scripts\temp_code.py <bí_mật>
```

## 4. Giới hạn

- Không thu hồi được một mã đã gửi: nó dùng được tới hết 30 phút. Cần chặn gấp thì đổi bí mật.
- Ai có bí mật đều tạo được mã. Bí mật nằm dạng chữ thường trong trình duyệt (dự án học tập, không mã hóa thêm).
- Đoán mò: mỗi lúc có khoảng 32 mã hợp lệ trên 10^8, và 5 lần sai là PIN bị khóa, nên gần như không đoán trúng.
- Web cần HTTPS hoặc `localhost` để tính mã (WebCrypto). Mở qua `http://<IP LAN>` thì báo lỗi.
