/* GStreamer command line scalable application
 *
 * Copyright (C) 2019 Stéphane Cerveau <scerveau@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

GST_DEBUG_CATEGORY (scalable_transcoder_debug);
#define GST_CAT_DEFAULT scalable_transcoder_debug

typedef struct _GstScalableBranch
{
  GstElement *pipeline;
  GstState state;
  gboolean buffering;
  gboolean is_live;
  gboolean quiet;
  gchar **exclude_args;
  gulong deep_notify_id;
} GstScalableBranch;

static GMainLoop *sLoop;
static guint signal_watch_intr_id;
static gboolean sQuiet = FALSE;

#define PRINT(FMT, ARGS...) do { \
    if (!sQuiet) \
        g_print(FMT "\n", ## ARGS); \
    } while (0)


#if defined(G_OS_UNIX) || defined(G_OS_WIN32)
/* As the interrupt handler is dispatched from GMainContext as a GSourceFunc
 * handler, we can react to this by posting a message. */
static gboolean
intr_handler (gpointer user_data)
{
  PRINT ("handling interrupt.");

  g_main_loop_quit (sLoop);

  /* remove signal handler */
  signal_watch_intr_id = 0;
  return G_SOURCE_REMOVE;
}
#endif

static gboolean
message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GstScalableBranch *thiz = (GstScalableBranch *) user_data;
  GST_DEBUG_OBJECT (thiz, "Received new message");
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (message->src);
      gst_message_parse_error (message, &err, &debug);

      GST_ERROR_OBJECT (thiz, "ERROR: from element %s: %s\n", name,
          err->message);
      if (debug != NULL)
        GST_ERROR_OBJECT (thiz, "Additional debug info:%s", debug);

      g_error_free (err);
      g_free (debug);
      g_free (name);

      g_main_loop_quit (sLoop);
      break;
    }
    case GST_MESSAGE_WARNING:{
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (message->src);
      gst_message_parse_warning (message, &err, &debug);

      GST_WARNING_OBJECT (thiz, "ERROR: from element %s: %s\n", name,
          err->message);
      if (debug != NULL)
        GST_WARNING_OBJECT (thiz, "Additional debug info:\n%s\n", debug);

      g_error_free (err);
      g_free (debug);
      g_free (name);
      break;
    }
    case GST_MESSAGE_EOS:
      GST_DEBUG_OBJECT (thiz, "Got EOS\n");
      g_main_loop_quit (sLoop);
      break;

    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old, new, pending;
      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (thiz->pipeline)) {
        gst_message_parse_state_changed (message, &old, &new, &pending);
        if (thiz->state == GST_STATE_PAUSED && thiz->state == new) {
          PRINT ("Prerolled done");
          gst_element_set_state (thiz->pipeline, GST_STATE_PLAYING);
          PRINT ("PLAYING");
        }
      }
      break;
    }
    case GST_MESSAGE_BUFFERING:{
      gint percent;

      gst_message_parse_buffering (message, &percent);
      PRINT ("buffering  %d%% ", percent);

      /* no state management needed for live pipelines */
      if (thiz->is_live)
        break;

      if (percent == 100) {
        /* a 100% message means buffering is done */
        thiz->buffering = FALSE;
        /* if the desired state is playing, go back */
        if (thiz->state == GST_STATE_PLAYING) {
          PRINT ("Done buffering, setting pipeline to PLAYING ...");
          gst_element_set_state (thiz->pipeline, GST_STATE_PLAYING);
        }
      } else {
        /* buffering busy */
        if (!thiz->buffering && thiz->state == GST_STATE_PLAYING) {
          /* we were not buffering but PLAYING, PAUSE  the pipeline. */
          PRINT ("Buffering, setting pipeline to PAUSED ...");
          gst_element_set_state (thiz->pipeline, GST_STATE_PAUSED);
        }
        thiz->buffering = TRUE;
      }
      break;
    }
    case GST_MESSAGE_PROPERTY_NOTIFY:{
      const GValue *val;
      const gchar *name;
      GstObject *obj;
      gchar *val_str = NULL;
      gchar **ex_prop, *obj_name;

      if (thiz->quiet)
        break;

      gst_message_parse_property_notify (message, &obj, &name, &val);

      /* Let's not print anything for excluded properties... */
      ex_prop = thiz->exclude_args;
      while (ex_prop != NULL && *ex_prop != NULL) {
        if (g_strcmp0 (name, *ex_prop) == 0)
          break;
        ex_prop++;
      }
      if (ex_prop != NULL && *ex_prop != NULL)
        break;

      obj_name = gst_object_get_path_string (GST_OBJECT (obj));
      if (val != NULL) {
        if (G_VALUE_HOLDS_STRING (val))
          val_str = g_value_dup_string (val);
        else if (G_VALUE_TYPE (val) == GST_TYPE_CAPS)
          val_str = gst_caps_to_string (g_value_get_boxed (val));
        else if (G_VALUE_TYPE (val) == GST_TYPE_TAG_LIST)
          val_str = gst_tag_list_to_string (g_value_get_boxed (val));
        else if (G_VALUE_TYPE (val) == GST_TYPE_STRUCTURE)
          val_str = gst_structure_to_string (g_value_get_boxed (val));
        else
          val_str = gst_value_serialize (val);
      } else {
        val_str = g_strdup ("(no value)");
      }

      PRINT ("%s: %s = %s", obj_name, name, val_str);
      g_free (obj_name);
      g_free (val_str);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

void
destroy_branch (gpointer data)
{
  GstScalableBranch *branch = (GstScalableBranch *) data;
  PRINT ("Destroying pipeline %s", GST_ELEMENT_NAME (branch->pipeline));
  gst_element_set_state (branch->pipeline, GST_STATE_READY);
  gst_element_set_state (branch->pipeline, GST_STATE_NULL);
  if (branch->pipeline)
    gst_object_unref (branch->pipeline);
  if (branch->deep_notify_id != 0)
    g_signal_handler_disconnect (branch->pipeline, branch->deep_notify_id);
}

GstScalableBranch *
add_branch (gchar * src_desc, gchar * branch_desc, gchar * sink_desc)
{
  GstElement *src, *transform, *sink;
  GError *err = NULL;
  GstPad *src_pad = NULL;
  GstPad *sink_pad = NULL;
  GstScalableBranch *branch = NULL;

  GST_DEBUG ("Add branch with src %s transform %s sink %s",
      src_desc, branch_desc, sink_desc);
  /* create source element and add it to the main pipeline */
  /* create transform bin element and add it to the main pipeline */
  branch = g_new0 (GstScalableBranch, 1);
  branch->pipeline = gst_pipeline_new (NULL);
  branch->state = GST_STATE_NULL;
  if (!src_desc && !sink_desc)
    transform = gst_parse_launch_full (branch_desc, NULL, GST_PARSE_FLAG_NONE,
        &err);
  else
    transform = gst_parse_bin_from_description (branch_desc, TRUE, &err);

  if (err) {
    GST_ERROR_OBJECT (branch,
        "Unable to instantiate the transform branch %s with error %s",
        branch_desc, err->message);
    goto error;
  }
  gst_bin_add (GST_BIN (branch->pipeline), transform);

  if (src_desc) {
    src = gst_element_factory_make (src_desc, NULL);
    if (!src) {
      GST_ERROR_OBJECT (branch, "Unable to create src element %s", src_desc);
      goto error;
    }
    /* retrieve the src pad which will be connected to the transform bin */
    src_pad = gst_element_get_static_pad (src, "src");
    if (!src_pad) {
      GST_ERROR_OBJECT (branch,
          "Unable to retrieve the src pad of src element: %s", src_desc);
      gst_object_unref (src);
      return FALSE;
    }
    gst_bin_add (GST_BIN (branch->pipeline), src);
    /* retrieve a compatible pad with the src pad */
    sink_pad = gst_element_get_compatible_pad (transform, src_pad, NULL);
    if (!sink_pad) {
      GST_ERROR_OBJECT (branch, "Unable to retreive a sink pad ");
      return FALSE;
    }
    /* connect src element with transform bin */
    if (GST_PAD_LINK_FAILED (gst_pad_link (src_pad, sink_pad))) {
      GST_ERROR_OBJECT (branch, "Unable to link src to transform");
      return FALSE;
    }
    gst_object_unref (src_pad);
    gst_object_unref (sink_pad);
  }

  if (sink_desc) {
    /* create sink element and add it to the main pipeline */
    sink = gst_element_factory_make (sink_desc, NULL);
    if (!sink) {
      GST_ERROR_OBJECT (branch, "Unable to create sink element %s", sink_desc);
      goto error;
    }
    /* retrieve the sink pad which will be connected to the transform bin */
    sink_pad = gst_element_get_static_pad (sink, "sink");
    if (!sink_pad) {
      GST_ERROR_OBJECT (branch,
          "Unable to retrieve the sink pad of sink element %s", sink_desc);
      gst_object_unref (sink);
      return FALSE;
    }
    gst_bin_add (GST_BIN (branch->pipeline), sink);
    /* retrieve a compatible pad with the sink pad */
    src_pad = gst_element_get_compatible_pad (transform, sink_pad, NULL);
    if (!src_pad) {
      GST_ERROR_OBJECT (branch, "Unable to get a src pad from transform\n");
      return FALSE;
    }
    /* connect sink element with transform bin */
    if (GST_PAD_LINK_FAILED (gst_pad_link (src_pad, sink_pad))) {
      GST_ERROR_OBJECT (branch, "Unable to link sink to transform");
      return FALSE;
    }

    gst_object_unref (src_pad);
    gst_object_unref (sink_pad);
  }
done:
  return branch;

error:
  destroy_branch (branch);
  branch = NULL;
  goto done;
}

gboolean
set_branch_state (GstScalableBranch * branch, GstState state)
{
  gboolean res = TRUE;
  GstStateChangeReturn ret;

  g_assert (branch != NULL);

  ret = gst_element_set_state (branch->pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      PRINT ("ERROR: Pipeline doesn't want to pause.");
      res = FALSE;
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      PRINT ("Pipeline is live and does not need PREROLL ...");
      branch->is_live = TRUE;
      break;
    case GST_STATE_CHANGE_ASYNC:
      PRINT ("Pipeline is PREROLLING ...");
      branch->state = GST_STATE_PAUSED;
      break;
      /* fallthrough */
    case GST_STATE_CHANGE_SUCCESS:
      PRINT ("Pipeline is PREROLLED ...");
      gst_element_set_state (branch->pipeline, GST_STATE_PLAYING);
      break;
  }
  return res;
}



int
main (int argc, char **argv)
{
  int res = EXIT_SUCCESS;
  GError *err = NULL;
  GOptionContext *ctx;
  GList *branches = NULL;
  GList *l = NULL;
  GstScalableBranch *branch;
  gchar **full_branch_desc_array = NULL;
  gchar **branch_desc;
  gboolean verbose = FALSE;

  GOptionEntry options[] = {
    {"branch", 'b', 0, G_OPTION_ARG_STRING_ARRAY, &full_branch_desc_array,
        "Add a custom full branch with gst-launch style description", NULL}
    ,
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
        ("Output status information and property notifications"), NULL},
    {NULL}
  };


  ctx = g_option_context_new ("[ADDITIONAL ARGUMENTS]");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    GST_ERROR ("Error initializing: %s\n", GST_STR_NULL (err->message));
    res = -1;
    goto done;
  }
  g_option_context_free (ctx);

  GST_DEBUG_CATEGORY_INIT (scalable_transcoder_debug, "n-launch", 0,
      "gst-n-launch");

  if (!full_branch_desc_array) {
    g_printerr ("Usage: %s -b branch1 \n", argv[0]);
    goto done;
  }

  for (branch_desc = full_branch_desc_array;
      branch_desc != NULL && *branch_desc != NULL; ++branch_desc) {
    GstBus *bus;
    branch = add_branch (NULL, *branch_desc, NULL);

    if (!branch) {
      res = -2;
      PRINT ("ERROR: unable to add branch \"%s\"", *branch_desc);
      goto done;
    }
    bus = gst_pipeline_get_bus (GST_PIPELINE (branch->pipeline));
    g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (message_cb),
        branch);
    gst_bus_add_signal_watch (bus);
    gst_object_unref (GST_OBJECT (bus));
    if (verbose) {
      branch->deep_notify_id =
          gst_element_add_property_deep_notify_watch (branch->pipeline, NULL,
          TRUE);
    }
    if (set_branch_state (branch, GST_STATE_READY))
      branches = g_list_append (branches, branch);
    else
      goto done;
  }
  PRINT ("Branches created");
  for (l = branches; l; l = l->next) {
    branch = (GstScalableBranch *) l->data;
    if (!set_branch_state (branch, GST_STATE_PAUSED))
      goto done;
  }
  PRINT ("Branches set to PAUSE state");
  sLoop = g_main_loop_new (NULL, FALSE);
#ifdef G_OS_UNIX
  signal_watch_intr_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, sLoop);
#endif
  g_main_loop_run (sLoop);

  /* No need to see all those pad caps going to NULL etc., it's just noise */

done:
  if (sLoop)
    g_main_loop_unref (sLoop);
  g_list_free_full (branches, destroy_branch);
  branches = NULL;
  g_strfreev (full_branch_desc_array);
  return res;
}
