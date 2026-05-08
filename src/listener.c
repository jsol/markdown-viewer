#include "listener.h"

struct listener {
  GFileMonitor *monitor;
  GFile *file;
  gchar *checksum;

  listener_approved_cb cb;
  gpointer user_data;

  listener_md_cb md_cb;
  gpointer md_user_data;

  gchar *original_run_cmd;
  GStrv run_cmd;
  gboolean approved;
  GSubprocess *subprocess;
  gboolean rerun;
};

static gchar *run_exts[] = { ".yaml", ".puml", NULL };
static gchar *run_prefix[] = { "#!", "'!", NULL };

static void run_command(listener_t *ctx);

static gchar *
change_ext_to_md(const gchar *input)
{
  gchar *output = NULL;
  gchar *ext = NULL;

  ext = g_strrstr(input, ".");

  if (!ext) {
    output = g_strdup_printf("%s.md", input);
  } else {
    output = g_strdup_printf("%.*s.md", (int) (ext - input), input);
  }

  return output;
}

static void
cmd_ready(G_GNUC_UNUSED GObject *source_object,
          GAsyncResult *res,
          gpointer user_data)
{
  GError *error = NULL;
  listener_t *ctx = user_data;

  if (!g_subprocess_wait_check_finish(ctx->subprocess, res, &error)) {
    g_warning("Failed to run command: %s", error->message);
    g_clear_error(&error);
    return;
  }
  g_clear_object(&ctx->subprocess);

  if (ctx->rerun) {
    ctx->rerun = FALSE;
    run_command(ctx);
  }
}

static void
run_command(listener_t *ctx)
{
  GError *error = NULL;

  if (!ctx->run_cmd || !ctx->approved) {
    g_message("Not running command: %s",
              ctx->original_run_cmd ? ctx->original_run_cmd : "NULL");
    return;
  }

  if (ctx->subprocess) {
    g_message("Already running command: %s", ctx->original_run_cmd);

    ctx->rerun = TRUE;

    return;
  }

  ctx->subprocess = g_subprocess_newv((const gchar *const *) ctx->run_cmd,
                                      G_SUBPROCESS_FLAGS_NONE,
                                      &error);

  if (!ctx->subprocess) {
    g_warning("Failed to run command: %s", error->message);
    g_clear_error(&error);

    return;
  }

  g_message("Running command: %s", ctx->original_run_cmd);

  g_subprocess_wait_check_async(ctx->subprocess, NULL, cmd_ready, ctx);
}

static void
read_md(gpointer data)
{
  listener_t *ctx = data;
  gchar *markdown = NULL;
  GError *error = NULL;
  gsize len = 0;

  if (!g_file_load_contents(ctx->file, NULL, &markdown, &len, NULL, &error)) {
    g_warning("Failed to read file %s: %s",
              g_file_peek_path(ctx->file),
              error->message);
    g_clear_error(&error);
    return;
  }

  ctx->md_cb(ctx, markdown, ctx->md_user_data);
  g_free(markdown);
}

static gboolean
checksum(listener_t *ctx, GFile *file)
{
  GChecksum *checksum = NULL;
  const gchar *digest = NULL;
  GFileInputStream *stream = NULL;
  gboolean ret = FALSE;

  stream = g_file_read(file, NULL, NULL);
  if (!stream) {
    g_warning("Failed to read file: %s", g_file_peek_path(file));
    return FALSE;
  }

  checksum = g_checksum_new(G_CHECKSUM_MD5);
  while (TRUE) {
    guchar buffer[4096];
    GError *error = NULL;
    gssize bytes_read;

    bytes_read = g_input_stream_read(G_INPUT_STREAM(stream),
                                     buffer,
                                     sizeof(buffer),
                                     NULL,
                                     &error);
    if (bytes_read < 0) {
      g_warning("Failed to read file %s: %s",
                g_file_peek_path(file),
                error->message);
      g_clear_error(&error);
      goto out;
    } else if (bytes_read == 0) {
      break;
    }

    g_checksum_update(checksum, buffer, bytes_read);
  }

  digest = g_checksum_get_string(checksum);

  if (g_strcmp0(ctx->checksum, digest) != 0) {
    g_free(ctx->checksum);
    ctx->checksum = g_strdup(digest);
    ret = TRUE;
  }

out:
  g_clear_object(&stream);
  g_clear_object(&checksum);

  return ret;
}

static void
monitor_changes(G_GNUC_UNUSED GFileMonitor *monitor,
                GFile *file,
                G_GNUC_UNUSED GFile *other_file,
                GFileMonitorEvent event_type,
                gpointer user_data)
{
  listener_t *ctx = user_data;
  if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
      event_type == G_FILE_MONITOR_EVENT_CREATED) {
    g_message("File changed: %s", g_file_peek_path(file));

    if (!checksum(ctx, file)) {
      g_message("File content did not change: %s", g_file_peek_path(file));
      return;
    }

    if (ctx->md_cb) {
      read_md(ctx);
    } else {
      run_command(user_data);
    }
  } else {
    g_message("File event: %d for file: %s",
              event_type,
              g_file_peek_path(file));
  }
}

static void
setup_run_cmd(listener_t *ctx, const gchar *prefix)
{
  GFileInputStream *stream = NULL;
  GDataInputStream *data_stream = NULL;

  stream = g_file_read(ctx->file, NULL, NULL);
  data_stream = g_data_input_stream_new(G_INPUT_STREAM(stream));

  gchar *line = NULL;

  while ((line = g_data_input_stream_read_line(data_stream,
                                               NULL,
                                               NULL,
                                               NULL))) {
    if (g_str_has_prefix(line, prefix)) {
      break;
    }
    g_free(line);
    line = NULL;
  }

  g_clear_object(&stream);
  g_clear_object(&data_stream);

  if (!line) {
    ctx->cb(ctx, NULL, ctx->user_data);
    return;
  }

  ctx->original_run_cmd = g_strdup(line + strlen(prefix));

  g_strstrip(ctx->original_run_cmd);
  ctx->cb(ctx, ctx->original_run_cmd, ctx->user_data);
  g_free(line);
}

listener_t *
listener_new(GFile *file, listener_approved_cb cb, gpointer user_data)
{
  listener_t *listener = g_new0(listener_t, 1);

  listener->file = g_object_ref(file);
  listener->user_data = user_data;
  listener->monitor =
          g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, NULL);

  if (!listener_is_md(listener)) {
    listener->cb = cb;
  }

  for (int i = 0; run_exts[i] != NULL; i++) {
    if (g_str_has_suffix(g_file_peek_path(file), run_exts[i])) {
      setup_run_cmd(listener, run_prefix[i]);
      break;
    }
  }

  g_signal_connect(listener->monitor,
                   "changed",
                   G_CALLBACK(monitor_changes),
                   listener);

  return listener;
}

void
listener_set_md_cb(listener_t *ctx, listener_md_cb md_cb, gpointer user_data)
{
  ctx->md_cb = md_cb;
  ctx->md_user_data = user_data;

  if (g_file_query_exists(ctx->file, NULL)) {
    g_idle_add_once(read_md, ctx);
  }
}

void
listener_approve_run(listener_t *ctx)
{
  GString *cmd = NULL;
  gchar *output = NULL;
  GError *error = NULL;

  if (ctx->approved) {
    return;
  }

  ctx->approved = TRUE;
  cmd = g_string_new(ctx->original_run_cmd);

  output = change_ext_to_md(g_file_peek_path(ctx->file));

  g_string_replace(cmd, "$INPUT", g_file_peek_path(ctx->file), 0);
  g_string_replace(cmd, "$OUTPUT", output, 0);

  if (ctx->run_cmd) {
    g_strfreev(ctx->run_cmd);
  }

  if (!g_shell_parse_argv(cmd->str, NULL, &ctx->run_cmd, &error)) {
    g_warning("Failed to parse command: %s", error->message);
    g_clear_error(&error);
    ctx->run_cmd = NULL;
  }

  g_free(output);
  g_string_free(cmd, TRUE);
}

gboolean
listener_is_md(listener_t *listener)
{
  return g_str_has_suffix(g_file_peek_path(listener->file), ".md");
}

const gchar *
listener_get_run_cmd(listener_t *listener)
{
  return listener->original_run_cmd;
}

const gchar *
listener_get_file_path(listener_t *listener)
{
  return g_file_peek_path(listener->file);
}

GFile *
listener_get_output_file(listener_t *listener)
{
  gchar *output;
  GFile *output_file;

  g_return_val_if_fail(listener != NULL, NULL);

  output = change_ext_to_md(listener_get_file_path(listener));
  output_file = g_file_new_for_path(output);
  g_free(output);

  return output_file;
}

void
listener_free(listener_t *listener)
{
  if (!listener) {
    return;
  }

  listener->approved = FALSE;
  g_strfreev(listener->run_cmd);
  g_free(listener->original_run_cmd);
  g_clear_object(&listener->monitor);
  g_clear_object(&listener->file);
  g_free(listener);
}
