// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: меню страницы — списки с сервера и закрытие после выбора

'use strict';

var IskraMenu = (function () {

// Раскрывает меню CSS, по наведению. Единственное, чего CSS не умеет, —
// закрыться после выбора: пока указатель стоит на пункте, наведение никуда
// не делось. Поэтому выбравший пункт сам гасит меню классом (`shut`), а
// снимается класс, когда указатель ушёл.
//
// Гасить по всякому щелчку внутри меню нельзя: щелчок по «Система» ничего
// не выбирает, и меню закрылось бы под рукой.
function init(root) {
    root.addEventListener('mouseleave', function () {
        root.classList.remove('shut');
    });
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

return { init: init, fill: fill, shut: shut };

})();
