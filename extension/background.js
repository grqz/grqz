// CLDM background worker: owns the native helper connection and the download list.
'use strict';

const HOST = 'com.cldm.host';
const MAX_PARALLEL = 3;
const IDLE_MS = 15000;
const RUNNING = new Set(['starting', 'downloading', 'processing', 'pausing', 'canceling']);
const ACTIVE = new Set([...RUNNING, 'queued']);

let port = null;
let host = { status: 'unknown', error: '' };
let jobs = [];
let settings = { dir: '', ask: true, preset: 'best' };
const popups = new Set();
const pendingFolders = new Map();
let saveTimer = null;
let idleTimer = null;

const ready = (async () => {
  const saved = await chrome.storage.local.get(['jobs', 'settings']);
  settings = { ...settings, ...(saved.settings || {}) };
  // A restarted worker means the helper (and every download it ran) is gone.
  jobs = (saved.jobs || []).map((j) =>
    RUNNING.has(j.state) ? { ...j, state: j.state === 'canceling' ? 'paused' : 'interrupted', speed: -1, eta: -1 } : j);
  updateBadge();
})();

// ---------- helper connection ----------

function connect() {
  if (port) return port;
  try {
    port = chrome.runtime.connectNative(HOST);
  } catch (e) {
    setHost({ status: 'missing', error: String(e.message || e) });
    return null;
  }
  port.onMessage.addListener(onHostMessage);
  port.onDisconnect.addListener(() => {
    const error = chrome.runtime.lastError?.message || '';
    port = null;
    for (const [, resolve] of pendingFolders) resolve(null);
    pendingFolders.clear();
    for (const j of jobs) {
      if (RUNNING.has(j.state)) Object.assign(j, { state: 'interrupted', speed: -1, eta: -1 });
    }
    const missing = /not found|forbidden|not installed/i.test(error);
    setHost({ status: missing ? 'missing' : 'offline', error });
    changed();
  });
  setHost({ status: 'connecting', error: '' });
  port.postMessage({ type: 'hello' });
  return port;
}

function post(msg) {
  const p = connect();
  if (!p) return false;
  p.postMessage(msg);
  return true;
}

function setHost(next) {
  host = { ...host, ...next };
  broadcast({ type: 'host', host });
}

function onHostMessage(m) {
  if (m.type === 'hello') {
    if (!settings.dir && m.downloads) settings.dir = m.downloads;
    setHost({ status: 'ok', error: '', ytdlp: m.ytdlp, ffmpeg: m.ffmpeg, deno: m.deno, version: m.version, downloads: m.downloads });
    broadcast({ type: 'settings', settings });
    pump();
    return;
  }
  if (m.type === 'folder') {
    pendingFolders.get(m.reqId)?.(m.path || null);
    pendingFolders.delete(m.reqId);
    if (m.busy) toast('A folder picker is already open.');
    return;
  }
  if (m.type === 'updated') {
    toast(m.ok ? (m.text || 'yt-dlp is up to date.') : `Update failed: ${m.text}`);
    return;
  }

  const job = jobs.find((j) => j.id === m.id);
  if (!job) return;
  switch (m.type) {
    case 'started':
      if (job.state === 'starting') job.phase = '';
      break;
    case 'info':
      if (m.title) job.title = m.title;
      job.item = m.count ? `${Number(m.index) || m.index}/${m.count}` : '';
      break;
    case 'dest':
      if (!job.files.includes(m.file)) job.files.push(m.file);
      break;
    case 'progress':
      if (job.state === 'starting' || job.state === 'processing') job.state = 'downloading';
      Object.assign(job, { pct: m.pct, done: m.done, total: m.total, speed: m.speed, eta: m.eta, stream: m.stream });
      broadcast({ type: 'job', job });
      scheduleSave();
      return;
    case 'phase':
      if (job.state !== 'pausing' && job.state !== 'canceling') job.state = 'processing';
      job.phase = m.phase;
      break;
    case 'file':
      job.file = m.file;
      break;
    case 'done':
      Object.assign(job, { state: 'done', pct: 100, speed: -1, eta: -1, finished: Date.now() });
      notify(job, 'Download complete', job.title || job.url);
      break;
    case 'error':
      Object.assign(job, { state: 'error', error: m.message, speed: -1, eta: -1 });
      notify(job, 'Download failed', m.message);
      break;
    case 'stopped':
      if (m.canceled) jobs = jobs.filter((j) => j !== job);
      else Object.assign(job, { state: 'paused', speed: -1, eta: -1 });
      break;
    default:
      return;
  }
  changed();
  if (['done', 'error', 'stopped'].includes(m.type)) pump();
}

// Starts queued jobs, oldest first, while there are free slots.
function pump() {
  let running = jobs.filter((j) => RUNNING.has(j.state)).length;
  const queued = jobs.filter((j) => j.state === 'queued').reverse();
  for (const job of queued) {
    if (running >= MAX_PARALLEL) break;
    Object.assign(job, { state: 'starting', error: '', phase: '' });
    if (!post({ type: 'start', id: job.id, url: job.url, dir: job.dir, preset: job.preset, playlist: job.playlist })) {
      Object.assign(job, { state: 'error', error: 'The CLDM helper is not installed. Run install.cmd.' });
      continue;
    }
    running++;
  }
  changed();
}

function pickFolder(initial) {
  const reqId = crypto.randomUUID();
  if (!post({ type: 'pickFolder', reqId, initial: initial || '' })) return Promise.resolve(null);
  return new Promise((resolve) => pendingFolders.set(reqId, resolve));
}

// ---------- commands from the popup ----------

async function download({ url, preset, playlist }) {
  if (!/^https?:\/\//i.test(url || '')) return toast('That is not a web link.');
  if (!connect()) return toast('The CLDM helper is not installed.');
  let dir = settings.dir || host.downloads || '';
  if (settings.ask || !dir) {
    dir = await pickFolder(dir);
    if (!dir) return;
  }
  settings = { ...settings, dir, preset };
  jobs.unshift({
    id: crypto.randomUUID(), url, preset, playlist: !!playlist, dir,
    title: '', item: '', state: 'queued', phase: '', error: '', file: '', files: [],
    pct: -1, done: -1, total: -1, speed: -1, eta: -1, stream: '', created: Date.now(),
  });
  broadcast({ type: 'settings', settings });
  pump();
}

function command(msg) {
  const job = msg.id && jobs.find((j) => j.id === msg.id);
  switch (msg.cmd) {
    case 'download':
      return download(msg);
    case 'pause':
      if (!job) return;
      if (job.state === 'queued') job.state = 'paused';
      else if (['starting', 'downloading', 'processing'].includes(job.state)) {
        job.state = 'pausing';
        post({ type: 'stop', id: job.id, cancel: false });
      }
      return changed();
    case 'resume':
      if (job && ['paused', 'interrupted', 'error'].includes(job.state)) {
        Object.assign(job, { state: 'queued', error: '' });
        pump();
      }
      return;
    case 'cancel':
      if (!job) return;
      if (RUNNING.has(job.state) || job.files.length) {
        const wasRunning = RUNNING.has(job.state);
        job.state = 'canceling';
        if (post({ type: 'stop', id: job.id, cancel: true, dir: job.dir, files: job.files })) return changed();
        if (wasRunning) return changed();
      }
      jobs = jobs.filter((j) => j !== job);
      return changed();
    case 'remove':
      if (job && !ACTIVE.has(job.state)) jobs = jobs.filter((j) => j !== job);
      return changed();
    case 'clearFinished':
      jobs = jobs.filter((j) => j.state !== 'done');
      return changed();
    case 'open':
      if (job) post({ type: 'open', path: job.file || job.files[job.files.length - 1] || job.dir });
      return;
    case 'openDir':
      if (settings.dir) post({ type: 'open', path: settings.dir });
      return;
    case 'chooseDir':
      return pickFolder(settings.dir).then((dir) => {
        if (dir) settings.dir = dir;
        broadcast({ type: 'settings', settings });
        scheduleSave();
      });
    case 'setSettings':
      settings = { ...settings, ...pick(msg.settings, ['ask', 'preset']) };
      broadcast({ type: 'settings', settings });
      return scheduleSave();
    case 'update':
      if (jobs.some((j) => RUNNING.has(j.state))) return toast('Finish or pause downloads before updating.');
      if (post({ type: 'update' })) toast('Updating yt-dlp…');
      return;
  }
}

function pick(obj, keys) {
  const out = {};
  for (const k of keys) if (obj && k in obj) out[k] = obj[k];
  return out;
}

// ---------- state fan-out ----------

function changed() {
  broadcast({ type: 'jobs', jobs });
  updateBadge();
  scheduleSave();
  scheduleIdle();
}

function broadcast(msg) {
  for (const p of popups) {
    try { p.postMessage(msg); } catch { popups.delete(p); }
  }
}

function toast(text) {
  broadcast({ type: 'toast', text });
}

function scheduleSave() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    const keep = jobs.filter((j, i) => ACTIVE.has(j.state) || i < 100);
    chrome.storage.local.set({ jobs: keep, settings });
  }, 800);
}

// Lets the helper exit when nothing is happening, so it uses no memory while idle.
function scheduleIdle() {
  clearTimeout(idleTimer);
  idleTimer = setTimeout(() => {
    const busy = popups.size || pendingFolders.size || jobs.some((j) => RUNNING.has(j.state));
    if (port && !busy) {
      port.disconnect();
      port = null;
      setHost({ status: 'idle' });
    }
  }, IDLE_MS);
}

function updateBadge() {
  const n = jobs.filter((j) => ACTIVE.has(j.state)).length;
  chrome.action.setBadgeText({ text: n ? String(n) : '' });
  chrome.action.setBadgeBackgroundColor({ color: '#39ff14' });
  chrome.action.setBadgeTextColor?.({ color: '#000000' });
}

function notify(job, title, message) {
  if (popups.size) return;
  chrome.notifications.create(job.id, {
    type: 'basic', iconUrl: 'icons/icon128.png', title, message: String(message || '').slice(0, 200),
  });
}

// ---------- wiring ----------

chrome.runtime.onConnect.addListener(async (p) => {
  if (p.name !== 'popup') return;
  popups.add(p);
  p.onDisconnect.addListener(() => {
    popups.delete(p);
    scheduleIdle();
  });
  p.onMessage.addListener(async (msg) => {
    await ready;
    command(msg);
  });
  await ready;
  if (host.status !== 'ok') connect();
  p.postMessage({ type: 'state', jobs, settings, host });
});

chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({
    id: 'cldm',
    title: 'Download with CLDM',
    contexts: ['page', 'link', 'video', 'audio', 'frame'],
  });
});

chrome.contextMenus.onClicked.addListener((info) => {
  const src = info.srcUrl && /^https?:/i.test(info.srcUrl) ? info.srcUrl : '';
  const url = info.linkUrl || src || info.frameUrl || info.pageUrl || '';
  chrome.windows.create({
    url: `popup.html?url=${encodeURIComponent(url)}`,
    type: 'popup', width: 440, height: 660, focused: true,
  });
});

chrome.notifications.onClicked.addListener(async (id) => {
  await ready;
  command({ cmd: 'open', id });
  chrome.notifications.clear(id);
});
