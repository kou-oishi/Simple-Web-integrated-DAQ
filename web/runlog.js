const pageTitle = document.getElementById('page_title');
const runLogBody = document.getElementById('runlog_tbody');
const limitInput = document.getElementById('runlog_limit');
const offsetInput = document.getElementById('runlog_offset');
const statusSelect = document.getElementById('runlog_status');
const runInput = document.getElementById('runlog_run');
const subrunInput = document.getElementById('runlog_subrun');
const autoInput = document.getElementById('runlog_auto');
const viewAllInput = document.getElementById('view_all');
const viewSummaryInput = document.getElementById('view_summary');
const subrunHeader = document.getElementById('th_subrun');

let timerId = null;
let statusTimerId = null;
let eventsPerFileDefault = 0;

function setMessage(text, ok = true) {
  if (!window.SimpleDaqStatusPanel || typeof window.SimpleDaqStatusPanel.ensureElements !== 'function') return;
  const elements = window.SimpleDaqStatusPanel.ensureElements();
  if (!elements || !elements.msg) return;
  elements.msg.className = 'msg status-msg ' + (ok ? 'ok' : 'err');
  elements.msg.textContent = text || '';
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

async function postApi(path, payload) {
  const res = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload || {}),
  });
  let data = null;
  try { data = await res.json(); } catch (_) {}
  if (!res.ok) {
    throw new Error(parseApiError(data, res.status));
  }
  return data;
}

async function setSelectedAnalysis(moduleName) {
  try {
    await postApi('/api/monitor/selected-analysis', { module: moduleName || null });
  } catch (_) {
    // ignore selection sync failures
  }
}

function statusClass(statusText) {
  const s = String(statusText || '').toLowerCase();
  if (s.startsWith('completed with ')) return 'runlog-status-warn';
  if (s === 'completed' || s.startsWith('completed ')) return 'runlog-status-ok';
  if (s.startsWith('exit with ')) return 'runlog-status-error';
  if (s === 'error' || s.includes('fail')) return 'runlog-status-error';
  if (s === 'warn' || s.includes('warn')) return 'runlog-status-warn';
  if (s === 'info' || s.includes('info')) return 'runlog-status-info';
  return '';
}

function matchFilter(row) {
  const statusFilter = String(statusSelect.value || '').trim().toLowerCase();
  const runFilter = String(runInput.value || '').trim();
  const subrunFilter = String(subrunInput.value || '').trim();
  if (statusFilter !== '') {
    const statusValue = String(row.status || '').toLowerCase();
    if (!(statusValue === statusFilter || statusValue.startsWith(statusFilter + " "))) {
      return false;
    }
  }
  if (runFilter !== '' && Number(row.run) !== Number(runFilter)) return false;
  if (subrunFilter !== '' && Number(row.subrun) !== Number(subrunFilter)) return false;
  return true;
}

function selectedView() {
  if (viewSummaryInput && viewSummaryInput.checked) {
    return 'summary';
  }
  return 'all';
}

function updateViewLabels() {
  const view = selectedView();
  if (subrunHeader) {
    subrunHeader.textContent = (view === 'summary') ? 'subruns' : 'subrun';
  }
}

async function refreshRunLog() {
  try {
    const limit = Math.max(1, Math.min(1000, Number(limitInput.value || 200)));
    const offset = Math.max(0, Number(offsetInput.value || 0));
    const view = selectedView();
    const data = await callApi(`/api/run-log?limit=${limit}&offset=${offset}&view=${encodeURIComponent(view)}`);
    runLogBody.innerHTML = '';
    (data.rows || []).filter(matchFilter).forEach((r) => {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${r.run}</td><td>${r.subrun}</td><td>${r.nevents}</td><td>${r.start_time}</td><td>${r.end_time}</td><td class="${statusClass(r.status)}">${r.status || ''}</td><td>${r.connected || ''}</td><td>${r.disconnected || ''}</td><td>${r.comment || ''}</td>`;
      runLogBody.appendChild(tr);
    });
    updateViewLabels();
    setMessage('', true);
  } catch (e) {
    setMessage(`run log error: ${e.message}`, false);
  }
}

async function refreshStatus() {
  if (!window.SimpleDaqStatusPanel || typeof window.SimpleDaqStatusPanel.refreshGlobalStatus !== 'function') {
    return;
  }
  await window.SimpleDaqStatusPanel.refreshGlobalStatus({
    fetchJson: callApi,
    eventsPerFileDefault,
    setMessage,
  });
}

function updateAutoRefresh() {
  if (timerId !== null) {
    clearInterval(timerId);
    timerId = null;
  }
  if (autoInput.checked) {
    refreshRunLog();
    timerId = setInterval(refreshRunLog, 2000);
  }
}

async function loadUiConfig() {
  try {
    const cfg = await callApi('/api/ui-config');
    const title = String(cfg.title || 'DAQ Control');
    document.title = `${title} - Run Log`;
    pageTitle.textContent = title;
    limitInput.value = String(Number(cfg.run_log_page_limit || 200));
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
if (viewAllInput) {
  viewAllInput.addEventListener('change', refreshRunLog);
}
if (viewSummaryInput) {
  viewSummaryInput.addEventListener('change', refreshRunLog);
}

if (window.SimpleDaqStatusPanel && typeof window.SimpleDaqStatusPanel.mountGlobalStatusPanel === 'function') {
  window.SimpleDaqStatusPanel.mountGlobalStatusPanel();
}
updateViewLabels();
loadUiConfig().then(() => setSelectedAnalysis(null)).then(refreshRunLog);
refreshStatus();
statusTimerId = setInterval(refreshStatus, 1000);
