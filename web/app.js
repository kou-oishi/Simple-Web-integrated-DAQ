const msg = document.getElementById('msg');
const runLogBody = document.getElementById('runlog_tbody');
const healthLamp = document.getElementById('health_lamp');
const healthText = document.getElementById('health_text');
const stateLamp = document.getElementById('state_lamp');
const statusState = document.getElementById('status_state');
const statusRun = document.getElementById('status_run');
const statusSubrun = document.getElementById('status_subrun');
const statusEventsTotal = document.getElementById('status_events_total');
const statusUptime = document.getElementById('status_uptime');
const statusFileProgressText = document.getElementById('status_file_progress_text');
const statusFileProgressFill = document.getElementById('status_file_progress_fill');
const statusError = document.getElementById('status_error');
const frontendSelect = document.getElementById('frontend_select');
const deviceFields = document.getElementById('device_fields');
const devicesBody = document.getElementById('devices_tbody');
const pageTitle = document.getElementById('page_title');
const daqdLogView = document.getElementById('daqd_log_view');
const datamonLogView = document.getElementById('datamon_log_view');
const monitorHealthLamp = document.getElementById('monitor_health_lamp');
const monitorHealthText = document.getElementById('monitor_health_text');
const monitorStatusError = document.getElementById('monitor_status_error');
const monitorDecoders = document.getElementById('monitor_decoders');
const monitorAnalyses = document.getElementById('monitor_analyses');

let uiConfig = {};
let frontendCatalog = [];
let deviceEntries = [];
let monitorCatalog = { decoders: [], analyses: [] };
let selectedMonitorDecoder = '';
let selectedMonitorAnalyses = new Set();
let monitorSnapshotIntervalSec = 1.0;
let cachedNextRun = null;
let cachedNextRunAtMs = 0;
let daqConnected = false;
let statusPollInFlight = false;
let msgSource = '';
let daqState = '-';
let runLogLimit = 50;
let monitorRunning = false;
const STATUS_API_TIMEOUT_MS = 1500;
const MAIN_FORM_STATE_KEY = 'simpledaq_main_form_state_v1';
const MONITOR_STATE_KEY = 'simpledaq_monitor_state_v1';
const DEVICES_STATE_KEY = 'simpledaq_devices_state_v1';

function loadMainFormState() {
  try {
    const raw = window.localStorage.getItem(MAIN_FORM_STATE_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== 'object') return null;
    return parsed;
  } catch (_) {
    return null;
  }
}

function saveMainFormState() {
  try {
    const eventsPerFileInput = document.getElementById('events_per_file');
    const startupTimeoutInput = document.getElementById('startup_connect_timeout_sec');
    const reconnectFailureTimeoutInput = document.getElementById('reconnect_failure_timeout_sec');
    const partialRunInput = document.getElementById('allow_partial_run_on_runtime_disconnect');
    const commentInput = document.getElementById('comment');
    const payload = {
      events_per_file: eventsPerFileInput ? String(eventsPerFileInput.value || '') : '',
      startup_connect_timeout_sec: startupTimeoutInput ? String(startupTimeoutInput.value || '') : '',
      reconnect_failure_timeout_sec: reconnectFailureTimeoutInput ? String(reconnectFailureTimeoutInput.value || '') : '',
      allow_partial_run_on_runtime_disconnect: partialRunInput ? !!partialRunInput.checked : false,
      comment: commentInput ? String(commentInput.value || '') : '',
    };
    window.localStorage.setItem(MAIN_FORM_STATE_KEY, JSON.stringify(payload));
  } catch (_) {
    // ignore storage failures
  }
}

function bindMainFormStateSave() {
  const ids = [
    'events_per_file',
    'startup_connect_timeout_sec',
    'reconnect_failure_timeout_sec',
    'allow_partial_run_on_runtime_disconnect',
    'comment',
  ];
  ids.forEach((id) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.addEventListener('input', saveMainFormState);
    el.addEventListener('change', saveMainFormState);
  });
}

function bindMonitorStateSave() {
  const snapshotIntervalInput = document.getElementById('monitor_snapshot_interval_sec');
  if (snapshotIntervalInput) {
    const persist = () => {
      monitorSnapshotIntervalSec = Math.max(0.1, Number(snapshotIntervalInput.value || 1.0));
      saveMonitorState();
    };
    snapshotIntervalInput.addEventListener('input', persist);
    snapshotIntervalInput.addEventListener('change', persist);
  }
}

function loadMonitorState() {
  try {
    const raw = window.localStorage.getItem(MONITOR_STATE_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== 'object') return null;
    return parsed;
  } catch (_) {
    return null;
  }
}

function saveMonitorState() {
  try {
    const payload = {
      decoder: selectedMonitorDecoder || '',
      analyses: Array.from(selectedMonitorAnalyses.values()),
      snapshot_interval_sec: Number(monitorSnapshotIntervalSec || 1.0),
    };
    window.localStorage.setItem(MONITOR_STATE_KEY, JSON.stringify(payload));
  } catch (_) {
    // ignore storage failures
  }
}

function loadDevicesState() {
  try {
    const raw = window.localStorage.getItem(DEVICES_STATE_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw);
    if (!Array.isArray(parsed)) return null;
    return parsed
      .filter((item) => item && typeof item === 'object')
      .map((item) => ({ ...item }));
  } catch (_) {
    return null;
  }
}

function saveDevicesState() {
  try {
    window.localStorage.setItem(DEVICES_STATE_KEY, JSON.stringify(deviceEntries));
  } catch (_) {
    // ignore storage failures
  }
}

function clearSavedMainSetupState() {
  try {
    window.localStorage.removeItem(MAIN_FORM_STATE_KEY);
    window.localStorage.removeItem(MONITOR_STATE_KEY);
    window.localStorage.removeItem(DEVICES_STATE_KEY);
  } catch (_) {
    // ignore storage failures
  }
}

function setActionButtonsByState(state, running) {
  const btnStart = document.getElementById('btn_start');
  const btnPause = document.getElementById('btn_pause');
  const btnResume = document.getElementById('btn_resume');
  const btnStop = document.getElementById('btn_stop');
  const btnShutdown = document.getElementById('btn_shutdown');

  if (!daqConnected) {
    if (btnStart) btnStart.disabled = true;
    if (btnPause) btnPause.disabled = true;
    if (btnResume) btnResume.disabled = true;
    if (btnStop) btnStop.disabled = true;
    if (btnShutdown) btnShutdown.disabled = true;
    return;
  }

  if (running || state === 'running') {
    if (btnStart) btnStart.disabled = true;
    if (btnPause) btnPause.disabled = false;
    if (btnResume) btnResume.disabled = true;
    if (btnStop) btnStop.disabled = false;
    if (btnShutdown) btnShutdown.disabled = false;
    return;
  }

  if (state === 'paused') {
    if (btnStart) btnStart.disabled = true;
    if (btnPause) btnPause.disabled = true;
    if (btnResume) btnResume.disabled = false;
    if (btnStop) btnStop.disabled = false;
    if (btnShutdown) btnShutdown.disabled = false;
    return;
  }

  if (state === 'pausing' || state === 'stopping') {
    if (btnStart) btnStart.disabled = true;
    if (btnPause) btnPause.disabled = true;
    if (btnResume) btnResume.disabled = true;
    if (btnStop) btnStop.disabled = true;
    if (btnShutdown) btnShutdown.disabled = false;
    return;
  }

  if (btnStart) btnStart.disabled = false;
  if (btnPause) btnPause.disabled = true;
  if (btnResume) btnResume.disabled = true;
  if (btnStop) btnStop.disabled = true;
  if (btnShutdown) btnShutdown.disabled = false;
}

function setMonitorControlsByState(running) {
  monitorRunning = running;
  const btnStart = document.getElementById('btn_start_datamon');
  const btnShutdown = document.getElementById('btn_shutdown_datamon');
  const snapshotIntervalInput = document.getElementById('monitor_snapshot_interval_sec');
  if (btnStart) btnStart.disabled = running;
  if (btnShutdown) btnShutdown.disabled = !running;
  if (snapshotIntervalInput) snapshotIntervalInput.disabled = running;
  const decoderInputs = monitorDecoders ? monitorDecoders.querySelectorAll('input[type="checkbox"]') : [];
  const analysisInputs = monitorAnalyses ? monitorAnalyses.querySelectorAll('input[type="checkbox"]') : [];
  decoderInputs.forEach((el) => { el.disabled = running; });
  analysisInputs.forEach((el) => { el.disabled = running; });
}

function ensureMonitorSelections() {
  if (!Array.isArray(monitorCatalog.decoders) || monitorCatalog.decoders.length === 0) {
    selectedMonitorDecoder = '';
    selectedMonitorAnalyses.clear();
    return;
  }
  if (!selectedMonitorDecoder) {
    const fromConfig = String(uiConfig.datamon_decoder || '').trim();
    const found = monitorCatalog.decoders.find((d) => String(d.name || '') === fromConfig);
    selectedMonitorDecoder = found ? String(found.name) : String(monitorCatalog.decoders[0].name || '');
  }
  const validDecoders = new Set(monitorCatalog.decoders.map((d) => String(d.name || '')));
  if (!validDecoders.has(selectedMonitorDecoder)) {
    selectedMonitorDecoder = String(monitorCatalog.decoders[0].name || '');
  }
  const validAnalyses = new Set(
    (Array.isArray(monitorCatalog.analyses) ? monitorCatalog.analyses : [])
      .map((a) => String(a.name || '').trim())
      .filter((x) => x !== '')
  );
  selectedMonitorAnalyses = new Set(
    Array.from(selectedMonitorAnalyses.values()).filter((name) => validAnalyses.has(name))
  );
}

function onMonitorAnalysisToggle(name, checked) {
  if (checked) {
    selectedMonitorAnalyses.add(name);
    const analysis = monitorCatalog.analyses.find((a) => String(a.name || '') === name);
    if (analysis) {
      const expected = String(analysis.expected_decoder || '').trim();
      if (expected) {
        selectedMonitorDecoder = expected;
      }
    }
  } else {
    selectedMonitorAnalyses.delete(name);
  }
  saveMonitorState();
  renderMonitorModules();
}

function renderMonitorModules() {
  if (!monitorDecoders || !monitorAnalyses) return;
  ensureMonitorSelections();
  const decoderTitleByName = new Map();
  (Array.isArray(monitorCatalog.decoders) ? monitorCatalog.decoders : []).forEach((item) => {
    const name = String(item.name || '').trim();
    if (!name) return;
    const title = String(item.title || name).trim() || name;
    decoderTitleByName.set(name, title);
  });

  monitorDecoders.innerHTML = '';
  if (!Array.isArray(monitorCatalog.decoders) || monitorCatalog.decoders.length === 0) {
    monitorDecoders.innerHTML = '<div class="check-help">(none)</div>';
  } else {
    monitorCatalog.decoders.forEach((item) => {
      const name = String(item.name || '').trim();
      const title = String(item.title || name).trim() || name;
      if (!name) return;
      const row = document.createElement('label');
      row.className = 'check-item';
      const checked = name === selectedMonitorDecoder ? 'checked' : '';
      row.innerHTML = `<input type="checkbox" data-monitor-decoder="${name}" ${checked} /> <span>${title}</span>`;
      monitorDecoders.appendChild(row);
    });
    monitorDecoders.querySelectorAll('input[data-monitor-decoder]').forEach((el) => {
      el.addEventListener('change', () => {
        if (el.checked) {
          selectedMonitorDecoder = String(el.getAttribute('data-monitor-decoder') || '');
          saveMonitorState();
        }
        renderMonitorModules();
      });
    });
  }

  monitorAnalyses.innerHTML = '';
  if (!Array.isArray(monitorCatalog.analyses) || monitorCatalog.analyses.length === 0) {
    monitorAnalyses.innerHTML = '<div class="check-help">(none)</div>';
  } else {
    monitorCatalog.analyses.forEach((item) => {
      const name = String(item.name || '').trim();
      const title = String(item.title || name).trim() || name;
      if (!name) return;
      const expected = String(item.expected_decoder || '').trim();
      const checked = selectedMonitorAnalyses.has(name) ? 'checked' : '';
      const expectedTitle = expected ? (decoderTitleByName.get(expected) || expected) : '';
      const hint = expected ? `<span class="check-help">requires ${expectedTitle}</span>` : '';
      const row = document.createElement('label');
      row.className = 'check-item';
      row.innerHTML = `<input type="checkbox" data-monitor-analysis="${name}" ${checked} /> <span>${title}</span>${hint}`;
      monitorAnalyses.appendChild(row);
    });
    monitorAnalyses.querySelectorAll('input[data-monitor-analysis]').forEach((el) => {
      el.addEventListener('change', () => {
        const name = String(el.getAttribute('data-monitor-analysis') || '');
        onMonitorAnalysisToggle(name, el.checked);
      });
    });
  }
  setMonitorControlsByState(monitorRunning);
}

function isNearBottom(el, thresholdPx = 8) {
  const remain = el.scrollHeight - el.scrollTop - el.clientHeight;
  return remain <= thresholdPx;
}

function setMessage(text, ok = true) {
  msg.className = 'msg status-msg ' + (ok ? 'ok' : 'err');
  msg.textContent = text || '';
  if (ok) {
    msgSource = '';
  }
  if (text.trim() === '') {
    msgSource = '';
  }
}

function setConnectionErrorMessage(text) {
  msgSource = 'connection';
  setMessage(text, false);
}

function clearConnectionErrorMessageIfAny() {
  if (msgSource === 'connection' && msg.className.includes('err') && msg.textContent.trim() !== '') {
    setMessage('', true);
  }
}

function setDaqConnection(connected, reason = '') {
  daqConnected = connected;
  const daqdStartButton = document.getElementById('btn_start_daqd');
  if (daqdStartButton) {
    daqdStartButton.disabled = connected;
  }
  setActionButtonsByState(daqState, false);

  if (!connected) {
    healthLamp.className = 'lamp bad';
    healthText.textContent = 'Disconnected';
    stateLamp.className = 'lamp bad';
    statusState.textContent = 'daqd unreachable';
    statusRun.textContent = '-';
    statusSubrun.textContent = '-';
    statusEventsTotal.textContent = '0';
    statusUptime.textContent = '0s';
    statusFileProgressText.textContent = '0 / 0';
    statusFileProgressFill.style.width = '0%';
    statusError.textContent = reason || 'Cannot communicate with daqd';
  }
}

async function refreshDaqdLog() {
  try {
    const shouldFollow = isNearBottom(daqdLogView);
    const limit = Number(uiConfig.daqd_log_limit || 300);
    const data = await callApi(`/api/daqd/log?limit=${limit}`);
    const lines = Array.isArray(data.lines) ? data.lines : [];
    if (lines.length === 0) {
      daqdLogView.textContent = '(no log)';
    } else {
      const rendered = lines.map((line) => {
        const escaped = line
          .replaceAll('&', '&amp;')
          .replaceAll('<', '&lt;')
          .replaceAll('>', '&gt;');
        if (line.includes('[ERROR]')) return `<span class="log-line log-error">${escaped}</span>`;
        if (line.includes('[WARN]')) return `<span class="log-line log-warn">${escaped}</span>`;
        if (line.includes('[INFO]')) return `<span class="log-line log-info">${escaped}</span>`;
        return `<span class="log-line">${escaped}</span>`;
      });
      daqdLogView.innerHTML = rendered.join('');
    }
    if (shouldFollow) {
      daqdLogView.scrollTop = daqdLogView.scrollHeight;
    }
  } catch (e) {
    daqdLogView.textContent = `log error: ${e.message}`;
  }
}

async function refreshDatamonLog() {
  if (!datamonLogView) return;
  try {
    const shouldFollow = isNearBottom(datamonLogView);
    const limit = Number(uiConfig.datamon_log_limit || 300);
    const data = await callApi(`/api/monitor/log?limit=${limit}`);
    const lines = Array.isArray(data.lines) ? data.lines : [];
    if (lines.length === 0) {
      datamonLogView.textContent = '(no log)';
    } else {
      const rendered = lines.map((line) => {
        const escaped = line
          .replaceAll('&', '&amp;')
          .replaceAll('<', '&lt;')
          .replaceAll('>', '&gt;');
        if (line.includes('[ERROR]')) return `<span class="log-line log-error">${escaped}</span>`;
        if (line.includes('[WARN]')) return `<span class="log-line log-warn">${escaped}</span>`;
        if (line.includes('[INFO]')) return `<span class="log-line log-info">${escaped}</span>`;
        return `<span class="log-line">${escaped}</span>`;
      });
      datamonLogView.innerHTML = rendered.join('');
    }
    if (shouldFollow) {
      datamonLogView.scrollTop = datamonLogView.scrollHeight;
    }
  } catch (e) {
    datamonLogView.textContent = `log error: ${e.message}`;
  }
}

async function refreshDatamonStatus() {
  if (!monitorHealthLamp || !monitorHealthText) return;
  try {
    const data = await callApi('/api/monitor/status');
    const running = Boolean(data.running);
    const state = String(data.state || (running ? 'running' : 'stopped'));
    const healthy = Number(data.healthy || 0) === 1;
    monitorHealthLamp.className = `lamp ${healthy ? 'ok' : (state === 'error' ? 'bad' : '')}`.trim();
    monitorHealthText.textContent = running ? 'Running' : (state === 'error' ? 'Error' : 'Stopped');
    monitorStatusError.textContent = (state === 'error' && Number(data.last_exit || 0) !== 0)
      ? `last exit code: ${Number(data.last_exit)}`
      : '';
    setMonitorControlsByState(running);
  } catch (e) {
    monitorHealthLamp.className = 'lamp bad';
    monitorHealthText.textContent = 'Unavailable';
    monitorStatusError.textContent = `monitor status error: ${e.message}`;
    setMonitorControlsByState(false);
  }
}

async function refreshWholeUi() {
  try {
    await loadMonitorModules();
  } catch (_) {
    // ignore
  }
  await Promise.all([
    refreshStatus(),
    refreshRunLog(),
    refreshDaqdLog(),
    refreshDatamonStatus(),
    refreshDatamonLog(),
  ]);
  if (window.SimpleDaqSidebar && typeof window.SimpleDaqSidebar.refreshAnalysisMenu === 'function') {
    try {
      await window.SimpleDaqSidebar.refreshAnalysisMenu();
    } catch (_) {
      // ignore sidebar refresh errors
    }
  }
}

function parseApiError(data, status) {
  if (!data) return `HTTP ${status}`;
  const detail = data.detail !== undefined ? data.detail : data;
  if (typeof detail === 'string') return detail;
  if (detail && typeof detail === 'object') {
    const parts = [];
    if (detail.error) parts.push(String(detail.error));
    if (detail.stderr) parts.push(`stderr: ${String(detail.stderr)}`);
    if (detail.query) parts.push(`query: ${String(detail.query)}`);
    if (parts.length > 0) return parts.join(' | ');
  }
  if (data.error) return String(data.error);
  return JSON.stringify(detail);
}

async function callApi(path, method = 'GET', payload = null, options = {}) {
  const timeoutMsRaw = Number(options && options.timeoutMs ? options.timeoutMs : 0);
  const timeoutMs = Number.isFinite(timeoutMsRaw) ? Math.max(0, Math.floor(timeoutMsRaw)) : 0;
  const controller = (typeof AbortController !== 'undefined') ? new AbortController() : null;
  const opt = { method, headers: {} };
  if (payload !== null) {
    opt.headers['Content-Type'] = 'application/json';
    opt.body = JSON.stringify(payload);
  }
  if (controller) {
    opt.signal = controller.signal;
  }
  let timeoutId = null;
  if (timeoutMs > 0) {
    timeoutId = window.setTimeout(() => {
      if (controller) controller.abort();
    }, timeoutMs);
  }
  let res;
  try {
    res = await fetch(path, opt);
  } catch (e) {
    if (timeoutMs > 0 && e && e.name === 'AbortError') {
      throw new Error(`request timeout after ${timeoutMs}ms`);
    }
    throw e;
  } finally {
    if (timeoutId !== null) {
      window.clearTimeout(timeoutId);
    }
  }
  let data = null;
  try { data = await res.json(); } catch (_) {}
  if (!res.ok) {
    throw new Error(parseApiError(data, res.status));
  }
  return data;
}

async function setSelectedAnalysis(moduleName) {
  try {
    await callApi('/api/monitor/selected-analysis', 'POST', { module: moduleName || null });
  } catch (_) {
    // ignore selection sync failures
  }
}

async function loadUiConfig(options = {}) {
  const preferSavedState = options.preferSavedState !== false;
  uiConfig = await callApi('/api/ui-config');
  const title = String(uiConfig.title || 'DAQ Control');
  document.title = title;
  pageTitle.textContent = title;
  const eventsPerFileInput = document.getElementById('events_per_file');
  const startupTimeoutInput = document.getElementById('startup_connect_timeout_sec');
  const reconnectFailureTimeoutInput = document.getElementById('reconnect_failure_timeout_sec');
  const partialRunInput = document.getElementById('allow_partial_run_on_runtime_disconnect');
  const commentInput = document.getElementById('comment');
  const monitorSnapshotIntervalInput = document.getElementById('monitor_snapshot_interval_sec');
  if (uiConfig.events_per_file) {
    if (eventsPerFileInput) eventsPerFileInput.value = Number(uiConfig.events_per_file);
  }
  if (uiConfig.startup_connect_timeout_sec) {
    if (startupTimeoutInput) startupTimeoutInput.value = Number(uiConfig.startup_connect_timeout_sec);
  }
  if (uiConfig.reconnect_failure_timeout_sec) {
    if (reconnectFailureTimeoutInput) reconnectFailureTimeoutInput.value = Number(uiConfig.reconnect_failure_timeout_sec);
  }
  if (partialRunInput && uiConfig.allow_partial_run_on_runtime_disconnect !== undefined) {
    partialRunInput.checked = !!uiConfig.allow_partial_run_on_runtime_disconnect;
  }
  if (uiConfig.comment !== undefined && uiConfig.comment !== null) {
    if (commentInput) commentInput.value = String(uiConfig.comment);
  }
  if (uiConfig.datamon_snapshot_interval_sec !== undefined && monitorSnapshotIntervalInput) {
    monitorSnapshotIntervalInput.value = String(Number(uiConfig.datamon_snapshot_interval_sec));
  }
  const saved = preferSavedState ? loadMainFormState() : null;
  if (saved) {
    if (eventsPerFileInput && saved.events_per_file !== undefined) {
      eventsPerFileInput.value = String(saved.events_per_file);
    }
    if (startupTimeoutInput && saved.startup_connect_timeout_sec !== undefined) {
      startupTimeoutInput.value = String(saved.startup_connect_timeout_sec);
    }
    if (reconnectFailureTimeoutInput && saved.reconnect_failure_timeout_sec !== undefined) {
      reconnectFailureTimeoutInput.value = String(saved.reconnect_failure_timeout_sec);
    }
    if (partialRunInput && saved.allow_partial_run_on_runtime_disconnect !== undefined) {
      partialRunInput.checked = !!saved.allow_partial_run_on_runtime_disconnect;
    }
    if (commentInput && saved.comment !== undefined) {
      commentInput.value = String(saved.comment);
    }
  }
  runLogLimit = Number(uiConfig.main_run_log_limit || 50);
  selectedMonitorDecoder = String(uiConfig.datamon_decoder || '').trim();
  selectedMonitorAnalyses = new Set(Array.isArray(uiConfig.datamon_analyses) ? uiConfig.datamon_analyses.map((x) => String(x)) : []);
  const monitorSaved = preferSavedState ? loadMonitorState() : null;
  if (monitorSaved) {
    if (monitorSaved.decoder !== undefined) {
      selectedMonitorDecoder = String(monitorSaved.decoder || '').trim();
    }
    if (Array.isArray(monitorSaved.analyses)) {
      selectedMonitorAnalyses = new Set(monitorSaved.analyses.map((x) => String(x)));
    }
    if (monitorSaved.snapshot_interval_sec !== undefined && monitorSnapshotIntervalInput) {
      monitorSnapshotIntervalInput.value = String(Number(monitorSaved.snapshot_interval_sec));
    } else if (monitorSaved.snapshot_interval_ms !== undefined && monitorSnapshotIntervalInput) {
      monitorSnapshotIntervalInput.value = String(Number(monitorSaved.snapshot_interval_ms) / 1000.0);
    }
  }
  monitorSnapshotIntervalSec = Math.max(0.1, Number((monitorSnapshotIntervalInput && monitorSnapshotIntervalInput.value) || 1.0));
  if (Array.isArray(uiConfig.devices)) {
    deviceEntries = uiConfig.devices.slice();
  }
  const savedDevices = preferSavedState ? loadDevicesState() : null;
  if (savedDevices) {
    deviceEntries = savedDevices;
  }
  if (Array.isArray(deviceEntries)) {
    renderDeviceTable();
  }
}

async function reloadDefaultSetup() {
  try {
    const data = await callApi('/api/ui-config/reload', 'POST');
    clearSavedMainSetupState();
    await loadUiConfig({ preferSavedState: false });
    await loadMonitorModules();
    await refreshWholeUi();
    const path = (data && data.config_path) ? ` (${data.config_path})` : '';
    setMessage(`default setup reloaded${path}`, true);
  } catch (e) {
    setMessage(`failed to reload default setup: ${e.message}`, false);
  }
}

function selectedFrontendSchema() {
  const id = frontendSelect.value;
  return frontendCatalog.find((x) => x.id === id) || null;
}

function renderDeviceFields() {
  const schema = selectedFrontendSchema();
  deviceFields.innerHTML = '';
  if (!schema) return;
  (schema.fields || []).forEach((f) => {
    const row = document.createElement('div');
    row.className = 'row';
    const label = f.description ? `${f.name} (${f.description})` : f.name;
    const min = (f.min !== undefined) ? ` min="${f.min}"` : '';
    const max = (f.max !== undefined) ? ` max="${f.max}"` : '';
    const type = (f.type === 'integer') ? 'number' : 'text';
    row.innerHTML = `<label for="field_${f.name}">${label}</label><input id="field_${f.name}" data-field="${f.name}" type="${type}"${min}${max} />`;
    deviceFields.appendChild(row);
  });
}

function renderDeviceTable() {
  devicesBody.innerHTML = '';
  deviceEntries.forEach((d, idx) => {
    const tr = document.createElement('tr');
    const params = Object.keys(d)
      .filter((k) => k !== 'frontend')
      .map((k) => `${k}=${d[k]}`)
      .join(', ');
    tr.innerHTML = `<td>${d.frontend}</td><td>${params}</td><td><button data-del="${idx}">Remove</button></td>`;
    devicesBody.appendChild(tr);
  });
  devicesBody.querySelectorAll('button[data-del]').forEach((btn) => {
    btn.addEventListener('click', () => {
      const idx = Number(btn.getAttribute('data-del'));
      deviceEntries.splice(idx, 1);
      saveDevicesState();
      renderDeviceTable();
    });
  });
}

function collectDeviceFromForm() {
  const schema = selectedFrontendSchema();
  if (!schema) throw new Error('No frontend selected');
  const entry = { frontend: schema.id };
  (schema.fields || []).forEach((f) => {
    const input = document.getElementById(`field_${f.name}`);
    if (!input) return;
    const raw = String(input.value || '').trim();
    if (f.required && raw === '') {
      throw new Error(`device field '${f.name}' is required`);
    }
    if (raw !== '') {
      entry[f.name] = (f.type === 'integer') ? Number(raw) : raw;
    }
  });
  return entry;
}

async function loadFrontends() {
  const data = await callApi('/api/frontends');
  frontendCatalog = data.frontends || [];
  frontendSelect.innerHTML = '';
  frontendCatalog.forEach((f) => {
    const opt = document.createElement('option');
    opt.value = f.id;
    opt.textContent = `${f.id} ${f.spec_format ? '(' + f.spec_format + ')' : ''}`;
    frontendSelect.appendChild(opt);
  });
  renderDeviceFields();
}

async function loadMonitorModules() {
  const data = await callApi('/api/monitor/modules');
  monitorCatalog = {
    decoders: Array.isArray(data.decoders) ? data.decoders : [],
    analyses: Array.isArray(data.analyses) ? data.analyses : [],
  };
  renderMonitorModules();
}

function buildStartPayload() {
  const eventsInput = document.getElementById('events_per_file');
  const startupTimeoutInput = document.getElementById('startup_connect_timeout_sec');
  const reconnectFailureTimeoutInput = document.getElementById('reconnect_failure_timeout_sec');
  const partialRunInput = document.getElementById('allow_partial_run_on_runtime_disconnect');
  const commentInput = document.getElementById('comment');
  const eventsRaw = eventsInput ? eventsInput.value.trim() : '';
  const startupTimeoutRaw = startupTimeoutInput ? startupTimeoutInput.value.trim() : '';
  const reconnectFailureTimeoutRaw = reconnectFailureTimeoutInput ? reconnectFailureTimeoutInput.value.trim() : '';
  const payload = {
    devices: deviceEntries,
    comment: commentInput ? (commentInput.value.trim() || null) : null,
  };
  if (eventsRaw !== '') payload.events_per_file = Number(eventsRaw);
  if (startupTimeoutRaw !== '') payload.startup_connect_timeout_sec = Number(startupTimeoutRaw);
  if (reconnectFailureTimeoutRaw !== '') payload.reconnect_failure_timeout_sec = Number(reconnectFailureTimeoutRaw);
  if (partialRunInput) payload.allow_partial_run_on_runtime_disconnect = !!partialRunInput.checked;
  return payload;
}

function buildMonitorStartPayload() {
  const snapshotIntervalInput = document.getElementById('monitor_snapshot_interval_sec');
  const snapshotIntervalRaw = snapshotIntervalInput ? String(snapshotIntervalInput.value || '').trim() : '';
  if (snapshotIntervalRaw !== '') {
    monitorSnapshotIntervalSec = Math.max(0.1, Number(snapshotIntervalRaw));
    saveMonitorState();
  }
  return {
    decoder: selectedMonitorDecoder || null,
    analyses: Array.from(selectedMonitorAnalyses.values()),
    snapshot_interval_sec: snapshotIntervalRaw === '' ? null : Number(snapshotIntervalRaw),
  };
}

async function refreshRunLog() {
  try {
    const limit = Number(runLogLimit || uiConfig.main_run_log_limit || 50);
    const data = await callApi(`/api/run-log?limit=${limit}&offset=0`);
    runLogBody.innerHTML = '';
    (data.rows || []).forEach((r) => {
      const tr = document.createElement('tr');
      const statusText = String(r.status || '');
      const statusLower = statusText.toLowerCase();
      let statusClass = '';
      if (statusLower.startsWith('completed with ')) {
        statusClass = 'runlog-status-warn';
      } else if (statusLower === 'completed' || statusLower.startsWith('completed ')) {
        statusClass = 'runlog-status-ok';
      } else if (statusLower.startsWith('exit with ')) {
        statusClass = 'runlog-status-error';
      } else if (statusLower === 'error' || statusLower.includes('fail')) {
        statusClass = 'runlog-status-error';
      } else if (statusLower === 'warn' || statusLower.includes('warn')) {
        statusClass = 'runlog-status-warn';
      } else if (statusLower === 'info' || statusLower.includes('info')) {
        statusClass = 'runlog-status-info';
      }
      tr.innerHTML = `<td>${r.run}</td><td>${r.subrun}</td><td>${r.nevents}</td><td>${r.start_time}</td><td>${r.end_time}</td><td class="${statusClass}">${statusText}</td><td>${r.comment || ''}</td>`;
      runLogBody.appendChild(tr);
    });
  } catch (e) {
    setMessage(`run log error: ${e.message}`, false);
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

async function refreshStatus() {
  if (statusPollInFlight) {
    return;
  }
  statusPollInFlight = true;
  try {
    const data = await callApi('/api/status', 'GET', null, { timeoutMs: STATUS_API_TIMEOUT_MS });
    setDaqConnection(true);
    await renderStatus(data);
    clearConnectionErrorMessageIfAny();
  } catch (e) {
    setDaqConnection(false, e.message);
    setConnectionErrorMessage(`daqd communication error: ${e.message}`);
  } finally {
    statusPollInFlight = false;
  }
}

async function renderStatus(data) {
  const healthy = Number(data.healthy || 0) === 1;
  const running = Number(data.running || 0) === 1;
  const state = String(data.state || '-');
  daqState = state;
  setActionButtonsByState(state, running);
  const run = (data.Run !== undefined) ? Number(data.Run) : 0;
  const subrun = (data.subrun !== undefined) ? Number(data.subrun) : 0;
  const total = (data.events_total !== undefined) ? Number(data.events_total) : 0;
  const uptime = (data.uptime_sec !== undefined) ? Number(data.uptime_sec) : 0;

  healthLamp.className = `lamp ${healthy ? 'ok' : 'bad'}`;
  healthText.textContent = healthy ? 'Healthy' : 'Error';
  if (state === 'running') {
    stateLamp.className = 'lamp ok';
  } else if (state === 'paused' || state === 'pausing' || state === 'stopping') {
    stateLamp.className = 'lamp warn';
  } else if (state === 'error') {
    stateLamp.className = 'lamp bad';
  } else {
    stateLamp.className = 'lamp';
  }
  statusState.textContent = `${state}`;
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
  statusRun.textContent = `${displayRun}`;
  statusSubrun.textContent = `${displaySubrun}`;
  const displayTotal = running ? total : 0;
  statusEventsTotal.textContent = `${displayTotal}`;
  statusUptime.textContent = `${uptime}s`;
  statusError.textContent = data.error ? String(data.error) : '';
  if (data.error) {
    if (msgSource !== 'command') {
      msgSource = 'status';
      setMessage(String(data.error), false);
    }
  } else if (msgSource === 'status') {
    setMessage('', true);
  }

  const fromStatusPerFile = Number(data.events_per_file || 0);
  const fromInputPerFile = Number(document.getElementById('events_per_file').value || 0);
  const eventsPerFile = fromStatusPerFile > 0 ? fromStatusPerFile : (fromInputPerFile > 0 ? fromInputPerFile : 0);
  const fromStatusInFile = Number(data.events_in_file || 0);
  let inFile = 0;
  if (running && fromStatusInFile > 0) {
    inFile = fromStatusInFile;
  } else if (eventsPerFile > 0 && running) {
    inFile = total % eventsPerFile;
    if (inFile === 0 && total > 0) {
      inFile = eventsPerFile;
    }
  }
  const ratio = (eventsPerFile > 0) ? Math.max(0, Math.min(1, inFile / eventsPerFile)) : 0;
  statusFileProgressText.textContent = `${inFile} / ${eventsPerFile}`;
  statusFileProgressFill.style.width = `${Math.round(ratio * 100)}%`;
}

async function runCommand(path, method = 'POST', payload = null) {
  if (!daqConnected) {
    setMessage('daqd is not reachable. Start daqd first.', false);
    return;
  }
  if (path === '/api/shutdown') {
    setDaqConnection(false, 'shutting down daqd...');
  }
  try {
    const data = await callApi(path, method, payload);
    setMessage(data.result || 'ok', true);
    if (path === '/api/shutdown') {
      setDaqConnection(false, 'daqd shutdown requested');
    }
    cachedNextRun = null;
    await refreshWholeUi();
  } catch (e) {
    msgSource = 'command';
    setMessage(e.message, false);
  }
}

async function startDaqdFromWeb() {
  try {
    const data = await callApi('/api/daqd/start', 'POST');
    setMessage(data.result || 'daqd started', true);
    await refreshWholeUi();
  } catch (e) {
    setMessage(`failed to start daqd: ${e.message}`, false);
  }
}

async function startDatamonFromWeb() {
  try {
    const payload = buildMonitorStartPayload();
    const data = await callApi('/api/monitor/start', 'POST', payload);
    setMessage(data.result || 'datamon started', true);
    await refreshWholeUi();
  } catch (e) {
    setMessage(`failed to start monitor: ${e.message}`, false);
  }
}

async function stopDatamonFromWeb() {
  try {
    const data = await callApi('/api/monitor/shutdown', 'POST');
    setMessage(data.result || 'datamon stopped', true);
    await refreshWholeUi();
  } catch (e) {
    setMessage(`failed to stop monitor: ${e.message}`, false);
  }
}

document.getElementById('btn_start_daqd').addEventListener('click', startDaqdFromWeb);
document.getElementById('btn_start').addEventListener('click', async () => {
  saveMainFormState();
  await runCommand('/api/start', 'POST', buildStartPayload());
});
document.getElementById('btn_pause').addEventListener('click', async () => {
  await runCommand('/api/pause');
});
document.getElementById('btn_resume').addEventListener('click', async () => {
  await runCommand('/api/resume');
});
document.getElementById('btn_stop').addEventListener('click', async () => {
  await runCommand('/api/stop');
});
document.getElementById('btn_shutdown').addEventListener('click', async () => {
  await runCommand('/api/shutdown');
});
document.getElementById('btn_start_datamon').addEventListener('click', startDatamonFromWeb);
document.getElementById('btn_shutdown_datamon').addEventListener('click', stopDatamonFromWeb);
document.getElementById('btn_refresh_status').addEventListener('click', refreshStatus);
document.getElementById('btn_reload_default_setup').addEventListener('click', reloadDefaultSetup);
document.getElementById('btn_refresh_runlog').addEventListener('click', refreshRunLog);
document.getElementById('btn_refresh_daqd_log').addEventListener('click', refreshDaqdLog);
document.getElementById('btn_refresh_datamon_log').addEventListener('click', refreshDatamonLog);
document.getElementById('btn_add_device').addEventListener('click', () => {
  try {
    const entry = collectDeviceFromForm();
    deviceEntries.push(entry);
    saveDevicesState();
    renderDeviceTable();
    setMessage('device added', true);
  } catch (e) {
    setMessage(e.message || String(e), false);
  }
});
frontendSelect.addEventListener('change', renderDeviceFields);
bindMainFormStateSave();
bindMonitorStateSave();

Promise.all([loadUiConfig(), loadFrontends()])
  .then(async () => {
    await setSelectedAnalysis(null);
    await loadMonitorModules();
    await refreshStatus();
    await refreshRunLog();
    await refreshDaqdLog();
    await refreshDatamonStatus();
    await refreshDatamonLog();
  })
  .catch((e) => {
    setDaqConnection(false, e.message);
    setMessage(`initialisation error: ${e.message}`, false);
  });

setInterval(refreshStatus, 1000);
setInterval(refreshDaqdLog, 2000);
setInterval(refreshDatamonStatus, 1000);
setInterval(refreshDatamonLog, 2000);
