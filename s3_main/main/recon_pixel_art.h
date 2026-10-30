/* Original code-native pixel art. Included only by the striped Watch renderer.
 * A tiny flash fallback keeps Recon usable without an SD card. No framebuffer,
 * filesystem reads, floating point, or allocation is required by this artwork. */
static void recon_pixel_head(int x, int y, uint16_t eye, bool blink, int look)
{
    static const char head_pixels[][18] = {
        "..11.........11..",
        ".1221.......1221.",
        ".12321.....12321.",
        ".123321111123321.",
        ".122333333333221.",
        "..1233333333321..",
        "..1233333333321..",
        "..1232233322321..",
        "..1233333333321..",
        "..1223444443221..",
        "...12344144321...",
        "...12341114321...",
        "....124444421....",
        "....122111221....",
        ".....1244421.....",
        "......12221......",
        ".......111.......",
    };
    const uint16_t palette[] = {DARK, DARK, ARMOR, STEEL, LIGHT};
    for (unsigned row = 0; row < sizeof(head_pixels) / sizeof(head_pixels[0]); ++row)
        for (unsigned col = 0; col < 17; ++col) {
            char p = head_pixels[row][col];
            if (p >= '1' && p <= '4') box(x + col * 4, y + row * 4, 4, 4, palette[p - '0']);
        }
    box(x + 20 + look * 2, y + 28, 8, blink ? 2 : 4, eye);
    box(x + 40 + look * 2, y + 28, 8, blink ? 2 : 4, eye);
}
static void recon_pixel_guardian(const snapshot_t *s)
{
    for (unsigned i = 0; i < ART_W * STRIPE_ROWS; ++i) stripe[i] = BG;
    uint16_t a = accent(s->scene);
    bool running = s->online && !s->paused;
    int step = running ? s->frame : 0;
    for (int y = 102; y < 310; y += 16) box(20, y, 280, 1, PANEL);
    for (int x = 20; x < 306; x += 16) box(x, 102, 1, 208, PANEL);
    /* Slow stepped sweep; decorative animation is not a signal measurement. */
    if (running) {
        int scan = 30 + (step % 20) * 13;
        box(scan, 102, 2, 208, EDGE);
        box(scan, 304, 4, 4, a);
    }
    box(76, 288, 168, 4, EDGE);
    int bob = running && step % 8 >= 4 ? 4 : 0;
    box(100, 206 + bob, 120, 60, DARK);
    box(108, 210 + bob, 104, 48, ARMOR);
    box(120, 214 + bob, 80, 32, STEEL);
    box(140, 216 + bob, 40, 40, DARK);
    box(148, 220 + bob, 24, 24, a);
    box(156, 224 + bob, 8, 16, BG);
    box(152, 228 + bob, 16, 8, BG);
    /* Four planted paws and stepped tail give the three heads one canine body. */
    box(108, 248, 20, 36, ARMOR); box(192, 248, 20, 36, ARMOR);
    box(128, 252, 20, 32, STEEL); box(172, 252, 20, 32, STEEL);
    box(100, 280, 28, 8, LIGHT); box(128, 280, 24, 8, LIGHT);
    box(168, 280, 24, 8, LIGHT); box(192, 280, 28, 8, LIGHT);
    box(88, 224 + bob, 20, 12, ARMOR); box(76, 212 + bob, 16, 20, STEEL);
    box(68, 196 + bob, 12, 20, STEEL); box(68, 188 + bob, 8, 12, LIGHT);
    box(96, 188 + bob, 32, 28, ARMOR); box(192, 188 + bob, 32, 28, ARMOR);
    bool blink = running && step == 18;
    int look = running ? step < 7 ? -1 : step < 14 ? 1 : 0 : 0;
    recon_pixel_head(64, 130 + bob, CYAN, blink, -1);
    recon_pixel_head(188, 130 + bob, LILAC, blink, 1);
    recon_pixel_head(126, 114 + bob, a, blink, look);
    box(104, 203 + bob, 24, 4, CYAN);
    box(192, 203 + bob, 24, 4, LILAC);
}
