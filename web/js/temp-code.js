// Temporary guest codes: made here, checked by the lock offline. Nothing is sent over MQTT (docs/temp_code.md).
// Same algorithm as app/temp_code/temp_code.c and scripts/temp_code.py: HMAC-SHA256(secret, "LOCKTMP1" + minute),
// HOTP dynamic truncation, 8 digits. crypto.subtle needs HTTPS or localhost.

export const TEMP_CODE_VALID_MIN = 30;
export const TEMP_CODE_DIGITS = 8;

const LABEL = new TextEncoder().encode('LOCKTMP1');

// Key 00 01 .. 0f, from scripts/temp_code.py (the firmware self-test uses the same ones).
const VECTORS = [
  [0, '80970255'],
  [29833333, '55184857'],
  [29833334, '74724184'],
  [0xffffffff, '32480546'],
];

// 32 hex characters (tmp_secret of the device) -> 16 bytes, or null.
export function parseSecret(text) {
  const hex = String(text ?? '').trim();
  if (!/^[0-9a-fA-F]{32}$/.test(hex)) return null;
  return Uint8Array.from(hex.match(/../g), (b) => parseInt(b, 16));
}

// The code of one minute slot (Unix time / 60).
export async function codeAt(keyBytes, slot) {
  if (!globalThis.crypto?.subtle) throw new Error('Trình duyệt không hỗ trợ (cần HTTPS hoặc localhost)');
  const key = await crypto.subtle.importKey('raw', keyBytes, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const message = new Uint8Array(LABEL.length + 4);
  message.set(LABEL);
  new DataView(message.buffer).setUint32(LABEL.length, slot >>> 0, false);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', key, message));
  const offset = mac[mac.length - 1] & 0x0f;
  const value = new DataView(mac.buffer).getUint32(offset, false) & 0x7fffffff;
  return String(value % 10 ** TEMP_CODE_DIGITS).padStart(TEMP_CODE_DIGITS, '0');
}

// A code made now: { code, expiresAt } with expiresAt in ms. The lock accepts it until then.
export async function makeCode(keyBytes, nowMs = Date.now()) {
  const slot = Math.floor(nowMs / 60000);
  return { code: await codeAt(keyBytes, slot), expiresAt: (slot + TEMP_CODE_VALID_MIN) * 60000 };
}

// False when this browser computes other codes than the lock would.
export async function selfTest() {
  const key = Uint8Array.from({ length: 16 }, (_, i) => i);
  for (const [slot, code] of VECTORS) if ((await codeAt(key, slot)) !== code) return false;
  return true;
}
