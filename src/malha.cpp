/*
 * malha.cpp
 * Geração procedural das malhas e desenho por VBO ou modo imediato (ver malha.h).
 */
#include "malha.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace {

const float PI = 3.14159265358979f;

struct V3 {
    float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 normalizar(V3 a) {
    const float n = std::sqrt(dot(a, a));
    return n > 1e-20f ? a * (1.0f / n) : V3{0, 1, 0};
}

// Superfície paramétrica P(u, v), u e v em [0, 1], amostrada numa grade de
// (nu + 1) x (nv + 1) vértices. A normal é calculada numericamente como
// dP/du x dP/dv (diferenças centrais), o que dá normais suaves e contínuas na costura.
// Os triângulos são orientados no sentido anti-horário visto pelo lado da normal.
// Com polos = true, as linhas v = 0 e v = 1 colapsam num ponto (esfera): nessas faixas
// é gerado apenas o triângulo não degenerado de cada quadrilátero.
Malha superficie(int nu, int nv, bool polos, float repU, float repV,
                 const std::function<V3(float, float)>& P) {
    Malha m;
    m.vertices.reserve(static_cast<size_t>(nu + 1) * (nv + 1));
    const float h = 1e-3f;
    for (int j = 0; j <= nv; ++j) {
        const float v = static_cast<float>(j) / nv;
        // Nos polos a derivada em u se anula: a normal é estimada um pouco para dentro.
        const float vn = std::clamp(v, 2 * h, 1 - 2 * h);
        for (int i = 0; i <= nu; ++i) {
            const float u = static_cast<float>(i) / nu;
            const V3 p = P(u, v);
            const V3 du = P(u + h, vn) - P(u - h, vn);
            const V3 dv = P(u, vn + h) - P(u, vn - h);
            const V3 n = normalizar(cross(du, dv));
            m.vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, u * repU, v * repV});
        }
    }
    const int linha = nu + 1;
    m.indices.reserve(static_cast<size_t>(nu) * nv * 6);
    for (int j = 0; j < nv; ++j) {
        for (int i = 0; i < nu; ++i) {
            const std::uint32_t a = j * linha + i, b = a + 1;          // (i, j), (i+1, j)
            const std::uint32_t d = (j + 1) * linha + i, c = d + 1;    // (i, j+1), (i+1, j+1)
            if (!(polos && j == 0)) m.indices.insert(m.indices.end(), {a, b, c});
            if (!(polos && j == nv - 1)) m.indices.insert(m.indices.end(), {a, c, d});
        }
    }
    return m;
}

}  // namespace

// ---------------------------------------------------------------------------
// Classe Malha
// ---------------------------------------------------------------------------
Malha::Malha(Malha&& o) noexcept { *this = std::move(o); }

Malha& Malha::operator=(Malha&& o) noexcept {
    if (this != &o) {
        liberarGPU();
        vertices = std::move(o.vertices);
        indices = std::move(o.indices);
        vbo_ = o.vbo_;
        ibo_ = o.ibo_;
        o.vbo_ = o.ibo_ = 0;
    }
    return *this;
}

Malha::~Malha() { liberarGPU(); }

std::size_t Malha::bytesGPU() const {
    return vertices.size() * sizeof(Vertice) + indices.size() * sizeof(std::uint32_t);
}

void Malha::enviarParaGPU() {
    liberarGPU();
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ARRAY_BUFFER_X, vbo_);
    glBufferData(GL_ARRAY_BUFFER_X, static_cast<GLsizeiptrX>(vertices.size() * sizeof(Vertice)),
                 vertices.data(), GL_STATIC_DRAW_X);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER_X, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER_X,
                 static_cast<GLsizeiptrX>(indices.size() * sizeof(std::uint32_t)), indices.data(),
                 GL_STATIC_DRAW_X);
    glBindBuffer(GL_ARRAY_BUFFER_X, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER_X, 0);
}

void Malha::liberarGPU() {
    if (vbo_ && glDeleteBuffers) glDeleteBuffers(1, &vbo_);
    if (ibo_ && glDeleteBuffers) glDeleteBuffers(1, &ibo_);
    vbo_ = ibo_ = 0;
}

void Malha::vincular(bool usarVBO) const {
    if (!usarVBO) return;  // o modo imediato não usa estado de vértices
    const GLsizei passo = sizeof(Vertice);
    glBindBuffer(GL_ARRAY_BUFFER_X, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER_X, ibo_);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, passo, reinterpret_cast<const void*>(offsetof(Vertice, px)));
    glNormalPointer(GL_FLOAT, passo, reinterpret_cast<const void*>(offsetof(Vertice, nx)));
    glTexCoordPointer(2, GL_FLOAT, passo, reinterpret_cast<const void*>(offsetof(Vertice, u)));
}

void Malha::desenharVinculada(bool usarVBO) const {
    if (usarVBO) {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
        return;
    }
    // Modo imediato: 3 chamadas glTexCoord/glNormal/glVertex por vértice de cada triângulo.
    glBegin(GL_TRIANGLES);
    for (const std::uint32_t k : indices) {
        const Vertice& p = vertices[k];
        glTexCoord2f(p.u, p.v);
        glNormal3f(p.nx, p.ny, p.nz);
        glVertex3f(p.px, p.py, p.pz);
    }
    glEnd();
}

void Malha::desvincular(bool usarVBO) const {
    if (!usarVBO) return;
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER_X, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER_X, 0);
}

// ---------------------------------------------------------------------------
// Geradores
// ---------------------------------------------------------------------------
std::uint64_t triangulosOrbe(int T) {
    return 4ull * static_cast<std::uint64_t>(T) * static_cast<std::uint64_t>(T - 1);
}

Malha gerarOrbe(int T, float amplitudeRelevo) {
    T = std::max(T, 2);
    // u -> longitude (0..2pi), v -> colatitude (0 = polo norte, pi = polo sul)
    return superficie(2 * T, T, true, 2.0f, 1.0f, [amplitudeRelevo](float u, float v) {
        const float th = 2 * PI * u, ph = PI * v;
        // Relevo em gomos: 8 ondas na longitude, modulado por sin(ph) para se anular nos polos
        const float r = 1.0f + amplitudeRelevo * std::sin(8 * th) * std::sin(ph) * std::sin(ph) +
                        0.5f * amplitudeRelevo * std::sin(5 * ph);
        return V3{r * std::sin(ph) * std::cos(th), r * std::cos(ph), r * std::sin(ph) * std::sin(th)};
    });
}

Malha gerarNoToroidal(int segmentos, int lados) {
    const float p = 2, q = 3, R = 1.0f, r = 0.42f, tubo = 0.16f;
    auto curva = [=](float t) {
        const float rr = R + r * std::cos(q * t);
        return V3{rr * std::cos(p * t), r * std::sin(q * t), rr * std::sin(p * t)};
    };
    return superficie(segmentos, lados, false, 12.0f, 1.0f, [=](float u, float v) {
        const float t = 2 * PI * u, a = -2 * PI * v, e = 1e-3f;
        // Referencial de Frenet obtido por diferenças finitas da curva
        const V3 c = curva(t), c1 = curva(t + e), c0 = curva(t - e);
        const V3 tan = normalizar(c1 - c0);
        const V3 acel = c1 - c * 2.0f + c0;
        const V3 nor = normalizar(acel - tan * dot(acel, tan));
        const V3 bin = cross(tan, nor);
        return c + (nor * std::cos(a) + bin * std::sin(a)) * tubo;
    });
}

Malha gerarPiso(int divisoes, float tamanho) {
    Malha m;
    const int n = divisoes;
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            const float x = -tamanho + 2 * tamanho * i / n;
            const float z = -tamanho + 2 * tamanho * j / n;
            m.vertices.push_back({x, 0, z, 0, 1, 0, x / 4.0f, z / 4.0f});
        }
    }
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const std::uint32_t a = j * (n + 1) + i, d = a + 1;           // (i,j), (i+1,j)
            const std::uint32_t b = (j + 1) * (n + 1) + i, c = b + 1;     // (i,j+1), (i+1,j+1)
            m.indices.insert(m.indices.end(), {a, b, c, a, c, d});        // normal para +y
        }
    }
    return m;
}
