#!/usr/bin/env python3
import asyncio
import hashlib
import hmac
import json
import requests
import socket
import sys
import threading
import time

from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pypresence import Presence
from pypresence.types import ActivityType, StatusDisplayType

VERSION = 2.3
APP_ID = "1353248127469228074"
REPO = "raw.githubusercontent.com/flamingnineteen/richpresencewups-db/main"
PORT = 5005
HTTP_PORT = 0 # 0 disables the HTTP listener
HTTP_BIND = "127.0.0.1"
HTTP_SECRET = "" # The secret the Wii U must send over HTTP. Empty accepts any request.

# Check for command line arguments
i=1
while i < len(sys.argv):
    if (sys.argv[i] in ["-v", "--version"]):
        print(f"Wii U Rich Presence v{VERSION}")
    elif i+1 < len(sys.argv):
        i+=1
        if (sys.argv[i-1] in ["-r", "--repo"]):
            REPO = sys.argv[i]
            print(f"Using repository {REPO}.")
        elif (sys.argv[i-1] in ["-a", "--app-id"]):
            APP_ID = sys.argv[i]
            print(f"Using repository {APP_ID}.")
        elif (sys.argv[i-1] in ["-p", "--port"]):
            PORT = int(sys.argv[i])
            print(f"Using port {PORT}.")
        elif (sys.argv[i-1] in ["-H", "--http-port"]):
            HTTP_PORT = int(sys.argv[i])
            print(f"Using HTTP port {HTTP_PORT}.")
        elif (sys.argv[i-1] in ["-b", "--http-bind"]):
            HTTP_BIND = sys.argv[i]
            print(f"Using HTTP bind address {HTTP_BIND}.")
        elif (sys.argv[i-1] in ["-s", "--http-secret"]):
            HTTP_SECRET = sys.argv[i]
            print("Using an HTTP secret.")
    i+=1

# Connect to Discord
client = Presence(client_id = APP_ID)
disconnected = True
while disconnected:
    try:
        client.connect()
        disconnected = False
    except:
        print("Failed to connect to Discord. Retrying...")
        time.sleep(2)
print("Connected to Discord")

# Bind Socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
binded = False
while not binded:
    try:
        sock.bind(('', PORT))
        binded = True
        print(f"Binded to UDP port {PORT}")
    except:
        print(f"Failed to bind to UDP port {PORT}. Is another program using the port? Retrying...")
        time.sleep(2)

# Recieve Image URLs
titles = {}
try:
    req = requests.get(f"http://{REPO}/titles.json")
    titles = json.loads(req.text)
    print("Successfully fetched titles.json!")
except:
    print("Error fetching titles.json. Using default image.")

# Parses the recieved json into a json object
def parse(msg):
    try:
        i = json.loads(msg)
    except:
        i = {}
    return i

# Change the recieved time elapsed to epoch
def toepoch(e, dst = False):
    dt = (datetime.now() if dst else datetime.utcnow()).astimezone()
    return e - int(dt.utcoffset().total_seconds())

# Asynchronous function to stop Rich Presence if nothing is recieved
idle = True

async def clearPresence():
    global idle, client
    allow = False
    while 1:
        await asyncio.sleep(5)
        if idle:
            if allow:
                await asyncio.to_thread(client.clear)
                print("Cleared Rich Presence")
                while idle:
                    await asyncio.sleep(0.1)
            else:
                allow = True
        else:
            allow = False
        idle = True

# Discord rejects text shorter than 2 or longer than 128 bytes, so shorten it or leave it out
def fit_discord_text(text):
    if len(text.encode()) < 2:
        return None
    while len(text.encode()) > 128:
        text = text[:-4] + "..."
    return text

# Sets Rich Presence from a received message
updateMsg = False
updateLock = threading.Lock() # Messages can arrive over UDP and HTTP at the same time

def handle(msg, source):
    global idle, updateMsg
    try:
        data = parse(msg.decode())
        print(f"Recieved via {source}: {data}")
        idle = False
    except:
        print("Failed to parse message")
        return

    # Attempt to set Rich Presence
    try:
        if (data["sender"] == "Wii U"):
            image = ""
            try:
                image = f"http://{REPO}/icons/{titles[data["long"]]}"
            except:
                image = "preview"
            
            img     = data['img'] if 'img' in data else ''
            dst     = (data['dst'] == 1) if 'dst' in data else False
            details = data['details'] if 'details' in data else ''

            # The small image shows the network, and hovering it shows the account's ID
            network = "Nintendo Network" if img == "nn" else "Pretendo Network"
            nnid    = data["nnid"]
            network_text = f"Using {network}" if nnid == "" else f"{"NNID" if img == "nn" else "PNID"}: {nnid}"

            # The party count shows the connected controllers next to the game's own text, like "In the menus (1 of 4)".
            # Discord only shows it alongside some text, so fall back to a label.
            show_party = data["ctrls"] >= 0
            state = fit_discord_text(details)
            if state is None and show_party:
                state = "Controllers"

            with updateLock:
                client.update(
                    name=                "Wii U",
                    activity_type=       ActivityType.PLAYING,
                    status_display_type= StatusDisplayType.DETAILS, # "Playing <game>" in the member list
                    details=             fit_discord_text(data["app"]),
                    state=               state,
                    start=               toepoch(data["time"], dst),
                    large_image=         image,
                    large_text=          fit_discord_text(data["long"]),
                    small_image=         None if img == "" else img,
                    small_text=          None if img == "" else network_text,
                    party_size=          [data["ctrls"] + 1, 4 if data["ctrls"] < 4 else 8] if show_party else None,
                    instance=            False
                )

            print("Updated Rich Presence")

            if "compatibility" in data:
                if data["compatibility"] > VERSION and not updateMsg:
                    print(f'A new update is available: v{data["compatibility"]}')
                    updateMsg = True
    except:
        print("Failed to update Rich Presence")

# Largest difference allowed between the Wii U's clock and this computer's, in seconds.
# The Wii U's clock is in local time, so this allows for any time zone.
MAX_CLOCK_DIFFERENCE = 25 * 60 * 60

# Timestamp of the last accepted update, so requests can't be replayed
lastTimestamp = 0
timestampLock = threading.Lock()

# Checks an update's signature. Returns None if it's valid, or the reason it isn't.
def check_signature(headers, body):
    global lastTimestamp
    timestamp = headers.get("X-WURP-Timestamp", "")
    signature = headers.get("X-WURP-Signature", "")
    if timestamp == "" or signature == "":
        return "missing signature (set the secret on the Wii U)"

    expected = hmac.new(HTTP_SECRET.encode(), timestamp.encode() + b"\n" + body, hashlib.sha256).hexdigest()
    if not hmac.compare_digest(signature.encode(), expected.encode()):
        return "wrong signature (check that the secret matches)"

    try:
        ts = int(timestamp)
    except ValueError:
        return "invalid timestamp"

    if abs(ts - time.time()) > MAX_CLOCK_DIFFERENCE:
        return "timestamp too far from this computer's time (check the Wii U's clock)"
    with timestampLock:
        if ts <= lastTimestamp:
            return "timestamp not newer than the last update (a replayed request, or the Wii U's clock was set back; restart this script if so)"
        lastTimestamp = ts
    return None

# Receives data over HTTP, e.g. from a Wii U outside the local network through a Cloudflare Tunnel
class HTTPHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", 0))
        except ValueError:
            length = -1
        if length < 0 or length > 16384:
            self.send_response(400)
            self.end_headers()
            return
        body = self.rfile.read(length)

        # Cloudflare passes along the address of the Wii U
        source = f"HTTP from {self.headers.get('CF-Connecting-IP', 'local network')}"

        rejection = check_signature(self.headers, body) if HTTP_SECRET != "" else None
        if rejection:
            self.send_response(401)
            self.end_headers()
            print(f"Rejected {source}: {rejection}")
            return

        self.send_response(204)
        self.end_headers()
        handle(body, source)

    # Lets the connection be tested from a browser
    def do_GET(self):
        body = b"Wii U Rich Presence is running."
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass

if HTTP_PORT != 0:
    httpd = ThreadingHTTPServer((HTTP_BIND, HTTP_PORT), HTTPHandler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    print(f"Listening for HTTP on {HTTP_BIND}:{HTTP_PORT}")
    if HTTP_SECRET == "":
        print("Warning: no HTTP secret is set, so anyone who can reach this server can update your status")

# Main loop
async def main():
    while 1:
        # Wait for a message
        msg, addr = await asyncio.to_thread(sock.recvfrom, 1024)
        await asyncio.to_thread(handle, msg, f"UDP from {addr[0]}")

async def run_all():
    await asyncio.gather(
        clearPresence(),
        main()
    )

asyncio.run(run_all())

sock.close()
