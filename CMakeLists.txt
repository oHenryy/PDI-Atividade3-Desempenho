cmake_minimum_required(VERSION 3.20)
project(PDI_Atividade3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Localização das bibliotecas: informe com -DFreeGLUT_DIR=... (e opcionalmente -DOpenCV_DIR=...)
# ou defina as variáveis de ambiente correspondentes (ver build.bat).
#   FreeGLUT_DIR -> pasta que contém FreeGLUTConfig.cmake (ex.: C:/libs/freeglut/lib/cmake/FreeGLUT)
#   OpenCV_DIR   -> pasta que contém OpenCVConfig.cmake   (ex.: C:/libs/opencv/build)
# O OpenCV é OPCIONAL: é usado apenas para gravar as capturas de tela em PNG.

find_package(OpenGL   REQUIRED)
find_package(FreeGLUT REQUIRED)
find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)

set(FONTES
    src/main.cpp
    src/gl_ext.cpp       src/gl_ext.h
    src/malha.cpp        src/malha.h
    src/texturas.cpp     src/texturas.h
    src/shaders.cpp      src/shaders.h
    src/cena.cpp         src/cena.h
    src/monitor.cpp      src/monitor.h
    src/benchmark.cpp    src/benchmark.h)

# Um único projeto, compilado em dois executáveis que diferem apenas na preferência de GPU:
#   desempenho_gl           -> exporta NvOptimusEnablement: usa a GPU dedicada (experimentos principais)
#   desempenho_gl_integrada -> sem a exportação: o Windows/driver escolhe (normalmente a GPU integrada)
foreach(alvo desempenho_gl desempenho_gl_integrada)
    add_executable(${alvo} ${FONTES})
    target_link_libraries(${alvo} PRIVATE FreeGLUT::freeglut OpenGL::GL OpenGL::GLU)
    if(WIN32)
        target_link_libraries(${alvo} PRIVATE pdh dxgi psapi)
    endif()
    if(OpenCV_FOUND)
        target_compile_definitions(${alvo} PRIVATE TEM_OPENCV)
        target_link_libraries(${alvo} PRIVATE ${OpenCV_LIBS})
    endif()
    if(MSVC)
        target_compile_options(${alvo} PRIVATE /utf-8 /W3 /O2)
    endif()
    # Copia as DLLs (freeglut, opencv_world) para junto do executável
    add_custom_command(TARGET ${alvo} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${alvo}> $<TARGET_FILE_DIR:${alvo}>
        COMMAND_EXPAND_LISTS)
endforeach()
target_compile_definitions(desempenho_gl PRIVATE PREFERIR_GPU_DEDICADA)

if(OpenCV_FOUND)
    message(STATUS "OpenCV ${OpenCV_VERSION} encontrado: capturas em PNG")
else()
    message(STATUS "OpenCV nao encontrado: capturas em PPM")
endif()
