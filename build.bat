@echo off
rem Configura e compila o projeto com o CMake que acompanha o Visual Studio 2022.
rem Ajuste os caminhos abaixo (ou defina as variaveis de ambiente antes de executar).
if not defined FreeGLUT_DIR set "FreeGLUT_DIR=%USERPROFILE%\dev-libs\freeglut\lib\cmake\FreeGLUT"
if not defined OpenCV_DIR   set "OpenCV_DIR=%USERPROFILE%\dev-libs\opencv\build"

set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist %CMAKE% set CMAKE=cmake

%CMAKE% -S . -B build -G "Visual Studio 17 2022" -A x64 -DFreeGLUT_DIR="%FreeGLUT_DIR%" -DOpenCV_DIR="%OpenCV_DIR%" || exit /b 1
%CMAKE% --build build --config Release || exit /b 1
echo.
echo Executaveis gerados em build\Release
echo   build\Release\desempenho_gl.exe               (modo interativo, GPU dedicada)
echo   build\Release\desempenho_gl.exe --benchmark   (protocolo automatico, ver executar_experimentos.bat)
echo   build\Release\desempenho_gl.exe --capturar    (gera as capturas do relatorio)
echo   build\Release\desempenho_gl_integrada.exe     (mesmo programa, sem preferencia pela GPU dedicada)
