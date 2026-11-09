@echo off
rem Executa todos os experimentos do relatorio (cerca de 2h30) e gera os graficos.
rem Antes de executar: notebook ligado na tomada, aplicativos em segundo plano fechados,
rem V-Sync do driver em "configuracao do aplicativo" (o programa desliga o V-Sync).
rem Cada ponto = 3 repeticoes x (2 s de estabilizacao + 10 s de medicao), janela 1280x720.
pushd "%~dp0"
set EXE=build\Release\desempenho_gl.exe
set EXEI=build\Release\desempenho_gl_integrada.exe
set SAIDA=resultados\dados

rem Capturas de tela das seis configuracoes e do desenho em arame
%EXE% --capturar

rem E1 (principal): GPU dedicada, VBO, aumento do detalhe T dos 64 orbes, configuracoes A..F
%EXE% --benchmark --rotulo e1_vbo_T --modo vbo --variavel T --N 64 --valores 16,64,128,192,256,320,384,448,512,640,768 --configs ABCDEF --parada 35 --saida %SAIDA%

rem E2: mesma cena enviada em modo imediato (glBegin/glEnd), configuracoes A..F
%EXE% --benchmark --rotulo e2_imediato_T --modo imediato --variavel T --N 64 --valores 8,16,24,32,48,64 --configs ABCDEF --parada 30 --saida %SAIDA%

rem E3: aumento do NUMERO de orbes (T = 8 fixo), uma chamada de desenho por orbe, configuracoes A e F
%EXE% --benchmark --rotulo e3_vbo_N --modo vbo --variavel N --T 8 --valores 64,256,1024,4096,8192,16384,32768,65536 --configs AF --parada 30 --saida %SAIDA%

rem E4: mesmo protocolo de E1 na GPU integrada (executavel sem preferencia pela GPU dedicada)
%EXEI% --benchmark --rotulo e4_integrada_T --modo vbo --variavel T --N 64 --valores 16,64,128,192,256,320 --configs ABCDEF --parada 35 --saida %SAIDA%

rem Graficos e tabelas do relatorio
python tools\graficos.py
popd
