"""Actual MAC Monitor menu/lifetime with production controller and rendering."""
from pathlib import Path
import os,re,subprocess,sys
root=Path(__file__).resolve().parents[1]
out=root/'artifacts/mac-monitor-live-ui-checks';out.mkdir(parents=True,exist_ok=True)
ui=(root/'s3_main/main/ui.c').read_text()
def end_block(text,opening):
    depth=0
    for i in range(opening,len(text)):
        if text[i]=='{':depth+=1
        elif text[i]=='}':
            depth-=1
            if not depth:return i+1
    raise ValueError('Unclosed production block')
enums=re.findall(r'typedef enum \{[^}]*\} ui_page_t;',ui);assert len(enums)==1
(out/'mac_route_types.h').write_text(enums[0])
start=ui.index('static void handle_tap(uint16_t x, uint16_t y)\n{')
handler=ui[start:end_block(ui,ui.index('{',start))]
guard=handler.index('    if (x >= CERBERUS_LCD_WIDTH')
mac=handler.index('    if (current_page == UI_PAGE_MAC_MONITOR)')
field=handler.index('    if (current_page == UI_PAGE_FIELD_MENU)')
route=handler.index('        if (action == CERBERUS_FIELD_MAC_MONITOR)',field)
parts=[handler[guard:end_block(handler,handler.index('{',guard))],
       handler[mac:end_block(handler,handler.index('{',mac))],
       handler[field:end_block(handler,handler.index('{',route))]+'\n    }']
(out/'mac_routes.h').write_text('static void actual_mac_route(uint16_t x,uint16_t y)\n{\n'+'\n'.join(parts)+'\n}\n')
assert 'if (current_page == UI_PAGE_MAC_MONITOR) return mac_monitor_draw(true);' in ui
assert 'mac_monitor_tick((uint32_t)now_ms, screen_asleep);' in ui
assert '!mac_monitor_keeps_awake()' in ui
assert mac < field
env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(out/'zig-cache'));exe=out/'check.exe'
subprocess.run([sys.executable,'-m','ziglang','cc','-std=c11','-O1','-Wall','-Wextra','-Werror',
    '-I',str(out),'-I',str(root/'tools/host_checks/stubs'),'-I',str(root/'s3_main/main'),'-I',str(root/'shared'),
    str(root/'tools/host_checks/mac_monitor_live_ui_check.c'),
    *[str(root/'s3_main/main'/name) for name in ('mac_monitor_inventory.c','mac_monitor_session.c','mac_monitor_ui.c','field_menu.c','ui_widgets.c','privacy.c')],
    str(root/'shared/mac_observation.c'),'-o',str(exe)],check=True,env=env)
subprocess.run([str(exe)],check=True)
