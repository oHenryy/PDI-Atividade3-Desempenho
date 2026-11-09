/*
 * shaders.cpp
 * Fontes GLSL 1.20 e compilação das 6 variantes (ver shaders.h).
 *
 * Usa os atributos e matrizes "embutidos" do perfil de compatibilidade (gl_Vertex, gl_Normal,
 * gl_MultiTexCoord0, gl_Color, gl_ModelViewMatrix, gl_NormalMatrix...). Assim o mesmo shader
 * atende tanto ao desenho por VBO (glVertexPointer...) quanto ao modo imediato (glVertex...).
 */
#include "shaders.h"

#include <vector>

namespace {

const char* FONTE_VERTICES = R"GLSL(
#version 120
varying vec3 vPosicao;   // posicao em coordenadas do observador
varying vec3 vNormal;    // normal em coordenadas do observador
varying vec2 vUV;
varying vec4 vCor;
void main() {
    vec4 p = gl_ModelViewMatrix * gl_Vertex;
    vPosicao = p.xyz;
    vNormal = gl_NormalMatrix * gl_Normal;
    vUV = gl_MultiTexCoord0.xy;
    vCor = gl_Color;
    gl_Position = gl_ProjectionMatrix * p;
}
)GLSL";

// ILUMINACAO e TEXTURA são definidos antes desta fonte (ver compilarVariante)
const char* FONTE_FRAGMENTOS = R"GLSL(
varying vec3 vPosicao;
varying vec3 vNormal;
varying vec2 vUV;
varying vec4 vCor;
uniform sampler2D uTextura;
uniform vec3 uLuzPos;
uniform vec3 uSpotDir;
uniform float uSpotCosExterno;
uniform float uSpotCosInterno;
uniform float uEmissivo;

const vec3 LUZ_COR = vec3(1.0, 0.95, 0.85);
const vec3 AMBIENTE = vec3(0.10, 0.10, 0.13);

void main() {
    vec3 base = vCor.rgb;
#if TEXTURA
    base *= texture2D(uTextura, vUV).rgb;
#endif

#if ILUMINACAO == 0
    vec3 cor = base;
#else
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 L = uLuzPos - vPosicao;
    float d = length(L);
    L /= d;
    float atenuacao = 1.0 / (1.0 + 0.010 * d + 0.0020 * d * d);
#if ILUMINACAO == 2
    // Spot: intensidade maxima dentro do cone interno, decaindo suavemente ate o externo
    float cosAng = dot(-L, normalize(uSpotDir));
    atenuacao *= smoothstep(uSpotCosExterno, uSpotCosInterno, cosAng);
#endif
    vec3 V = normalize(-vPosicao);
    vec3 H = normalize(L + V);                 // vetor intermediario (Blinn-Phong)
    float difusa = max(dot(N, L), 0.0);
    float especular = difusa > 0.0 ? pow(max(dot(N, H), 0.0), 48.0) : 0.0;
    vec3 cor = base * (AMBIENTE + difusa * atenuacao * LUZ_COR) + 0.45 * especular * atenuacao * LUZ_COR;
#endif

    cor = mix(cor, vCor.rgb, uEmissivo);       // marcador da lampada: cor pura
    gl_FragColor = vec4(cor, 1.0);
}
)GLSL";

GLuint compilar(GLenum tipo, const std::string& fonte, std::string& erro) {
    const GLuint s = glCreateShader(tipo);
    const GLcharX* p = fonte.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS_X, &ok);
    if (!ok) {
        GLint tam = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH_X, &tam);
        std::vector<GLcharX> log(static_cast<size_t>(tam) + 1, 0);
        glGetShaderInfoLog(s, tam, nullptr, log.data());
        erro = std::string(tipo == GL_VERTEX_SHADER_X ? "vertex" : "fragment") + " shader: " + log.data();
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool compilarVariante(int iluminacao, int textura, ProgramaCena& prog, std::string& erro) {
    const std::string cabecalho = "#version 120\n#define ILUMINACAO " + std::to_string(iluminacao) +
                                  "\n#define TEXTURA " + std::to_string(textura) + "\n";
    const GLuint vs = compilar(GL_VERTEX_SHADER_X, FONTE_VERTICES, erro);
    if (!vs) return false;
    const GLuint fs = compilar(GL_FRAGMENT_SHADER_X, cabecalho + FONTE_FRAGMENTOS, erro);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    prog.id = glCreateProgram();
    glAttachShader(prog.id, vs);
    glAttachShader(prog.id, fs);
    glLinkProgram(prog.id);
    glDeleteShader(vs);  // continuam vivos enquanto anexados ao programa
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog.id, GL_LINK_STATUS_X, &ok);
    if (!ok) {
        GLint tam = 0;
        glGetProgramiv(prog.id, GL_INFO_LOG_LENGTH_X, &tam);
        std::vector<GLcharX> log(static_cast<size_t>(tam) + 1, 0);
        glGetProgramInfoLog(prog.id, tam, nullptr, log.data());
        erro = std::string("ligacao do programa: ") + log.data();
        return false;
    }
    prog.uTextura = glGetUniformLocation(prog.id, "uTextura");
    prog.uLuzPos = glGetUniformLocation(prog.id, "uLuzPos");
    prog.uSpotDir = glGetUniformLocation(prog.id, "uSpotDir");
    prog.uSpotCosExterno = glGetUniformLocation(prog.id, "uSpotCosExterno");
    prog.uSpotCosInterno = glGetUniformLocation(prog.id, "uSpotCosInterno");
    prog.uEmissivo = glGetUniformLocation(prog.id, "uEmissivo");
    return true;
}

}  // namespace

bool criarProgramas(ProgramaCena programas[3][2], std::string& erro) {
    for (int luz = 0; luz < 3; ++luz)
        for (int tex = 0; tex < 2; ++tex)
            if (!compilarVariante(luz, tex, programas[luz][tex], erro)) {
                erro = "variante (luz=" + std::to_string(luz) + ", textura=" + std::to_string(tex) + ") " + erro;
                return false;
            }
    return true;
}

void destruirProgramas(ProgramaCena programas[3][2]) {
    for (int luz = 0; luz < 3; ++luz)
        for (int tex = 0; tex < 2; ++tex)
            if (programas[luz][tex].id) {
                glDeleteProgram(programas[luz][tex].id);
                programas[luz][tex].id = 0;
            }
}
