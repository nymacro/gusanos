#include "allegro_compat.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

BITMAP* screen = NULL;
char allegro_error[1024] = {0};
bool key[KEY_MAX] = {0};
int cpu_capabilities = 0;

static int current_color_depth = 32;

void draw_sprite(BITMAP* dest, BITMAP* src, int x, int y) {
    if (!dest || !src) return;
    SDL_Rect src_rect = {0, 0, src->w, src->h};
    SDL_Rect dest_rect = {x, y, src->w, src->h};
    SDL_BlitSurface(src->sdl_surface, &src_rect, dest->sdl_surface, &dest_rect);
}

BITMAP* load_bitmap(const char* filename, RGB* pal) {
    SDL_Surface* surf = IMG_Load(filename);
    if (!surf) return NULL;
    
    BITMAP* bmp = new BITMAP();
    bmp->w = surf->w;
    bmp->h = surf->h;
    bmp->cl = 0; bmp->ct = 0; bmp->cr = surf->w; bmp->cb = surf->h;
    bmp->sdl_surface = surf;
    bmp->pixels = surf->pixels;
    bmp->format = SDL_BITSPERPIXEL(surf->format);
    bmp->line = (unsigned char**)malloc(surf->h * sizeof(void*));
    for (int i = 0; i < surf->h; ++i) {
        bmp->line[i] = (unsigned char*)bmp->pixels + i * surf->pitch;
    }
    bmp->is_sub_bitmap = false;
    return bmp;
}

int save_bitmap(const char* filename, BITMAP* bmp, RGB* pal) {
    if (!bmp || !bmp->sdl_surface) return -1;
    // SDL3 doesn't have a generic save function, but we can use IMG_SavePNG
    if (IMG_SavePNG(bmp->sdl_surface, filename)) return 0;
    return -1;
}

BITMAP* create_bitmap_ex(int depth, int w, int h) {
    BITMAP* bmp = new BITMAP();
    bmp->w = w;
    bmp->h = h;
    bmp->cl = 0; bmp->ct = 0; bmp->cr = w; bmp->cb = h;
    bmp->format = depth;
    
    SDL_PixelFormat format;
    if (depth == 32) format = SDL_PIXELFORMAT_ARGB8888;
    else if (depth == 16) format = SDL_PIXELFORMAT_RGB565;
    else format = SDL_PIXELFORMAT_INDEX8;

    bmp->sdl_surface = SDL_CreateSurface(w, h, format);
    if (!bmp->sdl_surface) {
        delete bmp;
        return NULL;
    }
    bmp->pixels = bmp->sdl_surface->pixels;
    bmp->line = (unsigned char**)malloc(h * sizeof(void*));
    for (int i = 0; i < h; ++i) {
        bmp->line[i] = (unsigned char*)bmp->pixels + i * bmp->sdl_surface->pitch;
    }
    bmp->is_sub_bitmap = false;
    return bmp;
}

BITMAP* create_bitmap(int w, int h) {
    return create_bitmap_ex(current_color_depth, w, h);
}

void destroy_bitmap(BITMAP* bmp) {
    if (!bmp) return;
    if (!bmp->is_sub_bitmap) {
        if (bmp->sdl_surface) SDL_DestroySurface(bmp->sdl_surface);
        else free(bmp->pixels);
    }
    free(bmp->line);
    delete bmp;
}

BITMAP* create_sub_bitmap(BITMAP* parent, int x, int y, int w, int h) {
    BITMAP* bmp = new BITMAP();
    bmp->w = w;
    bmp->h = h;
    bmp->cl = 0; bmp->ct = 0; bmp->cr = w; bmp->cb = h;
    bmp->format = parent->format;
    bmp->pixels = parent->pixels; // Pointer to parent pixels
    bmp->line = (unsigned char**)malloc(h * sizeof(void*));
    
    int bpp = bmp->format / 8;
    int pitch = parent->sdl_surface ? parent->sdl_surface->pitch : parent->w * bpp;
    
    for (int i = 0; i < h; ++i) {
        bmp->line[i] = (unsigned char*)parent->line[y + i] + x * bpp;
    }
    bmp->is_sub_bitmap = true;
    bmp->sdl_surface = parent->sdl_surface;
    return bmp;
}

// set_color_depth/get_color_conversion/set_color_conversion are inline in allegro_compat.h

void drawing_mode(int mode, BITMAP* pattern, int x, int y) {}
void solid_mode() {}
void set_trans_blender(int r, int g, int b, int a) {}
void set_add_blender(int r, int g, int b, int a) {}

int set_gfx_mode(int card, int w, int h, int v_w, int v_h) {
    // This will be handled in gfx.cpp with real SDL3 calls
    return 0;
}

int set_display_switch_mode(int mode) { return 0; }

void blit(BITMAP* src, BITMAP* dest, int s_x, int s_y, int d_x, int d_y, int w, int h) {
    if (!src || !dest) return;
    SDL_Rect src_rect = {s_x, s_y, w, h};
    SDL_Rect dest_rect = {d_x, d_y, w, h};
    SDL_BlitSurface(src->sdl_surface, &src_rect, dest->sdl_surface, &dest_rect);
}

void masked_blit(BITMAP* src, BITMAP* dest, int s_x, int s_y, int d_x, int d_y, int w, int h) {
    // In SDL, masked blit is just blit with a colorkey or alpha
    // For simplicity, we'll just use SDL_BlitSurface for now
    blit(src, dest, s_x, s_y, d_x, d_y, w, h);
}

void stretch_blit(BITMAP* src, BITMAP* dest, int s_x, int s_y, int s_w, int s_h, int d_x, int d_y, int d_w, int d_h) {
    if (!src || !dest) return;
    SDL_Rect src_rect = {s_x, s_y, s_w, s_h};
    SDL_Rect dest_rect = {d_x, d_y, d_w, d_h};
    SDL_BlitSurfaceScaled(src->sdl_surface, &src_rect, dest->sdl_surface, &dest_rect, SDL_SCALEMODE_NEAREST);
}

void clear_bitmap(BITMAP* bmp) {
    if (!bmp) return;
    SDL_FillSurfaceRect(bmp->sdl_surface, NULL, 0);
}

void clear_to_color(BITMAP* bmp, int color) {
    if (!bmp) return;
    SDL_FillSurfaceRect(bmp->sdl_surface, NULL, color);
}

void rectfill(BITMAP* bmp, int x1, int y1, int x2, int y2, int color) {
    if (!bmp) return;
    SDL_Rect rect = {x1, y1, x2 - x1 + 1, y2 - y1 + 1};
    SDL_FillSurfaceRect(bmp->sdl_surface, &rect, color);
}

void hline(BITMAP* bmp, int x1, int y1, int x2, int color) {
    rectfill(bmp, x1, y1, x2, y1, color);
}

void line(BITMAP* bmp, int x1, int y1, int x2, int y2, int color) {
    // Basic line drawing if needed, but Gusanos uses its own linewu_blend most of the time
}

// Allegro compatibility bootstrap
int allegro_init() { return 0; }

void show_mouse(BITMAP* bmp) {}
void clear_keybuf() {}
void vsync() {}

// Screen dimensions
int SCREEN_W = 320;
int SCREEN_H = 240;

// Keyboard stubs (real SDL3 port pending)
int install_keyboard() { return 0; }
void remove_keyboard() {}
int keypressed() { return 0; }
int readkey() { return 0; }

// Mouse globals + stubs (real SDL3 port pending)
void (*mouse_callback)(int flags) = nullptr;
volatile int mouse_b = 0;
volatile int mouse_x = 0;
volatile int mouse_y = 0;
volatile int mouse_z = 0;

int install_mouse() { return 0; }
void remove_mouse() {}
void poll_mouse() {}

// Timer stubs (real SDL3 port pending)
int install_timer() { return 0; }
void remove_timer() {}
int install_int_ex(void (*)(), int) { return 0; }

// Additional drawing functions
void vline(BITMAP* bmp, int x, int y1, int y2, int color) {
	rectfill(bmp, x, y1, x, y2, color);
}

void set_clip_rect(BITMAP* bmp, int x1, int y1, int x2, int y2) {
	if (!bmp || !bmp->sdl_surface) return;
	bmp->cl = x1; bmp->ct = y1; bmp->cr = x2; bmp->cb = y2;
	SDL_Rect rect = {x1, y1, x2 - x1 + 1, y2 - y1 + 1};
	SDL_SetSurfaceClipRect(bmp->sdl_surface, &rect);
}

void get_clip_rect(BITMAP* bmp, int* x1, int* y1, int* x2, int* y2) {
	if (!bmp) return;
	*x1 = bmp->cl; *y1 = bmp->ct; *x2 = bmp->cr; *y2 = bmp->cb;
}

void _circle(BITMAP* bmp, int x, int y, int radius, int color, bool filled) {
    if (!bmp || radius <= 0) return;
    int cx = x, cy = y, r = radius;
    int d = 3 - 2 * r;
    int xp = 0, yp = r;
    while (xp <= yp) {
        if (filled) {
            hline(bmp, cx - xp, cy - yp, cx + xp, color);
            hline(bmp, cx - xp, cy + yp, cx + xp, color);
            hline(bmp, cx - yp, cy - xp, cx + yp, color);
            hline(bmp, cx - yp, cy + xp, cx + yp, color);
        } else {
            putpixel(bmp, cx + xp, cy + yp, color);
            putpixel(bmp, cx - xp, cy + yp, color);
            putpixel(bmp, cx + xp, cy - yp, color);
            putpixel(bmp, cx - xp, cy - yp, color);
            putpixel(bmp, cx + yp, cy + xp, color);
            putpixel(bmp, cx - yp, cy + xp, color);
            putpixel(bmp, cx + yp, cy - xp, color);
            putpixel(bmp, cx - yp, cy - xp, color);
        }
        if (d < 0) {
            d += 4 * xp + 6;
        } else {
            d += 4 * (xp - yp) + 10;
            yp--;
        }
        xp++;
    }
    // Draw center pixel for filled circles
    if (filled) {
        hline(bmp, cx - r, cy, cx + r, color);
    }
}

void draw_sprite_h_flip(BITMAP* dest, BITMAP* src, int x, int y) {
    if (!dest || !src) return;
    for (int sy = 0; sy < src->h; sy++) {
        int dy = y + sy;
        if (dy < 0 || dy >= dest->h) continue;
        for (int sx = 0; sx < src->w; sx++) {
            int dx = x + (src->w - 1 - sx);
            if (dx < 0 || dx >= dest->w) continue;
            unsigned char* sp = src->line[sy] + sx * 4;
            unsigned char* dp = dest->line[dy] + dx * 4;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
}

void rgb_to_hsv(int r, int g, int b, float* h, float* s, float* v) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float mx = std::max(rf, std::max(gf, bf));
    float mn = std::min(rf, std::min(gf, bf));
    float delta = mx - mn;
    *v = mx;
    if (delta < 0.0001f) {
        *h = 0.0f;
        *s = 0.0f;
        return;
    }
    *s = delta / mx;
    if (rf >= mx) {
        *h = (gf - bf) / delta;
    } else if (gf >= mx) {
        *h = 2.0f + (bf - rf) / delta;
    } else {
        *h = 4.0f + (rf - gf) / delta;
    }
    *h *= 60.0f;
    if (*h < 0.0f) *h += 360.0f;
}

void hsv_to_rgb(float h, float s, float v, int* r, int* g, int* b) {
    if (s < 0.0001f) {
        *r = *g = *b = static_cast<int>(v * 255.0f);
        return;
    }
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    h /= 60.0f;
    int hi = static_cast<int>(h);
    float f = h - hi;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    float rv, gv, bv;
    switch (hi) {
        case 0: rv = v; gv = t; bv = p; break;
        case 1: rv = q; gv = v; bv = p; break;
        case 2: rv = p; gv = v; bv = t; break;
        case 3: rv = p; gv = q; bv = v; break;
        case 4: rv = t; gv = p; bv = v; break;
        default: rv = v; gv = p; bv = q; break;
    }
    *r = static_cast<int>(rv * 255.0f);
    *g = static_cast<int>(gv * 255.0f);
    *b = static_cast<int>(bv * 255.0f);
}
