#include <glib.h>
#include <adwaita.h>

#include "toc.h"

#define OBJ_DATA_CTX "ctx"

struct _toc {
  GtkWidget *table;
  GtkWidget *sidebar;
  GtkTextView *view;
  gint heading_counts[7];

  GHashTable *mark_table;

  toc_clicked_cb cb;
  gpointer user_data;
};

static void
add_count(toc_t *toc, int level)
{
  if (level < 1 || level > 6) {
    return;
  }

  toc->heading_counts[level]++;

  for (gulong i = level + 1; i < G_N_ELEMENTS(toc->heading_counts); i++) {
    toc->heading_counts[i] = 0;
  }
}

static gchar *
get_prefix(toc_t *toc, int level)
{
  gboolean all_zeros = TRUE;
  if (level < 1 || level > 6) {
    return g_strdup("");
  }

  GString *prefix = g_string_new("");

  for (int i = 1; i <= level; i++) {
    if (toc->heading_counts[i] == 0 && all_zeros) {
      continue;
    }

    all_zeros = FALSE;
    g_string_append_printf(prefix, "%d.", toc->heading_counts[i]);
  }

  prefix->len--; // Remove the trailing dot

  return g_string_free(prefix, FALSE);
}

toc_t *
toc_new(GtkWidget *box,
        GtkTextView *view,
        toc_clicked_cb cb,
        gpointer user_data)
{
  toc_t *toc = g_new0(toc_t, 1);

  for (gulong i = 0; i < G_N_ELEMENTS(toc->heading_counts); i++) {
    toc->heading_counts[i] = 0;
  }

  toc->mark_table =
          g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  toc->sidebar = box;
  toc->view = view;
  toc->table = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_VERTICAL, 5));

  toc->cb = cb;
  toc->user_data = user_data;
  return toc;
}

gboolean
toc_is_empty(toc_t *toc)
{
  for (gulong i = 0; i < G_N_ELEMENTS(toc->heading_counts); i++) {
    if (toc->heading_counts[i] > 0) {
      return FALSE;
    }
  }
  return TRUE;
}

static void
sidebar_item_clicked(GtkWidget *button, gpointer user_data)
{
  toc_t *toc = g_object_get_data(G_OBJECT(button), OBJ_DATA_CTX);
  GtkTextMark *mark = user_data;

  g_assert(toc);
  g_assert(mark);

  if (toc->cb) {
    toc->cb(toc->user_data);
  }

  gtk_text_view_scroll_to_mark(toc->view, mark, 0.0, TRUE, 0.0, 0.0);
}

static GtkTextMark *
add_mark(GtkTextView *view)
{
  GtkTextMark *mark;
  GtkTextIter iter;
  GtkTextBuffer *buffer;

  buffer = gtk_text_view_get_buffer(view);
  gtk_text_buffer_get_end_iter(buffer, &iter);

  mark = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);

  return mark;
}

void
toc_add_heading(toc_t *toc, const gchar *heading_text, int level)
{
  gchar *heading_text_prefixed;
  gchar *prefix;
  GtkWidget *toc_item;
  GtkWidget *sidebar_item;
  GtkTextMark *mark;

  g_return_if_fail(toc != NULL);
  g_return_if_fail(heading_text != NULL);

  g_debug("Adding heading: %s (level %d)", heading_text, level);

  add_count(toc, level);
  prefix = get_prefix(toc, level);
  heading_text_prefixed = g_strdup_printf("%s %s", prefix, heading_text);
  g_free(prefix);

  toc_item = gtk_label_new(heading_text_prefixed);

  g_assert(toc->table);
  gtk_box_append(GTK_BOX(toc->table), toc_item);

  sidebar_item = gtk_button_new_with_label(heading_text_prefixed);
  gtk_widget_set_halign(sidebar_item, GTK_ALIGN_START);
  gtk_widget_set_margin_start(sidebar_item, 20);
  gtk_widget_set_margin_end(sidebar_item, 20);
  gtk_widget_add_css_class(sidebar_item, "sidebar-item");

  mark = add_mark(toc->view);
  g_object_set_data(G_OBJECT(sidebar_item), OBJ_DATA_CTX, toc);

  g_signal_connect(sidebar_item,
                   "clicked",
                   G_CALLBACK(sidebar_item_clicked),
                   mark);

  g_hash_table_insert(toc->mark_table, g_strdup(heading_text), mark);

  gtk_box_append(GTK_BOX(toc->sidebar), sidebar_item);
  g_debug("Added sidebar item: %s", heading_text_prefixed);
}

GtkWidget *
toc_get(toc_t *toc)
{
  return g_object_ref(toc->table);
}

void
toc_scroll_to_heading(toc_t *toc, const gchar *heading_text)
{
  GtkTextMark *mark;

  g_return_if_fail(toc != NULL);
  g_return_if_fail(heading_text != NULL);

  mark = g_hash_table_lookup(toc->mark_table, heading_text);

  if (!mark) {
    g_warning("No mark found for heading: %s", heading_text);
    return;
  }

  g_message("Scrolling to mark: %p", mark);
  gtk_text_view_scroll_to_mark(toc->view, mark, 0.0, TRUE, 0.0, 0.0);
}

void
toc_free(toc_t *toc)
{
  if (!toc) {
    return;
  }

  g_clear_pointer(&toc->mark_table, g_hash_table_unref);
  g_object_unref(toc->table);
  g_free(toc);
}
