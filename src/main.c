#include "gio/gio.h"
#include "glib-object.h"
#include <adwaita.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <glib/gstdio.h>
#ifdef USE_GFM
#include <cmark-gfm.h>
#else
#include <cmark.h>
#endif

#include "gtk/gtk.h"
#include "html.h"
#include "toc.h"

#define OBJ_DATA_IMG "image"

struct app_ctx {
  gchar id[4];

  GtkWidget *box;
  GtkWidget *scroll;
  GtkWidget *window;
  GFile *md_file;
  GPtrArray *image_monitors;
  GFileMonitor *md_monitor;
  toc_t *toc;
  html_t *html;
};

struct file_ctx {
  GFile *file;
  GFileMonitor *monitor;
  gchar *transform_cmd;
  struct app_ctx *app_ctx;
};

static void
print_node(cmark_node *node, int indent)
{
  for (int i = 0; i < indent; i++) {
    g_print("  ");
  }
  cmark_node *child = cmark_node_first_child(node);

  if (cmark_node_get_type(node) == CMARK_NODE_TEXT) {
    g_print("TEXT: %s\n", cmark_node_get_literal(node));
  } else if (cmark_node_get_type(node) == CMARK_NODE_HTML_INLINE) {
    g_print("HTML INLINE: %s\n", cmark_node_get_literal(node));
  } else if (cmark_node_get_type(node) == CMARK_NODE_HTML_BLOCK) {
    g_print("HTML BLOCK: %s\n", cmark_node_get_literal(node));
  } else {
    g_print("%s\n", cmark_node_get_type_string(node));
  }

  while (child) {
    print_node(child, indent + 1);
    child = cmark_node_next(child);
  }
}

static gboolean
allowed_html(const char *html)
{
  const char *allowed_tags[] = { "<b>",  "</b>",  "<i>",      "</i>",
                                 "<u>",  "</u>",  "<strong>", "</strong>",
                                 "<em>", "</em>", NULL };

  for (int i = 0; allowed_tags[i] != NULL; i++) {
    if (g_strcmp0(html, allowed_tags[i]) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

static GFile *
normalize_url(struct app_ctx *ctx, const char *url)
{
  gchar *dirname = NULL;
  GFile *dirname_file = NULL;
  GFile *image_file = NULL;

  if (g_str_has_prefix(url, "/")) {
    return g_file_new_for_path(url);
  }

  if (!ctx->md_file) {
    return g_file_new_for_path(url);
  }

  dirname = g_path_get_dirname(g_file_peek_path(ctx->md_file));
  dirname_file = g_file_new_for_path(dirname);

  image_file = g_file_resolve_relative_path(dirname_file, url);

  g_clear_object(&dirname_file);
  g_free(dirname);

  return image_file;
}

static void
display_html_table(html_t *ctx, const char *html, GtkWidget *box)
{
  GtkWidget *frame = NULL;
  GtkWidget *table;

  table = html_parse(ctx, html);

  if (!table) {
    return;
  }

  frame = gtk_frame_new(NULL);
  gtk_frame_set_child(GTK_FRAME(frame), table);
  gtk_box_append(GTK_BOX(box), frame);
}

static GtkWidget *
display_image(GFile *file, GtkWidget *old_image, GtkWidget *box)
{
  GtkWidget *image = gtk_picture_new_for_file(file);
  gtk_picture_set_can_shrink(GTK_PICTURE(image), FALSE);
  gtk_widget_set_halign(image, GTK_ALIGN_START);

  if (old_image == NULL) {
    gtk_box_append(GTK_BOX(box), g_object_ref(image));
    return image;
  }

  gtk_box_insert_child_after(GTK_BOX(box), g_object_ref(image), old_image);
  gtk_box_remove(GTK_BOX(box), old_image);

  return image;
}

static void
monitor_image_changes(GFileMonitor *monitor,
                      GFile *file,
                      G_GNUC_UNUSED GFile *other_file,
                      GFileMonitorEvent event_type,
                      G_GNUC_UNUSED gpointer user_data)
{
  if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
      event_type == G_FILE_MONITOR_EVENT_CREATED) {
    g_message("Image changed: %s", g_file_peek_path(file));
    GtkWidget *old_image = g_object_get_data(G_OBJECT(monitor), OBJ_DATA_IMG);

    GtkWidget *new_image =
            display_image(file, old_image, gtk_widget_get_parent(old_image));
    g_object_set_data_full(G_OBJECT(monitor),
                           OBJ_DATA_IMG,
                           g_object_ref_sink(new_image),
                           g_object_unref);
  }
}

static void
display_paragraph(struct app_ctx *ctx, cmark_node *node, GtkWidget *box)
{
  cmark_node *child = cmark_node_first_child(node);
  GString *paragraph_text = g_string_new("");

  while (child) {
    switch (cmark_node_get_type(child)) {
    case CMARK_NODE_IMAGE: {
      GFileMonitor *monitor = NULL;

      GFile *url = normalize_url(ctx, cmark_node_get_url(child));

      GtkWidget *image = display_image(url, NULL, box);

      monitor = g_file_monitor_file(url, G_FILE_MONITOR_NONE, NULL, NULL);

      g_object_set_data_full(G_OBJECT(monitor),
                             OBJ_DATA_IMG,
                             g_object_ref_sink(image),
                             g_object_unref);
      g_ptr_array_add(ctx->image_monitors, monitor);
      g_signal_connect(monitor,
                       "changed",
                       G_CALLBACK(monitor_image_changes),
                       NULL);
      g_free(url);

      break;
    }

    case CMARK_NODE_TEXT: {
      gchar *escaped_text =
              g_markup_escape_text(cmark_node_get_literal(child), -1);
      g_string_append(paragraph_text, escaped_text);
      g_free(escaped_text);
      break;
    }

    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK: {
      g_string_append(paragraph_text, "\n");
      break;
    }

    case CMARK_NODE_INLINE_HTML: {
      if (allowed_html(cmark_node_get_literal(child))) {
        g_string_append(paragraph_text, cmark_node_get_literal(child));
      }
      break;
    }
    default:
      break;
    }
    child = cmark_node_next(child);
  }

  if (paragraph_text->len > 0) {
    GtkWidget *label = gtk_label_new(paragraph_text->str);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);
  }

  g_string_free(paragraph_text, TRUE);
}

static void
display_markdown(struct app_ctx *ctx, cmark_node *node, GtkWidget *box)
{
  cmark_node *child = cmark_node_first_child(node);

  switch (cmark_node_get_type(node)) {
  case CMARK_NODE_TEXT: {
    if (toc_is_empty(ctx->toc)) {
      break;
    }
    if (g_strcmp0(cmark_node_get_literal(node), ":toc:") == 0) {
      //      gtk_box_append(GTK_BOX(box), toc_get(ctx->toc));
      break;
    }
    GtkWidget *label = gtk_label_new(cmark_node_get_literal(node));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);
    break;
  }
  case CMARK_NODE_HTML_BLOCK: {
    if (toc_is_empty(ctx->toc)) {
      break;
    }
    display_html_table(ctx->html, cmark_node_get_literal(node), box);
    break;
  }
  case CMARK_NODE_HEADING: {
    GtkWidget *label;
    cmark_node *heading_child = cmark_node_first_child(node);

    label = toc_add_heading(ctx->toc,
                            cmark_node_get_literal(heading_child),
                            cmark_node_get_heading_level(node));

    gtk_widget_set_margin_top(label, 10);
    gtk_widget_set_margin_bottom(label, 10);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);

    break;
  }
  case CMARK_NODE_PARAGRAPH: {
    if (toc_is_empty(ctx->toc)) {
      break;
    }
    GtkWidget *paragraph_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(box), paragraph_box);
    display_paragraph(ctx, node, paragraph_box);
    break;
  }
  case CMARK_NODE_DOCUMENT:
    while (child) {
      display_markdown(ctx, child, box);
      child = cmark_node_next(child);
    }
    break;

  default:
    break;
  }
}
static gchar *
get_output_file(GFile *input_file)
{
  gchar *output = NULL;
  const gchar *input = g_file_peek_path(input_file);
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
parse_markdown(struct app_ctx *ctx, const char *markdown)
{
  cmark_node *doc =
          cmark_parse_document(markdown, strlen(markdown), CMARK_OPT_DEFAULT);

  print_node(doc, 0);
  display_markdown(ctx, doc, ctx->box);
  cmark_node_free(doc);
}

static void
handle_markdown_file(struct app_ctx *ctx, GFile *file)
{
  gchar *markdown = NULL;
  gsize len = 0;

  g_message("Handling markdonw: %s", ctx->id);

  if (!g_file_load_contents(file, NULL, &markdown, &len, NULL, NULL)) {
    g_printerr("Failed to read file: %s\n", g_file_peek_path(file));
    return;
  }

  g_clear_object(&ctx->md_file);
  g_clear_pointer(&ctx->toc, toc_free);
  g_clear_pointer(&ctx->html, html_free);
  g_clear_pointer(&ctx->image_monitors, g_ptr_array_unref);

  ctx->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  ctx->image_monitors = g_ptr_array_new_with_free_func(g_object_unref);
  ctx->md_file = g_object_ref(file);

  ctx->toc = toc_new();
  ctx->html = html_new();

  gtk_box_set_homogeneous(GTK_BOX(ctx->box), FALSE);
  gtk_widget_set_halign(ctx->box, GTK_ALIGN_START);
  gtk_widget_set_margin_start(ctx->box, 20);
  gtk_widget_set_margin_end(ctx->box, 20);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ctx->scroll), ctx->box);

  parse_markdown(ctx, markdown);
  g_free(markdown);
}

static void
monitor_md_file_changes(G_GNUC_UNUSED GFileMonitor *monitor,
                        GFile *file,
                        G_GNUC_UNUSED GFile *other_file,
                        GFileMonitorEvent event_type,
                        gpointer user_data)
{
  if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
      event_type == G_FILE_MONITOR_EVENT_CREATED) {
    g_message("File changed: %s", g_file_peek_path(file));
    handle_markdown_file(user_data, file);
  } else {
    g_message("File event: %d for file: %s",
              event_type,
              g_file_peek_path(file));
  }
}

static void
validate_run_cmd_cb(G_GNUC_UNUSED AdwDialog *dialog, struct file_ctx *ctx)
{
  if (ctx->transform_cmd == NULL) {
    gtk_window_close(GTK_WINDOW(ctx->app_ctx->window));
  }
}

static void
show_no_run_comment(struct file_ctx *ctx)
{
  AdwDialog *dialog = adw_dialog_new();

  adw_dialog_set_title(dialog, "No run comment found");

  GtkWidget *label =
          gtk_label_new("The file needs to contain a #!<command> comment to be "
                        "executed. use $INPUT for a path to the raw file and "
                        "$OUTPUT for the path to the output md file.");

  gtk_label_set_wrap(GTK_LABEL(label), TRUE);

  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_margin_start(label, 20);
  gtk_widget_set_margin_end(label, 20);
  gtk_widget_set_margin_top(label, 20);
  gtk_widget_set_margin_bottom(label, 20);

  adw_dialog_set_child(dialog, label);
  g_signal_connect(dialog, "closed", G_CALLBACK(validate_run_cmd_cb), ctx);
  adw_dialog_present(dialog, ctx->app_ctx->window);
}

static void
remove_run_cmd(G_GNUC_UNUSED GtkButton *dialog, struct file_ctx *ctx)
{
  g_free(ctx->transform_cmd);
  ctx->transform_cmd = NULL;

  gtk_window_close(GTK_WINDOW(ctx->app_ctx->window));
}

static void
process_raw_file(struct file_ctx *ctx)
{
  GError *error = NULL;
  if (!g_spawn_command_line_async(ctx->transform_cmd, &error)) {
    g_printerr("Failed to run command: %s\n", error->message);
    g_error_free(error);
  }
}

static void
monitor_raw_changes(G_GNUC_UNUSED GFileMonitor *monitor,
                    GFile *file,
                    G_GNUC_UNUSED GFile *other_file,
                    GFileMonitorEvent event_type,
                    gpointer user_data)
{
  if (event_type == G_FILE_MONITOR_EVENT_CHANGED) {
    g_message("File changed: %s", g_file_peek_path(file));
    process_raw_file(user_data);
  } else {
    g_message("File event: %d for file: %s",
              event_type,
              g_file_peek_path(file));
  }
}

static void
setup_run_cmd(G_GNUC_UNUSED GtkWidget *button, struct file_ctx *ctx)
{
  ctx->monitor =
          g_file_monitor_file(ctx->file, G_FILE_MONITOR_NONE, NULL, NULL);

  g_signal_connect(ctx->monitor,
                   "changed",
                   G_CALLBACK(monitor_raw_changes),
                   ctx);
  process_raw_file(ctx);
}

static void
show_run_comment(struct file_ctx *ctx)
{
  AdwDialog *dialog = adw_dialog_new();
  GtkWidget *button_yes = NULL;
  GtkWidget *button_no = NULL;
  GtkWidget *grid;

  adw_dialog_set_title(dialog, "Allow run command");

  gchar *text =
          g_strdup_printf("Do you wish to run \"%s\" to transform this file?",
                          ctx->transform_cmd);

  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);

  button_yes = gtk_button_new_with_label("Yes");
  button_no = gtk_button_new_with_label("No");

  grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_margin_start(label, 20);
  gtk_widget_set_margin_end(label, 20);
  gtk_widget_set_margin_top(label, 20);
  gtk_widget_set_margin_bottom(label, 20);

  gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 2, 1);
  gtk_grid_attach(GTK_GRID(grid), button_yes, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), button_no, 1, 1, 1, 1);

  adw_dialog_set_child(dialog, grid);
  g_signal_connect(dialog, "closed", G_CALLBACK(validate_run_cmd_cb), ctx);
  g_signal_connect(button_no, "clicked", G_CALLBACK(remove_run_cmd), ctx);

  g_signal_connect(button_yes, "clicked", G_CALLBACK(setup_run_cmd), ctx);
  g_signal_connect_swapped(button_yes,
                           "clicked",
                           G_CALLBACK(adw_dialog_close),
                           dialog);

  adw_dialog_present(dialog, ctx->app_ctx->window);
}

static gchar *
get_transform_cmd(const gchar *line, GFile *input_file, const gchar *prefix)
{
  gchar *cmd = NULL;
  const gchar *input = g_file_peek_path(input_file);
  gchar *output = NULL;
  GString *cmd_str = NULL;

  if (g_str_has_prefix(line, prefix)) {
    line = line + strlen(prefix);
  }

  cmd_str = g_string_new(line);

  g_strstrip(cmd_str->str);
  cmd_str->len = strlen(cmd_str->str);

  output = get_output_file(input_file);

  g_string_replace(cmd_str, "$INPUT", input, 0);
  g_string_replace(cmd_str, "$OUTPUT", output, 0);

  cmd = g_string_free(cmd_str, FALSE);

  g_free(output);

  return cmd;
}

static void
validate_run_comment(struct file_ctx *ctx, const gchar *prefix)
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

  if (!line) {
    show_no_run_comment(ctx);
    return;
  }

  ctx->transform_cmd = get_transform_cmd(line, ctx->file, prefix);

  g_free(line);
  show_run_comment(ctx);
}

static void
setup_md_monitor(struct app_ctx *ctx, GFile *file)
{
  ctx->md_monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, NULL);
  g_signal_connect(ctx->md_monitor,
                   "changed",
                   G_CALLBACK(monitor_md_file_changes),
                   ctx);

  if (g_file_query_exists(file, NULL)) {
    handle_markdown_file(ctx, file);
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
  struct file_ctx *file_ctx = NULL;
  gchar *output = NULL;

  g_application_activate(self);

  for (gint i = 0; i < n_files; i++) {
    if (g_str_has_suffix(g_file_peek_path(files[i]), ".md")) {
      g_free(output);
      output = g_strdup(g_file_peek_path(files[i]));

    } else if (g_str_has_suffix(g_file_peek_path(files[i]), ".yaml")) {
      file_ctx = g_new0(struct file_ctx, 1);
      file_ctx->app_ctx = ctx;
      file_ctx->file = g_object_ref(files[i]);
      if (!output) {
        output = get_output_file(files[i]);
      }

      validate_run_comment(file_ctx, "#!");
    } else if (g_str_has_suffix(g_file_peek_path(files[i]), ".puml")) {
      file_ctx = g_new0(struct file_ctx, 1);
      file_ctx->app_ctx = ctx;
      file_ctx->file = g_object_ref(files[i]);

      if (!output) {
        output = get_output_file(files[i]);
      }

      validate_run_comment(file_ctx, "'!");
    } else {
      g_printerr("Unsupported file type: %s\n", g_file_peek_path(files[i]));
      continue;
    }
  }

  if (output) {
    GFile *output_file = g_file_new_for_path(output);
    setup_md_monitor(ctx, output_file);
    g_free(output);
    g_clear_object(&output_file);
  }
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
          ".table-header{background-color: #AAAAAA; }";

  provider = gtk_css_provider_new();
  display = gdk_display_get_default();
  gtk_css_provider_load_from_string(provider, style_light);
  gtk_style_context_add_provider_for_display(display,
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_USER);
}

static void
activate_cb(GtkApplication *app, gpointer user_data)
{
  GtkWidget *window = adw_application_window_new(app);
  GtkWidget *scroll = NULL;
  struct app_ctx *ctx = user_data;

  setup_styles();
  scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 300);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_ALWAYS);

  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), -1);

  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), scroll);
  ctx->window = g_object_ref_sink(window);
  ctx->scroll = g_object_ref_sink(scroll);

  gtk_window_set_default_size(GTK_WINDOW(window), 1200, 600);
  gtk_window_present(GTK_WINDOW(window));
}

int
main(int argc, char **argv)
{
  AdwApplication *app = NULL;
  struct app_ctx ctx = { 0 };

  ctx.id[0] = 'C';
  ctx.id[1] = 'T';
  ctx.id[2] = 'X';

  if (argc == 1) {
    g_printerr("Usage: %s <files-to-watch>\n", argv[0]);
    return 1;
  }

  app = adw_application_new("com.example.MarkdownParser",
                            G_APPLICATION_HANDLES_OPEN);
  g_signal_connect(app, "open", G_CALLBACK(open_cb), &ctx);
  g_signal_connect(app, "activate", G_CALLBACK(activate_cb), &ctx);

  return g_application_run(G_APPLICATION(app), argc, argv);
}
