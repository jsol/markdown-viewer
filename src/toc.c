#include <glib.h>
#include <adwaita.h>

#include "toc.h"

#define OBJ_DATA_HEADING "heading"

struct _toc {
  GtkWidget *table;
  GtkWidget *sidebar;
  gint heading_counts[7];
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
toc_new(GtkWidget *box)
{
  toc_t *toc = g_new0(toc_t, 1);

  for (gulong i = 0; i < G_N_ELEMENTS(toc->heading_counts); i++) {
    toc->heading_counts[i] = 0;
  }

  toc->sidebar = box;
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

static void
sidebar_item_clicked(GtkWidget *button, G_GNUC_UNUSED gpointer user_data)
{
  GtkWidget *heading = g_object_get_data(G_OBJECT(button), OBJ_DATA_HEADING);
  gboolean selectable = gtk_label_get_selectable(GTK_LABEL(heading));

  gtk_widget_set_focus_on_click(heading, TRUE);
  gtk_label_set_selectable(GTK_LABEL(heading), TRUE);
  if (!gtk_widget_grab_focus(heading)) {
    g_message("Could not focus heading");
  }
  gtk_widget_set_focus_on_click(heading, FALSE);
  gtk_label_set_selectable(GTK_LABEL(heading), selectable);

  g_debug("Sidebar item clicked: %s", gtk_label_get_text(GTK_LABEL(heading)));
}

GtkWidget *
toc_add_heading(toc_t *toc, const gchar *heading_text, int level)
{
  gchar class_name[12] = { 0 };
  gchar *heading_text_prefixed;
  gchar *prefix;
  GtkWidget *heading;
  GtkWidget *toc_item;
  GtkWidget *sidebar_item;

  g_return_val_if_fail(toc != NULL, NULL);
  g_return_val_if_fail(heading_text != NULL, NULL);

  g_debug("Adding heading: %s (level %d)", heading_text, level);

  add_count(toc, level);
  prefix = get_prefix(toc, level);
  heading_text_prefixed = g_strdup_printf("%s %s", prefix, heading_text);
  g_free(prefix);

  snprintf(class_name, sizeof(class_name), "heading-%d", level);

  heading = gtk_label_new(heading_text_prefixed);
  toc_item = gtk_label_new(heading_text_prefixed);
  gtk_widget_add_css_class(heading, class_name);

  g_assert(toc->table);
  gtk_box_append(GTK_BOX(toc->table), toc_item);

  sidebar_item = gtk_button_new_with_label(heading_text_prefixed);
  gtk_widget_set_halign(sidebar_item, GTK_ALIGN_START);
  gtk_widget_set_margin_start(sidebar_item, 20);
  gtk_widget_set_margin_end(sidebar_item, 20);
  gtk_widget_add_css_class(sidebar_item, "sidebar-item");
  g_object_set_data(G_OBJECT(sidebar_item), OBJ_DATA_HEADING, heading);
  g_signal_connect(sidebar_item,
                   "clicked",
                   G_CALLBACK(sidebar_item_clicked),
                   NULL);
  gtk_box_append(GTK_BOX(toc->sidebar), sidebar_item);
  g_debug("Added sidebar item: %s", heading_text_prefixed);

  return heading;
}

GtkWidget *
toc_get(toc_t *toc)
{
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
