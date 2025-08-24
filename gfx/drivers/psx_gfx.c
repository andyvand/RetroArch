/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *  Copyright (C) 2011-2017 - Higor Euripedes
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>

#include <gfx/scaler/scaler.h>
#include <gfx/video_frame.h>
#include "../../verbosity.h"

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#ifdef HAVE_MENU
#include "../../menu/menu_driver.h"
#endif

#ifdef HAVE_X11
#include "../common/x11_common.h"
#endif

#include <psx.h>

#include "../font_driver.h"

#include "../../configuration.h"
#include "../../retroarch.h"

unsigned int primitive_list[4000];
unsigned char framebuffer24[640*480*3];
unsigned char framebuffer[640*480*4];

typedef struct psx_menu_frame
{
   struct scaler_ctx scaler;
   uint8_t *frame;
   bool active;
} psx_menu_frame_t;

typedef struct psx_video
{
   struct scaler_ctx scaler;
   psx_menu_frame_t menu;
   GsImage image;
   GsDispEnv dispenv;
   GsDrawEnv drawenv;

   void *font;
   const font_renderer_driver_t *font_driver;
   uint8_t font_r;
   uint8_t font_g;
   uint8_t font_b;
   bool quitting;
} psx_video_t;

static void psx_gfx_free(void *data)
{
   psx_video_t *vid = (psx_video_t*)data;
   if (!vid)
      return;

   if (vid->menu.frame)
      free(vid->menu.frame);

   if (vid->font)
      vid->font_driver->free(vid->font);

   scaler_ctx_gen_reset(&vid->scaler);
   scaler_ctx_gen_reset(&vid->menu.scaler);

   free(vid);
}

static void psx_init_font(psx_video_t *vid,
      bool video_font_enable,
      const char *path_font,
      float video_font_size,
      float msg_color_r,
      float msg_color_g,
      float msg_color_b
      )
{
   int r, g, b;

   if (!video_font_enable)
      return;

   if (!font_renderer_create_default(
            &vid->font_driver, &vid->font,
            *path_font ? path_font : NULL,
            video_font_size))
   {
      RARCH_LOG("[psx] Could not initialize fonts.\n");
      return;
   }

   r = msg_color_r * 255;
   g = msg_color_g * 255;
   b = msg_color_b * 255;

   r = (r < 0) ? 0 : (r > 255 ? 255 : r);
   g = (g < 0) ? 0 : (g > 255 ? 255 : g);
   b = (b < 0) ? 0 : (b > 255 ? 255 : b);

   vid->font_r = r;
   vid->font_g = g;
   vid->font_b = b;
}

static void psx_render_msg(
      psx_video_t *vid,
      uint8_t *buffer,
      const char *msg,
      unsigned width,
      unsigned height,
      float msg_pos_x,
      float msg_pos_y
      )
{
   int x, y, msg_base_x, msg_base_y;
   unsigned rshift, gshift, bshift;
   const struct font_atlas *atlas = NULL;

   if (!vid->font)
      return;

   atlas      = vid->font_driver->get_atlas(vid->font);

   msg_base_x = msg_pos_x * width;
   msg_base_y = (1.0f - msg_pos_y) * height;

   rshift     = 16;
   gshift     = 8;
   bshift     = 0;

   for (; *msg; msg++)
   {
      int glyph_width, glyph_height;
      int base_x, base_y, max_width, max_height;
      uint32_t             *out      = NULL;
      const uint8_t             *src = NULL;
      const struct font_glyph *glyph = vid->font_driver->get_glyph(vid->font, (uint8_t)*msg);
      if (!glyph)
         continue;

      glyph_width  = glyph->width;
      glyph_height = glyph->height;

      base_x       = msg_base_x + glyph->draw_offset_x;
      base_y       = msg_base_y + glyph->draw_offset_y;
      src          = atlas->buffer + glyph->atlas_offset_x
         + glyph->atlas_offset_y * atlas->width;

      if (base_x < 0)
      {
         src         -= base_x;
         glyph_width += base_x;
         base_x       = 0;
      }

      if (base_y < 0)
      {
         src          -= base_y * (int)atlas->width;
         glyph_height += base_y;
         base_y        = 0;
      }

      max_width  = width - base_x;
      max_height = height - base_y;

      if (max_width <= 0 || max_height <= 0)
         continue;

      if (glyph_width > max_width)
         glyph_width = max_width;
      if (glyph_height > max_height)
         glyph_height = max_height;

      out = (uint32_t*)buffer + (base_y
         * 640) + base_x;

      for (y = 0; y < glyph_height; y++, src += atlas->width, out += 640)
      {
         for (x = 0; x < glyph_width; x++)
         {
            unsigned blend   = src[x];
            unsigned out_pix = out[x];
            unsigned       r = (out_pix >> rshift) & 0xff;
            unsigned       g = (out_pix >> gshift) & 0xff;
            unsigned       b = (out_pix >> bshift) & 0xff;

            unsigned   out_r = (r * (256 - blend) + vid->font_r * blend) >> 8;
            unsigned   out_g = (g * (256 - blend) + vid->font_g * blend) >> 8;
            unsigned   out_b = (b * (256 - blend) + vid->font_b * blend) >> 8;
            out[x]           = (out_r << rshift) |
                               (out_g << gshift) |
                               (out_b << bshift);
         }
      }

      msg_base_x += glyph->advance_x;
      msg_base_y += glyph->advance_y;
   }
}

static void *psx_gfx_init(const video_info_t *video,
      input_driver_t **input, void **input_data)
{
   unsigned full_x, full_y;
   psx_video_t                *vid = NULL;
   settings_t            *settings = config_get_ptr();
   const char *path_font           = settings->paths.path_font;
   float video_font_size           = settings->floats.video_font_size;
   bool video_font_enable          = settings->bools.video_font_enable;
   float msg_color_r               = settings->floats.video_msg_color_r;
   float msg_color_g               = settings->floats.video_msg_color_g;
   float msg_color_b               = settings->floats.video_msg_color_b;

   full_x     = 640;
   full_y     = 480;
   RARCH_LOG("[psx] Detecting resolution %ux%u.\n", full_x, full_y);

   GsSetVideoModeEx(full_x, full_y, VMODE_NTSC, 0, 1, 0);

   vid->dispenv.x = 0;
   vid->dispenv.y = 0;

   GsSetDispEnv(&vid->dispenv);

   vid->drawenv.dither = 0;
   vid->drawenv.draw_on_display = 1;
   vid->drawenv.x = 0;
   vid->drawenv.y = 0;
   vid->drawenv.w = 640;
   vid->drawenv.h = 512;
   vid->drawenv.ignore_mask = 0;
   vid->drawenv.set_mask = 0;

   GsSetDrawEnv(&vid->drawenv);
   GsSetList(primitive_list);

   if (input && input_data)
   {
      void *psx_input = input_driver_init_wrap(&input_psx,
            settings->arrays.input_joypad_driver);

      if (psx_input)
      {
         *input = &input_psx;
         *input_data = psx_input;
      }
      else
      {
         *input = NULL;
         *input_data = NULL;
      }
   }

   psx_init_font(vid,
         video_font_enable,
         path_font, video_font_size,
         msg_color_r,
         msg_color_g,
         msg_color_b);

   vid->scaler.scaler_type      = video->smooth ? SCALER_TYPE_BILINEAR : SCALER_TYPE_POINT;
   vid->scaler.in_fmt           = video->rgb32 ? SCALER_FMT_ARGB8888 : SCALER_FMT_RGB565;
   vid->scaler.out_fmt          = SCALER_FMT_ARGB8888;

   vid->menu.scaler             = vid->scaler;
   vid->menu.scaler.scaler_type = SCALER_TYPE_BILINEAR;
   vid->menu.frame              = (uint8_t *)framebuffer;

   return vid;
}

static bool psx_gfx_frame(void *data, const void *frame, unsigned width,
      unsigned height, uint64_t frame_count,
      unsigned pitch, const char *msg, video_frame_info_t *video_info)
{
   char title[128];
   psx_video_t   *vid = (psx_video_t*)data;
#ifdef HAVE_MENU
   bool menu_is_alive = (video_info->menu_st_flags & MENU_ST_FLAG_ALIVE) ? true : false;
#endif

   if (!vid)
      return true;

   title[0] = '\0';

   video_driver_get_window_title(title, sizeof(title));

   if (vid->menu.active)
   {
      for (int y = 0; y < 480; y++) {
         for (int x = 0; x < (640 * 3); x += 3) {
            framebuffer24[(y * x) + x] = ((*((uint32_t *)frame + ((x * y) + x / 3)) & 0xFF0000) >> 16);
            framebuffer24[(y * x) + (x + 1)] = ((*((uint32_t *)frame + ((x * y) + x / 3)) & 0xFF00) >> 8);
            framebuffer24[(y * x) + (x + 2)] = ((*((uint32_t *)frame + ((x * y) + x / 3)) & 0xFF));
         }
      }
#ifdef HAVE_MENU
      menu_driver_frame(menu_is_alive, video_info);
#endif
   } else {
      video_frame_scale(
            &vid->scaler,
            framebuffer,
            frame,
            vid->scaler.in_fmt,
            640,
            480,
            640 * 4,
            width,
            height,
            pitch);


      if (msg)
         psx_render_msg(vid, framebuffer,
         msg, 640, 480,
         video_info->font_msg_pos_x,
         video_info->font_msg_pos_y);

      for (int y = 0; y < 480; y++) {
         for (int x = 0; x < (640 * 3); x += 3) {
            framebuffer24[(y * x) + x] = ((*((uint32_t *)frame + ((x * y) + x / 3)) & 0xFF0000) >> 16);
            framebuffer24[(y * x) + (x + 1)] = ((*((uint32_t *)frame + ((x * y) + x / 3)) & 0xFF00) >> 8);
            framebuffer24[(y * x) + (x + 2)] = ((*((uint32_t *)frame + ((x * y) + x / 3)) & 0xFF));
         }
      }
   }

   vid->image.pmode = 3;
   vid->image.has_clut = 0;
   vid->image.clut_x = 0;
   vid->image.clut_y = 0;
   vid->image.clut_w = 0;
   vid->image.clut_h = 0;
   vid->image.x = 0;
   vid->image.y = 0;
   vid->image.w = 640;
   vid->image.h = 480;
   vid->image.clut_data = NULL;
   vid->image.data = (void *)framebuffer24;

   GsUploadImage(&vid->image);
   while(GsIsDrawing());

   return true;
}

static void psx_gfx_set_nonblock_state(void *a, bool b, bool c, unsigned d) { }

static bool psx_gfx_alive(void *data)
{
   return true;
}

static bool psx_gfx_focus(void *data)
{
   return true;
}

static bool psx_gfx_suspend_screensaver(void *data, bool enable) { return false; }
/* TODO/FIXME - implement */
static bool psx_gfx_has_windowed(void *data) { return true; }

static void psx_gfx_viewport_info(void *data, struct video_viewport *vp)
{
   psx_video_t *vid = (psx_video_t*)data;
   vp->x      = 0;
   vp->y      = 0;
   vp->width  = vp->full_width  = 640;
   vp->height = vp->full_height = 480;
}

static void psx_set_filtering(void *data, unsigned index, bool smooth, bool ctx_scaling)
{
   psx_video_t *vid = (psx_video_t*)data;
   vid->scaler.scaler_type = smooth ? SCALER_TYPE_BILINEAR : SCALER_TYPE_POINT;
}

static void psx_apply_state_changes(void *data)
{
   (void)data;
}

static void psx_set_texture_frame(void *data, const void *frame, bool rgb32,
      unsigned width, unsigned height, float alpha)
{
   enum scaler_pix_fmt format = rgb32
      ? SCALER_FMT_ARGB8888 : SCALER_FMT_RGBA4444;
   psx_video_t           *vid = (psx_video_t*)data;

   video_frame_scale(
         &vid->menu.scaler,
         vid->menu.frame,
         frame,
         format,
         640,
         480,
         640 * 4,
         width,
         height,
         width * (rgb32 ? sizeof(uint32_t) : sizeof(uint16_t))
         );
}

static void psx_set_texture_enable(void *data, bool state, bool full_screen)
{
   psx_video_t *vid = (psx_video_t*)data;

   (void)full_screen;

   vid->menu.active = state;
}

static void psx_show_mouse(void *data, bool state)
{
   (void)data;
}

static void psx_grab_mouse_toggle(void *data)
{
   (void)data;
}

static uint32_t psx_get_flags(void *data)
{
   uint32_t             flags   = 0;

   BIT32_SET(flags, GFX_CTX_FLAGS_SCREENSHOTS_SUPPORTED);

   return flags;
}

static const video_poke_interface_t psx_poke_interface = {
   psx_get_flags,
   NULL, /* load_texture */
   NULL, /* unload_texture */
   NULL, /* set_video_mode */
   NULL, /* get_refresh_rate */
   psx_set_filtering,
   NULL, /* get_video_output_size */
   NULL, /* get_video_output_prev */
   NULL, /* get_video_output_next */
   NULL, /* get_current_framebuffer */
   NULL, /* get_proc_address */
   NULL, /* set_aspect_ratio */
   psx_apply_state_changes,
   psx_set_texture_frame,
   psx_set_texture_enable,
   NULL, /* set_osd_msg */
   psx_show_mouse,
   psx_grab_mouse_toggle,
   NULL, /* get_current_shader */
   NULL, /* get_current_software_framebuffer */
   NULL, /* get_hw_render_interface */
   NULL, /* set_hdr_max_nits */
   NULL, /* set_hdr_paper_white_nits */
   NULL, /* set_hdr_contrast */
   NULL  /* set_hdr_expand_gamut */
};

static void psx_get_poke_interface(void *data, const video_poke_interface_t **iface)
{
   (void)data;

   *iface = &psx_poke_interface;
}

static bool psx_gfx_set_shader(void *data,
      enum rarch_shader_type type, const char *path)
{
   (void)data;
   (void)type;
   (void)path;

   return false;
}

video_driver_t video_psx = {
   psx_gfx_init,
   psx_gfx_frame,
   psx_gfx_set_nonblock_state,
   psx_gfx_alive,
   psx_gfx_focus,
#ifdef HAVE_X11
   x11_suspend_screensaver,
#else
   psx_gfx_suspend_screensaver,
#endif
   psx_gfx_has_windowed,
   psx_gfx_set_shader,
   psx_gfx_free,
   "psx",
   NULL, /* set_viewport */
   NULL, /* set_rotation */
   psx_gfx_viewport_info,
   NULL, /* read_viewport  */
   NULL, /* read_frame_raw */
#ifdef HAVE_OVERLAY
   NULL, /* get_overlay_interface */
#endif
   psx_get_poke_interface,
   NULL, /* wrap_type_to_enum */
#ifdef HAVE_GFX_WIDGETS
   NULL  /* gfx_widgets_enabled */
#endif
};
