#include "field_menu.h"
#include "display.h"
#include <stddef.h>
#include <stdio.h>

#define TRY(call) do { esp_err_t err_ = (call); if (err_ != ESP_OK) return err_; } while (0)
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define ATTACK_ROWS 3U
#define SCAN_ROW_TOP 116U
#define SCAN_ROW_STEP 37U
#define SCAN_ROW_HEIGHT 34U

typedef struct {
    const char *title;
    const char *description;
    cerberus_field_action_t action;
} scan_item_t;
static const scan_item_t scans[] = {
    {"WI-FI", "NETWORKS / CHANNELS / DEFENSE", CERBERUS_FIELD_WIFI},
    {"BLUETOOTH", "DEVICES / SIGNAL / GATT", CERBERUS_FIELD_BLE},
    {"WARDRIVING", "GPS OPTIONAL / WI-FI + BLE / SD", CERBERUS_FIELD_WARDRIVE},
    {"RADIO SENSE", "802.15.4 ENERGY / BEACONS", CERBERUS_FIELD_RADIO},
    {"EXPANSION SURVEY", "R1-R6 / VENDOR LOOKUP / SD REPORT", CERBERUS_FIELD_SURVEY},
    {"NETWORK SCANNERS", "PING / ARP / TCP / SSH / WEB / SERVICES", CERBERUS_FIELD_NETWORK},
    {"REMOTE ID", "AIRCRAFT BROADCASTS / WI-FI + BLE", CERBERUS_FIELD_REMOTE_ID},
    {"MAC MONITOR", "OBSERVATIONS / SIGNAL / WI-FI + BLE", CERBERUS_FIELD_MAC_MONITOR},
};
_Static_assert(SCAN_ROW_TOP + (COUNT(scans) - 1U) * SCAN_ROW_STEP +
    SCAN_ROW_HEIGHT <= 420U, "Scan rows must end before Home");

/* AP-dependent variants from the upstream attack prerequisites, reviewed
 * 2026-10-28; see docs/MARAUDER_AP_MENU.md. Include the AP-clone portal variant;
 * standalone beacon/SSID workflows do not belong to a selected AP.
 * Deauthentication and AP-directed probes have bounded backends. */
static const char *const wifi_attacks[] = {
    "DEAUTH FLOOD", "BAD MESSAGE", "ASSOCIATION SLEEP",
    "SAE COMMIT FLOOD", "CHANNEL SWITCH", "QUIET TIME",
    "PROBE REQUEST FLOOD", "AP CLONE SPAM", "EVIL PORTAL / AP CLONE",
};
static const char *const ble_attacks[] = {
    "SOUR APPLE", "APPLE JUICE", "SWIFT PAIR SPAM",
    "SAMSUNG BLE SPAM", "GOOGLE BLE SPAM", "FLIPPER BLE SPAM",
    "BLE SPAM ALL", "SPOOF AIRTAG", "FIND MY BEEP ALL",
};
static cerberus_field_action_t ble_action(const cerberus_field_menu_t *menu, unsigned index)
{
    if (index == 8U) return menu->ble_gatt_available ?
        CERBERUS_FIELD_BLE_FIND_MY_SOUND : CERBERUS_FIELD_NONE;
    if (!menu->ble_advertiser_available) return CERBERUS_FIELD_NONE;
    switch (index) {
    case 0: return CERBERUS_FIELD_BLE_SOUR_APPLE;
    case 1: return menu->ble_connectable_available ?
        CERBERUS_FIELD_BLE_APPLE_JUICE : CERBERUS_FIELD_NONE;
    case 2: return CERBERUS_FIELD_BLE_SWIFT_PAIR;
    case 3: return CERBERUS_FIELD_BLE_SAMSUNG;
    case 4: return CERBERUS_FIELD_BLE_GOOGLE;
    case 5: return CERBERUS_FIELD_BLE_FLIPPER;
    case 6: return menu->ble_connectable_available ?
        CERBERUS_FIELD_BLE_ALL : CERBERUS_FIELD_NONE;
    case 7: return CERBERUS_FIELD_BLE_SPOOF_AIRTAG; /* Opens capture selection first. */
    default: return CERBERUS_FIELD_NONE;
    }
}
static const char *ble_description(const cerberus_field_menu_t *menu, unsigned index)
{
    if ((index == 1U || index == 6U) && !menu->ble_connectable_available)
        return "NEEDS S3 CONNECTABLE ROLE";
    if (index == 8U) return menu->ble_gatt_available ?
        "ENDLESS / S3 SCAN + GATT / STOP" : "S3 BLE CENTRAL UNAVAILABLE";
    if (!menu->ble_advertiser_available) return "S3 BLE ADVERTISER UNAVAILABLE";
    if (index == 7U) {
        if (!menu->ble_capture_selected) return "SELECT OBSERVED BLE DEVICE";
        return menu->ble_capture_replayable ? "SELECTED RAW AD READY" :
            "SELECT RANDOM-STATIC DEVICE";
    }
    return "START / S3 ADVERTISER";
}
static cerberus_field_action_t ap_action(const cerberus_field_menu_t *menu,unsigned index)
{
    if(!menu->wifi_target_valid)return CERBERUS_FIELD_NONE;
    switch(index){
    case 0:return menu->wifi_deauth_available?CERBERUS_FIELD_WIFI_DEAUTH:CERBERUS_FIELD_NONE;
    case 1:return menu->wifi_bad_message_available?CERBERUS_FIELD_WIFI_BAD_MESSAGE:CERBERUS_FIELD_NONE;
    case 2:return menu->wifi_assoc_sleep_available?CERBERUS_FIELD_WIFI_ASSOC_SLEEP:CERBERUS_FIELD_NONE;
    case 3:return menu->wifi_sae_available?CERBERUS_FIELD_WIFI_SAE:CERBERUS_FIELD_NONE;
    case 4:return menu->wifi_channel_switch_available?CERBERUS_FIELD_WIFI_CHANNEL_SWITCH:CERBERUS_FIELD_NONE;
    case 5:return menu->wifi_quiet_available?CERBERUS_FIELD_WIFI_QUIET:CERBERUS_FIELD_NONE;
    case 6:return menu->wifi_probe_available?CERBERUS_FIELD_WIFI_PROBE:CERBERUS_FIELD_NONE;
    case 7:return menu->wifi_ap_clone_available?CERBERUS_FIELD_WIFI_AP_CLONE:CERBERUS_FIELD_NONE;
    case 8:return menu->wifi_portal_available?CERBERUS_FIELD_WIFI_PORTAL:CERBERUS_FIELD_NONE;
    default:return CERBERUS_FIELD_NONE;
    }
}
static cerberus_field_action_t attack_action(const cerberus_field_menu_t *menu, unsigned index)
{
    return menu->bluetooth ? ble_action(menu, index) : ap_action(menu, index);
}
static unsigned attack_count(const cerberus_field_menu_t *menu)
{
    return menu->bluetooth ? COUNT(ble_attacks) : COUNT(wifi_attacks);
}
static unsigned last_page(const cerberus_field_menu_t *menu)
{
    return (attack_count(menu) - 1U) / ATTACK_ROWS;
}
static esp_err_t button(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    const char *label, bool enabled, const cerberus_ui_widget_theme_t *theme)
{
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){x,y,width,height},
        label, 1, enabled, theme);
}
esp_err_t cerberus_field_menu_draw(const cerberus_field_menu_t *menu,
    const cerberus_ui_widget_theme_t *theme)
{
    if (!menu || !theme || menu->page > last_page(menu)) return ESP_ERR_INVALID_ARG;
    TRY(button(12, 56, 94, 36, "SCAN", true, theme));
    TRY(button(113, 56, 94, 36, "ATTACK", true, theme));
    TRY(button(214, 56, 94, 36, "RECON", true, theme));
    TRY(cerberus_display_fill_rect(menu->attack ? 114 : 13, 90, 92, 2,
        menu->attack ? 0xF471 : 0x3F1B));
    if (!menu->attack) {
        TRY(cerberus_display_draw_text(14, 103, "SELECT A SCAN TOOL", theme->muted, theme->background, 1));
        for (size_t i = 0; i < COUNT(scans); ++i) {
            uint16_t y = (uint16_t)(SCAN_ROW_TOP + i * SCAN_ROW_STEP);
            TRY(cerberus_display_fill_rect(12, y, 296, SCAN_ROW_HEIGHT, theme->panel));
            TRY(cerberus_display_fill_rect(12, y, 3, SCAN_ROW_HEIGHT, 0x3F1B));
            TRY(cerberus_display_draw_text(24, y + 4, scans[i].title, theme->text, theme->panel, 2));
            TRY(cerberus_display_draw_text(24, y + 23, scans[i].description, theme->muted, theme->panel, 1));
        }
    } else {
        TRY(button(12, 104, 144, 32, "WI-FI ATTACKS", true, theme));
        TRY(button(164, 104, 144, 32, "BLUETOOTH", true, theme));
        TRY(cerberus_display_fill_rect(menu->bluetooth ? 165 : 13, 135, 142, 2, 0xF471));
        char target[48];
        if (menu->bluetooth && menu->ble_capture_selected && menu->ble_target[0])
            snprintf(target,sizeof(target),"CHANGE BLE: %.32s",menu->ble_target);
        else if (menu->bluetooth) snprintf(target,sizeof(target),"SCAN / SELECT BLE DEVICE");
        else if (menu->wifi_target_valid) snprintf(target,sizeof(target),"CHANGE AP: %.32s",menu->wifi_target);
        else snprintf(target,sizeof(target),"SELECT APS FROM SCAN");
        TRY(button(12,144,296,34,target,true,theme));
        TRY(cerberus_display_draw_text(14,186,menu->bluetooth ? "BLUETOOTH ATTACKS / S3 RADIO" :
            menu->wifi_target_valid ? menu->wifi_target_detail : "TAP ABOVE, THEN CHOOSE AN AP FOR ATTACKS",
            theme->muted,theme->background,1));
        const char *const *items = menu->bluetooth ? ble_attacks : wifi_attacks;
        unsigned count = attack_count(menu);
        for (unsigned row = 0; row < ATTACK_ROWS; ++row) {
            unsigned index = menu->page * ATTACK_ROWS + row;
            if (index >= count) break;
            uint16_t y = (uint16_t)(208U + row * 42U);
            bool available=attack_action(menu,index)!=CERBERUS_FIELD_NONE;
            TRY(cerberus_display_fill_rect(12, y, 296, 36, theme->panel));
            TRY(cerberus_display_draw_text(24, y + 5, items[index], available?theme->text:theme->muted, theme->panel, 1));
            TRY(cerberus_display_draw_text(24, y + 21,
                menu->bluetooth ? ble_description(menu,index) :
                available?(index==8?"LOCAL S3 PORTAL / C5 AP PREFLIGHT":index==7?"CONFIGURE / SELECTED SSID CLONE":index==6?"CONFIGURE / AP-DIRECTED PROBES":index==0?"CONFIGURE / BROADCAST OR STATION":index<=2?"CONFIGURE / OBSERVED STATION":"CONFIGURE / AP BEACON ANNOUNCEMENT"):
                !menu->wifi_target_valid ? "SELECT AP FROM SCAN FIRST" : "NOT PORTED / UNAVAILABLE",
                theme->muted, theme->panel, 1));
        }
        char label[24];
        snprintf(label, sizeof(label), "PAGE %u / %u", menu->page + 1U, last_page(menu) + 1U);
        TRY(button(12, 338, 80, 32, "PREVIOUS", menu->page != 0, theme));
        TRY(cerberus_display_draw_text(109, 350, label, theme->text, theme->background, 1));
        TRY(button(228, 338, 80, 32, "NEXT", menu->page < last_page(menu), theme));
        TRY(button(12, 378, 296, 32, menu->bluetooth ? "RADIO LAB / S3 + C5" : "NETWORK TOOLS",
            menu->bluetooth || menu->wifi_target_valid, theme));
    }
    return button(64, 420, 192, 44, "HOME", true, theme);
}
cerberus_field_action_t cerberus_field_menu_tap(cerberus_field_menu_t *menu,
    uint16_t x, uint16_t y)
{
    if (!menu || x >= 320 || y >= 480) return CERBERUS_FIELD_NONE;
    if (y >= 56 && y < 92) {
        if (x >= 12 && x < 106) { menu->attack = false; return CERBERUS_FIELD_REDRAW; }
        if (x >= 113 && x < 207) { menu->attack = true; return CERBERUS_FIELD_REDRAW; }
        if (x >= 214 && x < 308) return CERBERUS_FIELD_RECON;
    }
    if (x >= 64 && x < 256 && y >= 420 && y < 464) return CERBERUS_FIELD_HOME;
    if (!menu->attack && x >= 12 && x < 308 && y >= SCAN_ROW_TOP) {
        unsigned row = (y - SCAN_ROW_TOP) / SCAN_ROW_STEP;
        if (row < COUNT(scans) && (y - SCAN_ROW_TOP) % SCAN_ROW_STEP < SCAN_ROW_HEIGHT)
            return scans[row].action;
    }
    if (menu->attack) {
        if(x>=12&&x<308&&y>=208&&y<334&&(y-208U)%42U<36U)
            return attack_action(menu,menu->page*ATTACK_ROWS+(y-208U)/42U);
        if (y >= 104 && y < 136) {
            if (x >= 12 && x < 156) { menu->bluetooth = false; menu->page = 0; return CERBERUS_FIELD_REDRAW; }
            if (x >= 164 && x < 308) { menu->bluetooth = true; menu->page = 0; return CERBERUS_FIELD_REDRAW; }
        }
        if (x >= 12 && x < 308 && y >= 378 && y < 410)
            return menu->bluetooth ? CERBERUS_FIELD_RADIO_LAB :
                menu->wifi_target_valid ? CERBERUS_FIELD_WIFI_TARGET : CERBERUS_FIELD_NONE;
        if (x >= 12 && x < 308 && y >= 144 && y < 178)
            return menu->bluetooth?CERBERUS_FIELD_BLE:CERBERUS_FIELD_WIFI_SELECT;
        if (y >= 338 && y < 370) {
            if (x >= 12 && x < 92 && menu->page > 0) { --menu->page; return CERBERUS_FIELD_REDRAW; }
            if (x >= 228 && x < 308 && menu->page < last_page(menu)) { ++menu->page; return CERBERUS_FIELD_REDRAW; }
        }
    }
    return CERBERUS_FIELD_NONE;
}
