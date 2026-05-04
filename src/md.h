#pragma once

#include <glib.h>
#include <adwaita.h>

#include "listener.h"

typedef struct md md_t;

md_t *md_new(listener_t *listener, GtkScrolledWindow *scroll);

void md_free(md_t *md);

