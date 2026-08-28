#!/bin/sh
# macOS, окно на AppKit. Сборка, тесты и упаковка одним заходом.
#
# Универсальный бинарник: без единой правки в исходниках Apple Clang
# компилирует и Intel-, и Apple Silicon-срез в один проход, и `lipo`
# складывает их в один файл. Так выпуск ставится на любой Mac, а собирать
# его достаточно на одном.

set -e
cd "$(dirname "$0")"

_PLATFORM=macos
_ARCHITECTURE=universal2
_BUILD_DIR="./build/${_PLATFORM}_${_ARCHITECTURE}"

_VERSION=$(tr -d ' \t\r\n' < ../VERSION)
_RELEASE_NAME="iskra-${_VERSION}-${_PLATFORM}-${_ARCHITECTURE}"
_RELEASE_DIR="./release/${_RELEASE_NAME}"

cmake -S .. -B "$_BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"
cmake --build "$_BUILD_DIR"
ctest --test-dir "$_BUILD_DIR" --output-on-failure

rm -rf "$_RELEASE_DIR"
mkdir -p "$_RELEASE_DIR"
cp "$_BUILD_DIR/iskra" "$_RELEASE_DIR/"
cp "$_BUILD_DIR/iskra-nohead" "$_RELEASE_DIR/"
cp ../README.md "$_RELEASE_DIR/"
cp ../LICENSE "$_RELEASE_DIR/"

rm -f "./release/${_RELEASE_NAME}.zip"
if command -v zip >/dev/null 2>&1; then
    (cd "$_RELEASE_DIR" && zip -q -9 -r "../${_RELEASE_NAME}.zip" .)
else
    echo "zip не найден" >&2
    exit 1
fi

echo
echo "  release/${_RELEASE_NAME}.zip"
echo
