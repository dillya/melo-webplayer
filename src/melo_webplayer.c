/*
 * melo_webplayer.c: Web based Player module
 *
 * Copyright (C) 2016 Alexandre Dilly <dillya@sparod.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <string.h>

#include "melo_webplayer.h"
#include "melo_browser_webplayer.h"
#include "melo_player_webplayer.h"
#include "melo_playlist_simple.h"

#include "melo_browser_youtube.h"

/* Module WebPlayer info */
static MeloModuleInfo melo_webplayer_info = {
  .name = "WebPlayer",
  .description = "Play any media from website like Youtube or Dailymotion",
  .config_id = "webplayer",
};

static void melo_webplayer_constructed (GObject *gobject);
static const MeloModuleInfo *melo_webplayer_get_info (MeloModule *module);

struct _MeloWebPlayerPrivate {
  MeloBrowser *browser;
  MeloPlayer *player;
  MeloPlaylist *playlist;

  MeloBrowser *youtube;
};

G_DEFINE_TYPE_WITH_PRIVATE (MeloWebPlayer, melo_webplayer, MELO_TYPE_MODULE)

static void
melo_webplayer_finalize (GObject *gobject)
{
  MeloWebPlayerPrivate *priv =
                 melo_webplayer_get_instance_private (MELO_WEBPLAYER (gobject));

  if (priv->youtube) {
    melo_module_unregister_browser (MELO_MODULE (gobject),
                                    "webplayer_youtube_browser");
    g_object_unref (priv->youtube);
  }

  if (priv->playlist)
    g_object_unref (priv->playlist);

  if (priv->player) {
    melo_module_unregister_player (MELO_MODULE (gobject), "webplayer_player");
    g_object_unref (priv->player);
  }

  if (priv->browser) {
    melo_module_unregister_browser (MELO_MODULE (gobject), "webplayer_browser");
    g_object_unref (priv->browser);
  }

  /* Chain up to the parent class */
  G_OBJECT_CLASS (melo_webplayer_parent_class)->finalize (gobject);
}

static void
melo_webplayer_class_init (MeloWebPlayerClass *klass)
{
  MeloModuleClass *mclass = MELO_MODULE_CLASS (klass);
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  mclass->get_info = melo_webplayer_get_info;

  /* Add custom constructed() function */
  oclass->constructed = melo_webplayer_constructed;

  /* Add custom finalize() function */
  oclass->finalize = melo_webplayer_finalize;
}

static void
melo_webplayer_init (MeloWebPlayer *self)
{
  MeloWebPlayerPrivate *priv = melo_webplayer_get_instance_private (self);

  self->priv = priv;
  priv->browser = melo_browser_new (MELO_TYPE_BROWSER_WEBPLAYER,
                                    "webplayer_browser");
  priv->player = melo_player_new (MELO_TYPE_PLAYER_WEBPLAYER,
                                  "webplayer_player", melo_webplayer_info.name);
  priv->playlist = melo_playlist_new (MELO_TYPE_PLAYLIST_SIMPLE,
                                      "webplayer_playlist");

  priv->youtube = melo_browser_new (MELO_TYPE_BROWSER_YOUTUBE,
                                    "webplayer_youtube_browser");

  if (!priv->browser || !priv->youtube || !priv->player || !priv->playlist)
    return;

  /* Setup playlist with playable and removable */
  g_object_set (G_OBJECT (priv->playlist), "playable", TRUE, "removable", TRUE,
                NULL);

  /* Register browser and player */
  melo_module_register_browser (MELO_MODULE (self), priv->browser);
  melo_module_register_player (MELO_MODULE (self), priv->player);

  /* Register other browsers */
  melo_module_register_browser (MELO_MODULE (self), priv->youtube);

  /* Create links between browsers, player and playlist */
  melo_browser_set_player (priv->browser, priv->player);
  melo_player_set_playlist (priv->player, priv->playlist);
  melo_playlist_set_player (priv->playlist, priv->player);

  /* Create links with player on other browsers */
  melo_browser_set_player (priv->youtube, priv->player);
}

static void
melo_webplayer_constructed (GObject *gobject)
{
  MeloWebPlayerPrivate *priv = melo_webplayer_get_instance_private(
                                                      MELO_WEBPLAYER (gobject));
  gchar *path;

  /* Set player binary path */
  path = melo_module_build_path (MELO_MODULE (gobject), "bin");
  melo_player_webplayer_set_bin_path (MELO_PLAYER_WEBPLAYER (priv->player),
                                      path);
  g_free (path);

  /* Update player binaries */
  melo_player_webplayer_update_grabber (MELO_PLAYER_WEBPLAYER (priv->player));

  /* Chain up to the parent class */
  G_OBJECT_CLASS (melo_webplayer_parent_class)->constructed (gobject);
}

static const MeloModuleInfo *
melo_webplayer_get_info (MeloModule *module)
{
  return &melo_webplayer_info;
}
