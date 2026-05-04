#include "gio/gio.h"
#include "glib-object.h"
#include <adwaita.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "gtk/gtk.h"
#include "listener.h"
#include "md.h"

struct app_ctx {
  gchar id[4];

  GtkWidget *window;
  GtkWidget *scroll;
  GPtrArray *file_listeners;

  md_t *md;
};

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

  g_application_activate(self);

  for (gint i = 0; i < n_files; i++) {
    listener_t *listener = listener_new(files[i], handle_run_comment, ctx);
    g_ptr_array_add(ctx->file_listeners, listener);
    if (listener_is_md(listener)) {
      if (ctx->md) {
        g_warning("Multiple markdown files found");
        continue;
      }
      ctx->md = md_new(listener, GTK_SCROLLED_WINDOW(ctx->scroll));
    }
  }

  if (!ctx->md) {
    GFile *markdown_file;
    listener_t *listener;

    markdown_file = listener_get_output_file(ctx->file_listeners->pdata[0]);
    listener = listener_new(markdown_file, handle_run_comment, ctx);
    g_ptr_array_add(ctx->file_listeners, listener);

    ctx->md = md_new(listener, GTK_SCROLLED_WINDOW(ctx->scroll));

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
