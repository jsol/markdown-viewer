#include <glib.h>
#include "md.h"
#include "gtk/gtk.h"
#include "html.h"
#include "listener.h"
#include "toc.h"

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

#define OBJ_DATA_IMG "image"

struct md {
  GtkWidget *content_parent;
  GtkWidget *toc_parent;
  GtkWidget *box;
  GtkWidget *toc_box;
  gchar *root_path;
  listener_t *listener;
  GPtrArray *image_monitors;
  toc_t *toc;
  html_t *html;
};

static void display_list(md_t *ctx, cmark_node *list_node, GtkWidget *box);

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

/*
 * Returns true if the given HTML tag is allowed (as in it has a corresponding
 * overlap in the Pango styles).
 */
static gboolean
allowed_html(const char *html)
{
  const char *allowed_tags[] = { "<b>",  "</b>",  "<i>",      "</i>",
                                 "<u>",  "</u>",  "<strong>", "</strong>",
                                 "<em>", "</em>", NULL };

  for (int i = 0; allowed_tags[i] != NULL; i++) {
    if (g_strcmp0(html, allowed_tags[i]) == 0) {
      return TRUE;
    }
  }
  return FALSE;
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
display_html_table(html_t *ctx, const char *html, GtkWidget *box)
{
  GtkWidget *frame = NULL;
  GtkWidget *table;

  table = html_parse(ctx, html);

  if (!table) {
    return;
  }

  frame = gtk_frame_new(NULL);
  gtk_frame_set_child(GTK_FRAME(frame), table);
  gtk_box_append(GTK_BOX(box), frame);
}

static GtkWidget *
display_image(GFile *file, GtkWidget *old_image, GtkWidget *box)
{
  GtkWidget *image = gtk_picture_new_for_file(file);
  gtk_picture_set_can_shrink(GTK_PICTURE(image), FALSE);
  gtk_widget_set_halign(image, GTK_ALIGN_START);

  if (old_image == NULL) {
    gtk_box_append(GTK_BOX(box), g_object_ref(image));
    return image;
  }

  gtk_box_insert_child_after(GTK_BOX(box), g_object_ref(image), old_image);
  gtk_box_remove(GTK_BOX(box), old_image);

  return image;
}

static void
monitor_image_changes(GFileMonitor *monitor,
                      GFile *file,
                      G_GNUC_UNUSED GFile *other_file,
                      GFileMonitorEvent event_type,
                      G_GNUC_UNUSED gpointer user_data)
{
  if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
      event_type == G_FILE_MONITOR_EVENT_CREATED) {
    g_message("Image changed: %s", g_file_peek_path(file));
    GtkWidget *old_image = g_object_get_data(G_OBJECT(monitor), OBJ_DATA_IMG);

    GtkWidget *new_image =
            display_image(file, old_image, gtk_widget_get_parent(old_image));
    g_object_set_data_full(G_OBJECT(monitor),
                           OBJ_DATA_IMG,
                           g_object_ref_sink(new_image),
                           g_object_unref);
  }
}

static void
display_formatted_text(md_t *ctx, GString *text, cmark_node *node)
{
  while (node) {
    switch (cmark_node_get_type(node)) {
    case CMARK_NODE_TEXT: {
      gchar *escaped_text;

      escaped_text = g_markup_escape_text(cmark_node_get_literal(node), -1);
      g_string_append(text, escaped_text);
      g_free(escaped_text);
      break;
    }
    case CMARK_NODE_EMPH: {
      cmark_node *child = cmark_node_first_child(node);

      g_string_append(text, "<i>");
      display_formatted_text(ctx, text, child);
      g_string_append(text, "</i>");
      break;
    }

    case CMARK_NODE_STRONG: {
      cmark_node *child = cmark_node_first_child(node);

      g_string_append(text, "<b>");
      display_formatted_text(ctx, text, child);
      g_string_append(text, "</b>");
      break;
    }

    case CMARK_NODE_LINEBREAK:
      g_string_append(text, "\n\n");
      break;
    case CMARK_NODE_SOFTBREAK:
      g_string_append(text, "\n");
      break;
    default:

      if (g_strcmp0(cmark_node_get_type_string(node), "strikethrough") == 0) {
        cmark_node *child = cmark_node_first_child(node);
        g_string_append(text, "<s>");
        display_formatted_text(ctx, text, child);
        g_string_append(text, "</s>");
        break;
      }

      g_message("Unhandled node type in style lead: %s",
                cmark_node_get_type_string(node));
      break;
    }

    node = cmark_node_next(node);
  }
}

static void
display_paragraph(md_t *ctx, cmark_node *node, GtkWidget *box)
{
  cmark_node *child = cmark_node_first_child(node);
  GString *paragraph_text = g_string_new("");

  while (child) {
    switch (cmark_node_get_type(child)) {
    case CMARK_NODE_IMAGE: {
      GFileMonitor *monitor = NULL;

      GFile *url = normalize_url(ctx, cmark_node_get_url(child));

      GtkWidget *image = display_image(url, NULL, box);

      monitor = g_file_monitor_file(url, G_FILE_MONITOR_NONE, NULL, NULL);

      g_object_set_data_full(G_OBJECT(monitor),
                             OBJ_DATA_IMG,
                             g_object_ref_sink(image),
                             g_object_unref);
      g_ptr_array_add(ctx->image_monitors, monitor);
      g_signal_connect(monitor,
                       "changed",
                       G_CALLBACK(monitor_image_changes),
                       NULL);
      g_free(url);

      break;
    }

    case CMARK_NODE_CODE: {
      gchar *escaped_text =
              g_markup_escape_text(cmark_node_get_literal(child), -1);
      g_string_append(paragraph_text, "<tt>");
      g_string_append(paragraph_text, escaped_text);
      g_string_append(paragraph_text, "</tt>");
      g_free(escaped_text);
      break;
    }

    case CMARK_NODE_TEXT: {
      gchar *escaped_text =
              g_markup_escape_text(cmark_node_get_literal(child), -1);
      g_string_append(paragraph_text, escaped_text);
      g_free(escaped_text);
      break;
    }

    case CMARK_NODE_EMPH: {
      g_string_append(paragraph_text, "<i>");
      display_formatted_text(ctx, paragraph_text, child);
      g_string_append(paragraph_text, "</i>");
      break;
    }

    case CMARK_NODE_STRONG: {
      cmark_node *emph_child = cmark_node_first_child(child);
      g_string_append(paragraph_text, "<b>");

      display_formatted_text(ctx, paragraph_text, emph_child);
      g_string_append(paragraph_text, "</b>");
      break;
    }

    case CMARK_NODE_LINEBREAK:
      g_string_append(paragraph_text, "\n\n");
      break;

    case CMARK_NODE_SOFTBREAK:
      g_string_append(paragraph_text, "\n");
      break;

    case CMARK_NODE_INLINE_HTML: {
      if (allowed_html(cmark_node_get_literal(child))) {
        g_string_append(paragraph_text, cmark_node_get_literal(child));
      }
      break;
    }

    case CMARK_NODE_LINK: {
      if (paragraph_text->len > 0) {
        GtkWidget *label = gtk_label_new(paragraph_text->str);
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(box), label);

        g_string_set_size(paragraph_text, 0);
      }
      GtkWidget *link_button;
      cmark_node *link_child = cmark_node_first_child(child);
      link_button = gtk_link_button_new_with_label(cmark_node_get_url(child),
                                                   cmark_node_get_literal(
                                                           link_child));

      gtk_widget_set_halign(link_button, GTK_ALIGN_START);
      gtk_box_append(GTK_BOX(box), link_button);
      break;
    }

    case CMARK_NODE_LIST: {
      GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

      g_message("Encountered list in paragraph, creating new box for it");
      if (paragraph_text->len > 0) {
        GtkWidget *label = gtk_label_new(paragraph_text->str);
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(box), label);

        g_string_set_size(paragraph_text, 0);
      }

      display_list(ctx, child, list_box);
      gtk_widget_set_margin_start(list_box, 20);
      gtk_box_append(GTK_BOX(box), list_box);
      break;
    }

    default:

      if (g_strcmp0(cmark_node_get_type_string(child), "strikethrough") == 0) {
        g_string_append(paragraph_text, "<s>");
        display_formatted_text(ctx, paragraph_text, child);
        g_string_append(paragraph_text, "</s>");
        break;
      }
      g_message("Unhandled node type in paragraph: %s",
                cmark_node_get_type_string(child));
      break;
    }
    child = cmark_node_next(child);
  }

  if (paragraph_text->len > 0) {
    GtkWidget *label = gtk_label_new(paragraph_text->str);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);
  }

  g_string_free(paragraph_text, TRUE);
}

static void
display_table_row(cmark_node *node,
                  guint8 *align,
                  guint num_col,
                  gint row,
                  gboolean header,
                  GtkWidget *table)
{
  cmark_node *child = cmark_node_first_child(node);
  guint col = 0;

  while (child) {
    cmark_node *header_cell_child = cmark_node_first_child(child);
    GtkWidget *label = gtk_label_new(cmark_node_get_literal(header_cell_child));
    GtkWidget *frame = gtk_frame_new(NULL);

    gtk_frame_set_child(GTK_FRAME(frame), label);

    gtk_widget_set_hexpand(label, TRUE);

    if (col >= num_col) {
      g_message("More header cells than columns in table");
      break;
    }

    switch (align[col]) {
    case 0:
    case 'l':
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      break;
    case 'c':
      gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
      break;
    case 'r':
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      break;
    }

    if (header) {
      gtk_widget_add_css_class(frame, "table-header");
    }

    gtk_grid_attach(GTK_GRID(table), frame, col, row, 1, 1);
    child = cmark_node_next(child);
    col++;
  }
}

static void
display_table(cmark_node *node, GtkWidget *box)
{
  cmark_node *child = cmark_node_first_child(node);

  GtkWidget *table = gtk_grid_new();
  guint8 *alignments = cmark_gfm_extensions_get_table_alignments(node);
  guint num_cols = cmark_gfm_extensions_get_table_columns(node);
  gint row_count = 0;

  while (child) {
    const gchar *name = cmark_node_get_type_string(child);
    if (g_strcmp0(name, "table_header") == 0) {
      display_table_row(child, alignments, num_cols, row_count, TRUE, table);
    } else if (g_strcmp0(name, "table_row") == 0) {
      display_table_row(child, alignments, num_cols, row_count, FALSE, table);
    } else {
      g_message("Unknown table child: %s", name);
    }
    row_count++;
    child = cmark_node_next(child);
  }

  gtk_box_append(GTK_BOX(box), table);
}

static void
display_list(md_t *ctx, cmark_node *list_node, GtkWidget *box)
{
  cmark_node *child = cmark_node_first_child(list_node);
  GtkGrid *grid = NULL;

  cmark_list_type list_type = cmark_node_get_list_type(list_node);
  gboolean is_ordered = list_type == CMARK_BULLET_LIST ? FALSE : TRUE;
  guint64 num = cmark_node_get_list_start(list_node);

  grid = GTK_GRID(gtk_grid_new());

  gtk_grid_set_row_spacing(grid, 5);
  gtk_grid_set_column_spacing(grid, 5);
  gtk_grid_set_column_homogeneous(grid, FALSE);

  gtk_box_append(GTK_BOX(box), GTK_WIDGET(grid));

  while (child) {
    if (cmark_node_get_type(child) == CMARK_NODE_ITEM) {
      gchar *item_prefix = NULL;
      GtkWidget *item_prefix_label;

      item_prefix =
              is_ordered ? g_strdup_printf("%" G_GUINT64_FORMAT ".", num) : "•";

      if (g_strcmp0(cmark_node_get_type_string(child), "tasklist") == 0) {
        gboolean item_checked = FALSE;
        item_checked = cmark_gfm_extensions_get_tasklist_item_checked(child);

        if (item_checked) {
          item_prefix = g_strdup_printf("%s ✅", item_prefix);
        } else {
          item_prefix = g_strdup_printf("%s ☐", item_prefix);
        }
      }

      item_prefix_label = gtk_label_new(item_prefix);
      gtk_widget_set_halign(item_prefix_label, GTK_ALIGN_END);
      gtk_widget_set_valign(item_prefix_label, GTK_ALIGN_START);
      gtk_grid_attach(grid, item_prefix_label, 0, num, 1, 1);

      cmark_node *item_child = cmark_node_first_child(child);
      GtkWidget *item_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
      while (item_child) {
        switch (cmark_node_get_type(item_child)) {
        case CMARK_NODE_PARAGRAPH: {
          g_message("Displaying list item %" G_GUINT64_FORMAT, num);
          display_paragraph(ctx, item_child, item_box);
          break;
        }
        case CMARK_NODE_LIST: {
          GtkWidget *sublist_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
          g_message("Displaying sublist in list item %" G_GUINT64_FORMAT, num);
          display_list(ctx, item_child, sublist_box);
          gtk_box_append(GTK_BOX(item_box), sublist_box);
          break;
        }
        default:
          g_message("Unknown list item child: %s",
                    cmark_node_get_type_string(item_child));
        }
        item_child = cmark_node_next(item_child);
      }
      gtk_grid_attach(grid, item_box, 1, num, 1, 1);
      num++;
    } else {
      g_message("Unknown list child: %s", cmark_node_get_type_string(child));
    }
    child = cmark_node_next(child);
  }
}

static void
display_markdown(md_t *ctx, cmark_node *node, GtkWidget *box)
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
    GtkWidget *label = gtk_label_new(cmark_node_get_literal(node));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);
    break;
  }
  case CMARK_NODE_HTML_BLOCK: {
    if (toc_is_empty(ctx->toc)) {
      break;
    }
    display_html_table(ctx->html, cmark_node_get_literal(node), box);
    break;
  }

  case CMARK_NODE_CODE_BLOCK: {
    GtkWidget *label = gtk_label_new(cmark_node_get_literal(node));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_add_css_class(label, "monospace");
    gtk_box_append(GTK_BOX(box), label);
    break;
  }

  case CMARK_NODE_HEADING: {
    GtkWidget *label;
    cmark_node *heading_child = cmark_node_first_child(node);

    label = toc_add_heading(ctx->toc,
                            cmark_node_get_literal(heading_child),
                            cmark_node_get_heading_level(node));

    gtk_widget_set_margin_top(label, 10);
    gtk_widget_set_margin_bottom(label, 10);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);

    break;
  }
  case CMARK_NODE_PARAGRAPH: {
    if (toc_is_empty(ctx->toc)) {
      break;
    }
    GtkWidget *paragraph_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(box), paragraph_box);
    display_paragraph(ctx, node, paragraph_box);
    break;
  }

  case CMARK_NODE_LIST: {
    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(box), list_box);

    display_list(ctx, node, list_box);
    break;
  }

  case CMARK_NODE_THEMATIC_BREAK: {
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), separator);
    break;
  }

  case CMARK_NODE_DOCUMENT:
    while (child) {
      display_markdown(ctx, child, box);
      child = cmark_node_next(child);
    }
    break;

  default:

    if (g_strcmp0(cmark_node_get_type_string(node), "table") == 0) {
      display_table(node, box);
      break;
    }
    break;
  }
}

static void
parse_markdown(md_t *ctx, const char *markdown)
{
  const gchar *extensions[] = { "table", "strikethrough", "tasklist", NULL };
  cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);
  cmark_gfm_core_extensions_ensure_registered();
  cmark_syntax_extension *syntax_extension;

  for (int i = 0; extensions[i] != NULL; i++) {
    syntax_extension = cmark_find_syntax_extension(extensions[i]);
    if (!syntax_extension) {
      g_warning("Unknown extension %s\n", extensions[i]);
      continue;
    }
    cmark_parser_attach_syntax_extension(parser, syntax_extension);
  }

  cmark_parser_feed(parser, markdown, strlen(markdown));

  cmark_node *doc = cmark_parser_finish(parser);

  print_node(doc, 0);
  display_markdown(ctx, doc, ctx->box);
  cmark_node_free(doc);
}

static void
clear_md(md_t *ctx)
{
  g_clear_pointer(&ctx->root_path, g_free);
  g_clear_pointer(&ctx->toc, toc_free);
  g_clear_pointer(&ctx->html, html_free);
  g_clear_pointer(&ctx->image_monitors, g_ptr_array_unref);
}

static void
set_sidebar_title(md_t *ctx, listener_t *listener)
{
  gchar *title;
  gchar *basename;
  GtkWidget *title_label;

  basename = g_path_get_basename(listener_get_file_path(listener));

  title = g_strdup_printf("<b>%s:</b>", basename);

  title_label = gtk_label_new(title);

  gtk_label_set_use_markup(GTK_LABEL(title_label), TRUE);
  gtk_widget_set_halign(title_label, GTK_ALIGN_START);
  gtk_widget_set_margin_start(title_label, 20);

  gtk_box_insert_child_after(GTK_BOX(ctx->toc_box), title_label, NULL);
}

static void
handle_markdown(listener_t *listener, const gchar *markdown, gpointer user_data)
{
  md_t *ctx = user_data;

  g_print("Parsing markdown file: %s\n", listener_get_file_path(listener));

  clear_md(ctx);

  ctx->root_path = g_path_get_dirname(listener_get_file_path(listener));

  if (ctx->box) {
    GtkWidget *old_box;
    GtkWidget *new_box;

    old_box = ctx->box;
    new_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_insert_child_after(GTK_BOX(ctx->content_parent), new_box, old_box);
    gtk_box_remove(GTK_BOX(ctx->content_parent), old_box);
    ctx->box = new_box;
  } else {
    ctx->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(ctx->content_parent), ctx->box);
  }

  if (ctx->toc_box) {
    GtkWidget *old_box;
    GtkWidget *new_box;

    old_box = ctx->toc_box;
    new_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_insert_child_after(GTK_BOX(ctx->toc_parent), new_box, old_box);
    gtk_box_remove(GTK_BOX(ctx->toc_parent), old_box);
    ctx->toc_box = new_box;
  } else {
    ctx->toc_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(ctx->toc_parent), ctx->toc_box);
  }

  ctx->image_monitors = g_ptr_array_new_with_free_func(g_object_unref);

  gtk_box_set_homogeneous(GTK_BOX(ctx->box), FALSE);
  gtk_widget_set_halign(ctx->box, GTK_ALIGN_START);
  gtk_widget_set_margin_start(ctx->box, 20);
  gtk_widget_set_margin_end(ctx->box, 20);

  gtk_widget_set_halign(ctx->toc_box, GTK_ALIGN_START);
  gtk_widget_set_margin_top(ctx->toc_box, 20);

  ctx->toc = toc_new(ctx->toc_box);
  ctx->html = html_new();
  g_print("Initiating parsing: %s\n", listener_get_file_path(listener));

  set_sidebar_title(ctx, listener);
  parse_markdown(ctx, markdown);
}

md_t *
md_new(listener_t *listener, GtkBox *content, GtkBox *toc)
{
  md_t *md = g_new0(md_t, 1);

  md->content_parent = GTK_WIDGET(g_object_ref(content));
  md->toc_parent = GTK_WIDGET(g_object_ref(toc));

  md->listener = listener;

  md->image_monitors = g_ptr_array_new_with_free_func(g_object_unref);
  md->root_path = g_path_get_dirname(listener_get_file_path(listener));

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
  g_free(md);

  return;
}
