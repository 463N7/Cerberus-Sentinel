#include "sentinel_tools.h"

#include "display.h"

#include <stddef.h>

#define CYAN 0x3F1BU
#define VIOLET 0xACDFU
#define AMBER 0xFDEAU
#define TRY_DRAW(call) do { const esp_err_t result_ = (call); if (result_ != ESP_OK) return result_; } while (0)

typedef struct {
    const char *title;
    const char *description;
    uint16_t accent;
    sentinel_tools_action_t action;
} tools_item_t;

static const tools_item_t main_items[] = {
    {"Settings", "DISPLAY, PRIVACY AND SAVED PREFERENCES", CYAN, SENTINEL_TOOLS_SETTINGS},
    {"SD files", "BROWSE FILES OR SHARE THE SD CARD BY USB", CYAN, SENTINEL_TOOLS_FILES},
    {"Logs + reports", "VIEW SAVED SCANS AND DEVICE LOGS", VIOLET, SENTINEL_TOOLS_LOGS},
    {"Local web", "OPEN THE DASHBOARD IN YOUR BROWSER", VIOLET, SENTINEL_TOOLS_WEB},
    {"Device care", "HEALTH, HARDWARE CHECKS AND USB SUPPORT", AMBER, SENTINEL_TOOLS_OPEN_CARE},
    {"Extras", "OFFLINE QR MAKER AND LOCAL SCRIPTS", AMBER, SENTINEL_TOOLS_OPEN_EXTRAS},
};
static const tools_item_t care_items[] = {
    {"System health", "DEVICE STATUS AND CRASH RECORDS", CYAN, SENTINEL_TOOLS_STATUS},
    {"Hardware checks", "TEST TOUCH INPUT AND SD READ / WRITE", VIOLET, SENTINEL_TOOLS_CHECKS},
    {"USB support", "DIAGNOSTIC CONSOLE AND RECOVERY MODE", AMBER, SENTINEL_TOOLS_USB},
};
static const tools_item_t extra_items[] = {
    {"QR maker", "CREATE WI-FI, TEXT AND LINK CODES", CYAN, SENTINEL_TOOLS_QR},
    {"Local scripts", "RUN MESSAGE, TIMER AND QR SEQUENCES", VIOLET, SENTINEL_TOOLS_SCRIPTS},
};

/* Rendering and touch routing use these same rectangles. */
static cerberus_ui_rect_t item_bounds(size_t index)
{
    return (cerberus_ui_rect_t){12U, (uint16_t)(70U + index * 54U), 296U, 48U};
}
static const cerberus_ui_rect_t back_bounds = {12U, 420U, 296U, 44U};

static bool valid_group(sentinel_tools_group_t group)
{
    return group == SENTINEL_TOOLS_MAIN || group == SENTINEL_TOOLS_CARE ||
        group == SENTINEL_TOOLS_EXTRAS;
}

static const tools_item_t *items_for(sentinel_tools_group_t group, size_t *count)
{
    if (group == SENTINEL_TOOLS_MAIN) {
        *count = sizeof(main_items) / sizeof(main_items[0]);
        return main_items;
    }
    if (group == SENTINEL_TOOLS_CARE) {
        *count = sizeof(care_items) / sizeof(care_items[0]);
        return care_items;
    }
    *count = sizeof(extra_items) / sizeof(extra_items[0]);
    return extra_items;
}

static bool inside(cerberus_ui_rect_t bounds, uint16_t x, uint16_t y)
{
    return x >= bounds.x && y >= bounds.y &&
        (uint32_t)x < (uint32_t)bounds.x + bounds.width &&
        (uint32_t)y < (uint32_t)bounds.y + bounds.height;
}

esp_err_t sentinel_tools_draw(
    sentinel_tools_group_t group,
    const cerberus_ui_widget_theme_t *theme)
{
    if (!valid_group(group) || theme == NULL) return ESP_ERR_INVALID_ARG;
    size_t count;
    const tools_item_t *items = items_for(group, &count);
    const char *subtitle = group == SENTINEL_TOOLS_MAIN ? "SET UP / SAVE / MAINTAIN" :
        (group == SENTINEL_TOOLS_CARE ? "CHECK AND MAINTAIN YOUR CERBERUS" : "SMALL TOOLS, AVAILABLE OFFLINE");
    TRY_DRAW(cerberus_display_draw_text(14U, 52U, subtitle,
        theme->muted, theme->background, 1U));
    for (size_t i = 0; i < count; ++i) {
        cerberus_ui_rect_t bounds = item_bounds(i);
        TRY_DRAW(cerberus_display_fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, theme->panel));
        TRY_DRAW(cerberus_display_fill_rect(bounds.x, bounds.y, 3U, bounds.height, items[i].accent));
        TRY_DRAW(cerberus_display_draw_text(bounds.x + 12U, bounds.y + 7U,
            items[i].title, theme->text, theme->panel, 2U));
        TRY_DRAW(cerberus_display_draw_text(bounds.x + 12U, bounds.y + 30U,
            items[i].description, theme->muted, theme->panel, 1U));
        TRY_DRAW(cerberus_display_draw_text(bounds.x + bounds.width - 20U, bounds.y + 9U,
            ">", items[i].accent, theme->panel, 2U));
    }
    if (group == SENTINEL_TOOLS_MAIN) {
        TRY_DRAW(cerberus_display_draw_text(24U, 400U, "RADIO TOOLS AND FIELD GUIDE ARE ON HOME",
            theme->muted, theme->background, 1U));
    } else {
        uint16_t note_y = group == SENTINEL_TOOLS_CARE ? 252U : 198U;
        TRY_DRAW(cerberus_display_fill_rect(12U, note_y, 296U, 82U, theme->panel));
        TRY_DRAW(cerberus_display_draw_text(24U, note_y + 12U,
            group == SENTINEL_TOOLS_CARE ? "LOOKING FOR USB FILE SHARING?" : "KEEP EXPLORING",
            AMBER, theme->panel, 1U));
        TRY_DRAW(cerberus_display_draw_text(24U, note_y + 34U,
            group == SENTINEL_TOOLS_CARE ? "OPEN SD FILES > TOOLS > USB SD DRIVE." :
                "SCRIPTS RUN LOCALLY FROM YOUR SD CARD.",
            theme->text, theme->panel, 1U));
        TRY_DRAW(cerberus_display_draw_text(24U, note_y + 56U,
            group == SENTINEL_TOOLS_CARE ? "USB SUPPORT IS FOR CHECKS AND RECOVERY." :
                "FOR GUIDED ACTIVITIES, OPEN FIELD GUIDE.",
            theme->muted, theme->panel, 1U));
    }
    return cerberus_ui_widget_draw_button(back_bounds,
        group == SENTINEL_TOOLS_MAIN ? "HOME" : "DEVICE TOOLS", 2U, true, theme);
}

sentinel_tools_action_t sentinel_tools_hit_test(
    sentinel_tools_group_t group, uint16_t x, uint16_t y)
{
    if (!valid_group(group)) return SENTINEL_TOOLS_NONE;
    if (inside(back_bounds, x, y)) return SENTINEL_TOOLS_BACK;
    size_t count;
    const tools_item_t *items = items_for(group, &count);
    for (size_t i = 0; i < count; ++i) {
        if (inside(item_bounds(i), x, y)) return items[i].action;
    }
    return SENTINEL_TOOLS_NONE;
}
