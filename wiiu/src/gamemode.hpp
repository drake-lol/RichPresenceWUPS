#pragma once

#include <mutex>
#include <string>

#include <wups.h>

/**
 * The text the current game shows on the Wii U friend list, like "In the menus".
 * Games set it through nn::fp, so those functions are hooked to capture it.
 */
std::mutex gameModeMutex;
std::string gameModeDescription = "";

/**
 * Converts a game mode description to UTF-8.
 * The console's wchar_t is 16 bits, so descriptions are UTF-16 rather than devkitPPC's 32-bit wchar_t.
 */
std::string Utf16ToUtf8(const uint16_t *s) {
    std::string out;
    if (s == nullptr) return out;

    // Descriptions are at most 128 characters
    for (int i = 0; i < 128 && s[i] != 0; i++) {
        uint32_t c = s[i];

        if (c >= 0xD800 && c <= 0xDBFF && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD; // Unpaired surrogate
        }

        if (c < 0x80) {
            out += (char) c;
        } else if (c < 0x800) {
            out += (char) (0xC0 | (c >> 6));
            out += (char) (0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += (char) (0xE0 | (c >> 12));
            out += (char) (0x80 | ((c >> 6) & 0x3F));
            out += (char) (0x80 | (c & 0x3F));
        } else {
            out += (char) (0xF0 | (c >> 18));
            out += (char) (0x80 | ((c >> 12) & 0x3F));
            out += (char) (0x80 | ((c >> 6) & 0x3F));
            out += (char) (0x80 | (c & 0x3F));
        }
    }
    return out;
}

void SetGameModeDescription(const uint16_t *description) {
    std::string text = Utf16ToUtf8(description);
    std::lock_guard<std::mutex> lock(gameModeMutex);
    gameModeDescription = text;
}

std::string GetGameModeDescription() {
    std::lock_guard<std::mutex> lock(gameModeMutex);
    return gameModeDescription;
}

void ClearGameModeDescription() {
    std::lock_guard<std::mutex> lock(gameModeMutex);
    gameModeDescription = "";
}

// Hooking these stopped the console from booting, so they're disabled until the cause is found.
// Until then, no friend list text is sent. Build with -DENABLE_GAME_MODE_HOOKS to try them.
#ifdef ENABLE_GAME_MODE_HOOKS

// nn::fp::UpdateGameModeDescription(const wchar_t*)
DECL_FUNCTION(uint32_t, FPUpdateGameModeDescription, const uint16_t *description) {
    SetGameModeDescription(description);
    return real_FPUpdateGameModeDescription(description);
}

// nn::fp::UpdateGameMode(const nn::fp::GameMode*, const wchar_t*)
DECL_FUNCTION(uint32_t, FPUpdateGameMode, const void *gameMode, const uint16_t *description) {
    SetGameModeDescription(description);
    return real_FPUpdateGameMode(gameMode, description);
}

// nn::fp::UpdateGameMode(const nn::fp::GameMode*, const wchar_t*, unsigned int)
DECL_FUNCTION(uint32_t, FPUpdateGameModeWithArg, const void *gameMode, const uint16_t *description, uint32_t arg) {
    SetGameModeDescription(description);
    return real_FPUpdateGameModeWithArg(gameMode, description, arg);
}

// nn::fp::UpdateGameModeEx(const nn::fp::GameMode*, const wchar_t*)
DECL_FUNCTION(uint32_t, FPUpdateGameModeEx, const void *gameMode, const uint16_t *description) {
    SetGameModeDescription(description);
    return real_FPUpdateGameModeEx(gameMode, description);
}

WUPS_MUST_REPLACE(FPUpdateGameModeDescription, WUPS_LOADER_LIBRARY_NN_FP, UpdateGameModeDescription__Q2_2nn2fpFPCw);
WUPS_MUST_REPLACE(FPUpdateGameMode, WUPS_LOADER_LIBRARY_NN_FP, UpdateGameMode__Q2_2nn2fpFPCQ3_2nn2fp8GameModePCw);
WUPS_MUST_REPLACE(FPUpdateGameModeWithArg, WUPS_LOADER_LIBRARY_NN_FP, UpdateGameMode__Q2_2nn2fpFPCQ3_2nn2fp8GameModePCwUi);
WUPS_MUST_REPLACE(FPUpdateGameModeEx, WUPS_LOADER_LIBRARY_NN_FP, UpdateGameModeEx__Q2_2nn2fpFPCQ3_2nn2fp8GameModePCw);

#endif
