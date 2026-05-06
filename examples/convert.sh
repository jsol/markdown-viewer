#!/bin/bash

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <input yaml file> <output md file>"
  exit 1
fi

IN=$(yq .list[] "$1")
echo "# Converted Markdown" > "$2.tmp"
for i in $IN; do
  echo "1. $i" >> "$2.tmp"
done

PUML="${1%.yaml}.puml"

echo "'!plantuml \$INPUT" > "$PUML.tmp"

IN=$(yq ".images | keys[]" "$1")

for i in $IN; do
  echo "@startuml $i" >> "$PUML.tmp"

  yq ".images.$i[]" "$1" >> "$PUML.tmp"

  echo "" >> "$2.tmp"
  echo "![$i](./$i.png)" >> "$2.tmp"

  echo "@enduml" >> "$PUML.tmp"
done

mv "$PUML.tmp" "$PUML"
mv "$2.tmp" "$2"
