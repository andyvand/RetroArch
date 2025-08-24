/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2014-2017 - Francisco Javier Trujillo Mata
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

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <psx.h>

#include "../audio_driver.h"

#define AUDIO_BUFFER 128 * 1024
#define AUDIO_CHANNELS 2
#define AUDIO_BITS 16

typedef struct psx_audio
{
   /* TODO/FIXME - nonblock is not implemented */
   bool nonblock;
   bool running;
   SsVag vag;
} psx_audio_t;

static void *psx_audio_init(const char *device,
      unsigned rate, unsigned latency,
      unsigned block_frames,
      unsigned *new_rate)
{
   psx_audio_t *psx = (psx_audio_t*)calloc(1, sizeof(psx_audio_t));

   if (!psx)
      return NULL;

   psx->vag.version = 1;
   psx->vag.data_size = AUDIO_BUFFER;
   psx->vag.sample_rate = rate;
   memcpy(psx->vag.name, "PSXAUDIO", 8);
   psx->vag.spu_addr = SPU_DATA_BASE_ADDR;
   psx->vag.cur_voice = 0;

   return psx;
}

static void psx_audio_free(void *data)
{
   psx_audio_t* psx = (psx_audio_t*)data;
   if (!psx)
      return;

   psx->running = false;
   SsStopVag(&psx->vag);
   free(psx);
}

static ssize_t psx_audio_write(void *data, const void *s, size_t len)
{
   psx_audio_t* psx = (psx_audio_t*)data;
   if (!psx->running)
      return -1;

   psx->vag.data = (void *)s;
   psx->vag.data_size = len;

   SsUploadVag(&psx->vag);
   SsPlayVag(&psx->vag, 0, SPU_MAXVOL, SPU_MAXVOL);

   return (ssize_t)len;
}

static bool psx_audio_alive(void *data)
{
   psx_audio_t* psx = (psx_audio_t*)data;
   if (psx)
      return psx->running;
   return false;
}

static bool psx_audio_stop(void *data)
{
   psx_audio_t* psx = (psx_audio_t*)data;
   if (psx)
   {
      SsStopVag(&psx->vag);
      psx->running = false;
   }
   return true;
}

static bool psx_audio_start(void *data, bool is_shutdown)
{
   psx_audio_t* psx = (psx_audio_t*)data;
   if (psx)
      psx->running = true;
   return true;
}

static void psx_audio_set_nonblock_state(void *data, bool toggle)
{
   psx_audio_t* psx = (psx_audio_t*)data;

   if (psx)
      psx->nonblock = toggle;
}

static size_t psx_audio_write_avail(void *data)
{
   psx_audio_t* psx = (psx_audio_t*)data;

   if (psx && psx->running)
      return AUDIO_BUFFER;

   return 0;
}

static bool psx_audio_use_float(void *data) { return false; }
static size_t psx_audio_buffer_size(void *data) { return AUDIO_BUFFER; }

audio_driver_t audio_psx = {
   psx_audio_init,
   psx_audio_write,
   psx_audio_stop,
   psx_audio_start,
   psx_audio_alive,
   psx_audio_set_nonblock_state,
   psx_audio_free,
   psx_audio_use_float,
   "psx",
   NULL,
   NULL,
   psx_audio_write_avail,
   psx_audio_buffer_size
};
