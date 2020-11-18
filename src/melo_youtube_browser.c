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

#include <melo/melo_http_client.h>
#include <melo/melo_library.h>
#include <melo/melo_playlist.h>

#define MELO_LOG_TAG "youtube_browser"
#include <melo/melo_log.h>

#include <melo/proto/browser.pb-c.h>

#include "config.h"

#include "melo_webplayer_player.h"
#include "melo_youtube_browser.h"

#define MELO_YOUTUBE_BROWSER_URL "https://www.googleapis.com/youtube/v3/"
#define MELO_YOUTUBE_BROWSER_ACTION_URL "http://www.youtube.com/watch?v="
#define MELO_YOUTUBE_BROWSER_ASSET_URL "https://i.ytimg.com/vi/"

struct _MeloYoutubeBrowser {
  GObject parent_instance;

  MeloHttpClient *client;
};

MELO_DEFINE_BROWSER (MeloYoutubeBrowser, melo_youtube_browser)

static bool melo_youtube_browser_handle_request (
    MeloBrowser *browser, const MeloMessage *msg, MeloRequest *req);
static char *melo_youtube_browser_get_asset (
    MeloBrowser *browser, const char *id);

static void
melo_youtube_browser_finalize (GObject *object)
{
  MeloYoutubeBrowser *browser = MELO_YOUTUBE_BROWSER (object);

  /* Release HTTP client */
  g_object_unref (browser->client);

  /* Chain finalize */
  G_OBJECT_CLASS (melo_youtube_browser_parent_class)->finalize (object);
}

static void
melo_youtube_browser_class_init (MeloYoutubeBrowserClass *klass)
{
  MeloBrowserClass *parent_class = MELO_BROWSER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  /* Setup callbacks */
  parent_class->handle_request = melo_youtube_browser_handle_request;
  parent_class->get_asset = melo_youtube_browser_get_asset;

  /* Set finalize */
  object_class->finalize = melo_youtube_browser_finalize;
}

static void
melo_youtube_browser_init (MeloYoutubeBrowser *self)
{
  /* Create new HTTP client */
  self->client = melo_http_client_new (NULL);
}

MeloYoutubeBrowser *
melo_youtube_browser_new ()
{
  return g_object_new (MELO_TYPE_YOUTUBE_BROWSER, "id", MELO_YOUTUBE_BROWSER_ID,
      "name", "Youtube", "description",
      "Navigate though all videos from Youtube", "icon", "fab:youtube",
      "support-search", true, NULL);
}

static const char *
melo_youtube_browser_get_cover (JsonObject *obj)
{
  JsonObject *thumb;
  const char *url;

  /* Get thumbnail */
  obj = json_object_get_object_member (obj, "thumbnails");
  if (!obj)
    return NULL;

  /* Find best thumbnail
   *  - default: 120,
   *  - medium: 320,
   *  - high: 480,
   *  - standard: 640,
   *  - maxres: 1280.
   */
  if (json_object_has_member (obj, "medium"))
    thumb = json_object_get_object_member (obj, "medium");
  else if (json_object_has_member (obj, "high"))
    thumb = json_object_get_object_member (obj, "high");
  else if (json_object_has_member (obj, "default"))
    thumb = json_object_get_object_member (obj, "default");
  else if (json_object_has_member (obj, "standard"))
    thumb = json_object_get_object_member (obj, "standard");
  else if (json_object_has_member (obj, "maxres"))
    thumb = json_object_get_object_member (obj, "maxres");
  else
    return NULL;

  /* Get thumbnail URL */
  url = json_object_get_string_member (thumb, "url");

  /* Remove prefix */
  if (!g_str_has_prefix (url, MELO_YOUTUBE_BROWSER_ASSET_URL))
    return NULL;

  return url + sizeof (MELO_YOUTUBE_BROWSER_ASSET_URL) - 1;
}

static void
list_cb (MeloHttpClient *client, JsonNode *node, void *user_data)
{
  static Browser__SortMenu__Item sort_menu_items[5] = {
      {.base = PROTOBUF_C_MESSAGE_INIT (&browser__sort_menu__item__descriptor),
          .id = "relevance",
          .name = "Relevance"},
      {
          .base =
              PROTOBUF_C_MESSAGE_INIT (&browser__sort_menu__item__descriptor),
          .id = "title",
          .name = "Title",
      },
      {
          .base =
              PROTOBUF_C_MESSAGE_INIT (&browser__sort_menu__item__descriptor),
          .id = "date",
          .name = "date",
      },
      {
          .base =
              PROTOBUF_C_MESSAGE_INIT (&browser__sort_menu__item__descriptor),
          .id = "rating",
          .name = "rating",
      },
      {
          .base =
              PROTOBUF_C_MESSAGE_INIT (&browser__sort_menu__item__descriptor),
          .id = "viewCount",
          .name = "View",
      },
  };
  static Browser__SortMenu__Item *sort_menu_items_ptr[5] = {
      &sort_menu_items[0],
      &sort_menu_items[1],
      &sort_menu_items[2],
      &sort_menu_items[3],
      &sort_menu_items[4],
  };
  static Browser__SortMenu sort_menus = {
      .base = PROTOBUF_C_MESSAGE_INIT (&browser__sort_menu__descriptor),
      .n_items = G_N_ELEMENTS (sort_menu_items_ptr),
      .items = sort_menu_items_ptr,
  };
  static Browser__SortMenu *sort_menus_ptr[1] = {
      &sort_menus,
  };
  static Browser__Action actions[] = {
      {
          .base = PROTOBUF_C_MESSAGE_INIT (&browser__action__descriptor),
          .type = BROWSER__ACTION__TYPE__PLAY,
          .name = "Play video",
          .icon = "fa:play",
      },
      {
          .base = PROTOBUF_C_MESSAGE_INIT (&browser__action__descriptor),
          .type = BROWSER__ACTION__TYPE__ADD,
          .name = "Add video to playlist",
          .icon = "fa:plus",
      },
      {
          .base = PROTOBUF_C_MESSAGE_INIT (&browser__action__descriptor),
          .type = BROWSER__ACTION__TYPE__SET_FAVORITE,
          .name = "Add video to favorites",
          .icon = "fa:star",
      },
      {
          .base = PROTOBUF_C_MESSAGE_INIT (&browser__action__descriptor),
          .type = BROWSER__ACTION__TYPE__UNSET_FAVORITE,
          .name = "Remove video from favorites",
          .icon = "fa:star",
      },
  };
  static Browser__Action *actions_ptr[] = {
      &actions[0],
      &actions[1],
      &actions[2],
      &actions[3],
  };
  static uint32_t set_fav_actions[] = {0, 1, 2};
  static uint32_t unset_fav_actions[] = {0, 1, 3};
  MeloRequest *req = user_data;

  /* Make media list response from JSON node */
  if (node) {
    Browser__Response resp = BROWSER__RESPONSE__INIT;
    Browser__Response__MediaList media_list =
        BROWSER__RESPONSE__MEDIA_LIST__INIT;
    Browser__Response__MediaItem **items_ptr;
    Browser__Response__MediaItem *items;
    Tags__Tags *tags;
    MeloMessage *msg;
    JsonArray *array;
    JsonObject *obj;
    unsigned int i, count;
    char *order;

    /* Set response type */
    resp.resp_case = BROWSER__RESPONSE__RESP_MEDIA_LIST;
    resp.media_list = &media_list;

    /* Set media sort menu */
    media_list.n_sort_menus = G_N_ELEMENTS (sort_menus_ptr);
    media_list.sort_menus = sort_menus_ptr;

    /* Set effective sort */
    order = melo_request_get_user_data (req);
    media_list.n_sort = 1;
    media_list.sort = &order;

    /* Get object */
    obj = json_node_get_object (node);

    /* Get list tokens */
    if (json_object_has_member (obj, "prevPageToken"))
      media_list.prev_token =
          (char *) json_object_get_string_member (obj, "prevPageToken");
    if (json_object_has_member (obj, "nextPageToken"))
      media_list.next_token =
          (char *) json_object_get_string_member (obj, "nextPageToken");

    /* Get items array */
    array = json_object_get_array_member (obj, "items");
    count = json_array_get_length (array);

    /* Allocate item list */
    items_ptr = malloc (sizeof (*items_ptr) * count);
    items = malloc (sizeof (*items) * count);
    tags = malloc (sizeof (*tags) * count);

    /* Set item list */
    media_list.n_items = count;
    media_list.items = items_ptr;

    /* Set list count */
    media_list.count = count;

    /* Set actions */
    media_list.n_actions = G_N_ELEMENTS (actions_ptr);
    media_list.actions = actions_ptr;

    /* Add media items */
    for (i = 0; i < count; i++) {
      JsonObject *o, *id, *snip;
      const char *cover;
      uint64_t media_id;

      /* Init media item */
      browser__response__media_item__init (&items[i]);
      tags__tags__init (&tags[i]);
      media_list.items[i] = &items[i];

      /* Get next entry */
      o = json_array_get_object_element (array, i);
      if (!o)
        continue;

      /* Get ID and snippet objects */
      id = json_object_get_object_member (o, "id");
      snip = json_object_get_object_member (o, "snippet");
      if (!id || !snip)
        continue;

      /* Set media */
      items[i].id = (char *) json_object_get_string_member (id, "videoId");
      items[i].name = (char *) json_object_get_string_member (snip, "title");
      items[i].type = BROWSER__RESPONSE__MEDIA_ITEM__TYPE__MEDIA;

      /* Set favorite and action IDs */
      media_id = melo_library_get_media_id_from_browser (
          MELO_YOUTUBE_BROWSER_ID, items[i].id);
      items[i].favorite =
          melo_library_media_get_flags (media_id) & MELO_LIBRARY_FLAG_FAVORITE;
      if (items[i].favorite) {
        items[i].n_action_ids = G_N_ELEMENTS (unset_fav_actions);
        items[i].action_ids = unset_fav_actions;
      } else {
        items[i].n_action_ids = G_N_ELEMENTS (set_fav_actions);
        items[i].action_ids = set_fav_actions;
      }

      /* Set tags */
      items[i].tags = &tags[i];

      /* Set title */
      tags[i].title = (char *) json_object_get_string_member (snip, "title");

      /* Set cover */
      cover = melo_youtube_browser_get_cover (snip);
      if (cover && *cover != '\0')
        tags[i].cover =
            melo_tags_gen_cover (melo_request_get_object (req), cover);
    }

    /* Pack message */
    msg = melo_message_new (browser__response__get_packed_size (&resp));
    melo_message_set_size (
        msg, browser__response__pack (&resp, melo_message_get_data (msg)));

    /* Free tags cover */
    for (i = 0; i < count; i++)
      if (tags[i].cover != protobuf_c_empty_string)
        g_free (tags[i].cover);

    /* Free item list */
    free (items_ptr);
    free (items);
    free (tags);
    free (order);

    /* Send media list response */
    melo_request_send_response (req, msg);
  }

  /* Release request */
  melo_request_complete (req);
}

static bool
melo_youtube_browser_get_media_list (MeloYoutubeBrowser *browser,
    Browser__Request__GetMediaList *r, MeloRequest *req)
{
  const char *query = r->query;
  const char *token, *order;
  char *url;
  bool ret;

  /* Limit results count */
  if (r->count > 25)
    r->count = 25;

  /* Support only search for now */
  if (!g_str_has_prefix (r->query, "search:"))
    return false;
  query += 7;

  /* Generate pageToken query */
  token = r->token && *r->token != '\0' ? r->token : "";

  /* Set order type */
  if (r->sort && r->n_sort) {
    order = r->sort[0];
  } else
    order = "relevance";

  /* Save order */
  melo_request_set_user_data (req, g_strdup (order));

  /* Create search URL */
  url = g_strdup_printf (MELO_YOUTUBE_BROWSER_URL
      "search?"
      "part=snippet"
      "&q=%s"
      "&maxResults=%d"
      "%s%s"
      "&type=video"
      "&order=%s"
      "&key=" MELO_YOUTUBE_BROWSER_API_KEY,
      query, r->count, token ? "&pageToken=" : "", token, order);

  /* Get list from URL */
  ret = melo_http_client_get_json (browser->client, url, list_cb, req);
  g_free (url);

  return ret;
}

static void
action_cb (MeloHttpClient *client, JsonNode *node, void *user_data)
{
  MeloRequest *req = user_data;
  JsonArray *array;
  JsonObject *obj;
  const char *id;

  /* Extract video tags from JSON node */
  if (!node)
    goto end;

  /* Get object */
  obj = json_node_get_object (node);
  if (!obj)
    goto end;

  /* Get array */
  array = json_object_get_array_member (obj, "items");
  if (!array || json_array_get_length (array) < 1)
    goto end;

  /* Get first object */
  obj = json_array_get_object_element (array, 0);
  if (!obj)
    goto end;

  /* Get video ID */
  id = json_object_get_string_member (obj, "id");
  if (id) {
    Browser__Action__Type type;
    MeloTags *tags = NULL;
    const char *name = NULL;
    char *url;

    /* Generate URL */
    url = g_strconcat (MELO_YOUTUBE_BROWSER_ACTION_URL, id, NULL);

    /* Extract tags */
    obj = json_object_get_object_member (obj, "snippet");
    if (obj) {
      /* Create tags */
      tags = melo_tags_new ();
      if (tags) {
        /* Set title */
        if (json_object_has_member (obj, "title")) {
          name = json_object_get_string_member (obj, "title");
          melo_tags_set_title (tags, name);
          melo_tags_set_browser (tags, MELO_YOUTUBE_BROWSER_ID);
          melo_tags_set_media_id (tags, id);
        }

        /* Set cover */
        melo_tags_set_cover (tags, melo_request_get_object (req),
            melo_youtube_browser_get_cover (obj));
      }
    } else
      name = id;

    MELO_LOGD ("play video '%s': %s", name, url);

    /* Get action type */
    type = (uintptr_t) melo_request_get_user_data (req);

    /* Do action */
    if (type == BROWSER__ACTION__TYPE__PLAY)
      melo_playlist_play_media (MELO_WEBPLAYER_PLAYER_ID, url, name, tags);
    else if (type == BROWSER__ACTION__TYPE__ADD)
      melo_playlist_add_media (MELO_WEBPLAYER_PLAYER_ID, url, name, tags);
    else {
      char *path, *media;

      /* Separate path */
      path = g_strdup (url);
      media = strrchr (path, '/');
      if (media)
        *media++ = '\0';

      /* Set / unset favorite marker */
      if (type == BROWSER__ACTION__TYPE__UNSET_FAVORITE) {
        uint64_t id;

        /* Get media ID */
        id = melo_library_get_media_id (
            MELO_WEBPLAYER_PLAYER_ID, 0, path, 0, media);

        /* Unset favorite */
        melo_library_update_media_flags (
            id, MELO_LIBRARY_FLAG_FAVORITE_ONLY, true);
      } else if (type == BROWSER__ACTION__TYPE__SET_FAVORITE)
        /* Set favorite */
        melo_library_add_media (MELO_WEBPLAYER_PLAYER_ID, 0, path, 0, media, 0,
            MELO_LIBRARY_SELECT (COVER), name, tags, 0,
            MELO_LIBRARY_FLAG_FAVORITE_ONLY);

      /* Free resources */
      g_free (path);
      melo_tags_unref (tags);
    }

    /* Free URL */
    g_free (url);
  }

end:
  /* Release request */
  melo_request_complete (req);
}

static bool
melo_youtube_browser_do_action (MeloYoutubeBrowser *browser,
    Browser__Request__DoAction *r, MeloRequest *req)
{
  const char *path = r->path;
  char *url;
  bool ret;

  /* Check action type */
  if (r->type != BROWSER__ACTION__TYPE__PLAY &&
      r->type != BROWSER__ACTION__TYPE__ADD &&
      r->type != BROWSER__ACTION__TYPE__SET_FAVORITE &&
      r->type != BROWSER__ACTION__TYPE__UNSET_FAVORITE)
    return false;

  /* Support only search for now */
  if (!g_str_has_prefix (path, "search:"))
    return false;
  path += 7;

  /* Save action type in request */
  melo_request_set_user_data (req, (void *) r->type);

  /* Generate URL from path */
  url = g_strdup_printf (MELO_YOUTUBE_BROWSER_URL
      "videos?"
      "part=snippet"
      "&id=%s"
      "&key=" MELO_YOUTUBE_BROWSER_API_KEY,
      path);

  /* Get radio URL from sparod */
  ret = melo_http_client_get_json (browser->client, url, action_cb, req);
  g_free (url);

  return ret;
}

static bool
melo_youtube_browser_handle_request (
    MeloBrowser *browser, const MeloMessage *msg, MeloRequest *req)
{
  MeloYoutubeBrowser *ybrowser = MELO_YOUTUBE_BROWSER (browser);
  Browser__Request *r;
  bool ret = false;

  /* Unpack request */
  r = browser__request__unpack (
      NULL, melo_message_get_size (msg), melo_message_get_cdata (msg, NULL));
  if (!r) {
    MELO_LOGE ("failed to unpack request");
    return false;
  }

  /* Handle request */
  switch (r->req_case) {
  case BROWSER__REQUEST__REQ_GET_MEDIA_LIST:
    ret =
        melo_youtube_browser_get_media_list (ybrowser, r->get_media_list, req);
    break;
  case BROWSER__REQUEST__REQ_DO_ACTION:
    ret = melo_youtube_browser_do_action (ybrowser, r->do_action, req);
    break;
  default:
    MELO_LOGE ("request %u not supported", r->req_case);
  }

  /* Free request */
  browser__request__free_unpacked (r, NULL);

  return ret;
}

static char *
melo_youtube_browser_get_asset (MeloBrowser *browser, const char *id)
{
  return g_strconcat (MELO_YOUTUBE_BROWSER_ASSET_URL, id, NULL);
}
