let msg = document.getElementById('msg');
let stateLamp = document.getElementById('state_lamp');
let statusState = document.getElementById('status_state');
let statusRun = document.getElementById('status_run');
let statusSubrun = document.getElementById('status_subrun');
let statusEventsTotal = document.getElementById('status_events_total');
let statusUptime = document.getElementById('status_uptime');
let statusFileProgressText = document.getElementById('status_file_progress_text');
let statusFileProgressFill = document.getElementById('status_file_progress_fill');

const pageTitle = document.getElementById('page_title');
const analysisTitle = document.getElementById('analysis_title');
const analysisMeta = document.getElementById('analysis_meta');
const analysisError = document.getElementById('analysis_error');
const analysisImages = document.getElementById('analysis_images');
const imageSizeRange = document.getElementById('image_size_range');
const imageSizeValue = document.getElementById('image_size_value');
const imageOverlay = document.getElementById('image_overlay');
const overlayImage = document.getElementById('overlay_image');

const MAIN_FORM_STATE_KEY = 'simpledaq_main_form_state_v1';
const ANALYSIS_IMAGE_SIZE_KEY = 'simpledaq_analysis_image_size_v1';

let eventsPerFileDefault = 0;
let cachedNextRun = null;
let cachedNextRunAtMs = 0;
let currentImageUrlsByName = new Map();
let overlayImageName = '';
let imageCardByName = new Map();

function selectedModule() {
  const params = new URLSearchParams(window.location.search || '');
  return String(params.get('module') || '').trim();
}

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
  return Boolean(msg && stateLamp && statusState && statusRun && statusSubrun && statusEventsTotal && statusUptime &&
                 statusFileProgressText && statusFileProgressFill);
}

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

function loadImageSize() {
  try {
    const raw = window.localStorage.getItem(ANALYSIS_IMAGE_SIZE_KEY);
    if (!raw) return 520;
    const value = Number(raw);
    if (!Number.isFinite(value)) return 520;
    return Math.max(220, Math.min(1400, Math.round(value)));
  } catch (_) {
    return 520;
  }
}

function saveImageSize(px) {
  try {
    window.localStorage.setItem(ANALYSIS_IMAGE_SIZE_KEY, String(px));
  } catch (_) {
    // ignore
  }
}

function applyImageSize(px) {
  if (!analysisImages) return;
  analysisImages.style.setProperty('--analysis-image-width', `${px}px`);
  if (imageSizeValue) {
    imageSizeValue.textContent = `${px} px`;
  }
}

function openOverlay(imageName) {
  const url = currentImageUrlsByName.get(imageName);
  if (!url || !imageOverlay || !overlayImage) return;
  overlayImageName = imageName;
  overlayImage.src = url;
  imageOverlay.classList.remove('hidden');
}

function closeOverlay() {
  if (!imageOverlay || !overlayImage) return;
  overlayImageName = '';
  imageOverlay.classList.add('hidden');
  overlayImage.removeAttribute('src');
}

function toggleOverlay(imageName) {
  if (!imageOverlay) return;
  const isOpen = !imageOverlay.classList.contains('hidden');
  if (isOpen && overlayImageName === imageName) {
    closeOverlay();
    return;
  }
  openOverlay(imageName);
}

function refreshOverlayIfOpen() {
  if (!imageOverlay || !overlayImage || imageOverlay.classList.contains('hidden')) return;
  const url = currentImageUrlsByName.get(overlayImageName);
  if (url) {
    overlayImage.src = url;
  } else {
    closeOverlay();
  }
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
  if (!ensureStatusPanelElements()) return;
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
    const saved = loadMainFormState();
    const fromSavedPerFile = Number(saved?.events_per_file || 0);
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

function ensureImageCard(imageName) {
  const existing = imageCardByName.get(imageName);
  if (existing) {
    return existing;
  }
  const card = document.createElement('div');
  card.className = 'analysis-card';
  card.setAttribute('data-image-name', imageName);

  const title = document.createElement('div');
  title.className = 'analysis-card-title';
  title.textContent = imageName;

  const img = document.createElement('img');
  img.className = 'analysis-image';
  img.alt = imageName;
  img.addEventListener('click', () => {
    toggleOverlay(imageName);
  });

  card.appendChild(title);
  card.appendChild(img);
  imageCardByName.set(imageName, card);
  return card;
}

function syncImages(items) {
  const nextNames = new Set();
  const nextUrls = new Map();
  items.forEach((item) => {
    const imageName = String(item.name || '');
    const imageUrl = String(item.url || '');
    if (!imageName || !imageUrl) return;
    nextNames.add(imageName);
    nextUrls.set(imageName, imageUrl);
  });

  Array.from(imageCardByName.keys()).forEach((name) => {
    if (!nextNames.has(name)) {
      const card = imageCardByName.get(name);
      if (card && card.parentElement === analysisImages) {
        card.remove();
      }
      imageCardByName.delete(name);
      currentImageUrlsByName.delete(name);
    }
  });

  items.forEach((item) => {
    const imageName = String(item.name || '');
    const imageUrl = String(item.url || '');
    if (!imageName || !imageUrl) return;

    const card = ensureImageCard(imageName);
    const img = card.querySelector('img.analysis-image');
    if (img && img.getAttribute('src') !== imageUrl) {
      img.setAttribute('src', imageUrl);
    }

    if (card.parentElement !== analysisImages) {
      analysisImages.appendChild(card);
    }
    currentImageUrlsByName.set(imageName, imageUrl);
  });
  refreshOverlayIfOpen();
}

async function refreshImages() {
  const moduleName = selectedModule();
  if (!moduleName) {
    analysisError.textContent = 'module is not specified';
    analysisImages.innerHTML = '';
    return;
  }
  try {
    const data = await callApi('/api/monitor/screens');
    const analyses = Array.isArray(data.analyses) ? data.analyses : [];
    const target = analyses.find((x) => String(x.name || '') === moduleName);
    const moduleTitle = target ? (String(target.title || moduleName).trim() || moduleName) : moduleName;
    const images = target && Array.isArray(target.images) ? target.images : [];
    analysisTitle.textContent = `Analysis: ${moduleTitle}`;
    analysisMeta.textContent = `module: ${moduleTitle} (${moduleName}) | images: ${images.length}`;
    analysisError.textContent = '';
    syncImages(images);
  } catch (e) {
    analysisError.textContent = `screen update error: ${e.message}`;
  }
}

async function loadUiConfig() {
  try {
    const cfg = await callApi('/api/ui-config');
    const title = String(cfg.title || 'DAQ Control');
    const moduleName = selectedModule();
    document.title = moduleName ? `${title} - ${moduleName}` : `${title} - Analysis`;
    pageTitle.textContent = title;
    analysisTitle.textContent = moduleName ? `Analysis: ${moduleName}` : 'Analysis';
    eventsPerFileDefault = Math.max(0, Number(cfg.events_per_file || 0));
  } catch (_) {
    // ignore
  }
}

function bindImageSizeControl() {
  if (!imageSizeRange) return;
  const initialSize = loadImageSize();
  imageSizeRange.value = String(initialSize);
  applyImageSize(initialSize);
  imageSizeRange.addEventListener('input', () => {
    const px = Math.max(220, Math.min(1400, Number(imageSizeRange.value || 520)));
    applyImageSize(px);
    saveImageSize(px);
  });
}

function bindOverlayClose() {
  if (!imageOverlay) return;
  imageOverlay.addEventListener('click', () => {
    closeOverlay();
  });
}

ensureStatusPanelElements();
bindImageSizeControl();
bindOverlayClose();
loadUiConfig().then(() => setSelectedAnalysis(selectedModule())).then(refreshImages);
refreshStatus();
setInterval(refreshStatus, 1000);
setInterval(refreshImages, 1000);
