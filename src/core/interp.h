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
#include "core/host.h"
#include "core/program.h"
#include "core/value.h"
#include "core/vars.h"

namespace iskra {

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
    // Один оператор: источник байт и вычислитель над ним. Живёт ровно на
    // время исполнения оператора.
    struct Stream {
        Stream(const uint8_t * p, unsigned n, const std::vector<VarInfo> * vars,
               VarStore & store)
            : src(p, n, vars), ev(src, store) {}
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
    bool do_abs_record(Stream & st, bool load);
    bool do_verify(Stream & st);
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
    bool do_redim(Stream & st);
    bool do_mat(Stream & st);
    bool do_mat_copy(Stream & st);
    bool do_mat_search(Stream & st);
    // Замена и перекодировка символьных данных (разд. 15.3).
    bool do_replace(Stream & st);
    bool do_tran(Stream & st);
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
    bool do_add(Stream & st);
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
    bool do_list_dc(Stream & st);
    // Команды диалога, встреченные внутри программы (разд. 3 и 8.3).
    bool do_return_clear(Stream & st, unsigned len);
    bool do_clear(Stream & st);
    bool do_run(Stream & st);
    bool do_list(Stream & st);
    // Один-два номера строк сырыми парами BCD; сколько прочитано.
    unsigned line_range(Stream & st, unsigned & from, unsigned & to);
    // Обмен программой через символьный буфер (EDITOR 5195, ASMBBAS 9056).
    bool do_keyin(Stream & st);
    bool do_save_buf(Stream & st);
    bool do_load_buf(Stream & st);
    unsigned buf_lines(Stream & st, unsigned * out, unsigned max);
    bool do_deffn(Stream & st, unsigned len);

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
    void rescan() { labels_.clear(); labels_ready_ = false; data_ready_ = false; }

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
    bool loop(std::string & error);
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
    unsigned long max_steps_;
    bool skip_machine_;
    std::string error_;
};

} // namespace iskra
