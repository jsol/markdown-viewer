#include "color.h"

#include <adwaita.h>
#include <gtk/gtk.h>

typedef enum item_type { ITEM_TYPE_TAG_BG_PARAGRAPH } item_type_t;

#define CSS_STYLE                                                              \
  ".in-text-button {padding: 0px; margin: 0px; margin-bottom: -7px;}"          \
  "frame {border-radius: 0px;}"

#define CLASS_TABLE_HEADER   "table-header"
#define CLASS_TABLE_EVEN_ROW "table-even-row"
#define CLASS_TABLE_ODD_ROW  "table-odd-row"
#define CLASS_TEXT           "text"
#define CLASS_CODE           "code"

struct handle {
  enum color_type type;
  enum item_type item_type;

  union {
    GtkTextTag *tag;
  } item;
};

struct color_pair {
  enum color_type type;
  gchar *dark;
  gchar *light;
};

struct _color {
  AdwStyleManager *style_manager;
  GPtrArray *handles;
  GHashTable *colors;
  GtkCssProvider *provider;
};

static void
free_color_pair(gpointer data)
{
  struct color_pair *pair = data;

  if (pair == NULL) {
    return;
  }

  g_free(pair->dark);
  g_free(pair->light);
  g_free(pair);
}

static void
remove_handle(gpointer data, GObject *where_the_object_was)
{
  color_t *ctx = data;
  struct handle *handle = NULL;

  for (guint i = 0; i < ctx->handles->len; i++) {
    handle = ctx->handles->pdata[i];

    switch (handle->item_type) {
    case ITEM_TYPE_TAG_BG_PARAGRAPH:
      if ((void *) handle->item.tag == where_the_object_was) {
        g_ptr_array_remove_index(ctx->handles, i);
        goto cleanup;
      }
      break;
    }
  }
cleanup:
  g_free(handle);
}

static struct color_pair *
set_color(color_t *color,
          enum color_type type,
          const gchar *dark,
          const gchar *light)
{
  struct color_pair *pair;

  pair = g_new0(struct color_pair, 1);
  pair->type = type;
  pair->dark = g_strdup(dark);
  pair->light = g_strdup(light);

  g_hash_table_insert(color->colors, GINT_TO_POINTER(type), pair);

  return pair;
}

static void
initiate_colors(color_t *color)
{
  set_color(color, COLOR_TYPE_TABLE_BG_HEADER, "#3D3846", "#DEDDDA");
  set_color(color, COLOR_TYPE_TABLE_BG_EVEN_ROW, "#77767b", "#f6f5f4");
  set_color(color, COLOR_TYPE_TEXT, "#000000", "#000000");
  set_color(color, COLOR_TYPE_CODE, "#63452c", "#cdab8f");
}

static const gchar *
get_color_name(color_t *color, struct color_pair *pair)
{
  if (adw_style_manager_get_dark(color->style_manager)) {
    return pair->dark;
  } else {
    return pair->light;
  }
}

static void
apply_color(color_t *color, struct color_pair *pair)
{
  const gchar *color_name;

  color_name = get_color_name(color, pair);

  for (guint i = 0; i < color->handles->len; i++) {
    struct handle *handle = color->handles->pdata[i];
    if (pair->type != handle->type) {
      continue;
    }

    switch (handle->item_type) {
    case ITEM_TYPE_TAG_BG_PARAGRAPH:
      g_object_set(handle->item.tag, "paragraph-background", color_name, NULL);
      break;
    }
  }
}

static void
set_css(color_t *color, gboolean also_apply)
{
  GString *css;
  const gchar *color_name;
  GHashTableIter iter;
  gpointer key;
  gpointer value;

  css = g_string_new(CSS_STYLE);
  g_hash_table_iter_init(&iter, color->colors);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    struct color_pair *pair = value;

    if (also_apply) {
      apply_color(color, pair);
    }
    color_name = get_color_name(color, pair);

    switch (pair->type) {
    case COLOR_TYPE_TABLE_BG_HEADER:
      g_string_append_printf(css,
                             ".%s { background-color: %s; font-weight: bold; }",
                             CLASS_TABLE_HEADER,
                             color_name);
      break;

    case COLOR_TYPE_TABLE_BG_EVEN_ROW:
      g_string_append_printf(css,
                             ".%s { background-color: %s; }",
                             CLASS_TABLE_EVEN_ROW,
                             color_name);
      break;
    case COLOR_TYPE_TABLE_BG_ODD_ROW:
      g_string_append_printf(css,
                             ".%s { background-color: %s; }",
                             CLASS_TABLE_ODD_ROW,
                             color_name);
      break;
    case COLOR_TYPE_TEXT:
      g_string_append_printf(css, ".%s { color: %s; }", CLASS_TEXT, color_name);
      break;
    case COLOR_TYPE_CODE:
      g_string_append_printf(css, ".%s { color: %s; }", CLASS_CODE, color_name);
      break;
    }
  }
  gtk_css_provider_load_from_string(color->provider, css->str);
  g_string_free(css, TRUE);
}

static void
dark_changed_cb(G_GNUC_UNUSED GObject *object,
                G_GNUC_UNUSED GParamSpec *pspec,
                gpointer user_data)
{
  color_t *color = user_data;
  set_css(color, TRUE);
}

void
color_set_type(color_t *color,
               enum color_type type,
               const gchar *dark,
               const gchar *light)
{
  struct color_pair *pair;

  pair = set_color(color, type, dark, light);
  set_css(color, false);
  apply_color(color, pair);
}

void
color_handle_tag(color_t *color, enum color_type type, GtkTextTag *tag)
{
  struct handle *handle;
  struct color_pair *pair;
  const gchar *color_name;

  handle = g_new0(struct handle, 1);
  handle->type = type;
  handle->item_type = ITEM_TYPE_TAG_BG_PARAGRAPH;
  handle->item.tag = tag;

  g_ptr_array_add(color->handles, handle);

  g_object_weak_ref(G_OBJECT(tag), remove_handle, color);

  pair = g_hash_table_lookup(color->colors, GINT_TO_POINTER(type));
  if (pair == NULL) {
    return;
  }

  color_name = get_color_name(color, pair);

  g_object_set(tag, "paragraph-background", color_name, NULL);
}

void
color_handle_widget(enum color_type type, GtkWidget *w)
{
  switch (type) {
  case COLOR_TYPE_TABLE_BG_HEADER:
    gtk_widget_add_css_class(w, CLASS_TABLE_HEADER);
    break;
  case COLOR_TYPE_TABLE_BG_EVEN_ROW:
    gtk_widget_add_css_class(w, CLASS_TABLE_EVEN_ROW);
    break;
  case COLOR_TYPE_TABLE_BG_ODD_ROW:
    gtk_widget_add_css_class(w, CLASS_TABLE_ODD_ROW);
    break;
  case COLOR_TYPE_TEXT:
    gtk_widget_add_css_class(w, CLASS_TEXT);
    break;
  case COLOR_TYPE_CODE:
    break;
  }
}

color_t *
color_new(void)
{
  color_t *color;
  GdkDisplay *display;

  color = g_new0(color_t, 1);

  color->handles = g_ptr_array_new();

  color->provider = gtk_css_provider_new();
  display = gdk_display_get_default();
  gtk_style_context_add_provider_for_display(display,
                                             GTK_STYLE_PROVIDER(
                                                     color->provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_USER);
  color->style_manager = adw_style_manager_get_for_display(display);
  color->colors = g_hash_table_new_full(g_direct_hash,
                                        g_direct_equal,
                                        NULL,
                                        free_color_pair);

  g_signal_connect(color->style_manager,
                   "notify::dark",
                   G_CALLBACK(dark_changed_cb),
                   color);

  initiate_colors(color);
  set_css(color, FALSE);

  return color;
}
