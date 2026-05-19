#!/bin/sh

set -e

APPID="com.github.jsol.markdownviewer"

cd /src

meson setup /tmp/markdown-viewer --buildtype=release \
  -Dappid="$APPID"
ninja -C /tmp/markdown-viewer

cd /src/debian
bash ./build_package.sh /tmp/markdown-viewer/src/markdown-viewer /tmp/dist /tmp/markdown-viewer/markdown-viewer.deb "$APPID"
