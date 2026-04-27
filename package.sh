#!/usr/bin/env sh
NAME="issex-0.0.1"
OUT="$NAME.tar.gz"

python3 ./man2html.py

rm -f "$OUT"
rm -rf dist

mkdir -p "dist/$NAME"

cp issex "dist/$NAME/"
cp README.md "dist/$NAME/"
cp LICENSE "dist/$NAME/"
cp -r docs "dist/$NAME/"

tar -czf "$OUT" -C dist "$NAME"

echo "Done: $OUT"
