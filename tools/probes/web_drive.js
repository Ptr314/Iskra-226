// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: прогон страницы эмулятора в живом Chrome
//
// Проверяет то, чего не видит ни `ctest`, ни `wasm_smoke.js`: саму страницу —
// вёрстку, меню, вкладки, настоящие нажатия и щелчки. Ошибки тут свои,
// страничные: скрытая вкладка, которую видно поверх выбранной; заставка,
// которая не убралась; невидимое подменю, по которому не попасть щелчком.
//
// Сначала поднять сервер над готовым пакетом и Chrome с отладочным портом:
//
//   cd deploy/release/iskra-<версия>-web && python3 -m http.server 8765
//   chrome --headless=new --remote-debugging-port=9222 \
//          --user-data-dir=/tmp/iskra-chrome --window-size=1200,1150 about:blank
//   node tools/probes/web_drive.js
//
// Снимки экрана ложатся в каталог, названный третьим доводом (по умолчанию
// текущий): смотреть их полезно и тогда, когда всё сошлось.

'use strict';

const fs = require('fs');

const URL  = process.argv[2] || 'http://127.0.0.1:8765/';
const CDP  = process.argv[3] || 'http://127.0.0.1:9222';
const SHOTS = process.argv[4] || '.';

const sleep = ms => new Promise(r => setTimeout(r, ms));

let ws, next = 1, failures = 0;
const waiting = new Map();
const problems = [];

function check(what, ok, note) {
    console.log((ok ? '  ок      ' : '  ОТКАЗ  ') + what + (note ? ' — ' + note : ''));
    if (!ok) ++failures;
}

function send(method, params) {
    return new Promise((res, rej) => {
        const id = next++;
        waiting.set(id, { res, rej });
        ws.send(JSON.stringify({ id, method, params: params || {} }));
    });
}

async function evaluate(expression) {
    const r = await send('Runtime.evaluate',
        { expression, awaitPromise: true, returnByValue: true });
    if (r.exceptionDetails)
        throw new Error(String(r.exceptionDetails.text) + ' в ' + expression.slice(0, 60));
    return r.result && r.result.value;
}

async function shot(name) {
    const r = await send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(SHOTS + '/' + name, Buffer.from(r.data, 'base64'));
}

// --- ввод -------------------------------------------------------------------

async function press(key, code, text, mods) {
    const base = { key, code: code || '', modifiers: mods || 0 };
    await send('Input.dispatchKeyEvent',
        Object.assign({ type: text ? 'keyDown' : 'rawKeyDown', text }, base));
    await send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, base));
    await sleep(10);
}

async function type(s) { for (const ch of s) await press(ch, '', ch); }
async function enter() { await press('Enter', 'Enter'); await sleep(80); }

async function box(sel) {
    return evaluate(
        '(() => { const e = document.querySelector(' + JSON.stringify(sel) + ');' +
        '  if (!e) return null;' +
        '  const r = e.getBoundingClientRect();' +
        '  return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; })()');
}

async function move(x, y) {
    await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
    await sleep(60);
}

async function click(sel) {
    const b = await box(sel);
    if (!b) throw new Error('нет такого элемента: ' + sel);
    await move(b.x, b.y);
    for (const type of ['mousePressed', 'mouseReleased'])
        await send('Input.dispatchMouseEvent',
            { type, x: b.x, y: b.y, button: 'left', clickCount: 1 });
    await sleep(150);
}

// Меню раскрывается одним CSS, по наведению, поэтому до щелчка надо навести.
// `:hover` достаётся и всем предкам наведённого узла, так что цепочку хватает
// пройти по порядку. После выбора указатель уводится прочь: иначе не снимется
// класс `shut`, которым меню гасится (`web/menu.js`).
async function pick(path) {
    for (const sel of path.slice(0, -1)) {
        const b = await box(sel);
        if (!b) throw new Error('нет такого элемента: ' + sel);
        await move(b.x, b.y);
    }
    await click(path[path.length - 1]);
    await move(600, 1140);
    await sleep(120);
}

const SYS   = '#menu > li:nth-child(1) > div';
const PROGS = '#menu > li:nth-child(1) > ul > li:nth-child(2) > div';
const RESET = '#m-reset div';
const DISKS = '#menu > li:nth-child(2) > div';
const DRV_F = '#menu > li:nth-child(2) > ul > li:nth-child(1) > div';
const SRV_F = '#menu > li:nth-child(2) > ul > li:nth-child(1) > ul > li:nth-child(3) > div';

// --- чтение экрана ----------------------------------------------------------
//
// Кадр — знакоместа 7x10 в поле 560x256 с полями по 8 точек сверху и снизу
// (`host_common/renderer.h`). Знакогенератора здесь нет, поэтому знак сводится
// к «занято или пусто»: этого хватает, чтобы отличить заставку от чистого
// экрана и сосчитать напечатанное.
const READ_SCREEN = `(() => {
    const c = document.getElementById('pane-0');
    const d = c.getContext('2d').getImageData(0, 0, 560, 256).data;
    const my = (256 - 24 * 10) / 2;
    const out = [];
    for (let r = 0; r < 24; ++r) {
        let s = '';
        for (let col = 0; col < 80; ++col) {
            let lit = 0;
            for (let y = 0; y < 10; ++y) {
                const py = my + r * 10 + y;
                for (let x = 0; x < 7; ++x) {
                    const i = ((py * 560) + col * 7 + x) * 4;
                    if (d[i] || d[i + 1] || d[i + 2]) ++lit;
                }
            }
            s += lit ? '#' : ' ';
        }
        out.push(s.replace(/ +$/, ''));
    }
    return out;
})()`;

// Точки пера на листе: у него цвета свои — тёмное перо на светлой бумаге,
// и «не чёрное» дало бы весь растр целиком.
const COUNT_INK = `(() => {
    const c = document.getElementById('pane-1');
    const d = c.getContext('2d').getImageData(0, 0, 560, 256).data;
    let n = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i] < 0x80) ++n;
    return n;
})()`;

const TABS = "Array.from(document.querySelectorAll('.tab')).map(t =>" +
             " t.dataset.pane + (t.hidden ? ':скрыта' :" +
             " (t.classList.contains('on') ? ':ВЫБРАНА' : ':видна')) +" +
             " (t.classList.contains('fresh') ? '+точка' : '')).join(' ')";

// ---------------------------------------------------------------------------

(async () => {
    const list = await (await fetch(CDP + '/json/list')).json();
    const page = list.find(t => t.type === 'page');
    if (!page) throw new Error('в Chrome нет ни одной вкладки: ' + CDP);

    ws = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise(r => ws.addEventListener('open', r));
    ws.addEventListener('message', ev => {
        const m = JSON.parse(ev.data);
        if (m.id && waiting.has(m.id)) {
            const w = waiting.get(m.id);
            waiting.delete(m.id);
            m.error ? w.rej(new Error(JSON.stringify(m.error))) : w.res(m.result);
            return;
        }
        if (m.method === 'Runtime.exceptionThrown')
            problems.push('исключение: ' + (m.params.exceptionDetails.text || ''));
        if (m.method === 'Runtime.consoleAPICalled' &&
            (m.params.type === 'error' || m.params.type === 'warning'))
            problems.push(m.params.type + ': ' +
                m.params.args.map(a => a.value).join(' '));
    });

    await send('Page.enable');
    await send('Runtime.enable');
    await send('Page.navigate', { url: URL });

    console.log('страница и модуль');
    let version = '';
    for (let i = 0; i < 100 && !version; ++i) {
        version = await evaluate("document.getElementById('version').textContent");
        if (!version) await sleep(200);
    }
    check('модуль поднялся', !!version, version);
    check('заставка убралась',
          await evaluate("document.getElementById('boot').hidden") === true);
    check('невыбранных вкладок не видно', await evaluate(
        "Array.from(document.querySelectorAll('#pane-1,#pane-2'))" +
        ".every(e => e.offsetParent === null)"));

    let scr = await evaluate(READ_SCREEN);
    check('на экране заставка машины', scr[0] === '##### ##### ## ########',
          '«' + scr[0] + '»');
    await shot('01-boot.png');

    console.log('');
    console.log('набор с клавиатуры и счёт');
    // Набирается строчными нарочно: приводить к прописным обязан хост, на
    // нажатии, как это делают все три настольных (`x11_host.cpp:613` и
    // соседи). Ошибка тут незаметна — экран высветит прописные в любом
    // случае, — а в текст программы знак ляжет строчным.
    await type('10 print "привет, искра"'); await enter();
    await type('20 for i=1 to 3');          await enter();
    await type('30 print i,i*i,sqr(i)');    await enter();
    await type('40 next i');                await enter();
    await type('run');                      await enter();
    await sleep(600);
    scr = await evaluate(READ_SCREEN);
    const body = scr.filter(l => l.length).length;
    check('кириллица дошла до машины и программа сосчиталась', body >= 9,
          body + ' непустых строк');
    // «ПРИВЕТ, ИСКРА» — семь занятых знакомест, пробел, ещё пять. Набрано
    // было строчными: не приведи их хост, строки 10 в программе не было бы
    // вовсе, а на экране стояла бы жалоба транслятора.
    check('строчные буквы приведены к прописным', scr[6] === '####### #####',
          '«' + scr[6] + '»');
    // Зонная печать: три значения расходятся по зонам. Зона — 16 позиций
    // («каждая строка условно делится на 5 зон», `Interp::ZONE`), значит
    // между занятыми знакоместами ровно пятнадцать пустых.
    check('печать разошлась по зонам', /^ # {15}# {15}#/.test(scr[7]),
          '«' + scr[7].slice(0, 44) + '»');

    console.log('');
    console.log('слово Бейсика с обычной клавиатуры');
    await press('й', 'KeyQ', 'й', 1);            // 1 — Alt
    // Ждать надо с запасом: `Page.captureScreenshot` останавливает страницу
    // на добрую долю секунды, и очередь нажатий разбирается уже после него.
    // По той же причине снимок делается после проверки, а не до неё.
    await sleep(500);
    scr = await evaluate(READ_SCREEN);
    const tail = scr.filter(l => l.length).slice(-3);
    const last = tail[tail.length - 1];
    // «:FOR» — четыре занятых знакоместа, а пятым бывает курсор: он
    // подстрочная черта и мигает дважды в секунду, так что попадается то
    // так, то этак.
    check('Alt+Й вводит FOR', /^#{4,5}$/.test(last), JSON.stringify(tail));
    await shot('02-run.png');
    for (let i = 0; i < 3; ++i) await press('Backspace', 'Backspace');

    console.log('');
    console.log('меню');
    const progs = await evaluate(
        "Array.from(document.querySelectorAll('#m-programs li div')).map(e=>e.textContent)");
    const disks = await evaluate(
        "Array.from(document.querySelectorAll('#m-disk-0 li div')).map(e=>e.textContent)");
    const named = await (await fetch(URL + 'bundle.json')).json();
    check('список программ пришёл из bundle.json',
          JSON.stringify(progs) === JSON.stringify(named.programs.map(p => p.name)),
          progs.join(', '));
    check('список дисков пришёл из bundle.json',
          JSON.stringify(disks) === JSON.stringify(named.disks.map(d => d.name)),
          disks.join(', '));

    console.log('');
    console.log('лист графопостроителя');
    await pick([SYS, PROGS, '#m-programs li:nth-child(3) div']);
    await sleep(600);
    await type('RUN'); await enter();
    await sleep(3500);
    check('вкладка листа появилась и выбралась',
          /1:ВЫБРАНА/.test(await evaluate(TABS)), await evaluate(TABS));
    const ink = await evaluate(COUNT_INK);
    check('на листе что-то начерчено', ink > 2000, ink + ' точек пера');
    check('кнопка «Сохранить лист» показана',
          await evaluate("document.getElementById('save-plot').hidden") === false);
    await shot('03-plotter.png');

    console.log('');
    console.log('лента АЦПУ');
    await pick([SYS, PROGS, '#m-programs li:nth-child(4) div']);
    await sleep(600);
    await type('RUN'); await enter();
    await sleep(2500);
    check('вкладка ленты появилась и выбралась',
          /2:ВЫБРАНА/.test(await evaluate(TABS)), await evaluate(TABS));
    check('лист убрался при перезагрузке',
          /1:скрыта/.test(await evaluate(TABS)));
    const tape = await evaluate("document.getElementById('pane-2').textContent");
    check('ведомость напечаталась целиком',
          /В Е Д О М О С Т Ь/.test(tape) && /ИТОГО +648\.70/.test(tape),
          tape.split('\n').length + ' строк');
    await shot('04-tape.png');

    console.log('');
    console.log('вывод в невыбранную вкладку');
    await pick([SYS, PROGS, '#m-programs li:nth-child(2) div']);
    await sleep(600);
    await type('RUN'); await enter();
    await sleep(3500);
    // Стоим на ленте, а рисует программа на экран: без метки это прошло бы
    // незамеченным — на десктопе все окна на виду сразу, а тут видно одно.
    check('на вкладке экрана появилась метка',
          /0:видна\+точка/.test(await evaluate(TABS)), await evaluate(TABS));
    await click('.tab[data-pane="0"]');
    await sleep(300);
    check('метка снялась при переходе',
          !/точка/.test(await evaluate(TABS)), await evaluate(TABS));
    await shot('05-graph.png');

    console.log('');
    console.log('дискета с сервера');
    await pick([DISKS, DRV_F, SRV_F, '#m-disk-0 li:nth-child(1) div']);
    await sleep(1500);
    check('дисковод F занят', /Статистика/.test(
        await evaluate("document.getElementById('slot-0').textContent")),
        await evaluate("document.getElementById('slot-0').textContent"));
    await click('.tab[data-pane="0"]');
    await type('LIST DC'); await enter();
    await sleep(2500);
    scr = await evaluate(READ_SCREEN);
    // `LIST DC` печатает указатель каталога по разд. 5.1 книги: тип диска,
    // числа секторов, потом таблица. Знаков мы не читаем, но строк с данными
    // на настоящем образе должно быть много.
    check('каталог прочитался', scr.filter(l => l.length).length >= 15,
          scr.filter(l => l.length).length + ' строк на экране');
    await shot('06-listdc.png');

    console.log('');
    console.log('экранная клавиатура');
    check('клавиш столько же, сколько на рис. 2.2',
          await evaluate("document.querySelectorAll('.keyboard .key').length") === 110);
    await pick([SYS, RESET]);
    await sleep(600);
    const kw = await evaluate(
        "(() => { const k = Array.from(document.querySelectorAll('.keyboard .k-word'))" +
        ".find(e => e.textContent === 'FOR');" +
        "  if (!k) return null; const r = k.getBoundingClientRect();" +
        "  return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; })()");
    check('слово FOR есть на клавише Й', !!kw);
    if (kw) {
        await move(kw.x, kw.y);
        for (const type of ['mousePressed', 'mouseReleased'])
            await send('Input.dispatchMouseEvent',
                { type, x: kw.x, y: kw.y, button: 'left', clickCount: 1 });
        await sleep(400);
        scr = await evaluate(READ_SCREEN);
        const line = scr.filter(l => l.length).pop();
        check('щелчок по слову вводит FOR', /^#{4,5}$/.test(line),
              '«' + line + '» (ожидалось «:FOR», с курсором или без)');
    }
    await shot('07-keyboard.png');

    console.log('');
    check('браузер ни на что не жаловался', problems.length === 0,
          problems.join(' | '));

    console.log('');
    console.log(failures ? failures + ' отказ(ов)' : 'всё сошлось');
    console.log('снимки: ' + SHOTS);
    process.exit(failures ? 1 : 0);
})().catch(e => { console.error('СБОЙ: ' + e.message); process.exit(2); });
