const pageTitle = document.getElementById('page_title');
const analysisTitle = document.getElementById('analysis_title');
const analysisMeta = document.getElementById('analysis_meta');
const analysisError = document.getElementById('analysis_error');
const analysisImages = document.getElementById('analysis_images');
const imageSizeRange = document.getElementById('image_size_range');
const imageSizeValue = document.getElementById('image_size_value');
const imageOverlay = document.getElementById('image_overlay');
const overlayImage = document.getElementById('overlay_image');

const ANALYSIS_IMAGE_SIZE_KEY = 'simpledaq_analysis_image_size_v1';

let eventsPerFileDefault = 0;
let currentImageUrlsByName = new Map();
let overlayImageName = '';
let imageCardByName = new Map();

function selectedModule() {
  const params = new URLSearchParams(window.location.search || '');
  return String(params.get('module') || '').trim();
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

async function refreshStatus() {
  if (!window.SimpleDaqStatusPanel || typeof window.SimpleDaqStatusPanel.refreshGlobalStatus !== 'function') return;
  await window.SimpleDaqStatusPanel.refreshGlobalStatus({
    fetchJson: callApi,
    eventsPerFileDefault,
    setMessage,
  });
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

if (window.SimpleDaqStatusPanel && typeof window.SimpleDaqStatusPanel.mountGlobalStatusPanel === 'function') {
  window.SimpleDaqStatusPanel.mountGlobalStatusPanel();
}
bindImageSizeControl();
bindOverlayClose();
loadUiConfig().then(() => setSelectedAnalysis(selectedModule())).then(refreshImages);
refreshStatus();
setInterval(refreshStatus, 1000);
setInterval(refreshImages, 1000);
