document.addEventListener('DOMContentLoaded', () => {
  const elements = {
    ipBadge: document.getElementById('ipBadge'),
    ipMirror: document.getElementById('ipMirror'),
    networkBadge: document.getElementById('networkBadge'),
    modeChip: document.getElementById('modeChip'),
    modeButtons: document.querySelectorAll('button.mode'),
    lockStatusDot: document.getElementById('lockStatusDot'),
    lockStatusText: document.getElementById('lockStatusText'),
    unlockButton: document.getElementById('unlockButton'),
    resetButton: document.getElementById('resetButton'),
    partySlider: document.getElementById('partySlider'),
    partyInput: document.getElementById('partyInput'),
    albumArt: document.getElementById('albumArt'),
    trackTitle: document.getElementById('trackTitle'),
    trackArtist: document.getElementById('trackArtist'),
    nextTrack: document.getElementById('nextTrack'),
    progressFill: document.getElementById('progressFill'),
    progressNow: document.getElementById('progressNow'),
    progressTotal: document.getElementById('progressTotal'),
    musicBpm: document.getElementById('musicBpm'),
  };

  const required = Object.values(elements);
  if (required.some((el) => !el)) {
    console.error('Missing required UI elements');
    return;
  }

  const picker = new iro.ColorPicker('#color-picker', {
    width: 320,
    color: 'rgb(255, 255, 255)',
    layout: [
      { component: iro.ui.Wheel },
      { component: iro.ui.Slider, options: { sliderType: 'value' } },
      { component: iro.ui.Slider, options: { sliderType: 'kelvin' } },
    ],
  });

  const API_HEADERS = { 'Content-Type': 'application/x-www-form-urlencoded' };
  let currentMode = 'wifi';
  let colorInFlight = false;
  let latestPayload = null;
  let lastRgb = { r: -1, g: -1, b: -1 };
  let musicData = null;
  let musicTimer = null;
  let progressTimer = null;

  function debounce(fn, wait, minInterval = 40) {
    let timeout;
    let lastCall = 0;
    return (...args) => {
      const now = Date.now();
      const run = () => {
        lastCall = Date.now();
        fn.apply(null, args);
      };
      if (timeout) clearTimeout(timeout);
      if (now - lastCall >= minInterval) {
        run();
      } else {
        timeout = setTimeout(run, wait);
      }
    };
  }

  function setModeChip(mode) {
    const labels = {
      wifi: 'remote',
      party: 'party mode',
      music: 'music visualizer',
      off: 'lights off',
    };
    elements.modeChip.textContent = `Mode: ${labels[mode] || mode}`;
  }

  function highlightMode(mode) {
    elements.modeButtons.forEach((btn) => {
      const isActive = btn.dataset.mode === mode;
      btn.classList.toggle('active', isActive);
    });
    setModeChip(mode);
  }

    function updateNetworkBadges({ ip, apFallback }) {
    elements.ipBadge.innerHTML = `<span class="dot"></span>IP - ${ip || 'unknown'}`;
    elements.networkBadge.textContent = apFallback ? 'Network - fallback hotspot' : 'Network - home Wi-Fi';
    elements.ipMirror.textContent = `Reachable at ${ip || 'static IP'}`;
    const dot = elements.ipBadge.querySelector('.dot');
    if (dot) {
      dot.style.background = apFallback ? '#ffa45c' : '#6de1ff';
      dot.style.boxShadow = apFallback ? '0 0 0 6px rgba(255, 164, 92, 0.18)' : '0 0 0 6px rgba(109, 225, 255, 0.18)';
    }
  }

  function updateLockUI(unlocked) {
    elements.lockStatusDot.classList.toggle('unlocked', unlocked);
    elements.lockStatusText.textContent = unlocked ? 'Full power unlocked' : 'Safe power mode';
    elements.unlockButton.disabled = unlocked;
  }

  function formatTime(ms) {
    if (!ms || ms < 0) return '0:00';
    const totalSec = Math.floor(ms / 1000);
    const m = Math.floor(totalSec / 60);
    const s = (totalSec % 60).toString().padStart(2, '0');
    return `${m}:${s}`;
  }

  function applyMusicUI(data) {
    elements.trackTitle.textContent = data?.track || '--';
    elements.trackArtist.textContent = data?.artist || '--';
    elements.nextTrack.textContent = data?.nextTrack ? `${data.nextTrack} — ${data.nextArtist || ''}` : '--';
    elements.musicBpm.textContent = data?.bpm ? data.bpm.toFixed(1) : '--';
    if (data?.albumArt) {
      elements.albumArt.style.backgroundImage = `url('${data.albumArt}')`;
    } else {
      elements.albumArt.style.backgroundImage = 'none';
    }
    const duration = data?.durationMs || 0;
    const progress = Math.min(data?.progressMs || 0, duration);
    elements.progressFill.style.width = duration > 0 ? `${Math.min(100, (progress / duration) * 100)}%` : '0%';
    elements.progressNow.textContent = formatTime(progress);
    elements.progressTotal.textContent = formatTime(duration);
  }

  function tickProgress() {
    if (!musicData || !musicData.durationMs) return;
    musicData.progressMs = Math.min(musicData.progressMs + 1000, musicData.durationMs);
    applyMusicUI(musicData);
  }

  async function fetchMusic() {
    try {
      const res = await fetch('/api/music');
      if (!res.ok) throw new Error('music failed');
      musicData = await res.json();
      applyMusicUI(musicData);
    } catch (err) {
      console.error('Music fetch failed', err);
    }
  }

  function startMusicPolling() {
    if (!musicTimer) {
      fetchMusic();
      musicTimer = setInterval(fetchMusic, 4000);
    }
    if (!progressTimer) {
      progressTimer = setInterval(tickProgress, 1000);
    }
  }

  function stopMusicPolling() {
    if (musicTimer) {
      clearInterval(musicTimer);
      musicTimer = null;
    }
    if (progressTimer) {
      clearInterval(progressTimer);
      progressTimer = null;
    }
  }

  async function fetchStatus() {
    try {
      const res = await fetch('/api/status');
      if (!res.ok) throw new Error('status failed');
      const data = await res.json();
      if (data.mode) {
        currentMode = data.mode;
        highlightMode(currentMode);
      }
      updateNetworkBadges(data);
      updateLockUI(Boolean(data.unlocked));
      if (typeof data.partyHz === 'number') {
        setPartyInputs(data.partyHz);
      }
      startMusicPolling(); // keep music info visible even outside music mode
    } catch (err) {
      console.error('Status check failed', err);
    }
  }

  async function setMode(mode) {
    try {
      const res = await fetch('/api/mode', {
        method: 'POST',
        headers: API_HEADERS,
        body: `mode=${encodeURIComponent(mode)}`,
      });
      if (!res.ok) throw new Error('mode failed');
      currentMode = mode;
      highlightMode(mode);
      startMusicPolling();
    } catch (err) {
      console.error('Failed to set mode', err);
    }
  }

  const sendColor = debounce(async (color) => {
    if (currentMode !== 'wifi') {
      await setMode('wifi');
    }
    const { r, g, b } = color.rgb;
    if (r === lastRgb.r && g === lastRgb.g && b === lastRgb.b) {
      return;
    }
    lastRgb = { r, g, b };
    latestPayload = `r=${r}&g=${g}&b=${b}`;
    if (colorInFlight) return;
    colorInFlight = true;
    while (latestPayload) {
      const body = latestPayload;
      latestPayload = null;
      try {
        await fetch('/postRGB', {
          method: 'POST',
          headers: API_HEADERS,
          body,
        });
      } catch (err) {
        console.error('Color update failed', err);
      }
    }
    colorInFlight = false;
  }, 180);

  picker.on('color:change', sendColor);

  elements.modeButtons.forEach((btn) => {
    btn.addEventListener('click', () => {
      const mode = btn.dataset.mode;
      setMode(mode);
    });
  });

  elements.unlockButton.addEventListener('click', async () => {
    const confirmed = confirm('Unlock full power? LEDs and heatsinks can run hot—use with care.');
    if (!confirmed) return;
    try {
      const res = await fetch('/unlock', { method: 'POST', headers: API_HEADERS });
      if (!res.ok) throw new Error('unlock failed');
      updateLockUI(true);
    } catch (err) {
      console.error('Unlock failed', err);
      alert('Unlock failed. Check connection.');
    }
  });

  elements.resetButton.addEventListener('click', async () => {
    const confirmed = confirm('Return to safe power mode?');
    if (!confirmed) return;
    try {
      const res = await fetch('/reset', { method: 'POST', headers: API_HEADERS });
      if (!res.ok) throw new Error('reset failed');
      updateLockUI(false);
    } catch (err) {
      console.error('Reset failed', err);
      alert('Reset failed. Check connection.');
    }
  });

  function setPartyInputs(hz) {
    const clamped = Math.min(Math.max(hz, 0.05), 5);
    elements.partySlider.value = clamped;
    elements.partyInput.value = clamped.toFixed(1);
  }

  async function pushPartyHz(hz) {
    try {
      await fetch('/api/party', {
        method: 'POST',
        headers: API_HEADERS,
        body: `hz=${hz}`,
      });
    } catch (err) {
      console.error('Failed to set party Hz', err);
    }
  }

  const syncParty = debounce((value) => {
    const hz = parseFloat(value);
    if (!isFinite(hz)) return;
    setPartyInputs(hz);
    pushPartyHz(hz);
    if (currentMode !== 'party') {
      setMode('party');
    }
  }, 250);

  elements.partySlider.addEventListener('input', (e) => {
    syncParty(e.target.value);
  });

  elements.partyInput.addEventListener('change', (e) => {
    syncParty(e.target.value);
  });

  // Always keep now-playing data in sync
  startMusicPolling();
  fetchStatus();
  setInterval(fetchStatus, 6000);
});

