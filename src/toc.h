#pragma once
#include <glib.h>
#include <adwaita.h>

typedef struct _toc toc_t;

toc_t *toc_new(GtkWidget *sidebar);

gboolean toc_is_empty(toc_t *toc);

GtkWidget *toc_add_heading(toc_t *toc, const gchar *heading_text, int level);

GtkWidget *toc_get(toc_t *toc);

void toc_free(toc_t *toc);
