# Music proxy (Spotify companion)

Small Express server that handles Spotify OAuth refresh and exposes a single `/playback` endpoint for the ESP32 web UI.

## Setup
1) Create a Spotify app at https://developer.spotify.com/dashboard and note the Client ID/Secret.
2) Obtain a refresh token with scopes: `user-read-playback-state user-read-currently-playing`.
   - You can use the Spotify Web Console or any OAuth helper to perform the Authorization Code flow once.
3) Set environment variables:
```
SPOTIFY_CLIENT_ID=your_client_id
SPOTIFY_CLIENT_SECRET=your_client_secret
SPOTIFY_REFRESH_TOKEN=your_refresh_token
PORT=3000
```
4) Install and run:
```
cd proxy
npm install
npm start
```

The ESP32 should be pointed at `http://<your-machine-ip>:3000` in `lib/WiFiManager/WiFiManager.h` (`musicProxyBase`).

## Response shape
`GET /playback` returns:
```
{
  track: "Song title",
  artist: "Artist",
  albumArt: "https://.../image.jpg",
  durationMs: 0,
  progressMs: 0,
  bpm: 120,
  nextTrack: "Next song",
  nextArtist: "Next artist"
}
```
