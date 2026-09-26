#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <ctime>

#include "hmac.hpp"

#if _WIN32
    typedef SOCKET httpSocket;
    #define HTTP_INVALID_SOCKET INVALID_SOCKET
    #define closeHttpSocket closesocket
#else
    #include <sys/time.h>

    typedef int httpSocket;
    #define HTTP_INVALID_SOCKET -1
    #define closeHttpSocket close
#endif

// Largest request that will be accepted, in bytes
const size_t HTTP_MAX_REQUEST = 16384;

// Sends a response with a plain text body
void sendHttpResponse(httpSocket client, std::string status, std::string body = "") {
    std::string response = "HTTP/1.1 " + status + "\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;
    send(client, response.c_str(), (int) response.size(), 0);
}

// Gets a header's value from a request's headers, or an empty string if it isn't present
std::string getHttpHeader(const std::string &headers, std::string name) {
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });

    size_t pos = lower.find("\r\n" + name + ":");
    if (pos == std::string::npos) return "";
    pos += name.size() + 3;

    // Keep the value's original case
    size_t end = headers.find("\r\n", pos);
    std::string value = headers.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    return value;
}

// Compares signatures in constant time, so they can't be guessed from response times
bool constantTimeEquals(const std::string &given, const std::string &expected) {
    unsigned char diff = given.size() != expected.size();
    for (size_t i = 0; i < given.size(); i++) {
        diff |= given[i] ^ expected[i % expected.size()];
    }
    return diff == 0;
}

// Largest difference allowed between the Wii U's clock and this computer's, in seconds.
// The Wii U's clock is in local time, so this allows for any time zone.
const long long HTTP_MAX_CLOCK_DIFFERENCE = 25 * 60 * 60;

// Checks an update's signature. Returns an empty string if it's valid, or the reason it isn't.
std::string checkSignature(const std::string &headers, const std::string &body, const std::string &secret, long long &lastTimestamp) {
    std::string timestamp = getHttpHeader(headers, "X-WURP-Timestamp");
    std::string signature = getHttpHeader(headers, "X-WURP-Signature");
    if (timestamp == "" || signature == "") return "missing signature (set the secret on the Wii U)";

    if (!constantTimeEquals(signature, Hmac::Sign(secret, Hmac::Message(timestamp, body)))) {
        return "wrong signature (check that the secret matches)";
    }

    long long ts;
    try {
        size_t used;
        ts = std::stoll(timestamp, &used);
        if (used != timestamp.size()) return "invalid timestamp";
    } catch (...) {
        return "invalid timestamp";
    }

    if (std::llabs(ts - (long long) time(nullptr)) > HTTP_MAX_CLOCK_DIFFERENCE) {
        return "timestamp too far from this computer's time (check the Wii U's clock)";
    }
    if (ts <= lastTimestamp) {
        return "timestamp not newer than the last update (a replayed request, or the Wii U's clock was set back; restart this app if so)";
    }

    lastTimestamp = ts;
    return "";
}

// Reads a full HTTP request. Returns false if the connection failed or the request was malformed.
bool readHttpRequest(httpSocket client, std::string &method, std::string &headers, std::string &body) {
    std::string data;
    char buffer[1024];
    size_t headerEnd = std::string::npos;
    size_t contentLength = 0;

    while (headerEnd == std::string::npos || data.size() < headerEnd + 4 + contentLength) {
        if (data.size() > HTTP_MAX_REQUEST) return false;

        int len = recv(client, buffer, sizeof(buffer), 0);
        if (len <= 0) return false;
        data.append(buffer, len);

        // Parse the headers once they have all arrived
        if (headerEnd == std::string::npos && (headerEnd = data.find("\r\n\r\n")) != std::string::npos) {
            headers = data.substr(0, headerEnd);
            method = headers.substr(0, headers.find(' '));

            std::string length = getHttpHeader(headers, "Content-Length");
            if (length != "") {
                try {
                    contentLength = std::stoul(length);
                } catch (...) {
                    return false;
                }
            }
            if (contentLength > HTTP_MAX_REQUEST) return false;
        }
    }

    body = data.substr(headerEnd + 4, contentLength);
    return true;
}

// Listens for data sent over HTTP, e.g. from a Wii U outside the local network through a Cloudflare Tunnel
void httpLoop(std::string repo, json images, std::string bindAddr, uint16_t port, std::string secret) {
#if _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
    signal(SIGPIPE, SIG_IGN); // Don't exit if a client disconnects before the response is sent
#endif

    httpSocket sock;
    do {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    } while (sock == HTTP_INVALID_SOCKET);

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*) &reuse, sizeof(reuse));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(bindAddr.c_str());

    while (bind(sock, (sockaddr*) &addr, sizeof(addr)) < 0 || listen(sock, 4) < 0) {
        fmt::println("Failed to bind to TCP port {} on {}. Is another program using it? Retrying...", port, bindAddr);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    fmt::println("Listening for HTTP on {}:{}", bindAddr, port);
    if (secret == "") fmt::println("Warning: no HTTP secret is set, so anyone who can reach this server can update your status");

    // Timestamp of the last accepted update, so requests can't be replayed
    long long lastTimestamp = 0;

    while (true) {
        httpSocket client = accept(sock, nullptr, nullptr);
        if (client == HTTP_INVALID_SOCKET) continue;

        // Don't let a stalled connection block the loop
#if _WIN32
        DWORD timeout = 5000;
#else
        timeval timeout {5, 0};
#endif
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*) &timeout, sizeof(timeout));

        std::string method, headers, body;
        if (!readHttpRequest(client, method, headers, body)) {
            sendHttpResponse(client, "400 Bad Request");
        }
        else if (method == "POST") {
            // Cloudflare passes along the address of the Wii U
            std::string from = getHttpHeader(headers, "CF-Connecting-IP");
            std::string source = "HTTP from " + (from != "" ? from : std::string("local network"));

            std::string rejection = secret == "" ? "" : checkSignature(headers, body, secret, lastTimestamp);
            if (rejection != "") {
                sendHttpResponse(client, "401 Unauthorized");
                fmt::println("Rejected {}: {}", source, rejection);
            }
            else {
                sendHttpResponse(client, "204 No Content");
                if (parseJsonAndUpdate(body, images, repo, adjustEpochToUtc, source) < 0) {
                    fmt::println("Failed to update Rich Presence");
                }
            }
        }
        else if (method == "GET" || method == "HEAD") {
            // Lets the connection be tested from a browser
            sendHttpResponse(client, "200 OK", method == "GET" ? "Wii U Rich Presence is running." : "");
        }
        else {
            sendHttpResponse(client, "405 Method Not Allowed");
        }

        closeHttpSocket(client);
    }
}
