/*
 * cena.h
 * Cena "Jardim de Orbes": um piso de ladrilhos, um nó toroidal dourado girando no centro,
 * N orbes de mármore distribuídos em espiral filotáxica (como sementes de girassol) que
 * giram em carrossel em torno do nó, cada um girando sobre si e oscilando na vertical, e
 * uma lâmpada (esfera emissiva) que marca a posição da fonte de luz.
 *
 * A carga geométrica é controlada por dois parâmetros independentes:
 *   T = nível de detalhe de cada orbe   -> 4T(T-1) triângulos por orbe;
 *   N = número de orbes                  -> N chamadas de desenho (glDrawElements).
 * Triângulos por quadro = cenário fixo (piso + nó + lâmpada) + N * 4T(T-1).
 *
 * As seis configurações experimentais (A..F) combinam o modo de iluminação e a textura:
 *   A: sem luz, sem textura    B: omnidirecional, sem textura    C: spot, sem textura
 *   D: sem luz, com textura    E: omnidirecional, com textura    F: spot, com textura
 */
#pragma once

#include "malha.h"
#include "shaders.h"
#include "texturas.h"

#include <cstdint>
#include <string>
#include <vector>

// Limites dos parâmetros de carga
const int T_MIN = 2, T_MAX = 1024;
const int N_MIN = 1, N_MAX = 65536;

Iluminacao iluminacaoDaConfig(char config);
bool texturaDaConfig(char config);
char configDe(Iluminacao luz, bool textura);
std::string descreverConfig(char config);  // ex.: "C (spot, sem textura)"
std::string nomeIluminacao(Iluminacao luz);

struct EstadoCena {
    char config = 'A';
    int T = 32;            // detalhe dos orbes
    int N = 64;            // número de orbes
    bool usarVBO = true;   // false = modo imediato (glBegin/glEnd)
    bool aramado = false;  // desenho em arame (glPolygonMode GL_LINE)
};

class Cena {
public:
    bool inicializar(std::string& erro);  // requer contexto OpenGL ativo
    void finalizar();

    void definirConfig(char config);
    void definirDetalhe(int T);  // recria a malha do orbe se T mudou
    void definirObjetos(int N);  // recalcula as posições dos orbes
    void definirVBO(bool usar) { estado_.usarVBO = usar; }
    void definirAramado(bool a) { estado_.aramado = a; }
    const EstadoCena& estado() const { return estado_; }

    // Desenha um quadro completo. 'tempo' = instante da animação (s).
    void desenhar(double tempo, int largura, int altura);

    // Contagem de triângulos submetidos por quadro
    std::uint64_t triangulosCenario() const;          // piso + nó + lâmpada
    std::uint64_t triangulosPorOrbe() const { return orbe_.triangulos(); }
    std::uint64_t triangulosPorQuadro() const;
    static std::uint64_t triangulosPara(int T, int N);  // sem precisar gerar a malha
    int chamadasDesenhoPorQuadro() const { return 3 + estado_.N; }
    double memoriaGeometriaMB() const;
    double memoriaTexturasMB() const;

private:
    struct Orbe {
        float x, z, escala, fase, r, g, b;
    };
    EstadoCena estado_;
    Malha orbe_, piso_, no_, lampada_;
    Textura texPiso_, texMarmore_, texListras_;
    ProgramaCena programas_[3][2];
    std::vector<Orbe> orbes_;
};
