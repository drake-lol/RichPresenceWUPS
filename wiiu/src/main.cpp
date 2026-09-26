#include <filesystem>
#include <fstream>
#include <malloc.h>
#include <string.h>
#include <thread>

#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mocha/mocha.h>

#include "config.hpp"
#include "gamemode.hpp"
#include "hmac.hpp"
#include "utils.hpp"

/**
    Mandatory plugin information.
    If not set correctly, the loader will refuse to use the plugin.
**/
WUPS_PLUGIN_NAME("RichPresence");
WUPS_PLUGIN_DESCRIPTION("Discord Rich Presence for the Wii U.");
WUPS_PLUGIN_VERSION(VERSION);
WUPS_PLUGIN_AUTHOR("Flaming19");
WUPS_PLUGIN_LICENSE("GPL");

WUPS_USE_WUT_DEVOPTAB();           // Use the wut devoptabs
WUPS_USE_STORAGE("rich_presence"); // Unique id for the storage api

// Create miscellanious variables
std::jthread tthread;
int elapsed;
std::string app    = "";
std::string preapp = "quantum random!!!11!";

bool INKAY_EXISTS;
std::string INKAY_CONFIG;

// Broadcast over port 5005
void Broadcast(const std::string& json) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return;

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    sockaddr_in dest {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(config.port.value);

    dest.sin_addr.s_addr = inet_addr(config.ip_filter.value ? IpToString(config.ip.value).c_str() : "255.255.255.255");

    sendto(sock, json.c_str(), json.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    close(sock);
}

// Waits until a socket is ready to read or write, up to a timeout
bool WaitSocket(int sock, bool write, int seconds) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);
    timeval tv {seconds, 0};
    return select(sock + 1, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv) > 0;
}

// Signs the body with the secret, so the secret itself is never sent.
// The timestamp only has to increase between updates, so the Wii U's clock doesn't need to be accurate.
std::string SignatureHeaders(const std::string& body) {
    if (config.secret.value.empty()) return "";

    // The server rejects timestamps that aren't newer than the last one, so never repeat one
    static time_t lastTimestamp = 0;
    lastTimestamp = std::max(time(NULL), lastTimestamp + 1);

    std::string timestamp = std::to_string(lastTimestamp);
    return "X-WURP-Timestamp: " + timestamp + "\r\n"
           "X-WURP-Signature: " + Hmac::Sign(config.secret.value, Hmac::Message(timestamp, body)) + "\r\n";
}

// Cache of the last resolved server address, so DNS isn't queried every update
std::string resolvedHost = "";
in_addr resolvedAddr {};

// Send over HTTP to a remote server (e.g. through a Cloudflare Tunnel)
void PostToServer(const std::string& json) {
    const std::string& host = config.server.value;

    // Resolve the domain, or use it directly if it is an IP address
    if (host != resolvedHost) {
        if (inet_aton(host.c_str(), &resolvedAddr) == 0) {
            hostent* he = gethostbyname(host.c_str());
            if (he == nullptr || he->h_addr_list[0] == nullptr) return;
            memcpy(&resolvedAddr, he->h_addr_list[0], sizeof(resolvedAddr));
        }
        resolvedHost = host;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return;

    sockaddr_in dest {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(config.server_port.value);
    dest.sin_addr = resolvedAddr;

    // Connect without blocking so an unreachable server can't stall the plugin
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
    connect(sock, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    int err = 0;
    socklen_t len = sizeof(err);
    if (!WaitSocket(sock, true, 3) || getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
        resolvedHost = ""; // Resolve again next time in case the address changed
        close(sock);
        return;
    }

    std::string hostHeader = host + (config.server_port.value == 80 ? "" : ":" + std::to_string(config.server_port.value));
    std::string request =
        "POST / HTTP/1.1\r\n"
        "Host: " + hostHeader + "\r\n"
        "User-Agent: WiiURichPresence/" VERSION "\r\n"
        "Content-Type: application/json\r\n" +
        SignatureHeaders(json) +
        "Content-Length: " + std::to_string(json.size()) + "\r\n"
        "Connection: close\r\n\r\n" + json;

    size_t sent = 0;
    while (sent < request.size() && WaitSocket(sock, true, 3)) {
        ssize_t n = send(sock, request.c_str() + sent, request.size() - sent, 0);
        if (n <= 0) break;
        sent += n;
    }

    // Wait briefly for the response so the request isn't cut off
    char buffer[256];
    if (sent == request.size() && WaitSocket(sock, false, 3)) recv(sock, buffer, sizeof(buffer), 0);

    close(sock);
}

// Escapes text for use in a JSON string
std::string JsonEscape(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

// Main background loop to broadcast current info
void GameLoop(std::stop_token stoken) {
    int ctrls;
    std::string details, nnid, network, json;

    while (!stoken.stop_requested() && config.enabled.value) {
        if (app != "") {
            // Get controller count
            switch (config.ctrl.value) {
                case NOCTRLCOUNT:
                    ctrls = -2;
                    break;
                case CTRLCOUNT:
                    ctrls = GetCtrlNum();
                    break;
                case CTRLCOUNTNODRC:
                    ctrls = GetCtrlNum()-1;
                    break;
                case CTRLCOUNTMETA:
                    ctrls = std::stoi(GetXmlTag("drc_use")) > 1 ? 0 : GetCtrlNum() + std::stoi(GetXmlTag("drc_use")) - 1;
                    break;
                default:
                    ctrls = -2;
            }

            // Get Network ID
            switch (config.net_id.value) {
                case NONETDISPLAY:
                    nnid = "";
                    break;
                case NETDISPLAYMETA:
                    nnid = std::stoi(GetXmlTag("online_account_use")) ? GetNetworkId() : "";
                    break;
                case NETDISPLAY:
                    nnid = GetNetworkId();
                    break;
                default:
                    nnid = "";
            }

            // Get currently used network
            switch (config.small_img.value) {
                case NONETDISPLAY:
                    network = "";
                    break;
                case NETDISPLAYMETA:
                    network = std::stoi(GetXmlTag("online_account_use")) ? GetNetwork(INKAY_EXISTS, INKAY_CONFIG) : "";
                    break;
                case NETDISPLAY:
                    network = GetNetwork(INKAY_EXISTS, INKAY_CONFIG);
                    break;
                default:
                    network = "";
            }

            // What the game shows on the friend list, like "In the menus"
            details = GetGameModeDescription();

            // if (ReplaceSlashN(GetXmlTag("longname_en")) == "Super Smash Bros. for Wii U") {
            //     details = std::to_string(ReadFromMemory(0x1098B2AB)>>24) + " | " + std::to_string(ReadFromMemory(0x1098EDEB)>>24);
            // }

            // Prepare and send json
            json = "{\"sender\":\"Wii U\",\"long\":\"" + JsonEscape(ReplaceSlashN(GetAppTitle(ENGLISH, true))) + "\",\"app\":\"" + JsonEscape(app) + "\",\"details\":\"" + JsonEscape(details) + "\",\"time\":" + std::to_string(elapsed + (config.timeset.value * 3600)) + ",\"ctrls\":" + std::to_string(ctrls) + ",\"nnid\":\"" + nnid + "\",\"img\":\"" + network + "\",\"dst\":" + std::to_string(config.dst.value) + ",\"compatibility\":" + std::to_string(COMPATIBLE_VERSION) + "}";
            if (config.remote.value && !config.server.value.empty()) PostToServer(json);
            else Broadcast(json);
        }

        // Five second interval
        for (int i=0; i<5 && !stoken.stop_requested() && config.enabled.value; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
    return;
}

void ConfigMenuClosedCallback() {
    WUPSStorageAPI::SaveStorage();

    app = GetXmlTag("shortname_en") == "Health and Safety Information" ? "Homebrew Application" : ReplaceSlashN(GetAppTitle(config.lang.value, config.title.value));
    preapp = app;

    if (tthread.joinable()) {
        tthread.request_stop();
        tthread.join(); // Wait for thread to finish before starting a new one
    }

    if ((config.enabled.value && !(config.cod.value && app.find("Call of Duty") != std::string::npos))) {
        tthread = std::jthread(GameLoop);
    }
}

INITIALIZE_PLUGIN() {
    Mocha_InitLibrary();

    WUPSConfigAPIOptionsV1 configOptions = {.name = "Rich Presence"};
    WUPSConfigAPI_Init(configOptions, ConfigMenuOpenedCallback, ConfigMenuClosedCallback);
    WUPSStorageAPI::GetOrStoreDefault(config.enabled.id, config.enabled.value, config.enabled.def);
    WUPSStorageAPI::GetOrStoreDefault(config.net_id.id, config.net_id.value, config.net_id.def);
    WUPSStorageAPI::GetOrStoreDefault(config.timeset.id, config.timeset.value, config.timeset.def);
    WUPSStorageAPI::GetOrStoreDefault(config.ctrl.id, config.ctrl.value, config.ctrl.def);
    WUPSStorageAPI::GetOrStoreDefault(config.small_img.id, config.small_img.value, config.small_img.def);
    WUPSStorageAPI::GetOrStoreDefault(config.dst.id, config.dst.value, config.dst.def);
    WUPSStorageAPI::GetOrStoreDefault(config.title.id, config.title.value, config.title.def);
    WUPSStorageAPI::GetOrStoreDefault(config.lang.id, config.lang.value, config.lang.def);
    WUPSStorageAPI::GetOrStoreDefault(config.ip.id, config.ip.value, config.ip.def);
    WUPSStorageAPI::GetOrStoreDefault(config.ip_filter.id, config.ip_filter.value, config.ip_filter.def);
    WUPSStorageAPI::GetOrStoreDefault(config.port.id, config.port.value, config.port.def);
    WUPSStorageAPI::GetOrStoreDefault(config.cod.id, config.cod.value, config.cod.def);
    WUPSStorageAPI::GetOrStoreDefault(config.remote.id, config.remote.value, config.remote.def);
    WUPSStorageAPI::GetOrStoreDefault(config.server.id, config.server.value, config.server.def);
    WUPSStorageAPI::GetOrStoreDefault(config.server_port.id, config.server_port.value, config.server_port.def);
    WUPSStorageAPI::GetOrStoreDefault(config.secret.id, config.secret.value, config.secret.def);
    WUPSStorageAPI::SaveStorage();

    char environment_path_buffer[0x100];
    Mocha_GetEnvironmentPath(environment_path_buffer, sizeof(environment_path_buffer));
    INKAY_CONFIG = std::string(environment_path_buffer) + std::string("/plugins/config/inkay.json");
    INKAY_EXISTS = std::filesystem::exists(INKAY_CONFIG);
}

ON_APPLICATION_START() {
    app = GetXmlTag("shortname_en") == "Health and Safety Information" ? "Homebrew Application" : ReplaceSlashN(GetAppTitle(config.lang.value, config.title.value)); 

    if (app != preapp) elapsed = time(NULL); // Only update elapsed time if app changed
    ClearGameModeDescription(); // The new app sets its own friend list text
    preapp = app;

    if (tthread.joinable()) {
        tthread.request_stop();
        tthread.join(); // Wait for thread to finish before starting a new one
    }
    if (config.enabled.value && !(config.cod.value && app.find("Call of Duty") != std::string::npos)) tthread = std::jthread(GameLoop);
}

ON_APPLICATION_REQUESTS_EXIT() {    
    if (tthread.joinable()) {
        tthread.request_stop();
        tthread.join(); // Wait for thread to finish
    }
}

DEINITIALIZE_PLUGIN() {
    if (tthread.joinable()) {
        tthread.request_stop();
        tthread.join(); // Wait for thread to finish
    }

    Mocha_DeInitLibrary();
}
