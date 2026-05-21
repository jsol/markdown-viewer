#include <glycin-gtk4.h>
#include <glycin.h>

#include "img_load.h"

/* Glycin is a linux-only fancy image loader that sandboxes the img loading
 * process and does other fancy linux magic tricks. It's not available on
 * older linux distros (or mac/windows), so we use gtk4 for those.
 */

GdkTexture *
img_load(GFile *file, GError **error)
{
  GlyLoader *loader = NULL;
  GlyImage *image = NULL;
  GlyFrame *frame = NULL;
  GdkTexture *texture = NULL;

  loader = gly_loader_new(file);
  image = gly_loader_load(loader, error);
  if (!image) {
    goto out;
  }
  frame = gly_image_next_frame(image, error);
  if (!frame) {
    goto out;
  }

  texture = gly_gtk_frame_get_texture(frame);

out:
  g_clear_object(&image);
  g_clear_object(&frame);
  g_clear_object(&loader);

  return texture;
}
