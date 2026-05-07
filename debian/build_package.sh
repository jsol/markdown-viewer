#!/bin/bash

NAME="markdown-viewer"
FULLNAME="com.github.jsol.markdownviewer"
VERSION=$(head -n 1 changelog | grep -Po '\d\.\d\.\d')
BINARY="$1"
TARGET="$2"
if [ -z "$BINARY" ]; then
  BINARY="../build/src/$NAME"
fi

if [ -z "$TARGET" ]; then
  TARGET="dist"
fi




echo "Building package for version $VERSION (fetched from changelog)..."

mkdir -p "$TARGET/$NAME/DEBIAN"

# Create control file
cp ubuntu.24.04.control "$TARGET/$NAME/DEBIAN/control"
# Update version in control file
sed -i "s/Version: .*/Version: $VERSION/" "$TARGET/$NAME/DEBIAN/control"

# Copy application file
mkdir -p "$TARGET/$NAME/usr/bin"
objcopy --strip-debug --strip-unneeded "$BINARY" "$TARGET/$NAME/usr/bin/$NAME"

# Add the changelog
mkdir -p "$TARGET/$NAME/usr/share/doc/$NAME"
gzip -9 -n -c  < changelog  > changelog.gz
mv changelog.gz "$TARGET/$NAME/usr/share/doc/$NAME"

# Add copyright
cp copyright "$TARGET/$NAME/usr/share/doc/$NAME/copyright"

# Add man page
mkdir -p "$TARGET/$NAME/usr/share/man/man1"
gzip -9 -n -c  < ../man/manpage.1  > $NAME.1.gz
mv $NAME.1.gz "$TARGET/$NAME/usr/share/man/man1"

# Add desktop entry
mkdir -p "$TARGET/$NAME/usr/share/applications"
cp "$FULLNAME.desktop" "$TARGET/$NAME/usr/share/applications/$FULLNAME.desktop"

# Add icons
mkdir -p "$TARGET/$NAME/usr/share/icons/hicolor"
for img in ../icons/*.png; do
	img_name=$(basename "$img")
	resolution=${img_name%.png}
	folder_name="${resolution}x${resolution}"
	mkdir -p "$TARGET/$NAME/usr/share/icons/hicolor/$folder_name/apps"
	cp "$img" "$TARGET/$NAME/usr/share/icons/hicolor/$folder_name/apps/$FULLNAME.png"
done

# Build the .deb package
cd $TARGET
dpkg-deb --build --root-owner-group $NAME
lintian $NAME.deb

cd -
mv "$TARGET/$NAME.deb" "${NAME}_${VERSION}_amd64.deb"
rm -rf "$TARGET"
