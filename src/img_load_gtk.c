#include "img_load.h"

/* Older "unsafe" version for systems without glycin */

GdkTexture *
img_load(GFile *file, GError **error)
{
  return gdk_texture_new_from_file(file, error);
}
