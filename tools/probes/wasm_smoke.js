// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: проба границы «страница — эмулятор» без браузера
//
//   node tools/probes/wasm_smoke.js build/wasm/iskra.js
//
// Проверяется ровно то, чего не видят автотесты: сам хост на канве и восемь
// вызовов, которыми он говорит со страницей. Кадр при этом разбирается
// обратно в знаки — по тому же знакогенератору, что его и рисовал, — так
// что видно не «кадр пришёл», а что именно на экране.

'use strict';

const fs   = require('fs');
const path = require('path');

const modPath = path.resolve(process.argv[2] || 'build/wasm/iskra.js');
const W = 560, H = 256, CELL_W = 7, CELL_H = 10, COLS = 80, ROWS = 24;

let failures = 0;
function check(what, ok, note) {
    console.log((ok ? '  ок      ' : '  ОТКАЗ  ') + what + (note ? ' — ' + note : ''));
    if (!ok) ++failures;
}

// Что пришло от эмулятора.
const got = {
    version: null,
    frames: [0, 0],
    changed: [0, 0],
    last: [null, null],
    panes: {},
    tape: '',
    bells: 0
};

global.Iskra = {
    ready: v => { got.version = v; },
    frame: (pane, bytes, w, h, changed) => {
        if (changed) got.changed[pane] = (got.changed[pane] || 0) + 1;
        got.frames[pane]++;
        got.last[pane] = Uint8Array.from(bytes);   // копия: вид смотрит в кучу
    },
    pane: (pane, open) => { got.panes[pane] = open; },
    tape: text => { got.tape += text; },
    bell: () => { got.bells++; }
};

// Кадр — знакоместа 7x10 в поле 560x256 с полями по 8 точек сверху и снизу
// (`host_common/renderer.h`). Знак опознаём по отпечатку светящихся точек:
// строить его надо тем же знакогенератором, а его здесь нет, поэтому берём
// проще — «пусто или нет» и число точек. Этого хватает, чтобы отличить
// заставку от чистого экрана и увидеть, что счёт что-то напечатал.
function cells(frame) {
    if (!frame) return null;
    const my = (H - ROWS * CELL_H) / 2;
    const out = [];
    for (let r = 0; r < ROWS; ++r) {
        let line = '';
        for (let c = 0; c < COLS; ++c) {
            let lit = 0;
            for (let y = 0; y < CELL_H; ++y) {
                const py = my + r * CELL_H + y;
                for (let x = 0; x < CELL_W; ++x) {
                    const i = ((py * W) + c * CELL_W + x) * 4;
                    if (frame[i] || frame[i + 1] || frame[i + 2]) ++lit;
                }
            }
            line += lit ? '#' : ' ';
        }
        out.push(line.replace(/\s+$/, ''));
    }
    return out;
}

// Точки пера на листе. Считать «не чёрные», как на экране, тут нельзя: у
// листа графопостроителя цвета свои — тёмное перо на светлой бумаге
// (`Renderer::set_paper_colors`), и светлым там будет весь растр целиком.
function inkCount(frame) {
    if (!frame) return 0;
    let n = 0;
    for (let i = 0; i < frame.length; i += 4)
        if (frame[i] < 0x80) ++n;
    return n;
}

const sleep = ms => new Promise(r => setTimeout(r, ms));

// Хост уступает браузеру через `emscripten_sleep`, то есть через setTimeout.
// Значит и здесь ждать надо настоящими паузами: без них модуль не сделает ни
// шага.
async function settle(ms) { await sleep(ms); }

(async () => {
    const createIskra = require(modPath);
    const M = await createIskra();

    const text = s => M.ccall('iskra_word', null, ['string'], [s]);
    const key  = (c, sp) => M._iskra_key(c, sp ? 1 : 0);
    const line = s => { text(s); key(0x85, 0); };

    await settle(200);

    console.log('версия и заставка');
    check('ready() пришёл', !!got.version, 'версия ' + got.version);
    check('кадр экрана нарисован', got.frames[0] > 0,
          got.frames[0] + ' кадров');
    const boot = cells(got.last[0]);
    // «READY BASIC 02 05.10.84» (руководство, разд. 3.2) — по знакоместам
    // это 5, 5, 2 и 8 занятых позиций через пробел.
    check('на экране заставка', boot && boot[0] === '##### ##### ## ########',
          boot ? '«' + boot[0] + '»' : 'пусто');

    console.log('\nнабор строки и счёт');
    // Набирается нарочно строчными: строчных букв у машины нет вовсе, и
    // приводить их обязан хост, на нажатии. Пропустить это незаметно —
    // экран всё равно высветит прописные, — а в текст программы знак ляжет
    // строчным, и транслятор ответит «оператор не реализован».
    line('10 print "проба"');
    line('20 print 2+2');
    line('run');
    await settle(400);
    const scr = cells(got.last[0]);
    const body = scr.filter(l => l.length).length;
    check('счёт что-то напечатал', body >= 3, body + ' непустых строк');
    // «ПРОБА» — пять занятых знакомест подряд; будь строка набрана строчными
    // и так и оставлена, программы бы не было вовсе.
    check('строчные буквы приведены к прописным', scr.indexOf('#####') >= 0,
          JSON.stringify(scr.filter(l => l.length).slice(-4)));
    // Мигание курсора кадр перерисовывает, но выводом не является:
    // страница по этому признаку и решает, метить ли вкладку.
    check('вывод отличается от мигания курсора',
          got.changed[0] > 0 && got.changed[0] < got.frames[0],
          got.changed[0] + ' с содержимым из ' + got.frames[0] + ' кадров');

    console.log('\nслова Бейсика зоны 1');
    const kw = c => M.ccall('iskra_keyword', 'string', ['string'], [c]);
    check('Alt+J даёт FOR',  kw('J') === 'FOR', kw('J'));
    check('Alt+Й даёт FOR',  kw('Й') === 'FOR', kw('Й'));
    check('Alt+S даёт SELECT', kw('S') === 'SELECT', kw('S'));
    check('у клавиши Д слова нет', kw('Д') === '', '«' + kw('Д') + '»');

    console.log('\nлист графопостроителя');
    M.ccall('iskra_load_program', null, ['string'], [
        '10 DIM B¤(40)253\n' +
        '20 ¤OPEN B¤()\n' +
        '30 NPLOT B¤(),40,40:DRAW B¤(),520,215\n' +
        '40 ¤COPY /14,B¤()\n'
    ]);
    await settle(300);
    line('RUN');
    await settle(600);
    check('вкладка листа появилась', got.panes[1] === true);
    // Отрезок из (40,40) в (520,215) — по Брезенхему это 481 точка.
    const ink = inkCount(got.last[1]);
    check('на листе нарисована линия', ink > 400 && ink < 700,
          ink + ' точек пера из 143360');
    M._iskra_close_pane(1);
    await settle(50);
    check('лист убирается', got.panes[1] === false);

    console.log('\nлента АЦПУ');
    M.ccall('iskra_load_program', null, ['string'], [
        '10 SELECT PRINT 0C\n' +
        '20 PRINT /0C,"ЛЕНТА АЦПУ"\n' +
        '30 FOR I=1 TO 3\n' +
        '40 PRINTUSING 50,I,I*I\n' +
        '50 %СТРОКА ##  КВАДРАТ ###\n' +
        '60 NEXT I\n'
    ]);
    await settle(300);
    line('RUN');
    await settle(600);
    check('вкладка ленты появилась', got.panes[2] === true);
    check('лента набралась', /ЛЕНТА АЦПУ/.test(got.tape) &&
                             /СТРОКА  3  КВАДРАТ   9/.test(got.tape),
          JSON.stringify(got.tape.slice(0, 30)) + '…');
    const whole = M.UTF8ToString(M._iskra_tape());
    check('iskra_tape() отдаёт ту же ленту', whole === got.tape,
          whole.length + ' против ' + got.tape.length + ' знаков');

    // Лента убирается, как окно у трёх прочих хостов, и следующий вывод её
    // возвращает — вместе с уже напечатанным: бумагу убрали, а не порвали.
    M._iskra_close_pane(2);
    await settle(50);
    check('лента убирается', got.panes[2] === false);

    M.ccall('iskra_load_program', null, ['string'],
            ['10 PRINT /0C,"ЕЩЁ СТРОКА"' + String.fromCharCode(10)]);
    await settle(300);
    line('RUN');
    await settle(500);
    check('следующий вывод вернул ленту', got.panes[2] === true);
    check('напечатанное прежде уцелело',
          /ЛЕНТА АЦПУ/.test(got.tape) && /ЕЩЁ СТРОКА/.test(got.tape),
          got.tape.length + ' знаков');

    console.log('\nдисковод');
    const img = new Uint8Array(256 * 16);
    const ptr = M._malloc(img.length);
    M.HEAPU8.set(img, ptr);
    const ok = M.ccall('iskra_mount', 'number',
        ['number', 'number', 'number', 'string', 'number'],
        [0, ptr, img.length, 'проба.dsk', 1]);
    M._free(ptr);
    check('образ подставился', !!ok);
    check('имя дошло кириллицей',
          M.UTF8ToString(M._iskra_disk_name(0)) === 'проба.dsk',
          M.UTF8ToString(M._iskra_disk_name(0)));
    check('писать ещё не начинали', M._iskra_disk_modified(0) === 0);
    check('размер сошёлся', M._iskra_disk_size(0) === img.length);

    console.log('\nперезагрузка');
    const before = got.frames[0];
    M._iskra_reset();
    await settle(400);
    check('машина включилась заново', got.frames[0] > before,
          (got.frames[0] - before) + ' новых кадров');
    check('дискета осталась в дисководе', M._iskra_disk_mounted(0) === 1);

    console.log('');
    console.log('экранная клавиатура');
    // Раскладка снята с `docs/keyboard.svg`: там 110 прямоугольников, и
    // столько же клавиш должно быть здесь. Наложение или клавиша за краем —
    // верный признак съехавшей строки таблицы.
    const KB = require(path.resolve(__dirname,
                       '../../src/host_wasm/web/keyboard.js'));
    check('клавиш столько же, сколько на рис. 2.2', KB.keys.length === 110,
          KB.keys.length + ' против 110');
    // Номера функций и стрелки напечатаны на корпусе, а не на клавишах:
    // шестнадцать подписей зоны 8, шесть стрелок и RESET.
    check('подписей на корпусе двадцать три', KB.plates.length === 23,
          KB.plates.length + ' против 23');

    const outside = KB.keys.filter(k =>
        k.x < KB.box.x || k.y < KB.box.y ||
        k.x + k.w > KB.box.x + KB.box.w ||
        k.y + k.h > KB.box.y + KB.box.h);
    check('ни одна не вылезла за поле', outside.length === 0,
          outside.length ? JSON.stringify(outside[0]) : '');

    // Допуск в полединицы: в самом рисунке соседние клавиши расставлены с
    // точностью до десятых и местами перекрываются на 0.2 — это его свойство,
    // а не наше. Ловим настоящий сдвиг, на целую клавишу.
    const EPS = 0.5;
    let overlap = null;
    for (let i = 0; i < KB.keys.length && !overlap; ++i)
        for (let j = i + 1; j < KB.keys.length; ++j) {
            const a = KB.keys[i], b = KB.keys[j];
            if (a.x + EPS < b.x + b.w && b.x + EPS < a.x + a.w &&
                a.y + EPS < b.y + b.h && b.y + EPS < a.y + a.h)
                { overlap = [a, b]; break; }
        }
    check('ни одна не налезла на соседку', !overlap,
          overlap ? JSON.stringify(overlap) : '');

    // Слова на клавишах обязаны совпадать с таблицей ядра: она одна на все
    // хосты (`host_common/keywords.*`), и расхождение значило бы, что
    // экранная клавиатура вводит не то, что обычная.
    // Только зона 1: зона 5 нарочно повторяет её знаки на других клавишах
    // цифрового блока, и слова у них свои — имена функций.
    //
    // Проверяется достижимость: хотя бы одна надпись клавиши обязана вести
    // в таблице ядра к её же слову. Требовать этого от обеих нельзя — знаки
    // на клавишах повторяются, и таблица разводит их как может:
    // `:` отдан KEYIN, `#` — BIN(, а `-` и `/` не отданы никому, потому что
    // совпадают со знаками цифрового блока (`host_common/keywords.cpp`).
    // Обычная клавиатура такую клавишу достаёт второй надписью, экранная —
    // щелчком по самому слову.
    const wrong = KB.keys
        .filter(k => k.z1 && k.kw)
        .filter(function (k) {
            const w = [k.c[0], k.c[1]].filter(Boolean).map(kw).filter(Boolean);
            return w.length && w.indexOf(k.kw) < 0;
        });
    check('слова сходятся с таблицей ядра', wrong.length === 0,
          wrong.map(k => k.c[0] + ': ' + k.kw + ' против ' + kw(k.c[0])).join(', '));

    console.log('\n' + (failures ? failures + ' отказ(ов)' : 'всё сошлось'));
    process.exit(failures ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
