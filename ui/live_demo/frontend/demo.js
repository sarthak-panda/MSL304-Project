// Replays two FlowSim engine runs (today vs plan) as a 3D hall plus live charts.
// Every passenger's arrival, service start, service end and counter come from the engine;
// walking between those points is cosmetic.
import * as THREE from "three";
import { GLTFLoader } from "./vendor/jsm/loaders/GLTFLoader.js";
import * as SkeletonUtils from "./vendor/jsm/utils/SkeletonUtils.js";

const C = { today: "#ff8a3d", plan: "#3fcf8e", sign: "#ffd23f", mute: "#8a949e", line: "#2a3036",
            ok: new THREE.Color("#3fcf8e"), warn: new THREE.Color("#f2b233"), bad: new THREE.Color("#ef5b4c") };
const LANE_GAP = 1.5, SLOT = 0.46, ROW = 0.58, PEOPLE_H = 0.7, LANE_Z = -2.6, FRONT_Z = -1.1, MAX_RENDERED = 260;
const PEOPLE = ["male-a", "female-a", "male-b", "female-b", "male-c", "female-c", "male-d", "female-d",
                "male-e", "female-e", "male-f", "female-f"];

// ---------- Streamlit component protocol ----------
const send = (type, extra = {}) => window.parent.postMessage({ isStreamlitMessage: true, type, ...extra }, "*");
const setHeight = () => send("streamlit:setFrameHeight", { height: document.getElementById("app").offsetHeight });

// ---------- state ----------
let data = null, runId = null, assets = null;
let simT = 0, playing = true, speed = 2, tEnd = 1, lastFrame = performance.now(), scrubbing = false;
const halls = [];
const $ = (id) => document.getElementById(id);

const fmtClock = (t) => { const m = Math.max(0, Math.floor(t)); return `${Math.floor(m / 60)}:${String(m % 60).padStart(2, "0")}`; };
const money = (v) => `${data.meta.currency}${Math.round(v).toLocaleString("en-US")}`;

// ---------- assets ----------
async function loadAssets() {
  const loader = new GLTFLoader();
  const get = (p) => loader.loadAsync(`./models/${p}.glb`);
  const [people, officer, scanner, belt, fence, column, luggage] = await Promise.all([
    Promise.all(PEOPLE.map((n) => get(`people/character-${n}`))),
    get("market/character-employee"), get("lane/scanner-high"), get("lane/conveyor"),
    get("market/fence"), get("market/column"), get("people/item-luggage-upright"),
  ]);
  const heightOf = (o) => new THREE.Box3().setFromObject(o).getSize(new THREE.Vector3()).y;
  return { people, officer, scanner, belt, fence, column, luggage, personScale: PEOPLE_H / heightOf(people[0].scene) };
}

function shadowed(obj) { obj.traverse((o) => { if (o.isMesh) { o.castShadow = true; o.receiveShadow = true; } }); return obj; }

// ---------- hall ----------
function buildHall(sim, lanesTotal, maxQueue) {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color("#14181c");
  scene.add(new THREE.HemisphereLight("#dfe8f0", "#2a2f35", 1.6));
  const sun = new THREE.DirectionalLight("#ffffff", 2.2);
  sun.position.set(4, 10, 6); sun.castShadow = true; sun.shadow.mapSize.set(1024, 1024);
  Object.assign(sun.shadow.camera, { left: -8, right: 8, top: 8, bottom: -8 });
  scene.add(sun);

  const width = Math.max(lanesTotal * LANE_GAP, 6);
  const qWidth = Math.min(width - 1.2, 6.4), perRow = Math.max(6, Math.floor(qWidth / SLOT));
  const rows = Math.min(10, Math.max(3, Math.ceil(maxQueue / perRow) + 1));
  const depth = (FRONT_Z + rows * ROW + 1.6) - (LANE_Z - 1.6);
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(width + 4, depth + 6), new THREE.MeshStandardMaterial({ color: "#2b3036", roughness: 0.95 }));
  floor.rotation.x = -Math.PI / 2; floor.position.z = (FRONT_Z + rows * ROW + LANE_Z) / 2; floor.receiveShadow = true; scene.add(floor);
  const back = new THREE.Mesh(new THREE.BoxGeometry(width + 4, 1.6, 0.15), new THREE.MeshStandardMaterial({ color: "#3a4048" }));
  back.position.set(0, 0.8, LANE_Z - 1.9); scene.add(back);
  const stripe = new THREE.Mesh(new THREE.PlaneGeometry(width + 4, 0.06), new THREE.MeshBasicMaterial({ color: C.sign }));
  stripe.rotation.x = -Math.PI / 2; stripe.position.set(0, 0.005, (LANE_Z + FRONT_Z) / 2 + 0.2); scene.add(stripe);

  const s = assets.personScale;
  const laneX = (i) => (i - (lanesTotal - 1) / 2) * LANE_GAP;
  const lamps = [];
  for (let i = 0; i < lanesTotal; i++) {
    const open = i < sim.counters, x = laneX(i);
    const sc = shadowed(assets.scanner.scene.clone()); sc.rotation.y = Math.PI / 2; sc.position.set(x, 0, LANE_Z); sc.scale.setScalar(0.62);
    const belt = shadowed(assets.belt.scene.clone()); belt.position.set(x + 0.6, 0, LANE_Z); belt.scale.set(0.32, 0.5, 1.1);
    scene.add(sc, belt);
    const lamp = new THREE.Mesh(new THREE.BoxGeometry(0.7, 0.1, 0.1),
      new THREE.MeshStandardMaterial({ color: "#222", emissive: open ? C.ok : new THREE.Color("#5a1f1a"), emissiveIntensity: open ? 1.2 : 0.8 }));
    lamp.position.set(x, 0.86, LANE_Z); scene.add(lamp); lamps.push(lamp);
    if (open) {
      const off = SkeletonUtils.clone(assets.officer.scene); shadowed(off);
      off.scale.setScalar(s * 1.05); off.position.set(x - 0.62, 0, LANE_Z + 0.45); off.rotation.y = Math.PI / 6; scene.add(off);
      const mix = new THREE.AnimationMixer(off);
      const idle = THREE.AnimationClip.findByName(assets.officer.animations, "idle"); if (idle) mix.clipAction(idle).play();
      lamp.userData.mixer = mix;
    } else {
      for (let k = -1; k <= 0; k++) { const f = shadowed(assets.fence.scene.clone()); f.position.set(x + 0.25 + k * 0.5, 0, LANE_Z + 0.75); scene.add(f); }
      sc.traverse((o) => { if (o.isMesh) { o.material = o.material.clone(); o.material.color.multiplyScalar(0.35); } });
    }
  }

  // Serpentine queue: slot 0 sits just in front of the lanes, rows extend toward the entrance.
  const x0 = -((perRow - 1) * SLOT) / 2;
  const slotPos = (k) => {
    const r = Math.floor(k / perRow), j = k % perRow, z = FRONT_Z + r * ROW;
    return new THREE.Vector3(r % 2 === 0 ? -x0 - j * SLOT : x0 + j * SLOT, 0, z);
  };
  for (let r = 0; r < rows - 1; r++) {
    const z = FRONT_Z + r * ROW + ROW / 2, gapRight = r % 2 === 0;
    for (let j = 0; j < perRow - 1; j++) {
      const x = x0 + j * SLOT + (gapRight ? 0 : SLOT) + SLOT * 0.5;
      const f = assets.fence.scene.clone(); f.position.set(x, 0, z); f.scale.set(SLOT / 0.5, 0.9, 1); scene.add(f);
    }
    for (const side of [-1, 1]) { const c = assets.column.scene.clone(); c.scale.set(0.24, 0.42, 0.24); c.position.set(side * (-x0 + SLOT * 0.5), 0, z); scene.add(c); }
  }

  const camera = new THREE.PerspectiveCamera(30, 1, 0.1, 100);
  const frameW = width + 1.2, frameZ = { near: FRONT_Z + rows * ROW + 0.4, far: LANE_Z - 1.0 };

  // ring for wait colour, shared geometry
  const ringGeo = new THREE.RingGeometry(0.15, 0.24, 24); ringGeo.rotateX(-Math.PI / 2);
  return { scene, camera, frameW, frameZ, sim, laneX, slotPos, lamps, ringGeo, actors: new Map(), lowPtr: 0,
           entrance: new THREE.Vector3(-x0 + 1.2, 0, FRONT_Z + rows * ROW + 0.8), exitZ: LANE_Z - 1.5 };
}

function spawn(h, i) {
  const tpl = assets.people[i % assets.people.length];
  const obj = SkeletonUtils.clone(tpl.scene); shadowed(obj);
  obj.scale.setScalar(assets.personScale);
  const g = new THREE.Group(); g.add(obj);
  if (i % 3 === 0) { const bag = assets.luggage.scene.clone(); bag.scale.setScalar(assets.personScale * 0.9); bag.position.set(0.17, 0, -0.05); g.add(bag); }
  const ring = new THREE.Mesh(h.ringGeo, new THREE.MeshBasicMaterial({ color: C.ok, transparent: true, opacity: 0.9 }));
  ring.position.y = 0.01; g.add(ring);
  g.position.copy(h.entrance).add(new THREE.Vector3((i % 5) * 0.12, 0, 0));
  h.scene.add(g);
  const mixer = new THREE.AnimationMixer(obj);
  const walk = mixer.clipAction(THREE.AnimationClip.findByName(tpl.animations, "walk"));
  const idle = mixer.clipAction(THREE.AnimationClip.findByName(tpl.animations, "idle"));
  idle.play(); walk.play(); walk.weight = 0;
  const a = { g, ring, mixer, walk, idle, moving: 0 };
  h.actors.set(i, a);
  return a;
}

function despawn(h, i) {
  const a = h.actors.get(i);
  h.scene.remove(a.g); a.mixer.stopAllAction(); a.mixer.uncacheRoot(a.mixer.getRoot());
  a.g.traverse((o) => { if (o.isSkinnedMesh) o.skeleton.dispose(); });
  a.ring.material.dispose();
  h.actors.delete(i);
}

// ---------- per-frame hall update ----------
const tmp = new THREE.Vector3();
function updateHall(h, t, dtReal, snap) {
  const s = h.sim, n = s.arrival.length, exitMin = Math.max(0.8, speed * 0.9);
  while (h.lowPtr < n && s.end[h.lowPtr] + exitMin < t && s.start[h.lowPtr] <= t) h.lowPtr++;
  const keep = new Set();
  let rank = 0, rendered = 0;
  const busy = new Array(s.counters).fill(false);
  for (let i = h.lowPtr; i < n && s.arrival[i] <= t; i++) {
    if (t >= s.end[i] + exitMin) continue;
    let target, waiting = false, wait = 0;
    if (t < s.start[i]) { waiting = true; wait = t - s.arrival[i]; target = h.slotPos(rank++); }
    else if (t < s.end[i]) { busy[s.counter[i] - 1] = true; wait = s.start[i] - s.arrival[i]; target = new THREE.Vector3(h.laneX(s.counter[i] - 1), 0, LANE_Z); }
    else { target = new THREE.Vector3(h.laneX(s.counter[i] - 1), 0, h.exitZ); wait = s.start[i] - s.arrival[i]; }
    if (rendered >= MAX_RENDERED) break;
    rendered++; keep.add(i);
    const a = h.actors.get(i) || spawn(h, i);
    const d = tmp.copy(target).sub(a.g.position); d.y = 0;
    const dist = d.length(), step = Math.min(dist, (snap ? 1e9 : 2.4 * Math.min(4, Math.max(1, speed / 2))) * dtReal);
    if (dist > 1e-3) { a.g.position.addScaledVector(d.normalize(), step); a.g.rotation.y = Math.atan2(d.x, d.z); }
    else if (waiting || t < s.end[i]) a.g.rotation.y = Math.PI;
    const moving = dist > 0.04 ? 1 : 0;
    a.moving += (moving - a.moving) * Math.min(1, dtReal * 8);
    a.walk.weight = a.moving; a.idle.weight = 1 - a.moving;
    a.mixer.update(dtReal * Math.min(2, Math.max(1, speed / 3)));
    const sla = data.meta.sla_wait_minutes;
    a.ring.material.color.copy(wait < sla / 2 ? C.ok : wait <= sla ? C.warn : C.bad);
    a.ring.visible = t < s.end[i];
  }
  for (const i of [...h.actors.keys()]) if (!keep.has(i)) despawn(h, i);
  h.lamps.forEach((l, i) => {
    if (i < s.counters) l.material.emissive.copy(busy[i] ? C.warn : C.ok);
    if (l.userData.mixer) l.userData.mixer.update(dtReal);
  });
}

// ---------- derived series (from engine records) ----------
function series(s) {
  const ev = [];
  for (let i = 0; i < s.arrival.length; i++) if (s.start[i] > s.arrival[i]) { ev.push([s.arrival[i], 1], [s.start[i], -1]); }
  ev.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const pts = [[0, 0]]; let q = 0;
  for (const [t, d] of ev) { q += d; pts.push([t, q]); }
  return pts;
}

function costAt(s, t) {
  const m = data.meta, fin = s.metrics.finish_time_min;
  let wait = 0, breaches = 0;
  for (let i = 0; i < s.arrival.length && s.arrival[i] <= t; i++) {
    wait += Math.min(t, s.start[i]) - s.arrival[i];
    if (s.start[i] - s.arrival[i] > m.sla_wait_minutes && s.arrival[i] + m.sla_wait_minutes < t) breaches++;
  }
  return s.counters * m.counter_cost_per_hour * Math.min(t, fin) / 60 + wait / 60 * m.waiting_cost_per_passenger_hour + breaches * m.sla_penalty_per_passenger;
}

// Number of entries in a sorted array that are <= t.
function countLE(arr, t) { let lo = 0, hi = arr.length; while (lo < hi) { const m = (lo + hi) >> 1; if (arr[m] <= t) lo = m + 1; else hi = m; } return lo; }

function indexSim(s) {
  s.startSorted = Float64Array.from(s.start).sort();
  const order = s.start.map((v, i) => i).sort((a, b) => s.start[a] - s.start[b]);
  s.waitPrefix = new Float64Array(order.length + 1);
  order.forEach((i, k) => { s.waitPrefix[k + 1] = s.waitPrefix[k] + (s.start[i] - s.arrival[i]); });
}

function boardStats(s, cost, t) {
  const started = countLE(s.startSorted, t), inLine = countLE(s.arrival, t) - started;
  const pts = cost.pts, j = Math.min(pts.length - 2, Math.floor((t / tEnd) * (pts.length - 1)));
  const f = Math.min(1, Math.max(0, (t - pts[j][0]) / (pts[j + 1][0] - pts[j][0] || 1)));
  return { inLine, avg: started ? s.waitPrefix[started] / started : 0, cost: pts[j][1] + f * (pts[j + 1][1] - pts[j][1]) };
}

// ---------- charts ----------
function prepCanvas(cv) {
  const r = window.devicePixelRatio || 1, w = cv.clientWidth, h = cv.clientHeight;
  if (cv.width !== Math.round(w * r)) { cv.width = Math.round(w * r); cv.height = Math.round(h * r); }
  const ctx = cv.getContext("2d"); ctx.setTransform(r, 0, 0, r, 0, 0); ctx.clearRect(0, 0, w, h);
  return { ctx, w, h };
}

function drawChart(cv, lines, yMax, t, fmt, step) {
  const { ctx, w, h } = prepCanvas(cv);
  const L = 44, R = 70, T = 10, B = 22, pw = w - L - R, ph = h - T - B;
  const X = (x) => L + (x / tEnd) * pw, Y = (y) => T + ph - (y / yMax) * ph;
  ctx.font = "500 12px 'Barlow Condensed'"; ctx.fillStyle = C.mute; ctx.strokeStyle = C.line; ctx.lineWidth = 1;
  for (let k = 0; k <= 4; k++) {
    const v = (yMax * k) / 4, y = Y(v);
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(L + pw, y); ctx.stroke();
    ctx.textAlign = "right"; ctx.fillText(fmt(v, true), L - 6, y + 4);
  }
  ctx.textAlign = "center";
  for (let m = 0; m <= tEnd; m += 60) ctx.fillText(fmtClock(m), X(m), h - 6);
  for (const ln of lines) {
    ctx.strokeStyle = ln.color; ctx.lineWidth = 2.5; ctx.beginPath();
    let last = 0, lastX = 0, started = false;
    for (const [x, y] of ln.pts) {
      if (x > t) { if (!step) last += ((t - lastX) / (x - lastX || 1)) * (y - last); break; }
      if (!started) { ctx.moveTo(X(x), Y(y)); started = true; }
      else { if (step) ctx.lineTo(X(x), Y(last)); ctx.lineTo(X(x), Y(y)); }
      last = y; lastX = x;
    }
    ctx.lineTo(X(t), Y(last)); ctx.stroke();
    ctx.fillStyle = ln.color; ctx.beginPath(); ctx.arc(X(t), Y(last), 4, 0, Math.PI * 2); ctx.fill();
    ln.now = last;
  }
  // value labels at the right edge, nudged apart
  const labs = lines.map((ln) => ({ y: Y(ln.now), ln })).sort((a, b) => a.y - b.y);
  if (labs.length === 2 && labs[1].y - labs[0].y < 16) labs[1].y = labs[0].y + 16;
  ctx.font = "700 15px 'Barlow Condensed'"; ctx.textAlign = "left";
  for (const { y, ln } of labs) { ctx.fillStyle = ln.color; ctx.fillText(fmt(ln.now, false), L + pw + 8, y + 5); }
}

let qLines, cLines, qMax, cMax;
function prepareCharts() {
  const sims = [data.today, data.plan];
  qLines = sims.map((s, k) => ({ pts: series(s), color: k ? C.plan : C.today }));
  qMax = qLines.reduce((mx, l) => l.pts.reduce((m2, p) => Math.max(m2, p[1]), mx), 4) * 1.1;
  cLines = sims.map((s, k) => {
    const pts = []; for (let j = 0; j <= 240; j++) { const t = (tEnd * j) / 240; pts.push([t, costAt(s, t)]); }
    return { pts, color: k ? C.plan : C.today };
  });
  cMax = Math.max(1, ...cLines.map((l) => l.pts[l.pts.length - 1][1])) * 1.1;
}

// ---------- HUD ----------
function board(el, st) {
  const html = `<div><small>In line</small><b>${st.inLine}</b></div>` +
    `<div><small>Avg wait</small><b>${st.avg.toFixed(1)} min</b></div>` +
    `<div><small>Cost</small><b>${money(st.cost)}</b></div>`;
  if (el.dataset.html !== html) { el.innerHTML = html; el.dataset.html = html; }
}

function showEnd(show) {
  const el = $("end");
  if (!show) { el.classList.add("hidden"); return; }
  const a = data.today.metrics, b = data.plan.metrics, m = data.meta;
  el.innerHTML = `<h2>${a.customers_served} passengers. <em>${data.today.counters} lanes vs ${data.plan.counters} lanes.</em></h2>
    <table><tr><th></th><th>Today</th><th>FlowSim plan</th></tr>
    <tr><td>Average wait</td><td class="today">${a.mean_wait_min.toFixed(1)} min</td><td class="plan">${b.mean_wait_min.toFixed(1)} min</td></tr>
    <tr><td>Longest line</td><td class="today">${a.max_queue_length}</td><td class="plan">${b.max_queue_length}</td></tr>
    <tr><td>Total cost</td><td class="today">${money(a.total_objective_cost)}</td><td class="plan">${money(b.total_objective_cost)}</td></tr></table>
    <p>Over ${m.replications} simulated runs the plan costs ${money(m.mean_savings)} less on average${m.mean_savings_percent == null ? "" : ` (${m.mean_savings_percent.toFixed(0)}%)`}.</p>`;
  el.classList.remove("hidden");
}

// ---------- renderer ----------
// Places the camera so the whole hall width fits the viewport, leaving headroom for the HUD.
function fitCamera(h, aspect) {
  const cam = h.camera; if (cam.aspect === aspect && cam.userData.fit) return;
  cam.aspect = aspect; cam.userData.fit = true;
  const vf = THREE.MathUtils.degToRad(cam.fov), hf = 2 * Math.atan(Math.tan(vf / 2) * aspect);
  const span = h.frameZ.near - h.frameZ.far, midZ = (h.frameZ.near + h.frameZ.far) / 2;
  const d = Math.max((h.frameW / 2) / Math.tan(hf / 2), (span * 0.62) / Math.tan(vf / 2)) * 1.02;
  const tilt = THREE.MathUtils.degToRad(40);
  cam.position.set(0, d * Math.sin(tilt), midZ + d * Math.cos(tilt));
  cam.lookAt(0, 0, midZ - 0.35);
  cam.far = Math.max(100, d * 2 + span);
  cam.updateProjectionMatrix();
}

let renderer;
function frame(now) {
  const dt = Math.min(0.25, (now - lastFrame) / 1000); lastFrame = now;
  if (data && assets && halls.length === 2) {
    let snap = false;
    if (playing && !scrubbing) { simT = Math.min(tEnd, simT + dt * speed); if (simT >= tEnd) { playing = false; $("play").textContent = "Play"; } }
    if (frame.jump) { snap = true; frame.jump = false; }
    const stage = $("stage"), W = stage.clientWidth, want = `${Math.round(Math.min(470, Math.max(300, W * 0.3)))}px`;
    if (stage.style.height !== want) stage.style.height = want;
    const H = stage.clientHeight;
    if (renderer.domElement.width !== Math.round(W * renderer.getPixelRatio())) renderer.setSize(W, H, false);
    renderer.setScissorTest(true);
    halls.forEach((h, k) => {
      updateHall(h, simT, dt, snap);
      fitCamera(h, W / 2 / H);
      renderer.setViewport(k * W / 2, 0, W / 2, H); renderer.setScissor(k * W / 2, 0, W / 2, H);
      renderer.render(h.scene, h.camera);
      board($(k ? "boardR" : "boardL"), boardStats(h.sim, cLines[k], simT));
    });
    $("clock").textContent = fmtClock(simT);
    if (!scrubbing) $("scrub").value = String(Math.round((simT / tEnd) * 1000));
    drawChart($("chartQ"), qLines, qMax, simT, (v, axis) => (axis ? Math.round(v) : String(Math.round(v))), true);
    drawChart($("chartC"), cLines, cMax, simT, (v, axis) => (axis ? (v ? `${data.meta.currency}${Math.round(v / 1000)}k` : "0") : money(v)), false);
    showEnd(simT >= tEnd && !scrubbing);
  }
  requestAnimationFrame(frame);
}

function reset() {
  for (const h of halls) for (const i of [...h.actors.keys()]) despawn(h, i);
  halls.length = 0;
  const lanes = Math.max(data.today.counters, data.plan.counters);
  const maxQ = Math.max(data.today.metrics.max_queue_length, data.plan.metrics.max_queue_length);
  halls.push(buildHall(data.today, lanes, maxQ), buildHall(data.plan, lanes, maxQ));
  tEnd = Math.max(1, data.today.metrics.finish_time_min, data.plan.metrics.finish_time_min);
  simT = 0; playing = true; $("play").textContent = "Pause";
  $("lanesL").textContent = data.today.counters; $("lanesR").textContent = data.plan.counters;
  $("sla1").textContent = $("sla2").textContent = data.meta.sla_wait_minutes;
  indexSim(data.today); indexSim(data.plan);
  prepareCharts(); showEnd(false);
}

function restart() { simT = 0; playing = true; $("play").textContent = "Pause"; halls.forEach((h) => { h.lowPtr = 0; for (const i of [...h.actors.keys()]) despawn(h, i); }); }

function wire() {
  $("play").onclick = () => { if (simT >= tEnd) restart(); else playing = !playing; $("play").textContent = playing ? "Pause" : "Play"; };
  $("replay").onclick = restart;
  $("speed").querySelectorAll("button").forEach((b) => (b.onclick = () => {
    speed = Number(b.dataset.speed) * 2;
    $("speed").querySelectorAll("button").forEach((o) => o.classList.toggle("on", o === b));
  }));
  speed = 4;
  const sc = $("scrub");
  sc.addEventListener("input", () => { scrubbing = true; simT = (Number(sc.value) / 1000) * tEnd; halls.forEach((h) => (h.lowPtr = 0)); frame.jump = true; });
  sc.addEventListener("change", () => { scrubbing = false; });
  new ResizeObserver(setHeight).observe($("app"));
}

async function init() {
  renderer = new THREE.WebGLRenderer({ canvas: $("gl"), antialias: true });
  renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
  renderer.shadowMap.enabled = true; renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  wire();
  assets = await loadAssets();
  $("loading").classList.add("hidden");
  if (data) reset();
  requestAnimationFrame(frame);
}

window.addEventListener("message", (ev) => {
  if (!ev.data || ev.data.type !== "streamlit:render") return;
  const args = ev.data.args;
  if (args.run_id === runId) return;
  runId = args.run_id; data = { today: args.today, plan: args.plan, meta: args.meta };
  if (assets) reset();
  setHeight();
});

send("streamlit:componentReady", { apiVersion: 1 });
init();
setHeight();
