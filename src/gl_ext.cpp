/*
 * gl_ext.cpp
 * Carregamento das funções do OpenGL posteriores à versão 1.1 (ver gl_ext.h).
 */
#include "gl_ext.h"

#include <GL/freeglut.h>

#include <cstring>

// Definição dos ponteiros (todos começam nulos)
#define GLX_DEFINIR(ret, nome, params, obrig) PFN_##nome nome = nullptr;
LISTA_FUNCOES_GL(GLX_DEFINIR)
#undef GLX_DEFINIR

static bool g_anisotropia = false;

// Verifica se o nome aparece como palavra inteira na lista de extensões do contexto.
static bool extensaoPresente(const char* nome) {
    const char* lista = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!lista) return false;
    const size_t n = std::strlen(nome);
    for (const char* p = std::strstr(lista, nome); p; p = std::strstr(p + 1, nome)) {
        const bool inicioOk = (p == lista) || (p[-1] == ' ');
        const bool fimOk = (p[n] == ' ') || (p[n] == '\0');
        if (inicioOk && fimOk) return true;
    }
    return false;
}

bool carregarExtensoesGL(std::string& faltando) {
    faltando.clear();
#define GLX_CARREGAR(ret, nome, params, obrig)                                       \
    nome = reinterpret_cast<PFN_##nome>(glutGetProcAddress(#nome));                  \
    if (!nome && obrig) faltando += std::string(faltando.empty() ? "" : ", ") + #nome;
    LISTA_FUNCOES_GL(GLX_CARREGAR)
#undef GLX_CARREGAR
    g_anisotropia = extensaoPresente("GL_EXT_texture_filter_anisotropic") ||
                    extensaoPresente("GL_ARB_texture_filter_anisotropic");
    return faltando.empty();
}

bool temConsultaTempoGPU() {
    return glGenQueries && glBeginQuery && glEndQuery && glGetQueryObjectiv && glGetQueryObjectui64v;
}

bool temControleVSync() { return wglSwapIntervalEXT != nullptr; }

bool temAnisotropia() { return g_anisotropia; }

int definirVSync(bool ligado) {
    if (!wglSwapIntervalEXT) return -1;
    wglSwapIntervalEXT(ligado ? 1 : 0);
    return consultarVSync();
}

int consultarVSync() { return wglGetSwapIntervalEXT ? wglGetSwapIntervalEXT() : -1; }
