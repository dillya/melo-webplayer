/*
 * melo_browser_youtube.h: Youtube browser using Youtube API v3
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

#ifndef __MELO_BROWSER_YOUTUBE_H__
#define __MELO_BROWSER_YOUTUBE_H__

#include "melo_browser.h"

G_BEGIN_DECLS

#define MELO_TYPE_BROWSER_YOUTUBE             (melo_browser_youtube_get_type ())
#define MELO_BROWSER_YOUTUBE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), MELO_TYPE_BROWSER_YOUTUBE, MeloBrowserYoutube))
#define MELO_IS_BROWSER_YOUTUBE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MELO_TYPE_BROWSER_YOUTUBE))
#define MELO_BROWSER_YOUTUBE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), MELO_TYPE_BROWSER_YOUTUBE, MeloBrowserYoutubeClass))
#define MELO_IS_BROWSER_YOUTUBE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), MELO_TYPE_BROWSER_YOUTUBE))
#define MELO_BROWSER_YOUTUBE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), MELO_TYPE_BROWSER_YOUTUBE, MeloBrowserYoutubeClass))

typedef struct _MeloBrowserYoutube MeloBrowserYoutube;
typedef struct _MeloBrowserYoutubeClass MeloBrowserYoutubeClass;
typedef struct _MeloBrowserYoutubePrivate MeloBrowserYoutubePrivate;

struct _MeloBrowserYoutube {
  MeloBrowser parent_instance;

  /*< private >*/
MeloBrowserYoutubePrivate *priv;
};

struct _MeloBrowserYoutubeClass {
  MeloBrowserClass parent_class;
};

GType melo_browser_youtube_get_type (void);

G_END_DECLS

#endif /* __MELO_BROWSER_YOUTUBE_H__ */
