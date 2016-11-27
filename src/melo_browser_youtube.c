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

#define MELO_BROWSER_YOUTUBE_THUMB_HOST "ytimg.com"
#define MELO_BROWSER_YOUTUBE_THUMB_URL "https://i.ytimg.com/"
#define MELO_BROWSER_YOUTUBE_THUMB_TYPE "image/jpeg"

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
                                                  const gchar *token,
                                                  MeloBrowserTagsMode tags_mode,
                                                  MeloTagsFields tags_fields);
static gboolean melo_browser_youtube_play (MeloBrowser *browser,
                                           const gchar *path);
static gboolean melo_browser_youtube_add (MeloBrowser *browser,
                                          const gchar *path);

static gboolean melo_browser_youtube_get_cover (MeloBrowser *browser,
                                                const gchar *path,
                                                GBytes **data, gchar **type);

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

  bclass->get_cover = melo_browser_youtube_get_cover;

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

static JsonObject *
melo_browser_youtube_get_json_object (MeloBrowserYoutube *byoutube,
                                      const gchar *url)
{
  MeloBrowserYoutubePrivate *priv = byoutube->priv;
  SoupMessage *msg;
  GInputStream *stream;
  JsonParser *parser;
  JsonNode *node;
  JsonObject *obj = NULL;

  /* Create request */
  msg = soup_message_new ("GET", url);
  if (!msg)
    return NULL;

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

  /* Get object */
  obj = json_object_ref (json_node_get_object (node));

bad_json:
  g_object_unref (parser);
bad_status:
  g_object_unref (stream);
bad_request:
  g_object_unref (msg);

  return obj;
}

static MeloTags *
melo_browser_youtube_gen_tags (MeloBrowserYoutube *byoutube, JsonObject *obj,
                               MeloTagsFields fields)
{
  const gchar *thumb = NULL;
  JsonObject *o;
  MeloTags *tags;

  /* Create a new MeloTags */
  tags = melo_tags_new ();
  if (!tags)
    return NULL;

  /* Add video title */
  if (fields & MELO_TAGS_FIELDS_TITLE)
    tags->title = g_strdup (json_object_get_string_member (obj, "title"));

  /* Get thumbnail */
  o = json_object_get_object_member (obj, "thumbnails");
  if (o) {
    JsonObject *t;

    /* Find best thumbnail */
    if (json_object_has_member (o, "standard"))
      t = json_object_get_object_member (o, "standard");
    else if (json_object_has_member (o, "high"))
      t = json_object_get_object_member (o, "high");
    else if (json_object_has_member (o, "medium"))
      t = json_object_get_object_member (o, "medium");
    else if (json_object_has_member (o, "default"))
      t = json_object_get_object_member (o, "default");
    else if (json_object_has_member (o, "maxres"))
      t = json_object_get_object_member (o, "maxres");

    /* Get thumbnail URL */
    if (t)
      thumb = json_object_get_string_member (t, "url");
  }

  /* Add thumbnail to MeloTags */
  if (thumb) {
    /* Add as cover URL */
    if (fields & MELO_TAGS_FIELDS_COVER_URL) {
      const gchar *path;

      /* Get image path */
      path = strstr (thumb, MELO_BROWSER_YOUTUBE_THUMB_HOST);
      if (path)
        path += sizeof (MELO_BROWSER_YOUTUBE_THUMB_HOST);

      /* Set cover URL */
      melo_tags_set_cover_url (tags, G_OBJECT (byoutube), path,
                               MELO_BROWSER_YOUTUBE_THUMB_TYPE);
    }

    /* Add as cover */
    if (fields & MELO_TAGS_FIELDS_COVER) {
      SoupMessage *msg;

      /* Prepare HTTP request */
      msg = soup_message_new ("GET", thumb);
      if (msg) {
        /* Download thumbnail */
        if (soup_session_send_message (byoutube->priv->session, msg) == 200) {
          GBytes *cover = NULL;

          /* Create cover */
          g_object_get (msg, "response-body-data", &cover, NULL);

          /* Add cover to tags */
          melo_tags_take_cover (tags, cover, MELO_BROWSER_YOUTUBE_THUMB_TYPE);
        }

        /* Free message */
        g_object_unref (msg);
      }
    }
  }

  return tags;
}

static MeloBrowserList *
melo_browser_youtube_search (MeloBrowser *browser, const gchar *path,
                             gint offset, gint count, const gchar *token,
                             MeloBrowserTagsMode tags_mode,
                             MeloTagsFields tags_fields)
{
  MeloBrowserYoutube *byoutube = MELO_BROWSER_YOUTUBE (browser);
  MeloBrowserYoutubePrivate  *priv = byoutube->priv;
  static MeloBrowserList *list;
  const gchar *page_token = "";
  JsonArray *array;
  JsonObject *obj;
  gchar *url;
  gint i;

  /* Create browser list */
  list = melo_browser_list_new (NULL);
  if (!list)
    return NULL;

  /* Limit results count */
  if (count > 50)
    count = 50;

  /* Generate pageToken query */
  if (token && *token != '\0')
    page_token = "&pageToken=";
  else
    token = "";

  /* Generate URL */
  url = g_strdup_printf ("https://www.googleapis.com/youtube/v3/search?"
                         "part=snippet"
                         "&q=%s"
                         "&maxResults=%d"
                         "%s%s"
                         "&type=video"
                         "&key=" MELO_BROWSER_YOUTUBE_API_KEY,
                         path, count, page_token, token);

  /* Get JSON response object */
  obj = melo_browser_youtube_get_json_object (byoutube, url);
  g_free (url);

  /* No videos found */
  if (!obj)
    return list;

  /* Get list details */
  if (json_object_has_member (obj, "nextPageToken"))
    list->next_token = g_strdup (json_object_get_string_member (obj,
                                                              "nextPageToken"));
  if (json_object_has_member (obj, "prevPageToken"))
    list->prev_token = g_strdup (json_object_get_string_member (obj,
                                                              "prevPageToken"));
  if (json_object_has_member (obj, "pageInfo")) {
    JsonObject *o;

    /* Get page info */
    o = json_object_get_object_member (obj, "pageInfo");
    if (o)
      list->count = json_object_get_int_member (o, "totalResults");
  }

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

    /* Generate MeloTags */
    if (tags_mode != MELO_BROWSER_TAGS_MODE_NONE)
      item->tags = melo_browser_youtube_gen_tags (byoutube, snip, tags_fields);

    /* Add item to list */
    list->items = g_list_prepend (list->items, item);
  }

  /* Reverse list */
  list->items = g_list_reverse (list->items);

  /* Free object */
  json_object_unref (obj);

  return list;
}

static gchar *
melo_browser_youtube_get_url (MeloBrowser *browser, const gchar *path)
{
  return g_strdup_printf ("http://www.youtube.com/watch?v=%s", path);
}

static MeloTags *
melo_browser_youtube_get_video_tags (MeloBrowserYoutube *byoutube,
                                     const gchar *id, gchar **title,
                                     MeloTagsFields tags_fields)
{
  MeloBrowserYoutubePrivate  *priv = byoutube->priv;
  MeloTags *tags = NULL;
  JsonObject *obj;
  gchar *url;

  /* Generate URL */
  url = g_strdup_printf ("https://www.googleapis.com/youtube/v3/videos?"
                         "part=snippet&"
                         "id=%s&"
                         "key=" MELO_BROWSER_YOUTUBE_API_KEY,
                         id);

  /* Get JSON response object */
  obj = melo_browser_youtube_get_json_object (byoutube, url);
  g_free (url);

  /* Parse JSON object */
  if (obj) {
    JsonArray *array;

    /* Get first result */
    array = json_object_get_array_member (obj, "items");
    if (array && json_array_get_length (array) > 0) {
      JsonObject *o;

      /* Get first object */
      o = json_array_get_object_element (array, 0);
      if (o) {
        JsonObject *snip;

        /* Get snippet */
        snip = json_object_get_object_member (o, "snippet");
        if (snip) {
          /* Get title video */
          if (title && json_object_has_member (snip, "title"))
            *title = g_strdup (json_object_get_string_member (snip, "title"));

          /* Generate MeloTags from object */
          tags = melo_browser_youtube_gen_tags (byoutube, snip, tags_fields);
        }
      }
    }

    /* Free object */
    json_object_unref (obj);
  }

  return tags;
}

static gboolean
melo_browser_youtube_play (MeloBrowser *browser, const gchar *path)
{
  MeloBrowserYoutube *byoutube = MELO_BROWSER_YOUTUBE (browser);
  gchar *title = NULL;
  MeloTags *tags;
  gboolean ret;
  gchar *url;

  /* Get final URL from browser path */
  url = melo_browser_youtube_get_url (browser, path);

  /* Get video tags */
  tags = melo_browser_youtube_get_video_tags (byoutube, path, &title,
                                              MELO_TAGS_FIELDS_FULL);

  /* Play youtube video */
  ret = melo_player_play (browser->player, url, title, tags,
                          TRUE);
  melo_tags_unref (tags);
  g_free (title);
  g_free (url);

  return ret;
}

static gboolean
melo_browser_youtube_add (MeloBrowser *browser, const gchar *path)
{
  MeloBrowserYoutube *byoutube = MELO_BROWSER_YOUTUBE (browser);
  gchar *title = NULL;
  MeloTags *tags;
  gboolean ret;
  gchar *url;

  /* Get final URL from browser path */
  url = melo_browser_youtube_get_url (browser, path);

  /* Get video tags */
  tags = melo_browser_youtube_get_video_tags (byoutube, path, &title,
                                              MELO_TAGS_FIELDS_FULL);

  /* Add youtube video */
  ret = melo_player_add (browser->player, url, title, tags);
  melo_tags_unref (tags);
  g_free (title);
  g_free (url);

  return ret;
}

static gboolean
melo_browser_youtube_get_cover (MeloBrowser *browser, const gchar *path,
                                GBytes **data, gchar **type)
{
  MeloBrowserYoutubePrivate *priv = (MELO_BROWSER_YOUTUBE (browser))->priv;
  SoupMessage *msg;
  gchar *url;

  /* Generate URL */
  url = g_strdup_printf (MELO_BROWSER_YOUTUBE_THUMB_URL "%s", path);
  if (url) {
    /* Prepare HTTP request */
    msg = soup_message_new ("GET", url);
    if (msg) {
      /* Download logo */
      if (soup_session_send_message (priv->session, msg) == 200) {
        g_object_get (msg, "response-body-data", data, NULL);
        *type = g_strdup (MELO_BROWSER_YOUTUBE_THUMB_TYPE);
      }

      /* Free message */
      g_object_unref (msg);
    }

    /* Free URL */
    g_free (url);
  }

  return TRUE;
}
