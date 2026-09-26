# Wii U Rich Presence Plugin

> [!IMPORTANT]
> **This is a personal fork of [FlamingNineteen/RichPresenceWUPS](https://github.com/FlamingNineteen/RichPresenceWUPS), made for my own use.** The changes in this fork were written with AI ([Claude Code](https://claude.com/claude-code)). If you choose to use it, you do so at your own risk, and **please don't report problems with this fork to the original author**. They didn't write or review these changes.
>
> The original plugin only works when the Wii U and the computer running Discord are on the same local network. I made this fork so my Discord status works from anywhere, by sending updates over the internet to my own server through a Cloudflare Tunnel.
>
> **Changes in this fork:**
> - **Remote server mode:** the plugin can send updates over HTTP to a domain or IP address set in its **Remote Server** settings, instead of broadcasting over UDP. This works through a Cloudflare Tunnel. Local UDP mode still works as before. See [Remote server](#remote-server-cloudflare-tunnel).
> - **Text entry in the plugin settings:** type the server's domain and secret with the D-pad.
> - **Signed updates:** each update is signed with a shared secret (HMAC-SHA256) and a timestamp, so the secret is never sent, and the computer application rejects unsigned, altered or replayed updates.
> - **HTTP listener in the computer application and Python script:** new `--http-port`, `--http-bind` and `--http-secret` options. The log shows whether each update arrived over UDP or HTTP, and from where.
> - **New Discord layout:** "Wii U" as the activity name, the game as the first line, the connected controllers as the party count, and your PNID/NNID when hovering the network icon. Your member list status still shows "Playing *game*".
> - **Fixes:** the "Show network ID" setting now works, game titles with quotes no longer break updates, and logs are written line by line on macOS/Linux so they show up when running as a service.
> - **Not working yet:** showing the game's friend list text (like "In the menus"). The code is included but disabled, because enabling it stopped the console from booting.

This plugin uses UDP to communicate with an application on your computer to set Discord Rich Presence for the user. The activity is set based on the application currently being played, the time the application was loaded, the amount of controllers connected and more.

## Installation

<details>
<summary><b>Windows</b></summary>
(`[ENVIRONMENT]` is a placeholder for the actual environment name.)

1. Download both the `RichPresence.wps` plugin and the `WURP-Windows` executable from the [Releases page](https://github.com/FlamingNineteen/RichPresenceWUPS/releases).
2. Copy `RichPresence.wps` into `sd:/wiiu/environments/[ENVIRONMENT]/plugins`.
    - Requires the [WiiUPluginLoaderBackend](https://github.com/wiiu-env/WiiUPluginLoaderBackend) in `sd:/wiiu/environments/[ENVIRONMENT]/modules`.
3. Keep the executable on your computer.
</details>

<details>
<summary><b>macOS</b></summary>
(`[ENVIRONMENT]` is a placeholder for the actual environment name.)

1. Download both the `RichPresence.wps` plugin and the `WURP-macOS` binary from the [Releases page](https://github.com/FlamingNineteen/RichPresenceWUPS/releases).
2. Copy `RichPresence.wps` into `sd:/wiiu/environments/[ENVIRONMENT]/plugins`.
    - Requires the [WiiUPluginLoaderBackend](https://github.com/wiiu-env/WiiUPluginLoaderBackend) in `sd:/wiiu/environments/[ENVIRONMENT]/modules`.
3. Keep the binary on your computer.
</details>

<details>
<summary><b>Linux</b></summary>
(`[ENVIRONMENT]` is a placeholder for the actual environment name.)  

1. Download both the `RichPresence.wps` plugin and the `WURP-Linux` binary from the [Releases page](https://github.com/FlamingNineteen/RichPresenceWUPS/releases).
2. Copy `RichPresence.wps` into `sd:/wiiu/environments/[ENVIRONMENT]/plugins`.
    - Requires the [WiiUPluginLoaderBackend](https://github.com/wiiu-env/WiiUPluginLoaderBackend) in `sd:/wiiu/environments/[ENVIRONMENT]/modules`.
3. Keep the binary on your computer.

**If you are using a systemd-based system and would like to have the application run on startup, follow these next steps:**

5. Download the `install.sh` script from the [repository](https://github.com/FlamingNineteen/RichPresenceWUPS/blob/main/install.sh)
6. make `install.sh` executable with this:
```bash
chmod +x ./install.sh
```
7. Place the `WURP-Linux` binary into the same directory as `install.sh`.  
8. Run the installation script (this installs the binary as a systemd user process)  
```bash
./install.sh
```
</details>

<details>
<summary><b>Python Script</b></summary>
(`[ENVIRONMENT]` is a placeholder for the actual environment name.)  

1. Download the `RichPresence.wps` plugin from the [Releases page](https://github.com/FlamingNineteen/RichPresenceWUPS/releases).
2. Copy `RichPresence.wps` into `sd:/wiiu/environments/[ENVIRONMENT]/plugins`.
    - Requires the [WiiUPluginLoaderBackend](https://github.com/wiiu-env/WiiUPluginLoaderBackend) in `sd:/wiiu/environments/[ENVIRONMENT]/modules`.
3. Download the `discord-script.py` file from the [repository](https://github.com/FlamingNineteen/RichPresenceWUPS/blob/main/discord-script.py).
4. Make sure that [`pypresence`](https://github.com/qwertyquerty/pypresence) and `requests` are installed by running the following command:
```bash
pip install requests pypresence
```
</details>

> [!NOTE]
> After installing, if Discord incorrectly reflects the amount of time you have been playing, open the plugin configuration menu and change the "Offset 'elapsed time' timezone for correct display" setting. If elapsed time displays `0:00:00`, you need to change the setting to a negative number. If the elapsed time displays hours ahead of your actual play time, you need to change the setting to a positive number.

## Usage
Start your Wii U with the environment you placed the plugin in, and run the executable, binary, or Python file with the Discord app open.

You can enter the plugin's configuration settings to change the way presence is displayed.

Additionally, you can specify command-line arguments for advanced customization:
- `-a, --app-id <APPLICATION ID>`: Set the application ID of the Discord app to connect to.
- `-p, --port <PORT>`: Set the UDP port number. Make sure the port matches the one set in the plugin's configuration settings.
- `-H, --http-port <PORT>`: Also listen for data over HTTP on this TCP port. Disabled by default. See [Remote server](#remote-server-cloudflare-tunnel).
- `-b, --http-bind <ADDRESS>`: Set the address the HTTP listener binds to (default `127.0.0.1`).
- `-s, --http-secret <SECRET>`: Only accept HTTP updates signed with this secret. Set the same secret in the plugin's **Remote Server** settings.
- `-r, --repo <DOMAIN/PATH>`: Set the link to the image repository. Omit http(s):// in the link, or the fetch will fail.
- `-v, --version`: Print the version number upon startup.
- `-w, --windows-logs`: Create a window to show logs on Windows.

## Remote server (Cloudflare Tunnel)
By default, the plugin broadcasts over UDP, so the Wii U and the computer must be on the same local network. To reach a computer on a different network, the plugin can instead send data over HTTP to a domain or IP address, for example one served by a [Cloudflare Tunnel](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/). Cloudflare Tunnels only forward HTTP, not UDP.

1. On the computer running Discord, start the application with the HTTP listener enabled and a secret of your choice (lowercase letters, numbers, `.` and `-` only, since that's what the Wii U can type):
```bash
WURP-Linux --http-port 5005 --http-secret your-secret-here
```
2. Create a tunnel that routes a hostname to `http://localhost:5005`, either in the Cloudflare dashboard (Zero Trust → Networks → Tunnels → add a public hostname with service type `HTTP` and URL `localhost:5005`) or with `cloudflared`:
```bash
cloudflared tunnel create wurp
```
```bash
cloudflared tunnel route dns wurp wiiu.example.com
```
Then add the route to `~/.cloudflared/config.yml` (use the tunnel ID printed by `tunnel create`) and run `cloudflared tunnel run wurp`:
```yaml
tunnel: wurp
credentials-file: /home/you/.cloudflared/<TUNNEL-ID>.json
ingress:
  - hostname: wiiu.example.com
    service: http://localhost:5005
  - service: http_status:404
```
3. The Wii U sends plain HTTP (it doesn't use HTTPS), so the hostname must accept HTTP on port 80. If **Always Use HTTPS** is enabled for your zone, add a Configuration Rule that turns it off for this hostname. **Bot Fight Mode** can also block the Wii U's requests.
4. Visit `http://wiiu.example.com` in a browser. It should say `Wii U Rich Presence is running.`
    - The log shows how each update arrived: `Received via HTTP from <address>` for updates through the tunnel (with the Wii U's public IP address), and `Received via UDP from <address>` for updates on the local network.
5. On the Wii U, open the plugin configuration menu, go to **Remote Server**, enable **Send data to a remote server**, and enter your domain (or IP address) in **Server domain or IP**. Keep the HTTP port at `80` for Cloudflare, and enter the same secret in **Secret**.
    - To edit the domain, press A, then use Left/Right to move the cursor and Up/Down to change the character. X deletes a character, Y clears the field, A saves, and B cancels.

> [!WARNING]
> Updates are sent without encryption, so anyone who can watch the Wii U's network traffic can see what you're playing (the same details Discord shows). The secret itself is never sent: each update is signed with it (HMAC-SHA256) along with a timestamp, and the application rejects unsigned, altered, or replayed updates. If you set the Wii U's clock back, restart the application.

## Contribute
The plugin might be missing images from a few Wii U games. If you are interested in adding game images, and have a Github account, check out the [image repository](https://github.com/flamingnineteen/RichPresenceWUPS-DB) for this plugin.

## Building
For specifics on building either the plugin or the binary, please check their respective directories:
- Plugin: [wiiu/README.md](https://github.com/FlamingNineteen/RichPresenceWUPS/blob/main/wiiu/README.md)
- Binary: [discord/README.md](https://github.com/FlamingNineteen/RichPresenceWUPS/blob/main/discord/README.md)
