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

#include <Python.h>

#include <melo/melo_http_client.h>

#define MELO_LOG_TAG "webplayer_player"
#include <melo/melo_log.h>

#include "melo_webplayer_player.h"

#define MELO_WEBPLAYER_PLAYER_GRABBER "youtube-dl"
#define MELO_WEBPLAYER_PLAYER_GRABBER_VERSION "version"

#define MELO_WEBPLAYER_PLAYER_GRABBER_PATH "output"
#define MELO_WEBPLAYER_PLAYER_GRABBER_MODULE "youtube_dl"
#define MELO_WEBPLAYER_PLAYER_GRABBER_CLASS "YoutubeDL"

#define MELO_WEBPLAYER_PLAYER_GRABBER_URL \
  "https://yt-dl.org/downloads/latest/" MELO_WEBPLAYER_PLAYER_GRABBER
#define MELO_WEBPLAYER_PLAYER_GRABBER_VERSION_URL \
  "https://yt-dl.org/update/LATEST_VERSION"

struct _MeloWebplayerPlayer {
  GObject parent_instance;

  GstElement *pipeline;
  GstElement *src;
  guint bus_id;

  char *path;

  MeloHttpClient *client;
  bool updating;
  char *version;
  char *url;

  GSubprocess *process;

  GThread *thread;
  bool stop;
  GAsyncQueue *queue;

  PyObject *module;
  PyObject *instance;
};

MELO_DEFINE_PLAYER (MeloWebplayerPlayer, melo_webplayer_player)

static char *melo_webplayer_player_empty_url = "";

static gboolean bus_cb (GstBus *bus, GstMessage *msg, gpointer data);
static void pad_added_cb (GstElement *src, GstPad *pad, GstElement *sink);

static void melo_webplayer_player_update_grabber (MeloWebplayerPlayer *player);

static gpointer melo_webplayer_player_thread_func (gpointer user_data);

static bool melo_webplayer_player_play (MeloPlayer *player, const char *url);
static bool melo_webplayer_player_set_state (
    MeloPlayer *player, MeloPlayerState state);
static bool melo_webplayer_player_set_position (
    MeloPlayer *player, unsigned int position);
static unsigned int melo_webplayer_player_get_position (MeloPlayer *player);

static void
melo_webplayer_player_finalize (GObject *object)
{
  MeloWebplayerPlayer *player = MELO_WEBPLAYER_PLAYER (object);

  /* Stop running process */
  if (player->process) {
    g_subprocess_force_exit (player->process);
    g_object_unref (player->process);
  }

  /* Free pending URL */
  g_free (player->url);

  /* Free version string */
  g_free (player->version);

  /* Release HTTP client */
  g_object_unref (player->client);

  /* Stop thread */
  player->stop = true;
  g_async_queue_push (player->queue, melo_webplayer_player_empty_url);
  g_thread_join (player->thread);

  /* Release queue */
  g_async_queue_unref (player->queue);

  /* Release python objects */
  Py_XDECREF (player->instance);
  Py_XDECREF (player->module);

  /* Finalize python */
  Py_Finalize ();

  /* Free path */
  g_free (player->path);

  /* Remove bus watcher */
  g_source_remove (player->bus_id);

  /* Stop and release pipeline */
  gst_element_set_state (player->pipeline, GST_STATE_NULL);
  gst_object_unref (player->pipeline);

  /* Chain finalize */
  G_OBJECT_CLASS (melo_webplayer_player_parent_class)->finalize (object);
}

static void
melo_webplayer_player_class_init (MeloWebplayerPlayerClass *klass)
{
  MeloPlayerClass *parent_class = MELO_PLAYER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  /* Setup callbacks */
  parent_class->play = melo_webplayer_player_play;
  parent_class->set_state = melo_webplayer_player_set_state;
  parent_class->set_position = melo_webplayer_player_set_position;
  parent_class->get_position = melo_webplayer_player_get_position;

  /* Set finalize */
  object_class->finalize = melo_webplayer_player_finalize;
}

static void
melo_webplayer_player_init (MeloWebplayerPlayer *self)
{
  GstElement *sink;
  GstBus *bus;
  wchar_t *path;
  size_t len;

  /* Create pipeline */
  self->pipeline = gst_pipeline_new (MELO_WEBPLAYER_PLAYER_ID "_pipeline");
  self->src = gst_element_factory_make (
      "uridecodebin", MELO_WEBPLAYER_PLAYER_ID "_src");
  sink = melo_player_get_sink (
      MELO_PLAYER (self), MELO_WEBPLAYER_PLAYER_ID "_sink");
  gst_bin_add_many (GST_BIN (self->pipeline), self->src, sink, NULL);

  /* Add signal handler on new pad */
  g_signal_connect (self->src, "pad-added", G_CALLBACK (pad_added_cb), sink);

  /* Add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (self->pipeline));
  self->bus_id = gst_bus_add_watch (bus, bus_cb, self);
  gst_object_unref (bus);

  /* Create binary path */
  self->path = g_build_filename (
      g_get_user_data_dir (), "melo", "webplayer", "bin", NULL);
  if (self->path)
    g_mkdir_with_parents (self->path, 0700);

  /* Generate python path */
  len = wcslen (Py_GetPath ()) + strlen (self->path) +
        sizeof (MELO_WEBPLAYER_PLAYER_GRABBER_PATH) + 3;
  path = malloc (len * sizeof (*path));
  swprintf (path, len, L"%s/%s:%ls", self->path,
      MELO_WEBPLAYER_PLAYER_GRABBER_PATH, Py_GetPath ());
  MELO_LOGW ("%ls", path);

  /* Set python path */
  Py_SetPath (path);
  free (path);

  /* Initialize python */
  Py_Initialize ();

  /* Create async queue */
  self->queue = g_async_queue_new_full (g_free);

  /* Start thread */
  self->thread = g_thread_new (
      "webplayer_thread", melo_webplayer_player_thread_func, self);

  /* Create HTTP client */
  self->client = melo_http_client_new (NULL);

  /* Start grabber update */
  melo_webplayer_player_update_grabber (self);
}

MeloWebplayerPlayer *
melo_webplayer_player_new ()
{
  return g_object_new (
      MELO_TYPE_WEBPLAYER_PLAYER, "id", MELO_WEBPLAYER_PLAYER_ID, NULL);
}

static void
unzip_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GSubprocess *subprocess = G_SUBPROCESS (source_object);
  MeloWebplayerPlayer *player = user_data;
  GError *error = NULL;
  char *file = NULL;
  gint status;

  /* Unzip finished */
  if (!g_subprocess_wait_finish (subprocess, res, &error)) {
    MELO_LOGE ("failed to unzip: %s", error->message);
    g_error_free (error);
    goto end;
  }

  /* Create version file path */
  file = g_build_filename (
      player->path, MELO_WEBPLAYER_PLAYER_GRABBER_VERSION, NULL);

  /* Get status */
  status = g_subprocess_get_exit_status (subprocess);

  MELO_LOGD ("unzip exited with %d", status);

  /* Check status */
  if (!status || status == 1) {
    /* Save version file */
    if (!g_file_set_contents (
            file, player->version, strlen (player->version), &error)) {
      MELO_LOGE ("failed to save version file: %s", error->message);
      g_error_free (error);
      goto end;
    }

    /* Update is done */
    MELO_LOGI ("latest version installed");
  } else {
    gchar *version;
    gsize len;

    /* Free previous version */
    g_free (player->version);
    player->version = NULL;

    /* Restore version file */
    if (g_file_get_contents (file, &version, &len, NULL)) {
      player->version = g_strndup (version, len);
      g_free (version);
    }
  }

end:
  /* Free process */
  g_object_unref (subprocess);
  player->process = NULL;

  /* Free string */
  g_free (file);
  player->updating = false;

  /* Play pending URL */
  if (player->url) {
    melo_webplayer_player_play (MELO_PLAYER (player), player->url);
    g_free (player->url);
    player->url = NULL;
  } else
    g_async_queue_push (player->queue, melo_webplayer_player_empty_url);
}

static void
update_cb (MeloHttpClient *client, unsigned int code, const char *data,
    size_t size, void *user_data)
{
  MeloWebplayerPlayer *player = user_data;
  GError *error = NULL;
  char *file, *output;

  /* Failed to download update */
  if (code != 200) {
    MELO_LOGE ("failed to download latest version");
    g_async_queue_push (player->queue, melo_webplayer_player_empty_url);
    player->updating = false;
    return;
  }

  /* Generate grabber file path */
  file = g_build_filename (player->path, MELO_WEBPLAYER_PLAYER_GRABBER, NULL);
  output =
      g_build_filename (player->path, MELO_WEBPLAYER_PLAYER_GRABBER_PATH, NULL);

  /* Remove script header */
  if (data && size > 0 && data[0] == '#') {
    const char *p;

    /* Find end of line */
    p = memchr (data, '\n', size);
    if (p) {
      size -= p - data;
      data = p + 1;
    }
  }

  /* Save file */
  if (!g_file_set_contents (file, data, size, &error)) {
    MELO_LOGE ("failed to save file: %s", error->message);
    g_async_queue_push (player->queue, melo_webplayer_player_empty_url);
    player->updating = false;
    g_error_free (error);
    goto end;
  }

  /* Unzip file */
  player->process = g_subprocess_new (
      G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
      &error, "unzip", "-od", output, file, NULL);
  if (!player->process) {
    MELO_LOGE ("failed to unzip: %s", error->message);
    g_async_queue_push (player->queue, melo_webplayer_player_empty_url);
    player->updating = false;
    g_error_free (error);
  } else
    g_subprocess_wait_async (player->process, NULL, unzip_cb, player);

end:
  /* Release strings */
  g_free (output);
  g_free (file);
}

static void
version_cb (MeloHttpClient *client, unsigned int code, const char *data,
    size_t size, void *user_data)
{
  MeloWebplayerPlayer *player = user_data;
  gchar *version = NULL;
  gsize len;
  char *file;

  /* Failed to get version */
  if (code != 200) {
    MELO_LOGE ("failed to get latest version");
    g_async_queue_push (player->queue, melo_webplayer_player_empty_url);
    player->updating = false;
    return;
  }

  /* Update version string */
  g_free (player->version);
  player->version = g_strndup (data, size);

  /* Create version file path */
  file = g_build_filename (
      player->path, MELO_WEBPLAYER_PLAYER_GRABBER_VERSION, NULL);

  /* Compare version */
  if (!g_file_get_contents (file, &version, &len, NULL) || !version ||
      memcmp (version, player->version, strlen (player->version))) {
    MELO_LOGI ("new version available: %s", player->version);

    /* Download new version */
    melo_http_client_get (
        player->client, MELO_WEBPLAYER_PLAYER_GRABBER_URL, update_cb, player);

    /* Free version */
    g_free (version);
  } else {
    g_async_queue_push (player->queue, melo_webplayer_player_empty_url);
    player->updating = false;
  }

  /* Free string */
  g_free (file);
}

static void
melo_webplayer_player_update_grabber (MeloWebplayerPlayer *player)
{
  if (!player || !player->client || player->updating)
    return;

  /* Start update */
  player->updating = true;

  /* Download version file */
  melo_http_client_get (player->client,
      MELO_WEBPLAYER_PLAYER_GRABBER_VERSION_URL, version_cb, player);
}

static gboolean
bus_cb (GstBus *bus, GstMessage *msg, gpointer user_data)
{
  MeloWebplayerPlayer *wplayer = MELO_WEBPLAYER_PLAYER (user_data);
  MeloPlayer *player = MELO_PLAYER (wplayer);

  /* Process bus message */
  switch (GST_MESSAGE_TYPE (msg)) {
  case GST_MESSAGE_DURATION_CHANGED:
  case GST_MESSAGE_ASYNC_DONE: {
    gint64 position = 0, duration = 0;

    /* Get position and duration */
    gst_element_query_position (wplayer->pipeline, GST_FORMAT_TIME, &position);
    gst_element_query_duration (wplayer->src, GST_FORMAT_TIME, &duration);

    /* Update player */
    melo_player_update_duration (
        player, position / 1000000, duration / 1000000);
    break;
  }
  case GST_MESSAGE_TAG: {
    GstTagList *tag_list;
    MeloTags *tags;

    /* Get tag list from message */
    gst_message_parse_tag (msg, &tag_list);

    /* Generate new tags */
    tags = melo_tags_new_from_taglist (G_OBJECT (player), tag_list);
    melo_player_update_tags (player, tags);

    /* Free tag list */
    gst_tag_list_unref (tag_list);
    break;
  }
  case GST_MESSAGE_STREAM_START:
    /* Playback is started */
    melo_player_update_status (
        player, MELO_PLAYER_STATE_PLAYING, MELO_PLAYER_STREAM_STATE_NONE, 0);
    break;
  case GST_MESSAGE_BUFFERING: {
    MeloPlayerStreamState state = MELO_PLAYER_STREAM_STATE_NONE;
    gint percent;

    /* Get current buffer state */
    gst_message_parse_buffering (msg, &percent);

    /* Still buffering */
    if (percent < 100)
      state = MELO_PLAYER_STREAM_STATE_BUFFERING;

    /* Update status */
    melo_player_update_stream_state (player, state, percent);
    break;
  }
  case GST_MESSAGE_ERROR: {
    GError *error;

    /* Stop pipeline on error */
    gst_element_set_state (wplayer->pipeline, GST_STATE_NULL);
    melo_player_update_state (player, MELO_PLAYER_STATE_STOPPED);

    /* Set error message */
    gst_message_parse_error (msg, &error, NULL);
    melo_player_error (player, error->message);
    g_error_free (error);
    break;
  }
  case GST_MESSAGE_EOS:
    /* Stop playing */
    gst_element_set_state (wplayer->pipeline, GST_STATE_NULL);
    melo_player_eos (player);
    break;
  default:
    break;
  }

  return TRUE;
}

static void
pad_added_cb (GstElement *src, GstPad *pad, GstElement *sink)
{
  GstStructure *str;
  GstPad *sink_pad;
  GstCaps *caps;

  /* Get sink pad from sink element */
  sink_pad = gst_element_get_static_pad (sink, "sink");
  if (GST_PAD_IS_LINKED (sink_pad)) {
    MELO_LOGE ("sink pad already linked");
    g_object_unref (sink_pad);
    return;
  }

  /* Only select audio pad */
  caps = gst_pad_query_caps (pad, NULL);
  str = gst_caps_get_structure (caps, 0);
  if (!g_strrstr (gst_structure_get_name (str), "audio")) {
    MELO_LOGW ("no audio sink pad");
    gst_object_unref (sink_pad);
    gst_caps_unref (caps);
    return;
  }
  gst_caps_unref (caps);

  /* Link elements */
  gst_pad_link (pad, sink_pad);
  g_object_unref (sink_pad);
}

static gpointer
melo_webplayer_player_thread_func (gpointer user_data)
{
  MeloWebplayerPlayer *player = user_data;

  while (!player->stop) {
    PyObject *result;
    const char *uri = NULL;
    char *url, *p;

    /* Wait next URL */
    url = g_async_queue_pop (player->queue);
    while ((p = g_async_queue_try_pop (player->queue)) != NULL) {
      g_free (url);
      url = p;
    }
    if (url == melo_webplayer_player_empty_url)
      url = NULL;

    /* Stop thread */
    if (player->stop) {
      g_free (url);
      break;
    }

    /* Import module */
    if (!player->module) {
      PyObject *name;

      /* Create module name */
      name = PyUnicode_FromString (MELO_WEBPLAYER_PLAYER_GRABBER_MODULE);

      /* Import module */
      player->module = PyImport_Import (name);
      if (!player->module) {
        MELO_LOGE ("failed to import module");
        g_free (url);
        continue;
      }
      Py_DECREF (name);

      MELO_LOGD ("module imported");
    }

    /* Instantiate object */
    if (!player->instance) {
      PyObject *dict, *class;

      /* Get module dictionary */
      dict = PyModule_GetDict (player->module);
      if (!dict) {
        MELO_LOGE ("failed to get module dictionary");
        g_free (url);
        continue;
      }

      /* Get class from module */
      class = PyDict_GetItemString (dict, MELO_WEBPLAYER_PLAYER_GRABBER_CLASS);
      if (!class) {
        MELO_LOGE ("failed to get class");
        g_free (url);
        continue;
      }

      /* Create object instance */
      player->instance = PyObject_CallObject (class, NULL);
      if (!player->instance) {
        MELO_LOGE ("failed to instantiate object");
        g_free (url);
        continue;
      }
      MELO_LOGD ("object instantiated");
    }

    /* No video to get */
    if (!url)
      continue;

    /* Get video info */
    result =
        PyObject_CallMethod (player->instance, "extract_info", "(sb)", url, 0);
    if (result) {
      unsigned int abr = 0;
      PyObject *formats;
      /* Get formats */
      formats = PyDict_GetItemString (result, "formats");
      if (PyList_Check (formats)) {
        unsigned int i, count;

        /* Get formats count */
        count = PyList_Size (formats);

        /* Parse formats list */
        for (i = 0; i < count; i++) {
          PyObject *fmt, *tmp;
          unsigned int br;

          /* Get next format */
          fmt = PyList_GetItem (formats, i);
          if (!fmt)
            continue;

          /* Get audio codec */
          tmp = PyDict_GetItemString (fmt, "acodec");
          if (!tmp || !strcmp (PyUnicode_AsUTF8 (tmp), "none"))
            continue;

          /* Get audio bitrate */
          tmp = PyDict_GetItemString (fmt, "abr");
          if (!tmp || (br = PyLong_AsLong (tmp)) < abr)
            continue;

          /* Get URL */
          tmp = PyDict_GetItemString (fmt, "url");
          if (!tmp)
            continue;

          /* Set best audio format */
          abr = br;
          uri = g_strdup (PyUnicode_AsUTF8 (tmp));
        }
      }
      MELO_LOGD ("best audio bitrate found: %u", abr);
      Py_DECREF (result);
    }

    /* Audio stream found */
    if (uri) {
      /* Set new webplayer URI */
      g_object_set (player->src, "uri", uri, NULL);

      /* Start playing */
      gst_element_set_state (player->pipeline, GST_STATE_PLAYING);
    } else {
      melo_player_update_state (
          MELO_PLAYER (player), MELO_PLAYER_STATE_STOPPED);
      melo_player_error (MELO_PLAYER (player), "video not found");
    }
  }

  return NULL;
}

static bool
melo_webplayer_player_play (MeloPlayer *player, const char *url)
{
  MeloWebplayerPlayer *wplayer = MELO_WEBPLAYER_PLAYER (player);

  /* Stop previously playing webplayer */
  gst_element_set_state (wplayer->pipeline, GST_STATE_NULL);

  /* Update in progress */
  if (wplayer->updating) {
    /* Save URL */
    g_free (wplayer->url);
    wplayer->url = g_strdup (url);
    return true;
  }

  /* Add URL to queue */
  g_async_queue_push (wplayer->queue, g_strdup (url));

  return true;
}

static bool
melo_webplayer_player_set_state (MeloPlayer *player, MeloPlayerState state)
{
  MeloWebplayerPlayer *wplayer = MELO_WEBPLAYER_PLAYER (player);

  if (state == MELO_PLAYER_STATE_PLAYING)
    gst_element_set_state (wplayer->pipeline, GST_STATE_PLAYING);
  else if (state == MELO_PLAYER_STATE_PAUSED)
    gst_element_set_state (wplayer->pipeline, GST_STATE_PAUSED);
  else
    gst_element_set_state (wplayer->pipeline, GST_STATE_NULL);

  return true;
}

static bool
melo_webplayer_player_set_position (MeloPlayer *player, unsigned int position)
{
  MeloWebplayerPlayer *wplayer = MELO_WEBPLAYER_PLAYER (player);
  gint64 pos = (gint64) position * 1000000;

  /* Seek to new position */
  return gst_element_seek (wplayer->pipeline, 1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_NONE,
      GST_CLOCK_TIME_NONE);
}

static unsigned int
melo_webplayer_player_get_position (MeloPlayer *player)
{
  MeloWebplayerPlayer *wplayer = MELO_WEBPLAYER_PLAYER (player);
  gint64 value;

  /* Get current position */
  if (!gst_element_query_position (wplayer->pipeline, GST_FORMAT_TIME, &value))
    return 0;

  return value / 1000000;
}