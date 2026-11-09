/*
 * texturas.cpp
 * Geração das texturas procedurais com ruído de valor periódico (ver texturas.h).
 */
#include "texturas.h"

#include <algorithm>
#include <cmath>

namespace {

// Pseudoaleatório determinístico em [0, 1) para um ponto inteiro da grade
float hash2(int x, int y, int semente) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u +
                      static_cast<std::uint32_t>(semente) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 16777216.0f;
}

float suave(float t) { return t * t * (3 - 2 * t); }

// Ruído de valor periódico: a grade de valores se repete a cada 'periodo' células,
// de modo que a textura não apresenta emenda quando repetida.
float ruido(float x, float y, int periodo, int semente) {
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float fx = suave(x - x0), fy = suave(y - y0);
    auto v = [&](int i, int j) {
        return hash2(((i % periodo) + periodo) % periodo, ((j % periodo) + periodo) % periodo, semente);
    };
    const float a = v(x0, y0), b = v(x0 + 1, y0), c = v(x0, y0 + 1), d = v(x0 + 1, y0 + 1);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

// Soma de oitavas (fractal Brownian motion), resultado aproximadamente em [0, 1]
float fbm(float u, float v, int periodoBase, int oitavas, int semente) {
    float soma = 0, amp = 0.5f, total = 0;
    int periodo = periodoBase;
    for (int o = 0; o < oitavas; ++o) {
        soma += amp * ruido(u * periodo, v * periodo, periodo, semente + 17 * o);
        total += amp;
        amp *= 0.5f;
        periodo *= 2;
    }
    return soma / total;
}

std::uint8_t byte(float x) { return static_cast<std::uint8_t>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); }

void pixel(std::vector<std::uint8_t>& img, int tam, int x, int y, float r, float g, float b) {
    const size_t k = (static_cast<size_t>(y) * tam + x) * 4;
    img[k] = byte(r);
    img[k + 1] = byte(g);
    img[k + 2] = byte(b);
    img[k + 3] = 255;
}

}  // namespace

Textura enviarTextura(const std::vector<std::uint8_t>& rgba, int tamanho) {
    Textura t;
    t.tamanho = tamanho;
    t.bytes = static_cast<std::size_t>(tamanho) * tamanho * 4 * 4 / 3;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);  // trilinear
    if (!glGenerateMipmap) glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_X, GL_TRUE);
    if (temAnisotropia()) {
        GLfloat maximo = 1;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_X, &maximo);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_X, std::min(8.0f, maximo));
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tamanho, tamanho, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (glGenerateMipmap) glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

Textura criarTexturaPiso(int tam) {
    std::vector<std::uint8_t> img(static_cast<size_t>(tam) * tam * 4);
    const int ladrilhos = 4;  // 4 x 4 ladrilhos por repetição da textura
    for (int y = 0; y < tam; ++y) {
        for (int x = 0; x < tam; ++x) {
            const float u = static_cast<float>(x) / tam, v = static_cast<float>(y) / tam;
            const float lu = u * ladrilhos, lv = v * ladrilhos;
            const int iu = static_cast<int>(lu), iv = static_cast<int>(lv);
            const float fu = lu - iu, fv = lv - iv;
            // distância até a borda do ladrilho -> rejunte
            const float borda = std::min(std::min(fu, 1 - fu), std::min(fv, 1 - fv));
            const float rejunte = 1.0f - std::clamp((borda - 0.015f) / 0.02f, 0.0f, 1.0f);
            const float variacao = hash2(iu, iv, 7) * 0.25f;  // tom próprio de cada ladrilho
            const float n = fbm(u, v, 8, 5, 3);
            const float base = 0.62f + variacao + 0.30f * (n - 0.5f);
            float r = base * 0.95f, g = base * 0.80f, b = base * 0.62f;  // arenito
            r = r * (1 - rejunte) + 0.22f * rejunte;
            g = g * (1 - rejunte) + 0.20f * rejunte;
            b = b * (1 - rejunte) + 0.19f * rejunte;
            pixel(img, tam, x, y, r, g, b);
        }
    }
    return enviarTextura(img, tam);
}

Textura criarTexturaMarmore(int tam) {
    std::vector<std::uint8_t> img(static_cast<size_t>(tam) * tam * 4);
    for (int y = 0; y < tam; ++y) {
        for (int x = 0; x < tam; ++x) {
            const float u = static_cast<float>(x) / tam, v = static_cast<float>(y) / tam;
            const float n = fbm(u, v, 4, 6, 11);
            // veios: seno da coordenada perturbado pelo ruído (período inteiro em u e v)
            const float veio = std::fabs(std::sin(2 * 3.14159265f * (2 * u + 1 * v) + 9.0f * n));
            const float escuro = std::pow(1.0f - veio, 6.0f);
            const float base = 0.93f - 0.10f * n;
            pixel(img, tam, x, y, base - 0.55f * escuro, base - 0.50f * escuro, base - 0.42f * escuro);
        }
    }
    return enviarTextura(img, tam);
}

Textura criarTexturaListras(int tam) {
    std::vector<std::uint8_t> img(static_cast<size_t>(tam) * tam * 4);
    for (int y = 0; y < tam; ++y) {
        for (int x = 0; x < tam; ++x) {
            const float u = static_cast<float>(x) / tam, v = static_cast<float>(y) / tam;
            const float n = fbm(u, v, 8, 4, 23);
            // faixas diagonais (efeito de cabo trançado) com leve ruído de escovado
            const float faixa = 0.5f + 0.5f * std::sin(2 * 3.14159265f * (4 * u + 2 * v));
            const float claro = 0.55f + 0.45f * std::pow(faixa, 3.0f) + 0.10f * (n - 0.5f);
            pixel(img, tam, x, y, claro, claro * 0.92f, claro * 0.80f);
        }
    }
    return enviarTextura(img, tam);
}
