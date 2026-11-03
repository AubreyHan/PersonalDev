/**
 * TrailMov 3D Studio - Core Application Logic
 * Integrates MapLibre GL 3D Terrain, Satellite Imagery, 3D Avatars,
 * Smooth Camera Choreography, and Real-Time Glassmorphic Telemetry HUD.
 */

// Global State
const state = {
  track: null,
  totalPoints: 0,
  duration: 36.0,          // total seconds
  currentTime: 0.0,        // current playback time in seconds
  isPlaying: false,
  aspect: '9-16',          // '9-16', '16-9', 'full'
  avatar: 'cyclist',       // 'cyclist', 'car', 'runner', 'hiker', 'dot'
  cameraMode: 'chase',     // 'chase', 'fixed', 'orbit', 'overview'
  cameraBearing: null,
  cameraCenter: null,
  exaggeration: 2.8,
  rafId: null,
  lastFrameTs: null,
  mapReady: false,
  map: null,
  avatarMarker: null,
  recordingCanvas: null,
  recordingCtx: null,
  isRecording: false,
  currentPointIdx: 0,
  currentProgress: 0.0,
  currentStage: 'intro'
};

// DOM References
const DOM = {
  frame: document.getElementById('viewport-frame'),
  trackHeader: document.getElementById('track-header-title'),
  aspectSelect: document.getElementById('aspect-select'),
  avatarSelect: document.getElementById('avatar-select'),
  cameraSelect: document.getElementById('camera-select'),
  durationSelect: document.getElementById('duration-select'),
  exaggerationSelect: document.getElementById('exaggeration-select'),
  btnPlay: document.getElementById('btn-play-toggle'),
  btnReplay: document.getElementById('btn-replay'),
  playIcon: document.getElementById('play-icon'),
  timeDisplay: document.getElementById('time-display'),
  timelineSlider: document.getElementById('timeline-slider'),
  
  // HUD Elements
  minimapCanvas: document.getElementById('minimap-canvas'),
  distanceVal: document.getElementById('hud-distance-val'),
  climbVal: document.getElementById('hud-climb-val'),
  descentVal: document.getElementById('hud-descent-val'),
  introCard: document.getElementById('hud-intro-card'),
  introTitle: document.getElementById('hud-intro-title'),
  introSub: document.getElementById('hud-intro-sub'),
  eleMax: document.getElementById('hud-ele-max'),
  eleMin: document.getElementById('hud-ele-min'),
  svgBg: document.getElementById('svg-profile-bg'),
  svgFill: document.getElementById('svg-profile-fill'),
  svgActive: document.getElementById('svg-profile-active'),
  curPin: document.getElementById('hud-cur-pin'),
  curBadgeTxt: document.getElementById('hud-cur-badge-txt'),

  // Export Modal
  modal: document.getElementById('export-modal'),
  btnExportDialog: document.getElementById('btn-export-dialog'),
  btnExportCancel: document.getElementById('btn-export-cancel'),
  btnExportStart: document.getElementById('btn-export-start'),
  btnSafeDelete: document.getElementById('btn-safe-delete'),
  modalCurAspect: document.getElementById('modal-cur-aspect'),
  progressWrap: document.getElementById('export-progress-wrap'),
  progressBar: document.getElementById('export-bar'),
  progressStatus: document.getElementById('export-status-txt'),
  progressPercent: document.getElementById('export-percent-txt')
};

// Pre-rendered Avatar Images for Canvas Compositing
const AVATAR_SVGS = {
  runner: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 48" width="40" height="48" fill="none">
    <ellipse cx="20" cy="44" rx="14" ry="3.5" fill="rgba(0,0,0,0.65)"/>
    <circle cx="28" cy="10" r="4.5" fill="#DFFF00"/>
    <path d="M25 17l-6 6-5-2.5-6 5" stroke="#ffffff" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="M19 23l3.5 7 7 2" stroke="#ffffff" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="M13 30l-4 9" stroke="#ffffff" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>
  </svg>`,
  hiker: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 48" width="40" height="48" fill="none">
    <ellipse cx="20" cy="44" rx="14" ry="3.5" fill="rgba(0,0,0,0.65)"/>
    <circle cx="20" cy="8" r="4.5" fill="#38bdf8"/>
    <path d="M15 15h10l2 10-8 2-4-2z" fill="#0284c7"/>
    <path d="M17 25v12m6-12v12" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
    <path d="M28 16l-4 20" stroke="#f59e0b" stroke-width="2.5" stroke-linecap="round"/>
  </svg>`,
  cyclist: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="48" height="48" fill="none">
    <ellipse cx="24" cy="44" rx="18" ry="4" fill="rgba(0,0,0,0.65)"/>
    <circle cx="12" cy="34" r="6.5" stroke="#DFFF00" stroke-width="3.5"/>
    <circle cx="36" cy="34" r="6.5" stroke="#DFFF00" stroke-width="3.5"/>
    <path d="M30 12a2 2 0 1 0 0-4 2 2 0 0 0 0 4zm-6 22L17 23l6-7h6l4 8" stroke="#ffffff" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="M24 34V28l7-5" stroke="#ffffff" stroke-width="3.5" stroke-linecap="round"/>
  </svg>`,
  car: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 40" width="48" height="40" fill="none">
    <ellipse cx="24" cy="36" rx="20" ry="4" fill="rgba(0,0,0,0.7)"/>
    <path d="M8 28h32m-26 0a3.5 3.5 0 1 1-7 0 3.5 3.5 0 0 1 7 0zm24 0a3.5 3.5 0 1 1-7 0 3.5 3.5 0 0 1 7 0z" fill="#1e293b"/>
    <path d="M6 28V15a3 3 0 0 1 3-3h18l8 8v8" fill="#f59e0b" stroke="#ffffff" stroke-width="2" stroke-linecap="round"/>
    <path d="M15 15v5h8v-5z" fill="#0f172a"/>
  </svg>`,
  dot: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 36" width="36" height="36" fill="none">
    <circle cx="18" cy="18" r="15" fill="rgba(223,255,0,0.3)" stroke="rgba(223,255,0,0.7)" stroke-width="2"/>
    <circle cx="18" cy="18" r="7" fill="#DFFF00" stroke="#ffffff" stroke-width="2.5"/>
  </svg>`
};

const avatarImages = {};
function initAvatarImages() {
  const cyclistImg = new Image();
  cyclistImg.src = '/img/cyclist_red.png';
  avatarImages['cyclist'] = cyclistImg;

  const runnerImg = new Image();
  runnerImg.src = '/img/runner_red_3d.png';
  avatarImages['runner'] = runnerImg;

  const carImg = new Image();
  carImg.src = '/img/car_custom.png';
  avatarImages['car'] = carImg;

  for (const [key, svg] of Object.entries(AVATAR_SVGS)) {
    if (key === 'runner' || key === 'car' || key === 'cyclist') continue;
    const img = new Image();
    img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
    avatarImages[key] = img;
  }
}

// -------------------------------------------------------------
// 1. Initialization & MapLibre GL 3D Terrain Setup
// -------------------------------------------------------------
async function init() {
  initAvatarImages();
  bindEvents();
  await loadTrackData();
  setup3DMap();
}

async function loadTrackData() {
  try {
    const res = await fetch('/api/track');
    if (!res.ok) throw new Error('API returned ' + res.status);
    state.track = await res.json();
    state.totalPoints = state.track.coordinates.length;
    renderInitialMetadata();
  } catch (err) {
    console.warn('[!] Failed to fetch /api/track, using local fallback:', err);
    // Embedded minimal fallback
    state.track = generateFallbackTrack();
    state.totalPoints = state.track.coordinates.length;
    renderInitialMetadata();
  }
}

function renderInitialMetadata() {
  const meta = state.track.metadata;
  DOM.trackHeader.innerText = `${meta.title} · ${meta.total_distance_km}km · ${meta.total_gain_m}m爬升`;
  DOM.introTitle.innerText = meta.title;
  DOM.introSub.innerText = `${meta.total_distance_km}KM   ${Math.round(meta.total_gain_m)}m 爬升`;
  DOM.eleMax.innerText = `Max ${Math.round(meta.max_ele_m)}m`;
  DOM.eleMin.innerText = `Min ${Math.round(meta.min_ele_m)}m`;

  drawInitialElevationProfile();
}

function setup3DMap() {
  const bounds = state.track.metadata.bounds;
  const center = [
    (bounds.min_lon + bounds.max_lon) / 2,
    (bounds.min_lat + bounds.max_lat) / 2
  ];

  state.map = new maplibregl.Map({
    container: 'map-container',
    style: {
      version: 8,
      sources: {
        'satellite': {
          type: 'raster',
          tiles: [
            'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}.jpg'
          ],
          tileSize: 256,
          maxzoom: 19
        },
        'terrainSource': {
          type: 'raster-dem',
          tiles: [
            'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png'
          ],
          encoding: 'terrarium',
          tileSize: 256,
          maxzoom: 15
        }
      },
      layers: [
        {
          id: 'satellite-layer',
          type: 'raster',
          source: 'satellite',
          minzoom: 0,
          maxzoom: 22
        }
      ],
      sky: {
        'sky-color': '#0f172a',
        'sky-horizon-blend': 0.6,
        'horizon-color': '#1e293b',
        'horizon-fog-blend': 0.8,
        'fog-color': '#030712',
        'fog-ground-blend': 0.95
      }
    },
    center: center,
    zoom: 14.2,
    pitch: 58,
    bearing: state.track.telemetry.bearings_deg[0] || 0,
    maxPitch: 85,
    preserveDrawingBuffer: true,
    attributionControl: false
  });

  state.map.on('load', () => {
    // Enable 3D Terrain
    state.map.setTerrain({ source: 'terrainSource', exaggeration: state.exaggeration });

    // Add Route Layers (Untraveled & Traveled)
    addRouteLayers();

    // Create 3D Avatar Marker
    createAvatarMarker();

    state.mapReady = true;
    console.log('[✓] MapLibre 3D Terrain ready!');

    // Check if running in automated headless export mode
    const urlParams = new URLSearchParams(window.location.search);
    if (urlParams.get('export') === 'true') {
      document.body.classList.add('export-mode');
      const reqAspect = urlParams.get('aspect') || '9-16';
      const reqDuration = parseFloat(urlParams.get('duration') || '36.0');
      const reqAvatar = urlParams.get('avatar') || 'cyclist';
      const reqCamera = urlParams.get('camera') || 'chase';
      const reqExaggeration = parseFloat(urlParams.get('exaggeration') || '2.8');
      state.aspect = reqAspect;
      state.duration = reqDuration;
      state.avatar = reqAvatar;
      state.cameraMode = reqCamera;
      state.exaggeration = reqExaggeration;
      if (state.map && state.mapReady) {
        state.map.setTerrain({ source: 'terrainSource', exaggeration: state.exaggeration });
      }
      updateAvatarAppearance();

      DOM.frame.className = `viewport-wrapper aspect-${state.aspect}`;
      state.map.resize();

      let started = false;
      const startExportTrigger = () => {
        if (started) return;
        started = true;
        console.log('[*] Map ready for export, launching in 1.2s...');
        setTimeout(() => runHeadlessExport(reqAspect), 1200);
      };

      state.map.once('idle', startExportTrigger);
      setTimeout(startExportTrigger, 2500);
    } else {
      // Initial seek to 0 in interactive mode
      seekTo(0.0);
    }
  });

  state.map.on('render', () => {
    if (state.isRecording) {
      renderCompositeFrame(state.currentPointIdx, state.currentProgress, state.currentStage);
    }
  });
}

// Headless Automated Recorder
async function runHeadlessExport(aspectName) {
  console.log('[*] Headless recording starting for ' + aspectName + '...');
  pause();
  seekTo(0.0);

  const recCanvas = setupRecordingCanvas();
  renderCompositeFrame(0, 0, 'intro');
  const stream = recCanvas.captureStream(60);

  let mimeType = 'video/webm;codecs=vp9';
  if (!MediaRecorder.isTypeSupported(mimeType)) mimeType = 'video/webm';

  const recorder = new MediaRecorder(stream, {
    mimeType: mimeType,
    videoBitsPerSecond: 28000000
  });

  const chunks = [];
  recorder.ondataavailable = e => { if (e.data.size > 0) chunks.push(e.data); };

  recorder.onstop = async () => {
    console.log('[*] Headless recording finished, uploading to server...');
    state.isRecording = false;
    const blob = new Blob(chunks, { type: 'video/webm' });
    const filename = `trailmov_raw_${aspectName}.webm`;
    await fetch(`/api/upload_export?filename=${filename}`, {
      method: 'POST',
      body: blob
    });
    console.log('[✓] Upload completed. Setting window._exportDone = true');
    window._exportDone = true;
  };

  recorder.start();
  play();

  const check = setInterval(() => {
    if (state.currentTime >= state.duration) {
      clearInterval(check);
      pause();
      recorder.stop();
    }
  }, 100);
}

// -------------------------------------------------------------
// 2. Trajectory Layers & 3D Avatar Rendering
// -------------------------------------------------------------
function addRouteLayers() {
  const coords = state.track.coordinates;

  // 1. Untraveled Route (Full Route in subtle white/grey)
  state.map.addSource('route-full', {
    type: 'geojson',
    data: {
      type: 'Feature',
      geometry: { type: 'LineString', coordinates: coords }
    }
  });

  state.map.addLayer({
    id: 'layer-route-full',
    type: 'line',
    source: 'route-full',
    layout: { 'line-join': 'round', 'line-cap': 'round' },
    paint: {
      'line-color': 'rgba(255, 255, 255, 0.40)',
      'line-width': 2.5
    }
  });

  // 2. Traveled Route (Vibrant TrailMov Neon Lime #DFFF00)
  state.map.addSource('route-traveled', {
    type: 'geojson',
    data: {
      type: 'Feature',
      geometry: { type: 'LineString', coordinates: [coords[0]] }
    }
  });

  // Traveled shadow
  state.map.addLayer({
    id: 'layer-route-shadow',
    type: 'line',
    source: 'route-traveled',
    layout: { 'line-join': 'round', 'line-cap': 'round' },
    paint: {
      'line-color': 'rgba(0, 0, 0, 0.55)',
      'line-width': 5.0,
      'line-blur': 1.5
    }
  });

  // Traveled neon body
  state.map.addLayer({
    id: 'layer-route-traveled',
    type: 'line',
    source: 'route-traveled',
    layout: { 'line-join': 'round', 'line-cap': 'round' },
    paint: {
      'line-color': '#DFFF00',
      'line-width': 2.5
    }
  });
}

function createAvatarMarker() {
  const el = document.createElement('div');
  el.className = 'trailmov-avatar-wrapper';
  el.innerHTML = getAvatarSvg(state.avatar);

  const startCoord = state.track.coordinates[0];
  state.avatarMarker = new maplibregl.Marker({
    element: el,
    anchor: 'bottom',
    pitchAlignment: 'map',
    rotationAlignment: 'map'
  })
  .setLngLat([startCoord[0], startCoord[1]])
  .addTo(state.map);
}

function updateAvatarAppearance() {
  if (!state.avatarMarker) return;
  const el = state.avatarMarker.getElement();
  el.innerHTML = getAvatarSvg(state.avatar);
}

function getAvatarSvg(type) {
  // SVG avatars matching TrailMov (Runner, Hiker, Cyclist, SUV, Pulse Pin)
  if (type === 'runner') {
    return `
      <div style="position:relative; width:22px; height:30px; display:flex; flex-direction:column; align-items:center;">
        <!-- Dynamic Ground Contact Shadow -->
        <div style="position:absolute; bottom:0; width:14px; height:5px; background:rgba(0,0,0,0.65); border-radius:50%; filter:blur(1.5px);"></div>
        <!-- 3D Red Runner (1/2 size) -->
        <img src="/img/runner_red_3d.png" style="width:20px; height:27px; object-fit:contain; filter:drop-shadow(0 3px 5px rgba(0,0,0,0.5)); transform:translateY(-1px);" />
      </div>`;
  } else if (type === 'hiker') {
    return `
      <div style="position:relative; width:36px; height:46px; display:flex; flex-direction:column; align-items:center;">
        <div style="position:absolute; bottom:0; width:24px; height:8px; background:rgba(0,0,0,0.6); border-radius:50%; filter:blur(2px);"></div>
        <svg viewBox="0 0 24 24" width="34" height="34" fill="none" stroke="#fff" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" style="filter: drop-shadow(0 4px 6px rgba(0,0,0,0.6));">
          <circle cx="12" cy="4" r="2.2" fill="#38bdf8" stroke="none" />
          <path d="M10 8h4l1 5-4 1-2-1z" fill="#0284c7" stroke="none" />
          <path d="M12 8v7m-2 3l2-3 2 3m-4 0v3m4-3v3" />
          <path d="M17 10l-2 10" stroke="#f59e0b" stroke-width="1.8" />
        </svg>
      </div>`;
  } else if (type === 'cyclist') {
    return `
      <div style="position:relative; width:40px; height:28px; display:flex; flex-direction:column; align-items:center;">
        <!-- Dynamic Ground Contact Shadow -->
        <div style="position:absolute; bottom:0; width:28px; height:6px; background:rgba(0,0,0,0.65); border-radius:50%; filter:blur(1.5px);"></div>
        <!-- Custom Red Cyclist Icon -->
        <img src="/img/cyclist_red.png" style="width:36px; height:24px; object-fit:contain; filter:drop-shadow(0 2px 4px rgba(0,0,0,0.6)); transform:translateY(-1px);" />
      </div>`;
  } else if (type === 'car') {
    return `
      <div style="position:relative; width:38px; height:24px; display:flex; flex-direction:column; align-items:center;">
        <!-- Dynamic Ground Contact Shadow -->
        <div style="position:absolute; bottom:0; width:30px; height:6px; background:rgba(0,0,0,0.65); border-radius:50%; filter:blur(1.5px);"></div>
        <!-- Custom Car Icon -->
        <img src="/img/car_custom.png" style="width:34px; height:20px; object-fit:contain; filter:drop-shadow(0 2px 4px rgba(0,0,0,0.6)); transform:translateY(-1px);" />
      </div>`;
  } else {
    // Pulse Pin
    return `
      <div style="position:relative; width:28px; height:28px; display:flex; align-items:center; justify-content:center;">
        <div style="position:absolute; width:26px; height:26px; background:rgba(223,255,0,0.35); border-radius:50%; animation: ping 1.5s cubic-bezier(0, 0, 0.2, 1) infinite;"></div>
        <div style="width:14px; height:14px; background:#DFFF00; border:2.5px solid #fff; border-radius:50%; box-shadow:0 0 10px #DFFF00;"></div>
      </div>`;
  }
}

// -------------------------------------------------------------
// 3. Animation Engine & Camera Choreography
// -------------------------------------------------------------
function play() {
  if (state.isPlaying) return;
  state.isPlaying = true;
  DOM.playIcon.setAttribute('data-lucide', 'pause');
  lucide.createIcons();
  state.lastFrameTs = performance.now();
  state.rafId = requestAnimationFrame(animateLoop);
}

function pause() {
  state.isPlaying = false;
  DOM.playIcon.setAttribute('data-lucide', 'play');
  lucide.createIcons();
  if (state.rafId) {
    cancelAnimationFrame(state.rafId);
    state.rafId = null;
  }
}

function animateLoop(now) {
  if (!state.isPlaying) return;

  const dt = (now - state.lastFrameTs) / 1000.0;
  state.lastFrameTs = now;

  state.currentTime += dt;
  if (state.currentTime >= state.duration) {
    state.currentTime = state.duration;
    seekTo(state.duration);
    pause();
    return;
  }

  seekTo(state.currentTime);
  state.rafId = requestAnimationFrame(animateLoop);
}

function seekTo(targetSeconds) {
  state.currentTime = Math.max(0, Math.min(state.duration, targetSeconds));
  const progress = state.currentTime / state.duration; // 0.0 ~ 1.0

  // 1. Update Playback Time & Range Slider
  updateTimelineUI(progress);

  // 2. Map progress to track index
  const introDuration = 1.0; // 1s snappy intro requested by user
  const outroDuration = 2.5; // 2.5s pullback
  const flightDuration = Math.max(1.0, state.duration - introDuration - outroDuration);

  let pointIdx = 0;
  let stage = 'intro';

  if (state.currentTime < 0.5) {
    stage = 'intro';
    pointIdx = 0;
  } else if (state.currentTime < introDuration) {
    stage = 'dive';
    pointIdx = 0;
  } else if (state.currentTime < state.duration - outroDuration) {
    stage = 'flight';
    const flightProgress = (state.currentTime - introDuration) / flightDuration;
    pointIdx = Math.min(state.totalPoints - 1, Math.floor(flightProgress * (state.totalPoints - 1)));
  } else {
    stage = 'outro';
    pointIdx = state.totalPoints - 1;
  }

  state.currentPointIdx = pointIdx;
  state.currentProgress = progress;
  state.currentStage = stage;

  // 3. Update Camera Position & Orientation
  updateCamera(stage, progress, pointIdx);

  // 4. Update Traveled Path GeoJSON
  updateTraveledPath(pointIdx);

  // 5. Update Avatar Marker
  updateAvatarPosition(pointIdx);

  // 6. Update HUD Elements
  updateHUD(stage, pointIdx, progress);

  // 7. If recording active, composite immediately
  if (state.isRecording) {
    renderCompositeFrame(pointIdx, progress, stage);
  }
}

function updateCamera(stage, totalProgress, currIdx) {
  if (!state.map || !state.mapReady) return;

  const coords = state.track.coordinates;
  const bearings = state.track.telemetry.bearings_deg;
  const bounds = state.track.metadata.bounds;

  if (stage === 'intro') {
    // 0 ~ 0.5s: High overview showing whole park
    state.cameraBearing = bearings[0] || 0;
    state.cameraCenter = [coords[0][0], coords[0][1]];
    state.map.jumpTo({
      center: [(bounds.min_lon + bounds.max_lon) / 2, (bounds.min_lat + bounds.max_lat) / 2],
      zoom: 13.8,
      pitch: 45,
      bearing: bearings[0] || 0
    });
  } else if (stage === 'dive') {
    // 0.5 ~ 1.0s: Fast smooth swoop into the start coordinate
    const t = (state.currentTime - 0.5) / 0.5;
    const ease = t * t * (3 - 2 * t);
    const startCoord = coords[0];
    const midCenter = [(bounds.min_lon + bounds.max_lon) / 2, (bounds.min_lat + bounds.max_lat) / 2];

    const curLng = midCenter[0] + (startCoord[0] - midCenter[0]) * ease;
    const curLat = midCenter[1] + (startCoord[1] - midCenter[1]) * ease;

    state.cameraBearing = bearings[0] || 0;
    state.cameraCenter = [curLng, curLat];

    state.map.jumpTo({
      center: [curLng, curLat],
      zoom: 13.8 + (15.8 - 13.8) * ease,
      pitch: 45 + (68 - 45) * ease,
      bearing: bearings[0] || 0
    });
  } else if (stage === 'flight') {
    // 1.0s ~ (Duration - 2.5s): Cinematic Drone Gimbal Tracking
    const curCoord = coords[currIdx];
    const targetBearing = bearings[currIdx] || 0;

    if (state.cameraBearing === null) state.cameraBearing = targetBearing;
    if (!state.cameraCenter) state.cameraCenter = [curCoord[0], curCoord[1]];

    // 1. Angular gimbal dampening (shortest circular path)
    let diff = targetBearing - state.cameraBearing;
    while (diff > 180) diff -= 360;
    while (diff < -180) diff += 360;
    // 0.04 damping factor gives ultra-stable cinematic drone flight without angular shaking!
    state.cameraBearing = (state.cameraBearing + diff * 0.04) % 360;

    // 2. Spatial position dampening
    state.cameraCenter[0] += (curCoord[0] - state.cameraCenter[0]) * 0.15;
    state.cameraCenter[1] += (curCoord[1] - state.cameraCenter[1]) * 0.15;

    if (state.cameraMode === 'chase') {
      state.map.jumpTo({
        center: [state.cameraCenter[0], state.cameraCenter[1]],
        zoom: 15.8,
        pitch: 68,
        bearing: state.cameraBearing
      });
    } else if (state.cameraMode === 'fixed') {
      // Fixed North-Up: 100% rock-solid, zero angle movement!
      state.map.jumpTo({
        center: [state.cameraCenter[0], state.cameraCenter[1]],
        zoom: 15.6,
        pitch: 58,
        bearing: 0
      });
    } else if (state.cameraMode === 'orbit') {
      const orbitBearing = (bearings[0] + totalProgress * 120) % 360;
      state.map.jumpTo({
        center: [state.cameraCenter[0], state.cameraCenter[1]],
        zoom: 15.6,
        pitch: 66,
        bearing: orbitBearing
      });
    } else {
      // Overview
      state.map.jumpTo({
        center: [state.cameraCenter[0], state.cameraCenter[1]],
        zoom: 14.6,
        pitch: 35,
        bearing: 0
      });
    }
  } else {
    // Outro: Camera pulls back to high-altitude glory shot
    const t = (state.currentTime - (state.duration - 2.5)) / 2.5;
    const ease = t * t * (3 - 2 * t);
    const lastCoord = coords[coords.length - 1];
    const midCenter = [(bounds.min_lon + bounds.max_lon) / 2, (bounds.min_lat + bounds.max_lat) / 2];

    const curLng = lastCoord[0] + (midCenter[0] - lastCoord[0]) * ease;
    const curLat = lastCoord[1] + (midCenter[1] - lastCoord[1]) * ease;

    state.map.jumpTo({
      center: [curLng, curLat],
      zoom: 15.8 + (13.8 - 15.8) * ease,
      pitch: 68 + (40 - 68) * ease,
      bearing: (state.cameraBearing || bearings[bearings.length - 1]) + ease * 30
    });
  }
}

function updateTraveledPath(currIdx) {
  if (!state.map || !state.mapReady) return;
  const traveledCoords = state.track.coordinates.slice(0, Math.max(1, currIdx + 1));
  
  const source = state.map.getSource('route-traveled');
  if (source) {
    source.setData({
      type: 'Feature',
      geometry: { type: 'LineString', coordinates: traveledCoords }
    });
  }
}

function updateAvatarPosition(currIdx) {
  if (!state.avatarMarker) return;
  const curCoord = state.track.coordinates[currIdx];
  const curBearing = state.track.telemetry.bearings_deg[currIdx] || 0;

  state.avatarMarker.setLngLat([curCoord[0], curCoord[1]]);
  state.avatarMarker.setRotation(curBearing);
}

// -------------------------------------------------------------
// 4. HUD Telemetry Updates
// -------------------------------------------------------------
function updateHUD(stage, currIdx, progress) {
  const telem = state.track.telemetry;

  // 1. Intro Card Visibility (Fade out after 1.0s)
  if (state.currentTime <= 1.0) {
    DOM.introCard.style.opacity = '1';
  } else {
    DOM.introCard.style.opacity = '0';
  }

  // 2. Distance Card
  const curDist = telem.dists_km[currIdx] || 0;
  DOM.distanceVal.innerText = curDist.toFixed(2);

  // 3. Climb & Descent Card
  const curGain = Math.round(telem.gains_m[currIdx] || 0);
  const curLoss = Math.round(telem.losses_m[currIdx] || 0);
  DOM.climbVal.innerText = `${curGain} m`;
  DOM.descentVal.innerText = `${curLoss} m`;

  // 4. Elevation Profile Card & Active SVG Path
  updateElevationProfileUI(currIdx, progress);

  // 5. Mini-map Drawing
  drawMinimap(currIdx);
}

function drawInitialElevationProfile() {
  const profile = state.track.profile;
  if (!profile || profile.length < 2) return;

  const minEle = state.track.metadata.min_ele_m;
  const maxEle = state.track.metadata.max_ele_m;
  const eleSpan = Math.max(maxEle - minEle, 10.0);

  // SVG viewBox: 0 0 400 50
  let bgPath = '';
  for (let i = 0; i < profile.length; i++) {
    const x = (i / (profile.length - 1)) * 400;
    const y = 50 - ((profile[i].ele - minEle) / eleSpan) * 44 - 3;
    bgPath += (i === 0 ? `M ${x.toFixed(1)} ${y.toFixed(1)}` : ` L ${x.toFixed(1)} ${y.toFixed(1)}`);
  }
  DOM.svgBg.setAttribute('d', bgPath);
}

function updateElevationProfileUI(currIdx, progress) {
  const profile = state.track.profile;
  if (!profile || profile.length < 2) return;

  const minEle = state.track.metadata.min_ele_m;
  const maxEle = state.track.metadata.max_ele_m;
  const eleSpan = Math.max(maxEle - minEle, 10.0);

  // Find sampled profile index corresponding to current track point
  const profIdx = Math.min(profile.length - 1, Math.floor(progress * (profile.length - 1)));
  const curEle = state.track.telemetry.eles_m[currIdx] || profile[profIdx].ele;

  // Active Line & Filled Gradient
  let activePath = '';
  let fillPath = 'M 0 50 ';

  for (let i = 0; i <= profIdx; i++) {
    const x = (i / (profile.length - 1)) * 400;
    const y = 50 - ((profile[i].ele - minEle) / eleSpan) * 44 - 3;
    activePath += (i === 0 ? `M ${x.toFixed(1)} ${y.toFixed(1)}` : ` L ${x.toFixed(1)} ${y.toFixed(1)}`);
    fillPath += `L ${x.toFixed(1)} ${y.toFixed(1)} `;
  }

  const curX = (profIdx / (profile.length - 1)) * 400;
  fillPath += `L ${curX.toFixed(1)} 50 Z`;

  DOM.svgActive.setAttribute('d', activePath);
  DOM.svgFill.setAttribute('d', fillPath);

  // Move Current Elevation Pin Badge
  const percentX = (profIdx / (profile.length - 1)) * 100;
  DOM.curPin.style.left = `${percentX.toFixed(1)}%`;
  DOM.curBadgeTxt.innerText = `${Math.round(curEle)}m`;
}

function drawMinimap(currIdx) {
  const ctx = DOM.minimapCanvas.getContext('2d');
  const w = DOM.minimapCanvas.width;
  const h = DOM.minimapCanvas.height;

  ctx.clearRect(0, 0, w, h);

  const coords = state.track.coordinates;
  const bounds = state.track.metadata.bounds;

  const pad = 12;
  const innerW = w - pad * 2;
  const innerH = h - pad * 2;
  const spanLon = Math.max(bounds.max_lon - bounds.min_lon, 0.001);
  const spanLat = Math.max(bounds.max_lat - bounds.min_lat, 0.001);

  // 1. Draw untraveled line (grey)
  ctx.beginPath();
  for (let i = 0; i < coords.length; i += Math.max(1, Math.floor(coords.length / 150))) {
    const x = pad + ((coords[i][0] - bounds.min_lon) / spanLon) * innerW;
    const y = h - pad - ((coords[i][1] - bounds.min_lat) / spanLat) * innerH;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.35)';
  ctx.lineWidth = 1.5;
  ctx.stroke();

  // 2. Draw traveled line (vibrant neon lime)
  ctx.beginPath();
  for (let i = 0; i <= currIdx; i += Math.max(1, Math.floor(coords.length / 150))) {
    const x = pad + ((coords[i][0] - bounds.min_lon) / spanLon) * innerW;
    const y = h - pad - ((coords[i][1] - bounds.min_lat) / spanLat) * innerH;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = '#DFFF00';
  ctx.lineWidth = 2.0;
  ctx.stroke();

  // 3. Start Point (Red Dot)
  const sx = pad + ((coords[0][0] - bounds.min_lon) / spanLon) * innerW;
  const sy = h - pad - ((coords[0][1] - bounds.min_lat) / spanLat) * innerH;
  ctx.fillStyle = '#ef4444';
  ctx.beginPath();
  ctx.arc(sx, sy, 3.5, 0, Math.PI * 2);
  ctx.fill();

  // 4. End Point (Green Dot)
  const lastIdx = coords.length - 1;
  const ex = pad + ((coords[lastIdx][0] - bounds.min_lon) / spanLon) * innerW;
  const ey = h - pad - ((coords[lastIdx][1] - bounds.min_lat) / spanLat) * innerH;
  ctx.fillStyle = '#22c55e';
  ctx.beginPath();
  ctx.arc(ex, ey, 3.5, 0, Math.PI * 2);
  ctx.fill();

  // 5. Current Position (Target Pin)
  const cx = pad + ((coords[currIdx][0] - bounds.min_lon) / spanLon) * innerW;
  const cy = h - pad - ((coords[currIdx][1] - bounds.min_lat) / spanLat) * innerH;

  // Pulse ring
  ctx.strokeStyle = 'rgba(223, 255, 0, 0.5)';
  ctx.lineWidth = 2.5;
  ctx.beginPath();
  ctx.arc(cx, cy, 6, 0, Math.PI * 2);
  ctx.stroke();

  // Center white dot
  ctx.fillStyle = '#ffffff';
  ctx.beginPath();
  ctx.arc(cx, cy, 2.5, 0, Math.PI * 2);
  ctx.fill();
}

function updateTimelineUI(progress) {
  DOM.timelineSlider.value = Math.floor(progress * 1000);

  const curSec = Math.floor(state.currentTime);
  const totalSec = Math.floor(state.duration);
  const fmt = s => `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
  DOM.timeDisplay.innerText = `${fmt(curSec)} / ${fmt(totalSec)}`;
}

// -------------------------------------------------------------
// 5. User Controls & Event Listeners
// -------------------------------------------------------------
function bindEvents() {
  // Play / Pause
  DOM.btnPlay.addEventListener('click', () => {
    if (state.isPlaying) pause();
    else play();
  });

  // Replay
  DOM.btnReplay.addEventListener('click', () => {
    pause();
    seekTo(0.0);
    play();
  });

  // Spacebar toggle
  window.addEventListener('keydown', e => {
    if (e.code === 'Space' && e.target.tagName !== 'INPUT' && e.target.tagName !== 'SELECT') {
      e.preventDefault();
      if (state.isPlaying) pause();
      else play();
    }
  });

  // Timeline Scrubbing
  DOM.timelineSlider.addEventListener('input', e => {
    pause();
    const frac = parseFloat(e.target.value) / 1000.0;
    seekTo(frac * state.duration);
  });

  // Aspect Ratio Switcher
  DOM.aspectSelect.addEventListener('change', e => {
    state.aspect = e.target.value;
    DOM.frame.className = `viewport-wrapper aspect-${state.aspect}`;
    DOM.modalCurAspect.innerText = e.target.options[e.target.selectedIndex].text;
    setTimeout(() => {
      if (state.map) state.map.resize();
    }, 360);
  });

  // Avatar Switcher
  DOM.avatarSelect.addEventListener('change', e => {
    state.avatar = e.target.value;
    updateAvatarAppearance();
  });

  // Camera Switcher
  DOM.cameraSelect.addEventListener('change', e => {
    state.cameraMode = e.target.value;
    seekTo(state.currentTime);
  });

  // Duration Switcher
  DOM.durationSelect.addEventListener('change', e => {
    state.duration = parseFloat(e.target.value);
    seekTo(state.currentTime);
  });

  // Exaggeration Switcher
  DOM.exaggerationSelect.addEventListener('change', e => {
    state.exaggeration = parseFloat(e.target.value);
    if (state.map && state.mapReady) {
      state.map.setTerrain({ source: 'terrainSource', exaggeration: state.exaggeration });
    }
  });

  // Export Modal Triggers
  DOM.btnExportDialog.addEventListener('click', () => {
    DOM.modal.classList.add('active');
  });

  DOM.btnExportCancel.addEventListener('click', () => {
    DOM.modal.classList.remove('active');
  });

  DOM.btnExportStart.addEventListener('click', () => {
    startExport();
  });

  // Safe GPX Deletion Gate
  DOM.btnSafeDelete.addEventListener('click', async () => {
    const ok = confirm('⚠️ 确认操作：\n您是否确认当前生成的视频完全合格？\n点击【确定】后将安全删除 GPX 目录下的原始轨迹文件。');
    if (!ok) return;

    try {
      const res = await fetch('/api/delete_gpx?confirm=true', { method: 'POST' });
      const data = await res.json();
      if (data.success) {
        alert('✓ ' + data.message);
        location.reload();
      } else {
        alert('❌ ' + data.message);
      }
    } catch (err) {
      alert('❌ 触发删除失败: ' + err.message);
    }
  });
}

// -------------------------------------------------------------
// 6. 60fps WebGL/Canvas Video Export & Telemetry Compositing
// -------------------------------------------------------------
function setupRecordingCanvas() {
  const mapCanvas = state.map.getCanvas();
  let recCanvas = document.getElementById('recording-composite-canvas');
  if (!recCanvas) {
    recCanvas = document.createElement('canvas');
    recCanvas.id = 'recording-composite-canvas';
    recCanvas.style.display = 'none';
    document.body.appendChild(recCanvas);
  }
  recCanvas.width = mapCanvas.width;
  recCanvas.height = mapCanvas.height;
  state.recordingCanvas = recCanvas;
  state.recordingCtx = recCanvas.getContext('2d');
  state.isRecording = true;
  return recCanvas;
}

function renderCompositeFrame(pointIdx, progress, stage) {
  if (!state.isRecording || !state.recordingCanvas || !state.recordingCtx || !state.map) return;
  const ctx = state.recordingCtx;
  const width = state.recordingCanvas.width;
  const height = state.recordingCanvas.height;
  const mapCanvas = state.map.getCanvas();

  // 1. Draw WebGL 3D Satellite Terrain Canvas
  ctx.drawImage(mapCanvas, 0, 0, width, height);

  // 2. Uniform Proportional Scale (harmonizes 9:16 vertical & 16:9 widescreen)
  const scale = Math.min(width / 450.0, height / 700.0);

  // 3. Draw 3D Avatar at projected screen coordinates
  drawAvatarOnCanvas(ctx, pointIdx, width, height, scale);

  // 4. Draw Frosted Glass HUD Telemetry Layer
  drawMinimapCard(ctx, pointIdx, scale);
  drawDistanceCard(ctx, pointIdx, scale, width);
  drawClimbBadge(ctx, pointIdx, scale);
  drawElevationProfile(ctx, pointIdx, scale, width, height);
  drawIntroCard(ctx, scale, width, height);
}

function drawAvatarOnCanvas(ctx, pointIdx, width, height, scale) {
  if (!state.map || !state.track) return;
  const coord = state.track.coordinates[pointIdx];
  if (!coord) return;
  
  const p = state.map.project([coord[0], coord[1]]);
  if (p.x < -100 || p.x > width + 100 || p.y < -100 || p.y > height + 100) return;

  const img = avatarImages[state.avatar] || avatarImages['cyclist'] || avatarImages['car'] || avatarImages['runner'];
  if (!img) return;

  ctx.save();
  ctx.translate(p.x, p.y);

  if (state.avatar === 'cyclist') {
    const w = 36 * scale;
    const h = 24 * scale;
    // Ground shadow under wheels
    ctx.fillStyle = 'rgba(0, 0, 0, 0.65)';
    ctx.beginPath();
    ctx.ellipse(0, 0, 15 * scale, 5 * scale, 0, 0, Math.PI * 2);
    ctx.fill();

    // Subtle white glow outline so red cyclist pops against dark/complex satellite terrain
    ctx.shadowColor = 'rgba(255, 255, 255, 0.65)';
    ctx.shadowBlur = 3 * scale;
    // Anchor center between wheels (at 0.64 * w) on the ground
    ctx.drawImage(img, -0.64 * w, -h + (2 * scale), w, h);
  } else if (state.avatar === 'car') {
    const w = 34 * scale;
    const h = 20 * scale;
    // Ground shadow under car chassis
    ctx.fillStyle = 'rgba(0, 0, 0, 0.65)';
    ctx.beginPath();
    ctx.ellipse(0, 0, 16 * scale, 5 * scale, 0, 0, Math.PI * 2);
    ctx.fill();

    // Subtle white glow outline so black car pops against dark satellite terrain
    ctx.shadowColor = 'rgba(255, 255, 255, 0.6)';
    ctx.shadowBlur = 3 * scale;
    ctx.drawImage(img, -w / 2, -h + (2 * scale), w, h);
  } else {
    // Runner & other avatars
    const w = 23 * scale;
    const h = 30 * scale;
    ctx.fillStyle = 'rgba(0, 0, 0, 0.65)';
    ctx.beginPath();
    ctx.ellipse(0, 0, 10 * scale, 3.5 * scale, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.drawImage(img, -w / 2, -h + (2 * scale), w, h);
  }

  ctx.restore();
}

function drawMinimapCard(ctx, pointIdx, scale) {
  const x = 20 * scale;
  const y = 24 * scale;
  const w = 126 * scale;
  const h = 136 * scale;
  const r = 14 * scale;

  ctx.save();
  ctx.fillStyle = 'rgba(18, 26, 43, 0.72)';
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.14)';
  ctx.lineWidth = 1.5 * scale;
  ctx.beginPath();
  ctx.roundRect(x, y, w, h, r);
  ctx.fill();
  ctx.stroke();

  if (DOM.minimapCanvas) {
    ctx.drawImage(DOM.minimapCanvas, x + 6 * scale, y + 6 * scale, w - 12 * scale, h - 12 * scale);
  }
  ctx.restore();
}

function drawDistanceCard(ctx, pointIdx, scale, width) {
  const cardW = 144 * scale;
  const cardH = 88 * scale;
  const x = width - cardW - (20 * scale);
  const y = 24 * scale;
  const r = 14 * scale;

  ctx.save();
  ctx.fillStyle = 'rgba(18, 26, 43, 0.72)';
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.14)';
  ctx.lineWidth = 1.5 * scale;
  ctx.beginPath();
  ctx.roundRect(x, y, cardW, cardH, r);
  ctx.fill();
  ctx.stroke();

  ctx.fillStyle = 'rgba(255, 255, 255, 0.6)';
  ctx.font = `600 ${11 * scale}px "Plus Jakarta Sans", system-ui, sans-serif`;
  ctx.fillText('DISTANCE', x + 16 * scale, y + 26 * scale);

  const distKm = (state.track.telemetry.dists_km[pointIdx] || 0).toFixed(2);
  ctx.fillStyle = '#ffffff';
  ctx.font = `800 ${28 * scale}px "Plus Jakarta Sans", system-ui, sans-serif`;
  ctx.fillText(distKm, x + 16 * scale, y + 60 * scale);

  const numW = ctx.measureText(distKm).width;
  ctx.fillStyle = '#DFFF00';
  ctx.font = `700 ${13 * scale}px "Plus Jakarta Sans", system-ui, sans-serif`;
  ctx.fillText('km', x + 20 * scale + numW, y + 60 * scale);
  ctx.restore();
}

function drawClimbBadge(ctx, pointIdx, scale) {
  const x = 20 * scale;
  const y = 176 * scale;
  const w = 110 * scale;
  const h = 48 * scale;
  const r = 12 * scale;

  ctx.save();
  ctx.fillStyle = 'rgba(18, 26, 43, 0.72)';
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.14)';
  ctx.lineWidth = 1.5 * scale;
  ctx.beginPath();
  ctx.roundRect(x, y, w, h, r);
  ctx.fill();
  ctx.stroke();

  const gain = Math.round(state.track.telemetry.gains_m[pointIdx] || 0);
  ctx.fillStyle = '#DFFF00';
  ctx.font = `800 ${16 * scale}px "Plus Jakarta Sans", system-ui, sans-serif`;
  ctx.fillText(`▲ +${gain} m`, x + 14 * scale, y + 26 * scale);

  ctx.fillStyle = 'rgba(255, 255, 255, 0.55)';
  ctx.font = `600 ${10 * scale}px "Plus Jakarta Sans", system-ui, sans-serif`;
  ctx.fillText('CLIMB GAIN', x + 14 * scale, y + 40 * scale);
  ctx.restore();
}

function drawElevationProfile(ctx, pointIdx, scale, width, height) {
  // If widescreen (16:9), center a 600px wide card; if vertical (9:16), fill width
  const isWidescreen = width > height;
  const cardW = isWidescreen ? Math.min(width - (40 * scale), 640 * scale) : width - (40 * scale);
  const cardH = 92 * scale;
  const x = isWidescreen ? (width - cardW) / 2 : 20 * scale;
  const y = height - cardH - (24 * scale);
  const r = 16 * scale;

  ctx.save();
  ctx.fillStyle = 'rgba(18, 26, 43, 0.75)';
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.14)';
  ctx.lineWidth = 1.5 * scale;
  ctx.beginPath();
  ctx.roundRect(x, y, cardW, cardH, r);
  ctx.fill();
  ctx.stroke();

  const padX = 20 * scale;
  const padY = 22 * scale;
  const chartW = cardW - padX * 2;
  const chartH = cardH - padY * 2;
  const chartX = x + padX;
  const chartY = y + padY;

  const profile = state.track.profile;
  if (!profile || profile.length < 2) {
    ctx.restore();
    return;
  }

  const meta = state.track.metadata;
  const eleSpan = Math.max(20, meta.max_ele_m - meta.min_ele_m);
  const totalD = meta.total_distance_km;

  // Untraveled grey curve
  ctx.beginPath();
  profile.forEach((pt, i) => {
    const px = chartX + (pt.dist_km / totalD) * chartW;
    const py = chartY + chartH - ((pt.ele - meta.min_ele_m) / eleSpan) * chartH;
    if (i === 0) ctx.moveTo(px, py);
    else ctx.lineTo(px, py);
  });
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.18)';
  ctx.lineWidth = 2 * scale;
  ctx.stroke();

  // Traveled curve
  const curDistKm = state.track.telemetry.dists_km[pointIdx] || 0;
  const curEle = state.track.telemetry.eles_m[pointIdx] || meta.min_ele_m;

  const grad = ctx.createLinearGradient(chartX, chartY, chartX, chartY + chartH);
  grad.addColorStop(0, 'rgba(223, 255, 0, 0.45)');
  grad.addColorStop(1, 'rgba(255, 170, 0, 0.05)');

  ctx.beginPath();
  ctx.moveTo(chartX, chartY + chartH);
  let activeEndX = chartX;
  let activeEndY = chartY + chartH;

  for (let i = 0; i < profile.length; i++) {
    const pt = profile[i];
    const px = chartX + (pt.dist_km / totalD) * chartW;
    const py = chartY + chartH - ((pt.ele - meta.min_ele_m) / eleSpan) * chartH;
    if (pt.dist_km <= curDistKm) {
      ctx.lineTo(px, py);
      activeEndX = px;
      activeEndY = py;
    } else {
      if (i > 0) {
        const prev = profile[i - 1];
        const f = Math.max(0, Math.min(1, (curDistKm - prev.dist_km) / (pt.dist_km - prev.dist_km)));
        const prevPx = chartX + (prev.dist_km / totalD) * chartW;
        const prevPy = chartY + chartH - ((prev.ele - meta.min_ele_m) / eleSpan) * chartH;
        activeEndX = prevPx + (px - prevPx) * f;
        activeEndY = prevPy + (py - prevPy) * f;
        ctx.lineTo(activeEndX, activeEndY);
      }
      break;
    }
  }
  ctx.lineTo(activeEndX, chartY + chartH);
  ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();

  // Traveled line top border
  ctx.beginPath();
  let lineStarted = false;
  for (let i = 0; i < profile.length; i++) {
    const pt = profile[i];
    const px = chartX + (pt.dist_km / totalD) * chartW;
    const py = chartY + chartH - ((pt.ele - meta.min_ele_m) / eleSpan) * chartH;
    if (pt.dist_km <= curDistKm) {
      if (!lineStarted) { ctx.moveTo(px, py); lineStarted = true; }
      else ctx.lineTo(px, py);
    } else {
      ctx.lineTo(activeEndX, activeEndY);
      break;
    }
  }
  ctx.strokeStyle = '#DFFF00';
  ctx.lineWidth = 2.5 * scale;
  ctx.stroke();

  // Active pin dot
  ctx.fillStyle = '#ffffff';
  ctx.beginPath();
  ctx.arc(activeEndX, activeEndY, 4.5 * scale, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = '#DFFF00';
  ctx.lineWidth = 2 * scale;
  ctx.stroke();

  // Altitude badge
  const badgeW = 46 * scale;
  const badgeH = 20 * scale;
  const badgeX = Math.max(chartX, Math.min(chartX + chartW - badgeW, activeEndX - badgeW / 2));
  const badgeY = Math.max(y + 4 * scale, activeEndY - badgeH - 8 * scale);

  ctx.fillStyle = 'rgba(15, 23, 42, 0.9)';
  ctx.strokeStyle = '#DFFF00';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.roundRect(badgeX, badgeY, badgeW, badgeH, 6 * scale);
  ctx.fill();
  ctx.stroke();

  ctx.fillStyle = '#ffffff';
  ctx.font = `700 ${10 * scale}px "Plus Jakarta Sans", system-ui, sans-serif`;
  ctx.textAlign = 'center';
  ctx.fillText(`${Math.round(curEle)}m`, badgeX + badgeW / 2, badgeY + 14 * scale);
  ctx.textAlign = 'left';

  ctx.restore();
}

function drawIntroCard(ctx, scale, width, height) {
  if (state.currentTime > 2.2) return;
  const alpha = state.currentTime < 1.4 ? 1.0 : Math.max(0, 1.0 - (state.currentTime - 1.4) / 0.8);
  if (alpha <= 0) return;

  ctx.save();
  ctx.globalAlpha = alpha;

  const cardW = Math.min(340 * scale, width * 0.85);
  const cardH = 90 * scale;
  const x = (width - cardW) / 2;
  const y = height * 0.28;
  const r = 16 * scale;

  ctx.fillStyle = 'rgba(15, 23, 42, 0.88)';
  ctx.strokeStyle = 'rgba(223, 255, 0, 0.4)';
  ctx.lineWidth = 2 * scale;
  ctx.beginPath();
  ctx.roundRect(x, y, cardW, cardH, r);
  ctx.fill();
  ctx.stroke();

  // Title
  ctx.fillStyle = '#ffffff';
  ctx.font = `800 ${20 * scale}px "Plus Jakarta Sans", system-ui, sans-serif`;
  ctx.textAlign = 'center';
  ctx.fillText(state.track.metadata.title, width / 2, y + 36 * scale);

  // Subtitle
  const dist = state.track.metadata.total_distance_km;
  const gain = Math.round(state.track.metadata.total_gain_m);
  const pts = state.track.metadata.total_points;
  ctx.fillStyle = '#DFFF00';
  ctx.font = `600 ${12 * scale}px "Plus Jakarta Sans", system-ui, sans-serif`;
  ctx.fillText(`${dist} km  ·  +${gain}m 爬升  ·  ${pts} 轨迹点`, width / 2, y + 62 * scale);
  ctx.textAlign = 'left';

  ctx.restore();
}

async function startExport() {
  DOM.progressWrap.style.display = 'block';
  DOM.btnExportStart.disabled = true;
  DOM.progressStatus.innerText = '正在初始化高清录制引擎...';
  DOM.progressBar.style.width = '5%';
  DOM.progressPercent.innerText = '5%';

  pause();
  seekTo(0.0);

  const recCanvas = setupRecordingCanvas();
  renderCompositeFrame(0, 0, 'intro');
  const stream = recCanvas.captureStream(60);

  let mimeType = 'video/webm;codecs=vp9';
  if (!MediaRecorder.isTypeSupported(mimeType)) {
    mimeType = 'video/webm';
  }

  const recorder = new MediaRecorder(stream, {
    mimeType: mimeType,
    videoBitsPerSecond: 28000000
  });

  const recordedChunks = [];
  recorder.ondataavailable = e => {
    if (e.data.size > 0) recordedChunks.push(e.data);
  };

  recorder.onstop = () => {
    state.isRecording = false;
    DOM.progressStatus.innerText = '生成下载文件...';
    DOM.progressBar.style.width = '100%';
    DOM.progressPercent.innerText = '100%';

    const blob = new Blob(recordedChunks, { type: 'video/mp4' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `trailmov_${state.track.metadata.title}_${state.aspect}.mp4`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);

    setTimeout(() => {
      DOM.modal.classList.remove('active');
      DOM.progressWrap.style.display = 'none';
      DOM.btnExportStart.disabled = false;
      DOM.progressBar.style.width = '0%';
    }, 1500);
  };

  recorder.start();
  play();

  // Progress monitoring
  const checkInterval = setInterval(() => {
    const p = Math.round((state.currentTime / state.duration) * 100);
    DOM.progressBar.style.width = `${Math.min(95, p)}%`;
    DOM.progressPercent.innerText = `${Math.min(95, p)}%`;
    DOM.progressStatus.innerText = `正在高质录制: ${state.currentTime.toFixed(1)}s / ${state.duration}s`;

    if (state.currentTime >= state.duration) {
      clearInterval(checkInterval);
      pause();
      recorder.stop();
    }
  }, 200);
}

// -------------------------------------------------------------
// Fallback Mock Data (Olympic Forest Park)
// -------------------------------------------------------------
function generateFallbackTrack() {
  return {
    metadata: {
      title: "20261101奥森",
      total_points: 5853,
      total_distance_km: 5.97,
      total_gain_m: 173,
      total_loss_m: 178,
      min_ele_m: 27,
      max_ele_m: 72,
      bounds: { min_lon: 116.385, max_lon: 116.415, min_lat: 40.015, max_lat: 40.035 }
    },
    coordinates: [[116.4023, 40.0257, 47], [116.4024, 40.0258, 48]],
    telemetry: { dists_km: [0, 5.97], gains_m: [0, 173], losses_m: [0, 178], eles_m: [47, 42], bearings_deg: [30, 45] },
    profile: [{ dist_km: 0, ele: 47 }, { dist_km: 5.97, ele: 42 }]
  };
}

// Start application
window.addEventListener('DOMContentLoaded', init);
