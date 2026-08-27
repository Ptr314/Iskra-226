// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: страница и эмулятор — всё, что между ними

'use strict';

var Module = null;

// Дисководов у «Искры» четыре, но с сервера подключаются первые два — те же
// F и R, что в SELECT (`DeviceTable::drive_index`).
var DRIVE_NAME = ['F', 'R'];

var PANE_SCREEN = 0, PANE_PLOT = 1, PANE_TAPE = 2;

var canvas = [], ctx = [], img = [];
var tapeEl, bootEl, audio = null;
var bundle = { disks: [], programs: [] };

// --- то, что эмулятор говорит странице ---------------------------------------
//
// Ровно четыре вызова, и больше хост о странице ничего не знает
// (`src/host_wasm/wasm_host.cpp`, «граница со страницей»).

window.Iskra = {

    ready: function (version) {
        document.getElementById('version').textContent = 'версия ' + version;
        document.title = 'Искра 226 ' + version + ' — BASIC 02';
        if (bootEl) bootEl.hidden = true;
    },

    // Готовый кадр 560x256 словами RGBA — так, как его собрал общий
    // растеризатор `host_common/renderer.*`.
    frame: function (pane, bytes, w, h, changed) {
        var c = canvas[pane];
        if (!c) return;
        if (!img[pane] || img[pane].width !== w || img[pane].height !== h)
            img[pane] = ctx[pane].createImageData(w, h);
        img[pane].data.set(bytes);
        ctx[pane].putImageData(img[pane], 0, 0);
        // Вывод в невыбранную вкладку иначе прошёл бы незамеченным: на
        // десктопе все три окна на виду сразу, а тут видно одно.
        if (changed) markFresh(pane);
    },

    // «Окно завелось» либо «окно закрыли»: у листа графопостроителя и у
    // ленты это появление и исчезновение вкладки.
    pane: function (pane, open) {
        var tab = document.querySelector('.tab[data-pane="' + pane + '"]');
        if (!tab) return;
        tab.hidden = !open;
        if (!open && tab.classList.contains('on')) selectPane(PANE_SCREEN);
        else if (open) markNew(pane);
        updateActions();
    },

    // Прибавка к ленте АЦПУ. Лента копится целиком: прокрутка, поиск и
    // сохранение — то, что на десктопе делает ключ `--printer`.
    tape: function (text) {
        if (!tapeEl) return;
        var atEnd = tapeEl.scrollTop + tapeEl.clientHeight >=
                    tapeEl.scrollHeight - 4;
        tapeEl.textContent += text;
        if (atEnd) tapeEl.scrollTop = tapeEl.scrollHeight;
        markFresh(PANE_TAPE);
    },

    // Код `07` ЗВ — звонок. Своего звука у страницы нет и брать его
    // неоткуда, поэтому он собирается из одного тона.
    bell: function () {
        try {
            if (!audio) audio = new (window.AudioContext ||
                                     window.webkitAudioContext)();
            var o = audio.createOscillator(), g = audio.createGain();
            o.frequency.value = 880;
            g.gain.value = 0.05;
            o.connect(g); g.connect(audio.destination);
            o.start(); o.stop(audio.currentTime + 0.09);
        } catch (e) { /* звук запрещён — не беда */ }
    }
};

// --- вкладки ----------------------------------------------------------------

function selectPane(pane) {
    Array.prototype.forEach.call(document.querySelectorAll('.tab'),
        function (t) {
            var on = +t.dataset.pane === pane;
            t.classList.toggle('on', on);
            if (on) t.classList.remove('fresh');
        });
    [0, 1, 2].forEach(function (p) {
        var el = document.getElementById('pane-' + p);
        if (el) el.hidden = p !== pane;
    });
    updateActions();
}

// Первый вывод на устройство переключает на него — это и есть «окно
// открылось». Дальше вкладка только помечается, чтобы не выдёргивать
// человека с экрана посреди работы.
function markNew(pane) {
    var tab = document.querySelector('.tab[data-pane="' + pane + '"]');
    if (!tab) return;
    if (!tab.dataset.seen) { tab.dataset.seen = '1'; selectPane(pane); }
    else markFresh(pane);
}

// Точка на вкладке: «тут появилось новое, пока вы смотрели в другую сторону».
function markFresh(pane) {
    var tab = document.querySelector('.tab[data-pane="' + pane + '"]');
    if (tab && !tab.hidden && !tab.classList.contains('on'))
        tab.classList.add('fresh');
}

function currentPane() {
    var t = document.querySelector('.tab.on');
    return t ? +t.dataset.pane : PANE_SCREEN;
}

function updateActions() {
    var p = currentPane();
    document.getElementById('save-plot').hidden = p !== PANE_PLOT;
    document.getElementById('save-tape').hidden = p !== PANE_TAPE;
}

// --- сохранение --------------------------------------------------------------

function save(blob, name) {
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
}

function saveDisk(drive) {
    var ptr = Module._iskra_disk_data(drive);
    var size = Module._iskra_disk_size(drive);
    if (!ptr || !size) return;
    var bytes = Module.HEAPU8.slice(ptr, ptr + size);
    var name = Module.UTF8ToString(Module._iskra_disk_name(drive)) ||
               ('drive-' + DRIVE_NAME[drive]);
    save(new Blob([bytes], { type: 'application/octet-stream' }), name);
}

// --- дисководы ---------------------------------------------------------------

function refreshSlots() {
    for (var d = 0; d < 2; ++d) {
        var el = document.getElementById('slot-' + d);
        if (!el) continue;
        var name = Module._iskra_disk_mounted(d)
                 ? Module.UTF8ToString(Module._iskra_disk_name(d)) : '';
        el.textContent = name ? '— ' + name : '— пусто';
    }
}

function mount(drive, bytes, name) {
    var ptr = Module._malloc(bytes.length);
    Module.HEAPU8.set(bytes, ptr);
    var ok = Module.ccall('iskra_mount', 'number',
        ['number', 'number', 'number', 'string', 'number'],
        [drive, ptr, bytes.length, name, 1]);
    Module._free(ptr);
    if (!ok) alert('Не удалось подставить образ: ' + name);
    refreshSlots();
}

function eject(drive) {
    if (Module._iskra_disk_mounted(drive) &&
        Module._iskra_disk_modified(drive)) {
        // Запись на дискету в браузере идёт только в память: файла у неё
        // нет. Молча потерять записанное нельзя — спрашиваем.
        if (confirm('На дискете в дисководе ' + DRIVE_NAME[drive] +
                    ' есть записанное программой.\n' +
                    'Сохранить образ перед извлечением?'))
            saveDisk(drive);
    }
    Module._iskra_unmount(drive);
    refreshSlots();
}

function fetchBytes(path) {
    return fetch(path).then(function (r) {
        if (!r.ok) throw new Error(path + ': ' + r.status);
        return r.arrayBuffer();
    }).then(function (b) { return new Uint8Array(b); });
}

// --- обычная клавиатура ------------------------------------------------------
//
// Раскладка та же, что у трёх прочих хостов (`iskra --help`): зона 8 на
// верхний ряд, слова Бейсика на Alt, правка на Ctrl+E и Ctrl+R.

var K = IskraKeyboard.KEY, CKEY = IskraKeyboard.CK;

var SF = {
    Escape: 0, F1: 1, F2: 2, F3: 3, F4: 4, F5: 5, F6: 6,
    F7: 7, F8: 8, F9: 9, F10: 10, F11: 11, F12: 12,
    PrintScreen: 13, ScrollLock: 14, Pause: 15
};

function api() {
    return {
        text: function (s) {
            Module.ccall('iskra_word', null, ['string'], [s]);
        },
        key: function (code, special) {
            Module._iskra_key(code, special ? 1 : 0);
        },
        control: function (ck) { Module._iskra_control(ck); }
    };
}

function onKeyDown(e) {
    if (!Module) return;
    var a = api();
    var ctrl = e.ctrlKey, alt = e.altKey, code = e.code, key = e.key;

    // Клавиши управления машиной: кодов у них нет вовсе, программе они не
    // достаются (`docs/format.md`, разд. 12).
    if (code === 'Pause' && ctrl && alt) { a.control(CKEY.RESET); e.preventDefault(); return; }
    if (code === 'Pause' && ctrl)        { a.control(CKEY.HALT);  e.preventDefault(); return; }
    if (code === 'Enter' && ctrl)        { a.control(CKEY.CONTINUE); e.preventDefault(); return; }
    if (code === 'KeyN' && ctrl)         { a.control(CKEY.STMT);  e.preventDefault(); return; }

    // Зона 8. Shift даёт верхний банк, 16…31; дубли на Ctrl+F1…F3 — на
    // случай, если PrtScr забрала себе среда.
    if (ctrl && (code === 'F1' || code === 'F2' || code === 'F3')) {
        a.key(13 + (+code[1] - 1) + (e.shiftKey ? 16 : 0), 1);
        e.preventDefault(); return;
    }
    if (Object.prototype.hasOwnProperty.call(SF, code) && !ctrl && !alt) {
        a.key(SF[code] + (e.shiftKey ? 16 : 0), 1);
        e.preventDefault(); return;
    }

    // Зона 6 — правка.
    var edit = null;
    if (code === 'ArrowLeft')  edit = ctrl ? K.LEFT5  : K.LEFT;
    else if (code === 'ArrowRight') edit = ctrl ? K.RIGHT5 : K.RIGHT;
    else if (code === 'ArrowUp')    edit = K.UP;
    else if (code === 'ArrowDown')  edit = K.DOWN;
    else if (code === 'Insert')     edit = K.INSERT;
    else if (code === 'Delete')     edit = K.DELETE;
    else if (code === 'End')        edit = K.ERASE;
    else if (code === 'Backspace')  edit = ctrl ? K.LINE_ERASE : K.BACKSPACE;
    else if (code === 'Enter')      edit = K.CR;
    else if (code === 'KeyE' && ctrl) edit = K.EDIT;
    else if (code === 'KeyR' && ctrl) edit = K.RECALL;
    if (edit !== null) { a.key(edit, 0); e.preventDefault(); return; }

    // Слово Бейсика верхнего регистра зоны 1. Разбирается **по знаку, а не
    // по положению**: на «Искре» русская и латинская буквы сидят на одной
    // клавише, а на PC разъезжаются по раскладкам.
    if (alt && key && key.length === 1) {
        var w = Module.ccall('iskra_keyword', 'string', ['string'], [key]);
        if (w) { a.text(w); e.preventDefault(); }
        return;
    }

    if (!ctrl && !alt && !e.metaKey && key && key.length === 1) {
        a.text(key);
        e.preventDefault();
    }
}

// --- размер кадра ------------------------------------------------------------
//
// Увеличение только целое: экран знакоместный, и дробное растяжение
// однопиксельному шрифту противопоказано — штрихи выходят разной толщины.
//
// Подбирается оно **по ширине окна, а не по высоте**: страница вправе быть
// длиннее экрана и прокручиваться, а вот ужимать из-за этого кадр не стоит —
// первым делом мельчает клавиатура, у которой на клавише три надписи.

var MIN_SCALE = 1, MAX_SCALE = 6;
var zoom = 0;                       // 0 — «подобрать самим»

function bestScale() {
    var availW = document.documentElement.clientWidth - 24;
    return Math.min(MAX_SCALE, Math.max(MIN_SCALE, Math.floor(availW / 560)));
}

function fit() {
    var s = zoom || bestScale();
    s = Math.min(MAX_SCALE, Math.max(MIN_SCALE, s));
    document.documentElement.style.setProperty('--scale', s);

    var val = document.getElementById('zoom-val');
    if (val) val.textContent = '×' + s;
    var out = document.getElementById('zoom-out');
    var inc = document.getElementById('zoom-in');
    if (out) out.disabled = s <= MIN_SCALE;
    if (inc) inc.disabled = s >= MAX_SCALE;

    // Надписи на клавишах меряются от размера самой клавиатуры, а он
    // становится известен только после раскладки.
    var kb = document.getElementById('keyboard');
    if (kb) requestAnimationFrame(function () { IskraKeyboard.resize(kb); });
}

function setZoom(s) {
    zoom = Math.min(MAX_SCALE, Math.max(MIN_SCALE, s));
    try { localStorage.setItem('iskra.zoom', String(zoom)); } catch (e) {}
    fit();
}

// --- запуск ------------------------------------------------------------------

function wireMenu() {
    var menu = document.getElementById('menu');
    IskraMenu.init(menu);

    document.getElementById('m-reset').addEventListener('click', function (e) {
        IskraMenu.shut(e.currentTarget);
        Module._iskra_reset();
    });

    IskraMenu.fill(document.getElementById('m-programs'), bundle.programs,
        function (p) { return p.name; },
        function (p) {
            fetch(p.file).then(function (r) {
                if (!r.ok) throw new Error(p.file + ': ' + r.status);
                return r.text();
            }).then(function (text) {
                // Программа кладётся в память перезагрузкой машины — ровно
                // так же, как ключ `--text` у трёх прочих хостов. Подменять
                // текст под работающим исполнителем нельзя.
                Module.ccall('iskra_load_program', null, ['string'], [text]);
            }).catch(function (err) { alert(String(err)); });
        });

    [0, 1].forEach(function (d) {
        IskraMenu.fill(document.getElementById('m-disk-' + d), bundle.disks,
            function (x) { return x.name; },
            function (x) {
                fetchBytes(x.file).then(function (bytes) {
                    mount(d, bytes, x.name + ' (' + x.file + ')');
                }).catch(function (err) { alert(String(err)); });
            });
    });

    menu.addEventListener('click', function (e) {
        var li = e.target.closest('li');
        if (!li) return;
        if (li.dataset.eject !== undefined) {
            IskraMenu.shut(li);
            eject(+li.dataset.eject);
        }
        if (li.dataset.file !== undefined) {
            IskraMenu.shut(li);
            pickFile(+li.dataset.file);
        }
    });
}

var pickTarget = 0;

function pickFile(drive) {
    pickTarget = drive;
    var picker = document.getElementById('picker');
    picker.value = '';
    picker.click();
}

function wireTabs() {
    document.getElementById('tabs').addEventListener('click', function (e) {
        var tab = e.target.closest('.tab');
        if (!tab) return;
        if (e.target.classList.contains('x')) {
            // Лист и лента закрываются, как окна у трёх прочих хостов: это
            // бумага, а не машина, и следующий вывод вернёт их. Вкладку
            // прячет сам хост ответным `pane(..., false)`, а написанное на
            // бумаге остаётся — её убрали, а не порвали.
            Module._iskra_close_pane(+tab.dataset.pane);
            return;
        }
        selectPane(+tab.dataset.pane);
    });

    document.getElementById('save-plot').addEventListener('click', function () {
        canvas[PANE_PLOT].toBlob(function (b) { save(b, 'plotter.png'); });
    });
    document.getElementById('save-tape').addEventListener('click', function () {
        var text = Module.UTF8ToString(Module._iskra_tape());
        save(new Blob([text], { type: 'text/plain;charset=utf-8' }), 'acpu.txt');
    });
}

function start() {
    canvas[PANE_SCREEN] = document.getElementById('pane-0');
    canvas[PANE_PLOT]   = document.getElementById('pane-1');
    ctx[PANE_SCREEN] = canvas[PANE_SCREEN].getContext('2d');
    ctx[PANE_PLOT]   = canvas[PANE_PLOT].getContext('2d');
    tapeEl = document.getElementById('pane-2');
    bootEl = document.getElementById('boot');

    try {
        var saved = parseInt(localStorage.getItem('iskra.zoom'), 10);
        if (saved > 0) zoom = saved;
    } catch (e) { /* хранилище закрыто — подберём сами */ }

    fit();
    window.addEventListener('resize', fit);
    document.getElementById('zoom-out').addEventListener('click', function () {
        setZoom((zoom || bestScale()) - 1);
    });
    document.getElementById('zoom-in').addEventListener('click', function () {
        setZoom((zoom || bestScale()) + 1);
    });

    IskraKeyboard.build(document.getElementById('keyboard'), {
        text:    function (s) { Module && api().text(s); },
        key:     function (c, sp) { Module && api().key(c, sp); },
        control: function (ck) { Module && api().control(ck); }
    });

    // Описание пакета: человекочитаемые названия дисков и программ. Файлы
    // лежат рядом с ним (`deploy/_bundle/bundle.json`).
    fetch('bundle.json')
        .then(function (r) { return r.ok ? r.json() : { disks: [], programs: [] }; })
        .catch(function () { return { disks: [], programs: [] }; })
        .then(function (b) {
            bundle = { disks: b.disks || [], programs: b.programs || [] };
            return createIskra();
        })
        .then(function (m) {
            Module = m;

            // Ключ `-i` у трёх прочих хостов: пропускать `ASMB`, `$GIO` и
            // вывод на устройство, которого у хоста нет. Командной строки у
            // страницы нет, поэтому он приходит адресом: `?i=1`.
            if (/[?&]i=1\b/.test(location.search)) Module._iskra_skip_machine(1);

            wireMenu();
            wireTabs();
            refreshSlots();

            document.getElementById('picker')
                .addEventListener('change', function (e) {
                    var f = e.target.files && e.target.files[0];
                    if (!f) return;
                    f.arrayBuffer().then(function (b) {
                        mount(pickTarget, new Uint8Array(b), f.name);
                    });
                });

            window.addEventListener('keydown', onKeyDown);

            window.addEventListener('beforeunload', function (e) {
                for (var d = 0; d < 2; ++d)
                    if (Module._iskra_disk_modified(d)) {
                        e.preventDefault();
                        e.returnValue = '';
                        return;
                    }
            });
        })
        .catch(function (err) {
            if (bootEl) bootEl.textContent = 'не удалось запустить: ' + err;
        });
}

if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', start);
else
    start();
