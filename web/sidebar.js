(function () {
  let lastSignature = '';

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

  function activeModuleName() {
    const path = window.location.pathname || '';
    if (path !== '/analysis') {
      return '';
    }
    const params = new URLSearchParams(window.location.search || '');
    return String(params.get('module') || '');
  }

  function createLink(moduleName) {
    const a = document.createElement('a');
    const encoded = encodeURIComponent(moduleName);
    a.href = `/analysis?module=${encoded}`;
    a.className = 'menu-btn';
    if (activeModuleName() === moduleName) {
      a.classList.add('active');
    }
    a.innerHTML = `<span class="menu-main">${moduleName}</span><span class="menu-sub">Analysis screen</span>`;
    return a;
  }

  function buildSignature(running, active) {
    const names = active
      .map((item) => `${String((item && item.name) || '').trim()}|${String((item && item.title) || '').trim()}`)
      .filter((x) => x !== '')
      .sort();
    return JSON.stringify({ running: Boolean(running), names, activePath: window.location.pathname || '', activeModule: activeModuleName() });
  }

  async function renderAnalysisMenu() {
    const nav = document.querySelector('.sidebar-nav');
    if (!nav) return;
    const markerId = 'analysis_menu_group';
    let group = document.getElementById(markerId);
    if (!group) {
      group = document.createElement('div');
      group.id = markerId;
      group.style.display = 'contents';
      nav.appendChild(group);
    }
    try {
      const status = await callApi('/api/monitor/status');
      const running = Boolean(status.running);
      const active = Array.isArray(status.active_analyses) ? status.active_analyses : [];
      const signature = buildSignature(running, active);
      if (signature === lastSignature) {
        return;
      }
      lastSignature = signature;
      group.innerHTML = '';
      if (!running) {
        return;
      }
      active.forEach((item) => {
        const name = String((item && item.name) || '').trim();
        if (!name) return;
        const title = String((item && item.title) || name).trim() || name;
        const link = createLink(name);
        const main = link.querySelector('.menu-main');
        if (main) {
          main.textContent = title;
        }
        group.appendChild(link);
      });
    } catch (_) {
      // keep menu without analysis entries on error
    }
  }

  window.SimpleDaqSidebar = {
    refreshAnalysisMenu: renderAnalysisMenu,
  };

  renderAnalysisMenu();
  setInterval(renderAnalysisMenu, 2500);
  window.addEventListener('focus', renderAnalysisMenu);
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) {
      renderAnalysisMenu();
    }
  });
})();
