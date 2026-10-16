#include "keyboard.h"

#include "display.h"
#include "esp_check.h"

#include <stdio.h>
#include <string.h>

#define KEYBOARD_BACKGROUND 0x0062
#define KEYBOARD_PANEL 0x08A4
#define KEYBOARD_BORDER 0x21A9
#define KEYBOARD_TEXT 0xFFFF
#define KEYBOARD_MUTED 0x84B5
#define KEYBOARD_GOOD 0x3F1B

#define KEYBOARD_PREVIEW_X 8
#define KEYBOARD_PREVIEW_Y 56
#define KEYBOARD_PREVIEW_WIDTH 304
#define KEYBOARD_PREVIEW_HEIGHT 44
#define KEYBOARD_DIGIT_Y 108
#define KEYBOARD_TOP_Y 148
#define KEYBOARD_HOME_Y 188
#define KEYBOARD_BOTTOM_Y 228
#define KEYBOARD_SYMBOL_Y 268
#define KEYBOARD_SYMBOL_ALT_Y 308
#define KEYBOARD_ACTION_Y 348
#define KEYBOARD_KEY_HEIGHT 34
#define KEYBOARD_ACTION_HEIGHT 40
#define KEYBOARD_VISIBLE_CHARACTERS 47
#define KEYBOARD_PASSWORD_VISIBLE_CHARACTERS 34

static const char *TAG = "keyboard";

static uint16_t text_width(const char *text, uint8_t scale)
{
    return (uint16_t)(strlen(text) * 6U * scale);
}

static esp_err_t draw_key(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *label)
{
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(x, y, width, height, KEYBOARD_BORDER),
        TAG,
        "key border failed");
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(x + 1U, y + 1U, width - 2U, height - 2U, KEYBOARD_PANEL),
        TAG,
        "key fill failed");

    uint8_t scale = strlen(label) == 1U ? 2U : 1U;
    uint16_t label_width = text_width(label, scale);
    uint16_t label_x = label_width < width ? x + (width - label_width) / 2U : x + 2U;
    uint16_t label_height = 7U * scale;
    uint16_t label_y = y + (height - label_height) / 2U;
    return cerberus_display_draw_text(label_x, label_y, label, KEYBOARD_TEXT, KEYBOARD_PANEL, scale);
}

static esp_err_t draw_uniform_row(uint16_t y, const char *characters)
{
    size_t count = strlen(characters);
    const uint16_t gap = 3;
    uint16_t width = (CERBERUS_LCD_WIDTH - 14U - (uint16_t)(count - 1U) * gap) / (uint16_t)count;
    uint16_t occupied = (uint16_t)count * width + (uint16_t)(count - 1U) * gap;
    uint16_t x = (CERBERUS_LCD_WIDTH - occupied) / 2U;
    char label[2] = {'\0', '\0'};

    for (size_t index = 0; index < count; ++index) {
        label[0] = characters[index];
        ESP_RETURN_ON_ERROR(draw_key(x, y, width, KEYBOARD_KEY_HEIGHT, label), TAG, "row key failed");
        x += width + gap;
    }
    return ESP_OK;
}

esp_err_t cerberus_keyboard_begin(
    cerberus_keyboard_t *keyboard,
    char *buffer,
    size_t capacity,
    const char *initial_text)
{
    if (keyboard == NULL || buffer == NULL || capacity < 2U || initial_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t initial_length = strnlen(initial_text, capacity);
    if (initial_length >= capacity) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(buffer, initial_text, initial_length + 1U);
    keyboard->buffer = buffer;
    keyboard->capacity = capacity;
    keyboard->length = initial_length;
    keyboard->uppercase = false;
    keyboard->masked = false;
    keyboard->mask_toggle_enabled = false;
    return ESP_OK;
}

void cerberus_keyboard_set_masked(cerberus_keyboard_t *keyboard, bool masked)
{
    if (keyboard != NULL) {
        keyboard->masked = masked;
        keyboard->mask_toggle_enabled = true;
    }
}

esp_err_t cerberus_keyboard_draw(const cerberus_keyboard_t *keyboard, const char *prompt)
{
    if (keyboard == NULL || keyboard->buffer == NULL || keyboard->capacity < 2U || prompt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(
            KEYBOARD_PREVIEW_X,
            KEYBOARD_PREVIEW_Y,
            KEYBOARD_PREVIEW_WIDTH,
            KEYBOARD_PREVIEW_HEIGHT,
            KEYBOARD_PANEL),
        TAG,
        "preview fill failed");

    const size_t visible_capacity = keyboard->mask_toggle_enabled
        ? KEYBOARD_PASSWORD_VISIBLE_CHARACTERS : KEYBOARD_VISIBLE_CHARACTERS;
    const char *visible = keyboard->buffer;
    char masked_preview[KEYBOARD_VISIBLE_CHARACTERS + 1U];
    if (keyboard->masked) {
        size_t visible_length = keyboard->length > visible_capacity
            ? visible_capacity
            : keyboard->length;
        memset(masked_preview, '*', visible_length);
        masked_preview[visible_length] = '\0';
        visible = masked_preview;
    } else if (keyboard->length > visible_capacity) {
        visible += keyboard->length - visible_capacity;
    }
    ESP_RETURN_ON_ERROR(
        cerberus_display_draw_text(16, 66, visible, KEYBOARD_TEXT, KEYBOARD_PANEL, 1),
        TAG,
        "preview text failed");

    if (keyboard->mask_toggle_enabled) {
        ESP_RETURN_ON_ERROR(
            draw_key(236, 61, 76, 30, keyboard->masked ? "SHOW" : "HIDE"),
            TAG,
            "password visibility key failed");
    }

    char count[32];
    snprintf(count, sizeof(count), "%s  %u/%u", prompt, (unsigned)keyboard->length, (unsigned)(keyboard->capacity - 1U));
    uint16_t count_width = text_width(count, 1);
    uint16_t count_x = count_width < CERBERUS_LCD_WIDTH ? (CERBERUS_LCD_WIDTH - count_width) / 2U : 4U;
    ESP_RETURN_ON_ERROR(
        cerberus_display_draw_text(count_x, 92, count, KEYBOARD_MUTED, KEYBOARD_PANEL, 1),
        TAG,
        "keyboard status failed");

    ESP_RETURN_ON_ERROR(draw_uniform_row(KEYBOARD_DIGIT_Y, "1234567890"), TAG, "digit row failed");
    ESP_RETURN_ON_ERROR(
        draw_uniform_row(KEYBOARD_TOP_Y, keyboard->uppercase ? "QWERTYUIOP" : "qwertyuiop"),
        TAG,
        "top row failed");
    ESP_RETURN_ON_ERROR(
        draw_uniform_row(KEYBOARD_HOME_Y, keyboard->uppercase ? "ASDFGHJKL" : "asdfghjkl"),
        TAG,
        "home row failed");

    ESP_RETURN_ON_ERROR(draw_key(4, KEYBOARD_BOTTOM_Y, 48, KEYBOARD_KEY_HEIGHT, "CASE"), TAG, "case key failed");
    const char *bottom = keyboard->uppercase ? "ZXCVBNM" : "zxcvbnm";
    char label[2] = {'\0', '\0'};
    for (size_t index = 0; index < 7U; ++index) {
        label[0] = bottom[index];
        ESP_RETURN_ON_ERROR(
            draw_key(56U + (uint16_t)index * 27U, KEYBOARD_BOTTOM_Y, 25, KEYBOARD_KEY_HEIGHT, label),
            TAG,
            "bottom row key failed");
    }
    ESP_RETURN_ON_ERROR(draw_key(248, KEYBOARD_BOTTOM_Y, 68, KEYBOARD_KEY_HEIGHT, "DEL"), TAG, "delete key failed");

    ESP_RETURN_ON_ERROR(draw_uniform_row(KEYBOARD_SYMBOL_Y, "@.-_/:?&=+"), TAG, "symbol row failed");
    ESP_RETURN_ON_ERROR(draw_uniform_row(KEYBOARD_SYMBOL_ALT_Y, "#%~!$*,;()\"\\"), TAG, "alternate symbol row failed");
    ESP_RETURN_ON_ERROR(draw_key(8, KEYBOARD_ACTION_Y, 136, KEYBOARD_ACTION_HEIGHT, "SPACE"), TAG, "space key failed");
    ESP_RETURN_ON_ERROR(draw_key(152, KEYBOARD_ACTION_Y, 72, KEYBOARD_ACTION_HEIGHT, "CLEAR"), TAG, "clear key failed");
    ESP_RETURN_ON_ERROR(draw_key(232, KEYBOARD_ACTION_Y, 80, KEYBOARD_ACTION_HEIGHT, "DONE"), TAG, "done key failed");

    const char *help = keyboard->length + 1U < keyboard->capacity ? "TAP DONE TO CONTINUE" : "INPUT LIMIT REACHED";
    uint16_t help_width = text_width(help, 1);
    uint16_t help_x = help_width < CERBERUS_LCD_WIDTH ? (CERBERUS_LCD_WIDTH - help_width) / 2U : 4U;
    return cerberus_display_draw_text(
        help_x,
        400,
        help,
        keyboard->length + 1U < keyboard->capacity ? KEYBOARD_GOOD : KEYBOARD_MUTED,
        KEYBOARD_BACKGROUND,
        1);
}

static int uniform_key_index(uint16_t x, size_t count)
{
    const uint16_t gap = 3;
    uint16_t width = (CERBERUS_LCD_WIDTH - 14U - (uint16_t)(count - 1U) * gap) / (uint16_t)count;
    uint16_t occupied = (uint16_t)count * width + (uint16_t)(count - 1U) * gap;
    uint16_t start_x = (CERBERUS_LCD_WIDTH - occupied) / 2U;
    if (x < start_x || x >= start_x + occupied) {
        return -1;
    }

    uint16_t position = x - start_x;
    uint16_t step = width + gap;
    int index = position / step;
    if ((size_t)index >= count || position % step >= width) {
        return -1;
    }
    return index;
}

static bool append_character(cerberus_keyboard_t *keyboard, char value)
{
    if (keyboard->length + 1U >= keyboard->capacity) {
        return false;
    }
    keyboard->buffer[keyboard->length++] = value;
    keyboard->buffer[keyboard->length] = '\0';
    return true;
}

esp_err_t cerberus_keyboard_handle_tap(
    cerberus_keyboard_t *keyboard,
    uint16_t x,
    uint16_t y,
    cerberus_keyboard_action_t *action)
{
    if (keyboard == NULL || keyboard->buffer == NULL || action == NULL || x >= CERBERUS_LCD_WIDTH ||
        y >= CERBERUS_LCD_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    *action = CERBERUS_KEYBOARD_ACTION_NONE;
    if (keyboard->mask_toggle_enabled && x >= 236U && x < 312U && y >= 56U && y < 100U) {
        keyboard->masked = !keyboard->masked;
        *action = CERBERUS_KEYBOARD_ACTION_CHANGED;
        return ESP_OK;
    }
    const char *characters = NULL;
    int index = -1;
    if (y >= KEYBOARD_DIGIT_Y && y < KEYBOARD_DIGIT_Y + KEYBOARD_KEY_HEIGHT) {
        characters = "1234567890";
        index = uniform_key_index(x, 10U);
    } else if (y >= KEYBOARD_TOP_Y && y < KEYBOARD_TOP_Y + KEYBOARD_KEY_HEIGHT) {
        characters = keyboard->uppercase ? "QWERTYUIOP" : "qwertyuiop";
        index = uniform_key_index(x, 10U);
    } else if (y >= KEYBOARD_HOME_Y && y < KEYBOARD_HOME_Y + KEYBOARD_KEY_HEIGHT) {
        characters = keyboard->uppercase ? "ASDFGHJKL" : "asdfghjkl";
        index = uniform_key_index(x, 9U);
    } else if (y >= KEYBOARD_BOTTOM_Y && y < KEYBOARD_BOTTOM_Y + KEYBOARD_KEY_HEIGHT) {
        if (x >= 4U && x < 52U) {
            keyboard->uppercase = !keyboard->uppercase;
            *action = CERBERUS_KEYBOARD_ACTION_CHANGED;
            return ESP_OK;
        }
        if (x >= 248U && x < 316U) {
            if (keyboard->length > 0U) {
                keyboard->buffer[--keyboard->length] = '\0';
                *action = CERBERUS_KEYBOARD_ACTION_CHANGED;
            }
            return ESP_OK;
        }
        if (x >= 56U && x < 245U) {
            uint16_t position = x - 56U;
            index = position / 27U;
            if (index < 7 && position % 27U < 25U) {
                characters = keyboard->uppercase ? "ZXCVBNM" : "zxcvbnm";
            } else {
                index = -1;
            }
        }
    } else if (y >= KEYBOARD_SYMBOL_Y && y < KEYBOARD_SYMBOL_Y + KEYBOARD_KEY_HEIGHT) {
        characters = "@.-_/:?&=+";
        index = uniform_key_index(x, 10U);
    } else if (y >= KEYBOARD_SYMBOL_ALT_Y && y < KEYBOARD_SYMBOL_ALT_Y + KEYBOARD_KEY_HEIGHT) {
        characters = "#%~!$*,;()\"\\";
        index = uniform_key_index(x, strlen(characters));
    } else if (y >= KEYBOARD_ACTION_Y && y < KEYBOARD_ACTION_Y + KEYBOARD_ACTION_HEIGHT) {
        if (x >= 8U && x < 144U) {
            if (append_character(keyboard, ' ')) {
                *action = CERBERUS_KEYBOARD_ACTION_CHANGED;
            }
        } else if (x >= 152U && x < 224U) {
            if (keyboard->length > 0U) {
                keyboard->length = 0;
                keyboard->buffer[0] = '\0';
                *action = CERBERUS_KEYBOARD_ACTION_CHANGED;
            }
        } else if (x >= 232U && x < 312U && keyboard->length > 0U) {
            *action = CERBERUS_KEYBOARD_ACTION_SUBMIT;
        }
        return ESP_OK;
    }

    if (characters != NULL && index >= 0 && append_character(keyboard, characters[index])) {
        *action = CERBERUS_KEYBOARD_ACTION_CHANGED;
    }
    return ESP_OK;
}
