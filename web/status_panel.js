(function () {
  let cachedNextRun = null;
  let cachedNextRunAtMs = 0;

  function statusPanelHtml() {
    return `
      <section class="panel-status-sticky" id="global_status_panel">
        <div class="status-top">
          <div class="lamp-wrap">
            <span id="state_lamp" class="lamp"></span>
            <span id="status_state">-</span>
          </div>
          <div id="msg" class="msg status-msg"></div>
        </div>
        <div class="status-line">
          <span class="status-item">Run<strong id="status_run">-</strong></span>
          <span class="status-item">Subrun<strong id="status_subrun">-</strong></span>
          <span class="status-item">Events Total<strong id="status_events_total">0</strong></span>
          <span class="status-item">DAQ time<strong id="status_uptime">0s</strong></span>
        </div>
        <div class="progress-wrap">
          <div class="progress-label">
            <span>Events in file</span>
            <span id="status_file_progress_text">0 / 0</span>
          </div>
          <div class="progress-bar">
            <div id="status_file_progress_fill" class="progress-fill"></div>
          </div>
        </div>
      </section>
    `;
  }

  function mountGlobalStatusPanel() {
    if (document.getElementById('global_status_panel')) return true;
    const mount = document.getElementById('global_status_mount');
    if (!mount) return false;
    mount.insertAdjacentHTML('beforebegin', statusPanelHtml());
    mount.remove();
    return true;
  }

  function ensureElements() {
    if (!mountGlobalStatusPanel()) return null;
    const msg = document.getElementById('msg');
    const stateLamp = document.getElementById('state_lamp');
    const statusState = document.getElementById('status_state');
    const statusRun = document.getElementById('status_run');
    const statusSubrun = document.getElementById('status_subrun');
    const statusEventsTotal = document.getElementById('status_events_total');
    const statusUptime = document.getElementById('status_uptime');
    const statusFileProgressText = document.getElementById('status_file_progress_text');
    const statusFileProgressFill = document.getElementById('status_file_progress_fill');
    if (!msg || !stateLamp || !statusState || !statusRun || !statusSubrun || !statusEventsTotal || !statusUptime ||
        !statusFileProgressText || !statusFileProgressFill) {
      return null;
    }
    return {
      msg,
      stateLamp,
      statusState,
      statusRun,
      statusSubrun,
      statusEventsTotal,
      statusUptime,
      statusFileProgressText,
      statusFileProgressFill,
    };
  }

  function readMainFormStateEventsPerFile() {
    try {
      const raw = window.localStorage.getItem('simpledaq_main_form_state_v1');
      if (!raw) return 0;
      const parsed = JSON.parse(raw);
      const value = Number(parsed && parsed.events_per_file);
      return Number.isFinite(value) && value > 0 ? Math.floor(value) : 0;
    } catch (_) {
      return 0;
    }
  }

  async function fetchNextRun(fetchJson, force) {
    const now = Date.now();
    if (!force && cachedNextRun !== null && (now - cachedNextRunAtMs) < 5000) {
      return cachedNextRun;
    }
    const data = await fetchJson('/api/next-run');
    cachedNextRun = Number(data.next_run || 0);
    cachedNextRunAtMs = now;
    return cachedNextRun;
  }

  function setMessage(elements, text, ok) {
    if (!elements || !elements.msg) return;
    elements.msg.className = 'msg status-msg ' + (ok ? 'ok' : 'err');
    elements.msg.textContent = text || '';
  }

  async function refreshGlobalStatus(options) {
    const opts = options || {};
    const fetchJson = opts.fetchJson;
    const eventsPerFileDefault = Math.max(0, Number(opts.eventsPerFileDefault || 0));
    const onMessage = (typeof opts.setMessage === 'function') ? opts.setMessage : null;

    if (typeof fetchJson !== 'function') {
      return false;
    }
    const elements = ensureElements();
    if (!elements) {
      return false;
    }

    try {
      const data = await fetchJson('/api/status');
      const running = Number(data.running || 0) === 1;
      const state = String(data.state || '-');
      const run = (data.Run !== undefined) ? Number(data.Run) : 0;
      const subrun = (data.subrun !== undefined) ? Number(data.subrun) : 0;
      const total = (data.events_total !== undefined) ? Number(data.events_total) : 0;
      const uptime = (data.uptime_sec !== undefined) ? Number(data.uptime_sec) : 0;

      if (state === 'running') {
        elements.stateLamp.className = 'lamp ok';
      } else if (state === 'paused' || state === 'pausing' || state === 'stopping') {
        elements.stateLamp.className = 'lamp warn';
      } else if (state === 'error') {
        elements.stateLamp.className = 'lamp bad';
      } else {
        elements.stateLamp.className = 'lamp';
      }

      let displayRun = run;
      let displaySubrun = subrun;
      if (!running && state !== 'running') {
        try {
          displayRun = await fetchNextRun(fetchJson, false);
          displaySubrun = 0;
        } catch (_) {
          displayRun = run + 1;
          displaySubrun = 0;
        }
      }

      elements.statusState.textContent = state;
      elements.statusRun.textContent = String(displayRun);
      elements.statusSubrun.textContent = String(displaySubrun);
      elements.statusEventsTotal.textContent = String(running ? total : 0);
      elements.statusUptime.textContent = `${uptime}s`;

      const fromStatusPerFile = Number(data.events_per_file || 0);
      const fromSavedPerFile = readMainFormStateEventsPerFile();
      const perFile = fromStatusPerFile > 0 ? fromStatusPerFile : (fromSavedPerFile > 0 ? fromSavedPerFile : eventsPerFileDefault);
      const fromStatusInFile = Number(data.events_in_file || 0);
      let inFile = 0;
      if (running && fromStatusInFile > 0) {
        inFile = fromStatusInFile;
      } else if (perFile > 0 && running) {
        inFile = total % perFile;
        if (inFile === 0 && total > 0) inFile = perFile;
      }
      const ratio = perFile > 0 ? Math.max(0, Math.min(1, inFile / perFile)) : 0;
      elements.statusFileProgressText.textContent = `${inFile} / ${perFile}`;
      elements.statusFileProgressFill.style.width = `${Math.round(ratio * 100)}%`;
      if (onMessage) onMessage('', true);
      return true;
    } catch (e) {
      elements.stateLamp.className = 'lamp bad';
      elements.statusState.textContent = 'daqd unreachable';
      elements.statusRun.textContent = '-';
      elements.statusSubrun.textContent = '-';
      elements.statusEventsTotal.textContent = '0';
      elements.statusUptime.textContent = '0s';
      elements.statusFileProgressText.textContent = '0 / 0';
      elements.statusFileProgressFill.style.width = '0%';
      const text = `status error: ${(e && e.message) ? e.message : String(e)}`;
      if (onMessage) onMessage(text, false);
      else setMessage(elements, text, false);
      return false;
    }
  }

  window.SimpleDaqStatusPanel = {
    mountGlobalStatusPanel,
    ensureElements,
    refreshGlobalStatus,
  };

  mountGlobalStatusPanel();
})();
