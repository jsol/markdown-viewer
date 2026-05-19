#pragma once
#include <glib.h>
#include <adwaita.h>

typedef struct _toc toc_t;

typedef void (*toc_clicked_cb)(gpointer user_data);

toc_t *toc_new(GtkWidget *sidebar,
               GtkTextView *view,
               toc_clicked_cb cb,
               gpointer user_data);

gboolean toc_is_empty(toc_t *toc);

void toc_add_heading(toc_t *toc, const gchar *heading_text, int level);
void toc_scroll_to_heading(toc_t *toc, const gchar *heading_text);

GtkWidget *toc_get(toc_t *toc);

void toc_free(toc_t *toc);
