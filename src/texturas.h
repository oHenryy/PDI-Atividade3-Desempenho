/*
 * texturas.h
 * Texturas procedurais da cena, geradas na CPU na inicialização (nenhum arquivo de imagem
 * é necessário). Todas são RGBA 8 bits, periódicas (sem emendas com GL_REPEAT), com mipmaps,
 * filtragem trilinear e, se disponível, filtragem anisotrópica 8x.
 */
#pragma once

#include "gl_ext.h"

#include <cstdint>
#include <vector>

struct Textura {
    GLuint id = 0;
    int tamanho = 0;
    std::size_t bytes = 0;  // memória estimada na GPU, incluindo mipmaps (~4/3 do nível 0)
};

Textura criarTexturaPiso(int tamanho);     // ladrilhos de pedra com rejunte
Textura criarTexturaMarmore(int tamanho);  // mármore claro com veios (orbes)
Textura criarTexturaListras(int tamanho);  // faixas metálicas trançadas (nó central)

// Envia uma imagem RGBA para a GPU e devolve a textura (usada pelas funções acima).
Textura enviarTextura(const std::vector<std::uint8_t>& rgba, int tamanho);
