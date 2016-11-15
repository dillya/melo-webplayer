/*
 * melo_player_webplayer.h: Web based Player player using Gstreamer and
 *                          youtube-dl utility
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

#ifndef __MELO_PLAYER_WEBPLAYER_H__
#define __MELO_PLAYER_WEBPLAYER_H__

#include "melo_player.h"

G_BEGIN_DECLS

#define MELO_TYPE_PLAYER_WEBPLAYER             (melo_player_webplayer_get_type ())
#define MELO_PLAYER_WEBPLAYER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), MELO_TYPE_PLAYER_WEBPLAYER, MeloPlayerWebPlayer))
#define MELO_IS_PLAYER_WEBPLAYER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MELO_TYPE_PLAYER_WEBPLAYER))
#define MELO_PLAYER_WEBPLAYER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), MELO_TYPE_PLAYER_WEBPLAYER, MeloPlayerWebPlayerClass))
#define MELO_IS_PLAYER_WEBPLAYER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), MELO_TYPE_PLAYER_WEBPLAYER))
#define MELO_PLAYER_WEBPLAYER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), MELO_TYPE_PLAYER_WEBPLAYER, MeloPlayerWebPlayerClass))

typedef struct _MeloPlayerWebPlayer MeloPlayerWebPlayer;
typedef struct _MeloPlayerWebPlayerClass MeloPlayerWebPlayerClass;
typedef struct _MeloPlayerWebPlayerPrivate MeloPlayerWebPlayerPrivate;

struct _MeloPlayerWebPlayer {
  MeloPlayer parent_instance;

  /*< private >*/
  MeloPlayerWebPlayerPrivate *priv;
};

struct _MeloPlayerWebPlayerClass {
  MeloPlayerClass parent_class;
};

GType melo_player_webplayer_get_type (void);

void melo_player_webplayer_set_bin_path (MeloPlayerWebPlayer *webp,
                                         const gchar *path);
gboolean melo_player_webplayer_update_grabber (MeloPlayerWebPlayer *webp);

G_END_DECLS

#endif /* __MELO_PLAYER_WEBPLAYER_H__ */
