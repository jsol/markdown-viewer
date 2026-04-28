#include <glib.h>
#include <adwaita.h>

#include "toc.h"

struct _toc {
  GtkWidget *table;
  gint heading_counts[6];
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
  if (level < 1 || level > 6) {
    return g_strdup("");
  }

  GString *prefix = g_string_new("");

  for (int i = 1; i <= level; i++) {
    g_string_append_printf(prefix, "%d.", toc->heading_counts[i]);
  }

  prefix->len--; // Remove the trailing dot

  return g_string_free(prefix, FALSE);
}

toc_t *
toc_new(void)
{
  toc_t *toc = g_new0(toc_t, 1);

  for (gulong i = 0; i < G_N_ELEMENTS(toc->heading_counts); i++) {
    toc->heading_counts[i] = 0;
  }

  toc->table = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_VERTICAL, 5));

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

GtkWidget *
toc_add_heading(toc_t *toc, const gchar *heading_text, int level)
{
  gchar class_name[12] = { 0 };
  gchar *heading_text_prefixed;
  gchar *prefix;
  GtkWidget *heading;
  GtkWidget *toc_item;

  g_return_val_if_fail(toc != NULL, NULL);
  g_return_val_if_fail(heading_text != NULL, NULL);

  g_message("Adding heading: %s (level %d)", heading_text, level);

  add_count(toc, level);
  prefix = get_prefix(toc, level);
  heading_text_prefixed = g_strdup_printf("%s %s", prefix, heading_text);
  g_free(prefix);

  snprintf(class_name, sizeof(class_name), "heading-%d", level);

  heading = gtk_label_new(heading_text_prefixed);
  toc_item = gtk_label_new(heading_text_prefixed);
  gtk_widget_add_css_class(heading, class_name);

  g_assert(toc->table);
  if (GTK_IS_BOX(toc->table)) {
    g_message("TOC table is a box");
  } else {
    g_warning("TOC table is not a box");
  }
  gtk_box_append(GTK_BOX(toc->table), toc_item);

  return heading;
}

GtkWidget *
toc_get(toc_t *toc)
{
  g_message("Getting TOC widget");
  return g_object_ref(toc->table);
}

void
toc_free(toc_t *toc)
{
  if (!toc) {
    return;
  }

  g_object_unref(toc->table);
  g_free(toc);
}
