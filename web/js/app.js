import config from '../config.js';
import { LockClient } from './lock-client.js';
import { VoiceRecorder } from './voice.js';

const STORE_KEY = 'smartlock.login';
const LIVE_MAX = 30;
const ALERT_MAX = 30;

const METHOD_LABEL = { pin: 'Mã PIN', nfc: 'Thẻ NFC', button: 'Nút bên trong', key: 'Chìa cơ' };
const ALERT_LABEL = {
  pin_bruteforce: 'Nhập sai PIN nhiều lần',
  unknown_card: 'Quẹt thẻ lạ',
  door_ajar: 'Cửa mở quá lâu',
  tamper: 'Phát hiện cạy phá',
  low_battery: 'Pin yếu',
  power_lost: 'Mất nguồn',
};
const CONN_LABEL = {
  connected: 'Đã kết nối',
  reconnecting: 'Đang kết nối lại…',
  offline: 'Mất kết nối',
  disconnected: 'Đã ngắt',
};

const $ = (id) => document.getElementById(id);
let client = null;
let oldestHistoryId = null;
let recorder = null;

// ---------- helpers ----------

// Every value from the network goes through textContent, never innerHTML.
function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  Object.assign(node, props);
  for (const c of children) node.append(c);
  return node;
}

const fmtTime = (ts) => new Date(ts * 1000).toLocaleString('vi-VN');
const methodLabel = (m) => METHOD_LABEL[m] ?? m;
const resultLabel = (r) => (r === 'granted' ? 'Mở thành công' : 'Bị từ chối');

function prepend(list, node, max) {
  list.querySelector('.empty')?.remove();
  list.prepend(node);
  while (list.children.length > max) list.lastElementChild.remove();
}

function loadSaved() {
  try {
    return JSON.parse(localStorage.getItem(STORE_KEY)) ?? {};
  } catch {
    return {};
  }
}

function save(data) {
  try {
    if (data) localStorage.setItem(STORE_KEY, JSON.stringify(data));
    else localStorage.removeItem(STORE_KEY);
  } catch {
    /* storage unavailable: nothing to remember */
  }
}

// ---------- login ----------

const form = $('login-form');
const submitBtn = form.querySelector('button[type=submit]');
const saved = { url: config.brokerUrl, deviceId: config.deviceId, ...loadSaved() };
for (const key of ['url', 'deviceId', 'username']) if (saved[key]) form.elements[key].value = saved[key];

form.addEventListener('submit', async (e) => {
  e.preventDefault();
  const f = form.elements;
  const opts = { url: f.url.value.trim(), deviceId: f.deviceId.value.trim(), username: f.username.value, password: f.password.value };
  $('login-error').textContent = '';
  submitBtn.disabled = true;

  try {
    client = new LockClient(opts);
    wireClient(client);
    await client.connect();
    save(f.remember.checked ? { url: opts.url, deviceId: opts.deviceId, username: opts.username } : null);
    f.password.value = '';
    showDashboard(true);
    loadHistory(true);
  } catch (err) {
    client = null;
    $('login-error').textContent = `Không kết nối được: ${err.message}`;
  } finally {
    submitBtn.disabled = false;
  }
});

$('logout').addEventListener('click', async () => {
  recorder?.cancel();
  await client?.disconnect();
  client = null;
  showDashboard(false);
});

function showDashboard(on) {
  $('login-view').hidden = on;
  $('dashboard').hidden = !on;
  $('session').hidden = !on;
  if (!on) {
    $('history').replaceChildren();
    $('live').replaceChildren(el('li', { className: 'empty', textContent: 'Chưa có hoạt động từ khi mở trang.' }));
    $('alerts').replaceChildren(el('li', { className: 'empty', textContent: 'Chưa có cảnh báo.' }));
    $('voices').replaceChildren(el('li', { className: 'empty', textContent: 'Chưa có tin nhắn thoại.' }));
    setPill($('device-status'), 'unknown', 'Thiết bị: chưa rõ');
  }
}

function setPill(node, state, text) {
  node.dataset.state = state;
  node.textContent = text;
}

// ---------- live data ----------

function wireClient(c) {
  c.addEventListener('connection', (e) => {
    const s = e.detail.state;
    setPill($('conn-status'), s === 'connected' ? 'online' : 'offline', CONN_LABEL[s] ?? s);
  });
  c.addEventListener('status', (e) => {
    const online = e.detail.online;
    setPill($('device-status'), online ? 'online' : 'offline', online ? 'Khóa: trực tuyến' : 'Khóa: ngoại tuyến');
  });
  c.addEventListener('event', (e) => {
    const x = e.detail.entry;
    const who = x.user ?? 'Không xác định';
    prepend(
      $('live'),
      el('li', { className: x.result === 'granted' ? 'ok' : 'bad' },
        el('strong', { textContent: resultLabel(x.result) }),
        ` · ${who} · ${methodLabel(x.method)}`,
        el('time', { textContent: fmtTime(x.ts) })),
      LIVE_MAX,
    );
    // Newest entries also belong at the top of the history table.
    if ($('history').children.length) $('history').prepend(historyRow(x));
  });
  c.addEventListener('alert', (e) => {
    const a = e.detail.alert;
    const title = ALERT_LABEL[a.code] ?? a.code;
    prepend(
      $('alerts'),
      el('li', { className: `alert-${a.level}` },
        el('strong', { textContent: title }),
        a.msg ? ` · ${a.msg}` : '',
        el('time', { textContent: fmtTime(a.ts) })),
      ALERT_MAX,
    );
    if (!e.detail.retained && a.level !== 'info') notify(title, a.msg);
  });
  c.addEventListener('voice', (e) => addVoice(e.detail));
  c.addEventListener('error', (e) => console.warn('MQTT', e.detail.error));
}

// Only works while the tab is open; background alerts are delivered by the Telegram bot.
function notify(title, body) {
  if (document.visibilityState === 'visible' || !('Notification' in window)) return;
  if (Notification.permission === 'granted') new Notification(`Smart Lock: ${title}`, { body });
}

$('enable-notify').addEventListener('click', async (e) => {
  if (!('Notification' in window)) {
    e.target.textContent = 'Trình duyệt không hỗ trợ';
    return;
  }
  const p = await Notification.requestPermission();
  e.target.textContent = p === 'granted' ? 'Đã bật thông báo' : 'Thông báo bị chặn';
});

// ---------- history ----------

function historyRow(x) {
  return el('tr', { className: x.result === 'granted' ? 'ok' : 'bad' },
    el('td', { textContent: fmtTime(x.ts) }),
    el('td', { textContent: x.user ?? '—' }),
    el('td', { textContent: methodLabel(x.method) }),
    el('td', { textContent: resultLabel(x.result) }));
}

async function loadHistory(reset) {
  if (!client) return;
  const status = $('history-status');
  const more = $('history-more');
  more.hidden = true;
  status.textContent = 'Đang tải…';
  if (reset) oldestHistoryId = null;

  try {
    const page = await client.requestHistory({ beforeId: oldestHistoryId ?? undefined });
    if (reset) $('history').replaceChildren();
    for (const x of page.items) $('history').append(historyRow(x));
    if (page.items.length) oldestHistoryId = Math.min(...page.items.map((x) => x.id));
    status.textContent = $('history').children.length ? '' : 'Chưa có lịch sử.';
    more.hidden = !page.more;
  } catch (err) {
    status.textContent = `Không tải được lịch sử: ${err.message}`;
    more.hidden = reset;
  }
}

$('history-refresh').addEventListener('click', () => loadHistory(true));
$('history-more').addEventListener('click', () => loadHistory(false));

// ---------- voice ----------

function addVoice({ blob, durationMs, receivedAt }) {
  const url = URL.createObjectURL(blob);
  const seconds = (durationMs / 1000).toFixed(1);
  prepend(
    $('voices'),
    el('li', {},
      el('strong', { textContent: 'Khách tại cửa' }),
      ` · ${seconds} giây`,
      el('time', { textContent: new Date(receivedAt).toLocaleString('vi-VN') }),
      el('audio', { controls: true, src: url, preload: 'metadata' })),
    20,
  );
}

const recordBtn = $('record');
const recordStatus = $('record-status');
if (!VoiceRecorder.isSupported()) {
  recordBtn.disabled = true;
  // Browsers only expose the microphone on HTTPS or localhost.
  recordStatus.textContent = window.isSecureContext ? 'Trình duyệt không hỗ trợ ghi âm.' : 'Ghi âm cần mở trang qua HTTPS.';
}

recordBtn.addEventListener('click', async () => {
  if (!recorder) {
    recorder = new VoiceRecorder();
    try {
      await recorder.start(() => recordBtn.click());
      recordBtn.textContent = 'Dừng và gửi';
      recordBtn.classList.add('recording');
      recordStatus.textContent = 'Đang ghi âm… (tự dừng sau 15 giây)';
    } catch (err) {
      recorder = null;
      recordStatus.textContent = `Không mở được micro: ${err.message}`;
    }
    return;
  }

  const r = recorder;
  recorder = null;
  recordBtn.disabled = true;
  recordBtn.classList.remove('recording');
  recordBtn.textContent = 'Ghi âm gửi tới cửa';
  try {
    recordStatus.textContent = 'Đang xử lý…';
    const { pcm16, sampleRate } = await r.stop();
    await client.sendVoice(pcm16, sampleRate, (p) => (recordStatus.textContent = `Đang gửi… ${Math.round(p * 100)}%`));
    recordStatus.textContent = 'Đã gửi tới cửa.';
  } catch (err) {
    recordStatus.textContent = `Gửi thất bại: ${err.message}`;
  } finally {
    recordBtn.disabled = false;
  }
});

// ---------- install as app ----------

// Service workers need HTTPS (or localhost); plain-HTTP LAN tests simply skip this.
if ('serviceWorker' in navigator && window.isSecureContext) {
  navigator.serviceWorker.register('sw.js').catch((err) => console.warn('Service worker', err));
}
