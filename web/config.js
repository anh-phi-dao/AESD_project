// Deployment settings. A value set here is fixed: its field is hidden from the login form.
// Only public values belong here: this file is served to everyone. Never put a password in it.
export default {
  // WebSocket URL of the broker. EMQX Cloud Serverless: 'wss://<address>:8084/mqtt'.
  brokerUrl: 'wss://o817ff06.ala.asia-southeast1.emqxsl.com:8084/mqtt',
  // Lock id, e.g. 'door01'. Leave empty when one deployment serves many locks (users then type it).
  deviceId: 'door01',
};
