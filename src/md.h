#pragma once

#include <glib.h>
#include <adwaita.h>
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

#include "listener.h"
#include "color.h"

typedef struct md md_t;

typedef void (*display_md)(const gchar *path, gpointer user_data);

md_t *md_new(listener_t *listener,
             GtkBox *toc,
             color_t *color,
             display_md display,
             gpointer user_data);

GtkWidget *md_get_view(md_t *md);

void md_free(md_t *md);
