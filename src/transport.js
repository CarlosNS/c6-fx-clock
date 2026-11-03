// How the control page talks to the clock (docs/PLAN-bluetooth.md, 3.5). One page, three links:
//   wifi:    served by the clock itself (Wi-Fi mode): WebSocket for commands, HTTP for JSON
//   ble:     Web Bluetooth from Chrome on a PC or phone (secure origin, e.g. http://localhost)
//   android: the Android app's WebView; Kotlin owns the Bluetooth link (window.ClockBridge)
// Every link offers the same calls: start(), send(cmd), request('status'|'faces'|'info').
// Bluetooth framing (3.2) is the same as tests/clocklib/protocol.py and the app's Framing.kt.
(function (g) {
  'use strict';
  const SVC = '7f1c0001-6c0c-4e0b-a1c6-0000c6c10c00';
  const CMD = '7f1c0002-6c0c-4e0b-a1c6-0000c6c10c00';
  const EVT = '7f1c0003-6c0c-4e0b-a1c6-0000c6c10c00';
  const LAST = 0x80, FIRST = 0x40, ID = 0x3f;
  const enc = new TextEncoder(), dec = new TextDecoder();

  function frame(id, payload, chunk) {
    const body = chunk - 1, parts = [];
    for (let i = 0; i < payload.length; i += body) parts.push(payload.slice(i, i + body));
    if (!parts.length) parts.push(new Uint8Array(0));
    return parts.map((p, i) => {
      const f = new Uint8Array(p.length + 1);
      f[0] = (id & ID) | (i === 0 ? FIRST : 0) | (i === parts.length - 1 ? LAST : 0);
      f.set(p, 1);
      return f;
    });
  }

  // Joins notifications back into whole messages; out-of-sequence data drops the partial message
  class Reassembler {
    constructor() { this.id = null; this.parts = []; this.dropped = 0; }
    feed(d) {
      if (!d.length) return null;
      const h = d[0], id = h & ID, body = d.slice(1);
      if (h & FIRST) { if (this.id !== null) this.dropped++; this.id = id; this.parts = [body]; }
      else if (this.id === id) this.parts.push(body);
      else { this.dropped++; this.id = null; this.parts = []; return null; }
      if (!(h & LAST)) return null;
      const out = new Uint8Array(this.parts.reduce((n, p) => n + p.length, 0));
      let at = 0;
      for (const p of this.parts) { out.set(p, at); at += p.length; }
      this.id = null; this.parts = [];
      return out;
    }
  }

  // Bluetooth links answer "get <what>" with a message {"t":"<what>",...}; this matches them up
  class Waiters {
    constructor() { this.list = []; }
    wait(t, ms) {
      return new Promise((resolve, reject) => {
        const w = {t, resolve, timer: setTimeout(() => { this.list = this.list.filter(x => x !== w); reject(new Error('timeout ' + t)); }, ms)};
        this.list.push(w);
      });
    }
    deliver(msg) {
      for (const w of this.list.filter(x => x.t === msg.t)) { clearTimeout(w.timer); w.resolve(msg); }
      this.list = this.list.filter(x => x.t !== msg.t);
    }
  }

  // Handlers given to start(): onState(connected), onMessage(msg) for everything the clock pushes
  class WifiLink {
    constructor() { this.kind = 'wifi'; this.pollMs = 1000; this.ws = null; }
    start(h) {
      const open = () => {
        this.ws = new WebSocket(`ws://${location.host}/ws`);
        this.ws.onopen = () => h.onState(true);
        this.ws.onclose = () => { h.onState(false); setTimeout(open, 1000); };
      };
      open();
    }
    send(s) { if (this.ws && this.ws.readyState === 1) { this.ws.send(s); return true; } return false; }
    async request(what) { return (await fetch('/' + what, {cache: 'no-store'})).json(); }
  }

  class WebBleLink {
    constructor() { this.kind = 'ble'; this.pollMs = 5000; this.needsPick = true; this.dev = null; this.cmd = null; this.queue = Promise.resolve(); this.waiters = new Waiters(); }
    // Needs a user gesture: Chrome shows its device chooser
    async pick() {
      this.dev = await navigator.bluetooth.requestDevice({filters: [{services: [SVC]}]});
      this.dev.addEventListener('gattserverdisconnected', () => { this.cmd = null; this.h.onState(false); this.reconnect(); });
      await this.open();
    }
    start(h) { this.h = h; }
    async open() {
      const gatt = await this.dev.gatt.connect();
      const svc = await gatt.getPrimaryService(SVC);
      const evt = await svc.getCharacteristic(EVT), cmd = await svc.getCharacteristic(CMD);
      const r = new Reassembler();
      evt.addEventListener('characteristicvaluechanged', e => {
        const v = e.target.value, out = r.feed(new Uint8Array(v.buffer, v.byteOffset, v.byteLength));
        if (!out) return;
        try { this.deliver(JSON.parse(dec.decode(out))); } catch (err) { console.warn('bad message', err); }
      });
      await evt.startNotifications();  // encrypted: the first time, the OS asks for the clock's code
      this.cmd = cmd;
      this.h.onState(true);
    }
    async reconnect() {
      for (let wait = 1000; !this.cmd; wait = Math.min(wait * 2, 15000)) {
        await new Promise(r => setTimeout(r, wait));
        try { await this.open(); } catch (e) { /* out of range or rebooting: keep trying */ }
      }
    }
    deliver(msg) { this.waiters.deliver(msg); this.h.onMessage(msg); }
    // Writes are queued: GATT allows one at a time
    send(s) {
      if (!this.cmd) return false;
      const c = this.cmd;
      this.queue = this.queue.then(() => c.writeValueWithResponse(enc.encode(s))).catch(e => console.warn('write failed', e));
      return true;
    }
    request(what) { const p = this.waiters.wait(what, 10000); if (!this.send('get ' + what)) return Promise.reject(new Error('not connected')); return p; }
  }

  // Kotlin calls window.clockState(bool), window.clockMessage(json string) and window.clockPickFailed(text)
  class AndroidLink {
    constructor() { this.kind = 'android'; this.pollMs = 5000; this.waiters = new Waiters(); this.up = false; this.needsPick = !g.ClockBridge.paired(); }
    start(h) {
      g.clockState = up => { this.up = !!up; if (this.up && this.picked) this.picked.resolve(); h.onState(this.up); };
      g.clockMessage = s => { const m = JSON.parse(s); this.waiters.deliver(m); h.onMessage(m); };
      g.clockPickFailed = text => { if (this.picked) this.picked.reject(new Error(text)); };
      g.ClockBridge.ready();
    }
    // The system's companion chooser, then the Bluetooth code; resolves once the clock is connected
    pick() {
      return new Promise((resolve, reject) => {
        this.picked = {resolve: () => { this.picked = null; resolve(); }, reject: e => { this.picked = null; reject(e); }};
        g.ClockBridge.pick();
      });
    }
    send(s) { return this.up && g.ClockBridge.send(s); }
    request(what) { const p = this.waiters.wait(what, 10000); if (!this.send('get ' + what)) return Promise.reject(new Error('not connected')); return p; }
  }

  // The clock serves the page over http:// from its own IP; anything else is Bluetooth
  function pick() {
    if (g.ClockBridge) return new AndroidLink();
    const q = new URLSearchParams(location.search).get('link');
    if (q === 'ble' || (q !== 'wifi' && location.protocol !== 'http:') || ['localhost', '127.0.0.1'].includes(location.hostname)) return new WebBleLink();
    return new WifiLink();
  }

  g.ClockLink = {pick, frame, Reassembler, SVC, CMD, EVT};
})(window);
