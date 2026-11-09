/*
 * cena.cpp
 * Construção, animação e desenho da cena (ver cena.h).
 */
#include "cena.h"

#include <GL/freeglut.h>

#include <algorithm>
#include <cmath>

namespace {

const float PI = 3.14159265358979f;
const float RAIO_INTERNO = 3.2f;   // espaço livre ao redor do nó central
const float RAIO_EXTERNO = 10.0f;  // raio do campo de orbes
const int DIVISOES_PISO = 48;
const int SEGMENTOS_NO = 256, LADOS_NO = 24;
const int T_LAMPADA = 12;
const int TAMANHO_TEXTURA = 1024;

void hsvParaRgb(float h, float s, float v, float& r, float& g, float& b) {
    const float i = std::floor(h * 6), f = h * 6 - i;
    const float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (static_cast<int>(i) % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = q; break;
    }
}

// Transforma um ponto (w = 1) ou uma direção (w = 0) pela matriz 4x4 (ordem de colunas do OpenGL)
void transformar(const float M[16], const float in[3], float w, float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = M[i] * in[0] + M[4 + i] * in[1] + M[8 + i] * in[2] + M[12 + i] * w;
}

}  // namespace

// ---------------------------------------------------------------------------
// Configurações A..F
// ---------------------------------------------------------------------------
Iluminacao iluminacaoDaConfig(char c) {
    switch (c) {
        case 'B': case 'E': return Iluminacao::Omnidirecional;
        case 'C': case 'F': return Iluminacao::Spot;
        default: return Iluminacao::Nenhuma;
    }
}

bool texturaDaConfig(char c) { return c == 'D' || c == 'E' || c == 'F'; }

char configDe(Iluminacao luz, bool textura) {
    static const char tabela[3][2] = {{'A', 'D'}, {'B', 'E'}, {'C', 'F'}};
    return tabela[static_cast<int>(luz)][textura ? 1 : 0];
}

std::string nomeIluminacao(Iluminacao luz) {
    switch (luz) {
        case Iluminacao::Omnidirecional: return "omnidirecional";
        case Iluminacao::Spot: return "spot";
        default: return "sem iluminacao";
    }
}

std::string descreverConfig(char c) {
    return std::string(1, c) + " (" + nomeIluminacao(iluminacaoDaConfig(c)) + ", " +
           (texturaDaConfig(c) ? "com textura" : "sem textura") + ")";
}

// ---------------------------------------------------------------------------
// Cena
// ---------------------------------------------------------------------------
bool Cena::inicializar(std::string& erro) {
    if (!criarProgramas(programas_, erro)) return false;
    texPiso_ = criarTexturaPiso(TAMANHO_TEXTURA);
    texMarmore_ = criarTexturaMarmore(TAMANHO_TEXTURA);
    texListras_ = criarTexturaListras(TAMANHO_TEXTURA);
    piso_ = gerarPiso(DIVISOES_PISO, RAIO_EXTERNO + 1.5f);
    piso_.enviarParaGPU();
    no_ = gerarNoToroidal(SEGMENTOS_NO, LADOS_NO);
    no_.enviarParaGPU();
    lampada_ = gerarOrbe(T_LAMPADA, 0.0f);
    lampada_.enviarParaGPU();
    const int T = estado_.T;
    estado_.T = -1;  // força a geração
    definirDetalhe(T);
    definirObjetos(estado_.N);
    return true;
}

void Cena::finalizar() {
    destruirProgramas(programas_);
    for (Textura* t : {&texPiso_, &texMarmore_, &texListras_})
        if (t->id) glDeleteTextures(1, &t->id), t->id = 0;
    orbe_.liberarGPU();
    piso_.liberarGPU();
    no_.liberarGPU();
    lampada_.liberarGPU();
}

void Cena::definirConfig(char c) {
    if (c >= 'A' && c <= 'F') estado_.config = c;
}

void Cena::definirDetalhe(int T) {
    T = std::clamp(T, T_MIN, T_MAX);
    if (T == estado_.T) return;
    estado_.T = T;
    orbe_ = gerarOrbe(T);
    orbe_.enviarParaGPU();
    // A cópia na RAM continua necessária para o modo imediato.
}

void Cena::definirObjetos(int N) {
    N = std::clamp(N, N_MIN, N_MAX);
    estado_.N = N;
    orbes_.clear();
    // Espiral filotáxica: ângulo de ouro entre orbes consecutivos e raio ~ sqrt(i), o que
    // distribui N orbes com densidade uniforme no anel [RAIO_INTERNO, RAIO_EXTERNO].
    const float anguloOuro = PI * (3.0f - std::sqrt(5.0f));
    const float area = PI * (RAIO_EXTERNO * RAIO_EXTERNO - RAIO_INTERNO * RAIO_INTERNO) / N;
    const float escala = std::min(0.9f, 0.42f * std::sqrt(area));  // orbes menores quando N cresce
    for (int i = 0; i < N; ++i) {
        const float raio = std::sqrt(RAIO_INTERNO * RAIO_INTERNO +
                                     (RAIO_EXTERNO * RAIO_EXTERNO - RAIO_INTERNO * RAIO_INTERNO) * (i + 0.5f) / N);
        const float ang = i * anguloOuro;
        Orbe o{raio * std::cos(ang), raio * std::sin(ang), escala, 0.7f * i, 0, 0, 0};
        hsvParaRgb(std::fmod(i * 0.618034f, 1.0f), 0.50f, 0.95f, o.r, o.g, o.b);
        orbes_.push_back(o);
    }
}

std::uint64_t Cena::triangulosPara(int T, int N) {
    const std::uint64_t cenario = 2ull * DIVISOES_PISO * DIVISOES_PISO + 2ull * SEGMENTOS_NO * LADOS_NO +
                                  triangulosOrbe(T_LAMPADA);
    return cenario + static_cast<std::uint64_t>(N) * triangulosOrbe(T);
}

std::uint64_t Cena::triangulosCenario() const {
    return piso_.triangulos() + no_.triangulos() + lampada_.triangulos();
}

std::uint64_t Cena::triangulosPorQuadro() const {
    return triangulosCenario() + static_cast<std::uint64_t>(estado_.N) * orbe_.triangulos();
}

double Cena::memoriaGeometriaMB() const {
    return (orbe_.bytesGPU() + piso_.bytesGPU() + no_.bytesGPU() + lampada_.bytesGPU()) / 1048576.0;
}

double Cena::memoriaTexturasMB() const {
    return (texPiso_.bytes + texMarmore_.bytes + texListras_.bytes) / 1048576.0;
}

void Cena::desenhar(double tempo, int largura, int altura) {
    const float t = static_cast<float>(tempo);
    const bool vbo = estado_.usarVBO;
    const Iluminacao luz = iluminacaoDaConfig(estado_.config);
    const bool tex = texturaDaConfig(estado_.config);
    const ProgramaCena& prog = programas_[static_cast<int>(luz)][tex ? 1 : 0];

    glViewport(0, 0, largura, altura);
    glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);  // descarta faces traseiras (após o processamento de vértices)
    glCullFace(GL_BACK);
    glPolygonMode(GL_FRONT_AND_BACK, estado_.aramado ? GL_LINE : GL_FILL);

    // Câmera fixa
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(50.0, static_cast<double>(largura) / std::max(altura, 1), 0.5, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0.0, 12.5, 19.5, 0.0, 0.8, 0.0, 0.0, 1.0, 0.0);
    float V[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, V);

    // Fontes de luz (coordenadas do mundo), animadas
    //  - omnidirecional: orbita o centro a 6,5 unidades de raio e 4,5 de altura;
    //  - spot: a 7 unidades de altura, varrendo o piso com um cone de 26 graus.
    const float luzPonto[3] = {6.5f * std::cos(0.6f * t), 4.5f, 6.5f * std::sin(0.6f * t)};
    const float spotPos[3] = {2.5f * std::cos(0.25f * t), 7.0f, 2.5f * std::sin(0.25f * t)};
    const float alvo[3] = {6.0f * std::cos(0.45f * t + 1.0f), 0.0f, 6.0f * std::sin(0.45f * t + 1.0f)};
    const float spotDir[3] = {alvo[0] - spotPos[0], alvo[1] - spotPos[1], alvo[2] - spotPos[2]};
    const float* posLuz = (luz == Iluminacao::Spot) ? spotPos : luzPonto;
    float luzOlho[3], dirOlho[3];
    transformar(V, posLuz, 1.0f, luzOlho);
    transformar(V, spotDir, 0.0f, dirOlho);

    glUseProgram(prog.id);
    glUniform1i(prog.uTextura, 0);
    glUniform3f(prog.uLuzPos, luzOlho[0], luzOlho[1], luzOlho[2]);
    glUniform3f(prog.uSpotDir, dirOlho[0], dirOlho[1], dirOlho[2]);
    glUniform1f(prog.uSpotCosExterno, std::cos(26.0f * PI / 180.0f));
    glUniform1f(prog.uSpotCosInterno, std::cos(18.0f * PI / 180.0f));
    glUniform1f(prog.uEmissivo, 0.0f);

    // 1) Piso
    glBindTexture(GL_TEXTURE_2D, tex ? texPiso_.id : 0);
    glColor3f(0.85f, 0.85f, 0.85f);
    piso_.vincular(vbo);
    piso_.desenharVinculada(vbo);
    piso_.desvincular(vbo);

    // 2) Nó toroidal central (animação: rotação em dois eixos)
    glBindTexture(GL_TEXTURE_2D, tex ? texListras_.id : 0);
    glColor3f(1.0f, 0.78f, 0.32f);
    glPushMatrix();
    glTranslatef(0.0f, 2.4f + 0.25f * std::sin(1.3f * t), 0.0f);
    glRotatef(30.0f * t, 0, 1, 0);
    glRotatef(17.0f * t, 1, 0, 0);
    glScalef(1.7f, 1.7f, 1.7f);
    no_.vincular(vbo);
    no_.desenharVinculada(vbo);
    no_.desvincular(vbo);
    glPopMatrix();

    // 3) Orbes (carrossel + rotação própria + oscilação vertical), uma chamada por orbe
    glBindTexture(GL_TEXTURE_2D, tex ? texMarmore_.id : 0);
    orbe_.vincular(vbo);
    glPushMatrix();
    glRotatef(8.0f * t, 0, 1, 0);
    for (const Orbe& o : orbes_) {
        glPushMatrix();
        glTranslatef(o.x, o.escala * (1.15f + 0.25f * std::sin(2.0f * t + o.fase)), o.z);
        glRotatef(40.0f * t + 57.0f * o.fase, 0, 1, 0);
        glScalef(o.escala, o.escala, o.escala);
        glColor3f(o.r, o.g, o.b);
        orbe_.desenharVinculada(vbo);
        glPopMatrix();
    }
    glPopMatrix();
    orbe_.desvincular(vbo);

    // 4) Lâmpada: esfera emissiva na posição da luz (desenhada em todas as configurações,
    //    para que a contagem de triângulos seja idêntica entre A..F)
    glBindTexture(GL_TEXTURE_2D, 0);
    glUniform1f(prog.uEmissivo, 1.0f);
    glColor3f(1.0f, 0.96f, 0.70f);
    glPushMatrix();
    glTranslatef(posLuz[0], posLuz[1], posLuz[2]);
    glScalef(0.28f, 0.28f, 0.28f);
    lampada_.vincular(vbo);
    lampada_.desenharVinculada(vbo);
    lampada_.desvincular(vbo);
    glPopMatrix();

    glUseProgram(0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}
