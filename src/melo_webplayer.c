/*
 * Copyright (C) 2020 Alexandre Dilly <dillya@sparod.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <stddef.h>

#include <melo/melo_module.h>

#define MELO_LOG_TAG "melo_radio_net"
#include <melo/melo_log.h>

#include "melo_webplayer_player.h"
#include "melo_youtube_browser.h"

#define MELO_WEBPLAYER_ID "com.sparod.webplayer"

static MeloYoutubeBrowser *youtube_browser;
static MeloWebplayerPlayer *player;

static void
melo_webplayer_enable (void)
{
  /* Create webplayer player */
  player = melo_webplayer_player_new ();

  /* Create youtube browser */
  youtube_browser = melo_youtube_browser_new ();
}

static void
melo_webplayer_disable (void)
{
  /* Release youtube browser */
  g_object_unref (youtube_browser);

  /* Release webplayer player */
  g_object_unref (player);
}

static const char *melo_webplayer_browser_list[] = {
    MELO_YOUTUBE_BROWSER_ID, NULL};
static const char *melo_webplayer_player_list[] = {
    MELO_WEBPLAYER_PLAYER_ID, NULL};

const MeloModule MELO_MODULE_SYM = {
    .id = MELO_WEBPLAYER_ID,
    .version = MELO_VERSION (1, 0, 0),
    .api_version = MELO_API_VERSION,

    .name = "WebPlayer",
    .description = "Web based Player support for Melo.",

    .browser_list = melo_webplayer_browser_list,
    .player_list = melo_webplayer_player_list,

    .enable_cb = melo_webplayer_enable,
    .disable_cb = melo_webplayer_disable,
};
