/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2011-2018 - Francisco Javier Trujillo Mata - fjtrujy
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
#include <stddef.h>
#include <boolean.h>

#include "../../config.def.h"
#include "../../tasks/tasks_internal.h"

#include "../input_driver.h"

#include <psx.h>

#ifndef ALIGNED
#define ALIGNED(X) __attribute__((aligned(X)))
#endif

#ifndef UINT64_C
#define UINT64_C(c) c ## ULL
#endif

#define PSX_MAX_PORT 2 /* each ps2 has 2 ports */
#define PSX_MAX_SLOT 4 /* maximum - 4 slots in one multitap */
#define PSX_ANALOG_STICKS 2
#define PSX_ANALOG_AXIS 2

/* TODO/FIXME - static globals */
static unsigned char padBuf[PSX_MAX_PORT][PSX_MAX_SLOT][256] ALIGNED(64);
static uint64_t pad_state[DEFAULT_MAX_PADS];
static int16_t analog_state[DEFAULT_MAX_PADS][PSX_ANALOG_STICKS][PSX_ANALOG_AXIS];

static INLINE int16_t convert_u8_to_s16(uint8_t val)
{
   if (val == 0)
      return -0x7fff;
   return val * 0x0101 - 0x8000;
}

static const char *psx_joypad_name(unsigned pad)
{
   return "PSX Controller";
}

static void *psx_joypad_init(void *data)
{
   return NULL;
}

static int32_t psx_joypad_button(unsigned port, uint16_t joykey)
{
   if (port >= DEFAULT_MAX_PADS)
      return 0;
   return pad_state[port] & (UINT64_C(1) << joykey);
}

static int16_t psx_joypad_axis_state(unsigned port_num, uint32_t joyaxis)
{
   if (AXIS_NEG_GET(joyaxis) < 4)
   {
      int16_t val  = 0;
      int16_t axis = AXIS_NEG_GET(joyaxis);
      switch (axis)
      {
         case 0:
         case 1:
            val = analog_state[port_num][0][axis];
            break;
         case 2:
         case 3:
            val = analog_state[port_num][1][axis - 2];
            break;
      }
      if (val < 0)
         return val;
   }
   else if (AXIS_POS_GET(joyaxis) < 4)
   {
      int16_t val  = 0;
      int16_t axis = AXIS_POS_GET(joyaxis);
      switch (axis)
      {
         case 0:
         case 1:
            val = analog_state[port_num][0][axis];
            break;
         case 2:
         case 3:
            val = analog_state[port_num][1][axis - 2];
            break;
      }
      if (val > 0)
         return val;
   }
   return 0;
}

static int16_t psx_joypad_state(
      rarch_joypad_info_t *joypad_info,
      const struct retro_keybind *binds,
      unsigned port)
{
   int16_t ret                          = 0;
   uint16_t port_idx                    = joypad_info->joy_idx;

   if (port_idx < DEFAULT_MAX_PADS)
   {
      int i;
      for (i = 0; i < RARCH_FIRST_CUSTOM_BIND; i++)
      {
         /* Auto-binds are per joypad, not per user. */
         const uint64_t joykey  = (binds[i].joykey != NO_BTN)
            ? binds[i].joykey  : joypad_info->auto_binds[i].joykey;
         const uint32_t joyaxis = (binds[i].joyaxis != AXIS_NONE)
            ? binds[i].joyaxis : joypad_info->auto_binds[i].joyaxis;
         if (
               (uint16_t)joykey != NO_BTN
               && pad_state[port_idx] & (UINT64_C(1) << joykey)
            )
            ret |= ( 1 << i);
         else if (joyaxis != AXIS_NONE &&
               ((float)abs(psx_joypad_axis_state(port_idx, joyaxis))
                / 0x8000) > joypad_info->axis_threshold)
            ret |= (1 << i);
      }
   }

   return ret;
}

static int16_t psx_joypad_axis(unsigned port_num, uint32_t joyaxis)
{
   if (port_num >= DEFAULT_MAX_PADS)
      return 0;
   return psx_joypad_axis_state(port_num, joyaxis);
}

static void psx_joypad_get_buttons(unsigned port_num, input_bits_t *state)
{
   BIT256_CLEAR_ALL_PTR(state);
}

static void psx_joypad_poll(void)
{
   int i;
   psx_pad_state buttons;

   for (i = 0; i < DEFAULT_MAX_PADS; i++)
   {
      int psx_port = i & 0x1;

      PSX_PollPad(psx_port, &buttons);
      int32_t state_tmp = buttons.buttons;
      pad_state[i] = 0;

      pad_state[i] |= (state_tmp & PAD_LEFT) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_LEFT) : 0;
      pad_state[i] |= (state_tmp & PAD_DOWN) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_DOWN) : 0;
      pad_state[i] |= (state_tmp & PAD_RIGHT) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_RIGHT) : 0;
      pad_state[i] |= (state_tmp & PAD_UP) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_UP) : 0;
      pad_state[i] |= (state_tmp & PAD_START) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_START) : 0;
      pad_state[i] |= (state_tmp & PAD_SELECT) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_SELECT) : 0;
      pad_state[i] |= (state_tmp & PAD_TRIANGLE) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_X) : 0;
      pad_state[i] |= (state_tmp & PAD_SQUARE) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_Y) : 0;
      pad_state[i] |= (state_tmp & PAD_CROSS) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_B) : 0;
      pad_state[i] |= (state_tmp & PAD_CIRCLE) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_A) : 0;
      pad_state[i] |= (state_tmp & PAD_R1) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R) : 0;
      pad_state[i] |= (state_tmp & PAD_L1) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L) : 0;
      pad_state[i] |= (state_tmp & PAD_R2) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R2) : 0;
      pad_state[i] |= (state_tmp & PAD_L2) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L2) : 0;
      pad_state[i] |= (state_tmp & PAD_RANALOGB) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R3) : 0;
      pad_state[i] |= (state_tmp & PAD_LANALOGB) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L3) : 0;

      /* Analog */
      analog_state[i][RETRO_DEVICE_INDEX_ANALOG_LEFT] [RETRO_DEVICE_ID_ANALOG_X] = convert_u8_to_s16(buttons.extra.analogJoy.x[0]);
      analog_state[i][RETRO_DEVICE_INDEX_ANALOG_LEFT] [RETRO_DEVICE_ID_ANALOG_Y] = convert_u8_to_s16(buttons.extra.analogJoy.y[0]);;
      analog_state[i][RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = convert_u8_to_s16(buttons.extra.analogJoy.x[1]);;
      analog_state[i][RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_Y] = convert_u8_to_s16(buttons.extra.analogJoy.y[1]);;
   }
}

static bool psx_joypad_query_pad(unsigned pad)
{
   return pad < DEFAULT_MAX_PADS && pad_state[pad];
}

static bool psx_joypad_rumble(unsigned pad,
      enum retro_rumble_effect effect, uint16_t strength) { return false; }

static void psx_joypad_destroy(void)
{
}

input_device_driver_t psx_joypad = {
   psx_joypad_init,
   psx_joypad_query_pad,
   psx_joypad_destroy,
   psx_joypad_button,
   psx_joypad_state,
   psx_joypad_get_buttons,
   psx_joypad_axis,
   psx_joypad_poll,
   psx_joypad_rumble,
   NULL, /* set_rumble_gain */
   NULL, /* set_sensor_state */
   NULL, /* get_sensor_input */
   psx_joypad_name,
   "psx",
};
