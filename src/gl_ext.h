/*
 * gl_ext.h
 * Atividade 3 - Análise de desempenho de CPU e GPU com OpenGL
 *
 * Carregador mínimo das funções do OpenGL posteriores à versão 1.1. No Windows, o
 * opengl32.dll exporta apenas o OpenGL 1.1; as demais funções (shaders GLSL, Vertex Buffer
 * Objects, consultas de tempo da GPU, controle do V-Sync) são obtidas do driver em tempo de
 * execução com glutGetProcAddress. Assim o projeto não depende de GLEW/GLAD.
 *
 * Cada função é declarada uma única vez na lista LISTA_FUNCOES_GL (técnica "X-macro"):
 * a mesma lista gera o typedef do ponteiro, a declaração extern e o carregamento.
 */
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#define APIENTRY
#endif

#include <GL/gl.h>

#include <cstddef>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Constantes que não existem no gl.h do OpenGL 1.1 (valores do glext.h oficial)
// ---------------------------------------------------------------------------
#define GL_ARRAY_BUFFER_X                 0x8892
#define GL_ELEMENT_ARRAY_BUFFER_X         0x8893
#define GL_STATIC_DRAW_X                  0x88E4
#define GL_FRAGMENT_SHADER_X              0x8B30
#define GL_VERTEX_SHADER_X                0x8B31
#define GL_COMPILE_STATUS_X               0x8B81
#define GL_LINK_STATUS_X                  0x8B82
#define GL_INFO_LOG_LENGTH_X              0x8B84
#define GL_SHADING_LANGUAGE_VERSION_X     0x8B8C
#define GL_TIME_ELAPSED_X                 0x88BF
#define GL_QUERY_RESULT_X                 0x8866
#define GL_QUERY_RESULT_AVAILABLE_X       0x8867
#define GL_GENERATE_MIPMAP_X              0x8191
#define GL_TEXTURE_MAX_ANISOTROPY_X       0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_X   0x84FF

typedef char GLcharX;
typedef std::ptrdiff_t GLsizeiptrX;

// ---------------------------------------------------------------------------
// Lista das funções carregadas: X(tipo de retorno, nome, (parâmetros), obrigatória)
// ---------------------------------------------------------------------------
#define LISTA_FUNCOES_GL(X)                                                                   \
    /* Shaders GLSL (OpenGL 2.0) */                                                           \
    X(GLuint, glCreateShader, (GLenum tipo), true)                                            \
    X(void, glShaderSource, (GLuint s, GLsizei n, const GLcharX* const* src, const GLint* tam), true) \
    X(void, glCompileShader, (GLuint s), true)                                                \
    X(void, glGetShaderiv, (GLuint s, GLenum p, GLint* v), true)                              \
    X(void, glGetShaderInfoLog, (GLuint s, GLsizei max, GLsizei* tam, GLcharX* log), true)    \
    X(GLuint, glCreateProgram, (void), true)                                                  \
    X(void, glAttachShader, (GLuint p, GLuint s), true)                                       \
    X(void, glLinkProgram, (GLuint p), true)                                                  \
    X(void, glGetProgramiv, (GLuint p, GLenum q, GLint* v), true)                             \
    X(void, glGetProgramInfoLog, (GLuint p, GLsizei max, GLsizei* tam, GLcharX* log), true)   \
    X(void, glUseProgram, (GLuint p), true)                                                   \
    X(void, glDeleteShader, (GLuint s), true)                                                 \
    X(void, glDeleteProgram, (GLuint p), true)                                                \
    X(GLint, glGetUniformLocation, (GLuint p, const GLcharX* nome), true)                     \
    X(void, glUniform1i, (GLint loc, GLint v), true)                                          \
    X(void, glUniform1f, (GLint loc, GLfloat v), true)                                        \
    X(void, glUniform3f, (GLint loc, GLfloat x, GLfloat y, GLfloat z), true)                  \
    /* Vertex Buffer Objects (OpenGL 1.5) */                                                  \
    X(void, glGenBuffers, (GLsizei n, GLuint* b), true)                                       \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint* b), true)                              \
    X(void, glBindBuffer, (GLenum alvo, GLuint b), true)                                      \
    X(void, glBufferData, (GLenum alvo, GLsizeiptrX tam, const void* dados, GLenum uso), true) \
    /* Consultas de tempo da GPU (OpenGL 1.5 / 3.3) */                                        \
    X(void, glGenQueries, (GLsizei n, GLuint* q), false)                                      \
    X(void, glDeleteQueries, (GLsizei n, const GLuint* q), false)                             \
    X(void, glBeginQuery, (GLenum alvo, GLuint q), false)                                     \
    X(void, glEndQuery, (GLenum alvo), false)                                                 \
    X(void, glGetQueryObjectiv, (GLuint q, GLenum p, GLint* v), false)                        \
    X(void, glGetQueryObjectui64v, (GLuint q, GLenum p, std::uint64_t* v), false)             \
    /* Mipmaps (OpenGL 3.0) */                                                                \
    X(void, glGenerateMipmap, (GLenum alvo), false)                                           \
    /* Intervalo de troca de buffers = V-Sync (WGL_EXT_swap_control) */                       \
    X(int, wglSwapIntervalEXT, (int intervalo), false)                                        \
    X(int, wglGetSwapIntervalEXT, (void), false)

#define GLX_DECLARAR(ret, nome, params, obrig) \
    typedef ret(APIENTRY* PFN_##nome) params;  \
    extern PFN_##nome nome;
LISTA_FUNCOES_GL(GLX_DECLARAR)
#undef GLX_DECLARAR

// Carrega todas as funções. Retorna false se faltar alguma obrigatória (nomes em 'faltando').
bool carregarExtensoesGL(std::string& faltando);

// Recursos opcionais detectados após o carregamento
bool temConsultaTempoGPU();   // GL_TIME_ELAPSED (ARB_timer_query / OpenGL 3.3)
bool temControleVSync();      // wglSwapIntervalEXT
bool temAnisotropia();        // EXT_texture_filter_anisotropic

// Liga/desliga o V-Sync (intervalo 1 ou 0). Retorna o intervalo efetivo (-1 se desconhecido).
int definirVSync(bool ligado);
int consultarVSync();
