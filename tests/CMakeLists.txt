# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226

# Тесты без сторонних библиотек: каждый — обычная программа, ненулевой код
# возврата означает провал. Запуск: ctest --test-dir build --output-on-failure

function(iskra_add_test name)
    add_executable(${name} ${name}.cpp check.h)
    target_link_libraries(${name} PRIVATE iskra_headless iskra_host_common
                                          iskra_warnings)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    # Часть тестов работает на корпусе — он лежит в самом репозитории.
    target_compile_definitions(${name} PRIVATE
        ISKRA_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus/hex")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

iskra_add_test(test_screen)
iskra_add_test(test_render)
iskra_add_test(test_diskfiles)
iskra_add_test(test_number)
iskra_add_test(test_stat04)
iskra_add_test(test_arrays)
iskra_add_test(test_strings)
iskra_add_test(test_convert)
iskra_add_test(test_mat)
iskra_add_test(test_bytes)
iskra_add_test(test_printusing)
iskra_add_test(test_read)
iskra_add_test(test_deffn)
iskra_add_test(test_disk)
iskra_add_test(test_catalog)
iskra_add_test(test_select)
iskra_add_test(test_diskio)
iskra_add_test(test_disksave)
iskra_add_test(test_logic)
iskra_add_test(test_program)
iskra_add_test(test_tokenize)
iskra_add_test(test_detokenize)
iskra_add_test(test_eval)
iskra_add_test(test_progfile)
iskra_add_test(test_console)
iskra_add_test(test_console_verbs)
iskra_add_test(test_keyin)
iskra_add_test(test_bt)