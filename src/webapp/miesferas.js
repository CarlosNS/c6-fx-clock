// Mis esferas: custom faces, made here (Android app only) and shown by face 28 on the watch
// (src/fx_custom.c). The page lists them in Esferas and edits them in #miesfera: a name (required, and not
// the name of any other face), the stretches of the day (each from a time until the next one starts, the
// first from 00:00, so the whole day is covered), each with its picture or GIF from the phone cropped to
// the screen, and the clock: digital (font, size, position) or analog (classic hands, or hands that come
// in from the edge), with colour, outline or shadow, seconds and date. The preview is drawn by the same
// rules as the watch from the same font data (fuentes.png / fuentes.json, made by tools/font_atlas.py).
//
// Everything is kept on the phone too (CustomFaces.kt, files/esferas/<id>/), so a new card can be
// filled again ("Restaurar en el reloj").
(function () {
'use strict';
const WN = 410, HN = 502, WA = 274, HA = 336, CORNER = 120;
const MAX_FRAMES = 40, MIN_DELAY = 100;
const FONTS = [
  {id: 'round', es: 'Redonda', en: 'Rounded', aa: ['round_s', 'round_m', 'round_xl', 'round_xxl']},
  {id: 'seg', es: 'Segmentos', en: 'Segments', aa: ['seg_s', 'seg_m', 'seg_xl', 'seg_xxl']},
  {id: 'nib', es: 'Caligráfica', en: 'Calligraphic', aa: ['nib_s', 'nib_m']},
  {id: 'pix', es: 'Píxel', en: 'Pixel', max: 10},
  {id: 'jersey', es: 'Jersey', en: 'Jersey', max: 7},
];
const WD = [['DOM', 'LUN', 'MAR', 'MIE', 'JUE', 'VIE', 'SAB'], ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT']];
const MO = [['ENE', 'FEB', 'MAR', 'ABR', 'MAY', 'JUN', 'JUL', 'AGO', 'SEP', 'OCT', 'NOV', 'DIC'],
            ['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN', 'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC']];

// ---------------------------------------------------------------- fonts (fuentes.json + atlas)
let FD = null, ATLAS = null, fontsP = null;
function loadFonts() { return fontsP || (fontsP = fetchFonts().catch(e => { fontsP = null; throw e; })); }  // once, shared
async function fetchFonts() {
  const fd = await (await fetch(A + 'fuentes.json')).json();
  const img = new Image();
  img.src = A + 'fuentes.png';
  await img.decode();
  const c = document.createElement('canvas');
  c.width = img.width; c.height = img.height;
  const x = c.getContext('2d');
  x.drawImage(img, 0, 0);
  const d = x.getImageData(0, 0, c.width, c.height).data;
  ATLAS = {w: c.width, a: new Uint8Array(c.width * c.height)};
  for (let i = 0; i < ATLAS.a.length; i++) ATLAS.a[i] = Math.round(d[i * 4] / 17);
  FD = fd;  // last: both are ready together
}

// ---------------------------------------------------------------- the time, exactly as fx_custom.c
let M = null;  // mask WN*HN
function mput(x, y, a) { if (x >= 0 && x < WN && y >= 0 && y < HN && a > M[y * WN + x]) M[y * WN + x] = a; }
function utf(s) { return Array.from(s); }
// font index 0..4 as in the firmware, -1 = VT323 (the date); returns [width, height]
function textRun(font, size, x, y, s, draw) {
  let pen = x;
  if (font >= 0 && font <= 2) {
    const names = FONTS[font].aa, f = FD[names[Math.min(size, names.length - 1)]];
    const g0 = f.g['0'];
    for (const ch of utf(s)) {
      const g = f.g[ch];
      if (!g) { pen += Math.trunc(g0.adv / 2); continue; }
      if (draw) for (let j = 0; j < g.h; j++) for (let i = 0; i < g.w; i++) {
        const a = ATLAS.a[(g.y + j) * ATLAS.w + g.x + i];
        if (a) mput(pen - f.pad + i, y - f.pad + j, a);
      }
      pen += g.adv;
    }
    return [pen - x, f.h];
  }
  if (font === 3) {
    const sc = size + 2, f = FD.pix;
    for (const ch of utf(s)) {
      const g = f.g[ch]; if (!g) continue;
      if (draw) for (let r = 0; r < 10; r++) for (let c = 0; c < g.w; c++)
        if (ATLAS.a[(g.y + r) * ATLAS.w + g.x + c]) for (let dy = 0; dy < sc; dy++) for (let dx = 0; dx < sc; dx++) mput(pen + c * sc + dx, y + r * sc + dy, 15);
      pen += (g.w + 1) * sc;
    }
    return [pen - x - sc, 10 * sc];
  }
  const f = font === 4 ? FD.jersey : FD.vt, sc = font === 4 ? size + 2 : size;
  for (const ch of utf(s)) {
    const g = f.g[ch]; if (!g) continue;
    if (draw) for (let j = 0; j < g.h; j++) for (let i = 0; i < g.w; i++)
      if (ATLAS.a[(g.y + j) * ATLAS.w + g.x + i]) for (let dy = 0; dy < sc; dy++) for (let dx = 0; dx < sc; dx++) mput(pen + (g.xo + i) * sc + dx, y + (g.yo + j) * sc + dy, 15);
    pen += g.adv * sc;
  }
  return [pen - x, f.h * sc];
}
// Lays the time out (and seconds and date) into M; returns the box [x0, y0, x1, y1]
function buildMask(F, hhmm, ss, date) {
  M = new Uint8Array(WN * HN);
  const fi = FONTS.findIndex(f => f.id === F.font);
  const [w1, h1] = textRun(fi, F.size, 0, 0, hhmm, false);
  const ssize = fi <= 2 ? Math.max(0, F.size - 1) : Math.max(1, Math.trunc((F.size + 2) / 2)) - 2;
  const [w2, h2] = F.secs ? textRun(fi, ssize, 0, 0, ss, false) : [0, 0];
  const gap = Math.max(2, Math.trunc(h1 / 16)), total = w1 + (F.secs ? gap + w2 : 0);
  const left = F.x - Math.trunc(total / 2);
  textRun(fi, F.size, left, F.y, hhmm, true);
  if (F.secs) textRun(fi, ssize, left + w1 + gap, F.y + h1 - h2, ss, true);
  let bottom = F.y + h1, wd = 0;
  const ds = h1 >= 100 ? 2 : 1;
  if (F.date) {
    let hd; [wd, hd] = textRun(-1, ds, 0, 0, date, false);
    const dy = bottom + Math.max(4, Math.trunc(h1 / 12));
    textRun(-1, ds, F.x - Math.trunc(wd / 2), dy, date, true);
    bottom = dy + hd;
  }
  const m = 8;
  let bx0 = Math.max(0, left - m - 6), bx1 = Math.min(WN, left + total + m + 6);
  if (F.date) {
    if (F.x - Math.trunc(wd / 2) - m < bx0) bx0 = Math.max(0, F.x - Math.trunc(wd / 2) - m);
    if (F.x + Math.trunc(wd / 2) + m > bx1) bx1 = Math.min(WN, F.x + Math.trunc(wd / 2) + m);
  }
  return [bx0, Math.max(0, F.y - m), bx1, Math.min(HN, bottom + m + 6)];
}
// Paints the mask onto RGBA pixels (WN x HN): outline or shadow, then the fill
function drawText(F, px, box) {
  const [x0, y0, x1, y1] = box, hgt = y1 - y0;
  const r = Math.min(4, Math.max(1, Math.trunc(hgt / 45))), o = r + 1;
  const col = parseInt(F.color.slice(1), 16), cr = col >> 16, cg = (col >> 8) & 255, cb = col & 255;
  const orr = cr * 3 + cg * 6 + cb < 800 ? 255 : 0;
  const mix = (i, R, G, B, a) => {
    px[i] += Math.trunc((R - px[i]) * a / 15); px[i + 1] += Math.trunc((G - px[i + 1]) * a / 15); px[i + 2] += Math.trunc((B - px[i + 2]) * a / 15);
  };
  for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
    const i = (y * WN + x) * 4;
    if (F.fx) {
      const sx = F.fx === 2 ? x - o : x, sy = F.fx === 2 ? y - o : y;
      let best = 0;
      for (let dy = -r; dy <= r && best < 15; dy++) for (let dx = -r; dx <= r; dx++) {
        if (dx * dx + dy * dy > r * r + r) continue;
        const xx = sx + dx, yy = sy + dy;
        if (xx >= 0 && xx < WN && yy >= 0 && yy < HN && M[yy * WN + xx] > best) best = M[yy * WN + xx];
      }
      if (best) F.fx === 2 ? mix(i, 0, 0, 0, Math.trunc(best * 9 / 15)) : mix(i, orr, orr, orr, best);
    }
    const a = M[y * WN + x];
    if (a) mix(i, cr, cg, cb, a);
  }
}
// ---------------------------------------------------------------- the analog clocks, as fx_custom.c
function edgePoint(dx, dy, m) {
  const a = WN / 2 - m, b = HN / 2 - m, r = CORNER - m;
  const tx = Math.abs(dx) > 1e-6 ? a / Math.abs(dx) : 1e9, ty = Math.abs(dy) > 1e-6 ? b / Math.abs(dy) : 1e9;
  let t = Math.min(tx, ty), px = dx * t, py = dy * t;
  if (Math.abs(px) > a - r && Math.abs(py) > b - r) {
    const cx = Math.sign(px) * (a - r), cy = Math.sign(py) * (b - r);
    const bb = dx * cx + dy * cy, disc = bb * bb - (cx * cx + cy * cy - r * r);
    t = bb + Math.sqrt(Math.max(0, disc)); px = dx * t; py = dy * t;
  }
  return [WN / 2 + px, HN / 2 + py];
}
const radiusOf = F => 100 + 30 * F.size;
function handSegs(F, d) {
  const s = [], ah = ((d.getHours() % 12) + d.getMinutes() / 60) * Math.PI / 6, am = (d.getMinutes() + d.getSeconds() / 60) * Math.PI / 30, as = d.getSeconds() * Math.PI / 30;
  const sin = Math.sin, cos = Math.cos;
  if (F.clock === 'analog') {
    const cx = F.ax, cy = F.ay, R = radiusOf(F);
    for (let i = 0; i < 12; i++) {
      const a = i * Math.PI / 6, dx = sin(a), dy = -cos(a), inr = i % 3 ? 0.9 : 0.82;
      s.push([cx + dx * R * inr, cy + dy * R * inr, cx + dx * R, cy + dy * R, i % 3 ? 1.5 : 2.5]);
    }
    s.push([cx - sin(ah) * R * 0.12, cy + cos(ah) * R * 0.12, cx + sin(ah) * R * 0.52, cy - cos(ah) * R * 0.52, 4 + F.size]);
    s.push([cx - sin(am) * R * 0.12, cy + cos(am) * R * 0.12, cx + sin(am) * R * 0.8, cy - cos(am) * R * 0.8, 2.8 + 0.7 * F.size]);
    if (F.secs) s.push([cx - sin(as) * R * 0.22, cy + cos(as) * R * 0.22, cx + sin(as) * R * 0.9, cy - cos(as) * R * 0.9, 1]);
    s.push([cx, cy, cx, cy, 6 + F.size]);
  } else {
    for (let i = 0; i < 12; i++) { const [ex, ey] = edgePoint(sin(i * Math.PI / 6), -cos(i * Math.PI / 6), 14); s.push([ex, ey, ex, ey, i % 3 ? 2.5 : 4]); }
    let [ex, ey] = edgePoint(sin(ah), -cos(ah), 24);
    s.push([ex, ey, ex - sin(ah) * 82, ey + cos(ah) * 82, 5 + 1.5 * F.size]);
    [ex, ey] = edgePoint(sin(am), -cos(am), 24);
    s.push([ex, ey, ex - sin(am) * 125, ey + cos(am) * 125, 3.5 + F.size]);
    if (F.secs) { [ex, ey] = edgePoint(sin(as), -cos(as), 14); s.push([ex, ey, ex, ey, 5]); }
  }
  return s;
}
function drawHands(ctx, F, d) {
  const segs = handSegs(F, d), col = parseInt(F.color.slice(1), 16);
  const dark = (col >> 16) * 3 + ((col >> 8) & 255) * 6 + (col & 255) < 800;
  const stroke = (s, color, grow, off) => {
    ctx.strokeStyle = ctx.fillStyle = color; ctx.lineCap = 'round';
    if (s[0] === s[2] && s[1] === s[3]) { ctx.beginPath(); ctx.arc(s[0] + off, s[1] + off, s[4] + grow, 0, 7); ctx.fill(); return; }
    ctx.lineWidth = 2 * (s[4] + grow); ctx.beginPath(); ctx.moveTo(s[0] + off, s[1] + off); ctx.lineTo(s[2] + off, s[3] + off); ctx.stroke();
  };
  if (F.fx === 1) for (const s of segs) stroke(s, dark ? '#fff' : '#000', 2, 0);
  if (F.fx === 2) for (const s of segs) stroke(s, 'rgba(0,0,0,.56)', 0, 3);
  for (const s of segs) stroke(s, F.color, 0, 0);
}
// The date alone, for the analog clocks (as build_date_mask)
function buildDateMask(F, date) {
  M = new Uint8Array(WN * HN);
  const [wd, hd] = textRun(-1, 2, 0, 0, date, false);
  const cx = F.clock === 'analog' ? F.ax : WN / 2, cy = F.clock === 'analog' ? F.ay + Math.trunc(radiusOf(F) * 42 / 100) : HN / 2 - Math.trunc(hd / 2);
  textRun(-1, 2, cx - Math.trunc(wd / 2), cy, date, true);
  const m = 8;
  return [Math.max(0, cx - Math.trunc(wd / 2) - m), Math.max(0, cy - m), Math.min(WN, cx + Math.trunc(wd / 2) + m), Math.min(HN, cy + hd + m)];
}

function nowStrings() {
  const d = new Date(), p = n => String(n).padStart(2, '0');
  return [p(d.getHours()) + ':' + p(d.getMinutes()), p(d.getSeconds()), `${WD[LANG][d.getDay()]} ${d.getDate()} ${MO[LANG][d.getMonth()]}`];
}

// ---------------------------------------------------------------- state
let LOCAL = [], WATCH = null, ED = null, busy = false;
let autoDone = false;  // faces kept only on the phone already sent on this connection (or tried)
const box = () => $('mislist');

function inject() {
  const host = $('mis');
  host.innerHTML = `
  <p class="lbl">${tr('Mis esferas', 'My faces')}</p>
  <div class="panel red hide" id="missd"></div>
  <div class="stack" id="mislist" style="gap:10px"></div>
  <div class="inline" style="gap:10px">
    <button class="btn gold" id="misnew" style="flex:1">${tr('+ Nueva esfera', '+ New face')}</button>
    <button class="btn hide" id="misrest" style="flex:1">${tr('Restaurar en el reloj', 'Restore to the watch')}</button>
  </div>
  <div class="dim hide" id="misst"></div>`;
  // While a face goes to the watch: a window over everything (nothing else can be changed meanwhile;
  // the page's back does nothing either, see route() in index.html)
  const m = document.createElement('div');
  m.className = 'modal hide'; m.id = 'missend'; m.setAttribute('role', 'dialog');
  m.innerHTML = `<div class="scrim"></div><div class="box stack" style="gap:12px">
    <h2 id="missendt"></h2><div class="bar"><div id="missendbar" style="width:0%"></div></div>
    <div class="dim" id="missendst"></div>
    <div class="dim small">${tr('Deja la app abierta y el reloj cerca hasta que termine.', 'Keep the app open and the watch near until it ends.')}</div></div>`;
  document.body.appendChild(m);
  $('misnew').onclick = () => openEditor(null);
  $('misrest').onclick = () => restore();
  const v = document.createElement('section');
  v.className = 'view hide'; v.id = 'v-miesfera'; v.dataset.parent = 'esferas';
  v.innerHTML = `
  <div class="top"><button class="back" aria-label="${tr('Atrás', 'Back')}"><svg width="20" height="20" viewBox="0 0 10 10" shape-rendering="crispEdges" aria-hidden="true"><path fill="#F9B25A" d="M5 1h2v2H5v1H4v2h1v1h2v2H5V8H4V7H3V6H2V4h1V3h1V2h1z"/></svg></button><h1 id="medt">${tr('Nueva esfera', 'New face')}</h1></div>
  <div class="list"><label class="row col"><span class="dim">${tr('Nombre', 'Name')}</span><input type="text" id="mename" maxlength="30"></label></div>
  <div class="panel stack" style="gap:10px;align-items:center">
    <canvas id="mecv" width="${WN}" height="${HN}" style="width:min(82vw,300px);aspect-ratio:${WN}/${HN};border-radius:24%;background:#000;touch-action:none;image-rendering:pixelated"></canvas>
    <div class="seg" role="group"><button id="memcrop" class="on">${tr('Encuadrar fondo', 'Frame the picture')}</button><button id="memtime">${tr('Mover la hora', 'Move the time')}</button></div>
    <div class="dim small" id="mehint"></div>
    <input type="file" id="mefile" accept="image/*" class="hide">
    <button class="btn full" id="mepick">${tr('Elegir imagen o GIF', 'Pick a picture or GIF')}</button>
    <div class="dim small" id="meinfo"></div>
  </div>
  <p class="lbl">${tr('Tramos del día', 'Stretches of the day')}</p>
  <div class="panel stack" style="gap:10px">
    <div class="inline" id="meslots" style="gap:6px;flex-wrap:wrap"></div>
    <div class="inline" style="gap:8px;flex-wrap:wrap">
      <label class="inline" style="gap:6px"><span class="dim">${tr('Desde', 'From')}</span><input type="time" id="mefrom" style="background:var(--noche);color:var(--crema);border:3px solid var(--linea);min-height:42px;padding:0 6px;font:inherit;color-scheme:dark"></label>
      <button class="btn" id="meadd">${tr('+ Tramo', '+ Stretch')}</button>
      <button class="btn" id="medrop" style="color:var(--coral)">${tr('Quitar', 'Remove')}</button>
    </div>
    <div class="dim small">${tr('Cada tramo muestra su imagen hasta que empieza el siguiente; el primero empieza a las 00:00, así que todo el día queda cubierto. Elige el tramo y luego su imagen.', 'Each stretch shows its picture until the next one starts; the first starts at 00:00, so the whole day is covered. Pick the stretch, then its picture.')}</div>
  </div>
  <p class="lbl">${tr('La hora', 'The time')}</p>
  <div class="list">
    <div class="row"><div class="t"><b>${tr('Reloj', 'Clock')}</b></div><div class="seg" role="group"><button data-clock="digital">${tr('Digital', 'Digital')}</button><button data-clock="analog">${tr('Clásico', 'Classic')}</button><button data-clock="edge">${tr('Borde', 'Edge')}</button></div></div>
    <div class="row" id="mefontrow"><div class="t"><b>${tr('Fuente', 'Font')}</b></div><select id="mefont">${FONTS.map(f => `<option value="${f.id}">${LANG ? f.en : f.es}</option>`).join('')}</select></div>
    <div class="row col"><div class="inline" style="justify-content:space-between"><span class="tt">${tr('Tamaño', 'Size')}</span><span class="j" id="mesizev" style="font-size:22px"></span></div><input type="range" id="mesize" min="0" max="3" step="1"></div>
    <div class="row"><div class="t"><b>${tr('Color', 'Colour')}</b></div><input type="color" id="mecolor" style="width:64px;height:40px;border:3px solid var(--linea);background:none;padding:0"></div>
    <div class="row"><div class="t"><b>${tr('Para que se lea', 'To read it')}</b></div><div class="seg" role="group"><button data-fx="0">${tr('Nada', 'None')}</button><button data-fx="1">${tr('Contorno', 'Outline')}</button><button data-fx="2">${tr('Sombra', 'Shadow')}</button></div></div>
    <div class="row"><div class="t"><b>${tr('Segundos', 'Seconds')}</b></div><input type="checkbox" class="sw" id="mesecs"></div>
    <div class="row"><div class="t"><b>${tr('Fecha', 'Date')}</b></div><input type="checkbox" class="sw" id="medate"></div>
  </div>
  <div class="stack" style="gap:10px">
    <div class="bar hide" id="mebar"><div></div></div>
    <div class="dim" id="mest"></div>
    <button class="btn gold full" id="mesave">${tr('Guardar en el reloj', 'Save to the watch')}</button>
    <div class="inline" style="gap:10px"><button class="btn" id="metry" style="flex:1">${tr('Probar en el reloj', 'Try it on the watch')}</button><button class="btn" id="mesel" style="flex:1">${tr('Ponerla', 'Use it')}</button></div>
    <button class="btn full hide" id="medel" style="color:var(--coral)">${tr('Eliminar esfera', 'Delete face')}</button>
  </div>`;
  document.querySelector('main').appendChild(v);
  v.querySelector('.back').onclick = () => { location.hash = '#esferas'; };
  bindEditor();
}

// ---------------------------------------------------------------- the list
function idsKnown() { return new Set([...LOCAL.map(f => f.id), ...(WATCH && WATCH.list ? WATCH.list.map(f => f.id) : [])]); }
function sdOk() { return WATCH && WATCH.sd === 'ok'; }
async function refresh() {
  try { LOCAL = JSON.parse(ClockBridge.customLocal()); } catch (e) { LOCAL = []; }
  renderList();
  if (window.curFaceRefresh) curFaceRefresh();
  if (up) ClockBridge.customWatch();
}
function renderList() {
  const onWatch = new Set(WATCH && WATCH.list ? WATCH.list.map(f => f.id) : []);
  const items = [...LOCAL];
  if (WATCH && WATCH.list) for (const w of WATCH.list) if (!items.find(f => f.id === w.id)) items.push({id: w.id, name: w.name, watchOnly: true});
  items.sort((a, b) => a.id - b.id);
  const sd = $('missd');
  sd.classList.toggle('hide', !WATCH || sdOk() || !!WATCH.err);
  if (WATCH && !sdOk() && !WATCH.err) sd.textContent = WATCH.sd === 'bad' ? tr('La microSD del reloj no se puede leer: Mis esferas la necesitan.', 'The watch’s microSD cannot be read: My faces need it.') : tr('El reloj no tiene microSD: Mis esferas la necesitan para guardarse.', 'The watch has no microSD: My faces need one.');
  $('misnew').disabled = WATCH !== null && !WATCH.err && !sdOk();
  const missing = WATCH && sdOk() && LOCAL.some(f => !onWatch.has(f.id));
  $('misrest').classList.toggle('hide', !missing);
  if (!items.length) { box().innerHTML = `<div class="panel dim">${tr('Aún no tienes ninguna. Crea una con tu foto o un GIF.', 'None yet. Make one with your photo or a GIF.')}</div>`; return; }
  box().innerHTML = items.map(f => {
    const sel = WATCH && WATCH.sel === f.id && status.fx === 28;
    const where = f.watchOnly ? tr('solo en el reloj', 'only on the watch') : !WATCH || WATCH.err ? '' : onWatch.has(f.id) ? tr('en el reloj', 'on the watch') : tr('solo en el móvil', 'only on the phone');
    const thumb = f.watchOnly ? '' : `<img class="prev" alt="" src="https://appassets.androidplatform.net/mis/${f.id}/thumb.jpg?v=${f.v || 0}" style="width:66px;height:81px;border:3px solid var(--linea);border-radius:12px;flex:none">`;
    return `<div class="panel${sel ? ' gold' : ''}" style="display:flex;gap:12px;align-items:center" data-id="${f.id}">
      ${thumb}<div style="flex:1;min-width:0"><div class="tt">${esc(f.name || tr('Sin nombre', 'No name'))}</div><div class="dim small">${where}${f.anim ? ' · GIF' : ''}${sel ? ' · ' + tr('puesta', 'showing') : ''}</div></div>
      <div class="stack" style="gap:6px">${onWatch.has(f.id) && sdOk() ? `<button class="btn mpick" style="min-height:40px">${tr('Poner', 'Use')}</button>` : ''}${f.watchOnly ? '' : `<button class="btn medit" style="min-height:40px">${tr('Editar', 'Edit')}</button>`}</div></div>`;
  }).join('');
  for (const el of box().querySelectorAll('[data-id]')) {
    const id = +el.dataset.id;
    const p = el.querySelector('.mpick'); if (p) p.onclick = () => { ClockBridge.customCmd('cust sel ' + id); if (WATCH) WATCH.sel = id; status.fx = 28; renderList(); };
    const e = el.querySelector('.medit'); if (e) e.onclick = () => openEditor(id);
  }
}
function sending(title, pct, text) {
  const m = $('missend');
  if (title === null) { m.classList.add('hide'); return; }
  m.classList.remove('hide'); $('missendt').textContent = title;
  $('missendbar').style.width = (pct || 0) + '%'; $('missendst').textContent = text || '';
}
// Every face kept on the phone (ids: only these) to the watch again
function restore(ids, auto) {
  if (busy) return; busy = true;
  const list = Array.isArray(ids) ? ids : [];
  sending(auto ? tr('Enviando tus esferas al reloj', 'Sending your faces to the watch') : tr('Restaurando en el reloj', 'Restoring to the watch'), 0,
    auto ? tr('Estaban guardadas solo en el móvil.', 'They were kept only on the phone.') : '');
  ClockBridge.customRestore(list.join(','));
}
// Back with the watch: what was kept only on the phone (saved while it was away, or a card that
// was changed or wiped) goes to it by itself, once per connection
function autoSend() {
  if (autoDone || busy || !up || !sdOk() || !WATCH.list) return;
  const on = new Set(WATCH.list.map(f => f.id)), missing = LOCAL.filter(f => !on.has(f.id)).map(f => f.id);
  autoDone = true;
  if (missing.length) restore(missing, true);
}

// ---------------------------------------------------------------- the editor
function defaults() { return {name: '', clock: 'digital', font: 'round', size: 2, x: WN / 2, y: 140, ax: WN / 2, ay: HN / 2, color: '#ffffff', fx: 1, secs: 0, date: 1, anim: 0}; }
// The stretches: {key, from (minutes), SRC, T, dirty, pic, src}; SRC and T below are the chosen one's
const slotOf = () => ED.slots[ED.cur];
function keepSlot() { const sl = slotOf(); if (sl) { sl.SRC = SRC; sl.T = T; } }
function pickSlot(i) {
  keepSlot(); ED.cur = i;
  const sl = slotOf(); SRC = sl.SRC; T = sl.T || {s: 1, ox: 0, oy: 0};
  ED.F.anim = SRC && SRC.frames.length > 1 ? 1 : 0; frame = 0;
  showInfo(); renderSlots();
}
const hhmmOf = m => String(Math.floor(m / 60)).padStart(2, '0') + ':' + String(m % 60).padStart(2, '0');
function renderSlots() {
  const box = $('meslots'); if (!box || !ED) return;
  box.innerHTML = ED.slots.map((sl, i) => `<button class="btn${i === ED.cur ? ' gold' : ''}" data-i="${i}" style="min-height:40px;padding:0 10px${sl.SRC || sl.pic ? '' : ';color:var(--coral)'}">${hhmmOf(sl.from)}${sl.SRC || sl.pic ? '' : ' !'}</button>`).join('');
  for (const b of box.querySelectorAll('button')) b.onclick = () => pickSlot(+b.dataset.i);
  $('mefrom').value = hhmmOf(slotOf().from); $('mefrom').disabled = ED.cur === 0;
  $('medrop').disabled = ED.cur === 0;
}
function showInfo() {
  if (!SRC) { $('meinfo').textContent = tr('Este tramo aún no tiene imagen: elige una.', 'This stretch has no picture yet: pick one.'); return; }
  const fr = SRC.frames, dur = fr.reduce((a, f) => a + f.delay, 0);
  $('meinfo').textContent = fr.length > 1
    ? tr(`GIF: ${fr.length} fotogramas, ${(dur / 1000).toFixed(1)} s`, `GIF: ${fr.length} frames, ${(dur / 1000).toFixed(1)} s`) + (SRC.cut ? tr(' (reducido)', ' (reduced)') : '') + tr(' · se repite mientras la pantalla está encendida', ' · it loops while the screen is on')
    : tr(`Imagen de ${SRC.w}×${SRC.h}`, `Picture ${SRC.w}×${SRC.h}`);
}
let SRC = null;  // {frames:[{bmp,delay}], w, h, ext, file|url}
let T = {s: 1, ox: 0, oy: 0};  // picture -> screen: x' = ox + x*s
let mode = 'crop', frame = 0, frameT = 0, raf = 0;

async function openEditor(id) {
  const f = id && LOCAL.find(l => l.id === id);
  ED = {id: id || Math.max(0, ...idsKnown()) + 1, isNew: !id, F: defaults(), saved: !!id, slots: [{key: 0, from: 0, SRC: null, T: null, dirty: true}], cur: 0};
  SRC = null;
  location.hash = '#miesfera';
  await loadFonts();
  if (f) {
    try {
      const j = await (await fetch(`https://appassets.androidplatform.net/mis/${id}/face.json?v=${Date.now()}`)).json();
      Object.assign(ED.F, j);
      const list = j.slots && j.slots.length ? j.slots : [{from: 0, pic: j.anim ? 'anim.bin' : 'bg.jpg', src: j.src, crop: j.crop}];
      ED.slots = list.map((x, k) => ({key: k, from: x.from || 0, SRC: null, T: x.crop || null, dirty: false, pic: x.pic, src: x.src}));
      delete ED.F.slots; delete ED.F.crop; delete ED.F.src;
      for (let k = 0; k < ED.slots.length; k++) {
        const sl = ED.slots[k];
        ED.cur = k; SRC = null; T = sl.T || {s: 1, ox: 0, oy: 0};
        if (sl.src) await loadSource(await (await fetch(`https://appassets.androidplatform.net/mis/${id}/${sl.src}?v=${Date.now()}`)).blob(), sl.src.split('.').pop(), !!sl.T);
        sl.SRC = SRC; sl.T = T;
      }
      ED.cur = 0; SRC = ED.slots[0].SRC; T = ED.slots[0].T || T;
    } catch (e) { $('mest').textContent = tr('No se pudo abrir: ', 'Could not open it: ') + e.message; }
  }
  $('medt').textContent = ED.isNew ? tr('Nueva esfera', 'New face') : tr('Editar esfera', 'Edit face');
  $('medel').classList.toggle('hide', ED.isNew);
  syncControls();
  pickSlot(0);
  setMode('crop');
  loop();
}

async function loadSource(blob, ext, keepCrop) {
  const isGif = /gif|webp/i.test(blob.type || ext) && 'ImageDecoder' in window;
  const frames = [];
  if (isGif) {
    const dec = new ImageDecoder({data: await blob.arrayBuffer(), type: blob.type || 'image/' + ext});
    await dec.tracks.ready;
    const n = dec.tracks.selectedTrack.frameCount;
    for (let i = 0; i < n; i++) {
      const {image} = await dec.decode({frameIndex: i});
      frames.push({bmp: await createImageBitmap(image), delay: Math.max(20, (image.duration || 100000) / 1000)});
      image.close();
    }
  } else frames.push({bmp: await createImageBitmap(blob), delay: 0});
  let fr = frames;
  if (fr.length > 1) {
    // at most 10 per second: merge frames shorter than 100 ms; at most 40 frames: keep evenly spaced ones
    const merged = [];
    for (const f of fr) { const last = merged[merged.length - 1]; if (last && last.delay < MIN_DELAY) last.delay += f.delay; else merged.push({...f}); }
    fr = merged;
    if (fr.length > MAX_FRAMES) {
      const total = fr.reduce((a, f) => a + f.delay, 0), out = [];
      for (let k = 0; k < MAX_FRAMES; k++) out.push({bmp: fr[Math.floor(k * fr.length / MAX_FRAMES)].bmp, delay: Math.round(total / MAX_FRAMES)});
      fr = out;
    }
  }
  SRC = {frames: fr, w: fr[0].bmp.width, h: fr[0].bmp.height, ext: ext || 'jpg', blob, cut: frames.length !== fr.length || frames.length > MAX_FRAMES};
  ED.F.anim = fr.length > 1 ? 1 : 0;
  if (!keepCrop) {  // fill the screen, centred
    const s = Math.max(WN / SRC.w, HN / SRC.h);
    T = {s, ox: (WN - SRC.w * s) / 2, oy: (HN - SRC.h * s) / 2};
  }
  showInfo(); frame = 0; frameT = performance.now(); return;
  const dur = fr.reduce((a, f) => a + f.delay, 0);
  $('meinfo').textContent = fr.length > 1
    ? tr(`GIF: ${fr.length} fotogramas, ${(dur / 1000).toFixed(1)} s`, `GIF: ${fr.length} frames, ${(dur / 1000).toFixed(1)} s`) + (SRC.cut ? tr(' (reducido)', ' (reduced)') : '') + tr(' · se repite mientras la pantalla está encendida', ' · it loops while the screen is on')
    : tr(`Imagen de ${SRC.w}×${SRC.h}`, `Picture ${SRC.w}×${SRC.h}`);
  frame = 0; frameT = performance.now();
}

function syncControls() {
  const F = ED.F;
  $('mename').value = F.name;
  $('mefont').value = F.font;
  syncClock();
  setSizeRange();
  $('mecolor').value = F.color;
  for (const b of document.querySelectorAll('#v-miesfera [data-fx]')) b.classList.toggle('on', +b.dataset.fx === F.fx);
  $('mesecs').checked = !!F.secs; $('medate').checked = !!F.date;
  $('mest').textContent = ''; $('mebar').classList.add('hide');
  $('metry').disabled = $('mesel').disabled = !ED.saved;
}
function syncClock() {
  for (const b of document.querySelectorAll('#v-miesfera [data-clock]')) b.classList.toggle('on', b.dataset.clock === ED.F.clock);
  $('mefontrow').classList.toggle('hide', ED.F.clock !== 'digital');
  $('memtime').classList.toggle('hide', ED.F.clock === 'edge');
  if (ED.F.clock === 'edge' && mode === 'time') setMode('crop');
}
function setSizeRange() {
  if (ED.F.clock !== 'digital') {  // analog: four sizes (the radius, or the thickness of the edge hands)
    ED.F.size = Math.min(ED.F.size, 3);
    $('mesize').max = 3; $('mesize').value = ED.F.size;
    $('mesizev').textContent = ED.F.clock === 'analog' ? radiusOf(ED.F) + ' px' : ['S', 'M', 'L', 'XL'][ED.F.size];
    return;
  }
  if (!FD) return;
  const fd = FONTS.find(f => f.id === ED.F.font), max = fd.aa ? fd.aa.length - 1 : fd.max;
  ED.F.size = Math.min(ED.F.size, max);
  $('mesize').max = max; $('mesize').value = ED.F.size;
  const h = fd.aa ? FD[fd.aa[ED.F.size]].h : fd.id === 'pix' ? 10 * (ED.F.size + 2) : FD.jersey.h * (ED.F.size + 2);
  $('mesizev').textContent = h + ' px';
}
function changed() { ED.saved = false; $('metry').disabled = $('mesel').disabled = true; }
function setMode(m) {
  mode = m;
  $('memcrop').classList.toggle('on', m === 'crop'); $('memtime').classList.toggle('on', m === 'time');
  $('mehint').textContent = m === 'crop' ? tr('Arrastra para mover y pellizca para acercar. Lo de fuera de las esquinas no se ve.', 'Drag to move, pinch to zoom. Outside the corners does not show.')
    : ED.F.clock === 'analog' ? tr('Arrastra el reloj; se centra solo cerca del medio.', 'Drag the clock; it snaps to the middle.')
    : tr('Arrastra la hora; se centra sola cerca del medio.', 'Drag the time; it snaps to the middle.');
}

function bindEditor() {
  $('memcrop').onclick = () => setMode('crop');
  $('memtime').onclick = () => setMode('time');
  $('mepick').onclick = () => $('mefile').click();
  $('mefile').onchange = async () => {
    const file = $('mefile').files[0]; if (!file) return;
    try {
      $('meinfo').textContent = tr('Cargando…', 'Loading…');
      await loadSource(file, (file.name.split('.').pop() || 'jpg').toLowerCase(), false);
      slotOf().dirty = true; keepSlot(); renderSlots(); changed(); setMode('crop');
    } catch (e) { console.error(e); $('meinfo').textContent = tr('No se pudo abrir esa imagen', 'That picture could not be opened') + ' (' + e.message + ')'; }
    $('mefile').value = '';
  };
  $('mename').oninput = () => { ED.F.name = $('mename').value; changed(); };
  $('mefont').onchange = () => { ED.F.font = $('mefont').value; setSizeRange(); changed(); };
  $('mesize').oninput = () => { ED.F.size = +$('mesize').value; setSizeRange(); changed(); };
  $('mecolor').oninput = () => { ED.F.color = $('mecolor').value; changed(); };
  for (const b of document.querySelectorAll('#v-miesfera [data-fx]')) b.onclick = () => { ED.F.fx = +b.dataset.fx; syncFx(); changed(); };
  $('mesecs').onchange = () => { ED.F.secs = $('mesecs').checked ? 1 : 0; changed(); };
  for (const b of document.querySelectorAll('#v-miesfera [data-clock]')) b.onclick = () => { ED.F.clock = b.dataset.clock; syncClock(); setSizeRange(); setMode(mode); changed(); };
  // the stretches
  $('mefrom').onchange = () => {
    const v = /^(\d\d):(\d\d)/.exec($('mefrom').value); if (!v || ED.cur === 0) return;
    const sl = slotOf(); sl.from = +v[1] * 60 + +v[2]; keepSlot();
    ED.slots.sort((a, b) => a.from - b.from); ED.cur = ED.slots.indexOf(sl); renderSlots(); changed();
  };
  $('meadd').onclick = () => {
    if (ED.slots.length >= 8) { $('mest').textContent = tr('Como mucho 8 tramos.', 'At most 8 stretches.'); return; }
    const last = ED.slots[ED.slots.length - 1].from, from = Math.min(23 * 60, last + 6 * 60 - (last + 6 * 60) % 60);
    const sl = {key: Math.max(...ED.slots.map(x => x.key)) + 1, from: ED.slots.some(x => x.from === from) ? Math.min(1439, last + 60) : from, SRC: null, T: null, dirty: true};
    keepSlot(); ED.slots.push(sl); ED.slots.sort((a, b) => a.from - b.from); pickSlot(ED.slots.indexOf(sl)); changed();
  };
  $('medrop').onclick = () => { if (ED.cur === 0) return; ED.slots.splice(ED.cur, 1); ED.cur = 0; SRC = ED.slots[0].SRC; T = ED.slots[0].T; pickSlot(Math.max(0, ED.cur)); changed(); };
  $('medate').onchange = () => { ED.F.date = $('medate').checked ? 1 : 0; changed(); };
  $('mesave').onclick = save;
  $('metry').onclick = () => ClockBridge.customCmd('cust try ' + ED.id);
  $('mesel').onclick = () => { ClockBridge.customCmd('cust sel ' + ED.id); status.fx = 28; if (WATCH) WATCH.sel = ED.id; };
  $('medel').onclick = () => {
    if (!ask(tr('¿Eliminar esta esfera?', 'Delete this face?'), tr('Se borra del reloj y del móvil.', 'It is deleted from the watch and the phone.'), tr('Eliminar', 'Delete'))) return;
    ClockBridge.customDelete(ED.id, true); location.hash = '#esferas'; setTimeout(refresh, 500);
  };
  // dragging and pinching on the preview
  const cv = $('mecv'), pts = new Map();
  let start = null;
  const toCanvas = e => { const r = cv.getBoundingClientRect(); return [(e.clientX - r.left) * WN / r.width, (e.clientY - r.top) * HN / r.height]; };
  cv.onpointerdown = e => { cv.setPointerCapture(e.pointerId); pts.set(e.pointerId, toCanvas(e)); start = null; ED.F._x = ED.F.x; ED.F._ax = ED.F.ax; ED.F._ay = ED.F.ay; };
  cv.onpointerup = cv.onpointercancel = e => { pts.delete(e.pointerId); start = null; };
  cv.onpointermove = e => {
    if (!pts.has(e.pointerId)) return;
    const prev = pts.get(e.pointerId), p = toCanvas(e);
    pts.set(e.pointerId, p);
    if (mode === 'time') {
      if (pts.size !== 1) return;
      if (ED.F.clock === 'analog') {  // the dial's centre, snapping to the middle both ways
        ED.F._ax = (ED.F._ax ?? ED.F.ax) + p[0] - prev[0]; ED.F._ay = (ED.F._ay ?? ED.F.ay) + p[1] - prev[1];
        ED.F.ax = Math.abs(ED.F._ax - WN / 2) < 8 ? WN / 2 : Math.max(0, Math.min(WN, Math.round(ED.F._ax)));
        ED.F.ay = Math.abs(ED.F._ay - HN / 2) < 8 ? HN / 2 : Math.max(0, Math.min(HN, Math.round(ED.F._ay)));
        changed(); return;
      }
      let x = Math.round(ED.F.x + p[0] - prev[0]), y = Math.round(ED.F.y + p[1] - prev[1]);
      ED.F._x = (ED.F._x ?? ED.F.x) + p[0] - prev[0];
      x = Math.abs(ED.F._x - WN / 2) < 8 ? WN / 2 : Math.round(ED.F._x);  // snaps to the middle
      ED.F.x = Math.max(0, Math.min(WN, x)); ED.F.y = Math.max(0, Math.min(HN - 20, y));
      changed(); return;
    }
    if (!SRC) return;
    if (pts.size === 1) { T.ox += p[0] - prev[0]; T.oy += p[1] - prev[1]; }
    else if (pts.size === 2) {
      const [a, b] = [...pts.values()];
      const other = [...pts.entries()].find(([k]) => k !== e.pointerId)[1];
      const d0 = Math.hypot(prev[0] - other[0], prev[1] - other[1]), d1 = Math.hypot(a[0] - b[0], a[1] - b[1]);
      if (d0 > 10) zoomAt((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, d1 / d0);
    }
    changed(); slotOf().dirty = true;
  };
  cv.onwheel = e => { if (mode === 'crop' && SRC) { e.preventDefault(); const p = toCanvas(e); zoomAt(p[0], p[1], e.deltaY < 0 ? 1.08 : 1 / 1.08); changed(); slotOf().dirty = true; } };
}
function syncFx() { for (const b of document.querySelectorAll('#v-miesfera [data-fx]')) b.classList.toggle('on', +b.dataset.fx === ED.F.fx); }
function zoomAt(cx, cy, k) {
  const min = Math.max(WN / SRC.w, HN / SRC.h) * 0.25, s = Math.min(Math.max(T.s * k, min), 8 * min * 4);
  k = s / T.s; T.ox = cx - (cx - T.ox) * k; T.oy = cy - (cy - T.oy) * k; T.s = s;
}

// One frame of the picture as the watch shows it (W x H), into ctx
function paintBg(ctx, w, h, k) {
  ctx.fillStyle = '#000'; ctx.fillRect(0, 0, w, h);
  if (!SRC) return;
  const f = SRC.frames[k], sc = w / WN;
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(f.bmp, T.ox * sc, T.oy * sc, SRC.w * T.s * sc, SRC.h * T.s * sc);
}
let pcv = null;
function compose(k, withGuides) {
  if (!pcv) { pcv = document.createElement('canvas'); pcv.width = WN; pcv.height = HN; }
  const x = pcv.getContext('2d');
  if (ED.F.anim) {  // the watch enlarges a 274x336 frame: show the same softness
    const t = document.createElement('canvas'); t.width = WA; t.height = HA;
    paintBg(t.getContext('2d'), WA, HA, k);
    x.imageSmoothingEnabled = false; x.drawImage(t, 0, 0, WN, HN);
  } else paintBg(x, WN, HN, k);
  if (FD && (ED.F.clock === 'digital' || ED.F.date)) {  // the fonts may still be loading
    const img = x.getImageData(0, 0, WN, HN);
    const [hhmm, ss, date] = nowStrings();
    const bx = ED.F.clock === 'digital' ? buildMask(ED.F, hhmm, ss, date) : buildDateMask(ED.F, date);
    drawText(ED.F, img.data, bx);
    x.putImageData(img, 0, 0);
  }
  if (ED.F.clock !== 'digital') drawHands(x, ED.F, new Date());
  const out = $('mecv').getContext('2d');
  out.drawImage(pcv, 0, 0);
  if (withGuides) {  // what the rounded screen hides
    out.save(); out.fillStyle = 'rgba(0,0,0,.6)';
    out.beginPath(); out.rect(0, 0, WN, HN); out.roundRect(2, 2, WN - 4, HN - 4, CORNER); out.fill('evenodd'); out.restore();
    if (mode === 'time' && (ED.F.clock === 'analog' ? ED.F.ax : ED.F.x) === WN / 2) { out.fillStyle = 'rgba(249,178,90,.6)'; out.fillRect(WN / 2, 0, 1, HN); }
    if (mode === 'time' && ED.F.clock === 'analog' && ED.F.ay === HN / 2) { out.fillStyle = 'rgba(249,178,90,.6)'; out.fillRect(0, HN / 2, WN, 1); }
  }
}
function loop() {
  cancelAnimationFrame(raf);
  if (view !== 'miesfera') return;
  if (SRC && SRC.frames.length > 1) {
    const now = performance.now();
    if (now - frameT > SRC.frames[frame].delay) { frame = (frame + 1) % SRC.frames.length; frameT = now; }
  }
  compose(SRC && SRC.frames.length > 1 ? frame : 0, true);
  raf = requestAnimationFrame(() => setTimeout(loop, 60));
}

// ---------------------------------------------------------------- saving
const b64 = buf => { let s = ''; const u = new Uint8Array(buf); for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000)); return btoa(s); };
const blobOf = (cv, q) => new Promise(r => cv.toBlob(r, 'image/jpeg', q));
async function buildMain() {
  if (!ED.F.anim) {
    const c = document.createElement('canvas'); c.width = WN; c.height = HN;
    paintBg(c.getContext('2d'), WN, HN, 0);
    return b64(await (await blobOf(c, 0.9)).arrayBuffer());
  }
  // anim.bin: "MAN1", frames, 0, then per frame delay, 0, offset, size; then the JPEGs (274 x 336)
  const jpgs = [];
  for (let k = 0; k < SRC.frames.length; k++) {
    const c = document.createElement('canvas'); c.width = WA; c.height = HA;
    paintBg(c.getContext('2d'), WA, HA, k);
    jpgs.push(new Uint8Array(await (await blobOf(c, 0.82)).arrayBuffer()));
  }
  const n = jpgs.length, head = 8 + n * 12, total = head + jpgs.reduce((a, j) => a + j.length, 0);
  if (total > 3 * 1024 * 1024) throw new Error(tr('el GIF ocupa demasiado (más de 3 MB)', 'the GIF is too big (over 3 MB)'));
  const out = new Uint8Array(total), dv = new DataView(out.buffer);
  out.set([77, 65, 78, 49]); dv.setUint16(4, n, true);
  let off = head;
  jpgs.forEach((j, k) => {
    dv.setUint16(8 + k * 12, Math.min(65535, Math.round(SRC.frames[k].delay)), true);
    dv.setUint32(12 + k * 12, off, true); dv.setUint32(16 + k * 12, j.length, true);
    out.set(j, off); off += j.length;
  });
  return b64(out);
}
const norm = t => (t || '').trim().toLocaleLowerCase();
// What would stop it being saved, or ''
function problem() {
  const name = norm(ED.F.name);
  if (!name) return tr('Ponle un nombre a la esfera.', 'Give the face a name.');
  // (FX: the page's catalogue, a let, so not on window)
  if ((typeof FX !== 'undefined' ? FX : []).some(f => !f.x && f.g >= 0 && norm(f.label) === name)) return tr('Ya hay una esfera de serie con ese nombre.', 'A built-in face already has that name.');
  const mine = [...LOCAL, ...(WATCH && WATCH.list ? WATCH.list : [])].filter(f => f.id !== ED.id);
  if (mine.some(f => norm(f.name) === name)) return tr('Ya tienes otra esfera con ese nombre.', 'You already have another face with that name.');
  const empty = ED.slots.find(sl => !sl.SRC && !sl.pic);
  if (empty) return tr(`El tramo de las ${hhmmOf(empty.from)} no tiene imagen.`, `The ${hhmmOf(empty.from)} stretch has no picture.`);
  if (new Set(ED.slots.map(sl => sl.from)).size !== ED.slots.length) return tr('Dos tramos empiezan a la misma hora.', 'Two stretches start at the same time.');
  return '';
}
async function save() {
  if (busy) return;
  keepSlot();
  const why = problem();
  if (why) { $('mest').textContent = why; return; }
  if (WATCH && !WATCH.err && !sdOk()) { $('mest').textContent = tr('El reloj necesita una microSD para guardarla.', 'The watch needs a microSD to keep it.'); return; }
  busy = true; $('mesave').disabled = true;
  const keepCur = ED.cur;
  try {
    $('mest').textContent = tr('Preparando…', 'Preparing…');
    const F = {...ED.F}; delete F._x; delete F._ax; delete F._ay; delete F.anim;
    F.name = F.name.trim();
    const files = {}, slots = [];
    for (let k = 0; k < ED.slots.length; k++) {
      const sl = ED.slots[k];
      pickSlot(k);
      if (sl.dirty || !sl.pic) {  // its picture as the watch shows it, and the original to edit it again
        const anim = SRC.frames.length > 1;
        sl.pic = `${anim ? 'anim' : 'bg'}_${sl.key}.${anim ? 'bin' : 'jpg'}`;
        sl.src = `source_${sl.key}.${SRC.ext}`;
        files[sl.pic] = await buildMain();
        files[sl.src] = b64(await SRC.blob.arrayBuffer());
      }
      slots.push({from: k ? sl.from : 0, pic: sl.pic, src: sl.src, crop: T});
    }
    F.slots = slots;
    pickSlot(0);
    compose(0, false);
    const t = document.createElement('canvas'); t.width = 132; t.height = 162;
    t.getContext('2d').drawImage(pcv, 0, 0, 132, 162);
    files['thumb.jpg'] = b64(await (await blobOf(t, 0.8)).arrayBuffer());
    pickSlot(keepCur);
    $('mest').textContent = '';
    sending(tr(`Enviando «${F.name}» al reloj`, `Sending «${F.name}» to the watch`), 0, tr('Preparando…', 'Preparing…'));
    const onWatch = !!(WATCH && WATCH.list && WATCH.list.some(w => w.id === ED.id));
    ClockBridge.customSave(ED.id, JSON.stringify(F), JSON.stringify(files), !onWatch);
  } catch (e) {
    busy = false; $('mesave').disabled = false; $('mest').textContent = e.message; sending(null);
  }
}

// Replies from the app (MainActivity.Bridge)
window.customReply = s => {
  const m = JSON.parse(s);
  if (m.t === 'cust') { WATCH = m.err ? {err: m.err} : m; renderList(); if (window.curFaceRefresh) curFaceRefresh(); autoSend(); return; }
  if (m.t === 'save') {
    if (m.pct !== undefined) { sending($('missendt').textContent, m.pct, m.pct + ' %'); return; }
    busy = false; $('mesave').disabled = false; $('mebar').classList.add('hide'); sending(null);
    $('mest').textContent = m.ok ? tr('Guardada en el reloj y en el móvil.', 'Saved on the watch and the phone.') : (m.msg || tr('No se pudo guardar', 'It could not be saved'));
    if (m.ok || /móvil/.test(m.msg || '')) for (const sl of ED.slots) sl.dirty = false;  // kept on the phone at least
    if (m.ok) { ED.saved = true; ED.isNew = false; $('metry').disabled = $('mesel').disabled = false; $('medel').classList.remove('hide'); }
    refresh(); return;
  }
  if (m.t === 'restore') {
    if (m.pct !== undefined) { sending($('missendt').textContent, m.pct, m.pct + ' %'); return; }
    busy = false; sending(null); $('misst').classList.remove('hide');
    $('misst').textContent = m.ok ? tr(`Restauradas ${m.n} esferas.`, `${m.n} faces restored.`) : (m.msg || tr('No se pudo restaurar', 'It could not be restored'));
    refresh();
  }
};

inject();
loadFonts().catch(() => {});  // ahead of time: the editor opens at once
window.misRefresh = refresh;
window.misReconnected = () => { autoDone = false; refresh(); };
window.misBusy = () => busy;
// The custom face shown now, for the page's "on the watch" cards: {name, thumb} (thumb only if the phone has it)
window.misFaceInfo = id => {
  const l = LOCAL.find(f => f.id === id);
  if (l) return {name: l.name, thumb: `https://appassets.androidplatform.net/mis/${id}/thumb.jpg?v=${l.v || 0}`};
  const w = WATCH && WATCH.list && WATCH.list.find(f => f.id === id);
  return w ? {name: w.name, thumb: null} : null;
};
window.misView = v => { if (v === 'esferas') refresh(); if (v === 'miesfera') loop(); };
refresh();
})();
