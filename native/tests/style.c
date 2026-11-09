// SPDX-License-Identifier: GPL-2.0-only
#include "status-bar.h"
#include "status-theme.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static struct mp_theme test_theme;

enum { W = 160, H = 120 };
static uint32_t buffers[3][W * H], clean[W * H];
static struct mp_font_surface surface(unsigned n)
{ return (struct mp_font_surface){buffers[n], W, H, 1, W}; }
static struct mp_box_style plain(void)
{ return (struct mp_box_style){.background=0x204060,.background_alpha=255,.border_alpha=255,.opacity=255,.clip_children=1}; }
static void reset(void)
{
    for (unsigned y=0;y<H;++y) for (unsigned x=0;x<W;++x)
        clean[y*W+x]=((x*13%256)<<16)|((y*11%256)<<8)|((x+y)*17%256);
    for (unsigned n=0;n<3;++n) memcpy(buffers[n],clean,sizeof clean);
}
static void layout(void)
{
    struct mp_layout_style s = test_theme.status.layout;
    s.margin=(struct mp_ui_edges){0};s.padding=(struct mp_ui_edges){2,3,4,5};
    s.anchor_x=MP_ALIGN_CENTER;s.anchor_y=MP_ALIGN_END;
    struct mp_ui_rect b;
    assert(!mp_layout_bounds(&s,40,20,160,120,&b));
    assert(b.x==56 && b.y==94 && b.width==48 && b.height==26);
    struct mp_ui_row_item items[]={{10,8,0},{20,12,4}};struct mp_ui_rect out[2];
    s.justify=MP_JUSTIFY_END;s.align=MP_ALIGN_CENTER;
    mp_layout_arrange(&s,b,items,2,out);
    assert(out[0].x==67 && out[1].x==81 && out[0].y==102 && out[1].y==100);
    s.direction=MP_REVERSE;mp_layout_arrange(&s,b,items,2,out);
    assert(out[1].x==67 && out[0].x==91);
    s.orientation=MP_VERTICAL;s.direction=MP_FORWARD;s.justify=MP_JUSTIFY_START;s.align=MP_ALIGN_END;
    mp_layout_arrange(&s,(struct mp_ui_rect){0,0,60,60},items,2,out);
    assert(out[0].x==47 && out[1].x==37 && out[0].y==2 && out[1].y==14);
    s.justify=MP_SPACE_BETWEEN;mp_layout_arrange(&s,(struct mp_ui_rect){0,0,60,60},items,2,out);
    assert(out[1].y==44);
    s.width=30;assert(mp_layout_bounds(&s,40,20,160,120,&b));
    s.overflow=MP_OVERFLOW_CLIP;assert(!mp_layout_bounds(&s,40,20,160,120,&b) && b.width==30);
    s.padding.right=128;assert(mp_layout_bounds(&s,40,20,160,120,&b));
    struct mp_layout_style content_layout;
    struct mp_box_style border=plain();border.border=(struct mp_ui_edges){10,10,10,10};
    struct mp_layout_style padded=test_theme.status.layout;
    assert(!mp_box_layout(&border,&padded,&content_layout));
    assert(content_layout.padding.top==15 && content_layout.padding.right==15 &&
        content_layout.padding.bottom==15 && content_layout.padding.left==15);
    padded.padding.left=119;assert(mp_box_layout(&border,&padded,&content_layout));
    struct mp_ui_clock clock; struct mp_ui_motion motion={0};
    for (unsigned easing=MP_EASE_OUT_CUBIC;easing<=MP_EASE_IN_OUT_CUBIC;++easing) {
        motion=(struct mp_ui_motion){.easing=easing};mp_ui_clock_begin(&clock,0,1);
        mp_ui_motion_to(&motion,1000,800,&clock);int previous=0;
        for(unsigned t=0;t<=800;++t){clock.now_ms=t;int v=mp_ui_motion_value(&motion,&clock);assert(v>=previous&&v<=1000);previous=v;}
        assert(previous==1000);clock.now_ms=400;
        assert(mp_ui_motion_value(&motion,&clock)==(easing==MP_EASE_OUT_CUBIC?875:500));
    }
}
static void renderer(void)
{
    reset();struct mp_box_cache cache={0};struct mp_box_workspace work={0};
    struct mp_font_surface s=surface(0),reference=surface(1);
    uint8_t art[40*24]={0};uint32_t palette[]={0,0x7dcfff};
    struct mp_ui_index_surface content={art,40,24,40};
    struct mp_ui_rect b={30,30,40,24};struct mp_box_style box=plain();
    box.background=0xff0000;box.background_alpha=128;
    assert(!mp_box_paint(&box,&s,b,&content,palette,2,&cache,&work,0));
    unsigned pixel=35*W+35;uint32_t expected=0;
    for(unsigned shift=0;shift<24;shift+=8)
        expected|=((((clean[pixel]>>shift&255)*127)+((box.background>>shift&255)*128)+127)/255)<<shift;
    assert(buffers[0][pixel]==expected);
    uint32_t once[W*H];memcpy(once,buffers[0],sizeof once);
    for(unsigned i=0;i<20;++i)assert(!mp_box_paint(&box,&s,b,&content,palette,2,&cache,&work,1));
    assert(!memcmp(once,buffers[0],sizeof once)); /* No cumulative alpha. */
    box.opacity=0;assert(!mp_box_paint(&box,&s,b,&content,palette,2,&cache,&work,1));
    assert(!memcmp(buffers[0],clean,sizeof clean));
    box.opacity=255;box.background_alpha=255;box.radius=(struct mp_ui_corners){9,7,5,3};
    box.border=(struct mp_ui_edges){2,3,4,5};box.border_color=0xffff00;
    assert(!mp_box_paint(&box,&s,b,&content,palette,2,&cache,&work,1));
    assert(buffers[0][30*W+30]==clean[30*W+30]);assert(buffers[0][30*W+45]==0xffff00);
    box.shadow.color=0x000000;box.shadow.alpha=160;box.shadow.x=5;box.shadow.y=6;box.shadow.spread=2;box.shadow.blur=3;
    box.blur=2;box.backdrop_blur=3;box.background_alpha=96;box.opacity=170;
    art[8*40+8]=art[8*40+9]=1;
    for(unsigned mode=MP_BLEND_NORMAL;mode<=MP_BLEND_ADD;++mode) {
        box.blend_mode=mode;box.border_style=(enum mp_border_style)(mode%3);
        assert(!mp_box_paint(&box,&s,b,&content,palette,2,&cache,&work,1));
        struct mp_box_cache fresh={0};struct mp_box_workspace other={0};memcpy(buffers[1],clean,sizeof clean);
        assert(!mp_box_paint(&box,&reference,b,&content,palette,2,&fresh,&other,0));
        assert(!memcmp(buffers[0],buffers[1],sizeof clean));
        mp_box_cache_dispose(&fresh);mp_box_workspace_dispose(&other);
    }
    /* Move, shrink and remove all effects. Old shadow/blur pixels must vanish. */
    b=(struct mp_ui_rect){90,70,12,10};content.width=12;content.height=10;
    assert(!mp_box_paint(&box,&s,b,&content,palette,2,&cache,&work,1));
    struct mp_box_cache fresh={0};struct mp_box_workspace other={0};memcpy(buffers[1],clean,sizeof clean);
    assert(!mp_box_paint(&box,&reference,b,&content,palette,2,&fresh,&other,0));
    assert(!memcmp(buffers[0],buffers[1],sizeof clean));
    mp_box_cache_dispose(&fresh);mp_box_workspace_dispose(&other);
    mp_box_cache_restore(&cache,&s);assert(!memcmp(buffers[0],clean,sizeof clean));
    box=plain();box.blend_mode=MP_BLEND_MULTIPLY;box.background=0x804020;
    memset(buffers[0],255,sizeof clean);cache.valid=0;
    assert(!mp_box_paint(&box,&s,b,&content,palette,2,&cache,&work,0));
    assert(buffers[0][75*W+95]==0x804020);
    /* Portrait framebuffer mapping, clipped effects and a scene replacement. */
    struct mp_font_surface rotated={buffers[2]+(W-1)*H,W,H,-H,1};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)rotated.pixels[(ptrdiff_t)x*rotated.x_stride+y]=clean[y*W+x];
    mp_box_cache_dispose(&cache);box=plain();box.shadow.alpha=90;box.shadow.x=-8;box.backdrop_blur=4;
    b=(struct mp_ui_rect){-5,-4,40,24};content.width=40;content.height=24;
    assert(!mp_box_paint(&box,&rotated,b,&content,palette,2,&cache,&work,0));
    memcpy(buffers[1],clean,sizeof clean);fresh=(struct mp_box_cache){0};
    assert(!mp_box_paint(&box,&reference,b,&content,palette,2,&fresh,&work,0));
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        assert(rotated.pixels[(ptrdiff_t)x*rotated.x_stride+y]==buffers[1][y*W+x]);
    mp_box_cache_dispose(&fresh);
    mp_ui_fill(&rotated,(struct mp_ui_rect){0,0,W,H},0x123456);
    assert(!mp_box_paint(&box,&rotated,b,&content,palette,2,&cache,&work,0));
    mp_box_cache_restore(&cache,&rotated);
    for(unsigned i=0;i<W*H;++i)assert(buffers[2][i]==0x123456);
    mp_box_cache_dispose(&cache);mp_box_workspace_dispose(&work);
    box.shadow.spread=33;assert(!mp_box_style_valid(&box));
    box=plain();box.background_alpha=256;assert(!mp_box_style_valid(&box));
}
static void transparent_index(void)
{
    struct mp_box_style box=plain();box.background_alpha=0;
    uint8_t art[40*24];uint32_t palette[]={0,0x7dcfff,0x123456};
    for(unsigned i=0;i<sizeof art;i++)art[i]=(i%7==0)?1:(i%11==0)?2:0;
    struct mp_ui_index_surface content={art,40,24,40};
    for(int rotated=0;rotated<2;rotated++)for(int clipped=0;clipped<2;clipped++){
        reset();struct mp_font_surface a=surface(0),b=surface(1);
        if(rotated){a=(struct mp_font_surface){buffers[0]+(W-1)*H,W,H,-H,1};b=(struct mp_font_surface){buffers[1]+(W-1)*H,W,H,-H,1};}
        struct mp_ui_rect bounds={clipped?-5:30,clipped?-4:30,40,24},all={0,0,40,24};
        struct mp_box_cache fast={0},reference={0};struct mp_box_workspace work={0};
        assert(mp_box_can_index_paint(&box));
        for(unsigned frame=0;frame<3;frame++){
            art[10]=frame%3;
            assert(!mp_box_index_paint(&box,&a,bounds,&content,palette,3,all,&fast,frame!=0));
            assert(!mp_box_paint(&box,&b,bounds,&content,palette,3,&reference,&work,frame!=0));
            assert(!memcmp(buffers[0],buffers[1],sizeof clean));
        }
        mp_box_cache_dispose(&fast);mp_box_cache_dispose(&reference);mp_box_workspace_dispose(&work);
    }
}
struct dummy {unsigned width;int visible;};
static int measure(const void *ctx,const struct mp_theme *t,struct mp_indicator_metrics *m)
{(void)t;const struct dummy*d=ctx;*m=(struct mp_indicator_metrics){d->width,20,100};return 0;}
static void tick(void *ctx,const struct mp_theme*t,struct mp_ui_clock*c,struct mp_indicator_frame*f)
{(void)t;(void)c;struct dummy*d=ctx;*f=(struct mp_indicator_frame){d->visible,1,0};}
static void paint(const void *ctx,const struct mp_theme*t,struct mp_ui_index_surface*s)
{(void)ctx;(void)t;for(unsigned y=0;y<s->height;++y)for(unsigned x=0;x<s->width;++x)s->pixels[y*s->stride+x]=5;}
static void status(void)
{
    static const struct mp_status_indicator_ops ops={measure,tick,paint};
    struct mp_theme theme=test_theme;
    theme.status.layout.orientation=MP_VERTICAL;theme.status.layout.align=MP_ALIGN_CENTER;
    theme.status.layout.justify=MP_JUSTIFY_END;theme.status.layout.margin.top=10;theme.status.layout.margin.right=10;
    theme.status.layout.padding=(struct mp_ui_edges){2,3,4,5};theme.status.layout.gap=3;
    theme.status.layout.max_height=512;theme.status.layout.max_width=140;
    struct mp_status_bar bar;assert(!mp_status_bar_init(&bar,&theme,960,720));
    struct dummy children[8];char names[8][4];
    for(unsigned i=0;i<8;++i){children[i]=(struct dummy){80-i*3,1};snprintf(names[i],4,"i%u",i);
        struct mp_status_indicator indicator={names[i],&ops,&children[i]};assert(!mp_status_bar_register(&bar,&indicator,i,0));}
    assert(bar.large_capacity>MP_STATUS_INLINE_PIXELS);mp_status_bar_tick(&bar,1000,0);
    assert(bar.bounds.width==96 && bar.bounds.height==195);
    assert(bar.items[1].box.y==bar.items[0].box.y+23);
    assert(!mp_status_bar_set_enabled(&bar,"i2",0,1001));mp_status_bar_tick(&bar,2000,1);assert(bar.items[2].extent==0);
    theme.status.collapse_hidden=0;assert(!mp_status_bar_set_theme(&bar,&theme));mp_status_bar_tick(&bar,2100,0);
    assert(bar.items[2].extent==23 && bar.items[2].paint_extent==0);
    theme.status.visible=0;assert(!mp_status_bar_set_theme(&bar,&theme));mp_status_bar_tick(&bar,2200,1);
    assert(!bar.bounds.width && mp_status_bar_wait_ms(&bar,2200)==UINT_MAX);
    theme.status.visible=1;theme.status.animations=0;assert(!mp_status_bar_set_theme(&bar,&theme));mp_status_bar_tick(&bar,2300,1);
    assert(bar.items[0].extent==20 && mp_status_bar_wait_ms(&bar,2300)==UINT_MAX);
    mp_status_bar_dispose(&bar);
}
static void status_buffers(void)
{
    static uint32_t rgb[3][960*720], backdrop[960*720];
    for(unsigned y=0;y<720;++y)for(unsigned x=0;x<960;++x)
        backdrop[y*960+x]=((x%256)<<16)|((y%256)<<8)|((x+y)%256);
    for(unsigned n=0;n<3;++n)memcpy(rgb[n],backdrop,sizeof backdrop);
    struct mp_theme t=test_theme;
    t.status.layout.margin.top=60;t.status.layout.margin.right=100;
    t.status.layout.max_width=300;t.status.layout.padding=(struct mp_ui_edges){6,7,8,9};
    t.status.box.frame=(struct mp_pixel_frame){0};
    t.status.box.background_alpha=100;t.status.box.opacity=180;
    t.status.box.border=(struct mp_ui_edges){2,2,2,2};
    t.status.box.radius=(struct mp_ui_corners){7,7,7,7};
    t.status.box.shadow.alpha=140;t.status.box.shadow.x=6;t.status.box.shadow.y=8;t.status.box.shadow.blur=3;
    t.status.box.backdrop_blur=4;t.status.box.blur=1;
    struct mp_status_bar bar;assert(!mp_status_bar_init(&bar,&t,960,720));
    struct dummy d[2]={{60,1},{50,1}};static const struct mp_status_indicator_ops ops={measure,tick,paint};
    struct mp_status_indicator a={"a",&ops,&d[0]},b={"b",&ops,&d[1]};
    assert(!mp_status_bar_register(&bar,&a,0,0));assert(!mp_status_bar_register(&bar,&b,1,0));
    struct mp_status_bar_cache caches[2]={{0},{0}};
    for(unsigned n=0;n<60;++n){
        if(n==10){assert(!mp_status_bar_set_enabled(&bar,"b",0,n*31));}
        if(n==20){t.status.layout.orientation=MP_VERTICAL;t.status.layout.direction=MP_REVERSE;assert(!mp_status_bar_set_theme(&bar,&t));}
        if(n==30){t.status.box=plain();assert(!mp_status_bar_set_theme(&bar,&t));}
        if(n==40){t.status.visible=0;assert(!mp_status_bar_set_theme(&bar,&t));}
        if(n==50){t.status.visible=1;t.status.box.opacity=120;assert(!mp_status_bar_set_theme(&bar,&t));}
        mp_status_bar_tick(&bar,n*31,1);unsigned index=n%2;
        struct mp_font_surface canvas={rgb[index],960,720,1,960},reference={rgb[2],960,720,1,960};
        if(n==25){memcpy(rgb[index],backdrop,sizeof backdrop);caches[index].valid=0;}
        assert(mp_status_bar_paint(&bar,&canvas,&caches[index])>=0);
        memcpy(rgb[2],backdrop,sizeof backdrop);struct mp_status_bar_cache fresh={0};
        assert(mp_status_bar_paint(&bar,&reference,&fresh)>=0);
        assert(!memcmp(rgb[index],rgb[2],sizeof backdrop));mp_status_bar_cache_dispose(&fresh);
    }
    for(unsigned n=0;n<2;++n)mp_status_bar_cache_dispose(&caches[n]);
    mp_status_bar_dispose(&bar);
}
int main(void){
    test_theme = mp_test_legacy_status_theme();
layout();renderer();transparent_index();status();status_buffers();puts("Styles: layout, motion, alpha, rounding, borders, shadows, blend modes, blur, rotation, restoration and status controls passed.");}
