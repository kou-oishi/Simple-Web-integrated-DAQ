(function () {
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
    const mount = document.getElementById('global_status_mount');
    if (!mount) return false;
    if (document.getElementById('global_status_panel')) return true;
    mount.insertAdjacentHTML('beforebegin', statusPanelHtml());
    mount.remove();
    return true;
  }

  window.SimpleDaqStatusPanel = {
    mountGlobalStatusPanel,
  };

  mountGlobalStatusPanel();
})();
