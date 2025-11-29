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
  };

  const required = [elements.ipBadge, elements.networkBadge, elements.modeChip, elements.lockStatusDot, elements.lockStatusText, elements.unlockButton, elements.resetButton, elements.partySlider, elements.partyInput];
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
      rgb: 'manual knobs',
      ltt: 'warm/cool mix',
      party: 'party mode',
      off: 'lights off',
    };
    elements.modeChip.textContent = `Mode: ${labels[mode] || mode}`;
  }

  function highlightMode(mode) {
    elements.modeButtons.forEach(btn => {
      const isActive = btn.dataset.mode === mode;
      btn.classList.toggle('active', isActive);
    });
    setModeChip(mode);
  }

  function updateNetworkBadges({ ip, apFallback }) {
    elements.ipBadge.innerHTML = `<span class="dot"></span>IP — ${ip || 'unknown'}`;
    if (apFallback) {
      elements.networkBadge.textContent = 'Network — fallback hotspot';
    } else {
      elements.networkBadge.textContent = 'Network — home Wi‑Fi';
    }
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

  elements.modeButtons.forEach(btn => {
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

  fetchStatus();
  setInterval(fetchStatus, 6000);
});
