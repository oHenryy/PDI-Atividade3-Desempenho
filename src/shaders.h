/*
 * shaders.h
 * Programas GLSL da cena. Uma única fonte de shader é compilada em 6 variantes, uma para
 * cada combinação (modo de iluminação x textura), selecionadas por #define. Desse modo cada
 * configuração experimental executa SOMENTE o código de que precisa (não há desvios
 * condicionais em tempo de execução que pudessem mascarar o custo de cada recurso).
 *
 *   Iluminação: 0 = sem iluminação (cor do material)
 *               1 = omnidirecional (point light) com atenuação pela distância
 *               2 = spot (spotlight) com cone interno/externo e atenuação
 *   Textura:    0 = desligada, 1 = ligada (texture2D modulando a cor do material)
 *
 * A iluminação é calculada POR FRAGMENTO (modelo de Blinn-Phong), usando a posição e a normal
 * interpoladas em coordenadas do observador.
 */
#pragma once

#include "gl_ext.h"

#include <string>

enum class Iluminacao { Nenhuma = 0, Omnidirecional = 1, Spot = 2 };

struct ProgramaCena {
    GLuint id = 0;
    GLint uTextura = -1;        // sampler2D (unidade 0)
    GLint uLuzPos = -1;         // posição da luz (coordenadas do observador)
    GLint uSpotDir = -1;        // direção do eixo do spot (coordenadas do observador)
    GLint uSpotCosExterno = -1; // cosseno do semiângulo externo do cone
    GLint uSpotCosInterno = -1; // cosseno do semiângulo interno (borda suave)
    GLint uEmissivo = -1;       // 1 = objeto emissor (marcador da lâmpada), ignora a luz
};

// Compila as 6 variantes. Retorna false e preenche 'erro' em caso de falha.
bool criarProgramas(ProgramaCena programas[3][2], std::string& erro);
void destruirProgramas(ProgramaCena programas[3][2]);
