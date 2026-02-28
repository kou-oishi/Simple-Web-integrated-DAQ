(function () {
  const root = document.querySelector('.app-layout');
  const logView = document.getElementById('full_log_view');
  const logMeta = document.getElementById('log_meta');
  const logTitle = document.getElementById('log_title');
  const pageTitle = document.getElementById('page_title');
  const refreshBtn = document.getElementById('btn_refresh_log');
  const grepInput = document.getElementById('log_grep');
  const excludeInput = document.getElementById('log_exclude');
  const regexInput = document.getElementById('log_regex');
  const ignoreCaseInput = document.getElementById('log_ignore_case');
  const levelErrorInput = document.getElementById('log_level_error');
  const levelWarnInput = document.getElementById('log_level_warn');
  const levelInfoInput = document.getElementById('log_level_info');
  const clearFilterBtn = document.getElementById('btn_clear_log_filters');
  const quickTokens = document.getElementById('log_quick_tokens');

  const apiPath = String((root && root.dataset && root.dataset.logApi) || '').trim();
  const title = String((root && root.dataset && root.dataset.logTitle) || 'Log').trim();
  const emptyText = String((root && root.dataset && root.dataset.logEmpty) || '(no log)').trim();
  const logKind = String((root && root.dataset && root.dataset.logKind) || '').trim();

  let pageSize = 300;
  let totalLines = 0;
  let loadedLines = [];
  let loadingOlder = false;
  let hasMoreOlder = true;
  let refreshTimer = null;

  function isNearBottom(el, thresholdPx = 8) {
    const remain = el.scrollHeight - el.scrollTop - el.clientHeight;
    return remain <= thresholdPx;
  }

  function escapeHtml(text) {
    return String(text)
      .replaceAll('&', '&amp;')
      .replaceAll('<', '&lt;')
      .replaceAll('>', '&gt;');
  }

  function renderLogLine(line) {
    const escaped = escapeHtml(line);
    if (line.includes('[ERROR]')) return `<span class="log-line log-error">${escaped}</span>`;
    if (line.includes('[WARN]')) return `<span class="log-line log-warn">${escaped}</span>`;
    if (line.includes('[INFO]')) return `<span class="log-line log-info">${escaped}</span>`;
    return `<span class="log-line">${escaped}</span>`;
  }

  async function callApi(path) {
    const res = await fetch(path);
    let data = null;
    try {
      data = await res.json();
    } catch (_) {}
    if (!res.ok) {
      throw new Error((data && data.detail) ? JSON.stringify(data.detail) : `HTTP ${res.status}`);
    }
    return data;
  }

  function getSelectedLevels() {
    const levels = [];
    if (levelErrorInput && levelErrorInput.checked) levels.push('ERROR');
    if (levelWarnInput && levelWarnInput.checked) levels.push('WARN');
    if (levelInfoInput && levelInfoInput.checked) levels.push('INFO');
    return levels;
  }

  function currentFilter() {
    return {
      grep: String((grepInput && grepInput.value) || '').trim(),
      exclude: String((excludeInput && excludeInput.value) || '').trim(),
      regex: !!(regexInput && regexInput.checked),
      ignoreCase: !(ignoreCaseInput && !ignoreCaseInput.checked),
      levels: getSelectedLevels(),
    };
  }

  function apiWithParams(limit, offset) {
    const f = currentFilter();
    const q = new URLSearchParams();
    q.set('limit', String(limit));
    q.set('offset', String(Math.max(0, offset)));
    q.set('regex', f.regex ? 'true' : 'false');
    q.set('ignore_case', f.ignoreCase ? 'true' : 'false');
    if (f.grep !== '') q.set('grep', f.grep);
    if (f.exclude !== '') q.set('exclude', f.exclude);
    if (f.levels.length > 0) q.set('levels', f.levels.join(','));
    return `${apiPath}?${q.toString()}`;
  }

  function updateMeta(pathText) {
    if (!logMeta) return;
    const loaded = loadedLines.length;
    const f = currentFilter();
    const filterSummary = [
      f.grep ? `grep=${f.grep}` : '',
      f.exclude ? `exclude=${f.exclude}` : '',
      f.levels.length > 0 ? `levels=${f.levels.join('|')}` : '',
      f.regex ? 'regex' : '',
      f.ignoreCase ? 'ignore-case' : 'case-sensitive',
    ].filter((x) => x !== '').join(', ');
    const suffix = filterSummary ? ` | ${filterSummary}` : '';
    logMeta.textContent = `${pathText || '-'} (${loaded}/${totalLines} lines${suffix})`;
  }

  function renderLoaded() {
    if (!logView) return;
    if (loadedLines.length === 0) {
      logView.textContent = emptyText;
      return;
    }
    logView.innerHTML = loadedLines.map((line) => renderLogLine(String(line))).join('');
  }

  async function loadLatest() {
    if (!apiPath || !logView) return;
    const follow = isNearBottom(logView);
    const data = await callApi(apiWithParams(pageSize, 0));
    const lines = Array.isArray(data.lines) ? data.lines.map((x) => String(x)) : [];
    totalLines = Number(data.total || lines.length || 0);
    loadedLines = lines;
    hasMoreOlder = loadedLines.length < totalLines;
    renderLoaded();
    updateMeta(String(data.path || '-'));
    if (follow || loadedLines.length <= pageSize) {
      logView.scrollTop = logView.scrollHeight;
    }
  }

  async function loadOlderIfNeeded() {
    if (!apiPath || !logView || loadingOlder || !hasMoreOlder) return;
    if (logView.scrollTop > 24) return;
    loadingOlder = true;
    try {
      const offset = loadedLines.length;
      const prevHeight = logView.scrollHeight;
      const prevTop = logView.scrollTop;
      const data = await callApi(apiWithParams(pageSize, offset));
      const older = Array.isArray(data.lines) ? data.lines.map((x) => String(x)) : [];
      totalLines = Number(data.total || totalLines || 0);
      if (older.length === 0) {
        hasMoreOlder = false;
        updateMeta(String(data.path || '-'));
        return;
      }
      loadedLines = older.concat(loadedLines);
      hasMoreOlder = loadedLines.length < totalLines;
      renderLoaded();
      updateMeta(String(data.path || '-'));
      const newHeight = logView.scrollHeight;
      logView.scrollTop = Math.max(0, newHeight - prevHeight + prevTop);
    } catch (e) {
      if (logView) {
        logView.textContent = `log error: ${e.message}`;
      }
    } finally {
      loadingOlder = false;
    }
  }

  async function loadUiConfigLimit() {
    try {
      const cfg = await callApi('/api/ui-config');
      if (apiPath.includes('/api/daqd/log')) {
        pageSize = Number(cfg.daqd_log_limit || pageSize);
      } else if (apiPath.includes('/api/monitor/log')) {
        pageSize = Number(cfg.datamon_log_limit || pageSize);
      }
    } catch (_) {
      // keep default page size
    }
    if (!Number.isFinite(pageSize)) pageSize = 300;
    pageSize = Math.max(1, Math.min(500, Math.floor(pageSize)));
  }

  async function refreshLog() {
    try {
      await loadLatest();
    } catch (e) {
      if (logView) {
        logView.textContent = `log error: ${e.message}`;
      }
    }
  }

  function scheduleRefresh(delayMs = 250) {
    if (refreshTimer !== null) {
      window.clearTimeout(refreshTimer);
    }
    refreshTimer = window.setTimeout(() => {
      refreshTimer = null;
      void refreshLog();
    }, delayMs);
  }

  function appendQuickToken(token) {
    if (!grepInput) return;
    const cur = String(grepInput.value || '').trim();
    if (cur === '') {
      grepInput.value = token;
    } else if (regexInput && regexInput.checked) {
      grepInput.value = `${cur}|${token}`;
    } else {
      grepInput.value = `${cur} ${token}`;
    }
    scheduleRefresh(0);
  }

  function renderQuickTokens() {
    if (!quickTokens) return;
    quickTokens.innerHTML = '';
    const daqSuggestions = ['start', 'stop', 'pause', 'resume', 'status', 'connect', 'disconnect'];
    const analysisSuggestions = ['decoder', 'analysis', 'snapshot', 'selected', 'canvas', 'module', 'screen'];
    const suggestions = (logKind === 'analysis') ? analysisSuggestions : daqSuggestions;
    suggestions.forEach((token) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.textContent = `+${token}`;
      btn.addEventListener('click', () => appendQuickToken(token));
      quickTokens.appendChild(btn);
    });
  }

  if (logTitle) {
    logTitle.textContent = title;
  }
  if (pageTitle) {
    pageTitle.textContent = `${title} | DAQ Control Panel`;
  }
  document.title = title;

  if (refreshBtn) {
    refreshBtn.addEventListener('click', () => {
      void refreshLog();
    });
  }
  if (clearFilterBtn) {
    clearFilterBtn.addEventListener('click', () => {
      if (grepInput) grepInput.value = '';
      if (excludeInput) excludeInput.value = '';
      if (regexInput) regexInput.checked = false;
      if (ignoreCaseInput) ignoreCaseInput.checked = true;
      if (levelErrorInput) levelErrorInput.checked = false;
      if (levelWarnInput) levelWarnInput.checked = false;
      if (levelInfoInput) levelInfoInput.checked = false;
      void refreshLog();
    });
  }

  [grepInput, excludeInput].forEach((el) => {
    if (!el) return;
    el.addEventListener('input', () => scheduleRefresh(250));
  });
  [regexInput, ignoreCaseInput, levelErrorInput, levelWarnInput, levelInfoInput].forEach((el) => {
    if (!el) return;
    el.addEventListener('change', () => {
      void refreshLog();
    });
  });

  if (logView) {
    logView.addEventListener('scroll', () => {
      void loadOlderIfNeeded();
    });
  }

  renderQuickTokens();
  loadUiConfigLimit()
    .then(() => refreshLog())
    .catch(() => refreshLog());

  setInterval(() => {
    if (!logView || loadingOlder) return;
    if (isNearBottom(logView)) {
      void refreshLog();
    }
  }, 2000);
})();
