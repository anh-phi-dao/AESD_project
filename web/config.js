// Deployment defaults, pre-filled on the login form the first time a user opens the page.
// Only public values belong here: this file is served to everyone. Never put a password in it.
export default {
  // WebSocket URL of the broker, e.g. 'wss://xxxx.s1.eu.hivemq.cloud:8884/mqtt'.
  brokerUrl: '',
  // Lock id, e.g. 'door01'. Leave empty when one deployment serves many locks.
  deviceId: '',
};
