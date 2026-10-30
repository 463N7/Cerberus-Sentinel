#include "sentinel_watch_view.h"
#include "display.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define BG RGB(6,11,21)
#define PANEL RGB(13,23,37)
#define EDGE RGB(34,53,73)
#define MUTED RGB(127,149,170)
#define WHITE RGB(229,239,249)
#define CYAN RGB(67,227,219)
#define LILAC RGB(183,157,255)
#define PINK RGB(248,133,188)
#define AMBER RGB(255,194,99)
#define DARK RGB(10,18,30)
#define STEEL RGB(104,137,159)
#define ARMOR RGB(45,68,92)
#define LIGHT RGB(159,184,203)
#define RED RGB(255,65,88)
#define BLUE RGB(65,139,255)
#define MINT RGB(104,241,166)
#define ORANGE RGB(246,151,68)
#define TRY(call) do { esp_err_t e_=(call); if(e_!=ESP_OK) return e_; } while(0)

typedef struct { int16_t x,y; } point_t;
typedef struct { uint16_t x,y,w,h; sentinel_watch_action_t action; } target_t;
static const target_t targets[] = {
    {8,426,68,44,SENTINEL_WATCH_ACTION_BACK},
    {84,426,68,44,SENTINEL_WATCH_ACTION_PAUSE},
    {160,426,68,44,SENTINEL_WATCH_ACTION_HISTORY},
    {236,426,76,44,SENTINEL_WATCH_ACTION_DEMO},
    {8,348,304,64,SENTINEL_WATCH_ACTION_DETAILS},
};

/* The original artwork is rasterized in eight-row stripes. The driver owns
 * composition; this module needs only 4736 bytes, with no heap allocation. */
#define ART_X 12
#define ART_Y 90
#define ART_W 296
#define ART_H 224
#define STRIPE_ROWS 8
static uint16_t stripe[ART_W*STRIPE_ROWS];
static int stripe_y;

typedef struct {
    sentinel_watch_scene_t scene;
    sentinel_watch_confidence_t confidence;
    bool demo,paused,online,details;
    uint16_t observations,matches;
    uint8_t frame;
    char phase[49],title[25],evidence[49],detail[49];
} snapshot_t;
static snapshot_t previous;
static bool previous_valid;

static void px(int x,int y,uint16_t c)
{
    if(x>=ART_X && x<ART_X+ART_W && y>=stripe_y && y<stripe_y+STRIPE_ROWS)
        stripe[(y-stripe_y)*ART_W+x-ART_X]=c;
}
static void box(int x,int y,int w,int h,uint16_t c)
{
    int l=x>ART_X?x:ART_X, r=x+w<ART_X+ART_W?x+w:ART_X+ART_W;
    int t=y>stripe_y?y:stripe_y, b=y+h<stripe_y+STRIPE_ROWS?y+h:stripe_y+STRIPE_ROWS;
    for(int yy=t;yy<b;++yy) for(int xx=l;xx<r;++xx) px(xx,yy,c);
}
static void line(int x0,int y0,int x1,int y1,uint16_t c)
{
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    for(;;){px(x0,y0,c); if(x0==x1 && y0==y1)break; int e=2*err;
        if(e>=dy){err+=dy;x0+=sx;} if(e<=dx){err+=dx;y0+=sy;}}
}
static void poly(const point_t *p,size_t count,uint16_t fill,uint16_t edge)
{
    for(int y=stripe_y;y<stripe_y+STRIPE_ROWS;++y){
        int intersections[16]; size_t used=0;
        for(size_t i=0,j=count-1;i<count;j=i++){
            int y0=p[j].y,y1=p[i].y;
            if((y0<=y && y1>y)||(y1<=y && y0>y))
                if(used<16)intersections[used++]=p[j].x+(y-y0)*(p[i].x-p[j].x)/(y1-y0);
        }
        for(size_t i=1;i<used;++i){int n=intersections[i];size_t j=i;
            while(j>0 && intersections[j-1]>n){intersections[j]=intersections[j-1];--j;}intersections[j]=n;}
        for(size_t i=0;i+1<used;i+=2)box(intersections[i],y,intersections[i+1]-intersections[i]+1,1,fill);
    }
    if(edge!=fill)for(size_t i=0,j=count-1;i<count;j=i++)line(p[j].x,p[j].y,p[i].x,p[i].y,edge);
}
#define POLY(fill,edge,...) do { const point_t q_[]={__VA_ARGS__}; poly(q_,sizeof(q_)/sizeof(q_[0]),fill,edge); } while(0)
static void oval(int cx,int cy,int rx,int ry,uint16_t c,bool filled)
{
    for(int y=stripe_y;y<stripe_y+STRIPE_ROWS;++y){
        int dy=y-cy;if(dy < -ry || dy > ry)continue;
        int extent=0;
        for(int x=0;x<=rx;++x){
            if((int64_t)x*x*ry*ry+(int64_t)dy*dy*rx*rx <= (int64_t)rx*rx*ry*ry)extent=x;else break;}
        if(filled)box(cx-extent,y,2*extent+1,1,c);else{px(cx-extent,y,c);px(cx+extent,y,c);}
    }
}
static void heart(int x,int y,int s,uint16_t c)
{
    POLY(c,c,{x,y+s},{x+s,y},{x+2*s,y},{x+3*s,y+s},{x+4*s,y},{x+5*s,y},
        {x+6*s,y+s},{x+6*s,y+2*s},{x+3*s,y+5*s},{x,y+2*s});
}
static void star(int x,int y,uint16_t c)
{ line(x-3,y,x+3,y,c);line(x,y-3,x,y+3,c); }
static uint16_t accent(sentinel_watch_scene_t scene)
{
    if(scene==SENTINEL_WATCH_SCENE_WELLNESS)return PINK;
    if(scene==SENTINEL_WATCH_SCENE_FINDMY)return MINT;
    if(scene==SENTINEL_WATCH_SCENE_TRACKER || scene==SENTINEL_WATCH_SCENE_BEACON ||
       scene==SENTINEL_WATCH_SCENE_FLIPPER)return ORANGE;
    if(scene==SENTINEL_WATCH_SCENE_SAMSUNG)return BLUE;
    if(scene==SENTINEL_WATCH_SCENE_FLOCK ||
       scene==SENTINEL_WATCH_SCENE_DRONE)return AMBER;
    if(scene==SENTINEL_WATCH_SCENE_CAMERA || scene==SENTINEL_WATCH_SCENE_SMART_GLASSES ||
       scene==SENTINEL_WATCH_SCENE_PWNAGOTCHI ||
       scene==SENTINEL_WATCH_SCENE_BLE_ACTIVITY)return LILAC;
    return CYAN;
}

static void footprints(int x,int y,uint16_t c)
{
    for(int i=0;i<4;++i){int xx=x+(i%2)*12,yy=y+i*16;
        oval(xx,yy,3,5,c,true);oval(xx-2,yy-7,1,1,c,true);oval(xx+2,yy-7,1,1,c,true);}
}
static void cloud(int x,int y)
{
    oval(x,y,15,6,EDGE,true);oval(x+14,y-6,13,10,EDGE,true);
    oval(x+29,y,15,6,EDGE,true);line(x-12,y+5,x+39,y+5,STEEL);
}
static void quadcopter(int x,int y,int f,uint16_t a)
{
    line(x-18,y-8,x+18,y+8,STEEL);line(x-18,y+8,x+18,y-8,STEEL);
    for(int side=-1;side<=1;side+=2)for(int row=-1;row<=1;row+=2){
        oval(x+side*18,y+row*8,11,3,STEEL,false);
        line(x+side*18-7,y+row*8+(f%3==0),x+side*18+7,y+row*8-(f%3==0),a);}
    POLY(ARMOR,LIGHT,{x-7,y-6},{x+7,y-6},{x+10,y+5},{x,y+10},{x-10,y+5});
    box(x-3,y+2,6,3,a);line(x-4,y+9,x-7,y+15,STEEL);line(x+4,y+9,x+7,y+15,STEEL);
}
/* Our own small six-sided digital companion; no third-party mascot artwork. */
static void companion(int x,int y,int f)
{
    POLY(RGB(47,38,70),LILAC,{x,y-22},{x+18,y-13},{x+18,y+9},{x,y+21},{x-18,y+9},{x-18,y-13});
    box(x-10,y-7,6,f==18?1:6,CYAN);box(x+5,y-7,6,f==18?1:6,PINK);
    box(x-4,y+5,9,2,LIGHT);line(x,y-22,x,y-29,STEEL);box(x-2,y-32,5,5,CYAN);
    line(x-10,y+17,x-13,y+25,LILAC);line(x+10,y+17,x+13,y+25,LILAC);
    box(x-17,y+25,8,3,STEEL);box(x+10,y+25,8,3,STEEL);
}

static void city(uint8_t frame)
{
    for(int i=0;i<9;++i){int x=18+33*i,top=190-(i*29)%70;
        box(x,top,24,242-top,RGB(18,29,47));line(x,top,x+23,top,EDGE);
        for(int row=top+8;row<233;row+=13)for(int col=x+5;col<x+21;col+=9)
            box(col,row,3,4,((row+col)/7+frame/5)%4==0?STEEL:EDGE);}
}
static void camera(int x,int y,bool right,uint16_t a)
{
    int d=right?1:-1;
    box(x-2,y+20,4,91,STEEL);box(x-8,y+109,16,4,ARMOR);
    line(x,y+33,x+16*d,y+20,STEEL);
    POLY(ARMOR,LIGHT,{x-18,y},{x+18,y},{x+18,y+20},{x-14,y+20},{x-20,y+14});
    box(x-14,y+4,25,11,DARK);box(x-11,y+7,12,2,a);
    box(right?x+17:x-23,y+4,6,13,LIGHT);box(right?x+20:x-23,y+7,3,7,a);
}
static void apple_mark(int x,int y,uint16_t ink,uint16_t surface)
{
    oval(x-4,y,7,9,ink,true);oval(x+4,y,7,9,ink,true);
    oval(x+10,y-3,4,4,surface,true);
    POLY(ink,ink,{x,y-11},{x+3,y-17},{x+8,y-18},{x+6,y-12});
    box(x-1,y+7,3,3,surface);
}
static void locator_tag(int x,int y,int radius)
{
    oval(x,y+3,radius+2,radius+2,RGB(37,65,65),true);
    oval(x,y,radius,radius,WHITE,true);
    oval(x,y,radius-4,radius-4,LIGHT,true);
    oval(x-2,y-2,radius-7,radius-7,RGB(210,224,230),true);
    apple_mark(x,y+2,RGB(79,103,114),RGB(210,224,230));
    line(x-radius+9,y-radius+12,x-8,y-radius+5,WHITE);
}
static void key_tags(int x,int y,int f)
{
    oval(x,y,14,14,WHITE,false);oval(x,y,11,11,STEEL,false);
    int sway=f>=5 && f<13?2:0;
    line(x-7,y+11,x-19+sway,y+31,WHITE);
    line(x+7,y+11,x+13-sway,y+44,WHITE);
    POLY(BLUE,LIGHT,{x-35+sway,y+27},{x-7+sway,y+27},{x-2+sway,y+33},
        {x-2+sway,y+57},{x-8+sway,y+63},{x-35+sway,y+63},{x-40+sway,y+57},{x-40+sway,y+33});
    oval(x-29+sway,y+34,3,3,DARK,true);
    box(x-25+sway,y+43,17,3,WHITE);box(x-18+sway,y+43,3,14,WHITE);
    oval(x+13-sway,y+59,18,18,AMBER,true);oval(x+13-sway,y+59,12,12,RGB(147,87,38),false);
    oval(x+13-sway,y+47,3,3,DARK,true);oval(x+13-sway,y+61,5,5,WHITE,false);
}
static void clapper(int x,int y,int f)
{
    box(x,y,62,38,DARK);box(x,y,62,2,LIGHT);box(x,y+36,62,2,LIGHT);
    box(x,y,2,38,LIGHT);box(x+60,y,2,38,LIGHT);
    int lift=f>=5 && f<13?7:2;
    POLY(WHITE,LIGHT,{x,y-3},{x+61,y-3-lift},{x+60,y-14-lift},{x,y-13});
    for(int i=0;i<5;++i)POLY(DARK,DARK,{x+5+i*12,y-4-i*lift/5},
        {x+11+i*12,y-4-i*lift/5},{x+17+i*12,y-14-i*lift/5},{x+11+i*12,y-14-i*lift/5});
    for(int i=0;i<3;++i)box(x+8,y+8+i*9,44-i*7,2,STEEL);
    box(x+42,y+24,10,7,ORANGE);
}
/* Large, simple props stay readable on the physical 320-pixel-wide display. */
static void handheld(int x,int y,int f)
{
    POLY(ORANGE,AMBER,{x+5,y+3},{x+56,y+3},{x+63,y+11},{x+63,y+48},
        {x+56,y+55},{x+5,y+55},{x-2,y+47},{x-2,y+11});
    POLY(WHITE,LIGHT,{x+4,y},{x+54,y},{x+60,y+7},{x+60,y+43},
        {x+54,y+49},{x+4,y+49},{x-3,y+42},{x-3,y+7});
    box(x+3,y+8,33,29,RGB(130,83,35));box(x+6,y+11,27,23,AMBER);
    /* Original small waveform, decorative rather than a measured signal. */
    line(x+9,y+25,x+13,y+25,DARK);line(x+13,y+25,x+17,y+16,DARK);
    line(x+17,y+16,x+22,y+29,DARK);line(x+22,y+29,x+26,y+21,DARK);
    line(x+26,y+21,x+30,y+21,DARK);box(x+9,y+31,5+(f/5)*4,2,DARK);
    box(x+45,y+12,5,19,ORANGE);box(x+38,y+19,19,5,ORANGE);
    oval(x+47,y+21,2,2,WHITE,true);oval(x+47,y+39,4,4,ORANGE,true);
    box(x+8,y+40,24,3,STEEL);
}
static void galaxy_tag(int x,int y,int f)
{
    /* Elongated finder tag with a large keyring opening; model-neutral art. */
    oval(x,y-19,25,25,WHITE,true);box(x-25,y-19,51,51,WHITE);
    oval(x,y+30,25,25,WHITE,true);
    oval(x,y-19,20,20,LIGHT,true);box(x-20,y-19,41,50,LIGHT);
    oval(x,y+30,20,20,LIGHT,true);
    oval(x,y-18,13,15,DARK,true);oval(x,y-18,10,12,RGB(23,47,103),true);
    oval(x,y+26,13,13,BLUE,true);oval(x,y+26,8,8,CYAN,false);
    oval(x,y+26,3,3,WHITE,true);box(x-8,y+44,16,2,STEEL);
    if((f/5)%2==0){line(x-32,y+19,x-36,y+15,CYAN);line(x+32,y+19,x+36,y+15,CYAN);}
}
static void background(const snapshot_t *s)
{
    uint16_t a=accent(s->scene);int f=s->frame;
    box(ART_X,stripe_y,ART_W,STRIPE_ROWS,PANEL);
    bool distinct=s->scene==SENTINEL_WATCH_SCENE_AXON || s->scene==SENTINEL_WATCH_SCENE_FINDMY ||
        s->scene==SENTINEL_WATCH_SCENE_TRACKER || s->scene==SENTINEL_WATCH_SCENE_CAMERA ||
        s->scene==SENTINEL_WATCH_SCENE_SMART_GLASSES || s->scene==SENTINEL_WATCH_SCENE_BEACON ||
        s->scene==SENTINEL_WATCH_SCENE_PWNAGOTCHI || s->scene==SENTINEL_WATCH_SCENE_FLIPPER ||
        s->scene==SENTINEL_WATCH_SCENE_SAMSUNG || s->scene==SENTINEL_WATCH_SCENE_FLOCK;
    /* Each revised scene has its own stage, rather than the idle radar grid. */
    if(!distinct){
        for(int y=98;y<280;y+=18)line(20,y,299,y,RGB(17,30,45));
        for(int x=27;x<300;x+=24)line(x,96,x,274,RGB(17,30,45));
        oval(160,196,112,93,RGB(27,42,63),false);
        oval(160,196,100,82,RGB(22,36,55),false);
    }
    if(s->scene==SENTINEL_WATCH_SCENE_AXON){
        /* Red/blue overtones switch sides every 800 ms, within the art only. The
         * backdrop never blanks and the PSRAM compositor presents it whole. */
        bool red_phase=(f/2)%2==0;
        box(20,98,140,180,red_phase?RGB(87,23,43):RGB(36,22,38));
        box(160,98,140,180,red_phase?RGB(18,30,62):RGB(21,50,109));
        POLY(red_phase?RGB(124,35,57):RGB(61,25,42),RGB(61,25,42),{24,110},{91,110},{156,276},{20,276});
        POLY(red_phase?RGB(24,40,78):RGB(29,70,145),RGB(24,40,78),{228,110},{296,110},{300,276},{164,276});
        city(s->frame);
        box(27,101,266,10,DARK);box(31,103,117,6,red_phase?RED:RGB(91,37,51));
        box(171,103,118,6,red_phase?RGB(29,53,96):BLUE);
        box(29,249,56,20,ARMOR);box(237,249,54,20,ARMOR);
        box(34,243,22,5,red_phase?RED:EDGE);box(57,243,22,5,red_phase?EDGE:BLUE);
        box(244,243,17,5,red_phase?RED:EDGE);box(262,243,20,5,red_phase?EDGE:BLUE);
    }else if(s->scene==SENTINEL_WATCH_SCENE_FLOCK){
        box(20,98,280,180,RGB(65,48,35));
        oval(265,116,16,16,RGB(128,85,41),true);
        POLY(RGB(44,55,47),RGB(44,55,47),{20,192},{55,178},{84,194},{137,176},
            {181,185},{236,169},{300,190},{300,225},{20,225});
        POLY(RGB(32,36,41),STEEL,{134,198},{184,198},{299,278},{20,278});
        line(132,202,36,276,AMBER);line(187,202,285,276,AMBER);
        for(int yy=211;yy<278;yy+=22)box(157,yy,5,11,WHITE);
        /* Solar-powered roadside camera silhouettes, rather than an idle HUD. */
        box(50,116,5,32,STEEL);box(262,132,5,45,STEEL);
        POLY(RGB(23,49,73),LIGHT,{29,103},{81,103},{87,127},{24,127});
        for(int x=36;x<82;x+=13)line(x,105,x+2,125,BLUE);
        line(28,114,83,114,BLUE);
        POLY(RGB(23,49,73),LIGHT,{241,123},{285,123},{292,145},{236,145});
        for(int x=247;x<286;x+=12)line(x,125,x+2,143,BLUE);
        line(239,134,288,134,BLUE);
        camera(54,138,true,a);camera(264,172,false,a);
        box(30,215,55,18,AMBER);box(34,219,47,10,DARK);
        for(int i=0;i<3;++i){line(42+i*13,221,47+i*13,224,AMBER);line(47+i*13,224,42+i*13,227,AMBER);}
    }else if(s->scene==SENTINEL_WATCH_SCENE_WELLNESS){
        box(25,104,270,172,RGB(39,28,53));
        box(87,106,146,125,EDGE);box(92,111,136,115,RGB(17,26,47));
        line(160,111,160,226,STEEL);line(92,160,228,160,STEEL);
        oval(204,130,9,9,LIGHT,true);oval(208,126,8,8,RGB(17,26,47),true);
        POLY(RGB(85,47,86),PINK,{28,104},{77,104},{63,213},{29,232});
        POLY(RGB(85,47,86),PINK,{243,104},{293,104},{292,232},{258,213});
        box(28,242,59,36,RGB(60,43,71));box(30,240,54,5,PINK);
        box(237,237,45,40,RGB(53,38,64));box(254,220,4,17,AMBER);
        POLY(AMBER,AMBER,{248,211},{264,211},{272,222},{240,222});
        heart(40,151-f%4,2,PINK);heart(263,173-f%3,2,LILAC);
        heart(70,115-f%3,1,AMBER);heart(240,111-f%4,1,PINK);
    }else if(s->scene==SENTINEL_WATCH_SCENE_FINDMY){
        box(20,98,280,180,RGB(19,52,47));
        for(int i=0;i<4;++i)oval(160,191,65+i*24,55+i*22,i==f/5?RGB(63,135,100):RGB(32,84,67),false);
        box(27,115,54,136,LIGHT);box(31,119,46,128,DARK);
        box(43,122,22,4,STEEL);oval(54,169,19,19,MINT,false);oval(54,169,10,10,WHITE,false);
        oval(54,169,3,3,MINT,true);line(54,191,54,224,MINT);
        POLY(MINT,MINT,{45,215},{54,226},{63,215},{54,220});
        box(40,234,28,3,STEEL);
        locator_tag(261,147,31);
        for(int i=0;i<4;++i)oval(260,206+i*15,2,2,i==f/5?WHITE:MINT,true);
    }else if(s->scene==SENTINEL_WATCH_SCENE_TRACKER){
        box(20,98,280,180,RGB(48,35,30));
        box(27,145,56,122,RGB(55,84,92));box(30,149,50,114,RGB(76,110,114));
        box(42,128,26,6,LIGHT);box(42,128,5,17,LIGHT);box(63,128,5,17,LIGHT);
        for(int i=0;i<3;++i)box(36+i*17,155,3,96,RGB(43,70,79));
        oval(38,269,5,5,DARK,true);oval(73,269,5,5,DARK,true);
        box(61,173,17,28,ORANGE);box(65,179,9,3,WHITE);line(69,163,69,173,LIGHT);
        footprints(265,119,ORANGE);footprints(275,191,RGB(129,90,58));
        line(24,111,93,111,ORANGE);line(231,260,295,260,ORANGE);
    }else if(s->scene==SENTINEL_WATCH_SCENE_CAMERA){
        box(20,98,280,180,RGB(48,26,51));
        POLY(RGB(80,52,60),RGB(80,52,60),{42,113},{69,113},{139,278},{20,278});
        POLY(RGB(65,42,74),RGB(65,42,74),{255,115},{279,115},{300,278},{186,278});
        for(int side=0;side<2;++side){int x=side?286:24;box(x,99,10,177,DARK);
            for(int y=102;y<272;y+=13)box(x+2,y,5,6,LIGHT);}
        oval(51,137,15,15,STEEL,true);oval(78,137,15,15,STEEL,true);
        for(int i=0;i<2;++i){int x=51+27*i;oval(x,137,4,4,DARK,true);oval(x-7,132,3,3,DARK,true);oval(x+6,142,3,3,DARK,true);}
        box(36,151,53,34,DARK);box(42,159,26,17,ARMOR);
        POLY(LILAC,LIGHT,{89,158},{101,152},{101,184},{89,178});
        line(62,186,40,267,STEEL);line(62,186,83,267,STEEL);line(62,186,62,267,STEEL);
        box(245,117,31,21,AMBER);box(250,122,21,11,WHITE);line(261,139,261,194,STEEL);
    }else if(s->scene==SENTINEL_WATCH_SCENE_SMART_GLASSES){
        box(20,98,280,180,RGB(32,19,66));
        POLY(RGB(49,32,89),LILAC,{27,111},{79,126},{79,253},{27,270});
        POLY(RGB(23,58,78),CYAN,{240,126},{291,111},{291,270},{240,253});
        for(int i=0;i<7;++i){box(34,128+i*16,28-(i%3)*5,3,i==f/3?WHITE:LILAC);
            box(250,135+i*16,29-(i%3)*5,3,i==f/3?WHITE:CYAN);}
        line(33,111,160,97,PINK);line(160,97,289,111,PINK);
        oval(53,230,15,15,CYAN,false);line(42,230,64,230,CYAN);line(53,219,53,241,CYAN);
        POLY(DARK,CYAN,{249,211},{280,211},{280,239},{249,239});
        line(253,234,262,218,WHITE);line(262,218,275,229,WHITE);
    }else if(s->scene==SENTINEL_WATCH_SCENE_FLIPPER){
        box(20,98,280,180,RGB(60,35,26));
        /* Workshop pegboard, circuit paths, and a large orange LCD. */
        for(int y=105;y<275;y+=18)for(int x=28;x<299;x+=18)box(x,y,2,2,RGB(105,66,41));
        box(28,115,60,83,ORANGE);box(32,119,52,71,DARK);box(36,124,44,54,RGB(232,168,75));
        POLY(DARK,DARK,{40,151},{45,142},{58,137},{62,129},{66,139},{75,144},{69,151},
            {60,150},{52,157},{48,156},{51,149},{45,150});
        POLY(DARK,DARK,{45,150},{40,145},{40,155});px(67,143,WHITE);
        line(41,164,75,164,RGB(155,95,33));box(42,169,8+(f/5)*6,3,DARK);
        box(48,198,19,6,STEEL);box(36,204,44,5,STEEL);
        for(int i=0;i<3;++i){int yy=122+i*29;
            line(245,yy,273,yy,ORANGE);line(273,yy,284,yy+10,ORANGE);
            oval(245,yy,4,4,i==f/7?WHITE:AMBER,true);box(281,yy+8,6,6,STEEL);}
        box(243,210,42,8,DARK);box(252,202,5,8,LIGHT);box(271,202,5,8,LIGHT);
        line(31,238,63,238,ORANGE);line(63,238,63,224,ORANGE);oval(63,221,4,4,WHITE,true);
        box(26,261,268,10,RGB(145,87,43));line(26,261,294,261,AMBER);
    }else if(s->scene==SENTINEL_WATCH_SCENE_MESHTASTIC){
        POLY(RGB(22,43,47),EDGE,{20,235},{64,153},{104,224},{143,164},{195,235},{252,142},{300,235});
        POLY(STEEL,STEEL,{58,164},{64,153},{75,173},{64,169},{60,175});
        POLY(STEEL,STEEL,{245,157},{252,142},{263,162},{254,157},{250,165});
        for(int i=0;i<3;++i){int xx=33+17*i;line(xx,230,xx,259,STEEL);
            POLY(RGB(34,68,64),EDGE,{xx,209+i*7},{xx+12,240+i*3},{xx-12,240+i*3});}
        oval(270,124,9,9,CYAN,false);oval(270,124,16,16,EDGE,false);
        line(270,124,270,177,STEEL);line(270,148,256,177,STEEL);line(270,148,284,177,STEEL);
        box(253,181,34,23,DARK);box(259,187,21,3,CYAN);box(259,194,10,2,STEEL);
    }else if(s->scene==SENTINEL_WATCH_SCENE_SAMSUNG){
        box(20,98,280,180,RGB(18,30,78));
        POLY(RGB(24,48,105),RGB(24,48,105),{21,219},{106,98},{154,98},{42,278},{21,278});
        POLY(RGB(29,43,94),RGB(29,43,94),{182,98},{238,98},{299,197},{299,270});
        for(int i=0;i<8;++i){int x=31+(i*73)%260,y=106+(i*47)%151;
            star(x,y,i==f/3?WHITE:RGB(106,146,217));}
        line(242,115,277,135,BLUE);line(277,135,255,165,BLUE);line(255,165,286,182,BLUE);
        oval(242,115,3,3,WHITE,true);oval(277,135,3,3,CYAN,true);
        oval(255,165,3,3,WHITE,true);oval(286,182,3,3,CYAN,true);
        box(28,137,54,121,BLUE);box(32,141,46,113,DARK);
        box(45,144,20,3,LIGHT);box(37,154,36,71,RGB(27,55,105));
        line(40,157,68,208,STEEL);line(69,158,40,213,STEEL);
        oval(55,188,14,14,CYAN,false);oval(55,188,5,5,WHITE,true);
        POLY(CYAN,CYAN,{46,235},{55,226},{64,235},{57,232},{57,244},{53,244},{53,232});
        oval(266,112,21,8,RGB(90,126,192),false);
        oval(266,112,10,10,BLUE,true);line(250,116,281,108,CYAN);
    }else if(s->scene==SENTINEL_WATCH_SCENE_BEACON){
        box(20,98,280,180,RGB(21,53,64));
        for(int y=104;y<274;y+=28)line(23,y,296,y,RGB(39,78,87));
        for(int x=25;x<300;x+=40)line(x,98,x,277,RGB(39,78,87));
        box(31,222,52,50,RGB(124,76,42));box(36,229,42,34,ORANGE);
        for(int i=0;i<3;++i){box(42+i*11,237,6,12+(i+f/5)%3*4,DARK);}
        line(55,138,40,221,LIGHT);line(55,138,71,221,LIGHT);
        line(41,212,68,194,STEEL);line(44,191,65,176,STEEL);line(48,174,62,160,STEEL);
        for(int i=0;i<3;++i)oval(55,137,10+i*10,10+i*10,i==f/7?AMBER:RGB(83,128,134),false);
        oval(55,137,4,4,ORANGE,true);
        box(238,121,55,99,LIGHT);box(243,127,45,70,DARK);
        for(int i=0;i<4;++i)line(247,136+i*16,284,136+i*16,EDGE);
        for(int i=0;i<3;++i)line(249+i*16,130,249+i*16,194,EDGE);
        line(246,169,253,169,CYAN);line(253,169,258,145,CYAN);line(258,145,265,183,CYAN);
        line(265,183,272,153,CYAN);line(272,153,278,169,CYAN);line(278,169,285,169,CYAN);
        oval(249+(f%7)*5,169,2,2,WHITE,true);oval(251,208,4,4,ORANGE,true);oval(279,208,4,4,DARK,true);
        box(232,224,65,8,STEEL);box(238,232,6,43,ARMOR);box(285,232,6,43,ARMOR);
    }else if(s->scene==SENTINEL_WATCH_SCENE_PWNAGOTCHI){
        box(20,98,280,180,RGB(42,23,63));
        for(int y=104;y<278;y+=24)for(int x=25;x<299;x+=27)
            if((x/27+y/24)%3==0)box(x,y,12,12,RGB(63,36,84));
        /* Oversized arcade cabinet and original pet make the silhouette read
         * as a digital-companion scene, even without reading the caption. */
        POLY(RGB(106,67,132),LILAC,{28,109},{79,109},{79,194},{88,211},{81,274},{27,274},{23,212},{30,194});
        box(33,116,40,13,AMBER);box(37,120,31,4,DARK);
        box(33,136,40,52,DARK);box(38,141,30,42,RGB(104,139,112));
        box(43,149,6,8,DARK);box(58,149,6,8,DARK);box(47,166,14,3,DARK);
        line(36,202,74,202,LIGHT);line(42,204,42,214,WHITE);oval(42,202,4,4,PINK,true);
        oval(62,209,3,3,CYAN,true);oval(73,212,3,3,AMBER,true);
        box(35,228,39,32,DARK);box(45,237,20,3,LILAC);
        companion(267,239+(f>=5 && f<13),f);
        for(int i=0;i<3;++i){int y=118+i*25;box(247,y,8,8,AMBER);box(270,y+8,8,8,LILAC);}
        heart(260,184-f%3,2,PINK);star(237,109,CYAN);
    }else if(s->scene==SENTINEL_WATCH_SCENE_DRONE){
        cloud(41,121);cloud(252,110);cloud(250,224);
        quadcopter(53,176+(f>=5 && f<13),f,CYAN);
        quadcopter(264,152-(f>=5 && f<13),f,AMBER);
        line(30,260,78,260,STEEL);line(38,247,69,247,STEEL);line(47,235,60,235,STEEL);
        line(34,265,58,225,EDGE);line(78,265,58,225,EDGE);
        box(245,255,44,15,DARK);box(252,260,29,3,AMBER);
    }else if(s->scene==SENTINEL_WATCH_SCENE_BLE_ACTIVITY){
        for(int i=0;i<5;++i){int yy=113+i*27;
            line(29,yy,41,yy+8,LILAC);line(41,yy+8,34,yy+15,LILAC);line(34,yy+15,55,yy+19,STEEL);
            line(289,yy+4,277,yy+11,CYAN);line(277,yy+11,284,yy+18,CYAN);line(284,yy+18,263,yy+22,STEEL);}
        oval(160,199,114,96,RGB(49,57,84),false);oval(160,199,109,92,EDGE,false);
        for(int i=0;i<5;++i){int h=7+(i*11+f/5*3)%22;box(33+i*8,274-h,4,h,STEEL);}
    }else{
        /* Three arcs echo the three independent senses of the guardian. */
        for(int i=0;i<3;++i){int x=48+112*i;
            oval(x,150,14,14,i==f/7?CYAN:EDGE,false);oval(x,150,5,5,STEEL,true);}
        star(51,223,CYAN);star(265,220,LILAC);
        line(35,260,70,260,EDGE);line(250,260,285,260,EDGE);
    }
    /* Floor and status-neutral decorative stage. */
    box(20,278,280,35,RGB(10,19,31));
    if(s->scene==SENTINEL_WATCH_SCENE_FLOCK){
        box(20,278,280,35,RGB(33,36,40));
        box(20,304,280,9,AMBER);
        for(int x=20;x<300;x+=22)POLY(DARK,DARK,{x,304},{x+10,304},{x+19,312},{x+9,312});
        for(int x=29;x<299;x+=49)box(x,285,29,4,WHITE);
    }else if(s->scene==SENTINEL_WATCH_SCENE_FLIPPER){
        box(20,278,280,35,RGB(44,29,25));
        line(23,282,296,282,ORANGE);line(23,310,296,310,ORANGE);
        for(int x=30;x<297;x+=37){box(x,287,25,18,RGB(86,53,35));box(x+8,290,9,2,AMBER);}
    }else if(s->scene==SENTINEL_WATCH_SCENE_SAMSUNG){
        box(20,278,280,35,RGB(22,38,83));
        POLY(RGB(45,70,119),BLUE,{34,288},{74,279},{246,279},{287,288},{273,306},{47,306});
        line(39,289,280,289,CYAN);line(56,303,266,303,BLUE);
        for(int x=53;x<280;x+=42)box(x,294,19,3,RGB(104,154,230));
    }else if(s->scene==SENTINEL_WATCH_SCENE_FINDMY){
        oval(160,300,110,12,RGB(72,112,103),true);oval(160,297,104,10,RGB(168,196,185),true);
    }else if(s->scene==SENTINEL_WATCH_SCENE_CAMERA || s->scene==SENTINEL_WATCH_SCENE_PWNAGOTCHI){
        for(int y=279;y<313;y+=12)for(int x=22;x<298;x+=23)
            if((x/23+y/12)%2)box(x,y,22,11,s->scene==SENTINEL_WATCH_SCENE_CAMERA?RGB(67,55,69):RGB(70,42,85));
    }else if(s->scene==SENTINEL_WATCH_SCENE_SMART_GLASSES){
        for(int x=20;x<=300;x+=35)line(160,254,x,312,RGB(67,51,110));
        line(20,287,300,287,RGB(67,51,110));line(20,304,300,304,LILAC);
    }else if(s->scene==SENTINEL_WATCH_SCENE_BEACON || s->scene==SENTINEL_WATCH_SCENE_TRACKER){
        line(20,284,300,284,STEEL);line(20,308,300,308,ORANGE);
        for(int x=32;x<290;x+=32)line(x,279,x-9,312,EDGE);
    }else{
        oval(160,300,108,12,EDGE,false);oval(160,300,89,8,a,false);
    }
    line(31,95,55,95,a);line(23,103,23,125,a);line(265,95,289,95,a);
    line(297,103,297,125,a);line(23,279,23,298,STEEL);line(297,279,297,298,STEEL);
    box(33,307,24,2,STEEL);box(263,307,24,2,STEEL);
}

/* Symmetric front-facing canine faces, each with ears, muzzle, armored cheek
 * plates, nose, lower jaw, and luminous eyes. The two side heads look outward. */
static void head(int cx,int y,int look,uint16_t eye,bool blink,bool cap,bool glasses)
{
    POLY(ARMOR,STEEL,{cx-26,y+23},{cx-27,y},{cx-10,y+13},{cx+9,y+13},
        {cx+26,y},{cx+27,y+24},{cx+24,y+49},{cx+11,y+66},{cx-10,y+66},{cx-25,y+48});
    POLY(DARK,DARK,{cx-23,y+6},{cx-21,y+24},{cx-11,y+18});
    POLY(DARK,DARK,{cx+22,y+6},{cx+12,y+18},{cx+22,y+25});
    line(cx-21,y+12,cx-18,y+19,eye);line(cx+21,y+12,cx+18,y+19,eye);
    POLY(STEEL,LIGHT,{cx-20,y+25},{cx-6,y+19},{cx+6,y+19},{cx+21,y+25},
        {cx+13,y+43},{cx,y+47},{cx-13,y+43});
    POLY(ARMOR,ARMOR,{cx,y+21},{cx+7,y+25},{cx+5,y+43},{cx-5,y+43},{cx-7,y+25});
    POLY(DARK,DARK,{cx-22,y+30},{cx-5,y+33},{cx-9,y+40},{cx-20,y+37});
    POLY(DARK,DARK,{cx+5,y+33},{cx+22,y+30},{cx+20,y+37},{cx+9,y+40});
    if(blink){line(cx-20,y+35,cx-9,y+37,eye);line(cx+9,y+37,cx+20,y+35,eye);}
    else {box(cx-18+look,y+33,9,3,eye);box(cx+10+look,y+33,9,3,eye);
        px(cx-13+look,y+33,WHITE);px(cx+15+look,y+33,WHITE);}
    POLY(LIGHT,STEEL,{cx-15,y+44},{cx+14,y+44},{cx+19,y+54},{cx+8,y+62},
        {cx-8,y+62},{cx-19,y+54});
    POLY(DARK,DARK,{cx-8+look,y+45},{cx+8+look,y+45},{cx+6+look,y+51},{cx+look,y+54},{cx-6+look,y+51});
    line(cx+look,y+54,cx+look,y+58,ARMOR);line(cx-11,y+59,cx+11,y+59,ARMOR);
    box(cx-16,y+55,3,3,WHITE);box(cx+13,y+55,3,3,WHITE);
    line(cx-23,y+44,cx-20,y+49,eye);line(cx+23,y+44,cx+20,y+49,eye);
    box(cx-4,y+63,8,2,DARK);
    if(cap){
        POLY(RGB(26,44,77),STEEL,{cx-26,y+19},{cx-29,y+9},{cx-16,y+3},{cx+17,y+3},{cx+29,y+9},{cx+26,y+19});
        box(cx-25,y+18,50,5,DARK);line(cx-21,y+18,cx+21,y+18,AMBER);
        POLY(AMBER,AMBER,{cx,y+7},{cx+5,y+9},{cx+4,y+14},{cx,y+17},{cx-4,y+14},{cx-5,y+9});
    }
    if(glasses){
        POLY(LILAC,WHITE,{cx-27,y+26},{cx-4,y+28},{cx-2,y+43},{cx-23,y+43});
        POLY(LILAC,WHITE,{cx+27,y+26},{cx+4,y+28},{cx+2,y+43},{cx+23,y+43});
        box(cx-23,y+29,17,11,RGB(24,72,92));box(cx+6,y+29,17,11,RGB(24,72,92));
        line(cx-4,y+30,cx+4,y+30,WHITE);line(cx-22,y+38,cx-13,y+29,CYAN);
        line(cx+7,y+38,cx+16,y+29,CYAN);box(cx+23,y+28,5,6,DARK);oval(cx+25,y+30,1,1,PINK,true);
    }
}

static void guardian(const snapshot_t *s)
{
    uint16_t a=accent(s->scene);int f=s->frame;
    int breathe=(f>=5 && f<13)?1:0;bool blink=f==18;
    int look=(f/5==1)?1:(f/5==3)?-1:0;
    bool coat=s->scene==SENTINEL_WATCH_SCENE_FINDMY;
    bool suit=s->scene==SENTINEL_WATCH_SCENE_WELLNESS;
    bool police=s->scene==SENTINEL_WATCH_SCENE_AXON;
    bool hoodie=s->scene==SENTINEL_WATCH_SCENE_FLIPPER;
    bool explorer=s->scene==SENTINEL_WATCH_SCENE_MESHTASTIC;
    bool finder=s->scene==SENTINEL_WATCH_SCENE_SAMSUNG;
    bool roadwatch=s->scene==SENTINEL_WATCH_SCENE_FLOCK;
    bool scientist=s->scene==SENTINEL_WATCH_SCENE_BEACON;
    bool aviator=s->scene==SENTINEL_WATCH_SCENE_DRONE;
    bool tracker=s->scene==SENTINEL_WATCH_SCENE_TRACKER;
    bool director=s->scene==SENTINEL_WATCH_SCENE_CAMERA;
    bool ar=s->scene==SENTINEL_WATCH_SCENE_SMART_GLASSES;
    bool petkeeper=s->scene==SENTINEL_WATCH_SCENE_PWNAGOTCHI;
    if(explorer){
        /* The pack and long aerial stay behind all three heads and the paws. */
        POLY(RGB(56,83,69),STEEL,{99,192},{222,192},{233,214},{226,259},{93,259},{87,214});
        box(86,214,20,31,RGB(73,96,76));box(214,214,22,31,RGB(73,96,76));
        line(234,219,246,149,STEEL);line(237,212,248,150,CYAN);
    }else if(finder){
        /* Blue life-support pack and shoulder tanks frame the three heads. */
        box(90,191,28,63,RGB(36,72,144));box(202,191,28,63,RGB(36,72,144));
        box(93,198,8,42,LIGHT);box(219,198,8,42,LIGHT);
        line(94,192,94,179,CYAN);line(226,192,226,179,CYAN);
    }
    /* Tail is a separate articulated limb; the full body has four grounded
       paws. Its cyan spine sweeps behind the right hindquarter. */
    POLY(ARMOR,STEEL,{192,250},{216,255},{236,245},{242,223},{235,211},{246,202},
        {253,219},{247,250},{221,267},{194,265});
    line(216,260,241,246,a);line(241,246,247,220,a);
    line(221,254,224,263,EDGE);line(237,244,245,249,EDGE);
    POLY(LIGHT,a,{239,207},{246,196-(f%3==0)},{254,207},{248,216});
    /* Hind legs and their broad canine paws. */
    POLY(ARMOR,STEEL,{112,234},{99,253},{106,280},{96,291},{97,299},{132,299},{136,288},{127,265},{136,241});
    POLY(ARMOR,STEEL,{208,234},{221,253},{214,280},{224,291},{223,299},{188,299},{184,288},{193,265},{184,241});
    box(100,291,29,8,STEEL);box(191,291,29,8,STEEL);
    box(101,296,6,3,LIGHT);box(112,296,6,3,LIGHT);box(202,296,6,3,LIGHT);box(213,296,6,3,LIGHT);
    oval(118,253,10,10,DARK,true);oval(118,253,5,5,a,false);
    oval(202,253,10,10,DARK,true);oval(202,253,5,5,a,false);
    /* Chest and shoulders shared by all three necks. */
    POLY(ARMOR,STEEL,{112,182+breathe},{143,176+breathe},{178,176+breathe},{209,182+breathe},
        {220,205+breathe},{198,227+breathe},{188,263},{132,263},{122,227+breathe},{100,205+breathe});
    uint16_t shoulders=coat?WHITE:ar?RGB(129,109,185):scientist?LIGHT:petkeeper?RGB(196,139,65):
        hoodie?ORANGE:finder?BLUE:roadwatch?RGB(211,224,82):STEEL;
    POLY(shoulders,LIGHT,{113,189+breathe},{143,181+breathe},{178,181+breathe},{207,189+breathe},
        {198,205+breathe},{181,218+breathe},{139,218+breathe},{121,205+breathe});
    oval(108,207+breathe,10,10,DARK,true);oval(108,207+breathe,6,6,a,false);
    oval(212,207+breathe,10,10,DARK,true);oval(212,207+breathe,6,6,a,false);
    uint16_t garment=coat?WHITE:suit?LIGHT:police?RGB(26,50,85):
        hoodie?ORANGE:explorer?RGB(73,96,76):finder?RGB(37,93,198):roadwatch?RGB(211,224,82):
        scientist?WHITE:aviator?RGB(113,80,55):tracker?ORANGE:director?RGB(106,48,71):
        ar?RGB(88,64,139):petkeeper?RGB(196,139,65):ARMOR;
    POLY(garment,STEEL,{129,203+breathe},{160,211+breathe},{191,203+breathe},
        {194,244},{184,270},{136,270},{126,244});
    if(coat){
        POLY(WHITE,MINT,{127,203},{144,215},{137,270},{113,274},{119,229});
        POLY(WHITE,MINT,{193,203},{176,215},{183,270},{207,274},{201,229});
        line(160,213,160,271,STEEL);box(137,253,46,7,RGB(52,110,85));
        oval(160,234+breathe,18,18,RGB(78,145,112),true);oval(160,234+breathe,14,14,MINT,false);
        oval(160,234+breathe,8,8,WHITE,false);oval(160,234+breathe,3,3,WHITE,true);
    }else if(suit){
        POLY(WHITE,WHITE,{142,210},{178,210},{165,249},{155,249});
        POLY(LILAC,LILAC,{147,216},{158,220},{147,224});
        POLY(LILAC,LILAC,{173,216},{162,220},{173,224});
        box(158,219,4,4,PINK);box(159,230,2,2,ARMOR);box(159,240,2,2,ARMOR);
        heart(180,223,1,PINK);
    }else if(police){
        box(134,226,16,12,RGB(19,36,62));line(134,226,150,226,LIGHT);
        POLY(AMBER,AMBER,{179,221},{186,224},{184,234},{179,238},{174,234},{172,224});
        box(130,247,60,7,DARK);box(155,247,10,7,AMBER);
        box(141,239,7,3,CYAN);line(160,216,160,243,STEEL);
    }else if(hoodie){
        POLY(ORANGE,AMBER,{124,202},{143,218},{138,273},{110,265},{117,224});
        POLY(ORANGE,AMBER,{196,202},{177,218},{182,273},{210,265},{203,224});
        box(142,216,36,48,RGB(80,46,31));line(160,217,160,266,WHITE);
        line(138,207,146,226,WHITE);line(182,207,174,226,WHITE);
        box(142,224,5,6,WHITE);box(173,224,5,6,WHITE);
        box(138,253,44,13,RGB(164,84,34));line(143,257,177,257,AMBER);
    }else if(explorer){
        box(134,218,15,13,RGB(49,72,56));box(172,218,15,13,RGB(49,72,56));
        line(135,214,145,264,LIGHT);line(185,214,175,264,LIGHT);
        box(146,249,28,5,DARK);box(157,248,7,7,AMBER);
    }else if(finder){
        POLY(BLUE,WHITE,{123,203},{145,215},{140,271},{110,265},{115,224});
        POLY(BLUE,WHITE,{197,203},{175,215},{180,271},{210,265},{205,224});
        box(141,215,38,31,RGB(19,43,99));box(145,219,30,23,WHITE);
        POLY(BLUE,BLUE,{160,221},{170,237},{160,233},{150,237});
        box(137,253,47,8,WHITE);box(154,251,13,12,CYAN);
        oval(138,227,3,3,CYAN,true);oval(182,227,3,3,CYAN,true);
    }else if(roadwatch){
        POLY(RGB(211,224,82),AMBER,{124,201},{146,216},{137,271},{109,263},{115,222});
        POLY(RGB(211,224,82),AMBER,{196,201},{174,216},{183,271},{211,263},{205,222});
        box(146,216,28,49,RGB(48,59,53));
        POLY(WHITE,LIGHT,{129,207},{136,211},{133,251},{125,251});
        POLY(WHITE,LIGHT,{191,207},{184,211},{187,251},{195,251});
        box(118,242,84,9,WHITE);box(118,245,84,3,STEEL);
        box(149,222,22,13,DARK);box(154,226,12,3,AMBER);
        line(160,234,160,263,STEEL);
    }else if(scientist){
        POLY(WHITE,STEEL,{123,201},{147,218},{141,275},{108,279},{118,236});
        POLY(WHITE,STEEL,{197,201},{173,218},{179,275},{212,279},{202,236});
        box(145,217,30,43,RGB(21,63,75));line(151,220,160,231,ORANGE);line(169,220,160,231,ORANGE);
        box(179,231,16,14,STEEL);box(181,226,3,13,CYAN);box(187,224,3,15,ORANGE);
        box(146,257,28,6,ORANGE);
    }else if(aviator){
        POLY(RGB(199,174,135),LIGHT,{130,205},{144,217},{151,213},{140,199});
        POLY(RGB(199,174,135),LIGHT,{190,205},{176,217},{169,213},{180,199});
        line(160,217,160,263,AMBER);box(135,232,13,9,RGB(81,57,42));box(172,232,13,9,RGB(81,57,42));
        line(137,267,183,267,RGB(199,174,135));
    }else if(tracker){
        POLY(ORANGE,AMBER,{126,204},{145,219},{137,267},{114,265},{120,229});
        POLY(ORANGE,AMBER,{194,204},{175,219},{183,267},{206,265},{200,229});
        box(130,234,60,7,LIGHT);box(154,211,12,55,RGB(122,69,35));
        line(127,205,194,255,DARK);line(128,209,192,257,DARK);
    }else if(director){
        box(134,210,52,7,RGB(166,77,84));box(171,216,11,30,RGB(166,77,84));
        line(130,208,150,235,DARK);line(190,208,175,235,DARK);
        box(137,253,45,11,RGB(70,32,53));
    }else if(ar){
        POLY(LILAC,CYAN,{123,204},{145,217},{134,270},{113,255});
        POLY(LILAC,CYAN,{197,204},{175,217},{186,270},{207,255});
        box(144,221,32,31,RGB(20,50,72));line(147,223,173,249,CYAN);
        line(173,223,147,249,PINK);box(150,254,21,6,CYAN);
    }else if(petkeeper){
        POLY(RGB(196,139,65),AMBER,{126,204},{143,214},{137,269},{112,263},{116,224});
        POLY(RGB(196,139,65),AMBER,{194,204},{177,214},{183,269},{208,263},{204,224});
        box(134,257,52,10,RGB(88,45,96));line(138,260,182,260,LILAC);
    }else{
        POLY(DARK,STEEL,{143,219+breathe},{177,219+breathe},{184,235+breathe},{170,249+breathe},
            {150,249+breathe},{136,235+breathe});
        POLY(a,a,{160,225+breathe},{170,233+breathe},{160,242+breathe},{150,233+breathe});
        POLY(WHITE,WHITE,{160,230+breathe},{164,233+breathe},{160,237+breathe},{156,233+breathe});
        line(141,253,179,253,STEEL);line(145,258,175,258,EDGE);
    }
    /* Front legs stay in front of the torso and show piston joints. */
    for(int side=0;side<2;++side){int x=side?181:124;
        POLY(STEEL,LIGHT,{x,230},{x+13,232},{x+11,254},{x+9,279},{x+16,294},{x+14,301},
            {x-8,301},{x-10,295},{x-2,279},{x-2,253});
        box(x+1,245,6,25,ARMOR);box(x+3,248,2,17,a);
        oval(x+3,276,5,5,DARK,true);oval(x+3,276,2,2,STEEL,true);
        box(x-8,294,22,7,LIGHT);box(x-5,297,2,4,DARK);box(x+3,297,2,4,DARK);box(x+10,297,2,4,DARK);
    }
    /* Outer necks and heads form a broad three-headed silhouette. */
    POLY(ARMOR,STEEL,{93,181+breathe},{122,178+breathe},{142,199+breathe},{125,217+breathe},{101,199+breathe});
    POLY(ARMOR,STEEL,{227,181+breathe},{198,178+breathe},{178,199+breathe},{195,217+breathe},{219,199+breathe});
    if(hoodie){
        POLY(ORANGE,AMBER,{124,173+breathe},{119,116+breathe},{136,96+breathe},
            {184,96+breathe},{201,116+breathe},{196,173+breathe},{183,189+breathe},{137,189+breathe});
        POLY(RGB(91,51,32),RGB(91,51,32),{130,166+breathe},{128,118+breathe},{140,104+breathe},
            {180,104+breathe},{192,118+breathe},{190,166+breathe});
    }else if(finder){
        /* Clear helmet rims preserve the canine faces and add a broad space silhouette. */
        oval(105,164+breathe,35,44,RGB(51,85,150),true);
        oval(215,164+breathe,35,44,RGB(51,85,150),true);
        oval(160,142+breathe,37,48,RGB(51,85,150),true);
    }
    head(105,128+breathe,-1,CYAN,blink,false,ar);
    head(215,128+breathe,1,LILAC,blink,false,ar);
    head(160,107+breathe,look,WHITE,blink,police,ar);
    /* The central collar carries our own three-sense insignia. */
    box(136,173+breathe,48,9,DARK);box(142,176+breathe,8,3,CYAN);
    box(156,176+breathe,8,3,WHITE);box(170,176+breathe,8,3,LILAC);
    if(coat){
        box(137,174+breathe,46,7,MINT);box(155,175+breathe,10,5,WHITE);
        locator_tag(259,260,30);line(215,225,236,243,WHITE);line(216,229,235,247,STEEL);
        oval(259,260,38,38,RGB(53,137,95),false);
    }else if(tracker){
        POLY(ORANGE,LIGHT,{133,126},{136,113},{149,107},{177,110},{187,126});
        box(132,124,62,6,RGB(171,94,41));box(141,119,35,4,WHITE);
        box(119,240,28,37,RGB(90,60,41));box(122,244,22,14,RGB(136,94,57));box(131,253,6,8,AMBER);
        key_tags(263,212,f);line(216,217,246,215,AMBER);
    }else if(director){
        POLY(RGB(79,32,56),LILAC,{130,124},{130,113},{142,104},{170,102},{190,111},{186,124});
        box(137,122,45,5,DARK);line(166,103,169,98,LILAC);
        box(132,227,57,31,DARK);box(138,223,17,5,LIGHT);box(137,234,11,5,ORANGE);
        oval(169,242,17,17,STEEL,true);oval(169,242,13,13,LILAC,true);
        oval(169,242,9,9,DARK,true);oval(165,238,4,4,WHITE,true);
        clapper(231,233,f);
    }else if(roadwatch){
        /* Roadside safety costume and illustrative car, never observed plate data. */
        POLY(AMBER,WHITE,{131,125},{132,113},{143,104},{177,104},{188,113},{189,125});
        box(126,124,68,7,ORANGE);box(156,104,8,21,ORANGE);box(141,118,7,4,WHITE);
        POLY(ORANGE,AMBER,{42,250},{52,250},{66,293},{28,293});
        POLY(WHITE,WHITE,{38,264},{57,264},{60,273},{35,273});
        box(25,293,44,7,DARK);box(29,294,36,3,ORANGE);
        POLY(RGB(91,116,135),LIGHT,{237,252},{244,235},{277,235},{286,252},{289,279},{234,279});
        POLY(DARK,STEEL,{246,239},{274,239},{280,252},{241,252});
        box(237,257,11,6,WHITE);box(276,257,10,6,WHITE);
        box(251,267,21,8,WHITE);box(254,270,4,2,DARK);box(261,270,7,2,DARK);
        box(236,280,9,8,DARK);box(278,280,9,8,DARK);
        box(249,255,24,3,((f/5)%2)?STEEL:LIGHT);
    }else if(hoodie){
        handheld(235,226+breathe,f);
        line(238,267,220,274,ORANGE);line(220,274,211,251,ORANGE);
        box(139,233,42,18,WHITE);box(144,237,20,10,AMBER);
        box(170,237,3,10,ORANGE);box(166,241,11,3,ORANGE);
        box(147,240,13,2,DARK);box(147,244,8,1,DARK);
        line(137,242,100,227,STEEL);line(100,227,83,246,STEEL);
        POLY(ORANGE,AMBER,{79,241},{87,247},{75,265},{68,260});
        line(80,240,90,225,WHITE);oval(92,222,3,3,AMBER,false);
        line(127,118+breathe,138,104+breathe,WHITE);line(193,118+breathe,182,104+breathe,WHITE);
    }else if(explorer){
        POLY(RGB(96,110,78),LIGHT,{133,121},{139,104},{178,104},{186,121});
        box(125,121,70,5,RGB(96,110,78));box(140,114,38,4,DARK);
        oval(160,235,11,11,DARK,true);oval(160,235,9,9,AMBER,false);
        POLY(CYAN,CYAN,{160,228},{165,239},{160,236},{155,242});
        box(211,202,13,22,DARK);box(214,207,7,3,CYAN);box(214,214,7,2,STEEL);
    }else if(finder){
        oval(105,164+breathe,35,44,LIGHT,false);oval(215,164+breathe,35,44,LIGHT,false);
        oval(160,142+breathe,37,48,WHITE,false);
        line(136,114+breathe,148,102+breathe,CYAN);line(139,118+breathe,155,102+breathe,CYAN);
        line(78,143+breathe,87,132+breathe,CYAN);line(237,139+breathe,243,148+breathe,CYAN);
        box(139,177+breathe,43,8,BLUE);box(149,180+breathe,23,3,CYAN);
        line(216,220,226,229,WHITE);line(226,229,234,215,WHITE);line(234,215,259,211,WHITE);
        galaxy_tag(265,237+breathe,f);
    }else if(scientist){
        box(137,128+breathe,20,11,CYAN);box(163,128+breathe,20,11,CYAN);
        box(140,130+breathe,14,7,DARK);box(166,130+breathe,14,7,DARK);
        line(157,131+breathe,163,131+breathe,LIGHT);line(141,131+breathe,148,131+breathe,WHITE);
        line(167,131+breathe,174,131+breathe,WHITE);
        box(137,232,48,26,RGB(124,76,42));box(142,237,38,16,ORANGE);
        oval(152,245,5,5,DARK,false);box(164,241,4,8,DARK);box(171,238,4,11,DARK);
        line(143,232,138,215,LIGHT);line(179,232,185,216,LIGHT);
        oval(138,214,3,3,CYAN,true);oval(185,215,3,3,CYAN,true);
    }else if(petkeeper){
        oval(160,137+breathe,34,33,LILAC,false);oval(160,137+breathe,32,31,LILAC,false);
        box(126,130+breathe,10,27,RGB(83,45,112));box(184,130+breathe,10,27,RGB(83,45,112));
        box(128,135+breathe,6,16,AMBER);box(186,135+breathe,6,16,AMBER);
        line(190,156,197,186,LILAC);line(197,186,180,219,LILAC);
        box(133,218,54,40,LILAC);box(137,222,46,31,RGB(208,220,187));
        box(144,229,10,blink?2:9,DARK);box(166,229,10,blink?2:9,DARK);
        box(152,244,17,3,DARK);box(146,240,5,3,DARK);box(170,240,5,3,DARK);
        box(155,256,10,3,AMBER);
    }else if(ar){
        POLY(RGB(22,72,88),CYAN,{232,221},{291,208},{291,267},{232,280});
        line(239,231,282,221,LIGHT);line(239,240,271,233,LILAC);line(239,249,281,239,CYAN);
        oval(261,260,9,9,CYAN,false);line(237,217,222,202,LILAC);
    }else if(aviator){
        POLY(RGB(113,80,55),LIGHT,{132,126},{134,111},{143,103},{177,103},{186,111},{188,126});
        box(132,124,8,18,RGB(113,80,55));box(180,124,8,18,RGB(113,80,55));
        oval(148,120,9,6,AMBER,true);oval(172,120,9,6,AMBER,true);
        oval(148,120,6,4,DARK,true);oval(172,120,6,4,DARK,true);line(157,120,163,120,STEEL);
        line(144,118,150,118,CYAN);line(168,118,174,118,CYAN);
        box(137,201,46,7,CYAN);POLY(CYAN,CYAN,{180,206},{213,212},{228,207},{218,219},{181,213});
        line(186,210,213,216,LIGHT);
    }else if(s->scene==SENTINEL_WATCH_SCENE_BLE_ACTIVITY){
        POLY(RGB(28,48,68),LILAC,{217,214},{242,220},{267,214},{265,252},{242,279},{219,252});
        POLY(ARMOR,STEEL,{224,224},{242,230},{260,224},{257,249},{242,268},{227,249});
        POLY(CYAN,CYAN,{242,235},{251,244},{242,258},{233,244});
        line(242,239,242,251,WHITE);line(238,244,246,244,WHITE);
        POLY(ARMOR,LILAC,{144,125+breathe},{160,115+breathe},{176,125+breathe},{160,133+breathe});
        box(157,123+breathe,6,3,CYAN);
    }
}

#include "recon_pixel_art.h"
static esp_err_t draw_art(const snapshot_t *s)
{
    for(stripe_y=ART_Y;stripe_y<ART_Y+ART_H;stripe_y+=STRIPE_ROWS){
        if (s->demo) { background(s); guardian(s); }
        else recon_pixel_guardian(s);
        TRY(cerberus_display_draw_rgb565_bitmap(ART_X,(uint16_t)stripe_y,ART_W,STRIPE_ROWS,stripe));
    }
    return ESP_OK;
}

static void copy_text(char *dst,size_t size,const char *src)
{
    size_t i=0;if(src)for(;i+1<size && src[i];++i){unsigned char c=(unsigned char)src[i];dst[i]=c>=32 && c<=126?(char)c:'?';}
    dst[i]='\0';
}
static const char *scene_title(sentinel_watch_scene_t scene)
{
    static const char *const names[]={"ON WATCH","AXON BROADCAST HINT","FLOCK BROADCAST HINT","WELLNESS DEVICE HINT",
        "AIRTAG / FIND MY HINT","TILE/CHIPOLO/FIND HUB","GOPRO CAMERA HINT","SMART GLASSES DEMO",
        "FLIPPER BROADCAST HINT","MESHTASTIC BLE HINT","SAMSUNG TRACKER HINT","BLUETOOTH BEACON HINT",
        "PWNAGOTCHI PEER HINT","DRONE REMOTE ID HINT","UNUSUAL BLE ACTIVITY"};
    _Static_assert(sizeof(names)/sizeof(names[0])==SENTINEL_WATCH_SCENE_COUNT,"every scene needs a title");
    return names[scene];
}
static bool valid(const sentinel_watch_view_t *v)
{
    return v && v->scene>=SENTINEL_WATCH_SCENE_GUARDIAN && v->scene<SENTINEL_WATCH_SCENE_COUNT &&
        v->confidence>=SENTINEL_WATCH_CONFIDENCE_NONE && v->confidence<=SENTINEL_WATCH_CONFIDENCE_HIGH;
}
static snapshot_t snapshot(const sentinel_watch_view_t *v,uint32_t now)
{
    snapshot_t s={0};s.scene=v->scene;s.confidence=v->confidence;s.demo=v->demo;
    s.paused=v->paused;s.online=v->radio_online;s.details=v->has_details;
    s.observations=v->observation_count;s.matches=v->match_count;
    s.frame=(v->paused || !v->radio_online) && !v->demo?0:(uint8_t)((now/400U)%20U);
    copy_text(s.phase,sizeof(s.phase),v->demo?"SCENE GALLERY / NO LIVE DETECTION":v->phase);
    if(!s.phase[0])copy_text(s.phase,sizeof(s.phase),v->paused?"WATCH PAUSED":v->radio_online?"READY TO WATCH":"RADIO OFFLINE");
    copy_text(s.title,sizeof(s.title),v->title && v->title[0]?v->title:scene_title(v->scene));
    copy_text(s.evidence,sizeof(s.evidence),v->evidence);
    copy_text(s.detail,sizeof(s.detail),v->detail);
    if(!s.evidence[0])copy_text(s.evidence,sizeof(s.evidence),v->demo?"Costume preview; no device was detected.":"Broadcast clues are not proof of identity.");
    if(!s.detail[0])copy_text(s.detail,sizeof(s.detail),v->demo?"NEXT explores another guardian scene.":"Only observed radio evidence appears here.");
    return s;
}
static esp_err_t text(int x,int y,const char *s,uint16_t color,uint16_t bg,uint8_t scale)
{ return cerberus_display_draw_text((uint16_t)x,(uint16_t)y,s,color,bg,scale); }
static esp_err_t centered(int y,const char *s,uint16_t color,uint16_t bg,uint8_t scale)
{return text((320-(int)strlen(s)*6*scale)/2,y,s,color,bg,scale);}
static esp_err_t draw_header(const snapshot_t *s)
{
    char linebuf[52];uint16_t a=accent(s->scene);
    TRY(cerberus_display_fill_rect(0,0,320,88,BG));
    TRY(text(12,12,"CERBERUS",WHITE,BG,2));TRY(text(13,32,"FIELD KIT / RECON",MUTED,BG,1));
    const char *badge=s->demo?"DEMO":s->paused?"PAUSED":s->online?"RECON":"OFFLINE";
    TRY(cerberus_display_fill_rect(238,20,70,18,PANEL));
    TRY(text(273-(int)strlen(badge)*3,25,badge,s->demo?LILAC:s->online?a:AMBER,PANEL,1));
    TRY(cerberus_display_fill_rect(12,48,296,37,PANEL));
    TRY(text(16,55,s->phase,WHITE,PANEL,1));
    if(s->demo)snprintf(linebuf,sizeof(linebuf),"ORIGINAL SCENES / %u OF %u",(unsigned)s->scene+1,SENTINEL_WATCH_SCENE_COUNT);
    else snprintf(linebuf,sizeof(linebuf),"OBSERVED %u    HINTS %u",(unsigned)s->observations,(unsigned)s->matches);
    TRY(text(16,72,linebuf,MUTED,PANEL,1));
    TRY(cerberus_display_fill_rect(12,84,296,1,a));
    return ESP_OK;
}
static esp_err_t draw_information(const snapshot_t *s)
{
    TRY(cerberus_display_fill_rect(0,318,320,95,BG));
    TRY(centered(323,s->title,accent(s->scene),BG,2));
    TRY(cerberus_display_fill_rect(8,348,304,64,PANEL));
    if(s->details)TRY(cerberus_display_fill_rect(8,348,2,64,accent(s->scene)));
    static const char *const confidence[]={"NO MATCH SELECTED","LOW CONFIDENCE","MEDIUM CONFIDENCE","HIGH CONFIDENCE"};
    TRY(text(16,356,s->demo?"DEMO ART / NOT A LIVE OBSERVATION":confidence[s->confidence],s->demo?LILAC:MUTED,PANEL,1));
    TRY(text(16,373,s->evidence,WHITE,PANEL,1));
    TRY(text(16,391,s->detail,MUTED,PANEL,1));
    if(s->details)TRY(text(254,403,"DETAILS >",accent(s->scene),PANEL,1));
    return ESP_OK;
}
static esp_err_t draw_controls(const snapshot_t *s)
{
    for(unsigned i=0;i<4;++i){const target_t *t=&targets[i];
        const char *label=i==0?"BACK":i==1?(s->paused?"RESUME":"PAUSE"):i==2?"LOG":s->demo?"NEXT":"OPTIONS";
        bool enabled=!(s->demo && (i==1 || i==2));uint16_t fg=enabled?WHITE:EDGE;
        TRY(cerberus_display_fill_rect(t->x,t->y,t->w,t->h,PANEL));
        TRY(cerberus_display_fill_rect(t->x,t->y,t->w,1,enabled?accent(s->scene):EDGE));
        TRY(text(t->x+(t->w-(int)strlen(label)*6)/2,t->y+18,label,fg,PANEL,1));
    }
    return ESP_OK;
}

void sentinel_watch_view_invalidate(void){previous_valid=false;}
static esp_err_t full_draw(const snapshot_t *s)
{
    TRY(cerberus_display_fill_rect(0,0,320,480,BG));
    TRY(draw_header(s));TRY(draw_art(s));TRY(draw_information(s));TRY(draw_controls(s));return ESP_OK;
}
esp_err_t sentinel_watch_view_draw(const sentinel_watch_view_t *v,uint32_t now_ms)
{
    previous_valid=false;if(!valid(v))return ESP_ERR_INVALID_ARG;
    snapshot_t s=snapshot(v,now_ms);TRY(full_draw(&s));previous=s;previous_valid=true;return ESP_OK;
}
esp_err_t sentinel_watch_view_update(const sentinel_watch_view_t *v,uint32_t now_ms)
{
    if(!valid(v)){previous_valid=false;return ESP_ERR_INVALID_ARG;}
    snapshot_t s=snapshot(v,now_ms);
    if(!previous_valid)return sentinel_watch_view_draw(v,now_ms);
    if(memcmp(&s,&previous,sizeof(s))==0)return ESP_OK;
    previous_valid=false;
    if(s.scene!=previous.scene || s.demo!=previous.demo || s.paused!=previous.paused || s.online!=previous.online ||
        s.observations!=previous.observations || s.matches!=previous.matches || strcmp(s.phase,previous.phase))TRY(draw_header(&s));
    if(s.scene!=previous.scene || s.frame!=previous.frame || s.demo!=previous.demo ||
        s.paused!=previous.paused || s.online!=previous.online)TRY(draw_art(&s));
    if(s.scene!=previous.scene || s.demo!=previous.demo || s.confidence!=previous.confidence || s.details!=previous.details ||
        strcmp(s.title,previous.title)||strcmp(s.evidence,previous.evidence)||strcmp(s.detail,previous.detail))TRY(draw_information(&s));
    if(s.scene!=previous.scene || s.demo!=previous.demo || s.paused!=previous.paused)TRY(draw_controls(&s));
    previous=s;previous_valid=true;return ESP_OK;
}
sentinel_watch_action_t sentinel_watch_view_hit_test(const sentinel_watch_view_t *v,uint16_t x,uint16_t y)
{
    if(!valid(v))return SENTINEL_WATCH_ACTION_NONE;
    for(unsigned i=0;i<sizeof(targets)/sizeof(targets[0]);++i){const target_t *t=&targets[i];
        if(x<t->x || x>=t->x+t->w || y<t->y || y>=t->y+t->h)continue;
        if(i==4 && !v->has_details)return SENTINEL_WATCH_ACTION_NONE;
        if(v->demo && (i==1 || i==2))return SENTINEL_WATCH_ACTION_NONE;
        return t->action;
    }
    return SENTINEL_WATCH_ACTION_NONE;
}
