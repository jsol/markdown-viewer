#include <glib.h>
#include "md.h"
#include "gtk/gtk.h"
#include "html.h"
#include "listener.h"
#include "toc.h"
#include "table.h"

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

#define OBJ_DATA_IMG_PATH "image"
#define OBJ_DATA_URL      "url"

#define TAG_BOLD          "bold"
#define TAG_ITALIC        "italic"
#define TAG_CODE          "code"
#define TAG_STRIKETHROUGH "strikethrough"
#define TAG_SUPERSCRIPT   "superscript"
#define TAG_HORIZONTAL    "horizontal"
#define TAG_H1            "h1"
#define TAG_H2            "h2"
#define TAG_H3            "h3"
#define TAG_H4            "h4"
#define TAG_H5            "h5"
#define TAG_H6            "h6"

#define RULER "\n•                                                  •\n"

struct md {
  GtkWidget *toc_parent;
  GtkWidget *toc_box;
  GtkWidget *footnotes_grid;

  GtkWidget *view;
  GtkWidget *scroll;
  GtkTextBuffer *buffer;
  GtkTextTagTable *tag_table;
  GHashTable *current_tags;

  display_md display;
  gpointer display_user_data;

  gint footnote_num;
  gchar *root_path;
  listener_t *listener;
  toc_t *toc;
  html_t *html;
  GHashTable *image_listeners;
  GPtrArray *current_images;
};

static void display_list(md_t *ctx, cmark_node *list_node, guint indent);

static void
print_node(cmark_node *node, int indent)
{
  for (int i = 0; i < indent; i++) {
    g_print("  ");
  }
  cmark_node *child = cmark_node_first_child(node);

  if (cmark_node_get_type(node) == CMARK_NODE_TEXT) {
    g_print("TEXT: %s\n", cmark_node_get_literal(node));
  } else if (cmark_node_get_type(node) == CMARK_NODE_HTML_INLINE) {
    g_print("HTML INLINE: %s\n", cmark_node_get_literal(node));
  } else if (cmark_node_get_type(node) == CMARK_NODE_HTML_BLOCK) {
    g_print("HTML BLOCK: %s\n", cmark_node_get_literal(node));
  } else {
    g_print("%s\n", cmark_node_get_type_string(node));
  }

  while (child) {
    print_node(child, indent + 1);
    child = cmark_node_next(child);
  }
}

static GtkWidget *
label_new(const gchar *text)
{
  GtkWidget *label;

  g_assert(text);

  label = gtk_label_new(text);

  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_START);

  return label;
}

static void
append_text(md_t *ctx, const gchar *text)
{
  GtkTextIter start_iter;
  GtkTextIter end_iter;
  GtkTextMark *mark = NULL;
  GHashTableIter tag_iter = { 0 };
  gpointer tag_key, tag_value;

  g_assert(ctx);
  g_assert(ctx->buffer);
  g_assert(ctx->current_tags);
  g_assert(ctx->tag_table);
  g_assert(text);

  gtk_text_buffer_get_end_iter(ctx->buffer, &end_iter);

  if (g_hash_table_size(ctx->current_tags) > 0) {
    mark = gtk_text_buffer_create_mark(ctx->buffer, NULL, &end_iter, TRUE);
  } else {
    start_iter = end_iter;
  }
  gtk_text_buffer_insert(ctx->buffer, &end_iter, text, -1);

  if (g_hash_table_size(ctx->current_tags) == 0) {
    return;
  }

  gtk_text_buffer_get_end_iter(ctx->buffer, &end_iter);
  gtk_text_buffer_get_iter_at_mark(ctx->buffer, &start_iter, mark);

  g_hash_table_iter_init(&tag_iter, ctx->current_tags);

  while (g_hash_table_iter_next(&tag_iter, &tag_key, &tag_value)) {
    GtkTextTag *tag;
    tag = gtk_text_tag_table_lookup(ctx->tag_table, tag_key);
    gtk_text_buffer_apply_tag(ctx->buffer, tag, &start_iter, &end_iter);
  }

  gtk_text_buffer_delete_mark(ctx->buffer, mark);
}

static void
add_tag(md_t *ctx, const gchar *tag)
{
  gint *num_tags = NULL;
  g_assert(ctx);
  g_assert(ctx->tag_table);
  g_assert(tag);

  num_tags = g_hash_table_lookup(ctx->current_tags, tag);

  if (!gtk_text_tag_table_lookup(ctx->tag_table, tag)) {
    g_warning("Attempting to add unknown tag: %s", tag);
    return;
  }

  if (num_tags) {
    (*num_tags)++;
    return;
  }
  num_tags = g_new(gint, 1);
  *num_tags = 1;
  g_hash_table_insert(ctx->current_tags, g_strdup(tag), num_tags);
}

static void
drop_tag(md_t *ctx, const gchar *tag)
{
  gint *num_tags = NULL;

  g_assert(ctx);
  g_assert(ctx->tag_table);

  num_tags = g_hash_table_lookup(ctx->current_tags, tag);
  if (!num_tags) {
    g_warning("Attempting to drop tag that is not active: %s", tag);
    return;
  }

  (*num_tags)--;
  if (*num_tags == 0) {
    g_hash_table_remove(ctx->current_tags, tag);
  }
}

static void
append_text_with_tag(md_t *ctx, const gchar *text, const gchar *tag)
{
  g_assert(ctx);
  g_assert(ctx->tag_table);
  g_assert(text);
  g_assert(tag);

  add_tag(ctx, tag);
  append_text(ctx, text);
  drop_tag(ctx, tag);
}

static void
append_widget(md_t *ctx, GtkWidget *widget)
{
  GtkTextChildAnchor *anchor;
  GtkTextIter iter;

  g_assert(ctx);
  g_assert(ctx->buffer);
  g_assert(widget);

  gtk_text_buffer_get_end_iter(ctx->buffer, &iter);
  anchor = gtk_text_buffer_create_child_anchor(ctx->buffer, &iter);
  gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(ctx->view), widget, anchor);
}

/*
 * Returns true if the given HTML tag is allowed (as in it has a corresponding
 * overlap in the Pango styles).
 */
static void
translate_html(const char *html, md_t *ctx)
{
  const char *start_tags[] = { "<b>", "<i>", "<strong>", "<em>", NULL };
  const char *end_tags[] = { "</b>", "</i>", "</strong>", "</em>", NULL };
  const char *translated_tags[] = {
    TAG_BOLD, TAG_ITALIC, TAG_BOLD, TAG_ITALIC, NULL,
  };

  for (int i = 0; start_tags[i] != NULL; i++) {
    if (g_strcmp0(html, start_tags[i]) == 0) {
      add_tag(ctx, translated_tags[i]);
      return;
    }
    if (g_strcmp0(html, end_tags[i]) == 0) {
      drop_tag(ctx, translated_tags[i]);
      return;
    }
  }
}

static GFile *
normalize_url(md_t *ctx, const char *url)
{
  GFile *dirname_file = NULL;
  GFile *image_file = NULL;

  if (g_str_has_prefix(url, "/")) {
    return g_file_new_for_path(url);
  }

  if (!ctx->root_path) {
    return g_file_new_for_path(url);
  }

  dirname_file = g_file_new_for_path(ctx->root_path);

  image_file = g_file_resolve_relative_path(dirname_file, url);

  g_clear_object(&dirname_file);

  return image_file;
}

static void
display_html_table(md_t *ctx, html_t *html_ctx, const char *html)
{
  GtkWidget *table;

  table = html_parse(html_ctx, html);

  if (!table) {
    return;
  }

  append_text(ctx, "\n");
  append_widget(ctx, table);
  append_text(ctx, "\n");
}

static void display_formatted_text(md_t *ctx, cmark_node *node);

static void
iterate_formatted_children(md_t *ctx, cmark_node *node, const gchar *tag)
{
  cmark_node *child = cmark_node_first_child(node);

  add_tag(ctx, tag);
  while (child) {
    display_formatted_text(ctx, child);
    child = cmark_node_next(child);
  }
  drop_tag(ctx, tag);
}

static void
display_formatted_text(md_t *ctx, cmark_node *node)
{
  switch (cmark_node_get_type(node)) {
  case CMARK_NODE_TEXT: {
    append_text(ctx, cmark_node_get_literal(node));
    break;
  }
  case CMARK_NODE_FOOTNOTE_REFERENCE:
    add_tag(ctx, TAG_SUPERSCRIPT);
    append_text(ctx, cmark_node_get_literal(node));
    drop_tag(ctx, TAG_SUPERSCRIPT);
    break;

  case CMARK_NODE_EMPH:
    iterate_formatted_children(ctx, node, TAG_ITALIC);
    break;

  case CMARK_NODE_STRONG:
    iterate_formatted_children(ctx, node, TAG_BOLD);
    break;

  case CMARK_NODE_CODE:
    iterate_formatted_children(ctx, node, TAG_CODE);
    break;

  case CMARK_NODE_LINEBREAK:
    append_text(ctx, "\n\n");
    break;
  case CMARK_NODE_SOFTBREAK:
    append_text(ctx, "\n");
    break;
  default:

    if (g_strcmp0(cmark_node_get_type_string(node), "strikethrough") == 0) {
      iterate_formatted_children(ctx, node, TAG_STRIKETHROUGH);
      break;
    }

    g_message("Unhandled node type in style lead: %s",
              cmark_node_get_type_string(node));
    break;
  }
}

static void
handle_image_change(listener_t *listener, GdkTexture *data, gpointer user_data)
{
  md_t *ctx = user_data;

  g_assert(ctx);
  g_assert(data);

  g_debug("Received image change for %s", listener_get_file_path(listener));
  for (guint i = 0; i < ctx->current_images->len; i++) {
    GtkWidget *image = g_ptr_array_index(ctx->current_images, i);
    GFile *image_path = g_object_get_data(G_OBJECT(image), OBJ_DATA_IMG_PATH);

    if (!GTK_IS_PICTURE(image)) {
      g_warning("Expected picture widget in current_images array");
      continue;
    }
    if (g_strcmp0(g_file_peek_path(image_path),
                  listener_get_file_path(listener)) != 0) {
      continue;
    }

    g_debug("Updating image widget for %s", listener_get_file_path(listener));
    gtk_picture_set_paintable(GTK_PICTURE(image), GDK_PAINTABLE(data));
  }
}

static GtkTextTag *
get_quote_tag(md_t *ctx)
{
  return gtk_text_buffer_create_tag(ctx->buffer,
                                    NULL,
                                    "accumulative-margin",
                                    TRUE,
                                    "left-margin",
                                    50,
                                    "style",
                                    PANGO_STYLE_ITALIC,
                                    NULL);
}

static gboolean
link_button_clicked(GtkLinkButton *button, gpointer user_data)
{
  GFile *file;
  md_t *ctx = user_data;
  const char *uri;

  g_assert(ctx);
  g_assert(button);

  uri = gtk_link_button_get_uri(button);
  g_debug("Link button clicked: %s", uri);

  if (g_str_has_prefix(uri, "#")) {
    gchar *unescaped_uri = g_uri_unescape_string(uri + 1, NULL);
    toc_scroll_to_heading(ctx->toc, unescaped_uri);
    return TRUE;
  }

  if (g_str_has_prefix(uri, "http")) {
    return FALSE;
  }

  if (!g_str_has_suffix(uri, ".md")) {
    return FALSE;
  }

  file = normalize_url(ctx, uri);

  if (ctx->display) {
    ctx->display(g_file_peek_path(file), ctx->display_user_data);
  }

  g_clear_object(&file);

  return TRUE;
}

static void
iterate_paragraph(md_t *ctx, cmark_node *node)
{
  cmark_node *child = cmark_node_first_child(node);
  while (child) {
    switch (cmark_node_get_type(child)) {
    case CMARK_NODE_IMAGE: {
      listener_t *img_listener;
      GtkWidget *image;
      GFile *url = normalize_url(ctx, cmark_node_get_url(child));

      img_listener =
              g_hash_table_lookup(ctx->image_listeners, g_file_peek_path(url));

      if (!img_listener) {
        img_listener = listener_new(url);
        g_hash_table_insert(ctx->image_listeners,
                            g_strdup(g_file_peek_path(url)),
                            img_listener);
      }

      image = gtk_picture_new();

      gtk_picture_set_can_shrink(GTK_PICTURE(image), FALSE);
      gtk_widget_set_halign(image, GTK_ALIGN_START);

      g_object_set_data_full(G_OBJECT(image),
                             OBJ_DATA_IMG_PATH,
                             g_object_ref(url),
                             (GDestroyNotify) g_object_unref);
      g_ptr_array_add(ctx->current_images, g_object_ref_sink(image));
      listener_set_img_cb(img_listener, handle_image_change, ctx);

      append_text(ctx, "\n");
      append_widget(ctx, image);
      append_text(ctx, "\n");

      g_clear_object(&url);
      break;
    }

    case CMARK_NODE_CODE:
      append_text_with_tag(ctx, cmark_node_get_literal(child), TAG_CODE);
      break;

    case CMARK_NODE_TEXT:
    case CMARK_NODE_EMPH:
    case CMARK_NODE_STRONG:
    case CMARK_NODE_LINEBREAK:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_FOOTNOTE_REFERENCE:
      display_formatted_text(ctx, child);
      break;

    case CMARK_NODE_INLINE_HTML: {
      translate_html(cmark_node_get_literal(child), ctx);
      break;
    }

    case CMARK_NODE_LINK: {
      GtkWidget *link_button;
      cmark_node *link_child = cmark_node_first_child(child);
      link_button = gtk_link_button_new_with_label(cmark_node_get_url(child),
                                                   cmark_node_get_literal(
                                                           link_child));

      gtk_widget_add_css_class(link_button, "in-text-button");
      gtk_widget_set_halign(link_button, GTK_ALIGN_START);

      g_signal_connect(link_button,
                       "activate-link",
                       G_CALLBACK(link_button_clicked),
                       ctx);

      append_widget(ctx, link_button);
      break;
    }

    case CMARK_NODE_LIST: {
      display_list(ctx, child, 1);
      break;
    }

    default:

      if (g_strcmp0(cmark_node_get_type_string(child), "strikethrough") == 0) {
        display_formatted_text(ctx, child);
        break;
      }
      g_message("Unhandled node type in paragraph: %s",
                cmark_node_get_type_string(child));
      break;
    }
    child = cmark_node_next(child);
  }
}

static void
display_paragraph(md_t *ctx, cmark_node *node)
{
  append_text(ctx, "\n\n");
  iterate_paragraph(ctx, node);
}

static void
iterate_table_cells(table_t *table, cmark_node *row)
{
  cmark_node *child = cmark_node_first_child(row);

  while (child) {
    cmark_node *text = cmark_node_first_child(child);

    table_add_cell(table, cmark_node_get_literal(text));
    child = cmark_node_next(child);
  }
}

static void
display_table(md_t *ctx, cmark_node *node)
{
  cmark_node *child = cmark_node_first_child(node);
  table_t *table_ctx;

  table_ctx = table_new(cmark_gfm_extensions_get_table_alignments(node),
                        cmark_gfm_extensions_get_table_columns(node));

  while (child) {
    const gchar *name = cmark_node_get_type_string(child);
    if (g_strcmp0(name, "table_header") == 0) {
      table_new_row(table_ctx, TRUE);
      iterate_table_cells(table_ctx, child);
    } else if (g_strcmp0(name, "table_row") == 0) {
      table_new_row(table_ctx, FALSE);
      iterate_table_cells(table_ctx, child);
    } else {
      g_warning("Unknown table child: %s", name);
    }
    child = cmark_node_next(child);
  }

  append_text(ctx, "\n");
  append_widget(ctx, table_finalize(table_ctx));
}

static void
display_list(md_t *ctx, cmark_node *list_node, guint indent)
{
  GtkTextTag *indent_tag;
  GtkTextMark *start_mark;
  GtkTextIter start_iter;
  GtkTextIter indent_iter;
  cmark_node *child = cmark_node_first_child(list_node);

  cmark_list_type list_type = cmark_node_get_list_type(list_node);
  gboolean is_ordered = list_type == CMARK_BULLET_LIST ? FALSE : TRUE;
  guint64 num = cmark_node_get_list_start(list_node);

  append_text(ctx, "\n");

  indent_tag = gtk_text_buffer_create_tag(ctx->buffer,
                                          NULL,
                                          "indent",
                                          indent * 20,
                                          NULL);

  gtk_text_buffer_get_end_iter(ctx->buffer, &indent_iter);
  start_mark =
          gtk_text_buffer_create_mark(ctx->buffer, NULL, &indent_iter, TRUE);
  while (child) {
    if (cmark_node_get_type(child) == CMARK_NODE_ITEM) {
      gchar *item_prefix = NULL;

      if (is_ordered) {
        item_prefix = g_strdup_printf("%" G_GUINT64_FORMAT ".\t", num);
      } else {
        item_prefix = g_strdup("•\t");
      }

      if (g_strcmp0(cmark_node_get_type_string(child), "tasklist") == 0) {
        gboolean item_checked = FALSE;
        gchar *tmp_prefix = NULL;
        item_checked = cmark_gfm_extensions_get_tasklist_item_checked(child);

        if (item_checked) {
          tmp_prefix = g_strdup_printf("%s✅\t", item_prefix);
        } else {
          tmp_prefix = g_strdup_printf("%s☐\t", item_prefix);
        }

        g_free(item_prefix);
        item_prefix = tmp_prefix;
      }

      append_text(ctx, item_prefix);
      g_free(item_prefix);

      cmark_node *item_child = cmark_node_first_child(child);
      while (item_child) {
        switch (cmark_node_get_type(item_child)) {
        case CMARK_NODE_PARAGRAPH: {
          g_debug("Displaying list item %" G_GUINT64_FORMAT, num);
          iterate_paragraph(ctx, item_child);
          break;
        }
        case CMARK_NODE_LIST: {
          g_debug("Displaying sublist in list item %" G_GUINT64_FORMAT, num);
          display_list(ctx, item_child, indent + 1);
          break;
        }
        default:
          g_message("Unknown list item child: %s",
                    cmark_node_get_type_string(item_child));
        }
        item_child = cmark_node_next(item_child);
      }
      append_text(ctx, "\n");
      num++;
    } else {
      g_message("Unknown list child: %s", cmark_node_get_type_string(child));
    }
    child = cmark_node_next(child);
  }

  gtk_text_buffer_get_iter_at_mark(ctx->buffer, &start_iter, start_mark);
  gtk_text_buffer_get_end_iter(ctx->buffer, &indent_iter);
  gtk_text_buffer_apply_tag(ctx->buffer, indent_tag, &start_iter, &indent_iter);

  gtk_text_buffer_delete_mark(ctx->buffer, start_mark);
}

static void
display_markdown(md_t *ctx, cmark_node *node)
{
  cmark_node *child = cmark_node_first_child(node);

  switch (cmark_node_get_type(node)) {
  case CMARK_NODE_TEXT: {
    if (toc_is_empty(ctx->toc)) {
      break;
    }
    if (g_strcmp0(cmark_node_get_literal(node), ":toc:") == 0) {
      //      gtk_box_append(GTK_BOX(box), toc_get(ctx->toc));
      break;
    }

    append_text(ctx, cmark_node_get_literal(node));

    break;
  }
  case CMARK_NODE_HTML_BLOCK: {
    if (toc_is_empty(ctx->toc)) {
      break;
    }
    display_html_table(ctx, ctx->html, cmark_node_get_literal(node));
    break;
  }

  case CMARK_NODE_CODE_BLOCK: {
    append_text(ctx, "\n");
    append_text_with_tag(ctx, cmark_node_get_literal(node), "code");
    append_text(ctx, "\n");
    break;
  }

  case CMARK_NODE_HEADING: {
    cmark_node *heading_child = cmark_node_first_child(node);
    gchar heading_tag_name[3];

    append_text(ctx, "\n");

    /* This also inserts a mark for the heading */
    toc_add_heading(ctx->toc,
                    cmark_node_get_literal(heading_child),
                    cmark_node_get_heading_level(node));

    g_snprintf(heading_tag_name,
               sizeof(heading_tag_name),
               "h%d",
               cmark_node_get_heading_level(node));

    append_text_with_tag(ctx,
                         cmark_node_get_literal(heading_child),
                         heading_tag_name);
    break;
  }
  case CMARK_NODE_PARAGRAPH: {
    if (toc_is_empty(ctx->toc)) {
      break;
    }
    display_paragraph(ctx, node);
    break;
  }

  case CMARK_NODE_LIST: {
    display_list(ctx, node, 1);
    break;
  }

  case CMARK_NODE_THEMATIC_BREAK: {
    GtkTextIter iter;

    gtk_text_buffer_get_end_iter(ctx->buffer, &iter);
    gtk_text_buffer_insert_with_tags_by_name(ctx->buffer,
                                             &iter,
                                             RULER,
                                             -1,
                                             TAG_HORIZONTAL,
                                             NULL);
    g_message("Displayed thematic break");
    break;
  }

  case CMARK_NODE_BLOCK_QUOTE: {
    cmark_node *quote_child = cmark_node_first_child(node);
    GtkTextTag *quote_tag;
    GtkTextMark *start_mark;
    GtkTextIter start_iter;
    GtkTextIter end_iter;

    quote_tag = get_quote_tag(ctx);

    gtk_text_buffer_get_end_iter(ctx->buffer, &start_iter);
    start_mark =
            gtk_text_buffer_create_mark(ctx->buffer, NULL, &start_iter, TRUE);

    while (quote_child) {
      display_markdown(ctx, quote_child);
      quote_child = cmark_node_next(quote_child);
    }

    gtk_text_buffer_get_end_iter(ctx->buffer, &end_iter);
    gtk_text_buffer_get_iter_at_mark(ctx->buffer, &start_iter, start_mark);
    gtk_text_buffer_apply_tag(ctx->buffer, quote_tag, &start_iter, &end_iter);
    gtk_text_buffer_delete_mark(ctx->buffer, start_mark);

    break;
  }

  case CMARK_NODE_FOOTNOTE_DEFINITION: {
    cmark_node *footnote_child = cmark_node_first_child(node);
    gchar *footnote_label_text =
            g_strdup_printf("\n\n%d.\t", ctx->footnote_num + 1);

    append_text(ctx, footnote_label_text);
    while (footnote_child) {
      iterate_paragraph(ctx, footnote_child);

      footnote_child = cmark_node_next(footnote_child);
    }

    ctx->footnote_num++;

    break;
  }

  case CMARK_NODE_DOCUMENT:
    while (child) {
      display_markdown(ctx, child);
      child = cmark_node_next(child);
    }
    break;

  default:
    if (g_strcmp0(cmark_node_get_type_string(node), "table") == 0) {
      display_table(ctx, node);
      break;
    }
    break;
  }
}

static void
parse_markdown(md_t *ctx, const char *markdown)
{
  const gchar *extensions[] = { "table", "strikethrough", "tasklist", NULL };
  cmark_parser *parser = cmark_parser_new(CMARK_OPT_FOOTNOTES);
  cmark_gfm_core_extensions_ensure_registered();
  cmark_syntax_extension *syntax_extension;

  for (int i = 0; extensions[i] != NULL; i++) {
    syntax_extension = cmark_find_syntax_extension(extensions[i]);
    if (!syntax_extension) {
      g_warning("Unknown extension %s", extensions[i]);
      continue;
    }
    cmark_parser_attach_syntax_extension(parser, syntax_extension);
  }

  cmark_parser_feed(parser, markdown, strlen(markdown));

  cmark_node *doc = cmark_parser_finish(parser);

#ifdef PRINT_DEBUG
  g_print("Parsed markdown AST:\n");
  print_node(doc, 0);
#endif

  display_markdown(ctx, doc);
  cmark_node_free(doc);
}

static void
clear_md(md_t *ctx)
{
  g_clear_pointer(&ctx->current_tags, g_hash_table_unref);
  g_clear_pointer(&ctx->current_images, g_ptr_array_unref);
  g_clear_pointer(&ctx->root_path, g_free);
  g_clear_pointer(&ctx->toc, toc_free);
  g_clear_pointer(&ctx->html, html_free);
}

static void
set_sidebar_title(md_t *ctx, listener_t *listener)
{
  gchar *title;
  gchar *basename;
  GtkWidget *title_label;

  basename = g_path_get_basename(listener_get_file_path(listener));

  title = g_strdup_printf("<b>%s:</b>", basename);

  title_label = label_new(title);

  gtk_widget_set_margin_start(title_label, 20);

  gtk_box_insert_child_after(GTK_BOX(ctx->toc_box), title_label, NULL);
}

static GtkTextTagTable *
setup_tag_table(void)
{
  GtkTextTagTable *tag_table;

  tag_table = gtk_text_tag_table_new();

  GtkTextTag *bold_tag = gtk_text_tag_new(TAG_BOLD);
  g_object_set(bold_tag, "weight", 800, NULL);
  gtk_text_tag_table_add(tag_table, bold_tag);

  GtkTextTag *italic_tag = gtk_text_tag_new(TAG_ITALIC);
  gtk_text_tag_table_add(tag_table, italic_tag);
  g_object_set(italic_tag, "style", PANGO_STYLE_ITALIC, NULL);

  GtkTextTag *code_tag = gtk_text_tag_new(TAG_CODE);
  g_object_set(code_tag,
               "family",
               "monospace",
               "paragraph-background",
               "lightgrey",
               "indent",
               20,
               NULL);
  gtk_text_tag_table_add(tag_table, code_tag);

  GtkTextTag *strikethrough_tag = gtk_text_tag_new(TAG_STRIKETHROUGH);
  g_object_set(strikethrough_tag, "strikethrough", TRUE, NULL);
  gtk_text_tag_table_add(tag_table, strikethrough_tag);

  GtkTextTag *superscript_tag = gtk_text_tag_new(TAG_SUPERSCRIPT);
  g_object_set(superscript_tag, "rise", 5 * PANGO_SCALE, "scale", 0.5, NULL);

  gtk_text_tag_table_add(tag_table, superscript_tag);

  GtkTextTag *h1_tag = gtk_text_tag_new(TAG_H1);
  g_object_set(h1_tag,
               "scale",
               2.5,
               "weight",
               800,
               "pixels-above-lines",
               20,
               NULL);
  gtk_text_tag_table_add(tag_table, h1_tag);

  GtkTextTag *h2_tag = gtk_text_tag_new(TAG_H2);
  g_object_set(h2_tag,
               "scale",
               2.1,
               "weight",
               800,
               "pixels-above-lines",
               20,
               NULL);
  gtk_text_tag_table_add(tag_table, h2_tag);

  GtkTextTag *h3_tag = gtk_text_tag_new(TAG_H3);
  g_object_set(h3_tag,
               "scale",
               1.8,
               "weight",
               800,
               "pixels-above-lines",
               20,
               NULL);
  gtk_text_tag_table_add(tag_table, h3_tag);

  GtkTextTag *h4_tag = gtk_text_tag_new(TAG_H4);
  g_object_set(h4_tag,
               "scale",
               1.5,
               "weight",
               800,
               "pixels-above-lines",
               20,
               NULL);
  gtk_text_tag_table_add(tag_table, h4_tag);

  GtkTextTag *h5_tag = gtk_text_tag_new(TAG_H5);
  g_object_set(h5_tag,
               "scale",
               1.2,
               "weight",
               800,
               "pixels-above-lines",
               20,
               NULL);
  gtk_text_tag_table_add(tag_table, h5_tag);

  GtkTextTag *h6_tag = gtk_text_tag_new(TAG_H6);
  g_object_set(h6_tag,
               "scale",
               1.0,
               "weight",
               800,
               "pixels-above-lines",
               20,
               NULL);
  gtk_text_tag_table_add(tag_table, h6_tag);

  GtkTextTag *horizontal_tag = gtk_text_tag_new(TAG_HORIZONTAL);
  g_object_set(horizontal_tag,
               "foreground",
               "darkgrey",
               "justification",
               GTK_JUSTIFY_FILL,
               "strikethrough",

               TRUE,
               "scale",
               1.5,
               NULL);
  gtk_text_tag_table_add(tag_table, horizontal_tag);

  return tag_table;
}

static void
show_content(gpointer user_data)
{
  md_t *ctx = user_data;

  ctx->display(listener_get_file_path(ctx->listener), ctx->display_user_data);
}

static void
handle_markdown(listener_t *listener, const gchar *markdown, gpointer user_data)
{
  md_t *ctx = user_data;

  g_message("Parsing markdown file: %s", listener_get_file_path(listener));

  clear_md(ctx);

  ctx->root_path = g_path_get_dirname(listener_get_file_path(listener));

  if (ctx->toc_box) {
    GtkWidget *old_box;
    GtkWidget *new_box;
    g_message("Clearing old toc");

    old_box = ctx->toc_box;
    new_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_insert_child_after(GTK_BOX(ctx->toc_parent), new_box, old_box);
    gtk_box_remove(GTK_BOX(ctx->toc_parent), old_box);
    ctx->toc_box = new_box;
    g_message("New toc box: %p", ctx->toc_box);
  } else {
    ctx->toc_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(ctx->toc_parent), ctx->toc_box);
  }

  gtk_widget_set_halign(ctx->toc_box, GTK_ALIGN_START);
  gtk_widget_set_margin_top(ctx->toc_box, 20);

  ctx->toc = toc_new(ctx->toc_box, GTK_TEXT_VIEW(ctx->view), show_content, ctx);
  ctx->html = html_new();
  ctx->current_images = g_ptr_array_new_with_free_func(g_object_unref);
  ctx->current_tags =
          g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

  ctx->tag_table = setup_tag_table();
  ctx->buffer = gtk_text_buffer_new(ctx->tag_table);

  gtk_text_view_set_buffer(GTK_TEXT_VIEW(ctx->view), ctx->buffer);

  gtk_text_buffer_set_enable_undo(ctx->buffer, FALSE);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(ctx->view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(ctx->view), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ctx->view), GTK_WRAP_WORD_CHAR);

  gtk_widget_set_hexpand(ctx->view, TRUE);

  set_sidebar_title(ctx, listener);
  parse_markdown(ctx, markdown);
}

md_t *
md_new(listener_t *listener,
       GtkBox *toc,
       display_md display,
       gpointer user_data)
{
  GtkWidget *scroll;
  md_t *md = g_new0(md_t, 1);

  md->toc_parent = GTK_WIDGET(g_object_ref(toc));

  md->listener = listener;

  md->image_listeners = g_hash_table_new_full(g_str_hash,
                                              g_str_equal,
                                              g_free,
                                              (GDestroyNotify) listener_free);
  md->root_path = g_path_get_dirname(listener_get_file_path(listener));

  md->view = gtk_text_view_new();
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(md->view), 20);

  scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 300);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), -1);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_widget_set_hexpand(scroll, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), md->view);
  md->scroll = g_object_ref_sink(scroll);

  md->display = display;
  md->display_user_data = user_data;

  listener_set_md_cb(listener, handle_markdown, md);

  return md;
}

void
md_free(md_t *md)
{
  if (!md) {
    return;
  }

  clear_md(md);
  g_clear_pointer(&md->image_listeners, g_hash_table_unref);
  g_free(md);

  return;
}

GtkWidget *
md_get_view(md_t *md)
{
  g_return_val_if_fail(md != NULL, NULL);

  return md->scroll;
}
