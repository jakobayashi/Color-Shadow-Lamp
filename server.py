from flask import Flask, request
import requests
import os

app = Flask(__name__)

# TODO: put your actual values here or load from env
CLIENT_ID = os.environ.get("SPOTIFY_CLIENT_ID", "ffbee24354484a15882bd2132ece5ba6")
CLIENT_SECRET = os.environ.get("SPOTIFY_CLIENT_SECRET", "dac6d5b7b5f04932a1dde899dadd4dee")
REDIRECT_URI = os.environ.get("SPOTIFY_REDIRECT_URI", "https://mahalia-hemiparetic-misha.ngrok-free.dev/callback")

@app.route("/")
def home():
    return "Spotify auth helper is running."

@app.route("/callback")
def callback():
    code = request.args.get("code")
    error = request.args.get("error")

    if error:
        return f"Error from Spotify: {error}"

    if not code:
        return "No code parameter in query string."

    # Exchange code for tokens
    token_url = "https://accounts.spotify.com/api/token"
    data = {
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": REDIRECT_URI,
        "client_id": CLIENT_ID,
        "client_secret": CLIENT_SECRET,
    }

    r = requests.post(token_url, data=data)
    if r.status_code != 200:
        return f"Token request failed: {r.status_code} {r.text}"

    tokens = r.json()
    access_token = tokens.get("access_token")
    refresh_token = tokens.get("refresh_token")

    return f"""
    <h1>Tokens received!</h1>
    <p><b>access_token</b>: {access_token}</p>
    <p><b>refresh_token</b>: {refresh_token}</p>
    <p>Save this refresh_token as your SPOTIFY_REFRESH_TOKEN.</p>
    """
    
if __name__ == "__main__":
    app.run(port=3000, debug=True)
