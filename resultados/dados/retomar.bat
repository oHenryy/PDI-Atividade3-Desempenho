@echo off
rem Retomada da coleta apos a interrupcao de 9/11/2026 (janela minimizada durante E1, config F).
rem E1-F e medida novamente em arquivo proprio (e1_vbo_T_F), que substitui a F incompleta de e1_vbo_T.
pushd "%~dp0\..\.."
set EXE=build\Release\desempenho_gl.exe
set EXEI=build\Release\desempenho_gl_integrada.exe
set SAIDA=resultados\dados
%EXE% --benchmark --rotulo e1_vbo_T_F --modo vbo --variavel T --N 64 --valores 16,64,128,192,256,320,384,448,512,640,768 --configs F --parada 35 --saida %SAIDA%
%EXE% --benchmark --rotulo e2_imediato_T --modo imediato --variavel T --N 64 --valores 8,16,24,32,48,64 --configs ABCDEF --parada 30 --saida %SAIDA%
%EXE% --benchmark --rotulo e3_vbo_N --modo vbo --variavel N --T 8 --valores 64,256,1024,4096,8192,16384,32768,65536 --configs AF --parada 30 --saida %SAIDA%
%EXEI% --benchmark --rotulo e4_integrada_T --modo vbo --variavel T --N 64 --valores 16,64,128,192,256,320 --configs ABCDEF --parada 35 --saida %SAIDA%
popd
