#include "html.h"
#include <glib.h>
#include <adwaita.h>

#include "table.h"

struct table_parse_ctx {
  table_t *table;
  gboolean active;
  gboolean in_header;
  GString *current_cell_text;
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
  g_warning("Error parsing HTML: %s", error->message);
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

  g_debug("Start element: %s", element_name);

  if (g_strcmp0(element_name, "table") == 0) {
    ctx->table = table_new(NULL, 0);
    ctx->active = TRUE;
  } else if (g_strcmp0(element_name, "thead") == 0) {
    ctx->in_header = TRUE;
    /* Thead could contain th directly without a tr, so start a new row here.
     * Empty rows will just be ignored by the table anyways
     */
    table_new_row(ctx->table, ctx->in_header);
  } else if (g_strcmp0(element_name, "tbody") == 0) {
    ctx->in_header = FALSE;
  } else if (g_strcmp0(element_name, "tr") == 0) {
    table_new_row(ctx->table, ctx->in_header);
  } else if (g_strcmp0(element_name, "td") == 0 ||
             g_strcmp0(element_name, "th") == 0) {
    if (ctx->current_cell_text) {
      g_warning("Starting new cell before finishing previous one");
      g_string_free(ctx->current_cell_text, TRUE);
    }
    ctx->current_cell_text = g_string_new("");
  }
}

static void
tbl_text(G_GNUC_UNUSED GMarkupParseContext *context,
         const char *text,
         gsize text_len,
         gpointer user_data,
         G_GNUC_UNUSED GError **error)
{
  struct table_parse_ctx *ctx = (struct table_parse_ctx *) user_data;

  if (!ctx->current_cell_text) {
    if (text_len > 0 && text[0] != '\n') {
      g_warning("Text outside of cell: %s", text);
    }
    return;
  }

  g_string_append_len(ctx->current_cell_text, text, text_len);
}

static void
tbl_end_element(G_GNUC_UNUSED GMarkupParseContext *context,
                const char *element_name,
                gpointer user_data,
                G_GNUC_UNUSED GError **error)
{
  struct table_parse_ctx *ctx = (struct table_parse_ctx *) user_data;

  g_debug("End element: %s", element_name);
  if (g_strcmp0(element_name, "td") == 0 ||
      g_strcmp0(element_name, "th") == 0) {
    if (!ctx->current_cell_text) {
      g_warning("Ending cell element without starting one");
      return;
    }
    table_add_cell(ctx->table, ctx->current_cell_text->str);
    g_string_free(ctx->current_cell_text, TRUE);
    ctx->current_cell_text = NULL;
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

  g_debug("Parsing table HTML: %s", html);

  if (!g_markup_parse_context_parse(ctx->context, html, -1, &error)) {
    g_warning("Failed to parse HTML: %s", error->message);
    g_clear_error(&error);
    return NULL;
  }

  if (ctx->table_ctx.active) {
    g_debug("Table parsing did not complete successfully.");
    return NULL;
  }
  return table_finalize(ctx->table_ctx.table);
}

GtkWidget *
html_parse(html_t *ctx, const char *html)
{
  g_debug("Parsing HTML: %s", html);
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
    g_warning("Error finalizing HTML parsing: %s", error->message);
    g_clear_error(&error);
  }
  g_markup_parse_context_free(ctx->context);
  g_free(ctx);
}
