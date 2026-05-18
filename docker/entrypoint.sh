#!/bin/sh

set -e
cd /src

meson setup /tmp/markdown-viewer
ninja -C /tmp/markdown-viewer

cd /src/debian
bash ./build_package.sh /tmp/markdown-viewer/src/markdown-viewer /tmp/dist /tmp/markdown-viewer/markdown-viewer.deb
