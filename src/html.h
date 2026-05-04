#pragma once

#include <glib.h>
#include <adwaita.h>
typedef struct _html html_t;

html_t *html_new(void);

GtkWidget *html_parse(html_t *ctx, const char *html);

void html_free(html_t *ctx);
