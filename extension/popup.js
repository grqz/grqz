'use strict';

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
const windowed = params.has('url') || params.has('window');
if (windowed) document.body.classList.add('windowed');

const bg = chrome.runtime.connect({ name: 'popup' });
const send = (msg) => bg.postMessage(msg);

let jobs = [];
let settings = {};
let host = {};
const rows = new Map();

const ICONS = {
  pause: '<svg viewBox="0 0 24 24"><path d="M9 5v14M15 5v14"/></svg>',
  resume: '<svg viewBox="0 0 24 24"><path d="M7 4l13 8-13 8z"/></svg>',
  retry: '<svg viewBox="0 0 24 24"><path d="M4 12a8 8 0 1 0 3-6.2M4 4v5h5"/></svg>',
  cancel: '<svg viewBox="0 0 24 24"><path d="M6 6l12 12M18 6L6 18"/></svg>',
  folder: '<svg viewBox="0 0 24 24"><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/></svg>',
  remove: '<svg viewBox="0 0 24 24"><path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/></svg>',
};

// ---------- formatting ----------

function bytes(n) {
  if (!(n >= 0)) return '';
  const u = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  while (n >= 1024 && i < u.length - 1) { n /= 1024; i++; }
  return `${n.toFixed(n >= 100 || i === 0 ? 0 : 1)} ${u[i]}`;
}

function duration(s) {
  if (!(s >= 0)) return '';
  s = Math.round(s);
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  const mm = h ? String(m).padStart(2, '0') : m;
  return `${h ? h + ':' : ''}${mm}:${String(sec).padStart(2, '0')}`;
}

function shortPath(p, max = 44) {
  if (!p || p.length <= max) return p || 'Choose a folder';
  return `${p.slice(0, 3)}…${p.slice(p.length - (max - 4))}`;
}

function fileName(p) {
  return p ? p.split(/[\\/]/).pop() : '';
}

function metaText(j) {
  const pct = j.pct >= 0 ? `${j.pct.toFixed(1)}%` : '';
  const item = j.item ? `#${j.item}` : '';
  switch (j.state) {
    case 'queued': return 'Waiting for a free slot…';
    case 'starting': return 'Fetching video info…';
    case 'downloading': {
      const size = j.total > 0 ? `${bytes(j.done)} / ${bytes(j.total)}` : bytes(j.done);
      const speed = j.speed > 0 ? `${bytes(j.speed)}/s` : '';
      const eta = j.eta >= 0 ? `${duration(j.eta)} left` : '';
      return [item, pct, size, speed, eta].filter(Boolean).join(' · ');
    }
    case 'processing': return `${j.phase || 'Processing'}…`;
    case 'pausing': return 'Pausing…';
    case 'canceling': return 'Cancelling…';
    case 'paused': return ['Paused', pct].filter(Boolean).join(' · ');
    case 'interrupted': return 'Stopped when the browser closed. Press play to continue.';
    case 'error': return j.error || 'Failed';
    case 'done': return ['Saved', bytes(j.size), fileName(j.file) || j.dir].filter(Boolean).join(' · ');
    default: return '';
  }
}

function actionsFor(state) {
  switch (state) {
    case 'queued': return ['pause', 'cancel'];
    case 'starting': case 'downloading': return ['pause', 'cancel'];
    case 'processing': return ['cancel'];
    case 'paused': case 'interrupted': return ['resume', 'cancel'];
    case 'error': return ['retry', 'folder', 'remove'];
    case 'done': return ['folder', 'remove'];
    default: return [];
  }
}

const ACTION = {
  pause: ['pause', 'Pause'],
  resume: ['resume', 'Resume'],
  retry: ['resume', 'Try again'],
  cancel: ['cancel', 'Cancel and delete partial files'],
  folder: ['open', 'Show in folder'],
  remove: ['remove', 'Remove from list'],
};

// ---------- rendering ----------

function buildRow(job) {
  const li = document.createElement('li');
  li.className = 'job';
  li.innerHTML = `
    <div class="job-top"><span class="tag"></span><span class="title"></span></div>
    <div class="bar"><i></i></div>
    <div class="job-bottom"><span class="meta"></span><span class="actions"></span></div>`;
  li.querySelector('.actions').addEventListener('click', (e) => {
    const btn = e.target.closest('button');
    if (btn) send({ cmd: btn.dataset.cmd, id: job.id });
  });
  const row = {
    li, tag: li.querySelector('.tag'), title: li.querySelector('.title'),
    bar: li.querySelector('.bar'), fill: li.querySelector('.bar i'),
    meta: li.querySelector('.meta'), actions: li.querySelector('.actions'), actionKey: '',
  };
  rows.set(job.id, row);
  return row;
}

function updateRow(job) {
  const row = rows.get(job.id) || buildRow(job);
  row.li.className = `job state-${job.state}`;
  row.title.textContent = job.title || job.url;
  row.title.title = job.title ? `${job.title}\n${job.url}` : job.url;

  const audioPreset = job.preset === 'mp3' || job.preset === 'm4a';
  row.tag.textContent = job.state === 'downloading' && job.stream ? job.stream.toUpperCase() : audioPreset ? 'AUDIO' : '';
  row.tag.className = `tag ${row.tag.textContent === 'AUDIO' ? 'audio' : ''}`;

  const busy = ['queued', 'starting', 'processing', 'pausing', 'canceling'].includes(job.state);
  const pct = job.state === 'done' ? 100 : job.pct >= 0 ? job.pct : 0;
  row.bar.classList.toggle('indet', busy || (job.state === 'downloading' && !(job.pct >= 0)));
  row.fill.style.width = `${pct}%`;

  row.meta.textContent = metaText(job);
  row.meta.title = job.state === 'error' ? job.error : '';

  const acts = actionsFor(job.state);
  const key = acts.join();
  if (key !== row.actionKey) {
    row.actionKey = key;
    row.actions.replaceChildren(...acts.map((a) => {
      const b = document.createElement('button');
      b.dataset.cmd = ACTION[a][0];
      b.title = ACTION[a][1];
      b.setAttribute('aria-label', ACTION[a][1]);
      if (a === 'cancel' || a === 'remove') b.className = 'danger';
      b.innerHTML = ICONS[a];
      return b;
    }));
  }
  return row;
}

function renderJobs() {
  const list = $('jobs');
  const ids = new Set(jobs.map((j) => j.id));
  for (const [id, row] of rows) if (!ids.has(id)) { row.li.remove(); rows.delete(id); }
  list.replaceChildren(...jobs.map((j) => updateRow(j).li));
  $('empty').hidden = jobs.length > 0;
  $('clear').hidden = !jobs.some((j) => j.state === 'done');
}

function renderSettings() {
  $('dirText').textContent = shortPath(settings.dir);
  $('dirText').title = settings.dir ? `${settings.dir}\n(click to open)` : '';
  $('ask').checked = settings.ask !== false;
  if (settings.preset && !$('preset').dataset.touched) $('preset').value = settings.preset;
}

function renderHost() {
  const dot = $('hostDot');
  const s = host.status;
  dot.className = `dot ${s === 'ok' ? 'ok' : s === 'missing' || s === 'offline' ? 'bad' : 'idle'}`;
  dot.title = {
    ok: `Helper connected (v${host.version || '?'})`,
    idle: 'Helper sleeping (starts on demand)',
    connecting: 'Connecting to helper…',
    missing: 'Helper not installed',
    offline: 'Helper stopped unexpectedly',
  }[s] || 'Checking helper…';

  $('setup').hidden = s !== 'missing';
  $('setupDetail').textContent = host.error || '';

  const missing = [];
  if (s === 'ok') {
    if (!host.ytdlp) missing.push('yt-dlp.exe (nothing can download)');
    if (!host.ffmpeg) {
      missing.push(host.ffmpegPath
        ? `a working ffmpeg: ${host.ffmpegPath} won't run (${host.ffmpegError}), so video and audio can't be merged`
        : 'ffmpeg (video and audio can\'t be merged, no MP3)');
    }
    if (!host.deno) missing.push('Deno (YouTube may fail or show fewer formats)');
  }
  $('warn').hidden = !missing.length;
  $('warn').textContent = missing.length ? `Missing: ${missing.join(', ')}. Run install.cmd again.` : '';
  $('go').disabled = s === 'missing';
}

let toastTimer;
function toast(text) {
  const t = $('toast');
  t.textContent = text;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 4000);
}

function updatePlaylistToggle() {
  const u = $('url').value;
  const isList = /[?&]list=|\/playlist\b|\/sets\/|\/album\//i.test(u);
  $('playlistWrap').hidden = !isList;
  if (!isList) $('playlist').checked = false;
}

// ---------- messages from the background worker ----------

bg.onMessage.addListener((m) => {
  switch (m.type) {
    case 'state':
      ({ jobs, settings, host } = m);
      renderSettings(); renderHost(); renderJobs();
      break;
    case 'jobs':
      jobs = m.jobs;
      renderJobs();
      break;
    case 'job': {
      const i = jobs.findIndex((j) => j.id === m.job.id);
      if (i >= 0) { jobs[i] = m.job; updateRow(m.job); }
      break;
    }
    case 'settings':
      settings = m.settings;
      renderSettings();
      break;
    case 'host':
      host = m.host;
      renderHost();
      break;
    case 'prefill':
      if (windowed) prefill(m.url, m.title);
      break;
    case 'toast':
      toast(m.text);
      break;
  }
});

// ---------- controls ----------

$('go').addEventListener('click', () => {
  const url = $('url').value.trim();
  if (!/^https?:\/\//i.test(url)) return toast('Paste a web link first.');
  send({ cmd: 'download', url, preset: $('preset').value, playlist: $('playlist').checked, name: $('name').value.trim() });
  setName('');
  if (settings.ask === false) toast('Added to downloads.');
});
$('url').addEventListener('input', updatePlaylistToggle);
$('url').addEventListener('keydown', (e) => { if (e.key === 'Enter') $('go').click(); });
$('preset').addEventListener('change', () => {
  $('preset').dataset.touched = '1';
  send({ cmd: 'setSettings', settings: { preset: $('preset').value } });
});
$('ask').addEventListener('change', () => send({ cmd: 'setSettings', settings: { ask: $('ask').checked } }));
$('changeDir').addEventListener('click', () => send({ cmd: 'chooseDir' }));
$('dirText').addEventListener('click', () => send({ cmd: 'openDir' }));
$('clear').addEventListener('click', () => send({ cmd: 'clearFinished' }));
$('update').addEventListener('click', () => send({ cmd: 'update' }));
$('popout').addEventListener('click', () => {
  send({ cmd: 'openWindow', url: $('url').value.trim(), title: $('name').value.trim() });
  if (!windowed) window.close();
});
// A name filled in from the page title is dropped when the link is changed by hand.
$('name').addEventListener('input', () => { nameIsAuto = false; });
$('url').addEventListener('input', () => { if (nameIsAuto) setName(''); });
if (windowed) $('popout').hidden = true;

// Page titles carry extras like "(3) " unread counts and " - YouTube".
function cleanTitle(t) {
  return String(t || '')
    .replace(/^\(\d+\)\s*/, '')
    .replace(/\s+[-|–—]\s+(YouTube|Vimeo|Dailymotion|Twitch|Facebook|Instagram|X|Twitter|TikTok|Reddit|SoundCloud)\s*$/i, '')
    .trim();
}

let nameIsAuto = false;
function setName(value, auto = false) {
  $('name').value = value;
  nameIsAuto = auto && !!value;
}

function prefill(url, title) {
  if (!/^https?:\/\//i.test(url || '')) return;
  $('url').value = url;
  setName(cleanTitle(title), true);
  updatePlaylistToggle();
}

// Prefill the link: the CLDM window gets it passed in, the toolbar popup uses the current tab.
(async () => {
  let url = params.get('url') || '';
  let title = params.get('title') || '';
  if (!url && !windowed) {
    try {
      const [tab] = await chrome.tabs.query({ active: true, lastFocusedWindow: true });
      url = tab?.url || '';
      title = tab?.title || '';
    } catch { /* no tab access */ }
  }
  prefill(url, title);
})();
