"""One-time Twitch OAuth (authorization code flow) to get a clips:edit user token.
1. In https://dev.twitch.tv/console/apps set redirect URL to http://localhost:3000
2. Fill client_id / client_secret in config.yaml, run this, log in in the browser."""
import http.server, urllib.parse, webbrowser, requests, yaml

cfg = yaml.safe_load(open("config.yaml", encoding="utf-8"))
tw = cfg["twitch"]
REDIRECT = "http://localhost:3000"
url = ("https://id.twitch.tv/oauth2/authorize?response_type=code"
       f"&client_id={tw['client_id']}&redirect_uri={REDIRECT}&scope=clips:edit")
print("Opening browser:", url)
webbrowser.open(url)


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        code = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query).get("code", [None])[0]
        r = requests.post("https://id.twitch.tv/oauth2/token", data={
            "client_id": tw["client_id"], "client_secret": tw["client_secret"],
            "code": code, "grant_type": "authorization_code", "redirect_uri": REDIRECT}).json()
        tw["access_token"], tw["refresh_token"] = r["access_token"], r["refresh_token"]
        who = requests.get("https://api.twitch.tv/helix/users", headers={
            "Client-Id": tw["client_id"], "Authorization": f"Bearer {r['access_token']}"}).json()["data"][0]["login"]
        print(f"Token belongs to: {who}" + ("" if who == tw.get("clipper_login", who) else
              f"   <-- WARNING: expected {tw['clipper_login']}, log out and rerun"))
        yaml.safe_dump(cfg, open("config.yaml", "w", encoding="utf-8"), sort_keys=False)
        self.send_response(200); self.end_headers(); self.wfile.write(b"Token saved to config.yaml. Close this tab.")
        raise SystemExit


http.server.HTTPServer(("localhost", 3000), H).serve_forever()
