#pragma once

#include <glib.h>
#include <adwaita.h>
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

#include "listener.h"

typedef struct md md_t;
extern cmark_node_type CMARK_NODE_TABLE, CMARK_NODE_TABLE_ROW,
        CMARK_NODE_TABLE_CELL;

md_t *
md_new(listener_t *listener, GtkScrolledWindow *scroll, GtkScrolledWindow *toc);

void md_free(md_t *md);
