#ifndef ALLEGRO_COMPAT_H
#define ALLEGRO_COMPAT_H

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <vector>
#include <string>
#include <cstdarg>
#include <cstdio>

// Basic Allegro types
typedef struct RGB {
    unsigned char r, g, b;
} RGB;

typedef struct BITMAP {
    int w, h;
    int cl, ct, cr, cb;
    unsigned char** line;
    int format; // bit depth: 8, 16, 32
    void* pixels;
    bool is_sub_bitmap;
    SDL_Surface* sdl_surface; // underlying SDL surface if applicable
} BITMAP;

// Color depth functions
inline int bitmap_color_depth(BITMAP* bmp) { return bmp->format; }
inline int get_color_depth() { extern int current_color_depth; return current_color_depth; } // Defaulting to 32

// 16-bit color functions (5-6-5 RGB format)
inline int getr16(int c) { return ((c >> 11) & 0x1F) * 255 / 31; }
inline int getg16(int c) { return ((c >> 5) & 0x3F) * 255 / 63; }
inline int getb16(int c) { return (c & 0x1F) * 255 / 31; }
inline int makecol16(int r, int g, int b) { return ((r/8) << 11) | ((g/4) << 5) | (b/8); }

// Bitmap creation/destruction
BITMAP* create_bitmap(int w, int h);
BITMAP* create_bitmap_ex(int depth, int w, int h);
void destroy_bitmap(BITMAP* bmp);
BITMAP* create_sub_bitmap(BITMAP* parent, int x, int y, int w, int h);
BITMAP* load_bitmap(const char* filename, RGB* pal);
int save_bitmap(const char* filename, BITMAP* bmp, RGB* pal);

// Color conversion and depth
inline void set_color_depth(int depth) { extern int current_color_depth; current_color_depth = depth; }
inline int get_color_conversion() { extern int current_color_conversion; return current_color_conversion; }
inline void set_color_conversion(int flags) { extern int current_color_conversion; current_color_conversion = flags; }

#define COLORCONV_NONE 0
#define COLORCONV_TOTAL 0
#define COLORCONV_KEEP_TRANS 0
#define COLORCONV_DITHER 0
#define COLORCONV_EXPAND_256 0
#define COLORCONV_REDUCE_TO_256 0
#define COLORCONV_15_TO_8 0
#define COLORCONV_16_TO_8 0
#define COLORCONV_24_TO_8 0
#define COLORCONV_32_TO_8 0
#define COLORCONV_EXPAND_15_TO_16 0
#define COLORCONV_REDUCE_16_TO_15 0
#define COLORCONV_EXPAND_HI_TO_TRUE 0
#define COLORCONV_REDUCE_TRUE_TO_HI 0
#define COLORCONV_24_EQUALS_32 0
#define COLORCONV_KEEP_ALPHA 0

// Drawing modes
#define DRAW_MODE_SOLID 0
#define DRAW_MODE_TRANS 1
void drawing_mode(int mode, BITMAP* pattern, int x, int y);
void solid_mode();
void set_trans_blender(int r, int g, int b, int a);
void set_add_blender(int r, int g, int b, int a);

// Graphics mode
#define GFX_AUTODETECT 0
#define GFX_AUTODETECT_FULLSCREEN 1
#define GFX_AUTODETECT_WINDOWED 2
#define GFX_DIRECTX 3
#define GFX_DIRECTX_WIN 4
#define GFX_XDGA 5
#define GFX_XDGA2 6
#define GFX_XWINDOWS 7
#define GFX_XWINDOWS_FULLSCREEN 8
#define GFX_XDGA_FULLSCREEN 9
#define GFX_AUTODETECT_SMART 10

#define SWITCH_BACKGROUND 0
#define SWITCH_BACKAMNESIA 1
int set_gfx_mode(int card, int w, int h, int v_w, int v_h);
int set_display_switch_mode(int mode);

// Blitting
void blit(BITMAP* src, BITMAP* dest, int s_x, int s_y, int d_x, int d_y, int w, int h);
void masked_blit(BITMAP* src, BITMAP* dest, int s_x, int s_y, int d_x, int d_y, int w, int h);
void stretch_blit(BITMAP* src, BITMAP* dest, int s_x, int s_y, int s_w, int s_h, int d_x, int d_y, int d_w, int d_h);
void draw_sprite(BITMAP* dest, BITMAP* src, int x, int y);
void clear_bitmap(BITMAP* bmp);
void clear_to_color(BITMAP* bmp, int color);
void rectfill(BITMAP* bmp, int x1, int y1, int x2, int y2, int color);
void hline(BITMAP* bmp, int x1, int y1, int x2, int color);
void line(BITMAP* bmp, int x1, int y1, int x2, int y2, int color);

// Pixel access
inline int getr(int c) { return (c >> 16) & 0xFF; }
inline int getg(int c) { return (c >> 8) & 0xFF; }
inline int getb(int c) { return c & 0xFF; }
inline int geta(int c) { return (c >> 24) & 0xFF; }
inline int makecol(int r, int g, int b) { return (255 << 24) | (r << 16) | (g << 8) | b; }
inline int makecol32(int r, int g, int b) { return (255 << 24) | (r << 16) | (g << 8) | b; }
inline int makecol_depth(int depth, int r, int g, int b) { return makecol(r, g, b); }

// Per-pixel operations (depth-aware: supports 8, 16, 32 bpp)
inline int getpixel(BITMAP* bmp, int x, int y) {
    if (x < 0 || x >= bmp->w || y < 0 || y >= bmp->h) return 0;
    int bpp = bmp->format / 8;
    unsigned char* p = bmp->line[y] + x * bpp;
    if (bpp == 1) return p[0];
    if (bpp == 2) return *(unsigned short*)p;
    return (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
}
inline void putpixel(BITMAP* bmp, int x, int y, int color) {
    if (x < 0 || x >= bmp->w || y < 0 || y >= bmp->h) return;
    int bpp = bmp->format / 8;
    unsigned char* p = bmp->line[y] + x * bpp;
    if (bpp == 1) { p[0] = color & 0xFF; return; }
    if (bpp == 2) { *(unsigned short*)p = (unsigned short)color; return; }
    // SDL_PIXELFORMAT_ARGB8888 on little-endian: byte order B, G, R, A
    // makecol() returns 0xAARRGGBB, so byte[0]=B, byte[1]=G, byte[2]=R, byte[3]=A
    p[0] = color & 0xFF;
    p[1] = (color >> 8) & 0xFF;
    p[2] = (color >> 16) & 0xFF;
    p[3] = (color >> 24) & 0xFF;
}

#define BYTES_PER_PIXEL(depth) ((depth + 7) / 8)
#define TRACE(...) printf(__VA_ARGS__)

inline bool is_video_bitmap(BITMAP* bmp) { return false; }
inline bool is_planar_bitmap(BITMAP* bmp) { return false; }
inline void bmp_select(BITMAP* bmp) {}
inline unsigned long bmp_write_line(BITMAP* bmp, int line) { return (unsigned long)bmp->line[line]; }
inline void bmp_write32(unsigned long addr, unsigned long color) { *(unsigned long*)addr = color; }
inline void bmp_unwrite_line(BITMAP* bmp) {}

inline void allegro_message(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    vprintf(msg, args);
    va_end(args);
    printf("\n");
}

#define CPU_MMX 1
#define CPU_MMXPLUS 2
#define CPU_SSE 4
extern int cpu_capabilities;

// Global screen pointer
extern BITMAP* screen;
extern char allegro_error[];

// Allegro bootstrap
int allegro_init();

// Timer
int install_timer();
void remove_timer();
int install_int_ex(void (*proc)(), int speed);
#define BPS_TO_TIMER(bps) (1000 / (bps))

// Locking macros — no-op on SDL3
#define LOCK_VARIABLE(x)
#define LOCK_FUNCTION(x)
#define END_OF_FUNCTION(f)

// Screen dimensions (set by set_gfx_mode)
extern int SCREEN_W;
extern int SCREEN_H;

// Additional drawing functions
void vline(BITMAP* bmp, int x, int y1, int y2, int color);
void _circle(BITMAP* bmp, int x, int y, int radius, int color, bool filled);
inline void circle(BITMAP* bmp, int x, int y, int radius, int color) { _circle(bmp, x, y, radius, color, false); }
inline void circlefill(BITMAP* bmp, int x, int y, int radius, int color) { _circle(bmp, x, y, radius, color, true); }
void draw_sprite_h_flip(BITMAP* dest, BITMAP* src, int x, int y);
void set_clip_rect(BITMAP* bmp, int x1, int y1, int x2, int y2);
void get_clip_rect(BITMAP* bmp, int* x1, int* y1, int* x2, int* y2);

// Color conversion
void rgb_to_hsv(int r, int g, int b, float* h, float* s, float* v);
void hsv_to_rgb(float h, float s, float v, int* r, int* g, int* b);

// Input
#define KEY_MAX 128
enum {
    KEY_NULL = 0,
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_0_PAD, KEY_1_PAD, KEY_2_PAD, KEY_3_PAD, KEY_4_PAD, KEY_5_PAD, KEY_6_PAD, KEY_7_PAD, KEY_8_PAD, KEY_9_PAD,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_ESC, KEY_TILDE, KEY_MINUS, KEY_EQUALS, KEY_BACKSPACE, KEY_TAB, KEY_OPENBRACE, KEY_CLOSEBRACE, KEY_ENTER,
    KEY_COLON, KEY_QUOTE, KEY_BACKSLASH, KEY_BACKSLASH2, KEY_COMMA, KEY_STOP, KEY_SLASH, KEY_SPACE,
    KEY_INSERT, KEY_DEL, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_SLASH_PAD, KEY_ASTERISK, KEY_MINUS_PAD, KEY_PLUS_PAD, KEY_DEL_PAD, KEY_ENTER_PAD, KEY_PRTSCR, KEY_PAUSE,
    KEY_ABNT_C1, KEY_YEN, KEY_KANA, KEY_CONVERT, KEY_NOCONVERT, KEY_AT, KEY_CIRCUMFLEX, KEY_COLON2, KEY_KANJI,
    KEY_EQUALS_PAD, KEY_BACKQUOTE, KEY_SEMICOLON, KEY_COMMAND, KEY_UNKNOWN1, KEY_UNKNOWN2, KEY_UNKNOWN3, KEY_UNKNOWN4,
    KEY_UNKNOWN5, KEY_UNKNOWN6, KEY_UNKNOWN7, KEY_UNKNOWN8,
    KEY_LSHIFT, KEY_RSHIFT, KEY_LCONTROL, KEY_RCONTROL, KEY_ALT, KEY_ALTGR, KEY_LWIN, KEY_RWIN, KEY_MENU,
    KEY_SCRLOCK, KEY_NUMLOCK, KEY_CAPSLOCK
};

void clear_keybuf();

void vsync();

// Utility
inline bool exists(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

void show_mouse(BITMAP* bmp);

#endif // ALLEGRO_COMPAT_H
