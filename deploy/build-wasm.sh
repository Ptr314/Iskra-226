#!/bin/sh
# Браузер, канва. Сборка, тесты и упаковка пакета для выкладки на сервер.
#
# На выходе — статика и ничего больше: страница, модуль, образы дискет и
# программы примерами. Ни сервера приложений, ни служб.
#
# Нужен Emscripten в PATH (`emcmake`, `emcc`). Проверено на 5.0.4.

set -e
cd "$(dirname "$0")"

_PLATFORM=web
_BUILD_DIR="./build/${_PLATFORM}"

_VERSION=$(tr -d ' \t\r\n' < ../VERSION)
_RELEASE_NAME="iskra-${_VERSION}-${_PLATFORM}"
_RELEASE_DIR="./release/${_RELEASE_NAME}"

if ! command -v emcmake >/dev/null 2>&1; then
    echo "emcmake не найден: нужен Emscripten (source emsdk_env.sh)" >&2
    exit 1
fi

emcmake cmake -S .. -B "$_BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$_BUILD_DIR"

# Тесты обязательны и здесь. Наборы собираются в .js, а гоняет их node —
# CMake подставляет его сам (`CMAKE_CROSSCOMPILING_EMULATOR`). Цепочка
# четвёртая по счёту, и ловит она своё: разрядность указателя тут 32 бита
# при 64-битной сборке рядом.
ctest --test-dir "$_BUILD_DIR" --output-on-failure

rm -rf "$_RELEASE_DIR"
mkdir -p "$_RELEASE_DIR/programs"

# Страница.
cp ../src/host_wasm/web/index.html    "$_RELEASE_DIR/"
cp ../src/host_wasm/web/iskra.css     "$_RELEASE_DIR/"
cp ../src/host_wasm/web/app.js        "$_RELEASE_DIR/"
cp ../src/host_wasm/web/menu.js       "$_RELEASE_DIR/"
cp ../src/host_wasm/web/keyboard.js   "$_RELEASE_DIR/"

# Сам эмулятор.
cp "$_BUILD_DIR/iskra.js"   "$_RELEASE_DIR/"
cp "$_BUILD_DIR/iskra.wasm" "$_RELEASE_DIR/"

# То, что прикладывается: описание с человекочитаемыми названиями, образы
# дискет и программы примерами.
cp _bundle/bundle.json "$_RELEASE_DIR/"
for f in _bundle/*.dsk; do
    [ -e "$f" ] && cp "$f" "$_RELEASE_DIR/"
done
for f in _bundle/programs/*.bas; do
    [ -e "$f" ] && cp "$f" "$_RELEASE_DIR/programs/"
done

# Как это выкладывать.
cp web/README.md            "$_RELEASE_DIR/"
cp web/nginx.conf.example   "$_RELEASE_DIR/"

rm -f "./release/${_RELEASE_NAME}.zip"
if command -v zip >/dev/null 2>&1; then
    (cd "$_RELEASE_DIR" && zip -q -9 -r "../${_RELEASE_NAME}.zip" .)
else
    echo "zip не найден: ставится пакет zip" >&2
    exit 1
fi

echo
echo "  release/${_RELEASE_NAME}.zip"
echo "  release/${_RELEASE_NAME}/  — можно класть в корень сайта как есть"
echo
