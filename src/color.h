#pragma once

#include <adwaita.h>

enum color_type {
  COLOR_TYPE_CODE,
  COLOR_TYPE_TABLE_BG_HEADER,
  COLOR_TYPE_TABLE_BG_EVEN_ROW,
  COLOR_TYPE_TABLE_BG_ODD_ROW,
  COLOR_TYPE_TEXT
};

typedef struct _color color_t;

color_t *color_new(void);

void color_set_type(color_t *color,
                    enum color_type type,
                    const gchar *dark,
                    const gchar *light);

void color_handle_tag(color_t *color, enum color_type type, GtkTextTag *tag);
void color_handle_widget(enum color_type type, GtkWidget *widget);

void color_free(color_t *color);
