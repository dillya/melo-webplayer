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

/* Module WebPlayer info */
static MeloModuleInfo melo_webplayer_info = {
  .name = "WebPlayer",
  .description = "Play any media from website like Youtube or Dailymotion",
  .config_id = "webplayer",
};

static const MeloModuleInfo *melo_webplayer_get_info (MeloModule *module);

struct _MeloWebPlayerPrivate {
  MeloBrowser *browser;
  MeloPlayer *player;
};

G_DEFINE_TYPE_WITH_PRIVATE (MeloWebPlayer, melo_webplayer, MELO_TYPE_MODULE)

static void
melo_webplayer_finalize (GObject *gobject)
{
  MeloWebPlayerPrivate *priv =
                 melo_webplayer_get_instance_private (MELO_WEBPLAYER (gobject));

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
                                  "webplayer_player");

  if (!priv->browser || !priv->player)
    return;

  /* Register browser and player */
  melo_module_register_browser (MELO_MODULE (self), priv->browser);
  melo_module_register_player (MELO_MODULE (self), priv->player);

  /* Create links between browser and player */
  melo_browser_set_player (priv->browser, priv->player);
}

static const MeloModuleInfo *
melo_webplayer_get_info (MeloModule *module)
{
  return &melo_webplayer_info;
}
