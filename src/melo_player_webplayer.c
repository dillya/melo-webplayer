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

#include <json-glib/json-glib.h>
#include <gst/gst.h>

#include "melo_player_webplayer.h"

#define MELO_PLAYER_WEBPLAYER_GRABBER "youtube-dl"
#define MELO_PLAYER_WEBPLAYER_GRABBER_URL \
    "https://yt-dl.org/downloads/latest/youtube-dl"

static gboolean bus_call (GstBus *bus, GstMessage *msg, gpointer data);
static void pad_added_handler (GstElement *src, GstPad *pad, GstElement *sink);

static gboolean melo_player_webplayer_add (MeloPlayer *player,
                                           const gchar *path, const gchar *name,
                                           MeloTags *tags);
static gboolean melo_player_webplayer_play (MeloPlayer *player,
                                            const gchar *path,
                                            const gchar *name, MeloTags *tags,
                                            gboolean insert);
static gboolean melo_player_webplayer_prev (MeloPlayer *player);
static gboolean melo_player_webplayer_next (MeloPlayer *player);
static MeloPlayerState melo_player_webplayer_set_state (MeloPlayer *player,
                                                        MeloPlayerState state);
static gint melo_player_webplayer_set_pos (MeloPlayer *player, gint pos);

static MeloPlayerState melo_player_webplayer_get_state (MeloPlayer *player);
static gchar *melo_player_webplayer_get_name (MeloPlayer *player);
static gint melo_player_webplayer_get_pos (MeloPlayer *player, gint *duration);
static MeloPlayerStatus *melo_player_webplayer_get_status (MeloPlayer *player);

struct _MeloPlayerWebPlayerPrivate {
  GMutex mutex;
  gchar *bin_path;
  gboolean updating;

  /* Status */
  MeloPlayerStatus *status;
  gchar *url;
  gchar *uri;

  /* Gstreamer pipeline */
  GstElement *pipeline;
  GstElement *src;
  guint bus_watch_id;

  /* Gstreamer tags */
  GstTagList *tag_list;
};

G_DEFINE_TYPE_WITH_PRIVATE (MeloPlayerWebPlayer, melo_player_webplayer, MELO_TYPE_PLAYER)

static void
melo_player_webplayer_finalize (GObject *gobject)
{
  MeloPlayerWebPlayer *webp = MELO_PLAYER_WEBPLAYER (gobject);
  MeloPlayerWebPlayerPrivate *priv =
                              melo_player_webplayer_get_instance_private (webp);

  /* Stop pipeline */
  gst_element_set_state (priv->pipeline, GST_STATE_NULL);
  melo_player_status_unref (priv->status);

  /* Remove message handler */
  g_source_remove (priv->bus_watch_id);

  /* Free gstreamer pipeline */
  g_object_unref (priv->pipeline);

  /* Free tag list */
  gst_tag_list_unref (priv->tag_list);

  /* Free URL and URI */
  g_free (priv->uri);
  g_free (priv->url);

  /* Free binary path */
  g_free (priv->bin_path);

  /* Free player mutex */
  g_mutex_clear (&priv->mutex);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (melo_player_webplayer_parent_class)->finalize (gobject);
}

static void
melo_player_webplayer_class_init (MeloPlayerWebPlayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MeloPlayerClass *pclass = MELO_PLAYER_CLASS (klass);

  /* Control */
  pclass->add = melo_player_webplayer_add;
  pclass->play = melo_player_webplayer_play;
  pclass->prev = melo_player_webplayer_prev;
  pclass->next = melo_player_webplayer_next;
  pclass->set_state = melo_player_webplayer_set_state;
  pclass->set_pos = melo_player_webplayer_set_pos;

  /* Status */
  pclass->get_state = melo_player_webplayer_get_state;
  pclass->get_name = melo_player_webplayer_get_name;
  pclass->get_pos = melo_player_webplayer_get_pos;
  pclass->get_status = melo_player_webplayer_get_status;

  /* Add custom finalize() function */
  object_class->finalize = melo_player_webplayer_finalize;
}

static void
melo_player_webplayer_init (MeloPlayerWebPlayer *self)
{
  MeloPlayerWebPlayerPrivate *priv =
                              melo_player_webplayer_get_instance_private (self);
  GstElement *sink;
  GstBus *bus;

  self->priv = priv;
  priv->url = NULL;
  priv->uri = NULL;

  /* Init player mutex */
  g_mutex_init (&priv->mutex);

  /* Create new status handler */
  priv->status = melo_player_status_new (MELO_PLAYER_STATE_NONE, NULL);

  /* Create a new tag list */
  priv->tag_list = gst_tag_list_new_empty ();

  /* Create pipeline */
  priv->pipeline = gst_pipeline_new ("webplayer_player_pipeline");
  priv->src = gst_element_factory_make ("uridecodebin",
                                        "webplayer_player_uridecodebin");
  sink = gst_element_factory_make ("autoaudiosink",
                                   "webplayer_player_autoaudiosink");
  gst_bin_add_many (GST_BIN (priv->pipeline), priv->src, sink, NULL);

  /* Add signal handler on new pad */
  g_signal_connect(priv->src, "pad-added",
                   G_CALLBACK (pad_added_handler), sink);

  /* Add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (priv->pipeline));
  priv->bus_watch_id = gst_bus_add_watch (bus, bus_call, self);
  gst_object_unref (bus);
}

void
melo_player_webplayer_set_bin_path (MeloPlayerWebPlayer *webp,
                                    const gchar *path)
{
  g_free (webp->priv->bin_path);
  webp->priv->bin_path = g_strdup (path);
}

static gpointer
melo_player_webplayer_update_thread(gpointer user_data)
{
  MeloPlayerWebPlayerPrivate *priv = user_data;
  gchar *path = NULL;
  gchar *argv[5];
  gboolean ret;

  /* Check grabber directory (create if necessary) */
  if (g_mkdir_with_parents (priv->bin_path, 0700))
    goto end;

  /* Generate grabber file path */
  path = g_strdup_printf ("%s/" MELO_PLAYER_WEBPLAYER_GRABBER, priv->bin_path);

  /* Check grabber */
  if (g_file_test (path, G_FILE_TEST_EXISTS)) {
    /* Prepare update command */
    argv[0] = path;
    argv[1] = "--update";
    argv[2] = NULL;

    /* Update grabber */
    ret = g_spawn_sync (NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL |
                        G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL,
                        NULL, NULL);
  } else {
    /* Prepare download command */
    argv[0] = "wget";
    argv[1] = MELO_PLAYER_WEBPLAYER_GRABBER_URL;
    argv[2] = "-O";
    argv[3] = path;
    argv[4] = NULL;

    /* Download grabber */
    ret = g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH |
                        G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                        NULL, NULL, NULL, NULL, NULL, NULL);
    if (!ret)
      goto end;

    /* Prepare change mode command */
    argv[0] = "chmod";
    argv[1] = "a+x";
    argv[2] = path;
    argv[3] = NULL;
    ret = g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH |
                        G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                        NULL, NULL, NULL, NULL, NULL, NULL);
  }

end:
  /* End of update */
  priv->updating = FALSE;
  g_free (path);

  return NULL;
}

gboolean
melo_player_webplayer_update_grabber (MeloPlayerWebPlayer *webp)
{
  MeloPlayerWebPlayerPrivate *priv = webp->priv;
  GThread *thread;

  /* An update is already in progress */
  if (priv->updating)
    return FALSE;

  /* Start of update */
  priv->updating = TRUE;

  /* Create thread */
  thread = g_thread_new ("webplayer_grabber_update",
                         melo_player_webplayer_update_thread, priv);
  g_thread_unref (thread);
}

static gboolean
bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
  MeloPlayerWebPlayer *webp = MELO_PLAYER_WEBPLAYER (data);
  MeloPlayerWebPlayerPrivate *priv = webp->priv;
  MeloPlayer *player = MELO_PLAYER (webp);
  GError *error;

  /* Process bus message */
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_DURATION_CHANGED:
    case GST_MESSAGE_ASYNC_DONE: {
      gint64 duration;

      /* Get duration */
      if (gst_element_query_duration (priv->src, GST_FORMAT_TIME, &duration))
        priv->status->duration = duration / 1000000;
      break;
    }
    case GST_MESSAGE_TAG: {
      GstTagList *tags;
      MeloTags *mtags;

      /* Get tag list from message */
      gst_message_parse_tag (msg, &tags);

      /* Lock player mutex */
      g_mutex_lock (&priv->mutex);

      /* Merge tags */
      gst_tag_list_insert (priv->tag_list, tags, GST_TAG_MERGE_REPLACE);

      /* Fill MeloTags with GstTagList */
      mtags = melo_tags_new_from_gst_tag_list (priv->tag_list,
                                               MELO_TAGS_FIELDS_FULL);
      melo_player_status_take_tags (priv->status, mtags);

      /* Unlock player mutex */
      g_mutex_unlock (&priv->mutex);

      /* Free tag list */
      gst_tag_list_unref (tags);
      break;
    }
    case GST_MESSAGE_EOS:
      /* Play next media */
      if (!melo_player_webplayer_next (player)) {
        /* Stop playing */
        gst_element_set_state (priv->pipeline, GST_STATE_NULL);
        priv->status->state = MELO_PLAYER_STATE_STOPPED;
      }
      break;

    case GST_MESSAGE_ERROR:
      /* End of stream */
      priv->status->state = MELO_PLAYER_STATE_ERROR;

      /* Lock player mutex */
      g_mutex_lock (&priv->mutex);

      /* Update error message */
      g_free (priv->status->error);
      gst_message_parse_error (msg, &error, NULL);
      priv->status->error = g_strdup (error->message);
      g_error_free (error);

      /* Unlock player mutex */
      g_mutex_unlock (&priv->mutex);
      break;

    default:
      break;
  }

  return TRUE;
}

static void
pad_added_handler (GstElement *src, GstPad *pad, GstElement *sink)
{
  GstStructure *str;
  GstPad *sink_pad;
  GstCaps *caps;

  /* Get sink pad from sink element */
  sink_pad = gst_element_get_static_pad (sink, "sink");
  if (GST_PAD_IS_LINKED (sink_pad)) {
    g_object_unref (sink_pad);
    return;
  }

  /* Only select audio pad */
  caps = gst_pad_query_caps (pad, NULL);
  str = gst_caps_get_structure (caps, 0);
  if (!g_strrstr (gst_structure_get_name (str), "audio")) {
    gst_caps_unref (caps);
    gst_object_unref (sink_pad);
    return;
  }
  gst_caps_unref (caps);

  /* Link elements */
  gst_pad_link (pad, sink_pad);
  g_object_unref (sink_pad);
}

static gchar *
melo_player_webplayer_get_uri (MeloPlayerWebPlayer *webp, const gchar *path)
{
  MeloPlayerWebPlayerPrivate *priv = webp->priv;
  JsonParser *parser;
  JsonObject *obj;
  JsonNode *node;
  gboolean ret;
  gchar *out = NULL;
  gchar *uri = NULL;
  gchar *argv[4];

  /* Prepare command to get media URI */
  argv[0] = g_strdup_printf ("%s/" MELO_PLAYER_WEBPLAYER_GRABBER,
                             priv->bin_path ? priv->bin_path : ".");
  argv[1] = "--dump-json";
  argv[2] = g_strdup (path);
  argv[3] = NULL;

  /* Get JSON information for web player URL */
  ret = g_spawn_sync (NULL, argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
                      &out, NULL, NULL, NULL);
  g_free (argv[2]);
  g_free (argv[0]);

  /* Execution failed */
  if (!ret || !out)
    goto error;

  /* Parse JSON information from output */
  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, out, -1, NULL))
    goto error;
  g_free (out);

  /* Get root node and object */
  node = json_parser_get_root (parser);
  obj = json_node_get_object (node);
  if (!node || !obj)
    goto bad_json;

  /* Get best format if requested_formats is available */
  if (json_object_has_member (obj, "requested_formats")) {
    JsonObject *o = NULL;
    JsonArray *formats;
    gint best_abr = 0;
    gint i, count;

    /* Get requested formats array */
    formats = json_object_get_array_member (obj, "requested_formats");
    count = json_array_get_length (formats);
    for (i = 0; i < count; i++) {
      JsonObject *format = json_array_get_object_element (formats, i);
      const gchar *acodec = NULL;
      gint abr = 0;

      /* Get values */
      if (json_object_has_member (format, "acodec"))
        acodec = json_object_get_string_member (format, "acodec");
      if (json_object_has_member (format, "abr"))
        abr = json_object_get_int_member (format, "abr");

      /* No audio codec */
      if (acodec && !strcmp (acodec, "none"))
        continue;

      /* Select format */
      if (abr > best_abr) {
        best_abr = abr;
        o = format;
      }
    }

    /* An object has been found: use it for next step */
    if (o)
      obj = o;
  }

  /* Get final URI to use with gstreamer */
  if (json_object_has_member (obj, "url"))
    uri = g_strdup (json_object_get_string_member (obj, "url"));

  /* Free JSON parser and objects */
  g_object_unref (parser);

  return uri;

bad_json:
  g_object_unref (parser);
  return NULL;
error:
  g_free (out);
  return NULL;
}

static gboolean
melo_player_webplayer_add (MeloPlayer *player, const gchar *path,
                           const gchar *name, MeloTags *tags)
{
  if (!player->playlist)
    return FALSE;

  /* Add URL to playlist */
  melo_playlist_add (player->playlist, name, name, path, tags, FALSE);

  return TRUE;
}

static gboolean
melo_player_webplayer_play (MeloPlayer *player, const gchar *path,
                            const gchar *name, MeloTags *tags, gboolean insert)
{
  MeloPlayerWebPlayer *webp = MELO_PLAYER_WEBPLAYER (player);
  MeloPlayerWebPlayerPrivate *priv = webp->priv;
  gchar *_name = NULL;

  /* Lock player mutex */
  g_mutex_lock (&priv->mutex);

  /* Stop pipeline */
  gst_element_set_state (priv->pipeline, GST_STATE_READY);
  melo_player_status_unref (priv->status);
  gst_tag_list_unref (priv->tag_list);

  /* Replace URL */
  g_free (priv->url);
  priv->url = g_strdup (path);

  /* Replace URI */
  g_free (priv->uri);
  priv->uri = melo_player_webplayer_get_uri (webp, path);

  /* Create new status */
  if (!name) {
    _name = g_path_get_basename (priv->url);
    name = _name;
  }
  priv->status = melo_player_status_new (MELO_PLAYER_STATE_PLAYING, name);
  priv->tag_list = gst_tag_list_new_empty ();

  /* Set new location to src element */
  g_object_set (priv->src, "uri", priv->uri, NULL);
  gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);

  /* Add new media to playlist */
  if (insert && player->playlist)
    melo_playlist_add (player->playlist, name, name, path, tags, TRUE);
  g_free (_name);

  /* Unlock player mutex */
  g_mutex_unlock (&priv->mutex);

  return TRUE;
}

static gboolean
melo_player_webplayer_prev (MeloPlayer *player)
{
  gboolean ret;
  gchar *path;

  g_return_val_if_fail (player->playlist, FALSE);

  /* Get URI for previous media */
  path = melo_playlist_get_prev (player->playlist, TRUE);
  if (!path)
    return FALSE;

  /* Play media */
  ret = melo_player_webplayer_play (player, path, NULL, NULL, FALSE);
  g_free (path);

  return ret;
}

static gboolean
melo_player_webplayer_next (MeloPlayer *player)
{
  gboolean ret;
  gchar *path;

  g_return_val_if_fail (player->playlist, FALSE);

  /* Get URI for next media */
  path = melo_playlist_get_next (player->playlist, TRUE);
  if (!path)
    return FALSE;

  /* Play media */
  ret = melo_player_webplayer_play (player, path, NULL, NULL, FALSE);
  g_free (path);

  return ret;
}

static MeloPlayerState
melo_player_webplayer_set_state (MeloPlayer *player, MeloPlayerState state)
{
  MeloPlayerWebPlayerPrivate *priv = (MELO_PLAYER_WEBPLAYER (player))->priv;

  /* Lock player mutex */
  g_mutex_lock (&priv->mutex);

  if (state == MELO_PLAYER_STATE_NONE) {
    gst_element_set_state (priv->pipeline, GST_STATE_NULL);
    melo_player_status_unref (priv->status);
    priv->status = melo_player_status_new (MELO_PLAYER_STATE_NONE, NULL);
  } else if (state == MELO_PLAYER_STATE_PLAYING)
    gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
  else if (state == MELO_PLAYER_STATE_PAUSED)
    gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);
  else if (state == MELO_PLAYER_STATE_STOPPED)
    gst_element_set_state (priv->pipeline, GST_STATE_NULL);
  else
    state = priv->status->state;
  priv->status->state = state;

  /* Unlock player mutex */
  g_mutex_unlock (&priv->mutex);

  return state;
}

static gint
melo_player_webplayer_set_pos (MeloPlayer *player, gint pos)
{
  MeloPlayerWebPlayerPrivate *priv = (MELO_PLAYER_WEBPLAYER (player))->priv;
  gint64 time = (gint64) pos * 1000000;

  /* Seek to new position */
  if (!gst_element_seek (priv->pipeline, 1.0, GST_FORMAT_TIME,
                         GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, time,
                         GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE))
    return -1;

  return melo_player_webplayer_get_pos (player, NULL);
}

static MeloPlayerState
melo_player_webplayer_get_state (MeloPlayer *player)
{
  return (MELO_PLAYER_WEBPLAYER (player))->priv->status->state;
}

static gchar *
melo_player_webplayer_get_name (MeloPlayer *player)
{
  MeloPlayerWebPlayerPrivate *priv = (MELO_PLAYER_WEBPLAYER (player))->priv;
  gchar *name = NULL;

  /* Lock player mutex */
  g_mutex_lock (&priv->mutex);

  /* Copy name */
  name = g_strdup (priv->status->name);

  /* Unlock player mutex */
  g_mutex_unlock (&priv->mutex);

  return name;
}

static gint
melo_player_webplayer_get_pos (MeloPlayer *player, gint *duration)
{
  MeloPlayerWebPlayerPrivate *priv = (MELO_PLAYER_WEBPLAYER (player))->priv;
  gint64 pos;

  /* Get duration */
  if (duration)
    *duration = priv->status->duration;

  /* Get length */
  if (!gst_element_query_position (priv->pipeline, GST_FORMAT_TIME, &pos))
    pos = 0;

  return pos / 1000000;
}

static MeloPlayerStatus *
melo_player_webplayer_get_status (MeloPlayer *player)
{
  MeloPlayerWebPlayerPrivate *priv = (MELO_PLAYER_WEBPLAYER (player))->priv;
  MeloPlayerStatus *status;

  /* Lock player mutex */
  g_mutex_lock (&priv->mutex);

  /* Copy status */
  status = melo_player_status_ref (priv->status);
  status->pos = melo_player_webplayer_get_pos (player, NULL);

  /* Unlock player mutex */
  g_mutex_unlock (&priv->mutex);

  return status;
}
