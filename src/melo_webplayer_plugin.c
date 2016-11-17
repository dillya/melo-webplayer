/*
 * melo_webplayer_plugin.c: WebPlayer plugin for Melo
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

#include <melo_plugin.h>
#include <melo_module.h>

#include "melo_webplayer.h"

static gboolean
melo_webplayer_enable (void)
{
  return melo_module_register (MELO_TYPE_WEBPLAYER, "webplayer");
}

static gboolean
melo_webplayer_disable (void)
{
  melo_module_unregister ("webplayer");
  return TRUE;
}

G_MODULE_EXPORT
const MeloPlugin melo_plugin = {
  .name = "WebPlayer",
  .description = "Web based Player support for Melo",
  .enable = melo_webplayer_enable,
  .disable = melo_webplayer_disable,
};