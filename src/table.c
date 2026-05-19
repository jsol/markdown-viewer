#include <glib.h>
#include <adwaita.h>

#include "table.h"

#include "color.h"

struct _table {
  guint num_align;
  guint *align;

  GtkWidget *table;
  gint current_col;
  gint current_row;

  gboolean current_row_is_header;
};

table_t *
table_new(guint8 *align, guint16 num_col)
{
  table_t *table = g_new0(table_t, 1);
  table->num_align = num_col;

  if (align) {
    table->align = g_new(guint, num_col);
    memcpy(table->align, align, num_col * sizeof(guint));
  } else {
    table->num_align = 0;
  }

  table->table = gtk_grid_new();
  gtk_widget_set_hexpand(table->table, TRUE);

  table->current_col = 0;
  table->current_row = -1;

  return table;
}

void
table_new_row(table_t *ctx, gboolean header)
{
  ctx->current_row_is_header = header;

  ctx->current_row++;
  ctx->current_col = 0;
}

void
table_add_cell(table_t *ctx, const gchar *text)
{
  if (ctx->current_row < 0) {
    g_warning("Adding cell before starting first row");
    return;
  }

  GtkWidget *label = gtk_label_new(text);
  GtkWidget *frame = gtk_frame_new(NULL);

  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap(GTK_LABEL(label), FALSE);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_START);

  gtk_frame_set_child(GTK_FRAME(frame), label);
  gtk_widget_set_hexpand(frame, TRUE);

  if (ctx->current_row_is_header) {
    color_handle_widget(COLOR_TYPE_TABLE_BG_HEADER, frame);
  } else {
    if (ctx->current_row % 2 == 0) {
      color_handle_widget(COLOR_TYPE_TABLE_BG_EVEN_ROW, frame);
    } else {
      color_handle_widget(COLOR_TYPE_TABLE_BG_ODD_ROW, frame);
    }
  }

  if (ctx->current_col < (gint) ctx->num_align) {
    switch (ctx->align[ctx->current_col]) {
    case TABLE_ALIGN_LEFT:
    case TABLE_ALIGN_DEFAULT:
      break;
    case TABLE_ALIGN_CENTER:
      gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
      break;
    case TABLE_ALIGN_RIGHT:
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      break;
    }
  }

  gtk_grid_attach(GTK_GRID(ctx->table),
                  frame,
                  ctx->current_col,
                  ctx->current_row,
                  1,
                  1);

  ctx->current_col++;
}

GtkWidget *
table_finalize(table_t *table)
{
  GtkWidget *result = table->table;

  g_free(table->align);
  g_free(table);

  return result;
}
