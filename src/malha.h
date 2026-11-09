/*
 * malha.h
 * Malhas de triângulos da cena: geração procedural, envio para a GPU e desenho.
 *
 * Toda a geometria da aplicação é formada exclusivamente por TRIÂNGULOS indexados
 * (GL_TRIANGLES). A quantidade de polígonos de uma malha é, portanto, exatamente
 * indices.size() / 3 -- não há quadriláteros, leques ou faixas que precisem de conversão.
 *
 * Há dois caminhos de envio da mesma malha ao OpenGL, selecionáveis em tempo de execução:
 *   - VBO      : vértices e índices ficam na memória de vídeo; um glDrawElements por objeto;
 *   - imediato : glBegin/glEnd com glTexCoord/glNormal/glVertex para cada vértice de cada
 *                triângulo, a cada quadro (todo o trabalho de envio recai sobre a CPU).
 */
#pragma once

#include "gl_ext.h"

#include <cstdint>
#include <vector>

struct Vertice {
    float px, py, pz;  // posição
    float nx, ny, nz;  // normal (unitária)
    float u, v;        // coordenadas de textura
};

class Malha {
public:
    std::vector<Vertice> vertices;
    std::vector<std::uint32_t> indices;  // 3 índices por triângulo

    Malha() = default;
    Malha(const Malha&) = delete;
    Malha& operator=(const Malha&) = delete;
    Malha(Malha&& o) noexcept;
    Malha& operator=(Malha&& o) noexcept;
    ~Malha();

    std::uint64_t triangulos() const { return indices.size() / 3; }
    std::size_t bytesGPU() const;  // memória ocupada pelo VBO + IBO

    void enviarParaGPU();  // cria (ou recria) o VBO e o IBO
    void liberarGPU();

    // Desenho em lote: vincular() uma vez, desenharVinculada() para cada objeto que usa
    // esta malha (com a matriz de modelagem já ajustada) e desvincular() ao final.
    void vincular(bool usarVBO) const;
    void desenharVinculada(bool usarVBO) const;
    void desvincular(bool usarVBO) const;

private:
    GLuint vbo_ = 0, ibo_ = 0;
};

// ---------------------------------------------------------------------------
// Geradores
// ---------------------------------------------------------------------------

// "Orbe": esfera de raio 1 com relevo senoidal (gomos), parametrizada por longitude e
// latitude. T = nível de detalhe: T faixas de latitude e 2T de longitude.
// Os polos usam um só triângulo por faixa (sem triângulos degenerados):
//   triângulos = 2 * (2T) * (T - 1) = 4T(T - 1).
Malha gerarOrbe(int T, float amplitudeRelevo = 0.10f);
std::uint64_t triangulosOrbe(int T);

// Nó toroidal (p, q) = (2, 3) com seção circular. Triângulos = 2 * segmentos * lados.
Malha gerarNoToroidal(int segmentos, int lados);

// Piso quadrado [-tam, tam] x [-tam, tam] em y = 0, dividido em div x div quadrados
// (cada um com 2 triângulos). Triângulos = 2 * div^2.
Malha gerarPiso(int divisoes, float tamanho);
