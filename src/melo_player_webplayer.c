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
#include <libsoup/soup.h>
#include <gst/gst.h>

#include "melo_player_webplayer.h"

#define MELO_PLAYER_WEBPLAYER_GRABBER "youtube-dl"
#define MELO_PLAYER_WEBPLAYER_GRABBER_URL \
    "https://yt-dl.org/downloads/latest/youtube-dl"

#define MELO_PLAYER_WEBPLAYER_BUFFER_SIZE 8192

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
static gboolean melo_player_webplayer_get_cover (MeloPlayer *player,
                                                 GBytes **data, gchar **type);

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

  /* Current thumbnail / cover art */
  gboolean has_gst_cover;
  GBytes *cover;
  gchar *cover_type;

  /* HTTP client */
  SoupSession *session;

  /* Child process */
  GPid child_pid;
  gint child_fd;
  GString *child_buffer;
  guint child_watch;
  guint child_unix_watch;
};

G_DEFINE_TYPE_WITH_PRIVATE (MeloPlayerWebPlayer, melo_player_webplayer, MELO_TYPE_PLAYER)

static void
melo_player_webplayer_finalize (GObject *gobject)
{
  MeloPlayerWebPlayer *webp = MELO_PLAYER_WEBPLAYER (gobject);
  MeloPlayerWebPlayerPrivate *priv =
                              melo_player_webplayer_get_instance_private (webp);

  /* Stop process */
  if (priv->child_pid > 0) {
    /* Remove sources */
    g_source_remove (priv->child_unix_watch);
    g_source_remove (priv->child_watch);

    /* Kill process */
    kill (priv->child_pid, SIGTERM);
    waitpid (priv->child_pid);

    /* Free ressources */
    g_spawn_close_pid (priv->child_pid);
    g_string_free (priv->child_buffer, TRUE);
    close (priv->child_fd);
  }

  /* Free HTTP client */
  g_object_unref (priv->session);

  /* Free cover */
  if (priv->cover)
    g_bytes_unref (priv->cover);
  g_free (priv->cover_type);

  /* Stop pipeline */
  gst_element_set_state (priv->pipeline, GST_STATE_NULL);
  melo_player_status_unref (priv->status);

  /* Remove message handler */
  g_source_remove (priv->bus_watch_id);

  /* Free gstreamer pipeline */
  g_object_unref (priv->pipeline);

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
  pclass->get_cover = melo_player_webplayer_get_cover;

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
  priv->child_fd = -1;
  priv->url = NULL;
  priv->uri = NULL;

  /* Init player mutex */
  g_mutex_init (&priv->mutex);

  /* Create new status handler */
  priv->status = melo_player_status_new (MELO_PLAYER_STATE_NONE, NULL);

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

  /*  Create a new HTTP client */
  priv->session = soup_session_new_with_options (
                                SOUP_SESSION_USER_AGENT, "Melo",
                                NULL);
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
      MeloTags *mtags, *otags;

      /* Get tag list from message */
      gst_message_parse_tag (msg, &tags);

      /* Lock player mutex */
      g_mutex_lock (&priv->mutex);

      /* Fill MeloTags with GstTagList */
      mtags = melo_tags_new_from_gst_tag_list (tags, MELO_TAGS_FIELDS_FULL);

      /* Add cover if available */
      priv->has_gst_cover = melo_tags_has_cover (mtags);
      if (priv->cover && !priv->has_gst_cover) {
        melo_tags_set_cover (mtags, priv->cover, priv->cover_type);
        melo_tags_set_cover_url (mtags, G_OBJECT (webp), NULL, NULL);
      }

      /* Merge with old tags */
      otags = melo_player_status_get_tags (priv->status);
      if (otags) {
        melo_tags_merge (mtags, otags);
        melo_tags_unref (otags);
      }

      /* Set tags to player status */
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
melo_player_webplayer_parse_json (MeloPlayerWebPlayerPrivate *priv,
                                  gchar **thumb_url)
{
  JsonParser *parser;
  JsonObject *obj;
  JsonNode *node;
  gchar *uri = NULL;
  gchar *out = NULL;

  /* Get data */
  out = g_string_free (priv->child_buffer, FALSE);
  if (!out)
    return NULL;

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

  /* Get thumbnail URL */
  if (thumb_url && json_object_has_member (obj, "thumbnail"))
    *thumb_url = g_strdup (json_object_get_string_member (obj, "thumbnail"));

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
on_child_stdout (gint fd, GIOCondition condition, gpointer user_data)
{
  MeloPlayerWebPlayer *webp = MELO_PLAYER_WEBPLAYER (user_data);
  MeloPlayerWebPlayerPrivate *priv = webp->priv;
  char buffer[MELO_PLAYER_WEBPLAYER_BUFFER_SIZE];
  gboolean ret = FALSE;
  gssize len;

  /* Lock player mutex */
  g_mutex_lock (&priv->mutex);

  /* Stream error */
  if (condition & G_IO_ERR) {
    g_string_free (priv->child_buffer, TRUE);
    goto end;
  }

  /* New data available from child */
  if (condition & G_IO_IN) {
    /* Read data from child */
    len = read (fd, buffer, sizeof (buffer));
    if (len < 0) {
      /* Error when reading */
      g_string_free (priv->child_buffer, TRUE);
      goto end;
    }

    /* Append data to stdout buffer */
    g_string_append_len (priv->child_buffer, buffer, len);
    if (len)
      ret = TRUE;
  }

end:
  /* Close stdout */
  if (!ret)
    close (priv->child_fd);

  /* Unlock player mutex */
  g_mutex_unlock (&priv->mutex);

  return ret;
}

static void
on_request_done (SoupSession *session, SoupMessage *msg, gpointer user_data)
{
  MeloPlayerWebPlayer *webp = MELO_PLAYER_WEBPLAYER (user_data);
  MeloPlayerWebPlayerPrivate *priv = webp->priv;
  GBytes *cover = NULL;
  const gchar *type;
  MeloTags *tags;

  /* Get content type */
  type = soup_message_headers_get_one (msg->response_headers, "Content-Type");

  /* Get thumnail */
  g_object_get (msg, "response-body-data", &cover, NULL);

  /* Lock status */
  g_mutex_lock (&priv->mutex);

  /* Change thumbnail and type */
  if (priv->cover)
    g_bytes_unref (priv->cover);
  priv->cover = cover;
  g_free (priv->cover_type);

  /* Set cover if not provided by gstreamer */
  if (!priv->has_gst_cover) {
    /* Set cover into current player tags */
    tags = melo_player_status_get_tags (priv->status);
    if (tags) {
        melo_tags_set_cover (tags, cover, type);
        melo_tags_set_cover_url (tags, G_OBJECT (webp), NULL, NULL);
        melo_tags_unref (tags);
      }
  }

  /* Unlock status */
  g_mutex_unlock (&priv->mutex);
}

static void
on_child_exited (GPid pid, gint status, gpointer user_data)
{
  MeloPlayerWebPlayer *webp = MELO_PLAYER_WEBPLAYER (user_data);
  MeloPlayerWebPlayerPrivate *priv = webp->priv;

  /* Lock player mutex */
  g_mutex_lock (&priv->mutex);

  /* Parse JSON response */
  if (priv->child_buffer) {
    gchar *thumb_url = NULL;

    /* Get URI from website player */
    priv->uri = melo_player_webplayer_parse_json (priv, &thumb_url);

    /* Set new location to src element */
    g_object_set (priv->src, "uri", priv->uri, NULL);
    gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);

    /* Get thumbnail */
    if (thumb_url) {
      SoupMessage *msg;

     /* Download thumbnail */
      msg = soup_message_new ("GET", thumb_url);
      soup_session_queue_message (priv->session, msg, on_request_done, webp);
      g_free (thumb_url);
    }
  }

  /* Reset PID */
  g_spawn_close_pid (priv->child_pid);
  priv->child_pid = 0;

  /* Unlock player mutex */
  g_mutex_unlock (&priv->mutex);
}

static gboolean
melo_player_webplayer_get_uri (MeloPlayerWebPlayer *webp, const gchar *path)
{
  MeloPlayerWebPlayerPrivate *priv = webp->priv;
  gchar *argv[4];
  gboolean ret;

  /* Stop previous process instance */
  if (priv->child_pid > 0) {
    /* Remove sources */
    g_source_remove (priv->child_unix_watch);
    g_source_remove (priv->child_watch);

    /* Kill and wait child process */
    kill (priv->child_pid, SIGTERM);
    waitpid (priv->child_pid, NULL, 0);

    /* Free ressources */
    g_spawn_close_pid (priv->child_pid);
    g_string_free (priv->child_buffer, TRUE);
    close (priv->child_fd);
    priv->child_buffer = NULL;
    priv->child_pid = 0;
    priv->child_fd = -1;
  }

  /* Prepare command to get media URI */
  argv[0] = g_strdup_printf ("%s/" MELO_PLAYER_WEBPLAYER_GRABBER,
                             priv->bin_path ? priv->bin_path : ".");
  argv[1] = "--dump-json";
  argv[2] = g_strdup (path);
  argv[3] = NULL;

  /* Get JSON information for web player URL */
  ret = g_spawn_async_with_pipes (NULL, argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL |
                                  G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
                                  &priv->child_pid, NULL, &priv->child_fd,
                                  NULL, NULL);
  g_free (argv[2]);
  g_free (argv[0]);

  /* Setup callbacks for output */
  if (ret == TRUE) {
    priv->child_buffer = g_string_new (0);

    /* Capture child exit event */
    priv->child_watch = g_child_watch_add (priv->child_pid, on_child_exited,
                                           webp);

    /* Capture child stdout */
    priv->child_unix_watch = g_unix_fd_add (priv->child_fd,
                                            G_IO_IN | G_IO_ERR | G_IO_HUP,
                                            on_child_stdout, webp);
  }

  return ret;
}

static gboolean
melo_player_webplayer_add (MeloPlayer *player, const gchar *path,
                           const gchar *name, MeloTags *tags)
{
  if (!player->playlist)
    return FALSE;

  /* Add URL to playlist */
  melo_playlist_add (player->playlist, path, name, tags, FALSE);

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
  if (priv->cover) {
    g_bytes_unref (priv->cover);
    priv->cover = NULL;
  }
  g_free (priv->cover_type);
  priv->has_gst_cover = FALSE;
  priv->cover_type = NULL;

  /* Replace URL */
  g_free (priv->url);
  priv->url = g_strdup (path);

  /* Get URI */
  g_free (priv->uri);
  priv->uri = NULL;
  melo_player_webplayer_get_uri (webp, path);

  /* Create new status */
  if (!name) {
    _name = g_path_get_basename (priv->url);
    name = _name;
  }
  priv->status = melo_player_status_new (MELO_PLAYER_STATE_PLAYING, name);
  if (tags)
    melo_player_status_take_tags (priv->status, melo_tags_copy (tags));

  /* Add new media to playlist */
  if (insert && player->playlist)
    melo_playlist_add (player->playlist, path, name, tags, TRUE);
  g_free (_name);

  /* Unlock player mutex */
  g_mutex_unlock (&priv->mutex);

  return TRUE;
}

static gboolean
melo_player_webplayer_prev (MeloPlayer *player)
{
  MeloTags *tags = NULL;
  gchar *name = NULL;
  gboolean ret;
  gchar *path;

  g_return_val_if_fail (player->playlist, FALSE);

  /* Get URI for previous media */
  path = melo_playlist_get_prev (player->playlist, &name, &tags, TRUE);
  if (!path)
    return FALSE;

  /* Play media */
  ret = melo_player_webplayer_play (player, path, name, tags, FALSE);
  melo_tags_unref (tags);
  g_free (name);
  g_free (path);

  return ret;
}

static gboolean
melo_player_webplayer_next (MeloPlayer *player)
{
  MeloTags *tags = NULL;
  gchar *name = NULL;
  gboolean ret;
  gchar *path;

  g_return_val_if_fail (player->playlist, FALSE);

  /* Get URI for next media */
  path = melo_playlist_get_next (player->playlist, &name, &tags, TRUE);
  if (!path)
    return FALSE;

  /* Play media */
  ret = melo_player_webplayer_play (player, path, name, tags, FALSE);
  melo_tags_unref (tags);
  g_free (name);
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

static gboolean
melo_player_webplayer_get_cover (MeloPlayer *player, GBytes **data,
                                 gchar **type)
{
  MeloPlayerWebPlayerPrivate *priv = (MELO_PLAYER_WEBPLAYER (player))->priv;
  MeloTags *tags;

  /* Lock status mutex */
  g_mutex_lock (&priv->mutex);

  /* Copy status */
  tags = melo_player_status_get_tags (priv->status);
  if (tags) {
    *data = melo_tags_get_cover (tags, type);
    melo_tags_unref (tags);
  }

  /* Unlock status mutex */
  g_mutex_unlock (&priv->mutex);

  return TRUE;
}
