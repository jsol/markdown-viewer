#include "html.h"
#include <glib.h>
#include <adwaita.h>

struct table_parse_ctx {
  GtkWidget *table;
  gint current_row;
  gint current_col;
  GtkWidget *current_cell;
  gboolean active;
};

struct _html {
  struct table_parse_ctx table_ctx;
  GMarkupParseContext *context;
};

static void
tbl_error(G_GNUC_UNUSED GMarkupParseContext *context,
          GError *error,
          G_GNUC_UNUSED gpointer user_data)
{
  g_printerr("Error parsing HTML: %s\n", error->message);
}

static void
tbl_start_element(G_GNUC_UNUSED GMarkupParseContext *context,
                  const char *element_name,
                  G_GNUC_UNUSED const char **attribute_names,
                  G_GNUC_UNUSED const char **attribute_values,
                  gpointer user_data,
                  G_GNUC_UNUSED GError **error)
{
  struct table_parse_ctx *ctx = (struct table_parse_ctx *) user_data;

  g_message("Start element: %s", element_name);

  if (g_strcmp0(element_name, "table") == 0) {
    ctx->table = gtk_grid_new();
    ctx->current_row = -1;
    ctx->current_col = -1;
    ctx->active = TRUE;
    ctx->current_cell = NULL;
    ctx->table = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(ctx->table), 5);
    gtk_grid_set_column_spacing(GTK_GRID(ctx->table), 5);
  } else if (g_strcmp0(element_name, "thead") == 0) {
    ctx->current_col = 0;
    ctx->current_row = 0;
  } else if (g_strcmp0(element_name, "tr") == 0) {
    ctx->current_col = 0;
    ctx->current_row++;
  } else if (g_strcmp0(element_name, "td") == 0 ||
             g_strcmp0(element_name, "th") == 0) {
    GtkWidget *frame = gtk_frame_new(NULL);
    GtkWidget *label = gtk_label_new(NULL);

    gtk_frame_set_child(GTK_FRAME(frame), label);
    gtk_grid_attach(GTK_GRID(ctx->table),
                    frame,
                    ctx->current_col,
                    ctx->current_row,
                    1,
                    1);
    ctx->current_col++;
    ctx->current_cell = label;

    if (g_strcmp0(element_name, "th") == 0) {
      gtk_widget_add_css_class(frame, "table-header");
    } else {
      gtk_widget_set_halign(label, GTK_ALIGN_START);
    }
  }
}

static void
tbl_text(G_GNUC_UNUSED GMarkupParseContext *context,
         const char *text,
         G_GNUC_UNUSED gsize text_len,
         gpointer user_data,
         G_GNUC_UNUSED GError **error)
{
  struct table_parse_ctx *ctx = (struct table_parse_ctx *) user_data;
  const gchar *old_text;
  gchar *new_text;

  if (ctx->current_cell == NULL) {
    return;
  }
  g_message("Text element: %s", text);

  old_text = gtk_label_get_text(GTK_LABEL(ctx->current_cell));
  new_text = g_strconcat(old_text, " ", text, NULL);

  gtk_label_set_text(GTK_LABEL(ctx->current_cell), new_text);
  g_free(new_text);
}

static void
tbl_end_element(G_GNUC_UNUSED GMarkupParseContext *context,
                const char *element_name,
                gpointer user_data,
                G_GNUC_UNUSED GError **error)
{
  struct table_parse_ctx *ctx = (struct table_parse_ctx *) user_data;

  g_message("End element: %s", element_name);
  if (g_strcmp0(element_name, "td") == 0 ||
      g_strcmp0(element_name, "th") == 0) {
    ctx->current_cell = NULL;
  }

  if (g_strcmp0(element_name, "table") == 0) {
    ctx->active = FALSE;
  }
}

static GtkWidget *
html_parse_table(html_t *ctx, const char *html)
{
  GError *error = NULL;

  g_assert(html != NULL);

  g_message("Parsing table HTML: %s", html);

  if (!g_markup_parse_context_parse(ctx->context, html, -1, &error)) {
    g_printerr("Failed to parse HTML: %s\n", error->message);
    g_clear_error(&error);
    return NULL;
  }

  if (ctx->table_ctx.active) {
    g_message("Table parsing did not complete successfully.\n");
    return NULL;
  }
  return ctx->table_ctx.table;
}

GtkWidget *
html_parse(html_t *ctx, const char *html)
{
  g_message("Parsing HTML: %s", html);
  if (ctx->table_ctx.active || g_str_has_prefix(html, "<table")) {
    return html_parse_table(ctx, html);
  }
  return NULL;
}

html_t *
html_new(void)
{
  static GMarkupParser parser = { 0 };
  html_t *ctx = g_new0(html_t, 1);

  parser.error = tbl_error;
  parser.start_element = tbl_start_element;
  parser.end_element = tbl_end_element;
  parser.text = tbl_text;
  ctx->context = g_markup_parse_context_new(&parser, 0, &ctx->table_ctx, NULL);

  return ctx;
}

void
html_free(html_t *ctx)
{
  GError *error = NULL;
  if (!ctx) {
    return;
  }
  if (!g_markup_parse_context_end_parse(ctx->context, &error)) {
    g_warning("Error finalizing HTML parsing: %s\n", error->message);
    g_clear_error(&error);
  }
  g_markup_parse_context_free(ctx->context);
  g_free(ctx);
}
