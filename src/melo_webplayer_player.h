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

#ifndef _MELO_WEBPLAYER_PLAYER_H_
#define _MELO_WEBPLAYER_PLAYER_H_

#include <melo/melo_player.h>

G_BEGIN_DECLS

#define MELO_WEBPLAYER_PLAYER_ID "com.sparod.webplayer.player"

#define MELO_TYPE_WEBPLAYER_PLAYER melo_webplayer_player_get_type ()
MELO_DECLARE_PLAYER (
    MeloWebplayerPlayer, melo_webplayer_player, WEBPLAYER_PLAYER)

/**
 * Create a new webplayer player.
 *
 * @return the newly webplayer player or NULL.
 */
MeloWebplayerPlayer *melo_webplayer_player_new (void);

G_END_DECLS

#endif /* !_MELO_WEBPLAYER_PLAYER_H_ */
