/* RetroArch - A frontend for libretro.
 * Copyright (C) 2010-2018 - Francisco Javier Trujillo Mata - fjtrujy
 *
 * RetroArch is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Found-
 * ation, either version 3 of the License, or (at your option) any later version.
 *
 * RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 * * You should have received a copy of the GNU General Public License along with RetroArch.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <psx.h>

#if defined(SCREEN_DEBUG)
#include <debug.h>
#endif

#ifndef IS_SALAMANDER
#include "../../retroarch.h"
#ifdef HAVE_MENU
#include "../../menu/menu_driver.h"
#endif
#endif

#include <compat/strl.h>
#include <file/file_path.h>
#include <string/stdstring.h>

#include "../frontend_driver.h"
#include "../../defaults.h"
#include "../../file_path_special.h"
#include "../../paths.h"
#include "../../verbosity.h"

#if defined(DEBUG)
#define DEFAULT_PARTITION "cdrom:"
#endif

#ifndef FILENAME_MAX
#define FILENAME_MAX 256
#endif

static enum frontend_fork psx_fork_mode      = FRONTEND_FORK_NONE;
static char cwd[FILENAME_MAX]                = {0};
static char mountString[10]                  = {0};
static char mountPoint[50]                   = {0};

static void create_path_names(void)
{
   char user_path[FILENAME_MAX];
   size_t _len = strlcpy(user_path, cwd, sizeof(user_path));
   strlcpy(user_path + _len, "/retroarch", sizeof(user_path) - _len);
   fill_pathname_basedir(g_defaults.dirs[DEFAULT_DIR_PORT], cwd, sizeof(g_defaults.dirs[DEFAULT_DIR_PORT]));

   /* Content in the same folder */

   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CORE], cwd,
         "cores", sizeof(g_defaults.dirs[DEFAULT_DIR_CORE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CORE_INFO], cwd,
         "info", sizeof(g_defaults.dirs[DEFAULT_DIR_CORE_INFO]));

   /* user data */
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_ASSETS], user_path,
         "assets", sizeof(g_defaults.dirs[DEFAULT_DIR_ASSETS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_DATABASE], user_path,
         "database/rdb", sizeof(g_defaults.dirs[DEFAULT_DIR_DATABASE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CHEATS], user_path,
         "cheats", sizeof(g_defaults.dirs[DEFAULT_DIR_CHEATS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_MENU_CONFIG], user_path,
         "config", sizeof(g_defaults.dirs[DEFAULT_DIR_MENU_CONFIG]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CORE_ASSETS], user_path,
         "downloads", sizeof(g_defaults.dirs[DEFAULT_DIR_CORE_ASSETS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_PLAYLIST], user_path,
         "playlists", sizeof(g_defaults.dirs[DEFAULT_DIR_PLAYLIST]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_REMAP], g_defaults.dirs[DEFAULT_DIR_MENU_CONFIG],
         "remaps", sizeof(g_defaults.dirs[DEFAULT_DIR_REMAP]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_SRAM], user_path,
         "savefiles", sizeof(g_defaults.dirs[DEFAULT_DIR_SRAM]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_SAVESTATE], user_path,
         "savestates", sizeof(g_defaults.dirs[DEFAULT_DIR_SAVESTATE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_SYSTEM], user_path,
         "system", sizeof(g_defaults.dirs[DEFAULT_DIR_SYSTEM]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CACHE], user_path,
         "temp", sizeof(g_defaults.dirs[DEFAULT_DIR_CACHE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_OVERLAY], user_path,
         "overlays", sizeof(g_defaults.dirs[DEFAULT_DIR_OVERLAY]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_THUMBNAILS], user_path,
         "thumbnails", sizeof(g_defaults.dirs[DEFAULT_DIR_THUMBNAILS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_LOGS], user_path,
         "logs", sizeof(g_defaults.dirs[DEFAULT_DIR_LOGS]));

   /* history and main config */
   strlcpy(g_defaults.dirs[DEFAULT_DIR_CONTENT_HISTORY],
         user_path, sizeof(g_defaults.dirs[DEFAULT_DIR_CONTENT_HISTORY]));
   fill_pathname_join(g_defaults.path_config, user_path,
         FILE_PATH_MAIN_CONFIG, sizeof(g_defaults.path_config));

#ifndef IS_SALAMANDER
   dir_check_defaults("custom.ini");
#endif
}

/* This method returns true if it can extract needed info from path, otherwise false.
 * In case of true, it also updates mountString, mountPoint and newCWD parameters
 * It splits path by ":", and requires a minimum of 3 elements
 * Example: if path = hdd0:__common:pfs:/retroarch/ then
 * mountString = "pfs:"
 * mountPoint = "hdd0:__common"
 * newCWD = pfs:/retroarch/
 * return true
*/
bool getMountInfo(char *path, char *mountString, char *mountPoint, char *newCWD)
{
   struct string_list *str_list = string_split(path, ":");
   if (str_list->size < 3)
      return false;

   sprintf(mountPoint, "%s:%s", str_list->elems[0].data, str_list->elems[1].data);
   sprintf(mountString, "%s:", str_list->elems[2].data);
   sprintf(newCWD, "%s%s", mountString, str_list->size == 4 ? str_list->elems[3].data : "");

   return true;
}

static int speed_counter = 0;

static void my_vblank_handler()
{
   speed_counter++;
}

static void init_drivers(bool extra_drivers)
{
   PSX_Init();
   GsInit();
   SetVBlankHandler(my_vblank_handler);
   GsClearMem();
   SsInit();
}

static void deinit_drivers(bool deinit_filesystem, bool deinit_powerOff)
{
   PSX_DeInit();
}

static void poweroffHandler(void *arg)
{
   deinit_drivers(true, false);
}

static void frontend_psx_get_env(int *argc, char *argv[],
      void *args, void *params_data)
{
   create_path_names();

#ifndef IS_SALAMANDER
   if (!string_is_empty(argv[1]))
   {
      static char path[FILENAME_MAX] = {0};
      struct rarch_main_wrap      *args =
         (struct rarch_main_wrap*)params_data;

      if (args)
      {
         strlcpy(path, argv[1], sizeof(path));

         args->flags         &= ~(RARCH_MAIN_WRAP_FLAG_VERBOSE
                                | RARCH_MAIN_WRAP_FLAG_NO_CONTENT);
         args->flags         |=   RARCH_MAIN_WRAP_FLAG_TOUCHED;
         args->config_path    = NULL;
         args->sram_path      = NULL;
         args->state_path     = NULL;
         args->content_path   = path;
         args->libretro_path  = NULL;

         RARCH_LOG("argv[0]: %s\n", argv[0]);
         RARCH_LOG("argv[1]: %s\n", argv[1]);

         RARCH_LOG("Auto-start game %s.\n", argv[1]);
      }
   }
#endif

#ifndef IS_SALAMANDER
   dir_check_defaults("custom.ini");
#endif
}

static void common_init_drivers(bool extra_drivers)
{
   init_drivers(extra_drivers);

   memcpy(cwd, "cdrom:", sizeof(cwd));

#if !defined(IS_SALAMANDER) && !defined(DEBUG)
   /* If it is not Salamander, we need to go one level
    * up for setting the CWD. */
   path_parent_dir(cwd, strlen(cwd));
#endif
}

static void frontend_psx_init(void *data)
{
#if defined(SCREEN_DEBUG)
   init_scr();
   scr_printf("\n\nStarting RetroArch...\n");
#endif
   common_init_drivers(true);
}

static void frontend_psx_deinit(void *data)
{
   bool deinit_filesystem = false;
#ifndef IS_SALAMANDER
   if (psx_fork_mode == FRONTEND_FORK_NONE)
      deinit_filesystem = true;
#endif
   deinit_drivers(deinit_filesystem, true);
}

static void frontend_psx_exec(const char *path, bool should_load_game)
{
   int args = 0;
   char *argv[1];
   RARCH_LOG("Attempt to load executable: [%s], partition [%s].\n", path, mountPoint);

   /* Reload IOP drivers for saving IOP ram */
   deinit_drivers(true, true);
   common_init_drivers(false);

#ifndef IS_SALAMANDER
   char game_path[FILENAME_MAX];
   if (should_load_game && !path_is_empty(RARCH_PATH_CONTENT))
   {
      args++;
      const char *content = path_get(RARCH_PATH_CONTENT);
      strlcpy(game_path, content, sizeof(game_path));
      argv[0] = game_path;
      RARCH_LOG("Attempt to load executable: [%s], partition [%s] with game [%s]\n", path, mountPoint, game_path);
   }
#endif
   LoadExec((char *)path, args, argv);
}

#ifndef IS_SALAMANDER
static bool frontend_psx_set_fork(enum frontend_fork fork_mode)
{
   switch (fork_mode)
   {
      case FRONTEND_FORK_CORE:
         RARCH_LOG("FRONTEND_FORK_CORE\n");
         psx_fork_mode  = fork_mode;
         break;
      case FRONTEND_FORK_CORE_WITH_ARGS:
         RARCH_LOG("FRONTEND_FORK_CORE_WITH_ARGS\n");
         psx_fork_mode  = fork_mode;
         break;
      case FRONTEND_FORK_RESTART:
         RARCH_LOG("FRONTEND_FORK_RESTART\n");
         /* NOTE: We don't implement Salamander, so just turn
          * this into FRONTEND_FORK_CORE. */
         psx_fork_mode  = FRONTEND_FORK_CORE;
         break;
      case FRONTEND_FORK_NONE:
      default:
         return false;
   }

   return true;
}
#endif

static void frontend_psx_exitspawn(char *s, size_t len, char *args)
{
   bool should_load_content = false;
#ifndef IS_SALAMANDER
   if (psx_fork_mode == FRONTEND_FORK_NONE)
      return;

   switch (psx_fork_mode)
   {
      case FRONTEND_FORK_CORE_WITH_ARGS:
         should_load_content = true;
         break;
      case FRONTEND_FORK_NONE:
      default:
         break;
   }
#endif
   frontend_psx_exec(s, should_load_content);
}

static int frontend_psx_get_rating(void) { return 4; }

enum frontend_architecture frontend_psx_get_arch(void)
{
   return FRONTEND_ARCH_MIPS;
}

static uint64_t frontend_psx_get_total_mem(void) { return 2*1024*1024; }

/* Crude try-and-fail approach, in lack of a better solution. */
static uint64_t frontend_psx_get_free_mem(void)
{
  uint64_t free_mem;
  size_t s0 = 2*1024*1024;
  void* p1;
  void* p2;
  void* p3;

  while (s0 && (p1 = malloc(s0)) == NULL)
    s0 >>= 1;

  free_mem = s0;

  s0 = 2*1024*1024;

  while (s0 && (p2 = malloc(s0)) == NULL)
    s0 >>= 1;

  free_mem += s0;

  s0 = 2*1024*1024;

  while (s0 && (p3 = malloc(s0)) == NULL)
    s0 >>= 1;

  free_mem += s0;

  if (p1)
    free(p1);
  if (p2)
    free(p2);
  if (p3)
    free(p3);

  return free_mem;
}

static int frontend_psx_parse_drive_list(void *data, bool load_content)
{
#ifndef IS_SALAMANDER
   char hdd[10];
   file_list_t *list = (file_list_t*)data;
   enum msg_hash_enums enum_idx = load_content
      ? MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR
      : MENU_ENUM_LABEL_FILE_BROWSER_DIRECTORY;

   menu_entries_append(list,
         "cdrom:",
         msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
         enum_idx,
         FILE_TYPE_DIRECTORY, 0, 0, NULL);
#endif

   return 0;
}

static void frontend_psx_process_args(int *argc, char *argv[])
{
#ifndef IS_SALAMANDER
   /* Make sure active core path is set here. */
   char path[PATH_MAX_LENGTH] = {0};
   strlcpy(path, argv[0], sizeof(path));
   if (path_is_valid(path))
      path_set(RARCH_PATH_CORE, path);
#endif
}

frontend_ctx_driver_t frontend_ctx_psx = {
   frontend_psx_get_env,         /* get_env */
   frontend_psx_init,            /* init */
   frontend_psx_deinit,          /* deinit */
   frontend_psx_exitspawn,       /* exitspawn */
   frontend_psx_process_args,    /* process_args */
   frontend_psx_exec,            /* exec */
#ifdef IS_SALAMANDER
   NULL,                         /* set_fork */
#else
   frontend_psx_set_fork,        /* set_fork */
#endif
   NULL,                         /* shutdown */
   NULL,                         /* get_name */
   NULL,                         /* get_os */
   frontend_psx_get_rating,      /* get_rating */
   NULL,                         /* load_content */
   frontend_psx_get_arch,        /* get_architecture */
   NULL,                         /* get_powerstate */
   frontend_psx_parse_drive_list,/* parse_drive_list */
   frontend_psx_get_total_mem,   /* get_total_mem */
   frontend_psx_get_free_mem,    /* get_free_mem */
   NULL,                         /* install_signal_handler */
   NULL,                         /* get_sighandler_state */
   NULL,                         /* set_sighandler_state */
   NULL,                         /* destroy_sighandler_state */
   NULL,                         /* attach_console */
   NULL,                         /* detach_console */
   NULL,                         /* get_lakka_version */
   NULL,                         /* set_screen_brightness */
   NULL,                         /* watch_path_for_changes */
   NULL,                         /* check_for_path_changes */
   NULL,                         /* set_sustained_performance_mode */
   NULL,                         /* get_cpu_model_name */
   NULL,                         /* get_user_language */
   NULL,                         /* is_narrator_running */
   NULL,                         /* accessibility_speak */
   NULL,                         /* set_gamemode */
   "ps2",                        /* ident */
   NULL                          /* get_video_driver */
};
