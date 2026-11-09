// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "display.h"
#include "status-bar.h"
#include "indicators/battery.h"
#include "indicators/wifi.h"
#include "indicators/clock.h"
#include "indicators/inspector.h"
#include "font.h"
#include "home.h"
#include "home-view.h"
#include "runtime-home.h"
#include "asset-platform.h"

#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void mp1_dev_status(char *status, size_t size);

/* UI tuning is opt-in independently of Wi-Fi/SSH development services. */
#ifndef MP_UI_DEBUG
#define MP_UI_DEBUG 0
#endif

enum page { HOME, LIBRARY, DISPLAY, POWER, GAME, SEARCH, FAVORITES, COLLECTIONS, DISCOVERY, SETTINGS };
static enum page game_parent = HOME;
static unsigned settings_selected;
static enum page destination(enum mp_home_action action)
{
    switch(action) {
    case MP_HOME_OPEN_GAME: game_parent=HOME; return GAME;
    case MP_HOME_OPEN_LIBRARY: return LIBRARY;
    case MP_HOME_OPEN_DISPLAY: return DISPLAY;
    case MP_HOME_OPEN_POWER: return POWER;
    case MP_HOME_OPEN_SEARCH: return SEARCH;
    case MP_HOME_OPEN_FAVORITES: return FAVORITES;
    case MP_HOME_OPEN_COLLECTIONS: return COLLECTIONS;
    case MP_HOME_OPEN_DISCOVERY: return DISCOVERY;
    case MP_HOME_OPEN_SETTINGS: settings_selected=0; return SETTINGS;
    default: return HOME;
    }
}
static struct mp_home_state home_view;
struct home_overlay { struct mp_status_bar *bar; struct mp_status_bar_cache *cache; int painted; };
static void paint_home_status(const struct mp_paint *paint,void *context)
{
    struct home_overlay *overlay=context;
    overlay->painted=mp_status_bar_paint(overlay->bar,&paint->surface,overlay->cache);
}

static unsigned font_mask(const struct mp_home_state *home,const struct mp_battery_indicator *battery,const struct mp_clock_indicator *clock)
{
 unsigned mask=mp_home_debug_visible(home,&mp_home_layout_default,MP1_WIDTH,MP1_HEIGHT);
 if(battery->ready)mask|=1u<<MP_HOME_FONT_BATTERY;
 if(clock->ready)mask|=1u<<MP_HOME_FONT_CLOCK;
 return mask;
}
static void report_fonts(const struct mp_home_state *home,unsigned visible)
{
 FILE *f=fopen("/run/minpocket-font-debug.tmp","w");if(!f)return;
 fprintf(f,"{\"version\":2,\"active\":\"%s\",\"modifiers\":%u,\"wrap_px\":%u,\"description_wrap_px\":%u,\"items\":{",mp_home_item_key(home->debug_target),home->debug_modifiers,mp_home_title_wrap_width(&mp_home_layout_default,MP1_WIDTH),mp_home_description_wrap_width(&mp_home_layout_default,MP1_WIDTH));
 for(unsigned i=0;i<MP_HOME_ITEM_COUNT;i++)fprintf(f,"%s\"%s\":{\"label\":\"%s\",\"has_font\":%s,\"px\":%u,\"margin_top_px\":%u,\"margin_bottom_px\":%u,\"visible\":%s}",i?",":"",mp_home_item_key((enum mp_home_item_role)i),mp_home_item_name((enum mp_home_item_role)i),mp_home_item_has_font((enum mp_home_item_role)i)?"true":"false",home->font_height[i],home->margin_top[i],home->margin_bottom[i],visible&(1u<<i)?"true":"false");
 fputs("}}\n",f);if(!fclose(f))(void)rename("/run/minpocket-font-debug.tmp","/run/minpocket-font-debug");
}
static const char *const menu[] = { "GAMES", "DISPLAY", "POWER" };
static const char brightness_path[] = "/sys/class/backlight/backlight/brightness";
static const char max_brightness_path[] = "/sys/class/backlight/backlight/max_brightness";
static int show_screen_edges;
static int show_status_edges;
static void status_outline(const struct mp_font_surface *surface, struct mp_ui_rect box)
{
    if (!box.width || !box.height) return;
    /* Prefer one diagnostic pixel outside the allocation. At a screen edge,
     * clamp onto the outermost display pixel so every side remains visible.
     * This runs only after the confirmed boot frames. */
    uint32_t color = mp_theme_default.colors.boundary;
    int left = box.x - 1, top = box.y - 1;
    int right = box.x + (int)box.width, bottom = box.y + (int)box.height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right >= (int)surface->width) right = (int)surface->width - 1;
    if (bottom >= (int)surface->height) bottom = (int)surface->height - 1;
    if (right < left || bottom < top) return;
    unsigned width = (unsigned)(right - left + 1), height = (unsigned)(bottom - top + 1);
    mp_ui_fill(surface, (struct mp_ui_rect){left, top, width, 1}, color);
    mp_ui_fill(surface, (struct mp_ui_rect){left, bottom, width, 1}, color);
    mp_ui_fill(surface, (struct mp_ui_rect){left, top, 1, height}, color);
    mp_ui_fill(surface, (struct mp_ui_rect){right, top, 1, height}, color);
}
static void fill(struct mp1_frame *d, unsigned x, unsigned y, unsigned w, unsigned h,
                 uint32_t color)
{
    /* The panel scans portrait rows in reverse logical X order. */
    for (unsigned i = x; i < x + w; ++i) {
        uint32_t *row = (uint32_t *)((char *)d->pixels + (MP1_WIDTH - 1 - i) * d->pitch);
        for (unsigned j = y; j < y + h; ++j)
            row[j] = color;
    }
}

/* All production text uses the embedded Monogram bitmap. Scale is integral;
 * Y remains the capital-top anchor so the existing layout stays stable. */
static void text(struct mp1_frame *d, unsigned x, unsigned y, unsigned scale,
                 const char *s, uint32_t color)
{
    struct mp_font_surface surface = {
        .pixels = (uint32_t *)((char *)d->pixels + (MP1_WIDTH - 1) * d->pitch),
        .width = MP1_WIDTH, .height = MP1_HEIGHT,
        .x_stride = -(ptrdiff_t)(d->pitch / sizeof(uint32_t)), .y_stride = 1
    };
    mp_font_draw(&surface, (int)x, (int)y, scale, s, color);
}

static int read_level(const char *path)
{
    int value = -1;
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d", &value) != 1)
            value = -1;
        fclose(f);
    }
    return value;
}

static void adjust_brightness(int direction)
{
    int level = read_level(brightness_path);
    int max = read_level(max_brightness_path);
    if (level < 0 || max < 1 || level + direction < 1 || level + direction > max)
        return;
    int fd = open(brightness_path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        dprintf(fd, "%d\n", level + direction);
        close(fd);
    }
}

static void draw(struct mp1_frame *d, const struct mp1_display *display,
                 enum page page, unsigned selected, int confirming, const char *dev_status)
{
    const struct mp_theme *theme = &mp_theme_default;
    const uint32_t ink = theme->colors.text, muted = theme->colors.muted, mint = theme->colors.accent;
    if (page >= SEARCH) {
        struct mp_font_surface surface = {
            .pixels=(uint32_t *)((char *)d->pixels+(MP1_WIDTH-1)*d->pitch),
            .width=MP1_WIDTH,.height=MP1_HEIGHT,
            .x_stride=-(ptrdiff_t)(d->pitch/sizeof(uint32_t)),.y_stride=1
        };
        const char *titles[]={"Search","Favorites","Collections","Discovery","Settings"};
        mp_ui_fill(&surface,(struct mp_ui_rect){0,0,MP1_WIDTH,MP1_HEIGHT},theme->status.box.background);
        mp_font_draw_px(&surface,60,100,36,titles[page-SEARCH],theme->colors.status_text);
        if(page==SETTINGS) {
            const char *items[]={"Display","Power"};
            for(unsigned i=0;i<2;i++) {
                int y=210+(int)i*74;
                if(settings_selected==i)mp_ui_fill(&surface,(struct mp_ui_rect){50,y-12,420,56},theme->colors.status_text);
                mp_font_draw_px(&surface,70,y,21,items[i],settings_selected==i?theme->status.box.frame.recess:theme->colors.status_text);
            }
        } else {
            mp_font_draw_px(&surface,60,205,21,"This feature is not available yet.",theme->colors.muted);
        }
        return;
    }
    if (page == HOME || page == LIBRARY || page == GAME) {
        struct mp_font_surface surface = {
            .pixels = (uint32_t *)((char *)d->pixels + (MP1_WIDTH - 1) * d->pitch),
            .width = MP1_WIDTH, .height = MP1_HEIGHT,
            .x_stride = -(ptrdiff_t)(d->pitch / sizeof(uint32_t)), .y_stride = 1
        };
        if(page==HOME)mp_home_view_draw(&home_view,theme,&mp_home_layout_default,&surface,NULL,NULL);
        else if(page==GAME&&game_parent==HOME){
            const struct mp_home_card *card=&home_view.rows[home_view.row].cards[home_view.index[home_view.row]];
            mp_home_game_draw(&surface,theme,card->title,card->metadata,card->description,card->art);
        } else mp_home_draw(&surface,theme,page==LIBRARY?MP_HOME_LIBRARY:MP_HOME_GAME,selected);
        if (show_screen_edges) {
            fill(d, 0, 0, MP1_WIDTH, 1, theme->colors.boundary);
            fill(d, 0, MP1_HEIGHT - 1, MP1_WIDTH, 1, theme->colors.boundary);
            fill(d, 0, 0, 1, MP1_HEIGHT, theme->colors.boundary);
            fill(d, MP1_WIDTH - 1, 0, 1, MP1_HEIGHT, theme->colors.boundary);
        }
        return;
    }
    fill(d, 0, 0, MP1_WIDTH, MP1_HEIGHT, theme->colors.surface);
    if (show_screen_edges) {
        /* Diagnostic only: the exact outermost pixel, with no inset or crop. */
        const uint32_t edge = theme->colors.boundary;
        fill(d, 0, 0, MP1_WIDTH, 1, edge);
        fill(d, 0, MP1_HEIGHT - 1, MP1_WIDTH, 1, edge);
        fill(d, 0, 1, 1, MP1_HEIGHT - 2, edge);
        fill(d, MP1_WIDTH - 1, 1, 1, MP1_HEIGHT - 2, edge);
    }
    fill(d, 60, 62, 42, 8, mint);
    text(d, 60, 95, theme->font_scale.title, "MINPOCKET", ink);
    text(d, 62, 184, theme->font_scale.hint, "POCKET ONE", muted);

    text(d, 62, 271, theme->font_scale.page, menu[page - LIBRARY], ink);
    if (page == DISPLAY) {
        int max = read_level(max_brightness_path);
        int level = read_level(brightness_path);
        text(d, 62, 377, theme->font_scale.body, "BRIGHTNESS", muted);
        fill(d, 62, 436, 720, 22, theme->colors.panel);
        if (max > 0 && level > 0 && level <= max)
            fill(d, 62, 436, 720 * (unsigned)level / (unsigned)max, 22, mint);
        text(d, 62, 478, theme->font_scale.hint, "LEFT AND RIGHT TO ADJUST", muted);
        text(d, 62, 545, theme->font_scale.body, display->mode.clock == 99000 ? "120 HZ" : "60 HZ", ink);
        if (display->mode_120.clock)
            text(d, 310, 552, theme->font_scale.hint, confirming ? "A KEEP 120 HZ" : "A SWITCH REFRESH RATE", mint);
        text(d, 62, 599, theme->font_scale.hint, dev_status, muted);
    } else {
        text(d, 62, 387, theme->font_scale.body, "SHUT DOWN", muted);
    }
}

static time_t seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec;
}

static void update_clock(struct mp_clock_indicator *indicator, uint64_t now_ms)
{
    struct timespec wall;
    struct tm local;
    if (clock_gettime(CLOCK_REALTIME, &wall) || wall.tv_sec < 1577836800 || !localtime_r(&wall.tv_sec, &local)) {
        mp_clock_indicator_update(indicator, -1, -1, 0, now_ms);
        return;
    }
    unsigned next = 60000 - (unsigned)(wall.tv_sec % 60) * 1000 - (unsigned)(wall.tv_nsec / 1000000);
    mp_clock_indicator_update(indicator, local.tm_hour, local.tm_min, next, now_ms);
}

static int open_input(const char *wanted)
{
    for (unsigned i = 0; i < 32; ++i) {
        char path[32], name[64] = {0};
        snprintf(path, sizeof path, "/dev/input/event%u", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0 &&
            (wanted ? strcmp(name, wanted) == 0 : strstr(name, "pwrkey") != NULL))
            return fd;
        close(fd);
    }
    return -1;
}

int main(void)
{
    struct mp1_boot_profile profile = { .entry = mp1_boot_ns() };
    mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, NULL);
    profile.devtmpfs = mp1_boot_ns();

    mp_home_view_init(&home_view);
    struct mp_runtime *runtime=mp_runtime_boot(&home_view);
    profile.home_ready=mp1_boot_ns();
    struct mp1_display display;
    for (;;) {
        profile.drm_attempts++;
        if (!mp1_display_open(&display))
            break;
        usleep(20000);
    }
    profile.drm_ready = mp1_boot_ns();
    struct pollfd inputs[] = {
        {.fd = -1}, {.fd = -1}, {.fd = display.fd, .events = POLLIN},
        {.fd = -1, .events = POLLIN}, {.fd = -1, .events = POLLIN}, {.fd = -1, .events = POLLIN}
    };
    enum page page = HOME;
    unsigned selected = 0;
    time_t confirm_until = 0;
    draw(&display.front, &display, page, selected, 0, "");
    profile.drawn = mp1_boot_ns();
    for (;;) {
        profile.modeset_attempts++;
        if (!mp1_display_show(&display))
            break;
        usleep(20000);
    }
    profile.modeset = mp1_boot_ns();

    while (mp1_display_confirm_first(&display))
        usleep(20000);
    profile.refresh = mp1_boot_ns();
    profile.first_sequence = display.last_sequence;
    mp1_counter_sample(&profile.first_counter, &profile.frequency);
    /* The first visible frame ends the CPU boost. All other deferred services
     * still wait for the second confirmed refresh. The kernel has a timeout. */
    mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    int boost = open("/proc/minpocket_boot_boost", O_WRONLY | O_CLOEXEC);
    if (boost >= 0) {
        (void)write(boost, "0", 1);
        close(boost);
    }
    profile.boost_released = mp1_boot_ns();
    while (mp1_display_wait_refresh(&display))
        usleep(20000);
    profile.second_refresh = mp1_boot_ns();
    profile.second_sequence = display.last_sequence;
    mp1_counter_sample(&profile.second_counter, &profile.frequency);

    mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    sethostname("minpocket", sizeof "minpocket" - 1);

    int development = !access("/sys/firmware/devicetree/base/chosen/minipocket,development", F_OK);
    int ui_debug = development && MP_UI_DEBUG;
    profile.interfaces = mp1_boot_ns();
    mp_runtime_report(runtime);
    if (development)
        mp1_dev_start(&profile, &display.profile);
    else if (fork() == 0) {
        mp1_launcher_report(&profile, &display.profile);
        if (mp1_start_late_drivers() || mp1_sd_start())
            _exit(1);
        execl("/sbin/minpocket-data", "minpocket-data", (char *)NULL);
        _exit(127);
    }
    char dev_status[64] = "";
    /* Do not read power-supply state, build the cache, fork its monitor, or
     * allocate the second buffer until both boot frames are confirmed. */
    /* Asset workers start below, after the initial monitor forks. */
    static struct mp_status_bar status_bar;
    /* Status layout, artwork caches and animation scheduling are all deferred. */
    struct mp_theme status_theme=mp_theme_default;
    if(ui_debug){
        const char *order[]={"inspector","battery","wifi","clock"};
        status_theme.status.indicator_order_count=4;
        for(unsigned i=0;i<4;i++)status_theme.status.indicator_order[i]=order[i];
        status_theme.status.layout.max_width=MP_STATUS_MAX_WIDTH;
    }
    if (mp_status_bar_init(&status_bar, &status_theme, MP1_WIDTH, MP1_HEIGHT))
        return 1;
    static struct mp_battery_indicator battery_indicator;
    static struct mp_wifi_indicator wifi_indicator;
    static struct mp_clock_indicator clock_indicator;
    static struct mp_inspector_indicator inspector_indicator;
    mp_battery_indicator_init(&battery_indicator);
    mp_wifi_indicator_init(&wifi_indicator);
    mp_clock_indicator_init(&clock_indicator);
    if(development){
        mp_battery_indicator_set_text_height(&battery_indicator,home_view.font_height[MP_HOME_FONT_BATTERY]);
        mp_clock_indicator_set_text_height(&clock_indicator,home_view.font_height[MP_HOME_FONT_CLOCK]);
    }
    tzset(); /* Private timezone file, first opened only after both boot frames. */
    struct mp_status_indicator indicators[] = {
        {"clock", &mp_clock_indicator_ops, &clock_indicator},
        {"battery", &mp_battery_indicator_ops, &battery_indicator},
        {"wifi", &mp_wifi_indicator_ops, &wifi_indicator},
        {"inspector", &mp_inspector_indicator_ops, &inspector_indicator}
    };
    uint64_t registered_ms = mp1_boot_ns() / 1000000;
    for (unsigned i = 0; i < (ui_debug?4u:3u); ++i)
        if (mp_status_bar_register(&status_bar, &indicators[i], i, registered_ms)) return 1;
    inputs[3].fd = mp_battery_monitor_start(display.fd);
    inputs[4].fd = mp_wifi_monitor_start(display.fd, inputs[3].fd);
    (void)mp_home_assets_start(&home_view,"/media/Data/Cache/Assets/v1",mp_asset_data_ready,NULL);
    inputs[5].fd=mp_asset_cache_fd(home_view.assets);
    struct rendered_frame {
        uint32_t fb;
        unsigned scene;
        struct mp_status_bar_cache status;
    } rendered[2] = {{.fb = display.front.fb, .scene = 1}, {0}};
    unsigned scene = 1, submitted_scene = 1;
    uint64_t submitted_status = UINT64_MAX, next_dev_status = 0, next_edge_check = 0;
    uint64_t next_wifi_monitor = 0;
    uint64_t next_clock_sample = 0;
    home_view.font_debug=ui_debug;
    int redraw = ui_debug,font_reported=0;unsigned reported_font_mask=0;
    int render_profile_fd = -1;unsigned render_profile_samples=0;
    for (;;) {
        uint64_t now_ms = mp1_boot_ns() / 1000000;
        if(mp_runtime_poll(runtime,&home_view,now_ms))redraw=1;
        inputs[5].fd=mp_asset_cache_fd(home_view.assets);
        if(page==HOME && mp_home_view_tick(&home_view,&mp_home_layout_default,MP1_WIDTH,MP1_HEIGHT,now_ms,1))redraw=1;
        if(page==HOME)(void)mp_home_view_prepare_cache(&home_view,&mp_theme_default,&mp_home_layout_default);
        if(ui_debug){
            unsigned visible=font_mask(&home_view,&battery_indicator,&clock_indicator);
            enum mp_home_item_role role=home_view.debug_target;
            unsigned structural=(1u<<MP_HOME_FONT_NAV)|(1u<<MP_HOME_FONT_BRAND)|(1u<<MP_HOME_FONT_BADGE)|(1u<<MP_HOME_FONT_BATTERY)|(1u<<MP_HOME_FONT_CLOCK);
            if(page==HOME&&(structural&(1u<<role))&&!(visible&(1u<<role))){mp_home_debug_cycle(&home_view,1,visible);font_reported=0;redraw=1;}
            mp_battery_indicator_set_text_selected(&battery_indicator,page==HOME&&home_view.debug_target==MP_HOME_FONT_BATTERY);
            mp_clock_indicator_set_text_selected(&clock_indicator,page==HOME&&home_view.debug_target==MP_HOME_FONT_CLOCK);
            mp_battery_indicator_set_text_height(&battery_indicator,home_view.font_height[MP_HOME_FONT_BATTERY]);
            mp_clock_indicator_set_text_height(&clock_indicator,home_view.font_height[MP_HOME_FONT_CLOCK]);
            (void)mp_status_bar_set_item_margins(&status_bar,"battery",home_view.margin_top[MP_HOME_FONT_BATTERY],home_view.margin_bottom[MP_HOME_FONT_BATTERY]);
            (void)mp_status_bar_set_item_margins(&status_bar,"clock",home_view.margin_top[MP_HOME_FONT_CLOCK],home_view.margin_bottom[MP_HOME_FONT_CLOCK]);
            mp_inspector_update(&inspector_indicator,mp_home_item_key(home_view.debug_target),mp_home_item_has_font(home_view.debug_target),home_view.font_height[home_view.debug_target],home_view.margin_top[home_view.debug_target],home_view.margin_bottom[home_view.debug_target],home_view.debug_modifiers,page==HOME&&battery_indicator.ready,now_ms);
            if(!font_reported||visible!=reported_font_mask){report_fonts(&home_view,visible);font_reported=1;reported_font_mask=visible;}
        }
        if (now_ms >= next_clock_sample) {
            next_clock_sample = now_ms + 1000;
            if(development){
                struct mp_asset_stats stats;mp_asset_cache_stats(home_view.assets,&stats);
                FILE *report=fopen("/run/minpocket-asset-cache.json.tmp","w");
                if(report){fprintf(report,"{\"bytes\":%zu,\"budget\":%zu,\"ready\":%u,\"pending\":%u,\"hits\":%u,\"misses\":%u,\"disk_hits\":%u,\"generated\":%u,\"rejected\":%u,\"evictions\":%u}\n",stats.bytes,stats.budget,stats.ready,stats.pending,stats.hits,stats.misses,stats.disk_hits,stats.generated,stats.rejected,stats.evictions);fclose(report);rename("/run/minpocket-asset-cache.json.tmp","/run/minpocket-asset-cache.json");}
            }
            update_clock(&clock_indicator, now_ms);
        }
        while (waitpid(-1, NULL, WNOHANG) > 0) {
        }
        if (development && now_ms >= next_edge_check) {
            next_edge_check = now_ms + 250;
            int profile_enabled = !access("/run/minpocket-render-profile-enable", F_OK);
            if(profile_enabled && render_profile_fd<0){
                render_profile_fd=open("/run/minpocket-render-profile",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
                render_profile_samples=0;
            } else if(!profile_enabled && render_profile_fd>=0){close(render_profile_fd);render_profile_fd=-1;}
            int enabled = !access("/run/minpocket-screen-edges", F_OK);
            if (enabled != show_screen_edges) {
                show_screen_edges = enabled;
                redraw = 1;
            }
            enabled = !access("/run/minpocket-status-edges", F_OK);
            if (enabled != show_status_edges) {
                show_status_edges = enabled;
                redraw = 1;
            }
            /* Development-only orientation override, after both boot frames.
             * Missing/invalid input restores the compiled theme setting. */
            int orientation = read_level("/run/minpocket-status-orientation");
            if (orientation != MP_HORIZONTAL && orientation != MP_VERTICAL)
                orientation = mp_theme_default.status.layout.orientation;
            int scale = read_level("/run/minpocket-status-scale");
            if (scale != 1 && scale != 2) scale = mp_theme_default.status.pixel_scale;
            if ((int)status_bar.theme.status.layout.orientation != orientation ||
                (int)status_bar.theme.status.pixel_scale != scale) {
                struct mp_theme theme = status_bar.theme;
                theme.status.layout.orientation = (enum mp_orientation)orientation;
                theme.status.pixel_scale = (unsigned)scale;
                if (!mp_status_bar_set_theme(&status_bar, &theme)) redraw = 1;
            }
        }
        if (inputs[4].fd < 0 && now_ms >= next_wifi_monitor) {
            inputs[4].fd = mp_wifi_monitor_start(display.fd, inputs[3].fd);
            next_wifi_monitor = now_ms + 2000;
        }
        if (confirm_until && seconds() >= confirm_until && !mp1_display_set_refresh(&display, 60)) {
            confirm_until = 0;
            redraw = 1;
        }
        if (development && page == DISPLAY && now_ms >= next_dev_status) {
            next_dev_status = now_ms + 250;
            char status[sizeof dev_status];
            mp1_dev_status(status, sizeof status);
            if (strcmp(status, dev_status)) {
                strcpy(dev_status, status);
                redraw = 1;
            }
        }
        if (redraw) {
            ++scene;
            redraw = 0;
        }
        mp_status_bar_tick(&status_bar, now_ms, 1);
        if (scene != submitted_scene || status_bar.generation != submitted_status) {
            struct mp1_frame *back = mp1_display_back_buffer(&display);
            if (back) {
                struct rendered_frame *mark = rendered[0].fb == back->fb ? &rendered[0] : &rendered[1];
                struct mp_ui_rect status_bounds = mp_status_bar_screen_bounds(&status_bar);
                int moved_outline = show_status_edges && mark->status.valid &&
                    (mark->status.bounds.x != status_bounds.x || mark->status.bounds.y != status_bounds.y ||
                     mark->status.bounds.width != status_bounds.width || mark->status.bounds.height != status_bounds.height);
                struct mp_font_surface surface = {
                    .pixels = (uint32_t *)((char *)back->pixels + (MP1_WIDTH - 1) * back->pitch),
                    .width = MP1_WIDTH, .height = MP1_HEIGHT,
                    .x_stride = -(ptrdiff_t)(back->pitch / sizeof(uint32_t)), .y_stride = 1
                };
                uint64_t paint_started=render_profile_fd>=0?mp1_boot_ns():0;
                int full=mark->fb!=back->fb || mark->scene!=scene || moved_outline;
                int painted=0;
                if(full){
                    mark->fb=back->fb;mark->scene=scene;mark->status.valid=0;
                    if(page==HOME){
                        struct home_overlay overlay={&status_bar,&mark->status,0};
                        mp_home_view_draw(&home_view,&mp_theme_default,&mp_home_layout_default,&surface,paint_home_status,&overlay);
                        painted=overlay.painted;
                    } else draw(back,&display,page,selected,confirm_until!=0,dev_status);
                }
                if(!full || page!=HOME)painted=mp_status_bar_paint(&status_bar,&surface,&mark->status);
                if (painted >= 0 && show_status_edges) status_outline(&surface, status_bounds);
                if (painted >= 0 && !mp1_display_flip(&display)) {
                    if(render_profile_fd>=0 && render_profile_samples++<64)
                        dprintf(render_profile_fd,"render_us=%llu full=%d scene=%u\n",
                            (unsigned long long)((mp1_boot_ns()-paint_started)/1000),full,scene);
                    submitted_scene = scene;
                    submitted_status = status_bar.generation;
                }
            }
        }
        if (inputs[0].fd < 0)
            inputs[0].fd = open_input("MP1 controls");
        if (inputs[1].fd < 0)
            inputs[1].fd = open_input(NULL);
        /* Keep navigation events in the kernel queue until the visible-only model
         * expands after second refresh. A bounded fallback leaves recovery
         * controls available if Data cannot mount. This never delays boot frames. */
        int pending_home = page == HOME && home_view.startup_geometry &&
            now_ms < profile.second_refresh / 1000000 + 2000;
        inputs[0].events = inputs[1].events = display.flip_pending || pending_home ? 0 : POLLIN;
        unsigned timeout = mp_status_bar_wait_ms(&status_bar, mp1_boot_ns() / 1000000);
        if(page==HOME){unsigned home_wait=mp_home_view_wait(&home_view,mp1_boot_ns()/1000000);if(home_wait<timeout)timeout=home_wait;}
        if (timeout > 250) timeout = 250;
        if (poll(inputs, sizeof inputs / sizeof inputs[0], (int)timeout) <= 0)
            continue;

        if(inputs[5].revents & POLLIN){char events[64];while(read(inputs[5].fd,events,sizeof events)>0){}redraw=1;}
        if (inputs[2].revents & POLLIN)
            mp1_display_dispatch(&display);
        if (inputs[3].revents & POLLIN) {
            struct mp_battery_reading reading;
            while (read(inputs[3].fd, &reading, sizeof reading) == sizeof reading)
                mp_battery_indicator_update(&battery_indicator, reading, mp1_boot_ns() / 1000000);
        }
        if (inputs[3].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            close(inputs[3].fd);
            inputs[3].fd = -1;
            mp_battery_indicator_update(&battery_indicator, (struct mp_battery_reading){-1, MP_BATTERY_UNKNOWN},
                              mp1_boot_ns() / 1000000);
        }
        if (inputs[4].revents & POLLIN) {
            struct mp_wifi_reading reading;
            while (read(inputs[4].fd, &reading, sizeof reading) == sizeof reading)
                mp_wifi_indicator_update(&wifi_indicator, reading, mp1_boot_ns() / 1000000);
        }
        if (inputs[4].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            close(inputs[4].fd);
            inputs[4].fd = -1;
            mp_wifi_indicator_update(&wifi_indicator, (struct mp_wifi_reading){.signal_dbm = MP_WIFI_NO_SIGNAL},
                           mp1_boot_ns() / 1000000);
            next_wifi_monitor = mp1_boot_ns() / 1000000 + 2000;
        }
        for (unsigned i = 0; i < 2; ++i) {
            if (inputs[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close(inputs[i].fd);
                inputs[i].fd = -1;
                continue;
            }
            if (!(inputs[i].revents & POLLIN))
                continue;

            struct input_event event;
            while (read(inputs[i].fd, &event, sizeof event) == sizeof event) {
                if(event.type!=EV_KEY)continue;
                if(ui_debug&&(event.code==BTN_TL2||event.code==BTN_TR2)){
                    mp_home_debug_modifier(&home_view,event.code==BTN_TL2?MP_HOME_MOD_TOP:MP_HOME_MOD_BOTTOM,event.value!=0);
                    report_fonts(&home_view,font_mask(&home_view,&battery_indicator,&clock_indicator));continue;
                }
                if(event.value!=1)continue;
                if(ui_debug&&page==HOME&&(event.code==BTN_NORTH||event.code==BTN_WEST||event.code==BTN_TL||event.code==BTN_TR)){
                    unsigned visible=font_mask(&home_view,&battery_indicator,&clock_indicator);
                    if(event.code==BTN_TL||event.code==BTN_TR){if(mp_home_debug_cycle(&home_view,event.code==BTN_TL?-1:1,visible))redraw=1;}
                    else if(mp_home_debug_adjust(&home_view,event.code==BTN_NORTH?1:-1))redraw=1;
                    report_fonts(&home_view,font_mask(&home_view,&battery_indicator,&clock_indicator));
                    continue;
                }
                if(page==HOME){
                    int handled=1;enum mp_home_key key=MP_HOME_BACK;
                    if(event.code==KEY_LEFT)key=MP_HOME_LEFT;
                    else if(event.code==KEY_RIGHT)key=MP_HOME_RIGHT;
                    else if(event.code==KEY_UP)key=MP_HOME_UP;
                    else if(event.code==KEY_DOWN)key=MP_HOME_DOWN;
                    else if(event.code==BTN_SOUTH)key=MP_HOME_BACK;
                    else if(event.code==BTN_EAST)key=MP_HOME_SELECT;
                    else handled=0;
                    if(handled){
                        enum mp_home_action action=mp_home_view_key(&home_view,&mp_home_layout_default,key,mp1_boot_ns()/1000000);
                        page=destination(action);
                        redraw=1;continue;
                    }
                }
                if (event.code == KEY_POWER) {
                    if (confirm_until && !mp1_display_set_refresh(&display, 60))
                        confirm_until = 0;
                    page = POWER;
                } else if (event.code == BTN_SOUTH) {
                    if (confirm_until && !mp1_display_set_refresh(&display, 60))
                        confirm_until = 0;
                    page = page == GAME ? game_parent : (page==DISPLAY||page==POWER)?SETTINGS:HOME;
                } else if (event.code == BTN_EAST) {
                    if (page == HOME) {
                        enum mp_home_action action = mp_home_activate(selected);
                        page=destination(action);
                    } else if (page == SETTINGS) {
                        page=settings_selected?POWER:DISPLAY;
                    } else if (page == LIBRARY) {
                        game_parent = LIBRARY; page = GAME;
                    } else if (page == DISPLAY) {
                        if (confirm_until)
                            confirm_until = 0;
                        else if (display.mode.clock == 99000)
                            mp1_display_set_refresh(&display, 60);
                        else if (!mp1_display_set_refresh(&display, 120))
                            confirm_until = seconds() + 8;
                    } else if (page == POWER) {
                        sync();
                        reboot(RB_POWER_OFF);
                    }
                } else if (page == SETTINGS && (event.code == KEY_UP || event.code == KEY_DOWN)) {
                    settings_selected=event.code==KEY_DOWN?1:0;
                } else if (page == HOME && (event.code == KEY_UP || event.code == KEY_DOWN)) {
                    selected = mp_home_focus_move(selected, 0, event.code == KEY_DOWN ? 1 : -1);
                } else if (page == HOME && (event.code == KEY_LEFT || event.code == KEY_RIGHT)) {
                    selected = mp_home_focus_move(selected, 1, event.code == KEY_RIGHT ? 1 : -1);
                } else if (page == DISPLAY && (event.code == KEY_LEFT || event.code == KEY_RIGHT)) {
                    adjust_brightness(event.code == KEY_RIGHT ? 1 : -1);
                } else {
                    continue;
                }
                redraw = 1;
            }
        }
    }
}
