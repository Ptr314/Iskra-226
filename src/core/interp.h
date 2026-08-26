// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: исполнение оттранслированной программы прямо из потока токенов

#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/byte_source.h"
#include "core/devtable.h"
#include "core/errors.h"
#include "core/eval.h"
#include "core/gbuffer.h"
#include "core/host.h"
#include "core/program.h"
#include "core/value.h"
#include "core/vars.h"

namespace iskra {

// ФАУ устройства пишут двумя шестнадцатеричными цифрами.
std::string hex2_str(unsigned v);

// Исполнитель работает с тем же образом программы, что лежит в памяти
// машины: поток строк и таблицы переменных. Промежуточного представления
// нет — «интерпретатор осуществляет пошаговый перевод операторов»
// (руководство, разд. 1.2), и операнды читаются из байтов при каждом
// исполнении (docs/DECISIONS.md, разд. 12).
class Interp
{
public:
    // Образ изменяемый: LOAD DC заменяет часть программы прямо во время
    // исполнения — «загружается программный сегмент с указанным именем»
    // (руководство, разд. 19.1).
    Interp(ProgramImage & img, Host & host);

    // Ограничение на число выполненных операторов: страховка от зацикливания
    // в автотестах. Ноль снимает ограничение.
    void set_max_steps(unsigned long n) { max_steps_ = n; }

    // Пропускать машинозависимые операторы вместо остановки. Их исполнение
    // требует эмуляции машины на уровне процессора и аппаратуры, а этого
    // здесь нет и не планируется (docs/DECISIONS.md, разд. 1). По умолчанию
    // такой оператор останавливает программу — молчаливый пропуск сделал бы
    // прогон неправдоподобным и незаметно для человека.
    void set_skip_machine(bool on) { skip_machine_ = on; }

    // Машинозависим ли глагол: `06 25 ASMB` — ассемблер, `40 $GIO` —
    // микропрограмма устройству.
    static bool machine_verb(unsigned verb)
    {
        return verb == 0x0625 || verb == 0x40;
    }

    // Исполняет программу с наименьшей строки. «При запуске программ с
    // помощью оператора RUN без указания номера строки числовым переменным
    // автоматически присваивается значение 0, а символьным — пробел»
    // (руководство, разд. 4.1).
    bool run(std::string & error);

    // RUN с номером строки: «переменные сохраняют значения, присвоенные им
    // ранее» (там же, пример 4.2).
    bool run_from(unsigned line_number, std::string & error);

    // Исполнить строку, которой в программе нет, — режим непосредственного
    // счёта. Если она передаст управление, исполнение продолжится в
    // программе, как на машине.
    bool execute(const uint8_t * body, unsigned len, std::string & error);

    // Почему счёт остановился. `STOP` и клавиша HALT оставляют программу
    // продолжаемой, `RESET` — нет.
    enum StopReason { SR_NONE = 0, SR_STOP, SR_HALT, SR_RESET };
    StopReason stop_reason() const { return stop_reason_; }

    // «Для продолжения выполнения программы необходимо нажать клавишу
    // CONTINUE, после чего выполнение программы продолжится со следующего
    // за оператором STOP оператора» (руководство, разд. 11.1).
    bool can_continue() const { return can_continue_; }

    // Продолжить нельзя после правки текста программы, RESET, CLEAR V и
    // CLEAR N (там же). Об этом говорит диалог — он и зовёт.
    void forget_continue() { can_continue_ = false; }

    // Продолжить счёт с оператора, на котором он стоит.
    bool resume(std::string & error);

    // Один оператор — это вторая половина клавиши HALT/STEP.
    bool step(std::string & error);

    // Номер строки, на которой стоит счёт; ноль — вне программы.
    unsigned current_line() const;

    // --- Клавиши специальных функций, разд. 10.5 и 10.6 ------------------
    // «Нажатие определённой клавиши специальных функций вызовет печать
    // текстовой константы, записанной в соответствующем операторе DEFFN'»
    // (разд. 10.6). false — определения с текстом у этой метки нет.
    bool sf_text(unsigned label, std::string & out);

    // Переход к помеченной подпрограмме нажатием клавиши. «Использовать
    // клавиши специальных функций для перехода к подпрограммам можно только
    // после того, как записанная в памяти машины программа начала
    // выполняться … до тех пор, пока не будет изменён текст программы»
    // (разд. 10.5).
    bool sf_call(unsigned label, std::string & error);
    bool sf_armed() const { return sf_armed_; }
    void forget_sf() { sf_armed_ = false; }

    // Состояние между командами диалога: переменные, таблица устройств,
    // циклы и возвраты живут в исполнителе и переживают правку текста.
    VarStore & vars() { return store_; }
    void clear_all();

    // Таблицу устройств стирает только CLEAR: «выбор устройства справедлив до
    // тех пор, пока не будет выполнен следующий оператор SELECT … или
    // оператор CLEAR» (руководство, разд. 6.1). RUN её не трогает.
    void clear_devices() { dev_ = DeviceTable(); }

    // Код последней ошибки машины, если она была машинной. У ограничения
    // эмулятора кода нет — и выдумывать его нельзя.
    const std::string & error_code() const { return err_code_; }

    // Значение переменной после прогона — для проверок.
    bool variable(unsigned index, Number & out) const;

    // Таблица устройств — её состояние программа видит сама (LIST%, LIMITS).
    const DeviceTable & devices() const { return dev_; }

private:
    // Подстановка функций пользователя. Отдельным объектом, а не самим
    // исполнителем: интерфейс вычислителя незачем выносить в его лицо.
    struct Functions : public FnResolver {
        Functions() : owner(0) {}
        bool call_fn(unsigned name, const Value & arg, Value & out,
                     std::string & err);
        Interp * owner;
    };
    friend struct Functions;

    // Один оператор: источник байт и вычислитель над ним. Живёт ровно на
    // время исполнения оператора.
    struct Stream {
        Stream(const uint8_t * p, unsigned n, const std::vector<VarInfo> * vars,
               VarStore & store, FnResolver * fn)
            : src(p, n, vars), ev(src, store) { ev.set_functions(fn); }
        ByteSource src;
        Evaluator ev;
    };

    struct Frame {
        unsigned var;
        Number limit;
        Number step;
        unsigned line;      // куда возвращает NEXT
        unsigned off;       // смещение оператора в теле строки
    };

    // Приставка дисковых операторов, уже разрешённая в номера.
    struct Disk {
        Disk() : row(0), drive(0) {}
        unsigned row;
        unsigned drive;
    };

    bool exec(unsigned verb, const uint8_t * ops, unsigned len);
    // Сам разбор глагола. Отделён от exec(), чтобы после него можно было
    // разобраться с кодом ошибки: у математических он есть, и ON ERROR их
    // ловит (руководство, пример 11.11).
    bool dispatch(unsigned verb, Stream & st, const uint8_t * ops, unsigned len);

    bool do_print(Stream & st);
    bool do_printusing(Stream & st);
    bool do_hexprint(Stream & st);
    bool do_read(Stream & st);
    bool do_restore(Stream & st);
    bool do_select(Stream & st);
    bool do_open(Stream & st, bool with_device);
    bool do_dsave_open(Stream & st);
    bool do_dsave(Stream & st);
    bool do_dclose(Stream & st);
    bool do_if_end(Stream & st);
    // Режим абсолютной адресации (разд. 18.9): приставка и номер
    // начального сектора, дальше блок в 256 байт (BA) либо обычная
    // логическая запись (DA).
    bool abs_prefix(Stream & st, Disk & d, unsigned & sector,
                    bool & has_target, Evaluator::Target & target);
    bool store_next(Stream & st, bool has_target,
                    const Evaluator::Target & target, unsigned next);
    bool do_block(Stream & st, bool load);
    // Блочный обмен с устройством по ФАУ: DATA SAVE/LOAD BT.
    bool bt_prefix(Stream & st, unsigned & addr);
    bool do_block_transfer(Stream & st, bool load);
    bool do_abs_record(Stream & st, bool load);
    bool do_verify(Stream & st);
    // Копирование диска на диск (руководство, разд. 18.9.6).
    bool do_copy(Stream & st);
    bool do_dload(Stream & st);
    // Значения списка `DATA SAVE DC`: массив целиком разворачивается в свои
    // элементы — «массивы записываются строка за строкой» (разд. 18.3).
    bool save_values(Stream & st, std::vector<Value> & vals);
    bool do_dskip(Stream & st, bool backwards);
    bool do_limits(Stream & st);
    bool do_onerr(Stream & st);
    bool do_scratch(Stream & st);
    bool do_scratch_disk(Stream & st);
    bool do_dim(Stream & st, unsigned len, const uint8_t * ops, bool common);
    // `COM CLEAR` — граница общих переменных (руководство, разд. 19.3).
    bool do_com_clear(Stream & st);
    bool do_redim(Stream & st);
    // MAT READ и MAT INPUT: подкоды 06 03 и 06 04 (разд. 12.2.2 и 12.2.3).
    bool do_mat_read(Stream & st, bool from_keyboard);
    // MAT PRINT: подкод 06 05 (руководство, разд. 12.2.1).
    bool do_mat_print(Stream & st);
    bool do_mat(Stream & st);
    bool do_mat_copy(Stream & st);
    bool do_mat_search(Stream & st);
    // Замена и перекодировка символьных данных (разд. 15.3).
    bool do_replace(Stream & st);
    bool do_tran(Stream & st);

    // Система координат буфера (docs/format.md, разд. 5, «WINDOW, FRAME и
    // ORIGIN»). `WINDOW` — область на устройстве в дискретах, и она общая:
    // имени буфера в операндах нет вовсе. `FRAME` — та же область в
    // пользовательских единицах, и вот она своя у каждого буфера.
    //
    // **В машине это отображение живёт в самом буфере**, в неразобранных
    // байтах заголовка 7-39. Выдумывать их нельзя — буфер уходит на дискету
    // через `DATA SAVE`, — поэтому держим его при себе. Для корпуса
    // неразличимо: `SLIDE` при загрузке чужой картинки заголовок отбрасывает
    // и зовёт `FRAME` заново (4550, 4510).
    struct GBox {
        GBox() {}
        Number x0, x1, y0, y1;
    };
    static GBox screen_box();
    GBox gwin_;
    std::map<unsigned, GBox> gframe_;

    // --- графика ---------------------------------------------------------
    // Разобрать операнд-буфер: символьная переменная целиком.
    bool skip_device_write() const;
    bool emit_to_device(const uint8_t * data, unsigned len);
    bool raw_comma(Stream & st);
    bool gbuf_operand(Stream & st, const char * who, unsigned & var,
                      Evaluator::Target & tgt);
    bool do_gbox(Stream & st, GBox & out, const char * who);
    // Пользовательские единицы — в дискреты устройства по FRAME и WINDOW.
    bool gmap(unsigned var, const Number & ux, const Number & uy,
              long & x, long & y);
    bool do_gopen(Stream & st);
    bool do_gwindow(Stream & st);
    bool do_gframe(Stream & st);
    bool gmap_delta(unsigned var, const Number & ux, const Number & uy,
                    long & x, long & y);
    bool do_gpoint(Stream & st, uint8_t op, const char * who,
                   bool relative = false);
    bool do_glabel(Stream & st);
    bool do_gcopy(Stream & st);
    // Преобразования уже лежащей картинки: буфер, запятая, ровно n чисел.
    bool gnums(Stream & st, const char * who, Number * out, unsigned n);
    bool gxform_head(Stream & st, const char * who, unsigned n,
                     unsigned & var, Evaluator::Target & tgt, Number * u);
    bool gxform(Evaluator::Target & tgt, const GAffine & m, const char * who);
    bool do_gmove(Stream & st);
    bool do_gstretch(Stream & st);
    bool do_gturn(Stream & st);
    bool do_glet(Stream & st);
    // `PLOT` — прямой вывод на устройство, без буфера.
    bool plot_group(Stream & st, Raster & out);
    bool do_plot(Stream & st);
    // Место символьного значения со знаком «в обратном порядке»
    // (руководство, разд. 15.2).
    bool str_place(Stream & st, bool & reverse, Evaluator::Target & out);
    bool do_input(Stream & st);
    bool do_linput(Stream & st);
    bool do_convert(Stream & st);
    // Упаковка чисел в десятично-упакованный формат (разд. 13.7).
    bool do_pack(Stream & st, bool unpack);
    bool do_bin(Stream & st);
    bool do_init(Stream & st);
    // Операции над байтами (гл. 14). `AND`, `OR` и `XOR` — частные
    // случаи `BOOL`: их таблицы истинности 8, E и 6.
    bool do_bitop(Stream & st, unsigned x, bool from_stream);
    bool do_add(Stream & st, bool carry_mode);
    bool do_rotate(Stream & st);
    // Второй аргумент: код байта либо вторая символьная переменная.
    bool byte_arg(Stream & st, Value & out);
    bool do_let(Stream & st);
    bool do_for(Stream & st);
    bool do_next(Stream & st);
    bool do_if(Stream & st);
    bool do_on(Stream & st);
    bool do_gosubq(Stream & st);
    bool do_save_dc(Stream & st);
    bool do_load_dc(Stream & st);
    // Загрузка программы по абсолютному адресу сектора (разд. 19.1).
    bool do_load_da(Stream & st);
    // Общий хвост обоих: до трёх номеров строк и сама замена текста.
    bool load_segment(Stream & st, const std::vector<uint8_t> & file);
    bool do_list_dc(Stream & st);
    // Команды диалога, встреченные внутри программы (разд. 3 и 8.3).
    bool do_return_clear(Stream & st, unsigned len);
    bool do_clear(Stream & st);
    bool do_run(Stream & st);
    // paged — форма `LIST S`, глагол `33`: «в 23 строки экрана» (разд. 3.4).
    bool do_list(Stream & st, bool paged);
    bool do_save_da(Stream & st);
    // Один-два номера строк сырыми парами BCD; сколько прочитано.
    unsigned line_range(Stream & st, unsigned & from, unsigned & to);
    // Обмен программой через символьный буфер (EDITOR 5195, ASMBBAS 9056).
    bool do_keyin(Stream & st);
    bool do_save_buf(Stream & st);
    bool do_load_buf(Stream & st);
    unsigned buf_lines(Stream & st, unsigned * out, unsigned max);
    bool do_deffn(Stream & st, unsigned len);
    // Функции пользователя: `DEFFN <имя>(<формальная>)=<а.в.>` (разд. 4.8).
    void build_functions();
    bool call_fn(unsigned name, const Value & arg, Value & out, std::string & err);

    // Приставка `<устройство>[¤][/адрес][#строка]` — в номер строки таблицы
    // устройств и номер дисковода хоста.
    bool disk_prefix(Stream & st, bool with_device, Disk & d,
                     bool allow_verify = true);
    // Ошибка машины, которую может перехватить ON ERROR. Простой fail() —
    // это ограничение эмулятора, и перехватывать его нельзя: иначе
    // нереализованный оператор молча превратится в «ошибку ввода-вывода».
    bool machine_error(const char * code, const std::string & m);
    // Передать управление обработчику. false — перехватывать некому.
    bool handle_error();
    // Присвоить приёмнику очередное значение записи; used — сколько значений
    // из vals уже разобрано.
    bool store_value(const Evaluator::Target & target, Stream & st,
                     const std::vector<Value> & vals, std::size_t & used);
    bool assign_string(Stream & st, const Evaluator::Target & target,
                       const std::string & value);
    bool read_line(const std::string & prompt, bool has_prompt, std::string & out);

    // Помеченные подпрограммы. Машина ищет DEFFN' просмотром текста
    // программы (руководство, разд. 10.4); здесь тот же просмотр сделан
    // один раз при первом вызове.
    void build_labels();

    // Операторы DATA в порядке строк. У машины они связаны цепочкой прямо в
    // потоке — два последних байта операндов каждого DATA указывают на
    // следующий (docs/format.md, разд. 5); здесь это просто список, который
    // строится заново после всякой правки программы.
    void build_data();
    // Очередное значение из операторов DATA. false и exhausted — данные
    // кончились (ERR 27); false без exhausted — операнд не разобрался.
    bool next_data(Value & out, bool & exhausted);
    // Указатель начала считывания: оператор DATA и смещение в его операндах.
    void restore_data(unsigned stmt) { data_i_ = stmt; data_off_ = 0; }
    // Сбросить построенное просмотром программы — после её правки.
    void rescan()
    {
        labels_.clear();
        labels_ready_ = false;
        data_ready_ = false;
        funcs_.clear();
        funcs_ready_ = false;
    }

    void emit(const std::string & koi8);
    void emit_newline();
    // Вывод группы PRINT таблицы устройств: «PRINT — устройство
    // вывода для операторов PRINTUSING, HEXPRINT и MATPRINT»
    // (руководство, разд. 11.5). По умолчанию это экран.
    void emit_print(const std::string & koi8);
    void emit_print_newline();
    // То же для любой группы: `LIST` печатает на свою (разд. 11.5).
    void emit_group(DeviceGroup g, const std::string & koi8);
    void emit_group_newline(DeviceGroup g);
    // Образ из строки с оператором `%`; false — такой строки нет
    // или в ней нет `%`.
    bool image_of_line(unsigned number, std::string & image) const;
    void emit_zone();

    bool jump(unsigned line_number);
    bool fail(const std::string & m);
    // limit — сколько операторов исполнить; ноль значит «пока не кончится».
    bool loop(std::string & error, unsigned long limit = 0);
    const std::vector<uint8_t> & body_at(unsigned li) const;

    // Строка вне программы: её тело живёт здесь, а li_ равен DIRECT.
    static const unsigned DIRECT = 0xFFFFFFFFu;
    std::vector<uint8_t> direct_;

    ProgramImage & img_;
    Host & host_;
    VarStore store_;
    DeviceTable dev_;
    std::vector<Frame> loops_;
    std::vector<std::pair<unsigned, unsigned> > calls_;   // GOSUB: куда вернуться

    // Обработка ошибок: «обработка возникшей ошибки всегда проводится с
    // учётом параметров последнего выполненного оператора» (разд. 11.6).
    struct ErrorTrap {
        ErrorTrap() : mode(EM_OFF), line(0), has_targets(false),
                      target_a(0), target_b(0) {}
        unsigned mode;
        unsigned line;
        bool has_targets;
        unsigned target_a;            // приёмник кода
        unsigned target_b;            // приёмник номера строки
    };
    ErrorTrap trap_;
    std::string err_code_;            // код текущей ошибки, если она машинная

    // Метка помеченной подпрограммы → её DEFFN': строка и смещение в теле.
    std::map<unsigned, std::pair<unsigned, unsigned> > labels_;
    bool labels_ready_;

    // Имя функции → её DEFFN: строка и смещение оператора в теле. «Функция
    // может быть объявлена в любом месте программы, независимо от того, где
    // она будет использоваться» (разд. 4.8), поэтому — тем же просмотром,
    // что и метки.
    std::map<unsigned, std::pair<unsigned, unsigned> > funcs_;
    bool funcs_ready_;
    Functions fnres_;
    unsigned fn_depth_;             // защита от бесконечной рекурсии

    // Оператор DATA: строка, смещение операндов и их длина уже без
    // двухбайтового хвоста цепочки.
    struct DataStmt {
        DataStmt() : line(0), at(0), len(0) {}
        unsigned line, at, len;
    };
    std::vector<DataStmt> data_;
    bool data_ready_;
    unsigned data_i_;       // указатель начала считывания: какой DATA
    unsigned data_off_;     // и смещение в его операндах

    // Последний `DATA LOAD DC` прочитал концевую запись — это и проверяет
    // `IF END THEN` (руководство, разд. 18.5).
    bool end_seen_;

    unsigned li_;           // индекс текущей строки
    unsigned off_;          // смещение текущего оператора в теле строки
    unsigned next_off_;     // смещение следующего оператора
    bool jumped_;
    bool stopped_;
    StopReason stop_reason_;
    bool can_continue_;
    bool sf_armed_;
    unsigned long max_steps_;
    // Счётчик для редких обращений к хосту: окно надо дёргать и тогда,
    // когда показывать нечего, — иначе оно не отвечает на события.
    unsigned long shown_;
    bool skip_machine_;

    // Устройство, на которое уводит вывод приставка `PRINT /<адрес>`. Ноль
    // и `05` — консольный экран, то есть обычный путь со знакоместами.
    // Сбрасывается на выходе из оператора: приставка действует только на
    // него одного.
    unsigned print_dev_;
    bool print_fail_;

    // Состояние пера `PLOT`. Живёт между операторами: `SLIDE` 6160 задаёт
    // размер знака один раз на серию, а рисует потом строкой 6200.
    long plot_x_, plot_y_;
    long plot_step_x_, plot_step_y_;
    unsigned plot_size_;

    std::string error_;
};

} // namespace iskra
