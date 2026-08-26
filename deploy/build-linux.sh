#!/bin/sh
# Linux, окно на Xlib. Сборка, тесты и упаковка одним заходом.
#
# Статически, как под Windows, здесь не увязать: libX11 статической
# библиотекой не поставляется почти нигде. Поэтому у `iskra` остаются
# зависимости — libX11, libstdc++ и libc, — и все они есть в любой системе,
# где вообще запущен X.

set -e
cd "$(dirname "$0")"

_PLATFORM=linux
_ARCHITECTURE=$(uname -m)
_BUILD_DIR="./build/${_PLATFORM}_${_ARCHITECTURE}"

_VERSION=$(tr -d ' \t\r\n' < ../VERSION)
_RELEASE_NAME="iskra-${_VERSION}-${_PLATFORM}-${_ARCHITECTURE}"
_RELEASE_DIR="./release/${_RELEASE_NAME}"

cmake -S .. -B "$_BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$_BUILD_DIR"
ctest --test-dir "$_BUILD_DIR" --output-on-failure

rm -rf "$_RELEASE_DIR"
mkdir -p "$_RELEASE_DIR"
cp "$_BUILD_DIR/iskra" "$_RELEASE_DIR/"
cp "$_BUILD_DIR/iskra-nohead" "$_RELEASE_DIR/"
cp ../README.md "$_RELEASE_DIR/"

rm -f "./release/${_RELEASE_NAME}.zip"
if command -v zip >/dev/null 2>&1; then
    (cd "$_RELEASE_DIR" && zip -q -9 -r "../${_RELEASE_NAME}.zip" .)
else
    echo "zip не найден: ставится пакет zip" >&2
    exit 1
fi

echo
echo "  release/${_RELEASE_NAME}.zip"
echo
