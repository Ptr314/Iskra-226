// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: клавиатура — правка строки, RECALL, останов и продолжение счёта

#include <string>
#include <vector>

#include "check.h"
#include "core/console.h"
#include "core/keys.h"
#include "core/koi8.h"
#include "core/tokenize.h"
#include "host_common/keywords.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

std::string trim_tail(const std::string & dump)
{
    std::string::size_type e = dump.size();
    while (e >= 2 && dump[e - 1] == '\n' && dump[e - 2] == '\n') --e;
    return dump.substr(0, e);
}

// Очередь нажатий собирается по знакам: текст в UTF-8 переводится в КОИ-8,
// клавиши добавляются кодами.
struct Keys
{
    std::string s;

    Keys & text(const char * utf8)
    {
        std::string koi8;
        utf8_to_koi8(utf8, koi8);
        s += koi8;
        return *this;
    }
    Keys & key(uint8_t code) { s += static_cast<char>(code); return *this; }
    Keys & key(uint8_t code, unsigned times)
    {
        for (unsigned i = 0; i < times; ++i) key(code);
        return *this;
    }
    Keys & cr() { return key(KEY_CR); }
};

std::string session(const Keys & keys)
{
    HeadlessHost host;
    host.feed_keys(reinterpret_cast<const uint8_t *>(keys.s.data()),
                   static_cast<unsigned>(keys.s.size()));
    ProgramImage img;
    Console con(img, host);
    std::string error;
    CHECK(con.run(error));
    CHECK_STR(error, "");
    return trim_tail(host.dump());
}

// Сколько раз слово встретилось на экране.
unsigned count_of(const std::string & hay, const char * utf8)
{
    unsigned n = 0;
    for (std::string::size_type p = hay.find(utf8); p != std::string::npos;
         p = hay.find(utf8, p + 1))
        ++n;
    return n;
}

// Последняя непустая строка экрана — обычно на неё и смотрим.
std::string last_line(const std::string & screen)
{
    std::string::size_type e = screen.size();
    while (e && screen[e - 1] == '\n') --e;
    const std::string::size_type b = screen.find_last_of('\n', e ? e - 1 : 0);
    return screen.substr(b == std::string::npos ? 0 : b + 1,
                         e - (b == std::string::npos ? 0 : b + 1));
}

// «При однократном нажатии клавиши BACKSPACE курсор передвигается на одну
// позицию влево, а находившийся в этой позиции символ стирается» (разд. 3.2).
void test_backspace()
{
    Keys k;
    k.text("PRINT 12345").key(KEY_BACKSPACE, 3).text("9").cr();
    const std::string scr = session(k);
    // Осталось `PRINT 129`, и оно исполнено.
    CHECK(scr.find("\n 129\n") != std::string::npos);
}

// Пример из разд. 3.3 книги слово в слово: набрана строка непосредственного
// счёта `PRINT 20.12+2.87`, ошибка замечена до ввода, EDIT переводит в режим
// правки, курсор подводится к нулю, DELETE его убирает, курсор возвращается
// в конец, набор продолжается, CR/LF исполняет.
void test_book_edit_session()
{
    Keys k;
    k.text("PRINT 20.12+2.87")
     .key(KEY_EDIT)
     .key(KEY_LEFT5).key(KEY_LEFT5).key(KEY_RIGHT)   // курсор под нулём
     .key(KEY_DELETE)
     .key(KEY_RIGHT5).key(KEY_RIGHT5)                // и обратно в конец
     .text("+5.5")
     .cr();
    const std::string scr = session(k);
    // Приглашение в режиме правки — звёздочка (разд. 3.3).
    CHECK(scr.find("*PRINT 2.12+2.87+5.5") != std::string::npos);
    CHECK(scr.find("\n 10.49\n") != std::string::npos);
}

// «Клавиша LINE ERASE … стирает всю редактируемую строку. При этом символ *
// в начале строки заменяется на :» (разд. 3.3).
void test_line_erase()
{
    Keys k;
    k.text("PRINT 1").key(KEY_EDIT).key(KEY_LINE_ERASE).text("PRINT 7").cr();
    const std::string scr = session(k);
    CHECK(scr.find(":PRINT 7") != std::string::npos);
    CHECK(scr.find("*") == std::string::npos);
    CHECK(scr.find("\n 7\n") != std::string::npos);
}

// INSERT сдвигает правую часть строки вправо, ERASE стирает её от курсора.
void test_insert_erase()
{
    Keys k;
    k.text("PRINT 1+2").key(KEY_EDIT)
     .key(KEY_LEFT).key(KEY_LEFT)                 // курсор под `+`
     .key(KEY_INSERT).text("0")                   // `PRINT 10+2`
     .cr();
    CHECK(session(k).find("\n 12\n") != std::string::npos);

    Keys e;
    e.text("PRINT 5+444").key(KEY_EDIT)
     .key(KEY_LEFT).key(KEY_LEFT).key(KEY_LEFT)   // курсор под первой четвёркой
     .key(KEY_ERASE)                              // `PRINT 5+`
     .text("1").cr();
    CHECK(session(e).find("\n 6\n") != std::string::npos);
}

// «Необходимо набрать номер строки … и нажать клавишу EDIT … нажатием
// клавиши RECALL вызовем на экран строку» (разд. 3.3).
void test_recall_line()
{
    Keys k;
    k.text("10 PRINT \"ОДИН\"").cr()
     .text("10").key(KEY_EDIT).key(KEY_RECALL)
     .key(KEY_ERASE)                              // курсор в конце — ничего
     .cr()
     .text("LIST").cr();
    const std::string scr = session(k);
    CHECK(scr.find("*10 PRINT \"ОДИН\"") != std::string::npos);
}

// «В конце строки набрать : и номер следующей присоединяемой строки. После
// нажатия клавиши RECALL копия текста этой второй строки присоединяется к
// тексту первой строки» (разд. 3.3), пример оттуда же.
void test_recall_join()
{
    Keys k;
    k.text("1 INPUT A").cr()
     .text("2 PRINT A*0.03").cr()
     .text("1").key(KEY_EDIT).key(KEY_RECALL)
     .text(":2").key(KEY_RECALL)
     .cr()
     .text("LIST").cr();
    const std::string scr = session(k);
    // Присоединённая строка входит без своего номера.
    // Детокенизатор пишет число так же, как машина: без ведущего нуля.
    CHECK(scr.find("1 INPUT A: PRINT A*.03") != std::string::npos);
    // «Сама присоединённая строка останется в памяти без изменений».
    CHECK(scr.find("2 PRINT A*.03") != std::string::npos);
}

// «Последняя введённая строка … может быть вновь вызвана на экран для
// редактирования и повторного ввода. Для этого необходимо нажать клавиши
// EDIT и RECALL» (разд. 3.3).
void test_recall_last()
{
    Keys k;
    k.text("PRINT 6*7").cr()
     .key(KEY_EDIT).key(KEY_RECALL).cr();
    const std::string scr = session(k);
    // Ответ 42 напечатан дважды: строка исполнена и вызвана заново.
    std::string::size_type a = scr.find("\n 42\n");
    CHECK(a != std::string::npos);
    CHECK(scr.find("\n 42\n", a + 1) != std::string::npos);
}

// «Автоматически набирается номер строки на 10 больше максимального номера
// строки программы, содержащейся в памяти» (разд. 3.2).
void test_stmt_number()
{
    HeadlessHost host;
    Keys k;
    k.text("10 REM ПЕРВАЯ").cr();
    host.feed_keys(reinterpret_cast<const uint8_t *>(k.s.data()),
                   static_cast<unsigned>(k.s.size()));

    ProgramImage img;
    Console con(img, host);
    std::string error;
    CHECK(con.run(error));

    // Ящик клавиш управления отдельный от очереди нажатий, и подавать его
    // надо тогда, когда до него дойдёт дело.
    host.feed_control_key(CK_STMT_NUMBER);
    CHECK(con.run(error));
    CHECK_STR(last_line(trim_tail(host.dump())), ":20");
}

// Клавиша HALT/STEP останавливает счёт, CONTINUE продолжает его со
// следующего оператора (разд. 11.1).
void test_halt_continue()
{
    HeadlessHost host;
    Keys k;
    k.text("10 PRINT \"РАЗ\"").cr()
     .text("20 STOP").cr()
     .text("30 PRINT \"ДВА\"").cr()
     .text("RUN").cr();
    host.feed_keys(reinterpret_cast<const uint8_t *>(k.s.data()),
                   static_cast<unsigned>(k.s.size()));

    ProgramImage img;
    Console con(img, host);
    std::string error;
    CHECK(con.run(error));

    // Программа стоит на строке 30 и продолжаема.
    CHECK(con.interp().can_continue());
    CHECK(con.interp().stop_reason() == Interp::SR_STOP);
    CHECK(con.interp().current_line() == 20);
    // «ДВА» пока только в наборанном тексте строки 30, напечатать его
    // ещё не успели.
    CHECK_STR(test::str(count_of(trim_tail(host.dump()), "ДВА")), "1");

    // CONTINUE доводит её до конца. Диалог при этом начинается заново и
    // чистит экран, поэтому напечатанное слово на нём одно — своё.
    host.feed_control_key(CK_CONTINUE);
    CHECK(con.run(error));
    CHECK(!con.interp().can_continue());
    CHECK_STR(test::str(count_of(trim_tail(host.dump()), "ДВА")), "1");
}

// Клавиша HALT прерывает счёт посреди программы: исполнитель спрашивает
// ящик по ходу прогона, а не только на вводе.
void test_halt_key()
{
    HeadlessHost host;
    std::string koi8;
    utf8_to_koi8("10 FOR I=1TO2000\n"
                 "20 NEXT I\n"
                 "30 PRINT \"КОНЕЦ\"\n", koi8);
    ProgramImage img;
    NameTable names;
    std::string error;
    CHECK(tokenize(koi8, img, names, error));

    Interp interp(img, host);
    host.feed_control_key(CK_HALT);
    CHECK(interp.run(error));
    CHECK(interp.stop_reason() == Interp::SR_HALT);
    CHECK(interp.can_continue());
    CHECK(trim_tail(host.dump()).find("КОНЕЦ") == std::string::npos);

    // И продолжается с того же места.
    CHECK(interp.resume(error));
    CHECK(trim_tail(host.dump()).find("КОНЕЦ") != std::string::npos);
}

// Правка текста программы делает продолжение невозможным: «изменён текст
// программы» — первый из перечисленных случаев (разд. 11.1).
void test_edit_forgets_continue()
{
    HeadlessHost host;
    Keys k;
    k.text("10 STOP").cr().text("20 PRINT \"ДВА\"").cr().text("RUN").cr();
    host.feed_keys(reinterpret_cast<const uint8_t *>(k.s.data()),
                   static_cast<unsigned>(k.s.size()));
    ProgramImage img;
    Console con(img, host);
    std::string error;
    CHECK(con.run(error));
    CHECK(con.interp().can_continue());

    Keys e;
    e.text("30 REM").cr();
    host.feed_keys(reinterpret_cast<const uint8_t *>(e.s.data()),
                   static_cast<unsigned>(e.s.size()));
    CHECK(con.run(error));
    CHECK(!con.interp().can_continue());
}

// Клавиша спецфункций уводит KEYIN на вторую строку, обычная — на первую;
// код у клавиши спецфункций это её номер (`EDITOR` 2700).
void test_special_key_code()
{
    HeadlessHost host;
    Keys k;
    k.text("10 KEYIN A¤,20,30").cr()
     .text("20 PRINT \"ОБЫЧНАЯ\";:END").cr()
     .text("30 PRINT \"СПЕЦ\";VAL(A¤);:END").cr()
     .text("RUN").cr();
    host.feed_keys(reinterpret_cast<const uint8_t *>(k.s.data()),
                   static_cast<unsigned>(k.s.size()));
    // Нажатие ложится в очередь за `RUN`: до него доберётся уже `KEYIN`.
    host.feed_special_key(29);

    ProgramImage img;
    Console con(img, host);
    std::string error;
    CHECK(con.run(error));
    const std::string scr = trim_tail(host.dump());
    CHECK(scr.find("СПЕЦ 29") != std::string::npos);
}

// «Нажатие определённой клавиши специальных функций вызовет печать
// текстовой константы, записанной в соответствующем операторе DEFFN'»
// (разд. 10.6). Это не переход к подпрограмме: текст просто становится
// частью набираемой строки.
void test_sf_text()
{
    HeadlessHost host;
    Keys a;
    a.text("10 DEFFN'5\"ПОКАЗАТЕЛЬ\"").cr().text("PRINT \"");
    host.feed_keys(reinterpret_cast<const uint8_t *>(a.s.data()),
                   static_cast<unsigned>(a.s.size()));
    host.feed_special_key(5);
    Keys b;
    b.text("\"").cr();
    host.feed_keys(reinterpret_cast<const uint8_t *>(b.s.data()),
                   static_cast<unsigned>(b.s.size()));

    ProgramImage img;
    Console con(img, host);
    std::string error;
    CHECK(con.run(error));
    // Слово и в наборе, и в напечатанном.
    CHECK_STR(test::str(count_of(trim_tail(host.dump()), "ПОКАЗАТЕЛЬ")), "3");
}

// «Клавиши специальных функций могут быть использованы для перехода к
// помеченным подпрограммам без параметров … только после того, как …
// программа начала выполняться» (разд. 10.5).
void test_sf_call()
{
    HeadlessHost host;
    Keys a;
    a.text("10 STOP").cr()
     .text("20 DEFFN'3").cr()
     .text("30 PRINT \"ВЫЗВАНА\"").cr()
     .text("40 RETURN").cr()
     .text("RUN").cr();
    host.feed_keys(reinterpret_cast<const uint8_t *>(a.s.data()),
                   static_cast<unsigned>(a.s.size()));
    host.feed_special_key(3);

    ProgramImage img;
    Console con(img, host);
    std::string error;
    CHECK(con.run(error));
    CHECK(con.interp().sf_armed());
    CHECK_STR(test::str(count_of(trim_tail(host.dump()), "ВЫЗВАНА")), "2");
}

// До запуска программы клавиша к подпрограмме не ведёт, и набранное от
// нажатия не теряется.
void test_sf_not_armed()
{
    HeadlessHost host;
    Keys a;
    a.text("10 DEFFN'3").cr()
     .text("20 PRINT \"ВЫЗВАНА\"").cr()
     .text("PRINT 5");
    host.feed_keys(reinterpret_cast<const uint8_t *>(a.s.data()),
                   static_cast<unsigned>(a.s.size()));
    host.feed_special_key(3);
    Keys b;
    b.text("+1").cr();
    host.feed_keys(reinterpret_cast<const uint8_t *>(b.s.data()),
                   static_cast<unsigned>(b.s.size()));

    ProgramImage img;
    Console con(img, host);
    std::string error;
    CHECK(con.run(error));
    CHECK(!con.interp().sf_armed());
    const std::string scr = trim_tail(host.dump());
    CHECK(scr.find("\n 6\n") != std::string::npos);
    CHECK_STR(test::str(count_of(scr, "ВЫЗВАНА")), "1");
}

// Курсор вверх и вниз ходит по строкам экрана, то есть по 80 позиций.
void test_cursor_rows()
{
    Keys k;
    // Длинная строка: 80 пробелов внутри кавычек, дальше хвост.
    k.text("PRINT \"");
    for (unsigned i = 0; i < 78; ++i) k.text("A");
    k.text("\";1")
     .key(KEY_EDIT)
     .key(KEY_UP)        // на строку экрана выше
     .key(KEY_DOWN)      // и обратно
     .key(KEY_BACKSPACE) // курсор влево, знак цел
     .text("2")          // поверх единицы
     .cr();
    const std::string scr = session(k);
    CHECK(scr.find("A\";2") != std::string::npos);
}

// Слова Бейсика верхнего регистра: ключ — знак, и русская буква с латинской
// дают одно слово (`docs/format.md`, разд. 12).
void test_keyword_table()
{
    std::string koi8;
    utf8_to_koi8("Й", koi8);
    const char * a = keyword_for_char(koi8_upper(static_cast<uint8_t>(koi8[0])));
    CHECK(a != 0);
    CHECK_STR(a, "FOR");
    CHECK_STR(keyword_for_char('J'), "FOR");
    CHECK_STR(keyword_for_char('S'), "SELECT");
    CHECK_STR(keyword_for_char('8'), "DEFFN");
    CHECK_STR(keyword_for_char('9'), "DEFFN'");
    CHECK_STR(keyword_for_pad(PAD_ADD), "SQR(");
    CHECK_STR(keyword_for_char('Q'), "RND(");
    // Клавиши, надпись которой с рисунка не прочитана, здесь нет.
    CHECK(keyword_for_char('D') == 0);
}

} // namespace

int main()
{
    test_backspace();
    test_book_edit_session();
    test_line_erase();
    test_insert_erase();
    test_recall_line();
    test_recall_join();
    test_recall_last();
    test_stmt_number();
    test_halt_continue();
    test_halt_key();
    test_edit_forgets_continue();
    test_special_key_code();
    test_sf_text();
    test_sf_call();
    test_sf_not_armed();
    test_cursor_rows();
    test_keyword_table();
    return test::summary("test_keyboard");
}
