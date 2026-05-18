#include <adwaita.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "listener.h"
#include "md.h"

#ifndef APP_ID
#define APP_ID "com.github.jsol.markdownviewer"
#endif

#define OBJ_DATA_CTX "app-ctx"

struct app_ctx {
  gchar id[4];

  GtkWidget *window;
  GtkWidget *content;
  GtkWidget *toc;
  GtkWidget *view;
  GPtrArray *file_listeners;
  GHashTable *md;

  GPtrArray *approved_run_cmds;
};

static void process_file(GFile *file, struct app_ctx *ctx);

static GFile *
get_approved_run_cmds_file(void)
{
  gchar *path = NULL;
  GFile *file = NULL;

  path = g_build_filename(g_get_user_config_dir(),
                          "markdown-viewer",
                          "approved-run-commands.txt",
                          NULL);

  const gchar *parent = g_path_get_dirname(path);
  if (g_mkdir_with_parents(parent, 0755) < 0) {
    g_warning("Failed to create directory %s", parent);
    return NULL;
  }

  file = g_file_new_for_path(path);

  g_free(path);

  return file;
}

static void
load_approved_run_cmds(struct app_ctx *ctx)
{
  GError *error = NULL;
  GFile *file = NULL;
  GFileInputStream *stream = NULL;
  GDataInputStream *data_stream = NULL;
  gchar *line = NULL;

  ctx->approved_run_cmds =
          g_ptr_array_new_with_free_func((GDestroyNotify) g_free);

  file = get_approved_run_cmds_file();
  if (!file) {
    return;
  }

  stream = g_file_read(file, NULL, &error);
  if (!stream) {
    g_warning("Failed to open file %s for reading: %s",
              g_file_peek_path(file),
              error->message);
    g_clear_error(&error);

    goto cleanup;
  }

  data_stream = g_data_input_stream_new(G_INPUT_STREAM(stream));

  while ((line = g_data_input_stream_read_line(data_stream,
                                               NULL,
                                               NULL,
                                               &error))) {
    g_strstrip(line);
    g_ptr_array_add(ctx->approved_run_cmds, line);
  }

cleanup:
  g_clear_object(&data_stream);
  g_clear_object(&stream);
  g_clear_object(&file);
}

static void
save_approved_run_cmds(struct app_ctx *ctx)
{
  GFile *file = NULL;
  GFileOutputStream *stream = NULL;
  GError *error = NULL;

  file = get_approved_run_cmds_file();
  if (!file) {
    return;
  }

  stream = g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &error);
  if (!stream) {
    g_warning("Failed to open file %s for writing: %s",
              g_file_peek_path(file),
              error->message);
    g_clear_error(&error);
    goto cleanup;
  }

  for (guint i = 0; i < ctx->approved_run_cmds->len; i++) {
    g_output_stream_write(G_OUTPUT_STREAM(stream),
                          ctx->approved_run_cmds->pdata[i],
                          strlen(ctx->approved_run_cmds->pdata[i]),
                          NULL,
                          NULL);
    g_output_stream_write(G_OUTPUT_STREAM(stream), "\n", 1, NULL, NULL);
  }

cleanup:
  g_clear_object(&stream);
  g_clear_object(&file);
}

static void
show_no_run_comment(struct app_ctx *ctx, const gchar *path)
{
  AdwDialog *dialog;
  gchar *text;
  gchar *title;

  title = g_strdup_printf("No Run command found for %s",
                          g_path_get_basename(path));
  text = g_strdup_printf("The file %s needs to contain a #!<command> comment "
                         "to be executed. use $INPUT for a path to the raw "
                         "file and $OUTPUT for the path to the output md file.",
                         path);

  dialog = adw_alert_dialog_new(title, text);
  adw_dialog_present(dialog, ctx->window);
}

static void
run_dialog_response_cb(AdwDialog *dialog,
                       const char *response,
                       gpointer user_data)
{
  listener_t *listener = user_data;
  struct app_ctx *ctx;

  if (g_strcmp0(response, "run") == 0) {
    listener_approve_run(listener);

    ctx = g_object_get_data(G_OBJECT(dialog), OBJ_DATA_CTX);

    const gchar *cmd = listener_get_run_cmd(listener);

    for (guint i = 0; i < ctx->approved_run_cmds->len; i++) {
      if (g_strcmp0(cmd, ctx->approved_run_cmds->pdata[i]) == 0) {
        return;
      }
    }

    g_ptr_array_add(ctx->approved_run_cmds, g_strdup(cmd));

    save_approved_run_cmds(ctx);
  }
}

static void
show_run_comment(struct app_ctx *ctx,
                 const gchar *run_cmd,
                 listener_t *listener)
{
  AdwDialog *dialog;

  gchar *text;
  gchar *title;

  text = g_strdup_printf("Do you wish to run \"%s\" to transform this file?",
                         run_cmd);

  title = g_strdup_printf("Run command found for %s",
                          g_path_get_basename(
                                  listener_get_file_path(listener)));

  dialog = adw_alert_dialog_new(title, text);

  adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog),
                                 "cancel",
                                 "Cancel",
                                 "run",
                                 "Run",
                                 NULL);

  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
  adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");

  g_object_set_data(G_OBJECT(dialog), OBJ_DATA_CTX, ctx);

  g_signal_connect(dialog,
                   "response",
                   G_CALLBACK(run_dialog_response_cb),
                   listener);

  adw_dialog_present(dialog, ctx->window);
  g_free(text);
  g_free(title);
}

static void
handle_run_comment(listener_t *listener,
                   const gchar *run_cmd,
                   gpointer user_data)
{
  struct app_ctx *ctx = user_data;

  for (guint i = 0; i < ctx->approved_run_cmds->len; i++) {
    if (g_strcmp0(run_cmd, ctx->approved_run_cmds->pdata[i]) == 0) {
      g_message("Pre-approved %s", run_cmd);
      listener_approve_run(listener);
      return;
    }
  }

  if (run_cmd == NULL) {
    show_no_run_comment(ctx, listener_get_file_path(listener));
    return;
  }

  show_run_comment(ctx, run_cmd, listener);
}

static void
setup_styles(void)
{
  GtkCssProvider *provider;
  GdkDisplay *display;
  const gchar *style_light =
          ".heading-1{font-size: xx-large; font-weight: bold;} "
          ".heading-2{font-size: x-large; font-weight: bold;} "
          ".heading-3{font-size: large; font-weight: bold;} "
          ".heading-4{font-size: medium; font-weight: bolder;} "
          ".heading-5{font-size: medium; font-weight: bold;} "
          ".heading-6{font-size: medium; font-weight: bold;} "
          ".monospace{font-family: monospace;} "
          ".table-header{background-color: #AAAAAA; font-weight: bold;} "
          ".in-text-button {padding: 0px; margin: 0px; "
          "margin-bottom: -7px;}";

  provider = gtk_css_provider_new();
  display = gdk_display_get_default();
  gtk_css_provider_load_from_string(provider, style_light);
  gtk_style_context_add_provider_for_display(display,
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_USER);
}

static void
show_content(const gchar *path, gpointer user_data)
{
  struct app_ctx *app = user_data;
  md_t *md;

  md = g_hash_table_lookup(app->md, path);

  if (!md) {
    GFile *file = g_file_new_for_path(path);
    process_file(file, app);
    g_object_unref(file);
  }
  GtkWidget *content = md_get_view(md);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(app->content), content);
}

static void
process_file(GFile *file, struct app_ctx *ctx)
{
  md_t *md;

  if (g_hash_table_contains(ctx->md, g_file_peek_path(file))) {
    return;
  }

  listener_t *listener = listener_new(file);

  listener_set_cmd_cb(listener, handle_run_comment, ctx);
  g_ptr_array_add(ctx->file_listeners, listener);
  if (listener_is_md(listener)) {
    md = md_new(listener, GTK_BOX(ctx->toc), show_content, ctx);

    g_hash_table_insert(ctx->md,
                        g_strdup(listener_get_file_path(listener)),
                        md);
    show_content(listener_get_file_path(listener), ctx);
  }
}

static GtkWidget *
setup_sidebar(struct app_ctx *ctx)
{
  GtkWidget *sidebar;
  GtkWidget *pages;

  sidebar = gtk_scrolled_window_new();
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sidebar), -1);
  gtk_widget_set_vexpand(sidebar, TRUE);
  gtk_widget_set_margin_top(sidebar, 40);

  pages = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  ctx->toc = g_object_ref_sink(pages);

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar), pages);

  return sidebar;
}

static void
collapse_cb(GtkWidget *button, struct app_ctx *ctx)
{
  adw_overlay_split_view_set_collapsed(ADW_OVERLAY_SPLIT_VIEW(ctx->view),
                                       !adw_overlay_split_view_get_collapsed(
                                               ADW_OVERLAY_SPLIT_VIEW(
                                                       ctx->view)));

  if (adw_overlay_split_view_get_collapsed(ADW_OVERLAY_SPLIT_VIEW(ctx->view))) {
    gtk_button_set_icon_name(GTK_BUTTON(button), "go-next-symbolic");
  } else {
    gtk_button_set_icon_name(GTK_BUTTON(button), "go-previous-symbolic");
  }
}

static void
file_select_cb(GObject *dialog, GAsyncResult *result, gpointer user_data)
{
  GFile *file;
  GError *error = NULL;

  file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(dialog), result, &error);

  if (file) {
    g_message("Opened file: %s", g_file_peek_path(file));
    process_file(file, user_data);
    g_object_unref(file);
  }
}

static void
menu_open_cb(G_GNUC_UNUSED GSimpleAction *action,
             G_GNUC_UNUSED GVariant *parameter,
             gpointer user_data)
{
  struct app_ctx *ctx = user_data;
  GtkFileDialog *dialog;

  dialog = gtk_file_dialog_new();
  gtk_file_dialog_open(dialog,
                       GTK_WINDOW(ctx->window),
                       NULL,
                       file_select_cb,
                       ctx);
}

static void
build_menu(GtkWidget *menu_button, GtkApplication *app, struct app_ctx *ctx)
{
  GMenu *menu_bar;
  GMenuItem *item;
  GSimpleAction *action;

  menu_bar = g_menu_new();

  item = g_menu_item_new("Open", "app.open");
  g_menu_append_item(menu_bar, item);
  g_object_unref(item);

  action = g_simple_action_new("open", NULL);
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
  g_signal_connect(action, "activate", G_CALLBACK(menu_open_cb), ctx);

  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button),
                                 G_MENU_MODEL(menu_bar));
}

static GtkWidget *
setup_content(struct app_ctx *ctx, GtkApplication *app)
{
  GtkWidget *content;
  GtkWidget *header;
  GtkWidget *collapse_button;
  GtkWidget *menu_button;

  content = adw_toolbar_view_new();
  header = adw_header_bar_new();
  menu_button = gtk_menu_button_new();

  build_menu(menu_button, app, ctx);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_button);

  collapse_button = gtk_button_new();
  g_signal_connect(collapse_button, "clicked", G_CALLBACK(collapse_cb), ctx);

  gtk_button_set_icon_name(GTK_BUTTON(collapse_button), "go-next-symbolic");
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), collapse_button);

  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(content), header);

  return content;
}

static GtkWidget *
setup_split_view(struct app_ctx *ctx, GtkApplication *app)
{
  GtkWidget *view;
  GtkWidget *sidebar;
  GtkWidget *content;

  view = adw_overlay_split_view_new();

  sidebar = setup_sidebar(ctx);
  content = setup_content(ctx, app);

  adw_overlay_split_view_set_sidebar(ADW_OVERLAY_SPLIT_VIEW(view), sidebar);

  adw_overlay_split_view_set_content(ADW_OVERLAY_SPLIT_VIEW(view), content);
  adw_overlay_split_view_set_collapsed(ADW_OVERLAY_SPLIT_VIEW(view), TRUE);

  ctx->view = g_object_ref_sink(view);
  ctx->content = g_object_ref_sink(content);

  return view;
}

static void
activate_cb(GtkApplication *app, gpointer user_data)
{
  struct app_ctx *ctx = user_data;
  GtkWidget *window = adw_application_window_new(app);
  GtkWidget *view;

  setup_styles();

  ctx->window = g_object_ref_sink(window);
  view = setup_split_view(ctx, app);

  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), view);

  gtk_window_set_default_size(GTK_WINDOW(window), 1200, 600);
  gtk_window_present(GTK_WINDOW(window));
}

static void
process_directory(GFile *dir, struct app_ctx *ctx)
{
  GDir *directory;
  const gchar *filename;

  directory = g_dir_open(g_file_peek_path(dir), 0, NULL);

  while ((filename = g_dir_read_name(directory)) != NULL) {
    GFile *file = g_file_new_for_path(
            g_build_filename(g_file_peek_path(dir), filename, NULL));

    if (g_file_test(g_file_peek_path(file), G_FILE_TEST_IS_DIR)) {
      process_directory(file, ctx);
      g_clear_object(&file);
      continue;
    }

    process_file(file, ctx);

    g_clear_object(&file);
  }
}

static void
open_cb(GApplication *self,
        gpointer files_pointer,
        gint n_files,
        G_GNUC_UNUSED gchar *hint,
        gpointer user_data)
{
  GFile **files = (GFile **) files_pointer;
  struct app_ctx *ctx = user_data;

  g_application_activate(self);

  for (gint i = 0; i < n_files; i++) {
    if (g_file_test(g_file_peek_path(files[i]), G_FILE_TEST_IS_DIR)) {
      process_directory(files[i], ctx);
      continue;
    }

    process_file(files[i], ctx);
  }

  if (g_hash_table_size(ctx->md) == 0) {
    GFile *markdown_file;

    markdown_file = listener_get_output_file(ctx->file_listeners->pdata[0]);

    process_file(markdown_file, ctx);

    g_clear_object(&markdown_file);
  }
}

int
main(int argc, char **argv)
{
  AdwApplication *app = NULL;
  struct app_ctx ctx = { 0 };

  ctx.id[0] = 'C';
  ctx.id[1] = 'T';
  ctx.id[2] = 'X';

  load_approved_run_cmds(&ctx);

  ctx.md = g_hash_table_new_full(g_str_hash,
                                 g_str_equal,
                                 g_free,
                                 (GDestroyNotify) md_free);
  ctx.file_listeners =
          g_ptr_array_new_with_free_func((GDestroyNotify) listener_free);
  app = adw_application_new(APP_ID, G_APPLICATION_HANDLES_OPEN);
  g_signal_connect(app, "open", G_CALLBACK(open_cb), &ctx);
  g_signal_connect(app, "activate", G_CALLBACK(activate_cb), &ctx);

  return g_application_run(G_APPLICATION(app), argc, argv);
}
