#!/usr/bin/env python3
"""Render the actual Watch C scenes using the actual display font.

Checks all bounds/hit areas, independent update regions, copied mutable text,
paused/unchanged frames, invalid inputs, display-failure repair and pixel parity.
Outputs scene PNGs, a gallery and a self-contained animated HTML preview. No
image libraries are required. Uses the installed Zig host compiler by default.
"""
from __future__ import annotations

import argparse
import base64
import os
from pathlib import Path
import shlex
import subprocess
import sys

from preview_sentinel_dashboard import HOST_C, ROOT, ppm_to_png
from preview_sentinel_scan import TRACE_C

TEST_C = r'''
#include "sentinel_watch_view.h"

static void parity(const sentinel_watch_view_t *v,uint32_t now)
{
    memcpy(expected,framebuffer,sizeof(expected));trace_enabled=false;
    assert(sentinel_watch_view_draw(v,now)==ESP_OK);
    assert(memcmp(expected,framebuffer,sizeof(expected))==0);
}
static void transition(const sentinel_watch_view_t *v,uint32_t now)
{
    trace_reset();assert(sentinel_watch_view_update(v,now)==ESP_OK);
    if(draw_count)trace_region(0,0,320,480);
    parity(v,now);
}
static void hits(sentinel_watch_view_t *v)
{
    const unsigned b[][5]={
        {8,426,68,44,SENTINEL_WATCH_ACTION_BACK},
        {84,426,68,44,SENTINEL_WATCH_ACTION_PAUSE},
        {160,426,68,44,SENTINEL_WATCH_ACTION_HISTORY},
        {236,426,76,44,SENTINEL_WATCH_ACTION_DEMO},
        {8,348,304,64,SENTINEL_WATCH_ACTION_DETAILS}};
    for(unsigned demo=0;demo<2;++demo)for(unsigned detail=0;detail<2;++detail){
        v->demo=demo!=0;v->has_details=detail!=0;
        for(unsigned y=0;y<480;++y)for(unsigned x=0;x<320;++x){
            unsigned result=SENTINEL_WATCH_ACTION_NONE;
            for(unsigned i=0;i<5;++i){
                if((i==4 && !detail)||(demo && (i==1 || i==2)))continue;
                if(x>=b[i][0] && x<b[i][0]+b[i][2] && y>=b[i][1] && y<b[i][1]+b[i][3])result=b[i][4];
            }
            assert((unsigned)sentinel_watch_view_hit_test(v,(uint16_t)x,(uint16_t)y)==result);
        }
    }
    assert(sentinel_watch_view_hit_test(v,65535,65535)==SENTINEL_WATCH_ACTION_NONE);
}
static uint32_t art_hash(void)
{
    uint32_t hash=2166136261U;
    for(unsigned y=90;y<314;++y)for(unsigned x=12;x<308;++x){
        hash=(hash^framebuffer[y*320+x])*16777619U;
    }
    return hash;
}
int main(void)
{
    const char *names[]={"guardian","axon","flock","wellness","findmy","tracker","camera","smart-glasses",
        "flipper","meshtastic","samsung","beacon","pwnagotchi","drone","ble-activity"};
    _Static_assert(sizeof(names)/sizeof(names[0])==SENTINEL_WATCH_SCENE_COUNT,"preview every scene");
    sentinel_watch_view_t v={.radio_online=true,.phase="BLE LISTENING / 4S LEFT"};
    assert(sentinel_watch_view_draw(NULL,0)==ESP_ERR_INVALID_ARG);
    assert(sentinel_watch_view_update(NULL,0)==ESP_ERR_INVALID_ARG);
    assert(sentinel_watch_view_hit_test(NULL,8,426)==SENTINEL_WATCH_ACTION_NONE);
    assert(sentinel_watch_view_draw(&v,0)==ESP_OK);save("watch-live.ppm");
    trace_reset();assert(sentinel_watch_view_update(&v,0)==ESP_OK);assert(draw_count==0);
    trace_reset();assert(sentinel_watch_view_update(&v,399)==ESP_OK);assert(draw_count==0);
    trace_reset();assert(sentinel_watch_view_update(&v,400)==ESP_OK);trace_region(12,90,296,224);parity(&v,400);
    v.paused=true;transition(&v,1600);
    trace_reset();assert(sentinel_watch_view_update(&v,UINT32_MAX)==ESP_OK);assert(draw_count==0);
    save("watch-paused.ppm");

    char phase[90]="BLE LISTENING / 3S LEFT",title[90]="ON WATCH";
    char evidence[90]="Clue in an observed broadcast",detail[90]="Read the evidence before drawing conclusions.";
    v.phase=phase;v.title=title;v.evidence=evidence;v.detail=detail;
    transition(&v,1600);
    strcpy(phase,"WI-FI SWEEP / 2S LEFT");
    trace_reset();assert(sentinel_watch_view_update(&v,1600)==ESP_OK);trace_region(0,0,320,88);parity(&v,1600);
    strcpy(title,"SHORTER TITLE");strcpy(evidence,"SHORT");strcpy(detail,"SHORT");
    trace_reset();assert(sentinel_watch_view_update(&v,1600)==ESP_OK);trace_region(0,318,320,95);parity(&v,1600);
    memset(phase,'P',sizeof(phase)-1);phase[sizeof(phase)-1]=0;
    memset(title,'T',sizeof(title)-1);title[sizeof(title)-1]=0;
    memset(evidence,'E',sizeof(evidence)-1);evidence[sizeof(evidence)-1]=0;
    memset(detail,'D',sizeof(detail)-1);detail[sizeof(detail)-1]=0;transition(&v,1600);
    evidence[55]='Z';trace_reset();assert(sentinel_watch_view_update(&v,1600)==ESP_OK);assert(draw_count==0);
    evidence[0]='\n';evidence[1]=(char)0xFF;transition(&v,1600);
    v.phase=v.title=v.evidence=v.detail=NULL;v.paused=false;v.demo=true;v.has_details=false;
    v.observation_count=UINT16_MAX;v.match_count=UINT16_MAX;

    uint32_t scene_hashes[SENTINEL_WATCH_SCENE_COUNT]={0};
    for(unsigned scene=0;scene<SENTINEL_WATCH_SCENE_COUNT;++scene){
        v.scene=(sentinel_watch_scene_t)scene;v.confidence=(sentinel_watch_confidence_t)(scene%4);
        bool animated=false;
        for(unsigned frame=0;frame<20;++frame){
            transition(&v,frame*400U);
            uint32_t hash=art_hash();
            if(frame==0)scene_hashes[scene]=hash;
            else if(hash!=scene_hashes[scene])animated=true;
            trace_reset();assert(sentinel_watch_view_update(&v,frame*400U)==ESP_OK);assert(draw_count==0);
            if(frame==5){char name[80];snprintf(name,sizeof(name),"watch-%s.ppm",names[scene]);save(name);}
            char name[80];snprintf(name,sizeof(name),"frame-%u-%02u.ppm",scene,frame);save(name);
        }
        assert(animated);
        for(unsigned i=0;i<scene;++i)assert(scene_hashes[i]!=scene_hashes[scene]);
    }
    v.demo=false;v.paused=true;v.has_details=true;v.radio_online=false;transition(&v,0);
    for(unsigned c=0;c<=SENTINEL_WATCH_CONFIDENCE_HIGH;++c){v.confidence=(sentinel_watch_confidence_t)c;transition(&v,0);}
    /* A failed partial drawing may modify the work surface: repair fully. */
    v.evidence="NEW EVIDENCE";trace_reset();fail_after=2;
    assert(sentinel_watch_view_update(&v,0)==ESP_FAIL);fail_after=-1;
    trace_reset();assert(sentinel_watch_view_update(&v,0)==ESP_OK);
    assert(draw_count>0 && draw_operations[0].width==320 && draw_operations[0].height==480);parity(&v,0);
    sentinel_watch_view_invalidate();trace_reset();assert(sentinel_watch_view_update(&v,0)==ESP_OK);
    assert(draw_count>0 && draw_operations[0].height==480);
    hits(&v);
    v.scene=SENTINEL_WATCH_SCENE_COUNT;assert(sentinel_watch_view_update(&v,0)==ESP_ERR_INVALID_ARG);
    assert(sentinel_watch_view_hit_test(&v,8,426)==SENTINEL_WATCH_ACTION_NONE);
    v.scene=SENTINEL_WATCH_SCENE_GUARDIAN;v.confidence=(sentinel_watch_confidence_t)99;
    assert(sentinel_watch_view_draw(&v,0)==ESP_ERR_INVALID_ARG);
    v.confidence=SENTINEL_WATCH_CONFIDENCE_NONE;fail_display=1;
    assert(sentinel_watch_view_draw(&v,0)==ESP_FAIL);fail_display=0;
    transition(&v,0);
    puts("Watch: 15 distinct scenes, 300 animation frames, bounds, exact hit areas, cache, mutable text, failure repair and pixel parity passed.");
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "sentinel-watch")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "esp_err.h").write_text(
        "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
        "#define ESP_ERR_INVALID_ARG 0x102\n", encoding="utf-8")
    display = (ROOT / "s3_main/main/display.c").read_text(encoding="utf-8")
    start = display.index("static const uint8_t *font5x7")
    end = display.index("esp_err_t cerberus_display_draw_text", start)
    host = HOST_C[:HOST_C.index("int main(void)")].replace("FONT_FUNCTION", display[start:end])
    host = host.replace("static int fail_display;", "static int fail_display;\n" + TRACE_C)
    host = host.replace("    assert(w > 0 && h > 0", """    if (trace_enabled) {
        assert(draw_count < sizeof(draw_operations) / sizeof(draw_operations[0]));
        draw_operations[draw_count++] = (draw_operation_t){x,y,w,h};
    }
    assert(w > 0 && h > 0""")
    code = (host + TEST_C).replace("if (fail_display) return ESP_FAIL;", "if (display_failure()) return ESP_FAIL;")
    source = output / "watch_host.c"
    source.write_text(code, encoding="utf-8")
    executable = output / ("watch_host.exe" if os.name == "nt" else "watch_host")
    compiler = shlex.split(args.cc) if args.cc else [sys.executable, "-m", "ziglang", "cc"]
    subprocess.run(compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I", str(output), "-I", str(ROOT / "s3_main/main"), str(source),
        str(ROOT / "s3_main/main/sentinel_watch_view.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], cwd=output, check=True)
    names = ["guardian", "axon", "flock", "wellness", "findmy", "tracker", "camera", "smart-glasses",
             "flipper", "meshtastic", "samsung", "beacon", "pwnagotchi", "drone", "ble-activity"]
    for path in sorted(output.glob("watch-*.ppm")):
        if path.name != "watch-gallery.ppm":
            print(ppm_to_png(path))
    tiles = []
    for name in names:
        with (output / f"watch-{name}.ppm").open("rb") as file:
            for _ in range(3):
                file.readline()
            tiles.append(file.read())
    gallery = output / "watch-gallery.ppm"
    columns = 5
    rows = (len(tiles) + columns - 1) // columns
    with gallery.open("wb") as file:
        file.write(f"P6\n{320 * columns} {480 * rows}\n255\n".encode("ascii"))
        for row in range(rows):
            for y in range(480):
                for col in range(columns):
                    index = row * columns + col
                    file.write(tiles[index][y * 960:(y + 1) * 960] if index < len(tiles) else bytes(960))
    print(ppm_to_png(gallery))
    frames = []
    for scene in range(len(names)):
        frame_urls = []
        for frame in range(20):
            path = ppm_to_png(output / f"frame-{scene}-{frame:02d}.ppm")
            frame_urls.append("data:image/png;base64," + base64.b64encode(path.read_bytes()).decode("ascii"))
        frames.append(frame_urls)
    import json
    html = """<!doctype html><meta charset=utf-8><title>Cerberus Watch scenes</title>
<style>body{background:#060b15;color:#e5eff9;font:16px system-ui;margin:28px}h1{font-size:24px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:24px}
figure{margin:0}img{width:320px;height:480px;image-rendering:pixelated;border:1px solid #223549}
figcaption{margin:8px 0;color:#b79dff}button{padding:12px 22px;background:#223549;border:0;color:white;margin:0 0 20px}</style>
<h1>Cerberus Watch / animated scene gallery</h1><p>Exact firmware pixels. Demonstration art; no live detections.</p>
<button id=toggle>Pause animation</button><div class=grid>"""
    html += "".join(f"<figure><img id=s{i} alt='{name}'><figcaption>{name}</figcaption></figure>" for i, name in enumerate(names))
    html += "</div><script>const frames=" + json.dumps(frames) + ";let f=0,playing=true;function render(){frames.forEach((a,i)=>document.getElementById('s'+i).src=a[f]);}render();setInterval(()=>{if(playing){f=(f+1)%20;render();}},400);document.getElementById('toggle').onclick=function(){playing=!playing;this.textContent=playing?'Pause animation':'Play animation';};</script>"
    (output / "watch-animation.html").write_text(html, encoding="utf-8")
    print(output / "watch-animation.html")


if __name__ == "__main__":
    main()
