"""Exercise actual passive relationship UI, incremental reads and bounded SD report."""
from pathlib import Path
import os, re, struct, subprocess, sys, zlib
root = Path(__file__).resolve().parents[1]
out = root / 'artifacts/wifi-relation-ui-checks'
out.mkdir(parents=True, exist_ok=True)
display = (root / 's3_main/main/display.c').read_text()
start = display.index('static const uint8_t *font5x7(')
(out / 'display_font.h').write_text(display[start:display.index('\nesp_err_t ', start)])
(out / 'relation_framebuffer.h').write_text((root / 'tools/host_checks/recon_render.c').read_text().split('/* Exercise the real private controls', 1)[0].replace(
    'if(!*text)return ESP_OK;', 'if(!*text)return ESP_OK; relation_text(x,y,text,scale);').replace(
    'bounds(x,y,w,h);\n    for(unsigned row=0;row<h;++row)for',
    'bounds(x,y,w,h); relation_rect(x,y,w,h);\n    for(unsigned row=0;row<h;++row)for'))
ui = (root / 's3_main/main/ui.c').read_text()
live = (root / 's3_main/main/sentinel_live_ui.h').read_text()

def close(text, opening, left='{', right='}'):
    depth = 0
    for i in range(opening, len(text)):
        if text[i] == left: depth += 1
        elif text[i] == right:
            depth -= 1
            if not depth: return i + 1
    raise ValueError('Unclosed production block')

def function(text, signature):
    start = text.index(signature + '\n{')
    return text[start:close(text, text.index('{', start))]

def scoped(text):
    # Retain the complete parent dispatcher/renderer and every condition/order.
    # Bodies belonging exclusively to other pages/modes are fail-fast stubs;
    # no Wi-Fi relation/target/list or generic navigation body is substituted.
    edits = []
    for match in re.finditer(r'\bif\s*\(', text):
        begin = text.index('(', match.start())
        end = close(text, begin, '(', ')')
        condition = text[begin + 1:end - 1].strip()
        body = end
        while body < len(text) and text[body].isspace(): body += 1
        if body == len(text) or text[body] != '{': continue
        page = re.match(r'current_page == (UI_PAGE_\w+)', condition)
        mode = re.match(r'(?:current_page == UI_PAGE_WIFI && )?wifi_mode == (UI_WIFI_MODE_\w+)', condition)
        unrelated = bool(page and page[1] != 'UI_PAGE_WIFI')
        unrelated |= bool(mode and mode[1] not in ('UI_WIFI_MODE_TARGET','UI_WIFI_MODE_OBSERVED_CLIENTS','UI_WIFI_MODE_LIST'))
        if unrelated:
            edits.append((body, close(text, body), '{ assert(!"Unrelated page/mode body reached"); }'))
    for begin, end, replacement in reversed(edits):
        if any(a < begin and end <= b for a, b, _ in edits): continue
        text = text[:begin] + replacement + text[end:]
    return text

enums = '\n'.join(re.findall(r'typedef enum \{[^}]*\} ui_\w+;', ui))
(out / 'relation_route_types.h').write_text(enums)
helpers = '\n'.join(function(ui, sig) for sig in (
    'static esp_err_t draw_header(const char *title)',
    'static esp_err_t draw_button(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *label)'))
parent = scoped(function(ui, 'static esp_err_t draw_detail_page(void)'))
tap = scoped(function(ui, 'static void handle_tap(uint16_t x, uint16_t y)'))
# The other-page single-line early returns are also compile-time fail-fast
# stubs below; actual generic guards and Wi-Fi branches remain in order.
wifi = function(ui, 'static void handle_wifi_tap(uint16_t x, uint16_t y)')
wifi = wifi[:wifi.index('    if (wifi_mode == UI_WIFI_MODE_MENU)')] + '    assert(!"Unrelated Wi-Fi body reached");\n}'
wifi = scoped(wifi)
draw = function(ui, 'static esp_err_t draw_wifi(void)')
draw = draw[:draw.index('    char line[64];')] + '    assert(!"Unrelated Wi-Fi draw reached"); return ESP_FAIL;\n}'
draw = scoped(draw)
(out / 'relation_parent_impl.h').write_text(helpers + '\n' + draw + '\n' + parent + '\n' + wifi + '\n' + tap)
known = {'if','while','for','sizeof','strcmp','memset','assert','ESP_RETURN_ON_ERROR','ESP_ERROR_CHECK',
    'draw_header','draw_button','draw_detail_page','draw_wifi','handle_tap','handle_wifi_tap',
    'cerberus_display_fill_rect','cerberus_display_draw_text','cerberus_ui_widget_draw_button',
    'find_item','draw_centered','draw_wifi_target','wifi_relation_draw','wifi_relation_back','wifi_relation_tap',
    'wifi_target_handle_tap','live_is_view','live_page_tool','live_toggle_pause','live_scan_action_label',
    'draw_live_scan','draw_current_page','return_to_launcher','handle_live_scan_tap','tools_group_title'}
stubs = sorted(set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', helpers + draw + parent + wifi + tap)) - known)
(out / 'relation_unused_routes.h').write_text('\n'.join('#define '+name+'(...) unexpected_call()' for name in stubs) + '\n')
(out / 'relation_live_helpers.h').write_text(function(live, 'static sentinel_scan_tool_t live_page_tool(void)') + '\n' +
    function(live, 'static bool live_is_view(void)') + '\n' +
    function(live, 'static void live_toggle_pause(void)'))
live_tap = function(live, 'static void handle_live_scan_tap(uint16_t x, uint16_t y)')
begin = live_tap.index('if (action == SENTINEL_VIEW_PAUSE)')
pause = live_tap[begin:close(live_tap, live_tap.index('{', begin))]
entry = function(live, 'static void live_scan_tick(uint32_t now, bool asleep)')
begin = entry.index('    bool cached_wifi_view =')
entry = entry[begin:entry.index('    sentinel_scan_session_select(&live_ble_session, ble_desired);', begin)]
(out / 'relation_scan_controls.h').write_text(
    'static void handle_live_scan_tap(uint16_t x,uint16_t y)\n{\n'
    '    sentinel_scan_tool_t tool = live_page_tool();\n'
    '    sentinel_view_action_t action = sentinel_scan_view_hit_test(&live_view, x, y);\n' +
    pause + '\n}\n'
    'static void actual_scan_page_selection(void)\n{\n'
    '    bool asleep = false; sentinel_scan_tool_t desired = live_page_tool();\n'
    '    bool changed = live_session.desired != desired;\n' + entry + '\n}\n')
exe = out / 'relation-ui.exe'
env = os.environ.copy(); env['ZIG_GLOBAL_CACHE_DIR'] = str(out / 'zig-cache')
subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
    '-I', str(out), '-I', str(root / 'tools/host_checks/stubs'), '-I', str(root / 's3_main/main'), '-I', str(root / 'shared'),
    str(root / 'tools/host_checks/wifi_relation_ui_check.c'), str(root / 's3_main/main/s3_wifi_relation.c'),
    str(root / 's3_main/main/ui_widgets.c'), str(root / 's3_main/main/wifi_target_view.c'),
    str(root / 's3_main/main/sentinel_scan_view.c'), str(root / 's3_main/main/sentinel_scan_session.c'),
    '-o', str(exe)], check=True, env=env)
subprocess.run([str(exe), str(out / 'relation')], check=True, env=env)
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
for ppm in out.glob('relation-*.ppm'):
    raw = ppm.read_bytes(); header = b'P6\n320 480\n255\n'
    assert raw.startswith(header) and len(raw) == len(header) + 320*480*3
    pixels = raw[len(header):]
    ppm.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 320, 480, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(b''.join(b'\0' + pixels[y*960:(y+1)*960] for y in range(480)))) + chunk(b'IEND', b''))
print(out)
