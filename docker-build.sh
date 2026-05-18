#!/bin/bash

NAME=markdown-viewer-builder
docker run -u $(id -u ${USER}):$(id -g $USER) --name $NAME -v $PWD:/src:z markdown-viewer-build-ubuntu24

docker cp $NAME:/tmp/markdown-viewer/src/markdown-viewer .
docker cp $NAME:/tmp/markdown-viewer/markdown-viewer.deb .

docker rm -f $NAME
