/*
 * TvStreamer Launcher v1.0 (C Edition)
 * Ultra-lightweight SDL2 media center launcher
 *
 * Features:
 * - Event-driven rendering (near-zero idle CPU)
 * - Pre-cached textures for all UI elements
 * - Minimal memory footprint (~15-20MB)
 * - Instant startup (<100ms)
 * - Arc Blueberry theme
 *
 * Build: make
 * Dependencies: SDL2, SDL2_ttf, SDL2_image
 *
 * Author: Aleksa
 * License: MIT
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>

#define VERSION "1.0.2"

/* Arc Blueberry Color Palette */
#define COL_BG              0x11, 0x14, 0x22, 0xFF
#define COL_BG_SECONDARY    0x1A, 0x1E, 0x33, 0xFF
#define COL_BG_TILE         0x1E, 0x23, 0x37, 0xB8
#define COL_BG_TILE_SEL     0x2D, 0x34, 0x50, 0xD0
#define COL_FG              0xBC, 0xC1, 0xDC, 0xFF
#define COL_FG_DIM          0x42, 0x47, 0x61, 0xFF
#define COL_ACCENT          0x8E, 0xB0, 0xE6, 0xFF
#define COL_PINK            0xF3, 0x8C, 0xEC, 0xFF
#define COL_GREEN           0x3C, 0xEC, 0x85, 0xFF
#define COL_YELLOW          0xEA, 0xCD, 0x61, 0xFF
#define COL_RED             0xE3, 0x55, 0x35, 0xFF
#define COL_ORANGE          0xFF, 0x95, 0x5C, 0xFF
#define COL_CYAN            0x69, 0xC3, 0xFF, 0xFF

/* Layout constants */
#define TILE_WIDTH      140
#define TILE_HEIGHT     130
#define TILE_SPACING    20
#define TILE_RADIUS     16
#define NUM_APPS        5

/* Nerd Font Unicode codepoints */
#define ICON_TV         "\xEF\x89\xAC"      /* U+F26C */
#define ICON_PLAY       "\xEF\x81\x8B"      /* U+F04B */
#define ICON_VIDEO      "\xEF\x80\xBD"      /* U+F03D */
#define ICON_MUSIC      "\xEF\x80\x81"      /* U+F001 */
#define ICON_BLUETOOTH  "\xEF\x8A\x93"      /* U+F293 */
#define ICON_SETTINGS   "\xEF\x80\x93"      /* U+F013 */
#define ICON_CPU        "\xEF\x92\xBC"      /* U+F4BC */
#define ICON_MEMORY     "\xEE\xBF\x85"      /* U+EFC5 */
#define ICON_TEMP       "\xEF\x8B\x89"      /* U+F2C9 */
#define ICON_DISK       "\xEF\x82\xA0"      /* U+F0A0 */

/* App configuration */
typedef struct {
    const char *name;
    const char *command;
    const char *icon;
} App;

static const App apps[NUM_APPS] = {
    {"Kodi",      "kodi",                              ICON_TV},
    {"Stremio",   "/home/aleksa/.local/bin/stremio",   ICON_PLAY},
    {"IPTV",      "/home/aleksa/omarchy-iptv",         ICON_VIDEO},
    {"Tidal",     "tidal-hifi",                        ICON_MUSIC},
    {"Bluetooth", "blueman-manager",                   ICON_BLUETOOTH},
};

/* System stats */
typedef struct {
    int cpu;
    int mem;
    int temp;
    int disk;
    volatile int changed;
    volatile int running;
} Stats;

/* Global state */
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    int width, height;

    /* Fonts */
    TTF_Font *font_clock;
    TTF_Font *font_date;
    TTF_Font *font_tile;
    TTF_Font *font_stat_value;
    TTF_Font *font_stat_label;
    TTF_Font *font_icon;
    TTF_Font *font_icon_small;

    /* Cached textures */
    SDL_Texture *background;
    SDL_Texture *tile_bg_normal;
    SDL_Texture *tile_bg_selected;
    SDL_Texture *stats_bar_bg;
    SDL_Texture *settings_bg_normal;
    SDL_Texture *settings_bg_selected;
    SDL_Texture *tile_labels[NUM_APPS];
    SDL_Texture *tile_icons[NUM_APPS];
    SDL_Texture *tile_icons_dim[NUM_APPS];
    SDL_Texture *stat_labels[4];
    SDL_Texture *stat_icons[4];
    SDL_Texture *settings_icon;
    SDL_Texture *settings_icon_dim;
    SDL_Texture *help_text;

    /* State */
    int selected;
    int settings_selected;
    int needs_redraw;
    int last_minute;
    int app_running;  /* 1 if an app is in foreground */

    /* Stats */
    Stats stats;
    pthread_t stats_thread;

    /* Layout */
    SDL_Rect tile_rects[NUM_APPS];
    int stats_bar_x, stats_bar_y;
    int stats_bar_w, stats_bar_h;
} Launcher;

static Launcher *g_launcher = NULL;

/* Forward declarations */
static void launcher_destroy(Launcher *l);
static void draw_rounded_rect(SDL_Renderer *r, SDL_Rect *rect, int radius, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca);

/* ============ Utility Functions ============ */

static SDL_Color make_color(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_Color c = {r, g, b, a};
    return c;
}

static SDL_Texture *render_text(Launcher *l, TTF_Font *font, const char *text, SDL_Color color) {
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(l->renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

static void blit_texture_centered(Launcher *l, SDL_Texture *tex, int cx, int cy) {
    if (!tex) return;
    int w, h;
    SDL_QueryTexture(tex, NULL, NULL, &w, &h);
    SDL_Rect dst = {cx - w/2, cy - h/2, w, h};
    SDL_RenderCopy(l->renderer, tex, NULL, &dst);
}

/* ============ Rounded Rectangle ============ */

static void draw_rounded_rect(SDL_Renderer *r, SDL_Rect *rect, int radius,
                              Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);

    /* Fill center rectangles */
    SDL_Rect center = {rect->x + radius, rect->y, rect->w - 2*radius, rect->h};
    SDL_RenderFillRect(r, &center);
    SDL_Rect sides = {rect->x, rect->y + radius, rect->w, rect->h - 2*radius};
    SDL_RenderFillRect(r, &sides);

    /* Draw corners using filled circles */
    int x0, y0, x, y, d;
    int corners[4][2] = {
        {rect->x + radius, rect->y + radius},
        {rect->x + rect->w - radius - 1, rect->y + radius},
        {rect->x + radius, rect->y + rect->h - radius - 1},
        {rect->x + rect->w - radius - 1, rect->y + rect->h - radius - 1}
    };

    for (int c = 0; c < 4; c++) {
        x0 = corners[c][0];
        y0 = corners[c][1];
        x = radius;
        y = 0;
        d = 1 - radius;

        while (x >= y) {
            SDL_RenderDrawLine(r, x0 - x, y0 + y, x0 + x, y0 + y);
            SDL_RenderDrawLine(r, x0 - x, y0 - y, x0 + x, y0 - y);
            SDL_RenderDrawLine(r, x0 - y, y0 + x, x0 + y, y0 + x);
            SDL_RenderDrawLine(r, x0 - y, y0 - x, x0 + y, y0 - x);
            y++;
            if (d < 0) {
                d += 2 * y + 1;
            } else {
                x--;
                d += 2 * (y - x) + 1;
            }
        }
    }
}

static SDL_Texture *create_rounded_rect_texture(Launcher *l, int w, int h, int radius,
                                                 Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
    SDL_Texture *tex = SDL_CreateTexture(l->renderer, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tex) return NULL;

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(l->renderer, tex);
    SDL_SetRenderDrawColor(l->renderer, 0, 0, 0, 0);
    SDL_RenderClear(l->renderer);

    SDL_Rect rect = {0, 0, w, h};
    draw_rounded_rect(l->renderer, &rect, radius, cr, cg, cb, ca);

    SDL_SetRenderTarget(l->renderer, NULL);
    return tex;
}

/* ============ System Stats Thread ============ */

static void read_cpu_stats(long *idle, long *total) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return;

    char buf[256];
    if (fgets(buf, sizeof(buf), f)) {
        long user, nice, sys, idle_t, iowait, irq, softirq;
        sscanf(buf, "cpu %ld %ld %ld %ld %ld %ld %ld",
               &user, &nice, &sys, &idle_t, &iowait, &irq, &softirq);
        *idle = idle_t + iowait;
        *total = user + nice + sys + idle_t + iowait + irq + softirq;
    }
    fclose(f);
}

static void *stats_thread_func(void *arg) {
    Launcher *l = (Launcher *)arg;
    long prev_idle = 0, prev_total = 0;

    while (l->stats.running) {
        int old_cpu = l->stats.cpu;
        int old_mem = l->stats.mem;
        int old_temp = l->stats.temp;
        int old_disk = l->stats.disk;

        /* CPU */
        long idle = 0, total = 0;
        read_cpu_stats(&idle, &total);
        if (prev_total > 0) {
            long idle_d = idle - prev_idle;
            long total_d = total - prev_total;
            if (total_d > 0) {
                l->stats.cpu = 100 - (int)(100 * idle_d / total_d);
            }
        }
        prev_idle = idle;
        prev_total = total;

        /* Memory */
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            unsigned long total_mem = si.totalram * si.mem_unit;
            unsigned long free_mem = si.freeram * si.mem_unit;
            unsigned long buffers = si.bufferram * si.mem_unit;
            /* Available = free + buffers + cached (approximate) */
            unsigned long avail = free_mem + buffers;
            l->stats.mem = (int)(100 * (total_mem - avail) / total_mem);
        }

        /* Temperature */
        FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
        if (f) {
            int temp_milli;
            if (fscanf(f, "%d", &temp_milli) == 1) {
                l->stats.temp = temp_milli / 1000;
            }
            fclose(f);
        }

        /* Disk */
        struct statvfs sv;
        if (statvfs("/", &sv) == 0) {
            unsigned long total_d = sv.f_blocks * sv.f_frsize;
            unsigned long free_d = sv.f_bavail * sv.f_frsize;
            l->stats.disk = (int)(100 * (total_d - free_d) / total_d);
        }

        /* Check if changed */
        if (l->stats.cpu != old_cpu || l->stats.mem != old_mem ||
            l->stats.temp != old_temp || l->stats.disk != old_disk) {
            l->stats.changed = 1;
        }

        sleep(2);
    }
    return NULL;
}

/* ============ Background Loading ============ */

static SDL_Texture *load_background(Launcher *l) {
    const char *paths[] = {
        "/home/aleksa/wallpapers/1.png",
        "/home/aleksa/Aleksa/Projects/TvStreamer/wallpapers/1.png",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        SDL_Surface *surf = IMG_Load(paths[i]);
        if (surf) {
            /* Scale to screen size */
            SDL_Surface *scaled = SDL_CreateRGBSurfaceWithFormat(
                0, l->width, l->height, 32, SDL_PIXELFORMAT_RGBA8888);
            if (scaled) {
                SDL_BlitScaled(surf, NULL, scaled, NULL);
                SDL_FreeSurface(surf);
                SDL_Texture *tex = SDL_CreateTextureFromSurface(l->renderer, scaled);
                SDL_FreeSurface(scaled);
                return tex;
            }
            SDL_FreeSurface(surf);
        }
    }

    /* Fallback: gradient */
    SDL_Texture *tex = SDL_CreateTexture(l->renderer, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, l->width, l->height);
    SDL_SetRenderTarget(l->renderer, tex);

    for (int y = 0; y < l->height; y++) {
        float ratio = (float)y / l->height;
        Uint8 r = 17 + (int)((26 - 17) * ratio);
        Uint8 g = 20 + (int)((30 - 20) * ratio);
        Uint8 b = 34 + (int)((51 - 34) * ratio);
        SDL_SetRenderDrawColor(l->renderer, r, g, b, 255);
        SDL_RenderDrawLine(l->renderer, 0, y, l->width, y);
    }

    SDL_SetRenderTarget(l->renderer, NULL);
    return tex;
}

/* ============ Font Loading ============ */

static TTF_Font *load_font(const char *path, int size) {
    /* Try user-provided path first */
    if (path) {
        TTF_Font *f = TTF_OpenFont(path, size);
        if (f) return f;
    }

    /* Try default paths - covers Arch, Debian, Ubuntu, Fedora */
    const char *defaults[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
        NULL
    };

    for (int i = 0; defaults[i]; i++) {
        TTF_Font *f = TTF_OpenFont(defaults[i], size);
        if (f) return f;
    }
    return NULL;
}

static TTF_Font *load_nerd_font(int size) {
    const char *paths[] = {
        "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
        "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Regular.ttf",
        "/usr/share/fonts/TTF/JetBrainsMonoNLNerdFont-Regular.ttf",
        "/usr/share/fonts/TTF/JetBrainsMonoNLNerdFontMono-Regular.ttf",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        TTF_Font *f = TTF_OpenFont(paths[i], size);
        if (f) return f;
    }
    return NULL;
}

/* ============ Cache Creation ============ */

static void cache_surfaces(Launcher *l) {
    SDL_Color fg = make_color(COL_FG);
    SDL_Color fg_dim = make_color(COL_FG_DIM);

    /* Tile backgrounds */
    l->tile_bg_normal = create_rounded_rect_texture(l, TILE_WIDTH, TILE_HEIGHT, TILE_RADIUS,
                                                     COL_BG_TILE);
    l->tile_bg_selected = create_rounded_rect_texture(l, TILE_WIDTH, TILE_HEIGHT, TILE_RADIUS,
                                                       COL_BG_TILE_SEL);

    /* Settings backgrounds */
    l->settings_bg_normal = create_rounded_rect_texture(l, 50, 50, 25, 0x1A, 0x1E, 0x33, 0x96);
    l->settings_bg_selected = create_rounded_rect_texture(l, 56, 56, 28, 0x2D, 0x34, 0x50, 0xC8);

    /* Stats bar background - more visible */
    l->stats_bar_w = 600;
    l->stats_bar_h = 100;
    l->stats_bar_bg = create_rounded_rect_texture(l, l->stats_bar_w, l->stats_bar_h, 16,
                                                   0x1A, 0x1E, 0x33, 0xD8);

    /* Tile labels and icons */
    for (int i = 0; i < NUM_APPS; i++) {
        l->tile_labels[i] = render_text(l, l->font_tile, apps[i].name, fg);
        l->tile_icons[i] = render_text(l, l->font_icon, apps[i].icon, fg);
        l->tile_icons_dim[i] = render_text(l, l->font_icon, apps[i].icon, fg_dim);
    }

    /* Settings icon */
    l->settings_icon = render_text(l, l->font_icon, ICON_SETTINGS, fg);
    l->settings_icon_dim = render_text(l, l->font_icon, ICON_SETTINGS, fg_dim);

    /* Stat labels */
    const char *stat_names[] = {"CPU", "RAM", "TEMP", "DISK"};
    const char *stat_icons[] = {ICON_CPU, ICON_MEMORY, ICON_TEMP, ICON_DISK};
    for (int i = 0; i < 4; i++) {
        l->stat_labels[i] = render_text(l, l->font_stat_label, stat_names[i], fg_dim);
        l->stat_icons[i] = render_text(l, l->font_icon_small, stat_icons[i], fg);
    }

    /* Help text */
    l->help_text = render_text(l, l->font_tile, "?", fg_dim);
}

/* ============ Layout Calculation ============ */

static void calc_layout(Launcher *l) {
    int total_w = NUM_APPS * TILE_WIDTH + (NUM_APPS - 1) * TILE_SPACING;
    int grid_x = (l->width - total_w) / 2;
    int grid_y = (int)(l->height * 0.48);

    for (int i = 0; i < NUM_APPS; i++) {
        l->tile_rects[i].x = grid_x + i * (TILE_WIDTH + TILE_SPACING);
        l->tile_rects[i].y = grid_y;
        l->tile_rects[i].w = TILE_WIDTH;
        l->tile_rects[i].h = TILE_HEIGHT;
    }

    l->stats_bar_x = (l->width - l->stats_bar_w) / 2;
    l->stats_bar_y = l->height - l->stats_bar_h - 65;
}

/* ============ Drawing ============ */

static SDL_Color get_stat_color(int value, int is_temp) {
    if (is_temp) {
        if (value >= 70) return make_color(COL_RED);
        if (value >= 55) return make_color(COL_ORANGE);
        if (value >= 45) return make_color(COL_YELLOW);
        return make_color(COL_GREEN);
    } else {
        if (value >= 80) return make_color(COL_RED);
        if (value >= 60) return make_color(COL_YELLOW);
        return make_color(COL_GREEN);
    }
}

static void draw(Launcher *l) {
    /* Background */
    SDL_RenderCopy(l->renderer, l->background, NULL, NULL);

    /* Clock */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[64];

    snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
    SDL_Color fg = make_color(COL_FG);
    SDL_Texture *clock_tex = render_text(l, l->font_clock, buf, fg);
    int clock_y = (int)(l->height * 0.12);
    blit_texture_centered(l, clock_tex, l->width / 2, clock_y + 90);
    SDL_DestroyTexture(clock_tex);

    /* Date */
    strftime(buf, sizeof(buf), "%A, %B %d", t);
    SDL_Texture *date_tex = render_text(l, l->font_date, buf, fg);
    blit_texture_centered(l, date_tex, l->width / 2, clock_y + 200);
    SDL_DestroyTexture(date_tex);

    /* Settings icon */
    int settings_x = l->width - 60;
    int settings_y = 50;
    if (l->settings_selected) {
        SDL_Rect dst = {settings_x - 28, settings_y - 28, 56, 56};
        SDL_RenderCopy(l->renderer, l->settings_bg_selected, NULL, &dst);
        /* Pink circle border */
        SDL_SetRenderDrawColor(l->renderer, COL_PINK);
        for (int r = 26; r <= 28; r++) {
            for (int a = 0; a < 360; a++) {
                int px = settings_x + (int)(r * cos(a * 3.14159 / 180));
                int py = settings_y + (int)(r * sin(a * 3.14159 / 180));
                SDL_RenderDrawPoint(l->renderer, px, py);
            }
        }
        blit_texture_centered(l, l->settings_icon, settings_x, settings_y);
    } else {
        SDL_Rect dst = {settings_x - 25, settings_y - 25, 50, 50};
        SDL_RenderCopy(l->renderer, l->settings_bg_normal, NULL, &dst);
        blit_texture_centered(l, l->settings_icon_dim, settings_x, settings_y);
    }

    /* Tiles */
    for (int i = 0; i < NUM_APPS; i++) {
        SDL_Rect *r = &l->tile_rects[i];
        int is_sel = (i == l->selected) && !l->settings_selected;

        /* Background */
        SDL_RenderCopy(l->renderer, is_sel ? l->tile_bg_selected : l->tile_bg_normal, NULL, r);

        /* Border */
        if (is_sel) {
            SDL_SetRenderDrawColor(l->renderer, COL_PINK);
            for (int b = 0; b < 3; b++) {
                SDL_Rect br = {r->x - b, r->y - b, r->w + 2*b, r->h + 2*b};
                SDL_RenderDrawRect(l->renderer, &br);
            }
        } else {
            SDL_SetRenderDrawColor(l->renderer, 0x42, 0x47, 0x61, 0x50);
            SDL_RenderDrawRect(l->renderer, r);
        }

        /* Icon */
        int icon_y = r->y + r->h / 2 - 15;
        blit_texture_centered(l, is_sel ? l->tile_icons[i] : l->tile_icons_dim[i],
                             r->x + r->w / 2, icon_y);

        /* Label */
        blit_texture_centered(l, l->tile_labels[i], r->x + r->w / 2, r->y + r->h - 25);
    }

    /* Stats bar */
    SDL_Rect stats_dst = {l->stats_bar_x, l->stats_bar_y, l->stats_bar_w, l->stats_bar_h};
    SDL_RenderCopy(l->renderer, l->stats_bar_bg, NULL, &stats_dst);

    int stat_values[] = {l->stats.cpu, l->stats.mem, l->stats.temp, l->stats.disk};
    const char *stat_units[] = {"%", "%", "°C", "%"};
    int stat_w = l->stats_bar_w / 4;

    for (int i = 0; i < 4; i++) {
        int x = l->stats_bar_x + i * stat_w + stat_w / 2;
        SDL_Color col = get_stat_color(stat_values[i], i == 2);

        /* Label at top */
        blit_texture_centered(l, l->stat_labels[i], x, l->stats_bar_y + 15);

        /* Value in middle */
        snprintf(buf, sizeof(buf), "%d%s", stat_values[i], stat_units[i]);
        SDL_Texture *val_tex = render_text(l, l->font_stat_value, buf, col);
        blit_texture_centered(l, val_tex, x, l->stats_bar_y + 48);
        SDL_DestroyTexture(val_tex);

        /* Icon at bottom */
        SDL_Texture *icon_tex = render_text(l, l->font_icon_small,
            (const char*[4]){ICON_CPU, ICON_MEMORY, ICON_TEMP, ICON_DISK}[i], col);
        blit_texture_centered(l, icon_tex, x, l->stats_bar_y + 80);
        SDL_DestroyTexture(icon_tex);
    }

    /* Help icon in bottom-right */
    blit_texture_centered(l, l->help_text, l->width - 35, l->height - 50);

    SDL_RenderPresent(l->renderer);
}

/* ============ App Launch ============ */

/* Track launched app PID */
static pid_t launched_app_pid = 0;

static void launch_app(const char *command) {
    pid_t pid = fork();
    if (pid == 0) {
        /* Child process */
        setsid();
        execl("/bin/sh", "sh", "-c", command, NULL);
        exit(1);
    }
    /* Parent: store PID and don't wait - let app run in foreground */
    if (pid > 0) {
        launched_app_pid = pid;
    }
}

/* Check if launched app is still running */
static int is_app_running(void) {
    if (launched_app_pid <= 0) return 0;

    int status;
    pid_t result = waitpid(launched_app_pid, &status, WNOHANG);
    if (result == launched_app_pid) {
        /* App has exited */
        launched_app_pid = 0;
        return 0;
    } else if (result == 0) {
        /* App still running */
        return 1;
    }
    /* Error - assume not running */
    launched_app_pid = 0;
    return 0;
}

/* ============ Confirmation Dialog ============ */

static int show_confirm(Launcher *l, const char *action) {
    /* Darken background */
    SDL_SetRenderDrawBlendMode(l->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(l->renderer, 0, 0, 0, 180);
    SDL_Rect full = {0, 0, l->width, l->height};
    SDL_RenderFillRect(l->renderer, &full);

    /* Dialog */
    int dw = 400, dh = 180;
    int dx = (l->width - dw) / 2;
    int dy = (l->height - dh) / 2;

    SDL_Rect dialog = {dx, dy, dw, dh};
    draw_rounded_rect(l->renderer, &dialog, 20, 0x1A, 0x1E, 0x33, 0xF0);

    /* Border */
    SDL_SetRenderDrawColor(l->renderer, COL_ACCENT);
    for (int b = 0; b < 2; b++) {
        SDL_Rect br = {dx - b, dy - b, dw + 2*b, dh + 2*b};
        SDL_RenderDrawRect(l->renderer, &br);
    }

    /* Title */
    char buf[64];
    snprintf(buf, sizeof(buf), "%s?", action);
    SDL_Color fg = make_color(COL_FG);
    SDL_Texture *title = render_text(l, l->font_date, buf, fg);
    blit_texture_centered(l, title, l->width / 2, dy + 60);
    SDL_DestroyTexture(title);

    /* Hint */
    SDL_Color dim = make_color(COL_FG_DIM);
    SDL_Texture *hint = render_text(l, l->font_tile, "Enter = Yes    Esc = No", dim);
    blit_texture_centered(l, hint, l->width / 2, dy + 130);
    SDL_DestroyTexture(hint);

    SDL_RenderPresent(l->renderer);

    /* Wait for input */
    SDL_Event e;
    while (1) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    return 1;
                } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                    return 0;
                }
            }
        }
        SDL_Delay(50);
    }
}

/* ============ Event Handling ============ */

static int handle_events(Launcher *l) {
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            return 0;
        } else if (e.type == SDL_KEYDOWN) {
            l->needs_redraw = 1;

            switch (e.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    return 0;

                case SDLK_LEFT:
                    if (l->settings_selected) {
                        l->settings_selected = 0;
                    } else {
                        l->selected = (l->selected - 1 + NUM_APPS) % NUM_APPS;
                    }
                    break;

                case SDLK_RIGHT:
                    if (l->settings_selected) {
                        l->settings_selected = 0;
                        l->selected = 0;
                    } else {
                        l->selected = (l->selected + 1) % NUM_APPS;
                    }
                    break;

                case SDLK_UP:
                    if (!l->settings_selected) {
                        l->settings_selected = 1;
                    }
                    break;

                case SDLK_DOWN:
                    if (l->settings_selected) {
                        l->settings_selected = 0;
                    }
                    break;

                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (l->settings_selected) {
                        launch_app("gnome-control-center");
                    } else {
                        launch_app(apps[l->selected].command);
                    }
                    /* Hide launcher and mark app as running */
                    l->app_running = 1;
                    SDL_HideWindow(l->window);
                    break;

                case SDLK_r:
                    if (show_confirm(l, "Reboot")) {
                        system("sudo reboot");
                    }
                    l->needs_redraw = 1;
                    break;

                case SDLK_p:
                    if (show_confirm(l, "Power Off")) {
                        system("sudo poweroff");
                    }
                    l->needs_redraw = 1;
                    break;
            }
        }
    }
    return 1;
}

/* ============ Initialization ============ */

static Launcher *launcher_create(void) {
    Launcher *l = calloc(1, sizeof(Launcher));
    if (!l) return NULL;

    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        free(l);
        return NULL;
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        free(l);
        return NULL;
    }

    if (IMG_Init(IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        TTF_Quit();
        SDL_Quit();
        free(l);
        return NULL;
    }

    /* Get display size */
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        l->width = dm.w;
        l->height = dm.h;
    } else {
        l->width = 1920;
        l->height = 1080;
    }
    /* Create window - use borderless fullscreen for better compositor compatibility */
    l->window = SDL_CreateWindow("TvStreamer",
                                  0, 0,
                                  l->width, l->height,
                                  SDL_WINDOW_BORDERLESS | SDL_WINDOW_SHOWN);

    /* Get actual window size after creation */
    int actual_w, actual_h;
    SDL_GetWindowSize(l->window, &actual_w, &actual_h);
    l->width = actual_w;
    l->height = actual_h;
    if (!l->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        launcher_destroy(l);
        return NULL;
    }

    /* Create renderer with VSync */
    l->renderer = SDL_CreateRenderer(l->window, -1,
                                      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!l->renderer) {
        /* Try software renderer as fallback */
        l->renderer = SDL_CreateRenderer(l->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!l->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        launcher_destroy(l);
        return NULL;
    }

    SDL_SetRenderDrawBlendMode(l->renderer, SDL_BLENDMODE_BLEND);
    SDL_ShowCursor(SDL_DISABLE);

    /* Load fonts */
    l->font_clock = load_font(NULL, 180);
    l->font_date = load_font(NULL, 42);
    l->font_tile = load_font(NULL, 22);
    l->font_stat_value = load_font(NULL, 36);
    l->font_stat_label = load_font(NULL, 16);
    l->font_icon = load_nerd_font(42);
    l->font_icon_small = load_nerd_font(22);

    if (!l->font_clock) {
        fprintf(stderr, "Failed to load clock font\n");
        launcher_destroy(l);
        return NULL;
    }
    if (!l->font_tile) {
        fprintf(stderr, "Failed to load tile font\n");
        launcher_destroy(l);
        return NULL;
    }
    if (!l->font_icon) {
        fprintf(stderr, "Warning: Failed to load Nerd Font for icons, using fallback\n");
        /* Use tile font as fallback for icons */
        l->font_icon = l->font_tile;
        l->font_icon_small = l->font_stat_label;
    }

    /* Load background */
    l->background = load_background(l);

    /* Calculate layout */
    calc_layout(l);

    /* Cache surfaces */
    cache_surfaces(l);

    /* Initialize state */
    l->selected = 0;
    l->settings_selected = 0;
    l->needs_redraw = 1;
    l->last_minute = -1;

    /* Start stats thread */
    l->stats.running = 1;
    pthread_create(&l->stats_thread, NULL, stats_thread_func, l);

    return l;
}

static void launcher_destroy(Launcher *l) {
    if (!l) return;

    /* Stop stats thread */
    l->stats.running = 0;
    pthread_join(l->stats_thread, NULL);

    /* Free textures */
    if (l->background) SDL_DestroyTexture(l->background);
    if (l->tile_bg_normal) SDL_DestroyTexture(l->tile_bg_normal);
    if (l->tile_bg_selected) SDL_DestroyTexture(l->tile_bg_selected);
    if (l->stats_bar_bg) SDL_DestroyTexture(l->stats_bar_bg);
    if (l->settings_bg_normal) SDL_DestroyTexture(l->settings_bg_normal);
    if (l->settings_bg_selected) SDL_DestroyTexture(l->settings_bg_selected);

    for (int i = 0; i < NUM_APPS; i++) {
        if (l->tile_labels[i]) SDL_DestroyTexture(l->tile_labels[i]);
        if (l->tile_icons[i]) SDL_DestroyTexture(l->tile_icons[i]);
        if (l->tile_icons_dim[i]) SDL_DestroyTexture(l->tile_icons_dim[i]);
    }

    for (int i = 0; i < 4; i++) {
        if (l->stat_labels[i]) SDL_DestroyTexture(l->stat_labels[i]);
        if (l->stat_icons[i]) SDL_DestroyTexture(l->stat_icons[i]);
    }

    if (l->settings_icon) SDL_DestroyTexture(l->settings_icon);
    if (l->settings_icon_dim) SDL_DestroyTexture(l->settings_icon_dim);
    if (l->help_text) SDL_DestroyTexture(l->help_text);

    /* Free fonts */
    if (l->font_clock) TTF_CloseFont(l->font_clock);
    if (l->font_date) TTF_CloseFont(l->font_date);
    if (l->font_tile) TTF_CloseFont(l->font_tile);
    if (l->font_stat_value) TTF_CloseFont(l->font_stat_value);
    if (l->font_stat_label) TTF_CloseFont(l->font_stat_label);
    if (l->font_icon) TTF_CloseFont(l->font_icon);
    if (l->font_icon_small) TTF_CloseFont(l->font_icon_small);

    if (l->renderer) SDL_DestroyRenderer(l->renderer);
    if (l->window) SDL_DestroyWindow(l->window);

    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    free(l);
}

/* ============ Main Loop ============ */

static void run(Launcher *l) {
    /* Initial draw */
    draw(l);
    l->needs_redraw = 0;

    while (1) {
        /* Handle events */
        if (!handle_events(l)) break;

        /* Check if launched app is still running */
        if (l->app_running) {
            if (!is_app_running()) {
                /* App closed - show launcher again */
                l->app_running = 0;
                SDL_ShowWindow(l->window);
                SDL_RaiseWindow(l->window);
                l->needs_redraw = 1;
            } else {
                /* App still running - sleep longer to save CPU */
                SDL_Delay(200);
                continue;
            }
        }

        /* Check clock minute change */
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        int current_minute = t->tm_hour * 60 + t->tm_min;
        if (current_minute != l->last_minute) {
            l->last_minute = current_minute;
            l->needs_redraw = 1;
        }

        /* Check stats change */
        if (l->stats.changed) {
            l->stats.changed = 0;
            l->needs_redraw = 1;
        }

        /* Redraw if needed */
        if (l->needs_redraw) {
            draw(l);
            l->needs_redraw = 0;
        }

        /* Sleep to save CPU - event driven, max 20 FPS polling */
        SDL_Delay(50);
    }
}

/* ============ Entry Point ============ */

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
    /* Handle signals */
    signal(SIGCHLD, SIG_IGN);

    Launcher *l = launcher_create();
    if (!l) {
        fprintf(stderr, "Failed to create launcher\n");
        return 1;
    }

    g_launcher = l;
    run(l);
    launcher_destroy(l);

    return 0;
}
