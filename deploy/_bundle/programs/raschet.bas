10 REM ТАБЛИЦА ФУНКЦИИ И СУММА РЯДА
20 PRINT HEX(03)
30 SELECT D
40 PRINT "ТРИГОНОМЕТРИЯ В ГРАДУСАХ"
50 PRINT
60 PRINT "  УГОЛ       SIN         COS         SQR"
70 FOR A=0 TO 90 STEP 15
80 PRINTUSING 90,A,SIN(A),COS(A),SQR(A)
90 %  ####      #.######    #.######    ##.####
100 NEXT A
110 PRINT
120 REM СУММА ОБРАТНЫХ КВАДРАТОВ - ЗАДАЧА БАЗЕЛЯ
130 S=0
140 FOR N=1 TO 500
150 S=S+1/(N*N)
160 NEXT N
170 PRINT "СУММА 1/N^2, N=1..500 :";S
180 PRINT "ЧИСЛО PI*PI/6         :";#PI*#PI/6
190 PRINT
200 PRINT "РАЗНОСТЬ              :";#PI*#PI/6-S
210 END
