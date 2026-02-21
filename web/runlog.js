let msg = document.getElementById('msg');
const pageTitle = document.getElementById('page_title');
const runLogBody = document.getElementById('runlog_tbody');
let stateLamp = document.getElementById('state_lamp');
let statusState = document.getElementById('status_state');
let statusRun = document.getElementById('status_run');
let statusSubrun = document.getElementById('status_subrun');
let statusEventsTotal = document.getElementById('status_events_total');
let statusUptime = document.getElementById('status_uptime');
let statusFileProgressText = document.getElementById('status_file_progress_text');
let statusFileProgressFill = document.getElementById('status_file_progress_fill');
const limitInput = document.getElementById('runlog_limit');
const offsetInput = document.getElementById('runlog_offset');
const statusSelect = document.getElementById('runlog_status');
const runInput = document.getElementById('runlog_run');
const subrunInput = document.getElementById('runlog_subrun');
const autoInput = document.getElementById('runlog_auto');

let timerId = null;
let statusTimerId = null;
let eventsPerFileDefault = 0;
let cachedNextRun = null;
let cachedNextRunAtMs = 0;

function ensureStatusPanelElements() {
  if (window.SimpleDaqStatusPanel && typeof window.SimpleDaqStatusPanel.mountGlobalStatusPanel === 'function') {
    window.SimpleDaqStatusPanel.mountGlobalStatusPanel();
  }
  msg = document.getElementById('msg');
  stateLamp = document.getElementById('state_lamp');
  statusState = document.getElementById('status_state');
  statusRun = document.getElementById('status_run');
  statusSubrun = document.getElementById('status_subrun');
  statusEventsTotal = document.getElementById('status_events_total');
  statusUptime = document.getElementById('status_uptime');
  statusFileProgressText = document.getElementById('status_file_progress_text');
  statusFileProgressFill = document.getElementById('status_file_progress_fill');
  return Boolean(
    msg &&
    stateLamp &&
    statusState &&
    statusRun &&
    statusSubrun &&
    statusEventsTotal &&
    statusUptime &&
    statusFileProgressText &&
    statusFileProgressFill
  );
}

function setMessage(text, ok = true) {
  if (!msg && !ensureStatusPanelElements()) return;
  msg.className = 'msg status-msg ' + (ok ? 'ok' : 'err');
  msg.textContent = text || '';
}

function parseApiError(data, status) {
  if (!data) return `HTTP ${status}`;
  const detail = data.detail !== undefined ? data.detail : data;
  if (typeof detail === 'string') return detail;
  if (detail && typeof detail === 'object' && detail.error) return String(detail.error);
  return JSON.stringify(detail);
}

async function callApi(path) {
  const res = await fetch(path);
  let data = null;
  try { data = await res.json(); } catch (_) {}
  if (!res.ok) {
    throw new Error(parseApiError(data, res.status));
  }
  return data;
}

function statusClass(statusText) {
  const s = String(statusText || '').toLowerCase();
  if (s === 'error' || s.includes('fail')) return 'runlog-status-error';
  if (s === 'warn' || s.includes('warn')) return 'runlog-status-warn';
  if (s === 'info' || s.includes('info')) return 'runlog-status-info';
  return '';
}

function matchFilter(row) {
  const statusFilter = String(statusSelect.value || '').trim().toLowerCase();
  const runFilter = String(runInput.value || '').trim();
  const subrunFilter = String(subrunInput.value || '').trim();
  if (statusFilter !== '' && String(row.status || '').toLowerCase() !== statusFilter) return false;
  if (runFilter !== '' && Number(row.run) !== Number(runFilter)) return false;
  if (subrunFilter !== '' && Number(row.subrun) !== Number(subrunFilter)) return false;
  return true;
}

async function refreshRunLog() {
  try {
    const limit = Math.max(1, Math.min(1000, Number(limitInput.value || 200)));
    const offset = Math.max(0, Number(offsetInput.value || 0));
    const data = await callApi(`/api/run-log?limit=${limit}&offset=${offset}`);
    runLogBody.innerHTML = '';
    (data.rows || []).filter(matchFilter).forEach((r) => {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${r.run}</td><td>${r.subrun}</td><td>${r.nevents}</td><td>${r.start_time}</td><td>${r.end_time}</td><td class="${statusClass(r.status)}">${r.status || ''}</td><td>${r.comment || ''}</td>`;
      runLogBody.appendChild(tr);
    });
    setMessage('', true);
  } catch (e) {
    setMessage(`run log error: ${e.message}`, false);
  }
}

async function refreshStatus() {
  if (!ensureStatusPanelElements()) {
    return;
  }
  try {
    const data = await callApi('/api/status');
    const running = Number(data.running || 0) === 1;
    const state = String(data.state || '-');
    const run = (data.Run !== undefined) ? Number(data.Run) : 0;
    const subrun = (data.subrun !== undefined) ? Number(data.subrun) : 0;
    const total = (data.events_total !== undefined) ? Number(data.events_total) : 0;
    const uptime = (data.uptime_sec !== undefined) ? Number(data.uptime_sec) : 0;

    if (state === 'running') {
      stateLamp.className = 'lamp ok';
    } else if (state === 'paused' || state === 'pausing' || state === 'stopping') {
      stateLamp.className = 'lamp warn';
    } else if (state === 'error') {
      stateLamp.className = 'lamp bad';
    } else {
      stateLamp.className = 'lamp';
    }
    statusState.textContent = state;
    let displayRun = run;
    let displaySubrun = subrun;
    if (!running && state !== 'running') {
      try {
        displayRun = await fetchNextRun(false);
        displaySubrun = 0;
      } catch (_) {
        displayRun = run + 1;
        displaySubrun = 0;
      }
    }
    statusRun.textContent = String(displayRun);
    statusSubrun.textContent = String(displaySubrun);
    statusEventsTotal.textContent = String(running ? total : 0);
    statusUptime.textContent = `${uptime}s`;

    const fromStatusPerFile = Number(data.events_per_file || 0);
    const perFile = fromStatusPerFile > 0 ? fromStatusPerFile : eventsPerFileDefault;
    const fromStatusInFile = Number(data.events_in_file || 0);
    let inFile = 0;
    if (running && fromStatusInFile > 0) {
      inFile = fromStatusInFile;
    } else if (perFile > 0 && running) {
      inFile = total % perFile;
      if (inFile === 0 && total > 0) {
        inFile = perFile;
      }
    }
    const ratio = perFile > 0 ? Math.max(0, Math.min(1, inFile / perFile)) : 0;
    statusFileProgressText.textContent = `${inFile} / ${perFile}`;
    statusFileProgressFill.style.width = `${Math.round(ratio * 100)}%`;
  } catch (e) {
    stateLamp.className = 'lamp bad';
    statusState.textContent = 'daqd unreachable';
    statusRun.textContent = '-';
    statusSubrun.textContent = '-';
    statusEventsTotal.textContent = '0';
    statusUptime.textContent = '0s';
    statusFileProgressText.textContent = '0 / 0';
    statusFileProgressFill.style.width = '0%';
    setMessage(`status error: ${e.message}`, false);
  }
}

async function fetchNextRun(force = false) {
  const now = Date.now();
  if (!force && cachedNextRun !== null && (now - cachedNextRunAtMs) < 5000) {
    return cachedNextRun;
  }
  const data = await callApi('/api/next-run');
  cachedNextRun = Number(data.next_run);
  cachedNextRunAtMs = now;
  return cachedNextRun;
}

function updateAutoRefresh() {
  if (timerId !== null) {
    clearInterval(timerId);
    timerId = null;
  }
  if (autoInput.checked) {
    timerId = setInterval(refreshRunLog, 2000);
  }
}

async function loadUiConfig() {
  try {
    const cfg = await callApi('/api/ui-config');
    const title = String(cfg.title || 'DAQ Control');
    document.title = `${title} - Run Log`;
    pageTitle.textContent = title;
    limitInput.value = String(Number(cfg.run_log_limit || 200));
    eventsPerFileDefault = Math.max(0, Number(cfg.events_per_file || 0));
  } catch (_) {
    // ignore
  }
}

document.getElementById('btn_refresh_runlog').addEventListener('click', refreshRunLog);
statusSelect.addEventListener('change', refreshRunLog);
runInput.addEventListener('change', refreshRunLog);
subrunInput.addEventListener('change', refreshRunLog);
limitInput.addEventListener('change', refreshRunLog);
offsetInput.addEventListener('change', refreshRunLog);
autoInput.addEventListener('change', updateAutoRefresh);

ensureStatusPanelElements();
loadUiConfig().then(refreshRunLog);
refreshStatus();
statusTimerId = setInterval(refreshStatus, 1000);
