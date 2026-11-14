// Mis esferas: custom faces, made here (Android app only) and shown by face 28 on the watch
// (src/fx_custom.c). The page lists them in Esferas and edits them in #miesfera: name, a picture or GIF
// from the phone cropped to the screen, and the time (font, size, position, colour, outline or shadow,
// seconds, date), with a preview drawn by the same rules as the watch from the same font data
// (fuentes.png / fuentes.json, made by tools/font_atlas.py).
//
// Everything is kept on the phone too (CustomFaces.kt, files/esferas/<id>/), so a new card can be
// filled again ("Restaurar en el reloj").
(function () {
'use strict';
const WN = 410, HN = 502, WA = 274, HA = 336, CORNER = 120;
const MAX_FRAMES = 40, MIN_DELAY = 100, ANIM_MAX = 10000;
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
function nowStrings() {
  const d = new Date(), p = n => String(n).padStart(2, '0');
  return [p(d.getHours()) + ':' + p(d.getMinutes()), p(d.getSeconds()), `${WD[LANG][d.getDay()]} ${d.getDate()} ${MO[LANG][d.getMonth()]}`];
}

// ---------------------------------------------------------------- state
let LOCAL = [], WATCH = null, ED = null, busy = false;
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
  $('misnew').onclick = () => openEditor(null);
  $('misrest').onclick = restore;
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
  <p class="lbl">${tr('La hora', 'The time')}</p>
  <div class="list">
    <div class="row"><div class="t"><b>${tr('Fuente', 'Font')}</b></div><select id="mefont">${FONTS.map(f => `<option value="${f.id}">${LANG ? f.en : f.es}</option>`).join('')}</select></div>
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
function restore() {
  if (busy) return; busy = true;
  $('misst').classList.remove('hide'); $('misst').textContent = tr('Enviando al reloj…', 'Sending to the watch…');
  ClockBridge.customRestore();
}

// ---------------------------------------------------------------- the editor
function defaults() { return {name: '', font: 'round', size: 2, x: WN / 2, y: 140, color: '#ffffff', fx: 1, secs: 0, date: 1, anim: 0}; }
let SRC = null;  // {frames:[{bmp,delay}], w, h, ext, file|url}
let T = {s: 1, ox: 0, oy: 0};  // picture -> screen: x' = ox + x*s
let mode = 'crop', frame = 0, frameT = 0, raf = 0;

async function openEditor(id) {
  const f = id && LOCAL.find(l => l.id === id);
  ED = {id: id || Math.max(0, ...idsKnown()) + 1, isNew: !id, F: defaults(), dirtyPic: !id, saved: !!id};
  SRC = null;
  location.hash = '#miesfera';
  await loadFonts();
  if (f) {
    try {
      const j = await (await fetch(`https://appassets.androidplatform.net/mis/${id}/face.json?v=${Date.now()}`)).json();
      Object.assign(ED.F, j);
      if (j.crop) T = j.crop;
      if (j.src) await loadSource(await (await fetch(`https://appassets.androidplatform.net/mis/${id}/${j.src}?v=${Date.now()}`)).blob(), j.src.split('.').pop(), true);
    } catch (e) { $('mest').textContent = tr('No se pudo abrir: ', 'Could not open it: ') + e.message; }
  }
  $('medt').textContent = ED.isNew ? tr('Nueva esfera', 'New face') : tr('Editar esfera', 'Edit face');
  $('medel').classList.toggle('hide', ED.isNew);
  syncControls();
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
  const dur = fr.reduce((a, f) => a + f.delay, 0);
  $('meinfo').textContent = fr.length > 1
    ? tr(`GIF: ${fr.length} fotogramas, ${(dur / 1000).toFixed(1)} s`, `GIF: ${fr.length} frames, ${(dur / 1000).toFixed(1)} s`) + (SRC.cut ? tr(' (reducido)', ' (reduced)') : '') + (dur > ANIM_MAX ? tr(' · en el reloj, como mucho 10 s', ' · up to 10 s on the watch') : '') + tr(' · se anima al encender la pantalla', ' · it plays when the screen comes on')
    : tr(`Imagen de ${SRC.w}×${SRC.h}`, `Picture ${SRC.w}×${SRC.h}`);
  frame = 0; frameT = performance.now();
}

function syncControls() {
  const F = ED.F;
  $('mename').value = F.name;
  $('mefont').value = F.font;
  setSizeRange();
  $('mecolor').value = F.color;
  for (const b of document.querySelectorAll('#v-miesfera [data-fx]')) b.classList.toggle('on', +b.dataset.fx === F.fx);
  $('mesecs').checked = !!F.secs; $('medate').checked = !!F.date;
  $('mest').textContent = ''; $('mebar').classList.add('hide');
  $('metry').disabled = $('mesel').disabled = !ED.saved;
}
function setSizeRange() {
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
      ED.dirtyPic = true; changed(); setMode('crop');
    } catch (e) { console.error(e); $('meinfo').textContent = tr('No se pudo abrir esa imagen', 'That picture could not be opened') + ' (' + e.message + ')'; }
    $('mefile').value = '';
  };
  $('mename').oninput = () => { ED.F.name = $('mename').value; changed(); };
  $('mefont').onchange = () => { ED.F.font = $('mefont').value; setSizeRange(); changed(); };
  $('mesize').oninput = () => { ED.F.size = +$('mesize').value; setSizeRange(); changed(); };
  $('mecolor').oninput = () => { ED.F.color = $('mecolor').value; changed(); };
  for (const b of document.querySelectorAll('#v-miesfera [data-fx]')) b.onclick = () => { ED.F.fx = +b.dataset.fx; syncFx(); changed(); };
  $('mesecs').onchange = () => { ED.F.secs = $('mesecs').checked ? 1 : 0; changed(); };
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
  cv.onpointerdown = e => { cv.setPointerCapture(e.pointerId); pts.set(e.pointerId, toCanvas(e)); start = null; ED.F._x = ED.F.x; };
  cv.onpointerup = cv.onpointercancel = e => { pts.delete(e.pointerId); start = null; };
  cv.onpointermove = e => {
    if (!pts.has(e.pointerId)) return;
    const prev = pts.get(e.pointerId), p = toCanvas(e);
    pts.set(e.pointerId, p);
    if (mode === 'time') {
      if (pts.size !== 1) return;
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
    changed(); ED.dirtyPic = true;
  };
  cv.onwheel = e => { if (mode === 'crop' && SRC) { e.preventDefault(); const p = toCanvas(e); zoomAt(p[0], p[1], e.deltaY < 0 ? 1.08 : 1 / 1.08); changed(); ED.dirtyPic = true; } };
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
  if (FD) {  // the fonts may still be loading
    const img = x.getImageData(0, 0, WN, HN);
    const [hhmm, ss, date] = nowStrings();
    const bx = buildMask(ED.F, hhmm, ss, date);
    drawText(ED.F, img.data, bx);
    x.putImageData(img, 0, 0);
  }
  const out = $('mecv').getContext('2d');
  out.drawImage(pcv, 0, 0);
  if (withGuides) {  // what the rounded screen hides
    out.save(); out.fillStyle = 'rgba(0,0,0,.6)';
    out.beginPath(); out.rect(0, 0, WN, HN); out.roundRect(2, 2, WN - 4, HN - 4, CORNER); out.fill('evenodd'); out.restore();
    if (mode === 'time' && ED.F.x === WN / 2) { out.fillStyle = 'rgba(249,178,90,.6)'; out.fillRect(WN / 2, 0, 1, HN); }
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
async function save() {
  if (busy) return;
  if (!SRC) { $('mest').textContent = tr('Elige primero una imagen o un GIF.', 'Pick a picture or a GIF first.'); return; }
  if (WATCH && !WATCH.err && !sdOk()) { $('mest').textContent = tr('El reloj necesita una microSD para guardarla.', 'The watch needs a microSD to keep it.'); return; }
  busy = true; $('mesave').disabled = true;
  try {
    $('mest').textContent = tr('Preparando…', 'Preparing…');
    const F = {...ED.F}; delete F._x;
    if (!F.name) F.name = tr('Mi esfera ', 'My face ') + ED.id;
    F.crop = T; F.src = 'source.' + SRC.ext;
    const main = ED.dirtyPic ? await buildMain() : null;
    const src = ED.dirtyPic ? b64(await SRC.blob.arrayBuffer()) : null;
    compose(0, false);
    const t = document.createElement('canvas'); t.width = 132; t.height = 162;
    t.getContext('2d').drawImage(pcv, 0, 0, 132, 162);
    const thumb = b64(await (await blobOf(t, 0.8)).arrayBuffer());
    $('mebar').classList.remove('hide'); $('mebar').firstElementChild.style.width = '0%';
    $('mest').textContent = tr('Enviando al reloj…', 'Sending to the watch…');
    ClockBridge.customSave(ED.id, JSON.stringify(F), main, !!F.anim, src, SRC.ext, thumb);
  } catch (e) {
    busy = false; $('mesave').disabled = false; $('mest').textContent = e.message;
  }
}

// Replies from the app (MainActivity.Bridge)
window.customReply = s => {
  const m = JSON.parse(s);
  if (m.t === 'cust') { WATCH = m.err ? {err: m.err} : m; renderList(); return; }
  if (m.t === 'save') {
    if (m.pct !== undefined) { $('mebar').firstElementChild.style.width = m.pct + '%'; return; }
    busy = false; $('mesave').disabled = false; $('mebar').classList.add('hide');
    $('mest').textContent = m.ok ? tr('Guardada en el reloj y en el móvil.', 'Saved on the watch and the phone.') : (m.msg || tr('No se pudo guardar', 'It could not be saved'));
    if (m.ok) { ED.saved = true; ED.isNew = false; ED.dirtyPic = false; $('metry').disabled = $('mesel').disabled = false; $('medel').classList.remove('hide'); }
    refresh(); return;
  }
  if (m.t === 'restore') {
    if (m.pct !== undefined) { $('misst').textContent = tr(`Enviando al reloj… ${m.pct} %`, `Sending to the watch… ${m.pct} %`); return; }
    busy = false;
    $('misst').textContent = m.ok ? tr(`Restauradas ${m.n} esferas.`, `${m.n} faces restored.`) : (m.msg || tr('No se pudo restaurar', 'It could not be restored'));
    refresh();
  }
};

inject();
loadFonts().catch(() => {});  // ahead of time: the editor opens at once
window.misRefresh = refresh;
window.misView = v => { if (v === 'esferas') refresh(); if (v === 'miesfera') loop(); };
refresh();
})();
