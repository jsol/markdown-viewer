#!/bin/sh

set -e
cd /src

meson setup /tmp/markdown-viewer
ninja -C /tmp/markdown-viewer

