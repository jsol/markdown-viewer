#!/bin/bash

NAME="markdown-viewer"
VERSION=$(head -n 1 changelog | grep -Po '\d\.\d\.\d')

echo "Building package for version $VERSION (fetched from changelog)..."

mkdir -p "dist/$NAME/DEBIAN"

# Create control file
cp ubuntu.24.04.control "dist/$NAME/DEBIAN/control"
# Update version in control file
sed -i "s/Version: .*/Version: $VERSION/" "dist/$NAME/DEBIAN/control"

# Copy application file
mkdir -p "dist/$NAME/usr/bin"
objcopy --strip-debug --strip-unneeded "../build/src/$NAME" "dist/$NAME/usr/bin/$NAME"

# Add the changelog
mkdir -p "dist/$NAME/usr/share/doc/$NAME"
gzip -9 -n -c  < changelog  > changelog.gz
mv changelog.gz "dist/$NAME/usr/share/doc/$NAME"

# Add copyright
cp copyright "dist/$NAME/usr/share/doc/$NAME/copyright"

# Add man page
mkdir -p "dist/$NAME/usr/share/man/man1"
gzip -9 -n -c  < ../man/manpage.1  > $NAME.1.gz
mv $NAME.1.gz "dist/$NAME/usr/share/man/man1"

# Add desktop entry
mkdir -p "dist/$NAME/usr/share/applications"
cp com.github.jsol.markdown-viewer.desktop "dist/$NAME/usr/share/applications/com.github.jsol.markdown-viewer.desktop"

# Add icons
mkdir -p "dist/$NAME/usr/share/icons/hicolor"
for img in ../icons/*.png; do
	img_name=$(basename "$img")
	resolution=${img_name%.png}
	folder_name="${resolution}x${resolution}"
	mkdir -p "dist/$NAME/usr/share/icons/hicolor/$folder_name"
	cp "$img" "dist/$NAME/usr/share/icons/hicolor/$folder_name/$NAME.png"
done

# Build the .deb package
cd dist
sudo chown -R root:root "$NAME"
dpkg-deb --build $NAME
lintian $NAME.deb

cd ..
mv "dist/$NAME.deb" "${NAME}_${VERSION}_amd64.deb"
sudo rm -rf "dist"
