# Сборка выпусков

Скрипты собирают, прогоняют тесты и упаковывают готовое в zip. Запускать
их можно откуда угодно — путь они берут от себя.

| Скрипт | Цель | Что получается |
|---|---|---|
| `build-win-i386.bat` | Windows XP…11, 32 бита, MinGW 4.9.2 | `iskra-<версия>-windows-i386.zip` |
| `build-win-mingw.bat` | Windows 7…11, 64 бита, MinGW 13.1 | `iskra-<версия>-windows-x86_64-mingw.zip` |
| `build-win-msvc.bat` | Windows 10…11, 64 бита, MSVC | `iskra-<версия>-windows-x86_64-msvc.zip` |

Версия берётся из файла `VERSION` в корне репозитория.

## Что куда кладётся

```
deploy/build/windows_i386/                    промежуточная сборка
deploy/release/iskra-0.1.0-windows-i386/      готовое к упаковке
deploy/release/iskra-0.1.0-windows-i386.zip   выпуск
```

Оба каталога — `build/` и `release/` — в git не входят.

В выпуск кладутся два исполняемых файла и `README.md`. Больше ничего не
нужно: сторонних библиотек у эмулятора нет вовсе, а рантайм увязан внутрь
(`-static` у MinGW, `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` у MSVC), так
что зависимостей, кроме системных `kernel32`, `user32`, `gdi32` и `shell32`,
у обоих файлов не остаётся.

## Пути к цепочкам

Пути прописаны в отдельных файлах — если у вас всё лежит иначе, править
надо только их:

| Файл | Что описывает |
|---|---|
| `vars-mingw-xp.cmd` | `C:\DEV\Qt\Tools\mingw492_32` — цепочка Windows XP |
| `vars-mingw-latest.cmd` | `C:\DEV\Qt\Tools\mingw1310_64` |
| `vars-msvc-latest.cmd` | `C:\DEV\MSVC\msvc` — переносимый MSVC |

CMake и Ninja берутся из поставки Qt (`Tools\CMake_64`, `Tools\Ninja`).
Сам Qt при этом не нужен — эмулятор им не пользуется.

Упаковкой занят общий `pack.cmd`: 7-Zip, если он установлен, иначе
`Compress-Archive` из PowerShell.

## Тесты обязательны

Каждый скрипт прогоняет `ctest` и на первом же отказе останавливается, не
доходя до упаковки. Цепочка MinGW 4.9.2 тут особенно полезна: она ловит то,
чего не замечает GCC 13.

## Другие системы

Окно пока написано только под Windows (`src/host_win32/`). Как появятся
`src/host_x11/`, `src/host_cocoa/` и `src/host_wasm/`, рядом лягут
`build-linux.sh`, `build-macos.sh` и `build-wasm.sh` с теми же именами
выпусков: `iskra-<версия>-<система>-<разрядность>[-<компилятор>].zip`.
