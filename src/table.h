#pragma once
#include <glib.h>
#include <adwaita.h>

enum table_align {
  TABLE_ALIGN_DEFAULT = 0,
  TABLE_ALIGN_LEFT = 'l',
  TABLE_ALIGN_CENTER = 'c',
  TABLE_ALIGN_RIGHT = 'r',
};

typedef struct _table table_t;

table_t *table_new(guint8 *align, guint16 num_align);

void table_new_row(table_t *table, gboolean header);

void table_add_cell(table_t *table, const gchar *text);

GtkWidget *table_finalize(table_t *table);
