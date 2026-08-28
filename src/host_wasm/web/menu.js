// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: меню страницы — списки с сервера, сторона раскрытия, закрытие после выбора

'use strict';

var IskraMenu = (function () {

// Раскрывает меню CSS, по наведению. Скрипту остаются две вещи, которых CSS
// не умеет вовсе.
//
// Первая — закрыться после выбора: пока указатель стоит на пункте,
// наведение никуда не делось. Поэтому выбравший пункт сам гасит меню
// классом (`shut`), а снимается класс, когда указатель ушёл. Гасить по
// всякому щелчку внутри меню нельзя: щелчок по «Система» ничего не
// выбирает, и меню закрылось бы под рукой.
//
// Вторая — сторона раскрытия. Уровни уходят вбок, вправо, и на узком окне
// последний уехал бы за край; измерить ширину раскрытого подменю CSS не
// может.
function init(root) {
    root.addEventListener('mouseleave', function () {
        root.classList.remove('shut');
    });

    // Невидимое подменю ширину уже имеет: `visibility: hidden` в раскладке
    // участвует, — так что мерить можно до показа.
    //
    // Считается вся цепочка от наведённого пункта наружу: уровни
    // разворачиваются сверху вниз, и уехал ли внук, зависит от того, куда
    // ушёл сын.
    root.addEventListener('mouseover', function (e) {
        var li = e.target.closest('li');
        var chain = [];
        while (li) {
            chain.unshift(li);
            li = li.parentNode && li.parentNode.closest('li');
        }
        chain.forEach(function (item) { side(item.querySelector(':scope > ul')); });
    });
}

// Куда раскрыть это подменю. Считать надо от правого края окна, а не от
// страницы: страница у́же окна и стоит по центру.
function side(ul) {
    if (!ul) return;
    ul.classList.remove('left');
    if (ul.getBoundingClientRect().right >
        document.documentElement.clientWidth - 2)
        ul.classList.add('left');
}

function shut(el) {
    var m = el.closest('.menu');
    if (m) m.classList.add('shut');
}

// Заполнить подменю списком из описания пакета. Пустой список остаётся
// пустым и так и написан: молча показывать пустоту нельзя — непонятно,
// список кончился или не доехал.
function fill(ul, items, label, pick) {
    ul.innerHTML = '';
    if (!items || !items.length) {
        var e = document.createElement('li');
        e.className = 'empty';
        e.innerHTML = '<div>список пуст</div>';
        ul.appendChild(e);
        return;
    }
    items.forEach(function (item) {
        var li = document.createElement('li');
        var d = document.createElement('div');
        d.textContent = label(item);
        li.appendChild(d);
        li.addEventListener('click', function (e) {
            e.stopPropagation();
            shut(ul);
            pick(item);
        });
        ul.appendChild(li);
    });
}

return { init: init, fill: fill, shut: shut, side: side };

})();
