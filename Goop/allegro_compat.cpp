#include "allegro_compat.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

BITMAP* screen = NULL;
char allegro_error[1024] = {0};
int cpu_capabilities = 0;

int current_color_depth = 32;
int current_color_conversion = 0;

void draw_sprite(BITMAP* dest, BITMAP* src, int x, int y) {
    if (!dest || !src) return;
    if (src->format == 32 && dest->format == 32) {
        // Masked blit via alpha: load_bitmap converts the magenta maskcolor
        // (0xFFFF00FF) to transparent black (0x00000000), so SDL_BLENDMODE_BLEND
        // skips those alpha-0 pixels while copying opaque (alpha-255) body pixels.
        sdlBlitBlendMode(dest, src, x, y, 0, 0, src->w, src->h, 255, SDL_BLENDMODE_BLEND);
    } else {
        // For sub-bitmaps, the source rect must be offset into the parent surface
        int src_sx = src->is_sub_bitmap ? src->sub_x : 0;
        int src_sy = src->is_sub_bitmap ? src->sub_y : 0;
        SDL_Rect src_rect = {src_sx, src_sy, src->w, src->h};
        SDL_Rect dest_rect = {x, y, src->w, src->h};
        SDL_BlitSurface(src->sdl_surface, &src_rect, dest->sdl_surface, &dest_rect);
    }
}

void sdlBlitBlendMode(BITMAP* dest, BITMAP* src, int dx, int dy,
                      int sx, int sy, int sw, int sh,
                      int fact, SDL_BlendMode mode) {
    if (!dest || !src) return;
    if (src->format != 32 || dest->format != 32) return;
    if (fact < 0) fact = 0;
    if (fact > 255) fact = 255;
    SDL_SetSurfaceAlphaMod(src->sdl_surface, (Uint8)fact);
    SDL_SetSurfaceBlendMode(src->sdl_surface, mode);
    // For sub-bitmaps, the source rect must be offset into the parent surface
    int src_sx = src->is_sub_bitmap ? src->sub_x + sx : sx;
    int src_sy = src->is_sub_bitmap ? src->sub_y + sy : sy;
    SDL_Rect srcrect = {src_sx, src_sy, sw, sh};
    SDL_Rect dstrect = {dx, dy, sw, sh};
    SDL_BlitSurface(src->sdl_surface, &srcrect, dest->sdl_surface, &dstrect);
    SDL_SetSurfaceBlendMode(src->sdl_surface, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceAlphaMod(src->sdl_surface, 255);
}

BITMAP* load_bitmap(const char* filename, RGB* pal) {
    SDL_Surface* surf = IMG_Load(filename);
    if (!surf) return NULL;
    
    // Convert to current color depth if needed (Allegro behavior)
    int target_depth = current_color_depth;
    int src_bpp = SDL_BITSPERPIXEL(surf->format);
    if (src_bpp != target_depth && target_depth != 0) {
        if (target_depth == 8) {
            // Manual conversion to INDEX8. The font system uses pixel index values
            // as alpha factors: 0 = transparent, 255 = fully opaque.
            // Map standard transparent magenta (255,0,255) to index 0.
            SDL_Surface* idx8 = SDL_CreateSurface(surf->w, surf->h, SDL_PIXELFORMAT_INDEX8);
            if (idx8) {
                // Create palette: index 0 = magenta (transparent), rest = white
                SDL_Color palette_colors[256];
                for (int i = 0; i < 256; ++i) {
                    palette_colors[i] = (SDL_Color){255, 255, 255, 255};
                }
                palette_colors[0] = (SDL_Color){255, 0, 255, 255};
                SDL_SetPaletteColors(SDL_GetSurfacePalette(idx8), palette_colors, 0, 256);
                
                // Lock both surfaces for pixel access
                SDL_LockSurface(surf);
                SDL_LockSurface(idx8);
                
                const SDL_PixelFormatDetails* src_fmt = SDL_GetPixelFormatDetails(surf->format);
                const SDL_PixelFormatDetails* dst_fmt = SDL_GetPixelFormatDetails(idx8->format);
                
                for (int y = 0; y < surf->h; ++y) {
                    Uint8* src_row = (Uint8*)surf->pixels + y * surf->pitch;
                    Uint8* dst_row = (Uint8*)idx8->pixels + y * idx8->pitch;
                    Uint8 src_bpp = SDL_BYTESPERPIXEL(surf->format);
                    
                    for (int x = 0; x < surf->w; ++x) {
                        Uint8 r, g, b, a = 255;
                        // Read pixel components directly from source row
                        if (src_bpp == 4) {
                            Uint32 pixel = *(Uint32*)(src_row + x * 4);
                            SDL_GetRGBA(pixel, src_fmt, NULL, &r, &g, &b, &a);
                        } else if (src_bpp == 3) {
                            r = src_row[x * 3 + 0];
                            g = src_row[x * 3 + 1];
                            b = src_row[x * 3 + 2];
                        } else if (src_bpp == 2) {
                            Uint16 pixel = *(Uint16*)(src_row + x * 2);
                            SDL_GetRGBA(pixel, src_fmt, NULL, &r, &g, &b, &a);
                        } else {
                            // 1 byte: palette index
                            Uint8 pixel = src_row[x];
                            SDL_GetRGBA(pixel, src_fmt, SDL_GetSurfacePalette(surf), &r, &g, &b, &a);
                        }
                        // Magenta (255,0,255) or fully transparent pixels map to index 0
                        if ((r == 255 && g == 0 && b == 255 && a >= 128) || (a < 128)) {
                            dst_row[x] = 0;
                        } else {
                            dst_row[x] = 255;
                        }
                    }
                }
                
                SDL_UnlockSurface(idx8);
                SDL_UnlockSurface(surf);
                SDL_DestroySurface(surf);
                surf = idx8;
            }
        } else {
            // Use SDL_ConvertSurface for 16 or 32 bit targets
            SDL_PixelFormat target_format;
            if (target_depth == 32) target_format = SDL_PIXELFORMAT_ARGB8888;
            else target_format = SDL_PIXELFORMAT_RGB565;
            
            SDL_Surface* converted = SDL_ConvertSurface(surf, target_format);
            if (converted) {
                SDL_DestroySurface(surf);
                surf = converted;
            }
        }
    }
    
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

    // Convert maskcolor (0xFFFF00FF magenta) → transparent black (0x00000000)
    // for 32-bit bitmaps. The now-alpha-0 mask pixels are skipped by
    // SDL_BLENDMODE_BLEND/BLENDMODE_ADD (used by draw_sprite/masked_blit and
    // the add/blend blitters), so the mask is transparent without per-pixel
    // 0xFFFF00FF checks.
    //
    // IMPORTANT: SDL_SetSurfaceColorKey is deliberately NOT used. SDL color-key
    // ignores alpha, so key 0x00000000 would also match opaque-black 0xFF000000,
    // eating legitimate black pixels during blit() (sprite-sheet splitting) —
    // e.g. the skin-mask's colorable markers and worm outlines. Alpha-based
    // blending (BLEND) is used instead, since it honours the alpha channel and
    // distinguishes 0x00000000 (mask) from 0xFF000000 (opaque black).
    if (bmp->format == 32) {
        SDL_LockSurface(surf);
        for (int i = 0; i < surf->h; ++i) {
            Uint32* row = (Uint32*)bmp->line[i];
            for (int x = 0; x < surf->w; ++x) {
                if (row[x] == 0xFFFF00FF)
                    row[x] = 0x00000000;
            }
        }
        SDL_UnlockSurface(surf);
        SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_NONE);
    }

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
    // Clamp to parent bounds so sub-bitmaps never reference rows/columns
    // past the parent's line array. Some font files declare glyph rects
    // that extend one row past the bitmap height; without clamping this
    // triggers an out-of-bounds read on parent->line[].
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x > parent->w) x = parent->w;
    if (y > parent->h) y = parent->h;
    if (x + w > parent->w) w = parent->w - x;
    if (y + h > parent->h) h = parent->h - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    BITMAP* bmp = new BITMAP();
    bmp->w = w;
    bmp->h = h;
    bmp->cl = 0; bmp->ct = 0; bmp->cr = w; bmp->cb = h;
    bmp->format = parent->format;
    bmp->pixels = parent->pixels; // Pointer to parent pixels
    bmp->line = (unsigned char**)malloc(h * sizeof(void*));

    int bpp = bmp->format / 8;

    for (int i = 0; i < h; ++i) {
        bmp->line[i] = (unsigned char*)parent->line[y + i] + x * bpp;
    }
    bmp->is_sub_bitmap = true;
    bmp->sub_x = x;
    bmp->sub_y = y;
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
    // For sub-bitmaps, the source rect must be offset into the parent surface
    int src_sx = src->is_sub_bitmap ? src->sub_x + s_x : s_x;
    int src_sy = src->is_sub_bitmap ? src->sub_y + s_y : s_y;
    SDL_Rect src_rect = {src_sx, src_sy, w, h};
    SDL_Rect dest_rect = {d_x, d_y, w, h};
    SDL_BlitSurface(src->sdl_surface, &src_rect, dest->sdl_surface, &dest_rect);
}

void masked_blit(BITMAP* src, BITMAP* dest, int s_x, int s_y, int d_x, int d_y, int w, int h) {
    if (!src || !dest) return;
    if (src->format == 32 && dest->format == 32) {
        // Alpha-masked blit (see draw_sprite): alpha-0 mask skipped, opaque
        // body pixels copied. SDL_BlitSurface alone would copy the 0x00000000
        // mask as black, so BLEND is required for masking.
        sdlBlitBlendMode(dest, src, d_x, d_y, s_x, s_y, w, h, 255, SDL_BLENDMODE_BLEND);
    } else {
        // For sub-bitmaps, the source rect must be offset into the parent surface
        int src_sx = src->is_sub_bitmap ? src->sub_x + s_x : s_x;
        int src_sy = src->is_sub_bitmap ? src->sub_y + s_y : s_y;
        SDL_Rect src_rect = {src_sx, src_sy, w, h};
        SDL_Rect dest_rect = {d_x, d_y, w, h};
        SDL_BlitSurface(src->sdl_surface, &src_rect, dest->sdl_surface, &dest_rect);
    }
}

void stretch_blit(BITMAP* src, BITMAP* dest, int s_x, int s_y, int s_w, int s_h, int d_x, int d_y, int d_w, int d_h) {
    if (!src || !dest) return;
    // For sub-bitmaps, the source rect must be offset into the parent surface
    int src_sx = src->is_sub_bitmap ? src->sub_x + s_x : s_x;
    int src_sy = src->is_sub_bitmap ? src->sub_y + s_y : s_y;
    SDL_Rect src_rect = {src_sx, src_sy, s_w, s_h};
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
    if (!bmp) return;
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy, e2;
    while (true) {
        putpixel(bmp, x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

// Allegro compatibility bootstrap
int allegro_init() { return 0; }

void show_mouse(BITMAP* bmp) {}
void clear_keybuf() {}
void vsync() {}

// Screen dimensions
int SCREEN_W = 320;
int SCREEN_H = 240;

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
