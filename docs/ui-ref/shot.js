// usage: node shot.js <out.png> [js-to-run-first] [wait-ms] [WxH]
// Runs optional JS in the old app's page, waits, saves a screenshot at the given size (default 1024x768).
const WebSocket = require('/home/bean/xylonic/node_modules/ws');
const http = require('http');
const fs = require('fs');
const [out, js, waitMs, size] = [process.argv[2], process.argv[3] || '', +process.argv[4] || 800, process.argv[5] || '1024x768'];
const [W, H] = size.split('x').map(Number);
http.get('http://localhost:9333/json', res => {
  let d = '';
  res.on('data', c => (d += c));
  res.on('end', () => {
    const t = JSON.parse(d).find(x => x.type === 'page' && !/mini/i.test(x.url));
    const ws = new WebSocket(t.webSocketDebuggerUrl);
    let id = 0;
    const pend = {};
    const send = (method, params = {}) => new Promise(r => { const i = ++id; pend[i] = r; ws.send(JSON.stringify({ id: i, method, params })); });
    ws.on('message', m => { const o = JSON.parse(m); if (pend[o.id]) { pend[o.id](o.result || o.error); delete pend[o.id]; } });
    ws.on('open', async () => {
      await send('Emulation.setDeviceMetricsOverride', { width: W, height: H, deviceScaleFactor: 1, mobile: false });
      let val = null;
      if (js) { const r = await send('Runtime.evaluate', { expression: js, awaitPromise: true, returnByValue: true }); val = r.result && r.result.value; }
      await new Promise(r => setTimeout(r, waitMs));
      const shot = await send('Page.captureScreenshot', { format: 'png' });
      fs.writeFileSync(out, Buffer.from(shot.data, 'base64'));
      if (val !== null && val !== undefined) console.log(typeof val === 'string' ? val : JSON.stringify(val));
      ws.close();
    });
  });
});
