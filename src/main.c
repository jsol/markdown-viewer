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
#include "listener.h"
#include "toc.h"

#define OBJ_DATA_IMG "image"

struct app_ctx {
  gchar id[4];

  GtkWidget *box;
  GtkWidget *scroll;
  GtkWidget *window;
  gchar *root_path;
  GPtrArray *image_monitors;
  GPtrArray *file_listeners;
  toc_t *toc;
  html_t *html;
};

struct file_ctx {
  GFile *file;
  GFileMonitor *monitor;
  gchar *transform_cmd;
  struct app_ctx *app_ctx;
};

static void print_node(cmark_node *node, int indent) {
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

static gboolean allowed_html(const char *html) {
  const char *allowed_tags[] = {"<b>",  "</b>",  "<i>",      "</i>",
                                "<u>",  "</u>",  "<strong>", "</strong>",
                                "<em>", "</em>", NULL};

  for (int i = 0; allowed_tags[i] != NULL; i++) {
    if (g_strcmp0(html, allowed_tags[i]) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

static GFile *normalize_url(struct app_ctx *ctx, const char *url) {
  GFile *dirname_file = NULL;
  GFile *image_file = NULL;

  if (g_str_has_prefix(url, "/")) {
    return g_file_new_for_path(url);
  }

  if (!ctx->root_path) {
    return g_file_new_for_path(url);
  }

  dirname_file = g_file_new_for_path(ctx->root_path);

  image_file = g_file_resolve_relative_path(dirname_file, url);

  g_clear_object(&dirname_file);

  return image_file;
}

static void display_html_table(html_t *ctx, const char *html, GtkWidget *box) {
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

static GtkWidget *display_image(GFile *file, GtkWidget *old_image,
                                GtkWidget *box) {
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

static void monitor_image_changes(GFileMonitor *monitor, GFile *file,
                                  G_GNUC_UNUSED GFile *other_file,
                                  GFileMonitorEvent event_type,
                                  G_GNUC_UNUSED gpointer user_data) {
  if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
      event_type == G_FILE_MONITOR_EVENT_CREATED) {
    g_message("Image changed: %s", g_file_peek_path(file));
    GtkWidget *old_image = g_object_get_data(G_OBJECT(monitor), OBJ_DATA_IMG);

    GtkWidget *new_image =
        display_image(file, old_image, gtk_widget_get_parent(old_image));
    g_object_set_data_full(G_OBJECT(monitor), OBJ_DATA_IMG,
                           g_object_ref_sink(new_image), g_object_unref);
  }
}

static void display_paragraph(struct app_ctx *ctx, cmark_node *node,
                              GtkWidget *box) {
  cmark_node *child = cmark_node_first_child(node);
  GString *paragraph_text = g_string_new("");

  while (child) {
    switch (cmark_node_get_type(child)) {
    case CMARK_NODE_IMAGE: {
      GFileMonitor *monitor = NULL;

      GFile *url = normalize_url(ctx, cmark_node_get_url(child));

      GtkWidget *image = display_image(url, NULL, box);

      monitor = g_file_monitor_file(url, G_FILE_MONITOR_NONE, NULL, NULL);

      g_object_set_data_full(G_OBJECT(monitor), OBJ_DATA_IMG,
                             g_object_ref_sink(image), g_object_unref);
      g_ptr_array_add(ctx->image_monitors, monitor);
      g_signal_connect(monitor, "changed", G_CALLBACK(monitor_image_changes),
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

static void display_markdown(struct app_ctx *ctx, cmark_node *node,
                             GtkWidget *box) {
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

    label = toc_add_heading(ctx->toc, cmark_node_get_literal(heading_child),
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

static void parse_markdown(struct app_ctx *ctx, const char *markdown) {
  cmark_node *doc =
      cmark_parse_document(markdown, strlen(markdown), CMARK_OPT_DEFAULT);

  print_node(doc, 0);
  display_markdown(ctx, doc, ctx->box);
  cmark_node_free(doc);
}

static void handle_markdown(listener_t *listener, const gchar *markdown,
                            gpointer user_data) {
  struct app_ctx *ctx = user_data;

  g_message("Handling markdonw: %s", ctx->id);

  g_clear_pointer(&ctx->root_path, g_free);
  g_clear_pointer(&ctx->toc, toc_free);
  g_clear_pointer(&ctx->html, html_free);
  g_clear_pointer(&ctx->image_monitors, g_ptr_array_unref);

  ctx->root_path = g_path_get_dirname(listener_get_file_path(listener));

  ctx->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  ctx->image_monitors = g_ptr_array_new_with_free_func(g_object_unref);

  ctx->toc = toc_new();
  ctx->html = html_new();

  gtk_box_set_homogeneous(GTK_BOX(ctx->box), FALSE);
  gtk_widget_set_halign(ctx->box, GTK_ALIGN_START);
  gtk_widget_set_margin_start(ctx->box, 20);
  gtk_widget_set_margin_end(ctx->box, 20);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ctx->scroll), ctx->box);

  parse_markdown(ctx, markdown);
}

static void show_no_run_comment(struct app_ctx *ctx, const gchar *path) {
  AdwDialog *dialog = adw_dialog_new();
  gchar *text;

  GtkWidget *label;

  text = g_strdup_printf("The file %s needs to contain a #!<command> comment "
                         "to be executed. use $INPUT for a path to the raw "
                         "file and $OUTPUT for the path to the output md file.",
                         path);

  label = gtk_label_new(text);

  adw_dialog_set_title(dialog, "No run comment found");
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);

  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_margin_start(label, 20);
  gtk_widget_set_margin_end(label, 20);
  gtk_widget_set_margin_top(label, 20);
  gtk_widget_set_margin_bottom(label, 20);

  adw_dialog_set_child(dialog, label);
  adw_dialog_present(dialog, ctx->window);
}

static void show_run_comment(struct app_ctx *ctx, const gchar *run_cmd,
                             listener_t *listener) {
  AdwDialog *dialog = adw_dialog_new();
  GtkWidget *button_yes = NULL;
  GtkWidget *button_no = NULL;
  GtkWidget *grid;

  adw_dialog_set_title(dialog, "Allow run command");

  gchar *text = g_strdup_printf(
      "Do you wish to run \"%s\" to transform this file?", run_cmd);

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

  g_signal_connect_swapped(button_yes, "clicked",
                           G_CALLBACK(listener_approve_run), listener);
  g_signal_connect_swapped(button_yes, "clicked", G_CALLBACK(adw_dialog_close),
                           dialog);
  g_signal_connect_swapped(button_no, "clicked", G_CALLBACK(adw_dialog_close),
                           dialog);

  adw_dialog_present(dialog, ctx->window);
}

static void handle_run_comment(listener_t *listener, const gchar *run_cmd,
                               gpointer user_data) {
  struct app_ctx *ctx = user_data;

  if (run_cmd == NULL) {
    show_no_run_comment(ctx, listener_get_file_path(listener));
    return;
  }

  show_run_comment(ctx, run_cmd, listener);
}

static void open_cb(GApplication *self, gpointer files_pointer, gint n_files,
                    G_GNUC_UNUSED gchar *hint, gpointer user_data) {
  GFile **files = (GFile **)files_pointer;
  struct app_ctx *ctx = user_data;
  gboolean has_markdown = FALSE;

  g_application_activate(self);

  for (gint i = 0; i < n_files; i++) {
    listener_t *listener =
        listener_new(files[i], handle_run_comment, handle_markdown, ctx);
    g_ptr_array_add(ctx->file_listeners, listener);
    has_markdown = has_markdown || listener_is_md(listener);
  }

  if (!has_markdown) {
    GFile *markdown_file;
    listener_t *listener;

    markdown_file = listener_get_output_file(ctx->file_listeners->pdata[0]);
    listener =
        listener_new(markdown_file, handle_run_comment, handle_markdown, ctx);
    g_ptr_array_add(ctx->file_listeners, listener);

    g_clear_object(&markdown_file);
  }
}

static void setup_styles(void) {
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
  gtk_style_context_add_provider_for_display(
      display, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
}

static void activate_cb(GtkApplication *app, gpointer user_data) {
  GtkWidget *window = adw_application_window_new(app);
  GtkWidget *scroll = NULL;
  struct app_ctx *ctx = user_data;

  setup_styles();
  scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 300);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), -1);

  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), scroll);
  ctx->window = g_object_ref_sink(window);
  ctx->scroll = g_object_ref_sink(scroll);

  gtk_window_set_default_size(GTK_WINDOW(window), 1200, 600);
  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
  AdwApplication *app = NULL;
  struct app_ctx ctx = {0};

  ctx.id[0] = 'C';
  ctx.id[1] = 'T';
  ctx.id[2] = 'X';

  if (argc == 1) {
    g_printerr("Usage: %s <files-to-watch>\n", argv[0]);
    return 1;
  }

  ctx.file_listeners =
      g_ptr_array_new_with_free_func((GDestroyNotify)listener_free);
  app = adw_application_new("com.example.MarkdownParser",
                            G_APPLICATION_HANDLES_OPEN);
  g_signal_connect(app, "open", G_CALLBACK(open_cb), &ctx);
  g_signal_connect(app, "activate", G_CALLBACK(activate_cb), &ctx);

  return g_application_run(G_APPLICATION(app), argc, argv);
}
