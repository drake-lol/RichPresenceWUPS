#include <thread>

#if defined(__linux__) || defined(__APPLE__)
	#include "unix/unix.hpp"
#elif _WIN32
    #include "win/win.hpp"
#endif

#include "http.hpp"

struct Config {
    // The image repository. Do not include https:// or http:// at the beginning of string.
    std::string repo = "raw.githubusercontent.com/flamingnineteen/richpresencewups-db/main";
    
    // The application ID of the Discord app to connect to.
    std::string app_id = "1353248127469228074";

    // The port to bind to.
    uint16_t port = 5005;

    // The TCP port to listen for HTTP on. 0 disables the HTTP listener.
    uint16_t http_port = 0;

    // The address to listen for HTTP on.
    std::string http_bind = "127.0.0.1";

    // The secret the Wii U must send over HTTP. Empty accepts any request.
    std::string http_secret = "";

    // Whether to show logs on Windows or not
    bool winlogs = false;
};

Config cmdLineArgs(int argc, char* argv[]) {
    Config config = Config();

    // Check for command line arguments
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--windows-logs") == 0 || std::strcmp(argv[i], "-w") == 0) {
            config.winlogs = true;
            fmt::println("Enabling Windows logging");
        }
        else if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            fmt::println("Wii U Rich Presence v{}", VERSION);
        }
        else if (i + 1 < argc) {
            if (std::strcmp(argv[i], "--repo") == 0 || std::strcmp(argv[i], "-r") == 0) {
                config.repo = argv[i+1];
                fmt::println("Using repository {}", config.repo);
            }
            else if (
                std::strcmp(argv[i], "--app-id") == 0 || std::strcmp(argv[i], "-a") == 0) {
                config.app_id = argv[i+1];
                fmt::println("Using application id {}", config.app_id);
            }
            else if (std::strcmp(argv[i], "--port") == 0 || std::strcmp(argv[i], "-p") == 0) {
                config.port = std::stoi(argv[i+1]);
                fmt::println("Using port {}", config.port);
            }
            else if (std::strcmp(argv[i], "--http-port") == 0 || std::strcmp(argv[i], "-H") == 0) {
                config.http_port = std::stoi(argv[i+1]);
                fmt::println("Using HTTP port {}", config.http_port);
            }
            else if (std::strcmp(argv[i], "--http-bind") == 0 || std::strcmp(argv[i], "-b") == 0) {
                config.http_bind = argv[i+1];
                fmt::println("Using HTTP bind address {}", config.http_bind);
            }
            else if (std::strcmp(argv[i], "--http-secret") == 0 || std::strcmp(argv[i], "-s") == 0) {
                config.http_secret = argv[i+1];
                fmt::println("Using an HTTP secret");
            }
            i++;
        }
    }

    return config;
}

void coreLogic(Config config) {
    std::thread tthread(checkIdle);

    discordSetup(config.app_id);
    discord::RPCManager::get().initialize();

    json images = getImageKeys(config.repo);

    // Also accept data over HTTP if enabled
    if (config.http_port != 0) {
        std::thread(httpLoop, config.repo, images, config.http_bind, config.http_port, config.http_secret).detach();
    }

    gameLoop(config.repo, images, config.port);

    runIdleLoop = false;
	if (tthread.joinable()) {
		tthread.join();
	}

    discord::RPCManager::get().shutdown();
    return;
}
