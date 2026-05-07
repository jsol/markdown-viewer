# Yet Another Markdown Reader

## What makes this one special?

### Hot reload of the markdown files
File change on disk makes the render update

### Hot reload of all the images in the markdown file
Image changes on disk are rendered

### Change detection and processing of non-markdown files
If a yaml file contains the header `#! <command>` then `<command>` gets
executed whenever the file is changed.

If a plantuml (.puml) file contains the line `'! <command>` then the same
goes for that file.

$INPUT will be replaced by the name of the file the command is in, and
$OUTPUT will be replaced by the name of the file but with the extension switched
to .md.

One use-case for this could be that a script can extract comments from a
YAML definition of a Kubernetes service and render a markdown for that.
The markdown file could have a plantuml file that gets rendered from that
yaml file. In that case, the command

```
markdown-viewer service.yaml service.puml service.md
```
would watch service.yaml for changes, and run the command defined therein which
generates a new version of service.puml and service.md. All the diagrams in
service.puml then gets recreated and the viewer shows the latest markdown with
the latest images.

An example of this kind of render chain is present in examples, with convert.md,
convert.puml and convert.sh (the script that does the rendering).

Each unique auto-command has to be approved in the UI before they are executed.
Approved commands are stored in `[config-dir]/markdown-viewer`

### Opening multiple files
Opening multiple markdown files (or a directory with them) just stacks them one
after another. The heading counter will reset to 1. when the new file begins.
All reloading still works.

### Ignores headers / preamble
Everything before the first header (of any size) is treated as some sort of
meta-data thing and ignored.

## Markdown support
The intention of this viewer is to support all the commonmark features as well
as the gfm extensions. It is however reliant on me finding them useful. Examples
of all the currently supported features are presenting the examples dir.

It also supports basic HTML tables and formatting tags.

