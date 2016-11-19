/*
 * melo_browser_webplayer.c: Web based Player simple URL browser
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

#include "melo_browser_webplayer.h"

/* Browser WebPlayer info */
static MeloBrowserInfo melo_browser_webplayer_info = {
  .name = "Open URL",
  .description = "Open any Website (Video & Audio) URL",
  .go_support = TRUE,
  .go_play_support = TRUE,
  .go_add_support = TRUE,
  .go_input_text = "Type your URL here...",
  .go_button_play_text = "Open",
  .go_button_add_text = "Add to playlist",
};

static const MeloBrowserInfo *melo_browser_webplayer_get_info (
                                                          MeloBrowser *browser);
static gboolean melo_browser_webplayer_play (MeloBrowser *browser,
                                             const gchar *path);
static gboolean melo_browser_webplayer_add (MeloBrowser *browser,
                                            const gchar *path);

G_DEFINE_TYPE (MeloBrowserWebPlayer, melo_browser_webplayer, MELO_TYPE_BROWSER)

static void
melo_browser_webplayer_class_init (MeloBrowserWebPlayerClass *klass)
{
  MeloBrowserClass *bclass = MELO_BROWSER_CLASS (klass);
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  bclass->get_info = melo_browser_webplayer_get_info;
  bclass->play = melo_browser_webplayer_play;
  bclass->add = melo_browser_webplayer_add;
}

static void
melo_browser_webplayer_init (MeloBrowserWebPlayer *self)
{
}

static const MeloBrowserInfo *
melo_browser_webplayer_get_info (MeloBrowser *browser)
{
  return &melo_browser_webplayer_info;
}

static gboolean
melo_browser_webplayer_play (MeloBrowser *browser, const gchar *path)
{
  return melo_player_play (browser->player, path, NULL, NULL, TRUE);
}

static gboolean
melo_browser_webplayer_add (MeloBrowser *browser, const gchar *path)
{
  return melo_player_add (browser->player, path, NULL, NULL);
}
