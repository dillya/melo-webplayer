/*
 * melo_browser_youtube.c: Youtube browser using Youtube API v3
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

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "melo_browser_youtube.h"

#define MELO_BROWSER_YOUTUBE_API_KEY "AIzaSyBPdeLGcRRWDBZqdk1NiawGnfkvukjjCd8"

/* Youtube browser info */
static MeloBrowserInfo melo_browser_youtube_info = {
  .name = "Browse Youtube",
  .description = "Navigate though all videos from Youtube",
  /* Search feature */
  .search_support = TRUE,
  .search_input_text = "Search...",
  .search_button_text = "Go",
};

static const MeloBrowserInfo *melo_browser_youtube_get_info (
                                                          MeloBrowser *browser);
static MeloBrowserList *melo_browser_youtube_search (MeloBrowser *browser,
                                                  const gchar *path,
                                                  gint offset, gint count,
                                                  MeloBrowserTagsMode tags_mode,
                                                  MeloTagsFields tags_fields);
static gboolean melo_browser_youtube_play (MeloBrowser *browser,
                                           const gchar *path);
static gboolean melo_browser_youtube_add (MeloBrowser *browser,
                                          const gchar *path);

struct _MeloBrowserYoutubePrivate {
  GMutex mutex;
  SoupSession *session;
};

G_DEFINE_TYPE_WITH_PRIVATE (MeloBrowserYoutube, melo_browser_youtube, MELO_TYPE_BROWSER)

static void
melo_browser_youtube_finalize (GObject *gobject)
{
  MeloBrowserYoutube *byoutube = MELO_BROWSER_YOUTUBE (gobject);
  MeloBrowserYoutubePrivate *priv =
                           melo_browser_youtube_get_instance_private (byoutube);

  /* Free Soup session */
  g_object_unref (priv->session);

  /* Clear mutex */
  g_mutex_clear (&priv->mutex);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (melo_browser_youtube_parent_class)->finalize (gobject);
}

static void
melo_browser_youtube_class_init (MeloBrowserYoutubeClass *klass)
{
  MeloBrowserClass *bclass = MELO_BROWSER_CLASS (klass);
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  bclass->get_info = melo_browser_youtube_get_info;
  bclass->search = melo_browser_youtube_search;
  bclass->play = melo_browser_youtube_play;
  bclass->add = melo_browser_youtube_add;

  /* Add custom finalize() function */
  oclass->finalize = melo_browser_youtube_finalize;
}

static void
melo_browser_youtube_init (MeloBrowserYoutube *self)
{
  MeloBrowserYoutubePrivate *priv =
                               melo_browser_youtube_get_instance_private (self);

  self->priv = priv;

  /* Init mutex */
  g_mutex_init (&priv->mutex);

  /* Create a new Soup session */
  priv->session = soup_session_new_with_options (NULL);
}

static const MeloBrowserInfo *
melo_browser_youtube_get_info (MeloBrowser *browser)
{
  return &melo_browser_youtube_info;
}

static MeloBrowserList *
melo_browser_youtube_search (MeloBrowser *browser, const gchar *path,
                             gint offset, gint count,
                             MeloBrowserTagsMode tags_mode,
                             MeloTagsFields tags_fields)
{
  MeloBrowserYoutube *byoutube = MELO_BROWSER_YOUTUBE (browser);
  MeloBrowserYoutubePrivate  *priv = byoutube->priv;
  static MeloBrowserList *list;
  GInputStream *stream;
  SoupMessage *msg;
  JsonParser *parser;
  JsonNode *node;
  JsonArray *array;
  JsonObject *obj;
  gchar *api_key = MELO_BROWSER_YOUTUBE_API_KEY;
  gchar *type = "video";
  gchar *url;
  gint i;

  /* Create browser list */
  list = melo_browser_list_new (NULL);
  if (!list)
    return NULL;

  /* Limit results count */
  if (count > 50)
    count = 50;

  /* Generate URL */
  url = g_strdup_printf ("https://www.googleapis.com/youtube/v3/search?"
                         "part=snippet&"
                         "q=%s&"
                         "maxResults=%d&"
                         "type=%s&"
                         "key=%s",
                         path, count, type, api_key);

  /* Create request */
  msg = soup_message_new ("GET", url);
  g_free (url);

  /* Send message and wait answer */
  stream = soup_session_send (priv->session, msg, NULL, NULL);
  if (!stream)
    goto bad_request;

  /* Bad status */
  if (msg->status_code != 200)
    goto bad_status;

  /* Parse JSON */
  parser = json_parser_new ();
  if (!json_parser_load_from_stream (parser, stream, NULL, NULL))
    goto bad_json;

  /* Get root node and check its type */
  node = json_parser_get_root (parser);
  if (!node || json_node_get_node_type (node) != JSON_NODE_OBJECT)
    goto bad_json;

  /* Get root object */
  obj = json_node_get_object (node);

  /* Get items array */
  array = json_object_get_array_member (obj, "items");
  count = json_array_get_length (array);
  for (i = 0; i < count; i ++) {
    const gchar *name, *full_name;
    MeloBrowserItem *item;
    JsonObject *o, *id, *snip;

    /* Get next entry */
    o = json_array_get_object_element (array, i);
    if (!o)
      continue;
    id = json_object_get_object_member (o, "id");
    snip = json_object_get_object_member (o, "snippet");
    if (!id || !snip)
      continue;

    /* Get name, full_name and type */
    name = json_object_get_string_member (id, "videoId");
    full_name = json_object_get_string_member (snip, "title");

    /* Generate new item */
    item = melo_browser_item_new (name, "video");
    item->full_name = full_name ? g_strdup (full_name) : g_strdup ("Unknown");
    item->add = g_strdup ("Add to playlist");

    /* Add item to list */
    list->items = g_list_prepend (list->items, item);
  }

  /* Reverse list */
  list->items = g_list_reverse (list->items);

  /* Free objects */
  g_object_unref (parser);
  g_object_unref (stream);
  g_object_unref (msg);

  return list;

bad_json:
  g_object_unref (parser);
bad_status:
  g_object_unref (stream);
bad_request:
  g_object_unref (msg);
  return NULL;
}

static gchar *
melo_browser_youtube_get_url (MeloBrowser *browser, const gchar *path)
{
  return g_strdup_printf ("http://www.youtube.com/watch?v=%s", path);
}

static gboolean
melo_browser_youtube_play (MeloBrowser *browser, const gchar *path)
{
  MeloBrowserYoutube *byoutube = MELO_BROWSER_YOUTUBE (browser);
  gboolean ret;
  gchar *url;

  /* Get final URL from browser path */
  url = melo_browser_youtube_get_url (browser, path);

  /* Play youtube video */
  ret = melo_player_play (browser->player, url, NULL, NULL, TRUE);
  g_free (url);

  return ret;
}

static gboolean
melo_browser_youtube_add (MeloBrowser *browser, const gchar *path)
{
  MeloBrowserYoutube *byoutube = MELO_BROWSER_YOUTUBE (browser);
  gboolean ret;
  gchar *url;

  /* Get final URL from browser path */
  url = melo_browser_youtube_get_url (browser, path);

  /* Add youtube video */
  ret = melo_player_add (browser->player, url, NULL, NULL);
  g_free (url);

  return ret;
}
