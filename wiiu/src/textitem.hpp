#pragma once

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

#include <wups/config.h>
#include <wups/config_api.h>
#include <wups/config/WUPSConfigItem.h>

/**
 * Characters that can be entered into a text item.
 * Limited to what is valid in a domain name or IP address.
 */
constexpr std::string_view TEXT_ITEM_CHARSET = "abcdefghijklmnopqrstuvwxyz0123456789.-";

/**
 * Maximum length of a text item's value.
 */
constexpr size_t TEXT_ITEM_MAX_LEN = 64;

/**
 * Amount of characters shown around the cursor while editing.
 */
constexpr size_t TEXT_ITEM_WINDOW = 32;

/**
 * Frames to wait before a held button starts repeating.
 */
constexpr uint32_t TEXT_ITEM_REPEAT_DELAY = 12;

/**
 * Frames between repeats while a button is held.
 */
constexpr uint32_t TEXT_ITEM_REPEAT_RATE = 3;

struct ConfigItemText;
typedef void (*TextValueChangedCallback)(ConfigItemText *item, const std::string &newValue);

/**
 * A config item that allows entering text with the D-pad.
 */
struct ConfigItemText {
    std::string identifier;
    std::string defaultValue;
    std::string currentValue;
    std::string valueAtCreation;
    std::string valueBeforeEdit;
    bool editing = false;
    size_t cursor = 0;
    uint32_t frameTimer = 0;
    TextValueChangedCallback callback = nullptr;
};

namespace TextItem {
    int32_t Display(ConfigItemText *item, bool isSelected, char *out_buf, int32_t out_size) {
        if (!item->editing) {
            std::string shown = item->currentValue.empty() ? "(not set)" : item->currentValue;
            snprintf(out_buf, out_size, "%s%s", isSelected ? "(Press A to edit) " : "", shown.c_str());
            return 0;
        }

        // Only show a window of text around the cursor so long values fit on screen
        const std::string &v = item->currentValue;
        size_t start = item->cursor > TEXT_ITEM_WINDOW - 8 ? item->cursor - (TEXT_ITEM_WINDOW - 8) : 0;
        size_t end   = std::min(v.size(), start + TEXT_ITEM_WINDOW);

        std::string shown = start > 0 ? "<" : "";
        shown += v.substr(start, item->cursor - start);
        shown += "[" + (item->cursor < v.size() ? std::string(1, v[item->cursor]) : std::string("_")) + "]";
        if (item->cursor < end) shown += v.substr(item->cursor + 1, end - item->cursor - 1);
        if (end < v.size()) shown += ">";

        snprintf(out_buf, out_size, "%s", shown.c_str());
        return 0;
    }

    int32_t GetDisplay(void *context, char *out_buf, int32_t out_size) {
        return Display(static_cast<ConfigItemText *>(context), false, out_buf, out_size);
    }

    int32_t GetSelectedDisplay(void *context, char *out_buf, int32_t out_size) {
        return Display(static_cast<ConfigItemText *>(context), true, out_buf, out_size);
    }

    void CycleChar(ConfigItemText *item, int delta) {
        std::string &v = item->currentValue;
        const int n = TEXT_ITEM_CHARSET.size();

        // At the end of the string, add a new character
        if (item->cursor >= v.size()) {
            if (v.size() >= TEXT_ITEM_MAX_LEN) return;
            v += TEXT_ITEM_CHARSET[delta > 0 ? 0 : n - 1];
            return;
        }

        size_t pos = TEXT_ITEM_CHARSET.find(v[item->cursor]);
        int idx = pos == std::string_view::npos ? 0 : ((int) pos + delta + n) % n;
        v[item->cursor] = TEXT_ITEM_CHARSET[idx];
    }

    void OnInput(void *context, WUPSConfigSimplePadData input) {
        auto *item = static_cast<ConfigItemText *>(context);

        // Handle edit request
        if (!item->editing) {
            if (input.buttons_d & WUPS_CONFIG_BUTTON_A) {
                item->valueBeforeEdit = item->currentValue;
                item->cursor          = item->currentValue.size();
                item->frameTimer      = 0;
                item->editing         = true;
            }
            return;
        }

        // When editing press A to accept, press B to abort
        if (input.buttons_d & (WUPS_CONFIG_BUTTON_A | WUPS_CONFIG_BUTTON_B)) {
            if (input.buttons_d & WUPS_CONFIG_BUTTON_B) {
                item->currentValue = item->valueBeforeEdit;
            }
            item->editing = false;
            return;
        }

        // X deletes the character under the cursor, Y deletes everything
        if (input.buttons_d & WUPS_CONFIG_BUTTON_X) {
            if (item->cursor < item->currentValue.size()) {
                item->currentValue.erase(item->cursor, 1);
            } else if (item->cursor > 0) {
                item->currentValue.pop_back();
                item->cursor--;
            }
            return;
        }
        if (input.buttons_d & WUPS_CONFIG_BUTTON_Y) {
            item->currentValue.clear();
            item->cursor = 0;
            return;
        }

        // Handle held buttons, repeating after a short delay
        const WUPS_CONFIG_SIMPLE_INPUT dirs = WUPS_CONFIG_BUTTON_LEFT | WUPS_CONFIG_BUTTON_RIGHT | WUPS_CONFIG_BUTTON_UP | WUPS_CONFIG_BUTTON_DOWN;
        if (!(input.buttons_h & dirs)) {
            item->frameTimer = 0;
            return;
        }
        if (input.buttons_d & dirs) {
            item->frameTimer = TEXT_ITEM_REPEAT_DELAY;
        } else if (item->frameTimer > 0) {
            item->frameTimer--;
            return;
        } else {
            item->frameTimer = TEXT_ITEM_REPEAT_RATE;
        }

        if (input.buttons_h & WUPS_CONFIG_BUTTON_LEFT) {
            if (item->cursor > 0) item->cursor--;
        } else if (input.buttons_h & WUPS_CONFIG_BUTTON_RIGHT) {
            if (item->cursor < item->currentValue.size()) item->cursor++;
        } else if (input.buttons_h & WUPS_CONFIG_BUTTON_UP) {
            CycleChar(item, 1);
        } else if (input.buttons_h & WUPS_CONFIG_BUTTON_DOWN) {
            CycleChar(item, -1);
        }
    }

    void RestoreDefault(void *context) {
        auto *item = static_cast<ConfigItemText *>(context);
        item->currentValue = item->defaultValue;
        item->cursor       = 0;
    }

    bool IsMovementAllowed(void *context) {
        return !static_cast<ConfigItemText *>(context)->editing;
    }

    void OnClose(void *context) {
        auto *item = static_cast<ConfigItemText *>(context);
        if (item->currentValue != item->valueAtCreation && item->callback != nullptr) {
            item->callback(item, item->currentValue);
        }
    }

    void OnDelete(void *context) {
        delete static_cast<ConfigItemText *>(context);
    }
}

/**
 * Wrapper so the text item can be added to a WUPSConfigCategory.
 */
class WUPSConfigItemText : public WUPSConfigItem {
public:
    static WUPSConfigItemText Create(std::string_view identifier, std::string_view displayName,
                                     const std::string &defaultValue, const std::string &currentValue,
                                     TextValueChangedCallback callback) {
        auto *item            = new ConfigItemText();
        item->identifier      = identifier;
        item->defaultValue    = defaultValue;
        item->currentValue    = currentValue;
        item->valueAtCreation = currentValue;
        item->callback        = callback;

        constexpr WUPSConfigAPIItemCallbacksV2 callbacks = {
            .getCurrentValueDisplay         = &TextItem::GetDisplay,
            .getCurrentValueSelectedDisplay = &TextItem::GetSelectedDisplay,
            .onSelected                     = nullptr,
            .restoreDefault                 = &TextItem::RestoreDefault,
            .isMovementAllowed              = &TextItem::IsMovementAllowed,
            .onCloseCallback                = &TextItem::OnClose,
            .onInput                        = &TextItem::OnInput,
            .onInputEx                      = nullptr,
            .onDelete                       = &TextItem::OnDelete,
        };

        std::string name(displayName);
        const WUPSConfigAPIItemOptionsV2 options = {
            .displayName = name.c_str(),
            .context     = item,
            .callbacks   = callbacks,
        };

        WUPSConfigItemHandle handle;
        if (WUPSConfigAPI_Item_Create(options, &handle) != WUPSCONFIG_API_RESULT_SUCCESS) {
            delete item;
            throw std::runtime_error("Failed to create WUPSConfigItemText");
        }
        return WUPSConfigItemText(handle);
    }

private:
    explicit WUPSConfigItemText(const WUPSConfigItemHandle itemHandle) : WUPSConfigItem(itemHandle) {}
};
