#pragma once

#include <gio/gio.h>
#include <glib.h>

typedef struct listener listener_t;

/**
 * Callback for when the listener needs to approve the run command.
 *
 * @param listener The listener
 * @param run_cmd The run command
 * @param user_data The user data
 */
typedef void (*listener_approved_cb)(listener_t *listener, const gchar *run_cmd,
                                     gpointer user_data);
/**
 * Callback for when the listener has read the markdown.
 *
 * @param listener The listener
 * @param md The markdown
 * @param user_data The user data
 */
typedef void (*listener_md_cb)(listener_t *listener, const gchar *md,
                               gpointer user_data);

/**
 * Create a new listener.
 *
 * @param file The file to listen to
 * @param cb The callback for when the listener needs to approve the run command
 * @param md_cb The callback for when the listener has read the markdown
 * @param user_data The user data for the callbacks
 *
 * @return The new listener
 */
listener_t *listener_new(GFile *file, listener_approved_cb cb,
                         gpointer user_data);


void listener_set_md_cb(listener_t *listener, listener_md_cb md_cb);

/**
 * Approve the run command.
 *
 * @param listener The listener
 */
void listener_approve_run(listener_t *listener);

/**
 * Check if the file is a markdown file.
 *
 * @param listener The listener
 *
 * @return TRUE if the file is a markdown file
 */
gboolean listener_is_md(listener_t *listener);

/**
 * Get the output file (the input file with extension switched to md).
 *
 * @param listener The listener
 *
 * @return The output file
 */
GFile *listener_get_output_file(listener_t *listener);

/**
 * Get the run command.
 *
 * @param listener The listener
 *
 * @return The run command
 */
const gchar *listener_get_run_cmd(listener_t *listener);

/**
 * Get the file path.
 *
 * @param listener The listener
 *
 * @return The file path
 */
const gchar *listener_get_file_path(listener_t *listener);

/**
 * Free the listener.
 *
 * @param listener The listener
 */
void listener_free(listener_t *listener);
