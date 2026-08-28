// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: клавиатура «Искры» на странице — раскладка с docs/keyboard.svg

'use strict';

var IskraKeyboard = (function () {

// Коды клавиш — те же, что в `src/core/keys.h`; восстановлены по корпусу,
// а не выдуманы (`docs/format.md`, разд. 12).
var KEY = {
    CR: 0x85, BACKSPACE: 0x89,
    EDIT: 0x9A, RECALL: 0x9B, INSERT: 0x9C, DELETE: 0x9D,
    ERASE: 0x9E, LINE_ERASE: 0x9F,
    UP: 0xAA, LEFT5: 0xAB, LEFT: 0xAC, RIGHT: 0xAD, RIGHT5: 0xAE, DOWN: 0xAF
};

// Клавиши управления машиной: кодов у них нет вовсе, программе они не
// достаются (`ControlKey` в `keys.h`).
var CK = { HALT: 1, CONTINUE: 2, RESET: 3, STMT: 4 };

// --- раскладка --------------------------------------------------------------
//
// Координаты — те же, что в `docs/keyboard.svg`, единица в единицу: клавиши
// стоят там же, где на рис. 2.2 руководства, вместе с пропусками между
// зонами.
//
// Поле поднято вверх против рисунка на четырнадцать единиц: **номера клавиш
// специальных функций и стрелки напечатаны не на клавишах, а на корпусе под
// ними**, и самих клавиш на рисунке нет вовсе — там только эти подписи.
// Здесь клавиша нужна настоящая, поэтому она встаёт над подписью, а подпись
// остаётся на своём месте, 39.7…49.7.
var X0 = 32, Y0 = 22, BOX_W = 469.6, BOX_H = 143;

var CAP_Y = 25.7, CAP_H = 12;       // шляпка клавиши зоны 8
var PLATE_Y = 39.7, PLATE_H = 10;   // подпись под ней, на корпусе

var keys = [];      // сами клавиши
var plates = [];    // подписи на корпусе под ними

function put(x, y, w, h, spec) {
    spec.x = x; spec.y = y; spec.w = w; spec.h = h;
    keys.push(spec);
}

function plate(x, spec) {
    spec.x = x; spec.y = PLATE_Y; spec.w = 20; spec.h = PLATE_H;
    plates.push(spec);
}

// Зона 8 — шестнадцать клавиш специальных функций. Shift даёт верхний банк,
// 16…31: номера при этом совпадают с номерами клавиш.
//
// Номера стоят **столбиком и прижаты вправо**: слева на корпусе оставлено
// поле, чтобы человек подписал там свою функцию. Так на машине и сделано.
(function () {
    for (var i = 0; i < 16; ++i) {
        var x = 37.6 + 20 * i;
        put(x, CAP_Y, 20, CAP_H, { sf: i });
        plate(x, { num: [String(i), String(i + 16)] });
    }
})();

// Зона 6 — правка. Пять шагов влево и вправо стоят снаружи одиночных, как на
// рисунке; стрелки — тоже подписи на корпусе, а не на клавишах. Шаг в пять
// позиций рисуется с хвостом (`←—` и `—→`), как его и называет разбор
// корпуса (`docs/format.md`, разд. 12): одним знаком их не отличить —
// `←` и `⟵` в большинстве шрифтов одинаковы.
[[357.6, '↑', KEY.UP], [377.6, '←', KEY.LEFT],
 [397.6, '←—', KEY.LEFT5], [417.6, '—→', KEY.RIGHT5],
 [437.6, '→', KEY.RIGHT], [457.6, '↓', KEY.DOWN]
].forEach(function (a) {
    put(a[0], CAP_Y, 20, CAP_H, { k: a[2] });
    plate(a[0], { arrow: a[1] });
});

// `RESET` на рисунке залит красным — как и весь столбец правки.
put(477.6, CAP_Y, 20, CAP_H, { ck: CK.RESET, red: 1 });
plate(477.6, { arrow: 'RESET', word: 1 });

// Зона 1. Три надписи на клавише, и стоят они по-разному:
//
//   * **основное значение — крупно по центру**: русская буква либо цифра;
//   * **второе — мельче, справа вверху**: латинская буква либо знак. Это
//     один и тот же семибитный код с другим пятым битом (Й = 6A, J = 4A),
//     потому они и на одной клавише;
//   * **третье — внизу по центру, мелко**: слово Бейсика верхнего регистра.
//
// Надпись слова берётся с рисунка **как есть, обрезанной шириной клавиши**
// (`PRINTUS`, `RENUMB`, `BACKSP`), а вводится слово целиком: в
// `keywords.cpp` оно дописано по смыслу. У клавиши `Д` надпись читается как
// `RES`, и слова у неё нет вовсе — выдумывать незачем.
function zone1(y, list) {
    list.forEach(function (a) {
        put(a[0], y, 20, 20,
            { z1: 1, c: [a[1], a[2]], label: a[3],
              kw: a.length > 4 ? a[4] : a[3],
              face: a.length > 5 ? a[5] : a[1] });
    });
}

zone1(60, [
    [57.8,  ';', '+', 'ADD'],     [77.8,  '1', '!', 'AND('],
    [97.8,  '2', '"', 'BACKSP', 'BACKSPACE'],
    [117.6, '3', '#', 'BIN('],    [137.6, '4', '¤', 'BOOL'],
    [157.6, '5', '%', 'COM'],     [177.6, '6', '&', 'CONVERT'],
    [197.6, '7', "'", 'DATA'],    [217.6, '8', '(', 'DEFFN'],
    [237.6, '9', ')', "DEFFN'"],  [257.6, '0', ':', 'DIM'],
    [277.6, '-', '=', 'END']
]);

zone1(80, [
    [67.8,  'Й', 'J', 'FOR'],   [87.7,  'Ц', 'C', '¤GIO'],
    [107.7, 'У', 'U', 'GOSUB'], [127.5, 'К', 'K', "GOSUB'"],
    [147.5, 'Е', 'E', 'GOTO'],  [167.4, 'Н', 'N', 'HEX('],
    [187.4, 'Г', 'G', 'HEXPRINT'], [207.3, 'Ш', '[', 'IF'],
    [227.3, 'Щ', ']', 'INIT'],  [247.1, 'З', 'Z', 'INPUT'],
    [267.1, 'Х', 'H', 'INT('],  [287.0, ':', '*', 'KEYIN']
]);

zone1(100, [
    [77.8,  'Ф', 'F', 'NEXT'],  [97.7,  'Ы', 'Y', 'ON'],
    [117.7, 'В', 'W', 'OR('],   [137.5, 'А', 'A', 'PACK('],
    [157.5, 'П', 'P', 'PRINTUS', 'PRINTUSING'],
    [177.4, 'Р', 'R', 'READ'],  [197.4, 'О', 'O', 'REM'],
    [217.3, 'Л', 'L', 'RENUMB', 'RENUMBER'],
    [237.3, 'Д', 'D', 'RES', null],
    [257.1, 'Ж', 'V', 'RESTORE'], [277.1, 'Э', '\\', 'RETURN'],
    [297.0, '.', '>', 'REWIND']
]);

zone1(120, [
    [87.8,  'Я', 'Q', 'RND('],  [107.7, 'Ч', '^', 'ROTATE'],
    [127.7, 'С', 'S', 'SELECT'], [147.5, 'М', 'M', 'SKIP'],
    [167.5, 'И', 'I', 'SGN('],  [187.4, 'Т', 'T', 'STEP'],
    [207.4, 'Ь', 'X', 'STOP'],  [227.3, 'Б', 'B', 'STR('],
    [247.3, 'Ю', '@', 'TAB('],  [267.1, ',', '<', 'TEN'],
    [287.1, '/', '?', 'TRACE'],
    // На клавише напечатан знак числа «пи», а вводит она три знака `#PI`:
    // именно так эта константа записывается в тексте программы.
    [307.0, '#PI', '_', 'UNPACK', 'UNPACK', 'π']
]);

// Зона 2 — клавиши-слова. Букв у них нет вовсе; вводят они слово целиком,
// так же как верхний регистр зоны 1. На обычной клавиатуре их вешать некуда,
// а здесь клавиша есть, и она работает.
[[337.6, 'RUN', 1], [357.6, 'CLEAR'], [377.6, 'LIST'],
 [397.6, 'PRINT'], [417.6, 'LOAD'], [437.6, 'SAVE']
].forEach(function (a) { put(a[0], 60, 20, 20, { w2: a[1], red: a[2] }); });

put(297.6, 60, 20, 20, { ck: CK.STMT, t: 'STMT NUMBER' });

// Зона 5 — обозначения стандартных функций Бейсика на цифровом блоке
// (разд. 2.1). Значений тут два: знак крупно по центру, имя функции под ним.
[[417.6, 80, ',', 'ARC'],   [437.6, 80, ':', 'TAN('],
 [417.6, 100, '(', 'SIN('], [437.6, 100, ')', 'COS('],
 [417.6, 120, '-', 'EXP('], [437.6, 120, '/', 'LOG('],
 [417.6, 140, '+', 'SQR('], [437.6, 140, '*', 'ABS(']
].forEach(function (a) {
    put(a[0], a[1], 20, 20, { two: 1, c: [a[2], null], label: a[3], kw: a[3] });
});

// Цифровой блок: одно значение, крупно по центру.
[[357.6, 80, '7'], [377.6, 80, '8'], [397.6, 80, '9'],
 [357.6, 100, '4'], [377.6, 100, '5'], [397.6, 100, '6'],
 [357.6, 120, '1'], [377.6, 120, '2'], [397.6, 120, '3'],
 [397.6, 140, '.']
].forEach(function (a) { put(a[0], a[1], 20, 20, { c: [a[2], null] }); });
put(357.6, 140, 40, 20, { c: ['0', null] });

// Зоны 6 и 7 — правка и конец набора. На рисунке весь этот столбец залит
// красным, как и `RESET`.
[[477.6, 60,  KEY.EDIT,       'EDIT',       1],
 [477.6, 80,  KEY.RECALL,     'RECALL',     1],
 [477.6, 100, KEY.INSERT,     'INSERT',     1],
 [477.6, 120, KEY.DELETE,     'DELETE',     1],
 [477.6, 140, KEY.ERASE,      'ERASE',      1],
 [337.6, 80,  KEY.BACKSPACE,  'BACK SPACE', 1],
 [337.6, 100, KEY.LINE_ERASE, 'LINE ERASE', 1]
].forEach(function (a) {
    put(a[0], a[1], 20, 20, { k: a[2], t: a[3], red: a[4] });
});

put(337.6, 120, 20, 20, { ck: CK.CONTINUE, t: 'CON- TINUE', red: 1 });
put(337.6, 140, 20, 20, { ck: CK.HALT,     t: 'HALT/ STEP', red: 1 });

// Регистр и пробел.
put(37.8, 100, 40, 20, { mod: 'lock',  t: 'SHIFT LOCK' });
put(37.8, 140, 40, 20, { mod: 'shift', t: 'SHIFT' });
put(107.7, 140, 180, 20, { c: [' ', ' '], blank: 1 });
put(287.7, 140, 40, 20, { k: KEY.CR, t: 'CR/LF' });

// --- сборка -----------------------------------------------------------------

function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
                    .replace(/>/g, '&gt;');
}

function face(spec) {
    // Зона 1: основное крупно по центру, второе мельче справа вверху, слово
    // внизу по центру.
    if (spec.z1) {
        var h = '<b class="k-main">' + esc(spec.face) + '</b>' +
                '<i class="k-alt">' + esc(spec.c[1]) + '</i>';
        if (spec.kw)
            h += '<u class="k-word" title="' + esc(spec.kw) + '">' +
                 esc(spec.label) + '</u>';
        else
            h += '<u class="k-word k-unk" ' +
                 'title="надпись на рис. 2.2 обрезана: слова у клавиши нет">' +
                 esc(spec.label) + '</u>';
        return h;
    }

    // Зона 5: знак крупно, имя функции под ним.
    if (spec.two)
        return '<b class="k-main k-high">' + esc(spec.c[0]) + '</b>' +
               '<u class="k-word">' + esc(spec.label) + '</u>';

    // Цифровой блок и пробел: одно значение.
    if (spec.c)
        return spec.blank ? '' : '<b class="k-main">' + esc(spec.c[0]) + '</b>';

    // Клавиша спецфункций пуста: номера напечатаны на корпусе под ней.
    if (typeof spec.sf === 'number') return '';

    return '<span class="k-text">' +
           esc(spec.w2 || spec.t || '').split(' ').join('<br>') + '</span>';
}

function cls(spec) {
    var c = 'key';
    if (spec.mod) c += ' mod';
    if (spec.red) c += ' red';
    if (typeof spec.sf === 'number') c += ' sf';
    return c;
}

function place(el, spec) {
    el.style.left   = ((spec.x - X0) / BOX_W * 100) + '%';
    el.style.top    = ((spec.y - Y0) / BOX_H * 100) + '%';
    el.style.width  = (spec.w / BOX_W * 100) + '%';
    el.style.height = (spec.h / BOX_H * 100) + '%';
}

// api: { text(строка), key(код, спецфункция), control(номер) }
function build(root, api) {
    var upper = false, latched = false;

    function show() { root.classList.toggle('up', upper); }

    function setUpper(on, latch) {
        upper = on;
        latched = !!latch;
        show();
        Array.prototype.forEach.call(root.querySelectorAll('.key.mod'),
            function (el) {
                el.classList.toggle('on',
                    upper && (latched ? el.dataset.mod === 'lock'
                                      : el.dataset.mod === 'shift'));
            });
    }

    root.innerHTML = '';

    // Подписи на корпусе кладутся первыми — они под клавишами и по смыслу, и
    // по порядку наложения.
    plates.forEach(function (spec) {
        var el = document.createElement('div');
        el.className = 'plate';
        place(el, spec);
        if (spec.num)
            el.innerHTML = '<span class="p-num">' + esc(spec.num[0]) +
                           '<br>' + esc(spec.num[1]) + '</span>';
        else
            el.innerHTML = '<span class="p-arrow' + (spec.word ? ' p-word' : '') +
                           '">' + esc(spec.arrow) + '</span>';
        root.appendChild(el);
    });

    keys.forEach(function (spec) {
        var el = document.createElement('button');
        el.type = 'button';
        el.className = cls(spec);
        place(el, spec);
        el.innerHTML = face(spec);
        if (spec.mod) el.dataset.mod = spec.mod;

        el.addEventListener('mousedown', function (e) { e.preventDefault(); });
        el.addEventListener('click', function (e) {
            // Щелчок по слову вводит слово — на обычной клавиатуре тому же
            // отвечает Alt+клавиша.
            var onWord = e.target.classList &&
                         e.target.classList.contains('k-word') &&
                         !e.target.classList.contains('k-unk');
            if (onWord && spec.kw) { api.text(spec.kw); return; }

            if (spec.mod) {
                if (spec.mod === 'lock') setUpper(!(upper && latched), true);
                else setUpper(!(upper && !latched), false);
                return;
            }
            if (spec.c) {
                var ch = (upper && spec.c[1]) ? spec.c[1] : spec.c[0];
                api.text(ch);
                if (upper && !latched) setUpper(false, false);
                return;
            }
            if (typeof spec.sf === 'number') {
                api.key(spec.sf + (upper ? 16 : 0), 1);
                if (upper && !latched) setUpper(false, false);
                return;
            }
            if (spec.w2) { api.text(spec.w2); return; }
            if (spec.k)  { api.key(spec.k, 0); return; }
            if (spec.ck) { api.control(spec.ck); return; }
        });
        root.appendChild(el);
    });
    show();
}

// Надписи меряются в тех же единицах, что и рисунок: у него знак 3.88 при
// клавише в 20 единиц. Хост зовёт это при каждом изменении размера, и все
// надписи растут вместе с полем.
function resize(root) {
    var w = root.clientWidth;
    if (w) root.style.fontSize = (w / BOX_W * 3.88) + 'px';
}

// `keys` отдаётся наружу ради проверки раскладки без браузера
// (`tools/probes/wasm_smoke.js`): 110 клавиш, ни одного наложения, ничего
// за краем — ровно как в `docs/keyboard.svg`.
return { build: build, resize: resize, KEY: KEY, CK: CK,
         keys: keys, plates: plates,
         box: { x: X0, y: Y0, w: BOX_W, h: BOX_H } };

})();

if (typeof module !== 'undefined') module.exports = IskraKeyboard;
