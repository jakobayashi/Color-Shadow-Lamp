import express from 'express';
import fetch from 'node-fetch';

const {
  SPOTIFY_CLIENT_ID = "ffbee24354484a15882bd2132ece5ba6",
  SPOTIFY_CLIENT_SECRET = "dac6d5b7b5f04932a1dde899dadd4dee",
  SPOTIFY_REFRESH_TOKEN = "AQBwOW44L0N4d1lxNxc43jcZikfPXMCO7E7tQR7BcMIiDRXnETCSccxz_kF6_NzkVA7PMy-HGCFDfEI34oUxmVAfeOs2-ZfxcS10mF9F_ChIOJNRA2euMtTSTM8YBc2pk28",
  PORT = 3000,
} = process.env;

if (!SPOTIFY_CLIENT_ID || !SPOTIFY_CLIENT_SECRET || !SPOTIFY_REFRESH_TOKEN) {
  console.error('Missing Spotify env vars (SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN)');
  process.exit(1);
}

const app = express();
let cachedToken = { access_token: null, expires_at: 0 };
let cachedFeatures = { trackId: null, tempo: null, updatedAt: 0 };

async function getAccessToken() {
  const now = Date.now();
  if (cachedToken.access_token && now < cachedToken.expires_at - 10_000) {
    return cachedToken.access_token;
  }

  const body = new URLSearchParams({
    grant_type: 'refresh_token',
    refresh_token: SPOTIFY_REFRESH_TOKEN,
  });

  const basic = Buffer.from(`${SPOTIFY_CLIENT_ID}:${SPOTIFY_CLIENT_SECRET}`).toString('base64');
  const res = await fetch('https://accounts.spotify.com/api/token', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
      Authorization: `Basic ${basic}`,
    },
    body,
  });

  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Token refresh failed: ${res.status} ${text}`);
  }

  const json = await res.json();
  cachedToken = {
    access_token: json.access_token,
    expires_at: Date.now() + json.expires_in * 1000,
  };
  return cachedToken.access_token;
}

async function spotifyGet(path) {
  const token = await getAccessToken();
  const res = await fetch(`https://api.spotify.com/v1${path}`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Spotify GET ${path} failed: ${res.status} ${text}`);
  }
  return res.json();
}

async function spotifyGetAudioFeatures(trackId) {
  if (!trackId) return null;

  // Basic single-item cache to avoid hammering the endpoint (helps with 429/403)
  if (cachedFeatures.trackId === trackId && cachedFeatures.tempo && Date.now() - cachedFeatures.updatedAt < 5 * 60_000) {
    return { tempo: cachedFeatures.tempo, cached: true };
  }

  try {
    const json = await spotifyGet(`/audio-features/${trackId}`);
    if (json?.tempo) {
      cachedFeatures = { trackId, tempo: json.tempo, updatedAt: Date.now() };
    }
    return json;
  } catch (err) {
    console.warn(`Audio features unavailable for ${trackId}: ${err.message}`);
    return null;
  }
}

function getTrackId(item) {
  return item?.id || item?.linked_from?.id || (item?.uri ? item.uri.split(':').pop() : null);
}

app.get('/playback', async (_req, res) => {
  try {
    const playback = await spotifyGet('/me/player/currently-playing');
    const queue = await spotifyGet('/me/player/queue');

    const item = playback?.item || {};
    const next = queue?.queue?.[0] || {};
    const trackId = getTrackId(item);

    let bpm = null;
    if (trackId && item.type === 'track') {
      const features = await spotifyGetAudioFeatures(trackId);
      bpm = features?.tempo || null;
    }

    const payload = {
      track: item.name || '',
      artist: (item.artists || []).map(a => a.name).join(', '),
      albumArt: item.album?.images?.[0]?.url || '',
      durationMs: item.duration_ms || 0,
      progressMs: playback?.progress_ms || 0,
      bpm,
      nextTrack: next.name || '',
      nextArtist: (next.artists || []).map(a => a.name).join(', '),
    };

    res.json(payload);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: err.message });
  }
});

app.listen(PORT, '0.0.0.0', () => {
  console.log(`Music proxy running on http://localhost:${PORT}/playback`);
});
