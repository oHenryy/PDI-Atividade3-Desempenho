/*
 * main.cpp
 * Atividade 3 - Análise de desempenho de CPU e GPU utilizando OpenGL
 *
 * Aplicação única com a cena "Jardim de Orbes" (ver cena.h), renderizada continuamente,
 * com medição de FPS, contagem de triângulos por quadro, seis configurações de iluminação e
 * textura (A..F), monitoramento de CPU/GPU (monitor.h) e protocolo automático de busca do
 * limite de 60 FPS (benchmark.h).
 *
 * Teclado (também resumido no painel da janela):
 *   1..6 ............ configuração A..F          l ......... alterna iluminação (nenhuma/omni/spot)
 *   t ............... liga/desliga texturas      + / - ..... detalhe T dos orbes (passo 4 ou 16)
 *   ] / [ ........... detalhe T +1 / -1          * / / ..... número de orbes x2 / /2
 *   v ............... VBO <-> modo imediato      s ......... liga/desliga V-Sync
 *   w ............... desenho em arame           p ......... pausa/retoma a animação
 *   h ............... mostra/oculta o painel     c ......... salva captura de tela
 *   m ............... mede o ponto atual (3 x 10 s) e grava em resultados/dados/manual_*.csv
 *   r ou Home ....... restaura a configuração inicial
 *   ESC ou q ........ encerra
 *
 * Linha de comando:
 *   desempenho_gl                         modo interativo
 *   desempenho_gl --info                  imprime o ambiente (CPU, GPUs, OpenGL) e encerra
 *   desempenho_gl --capturar              gera as capturas do relatório em resultados/capturas
 *   desempenho_gl --benchmark [opções]    executa o protocolo automático e encerra
 *       --rotulo NOME       prefixo dos arquivos CSV            (padrão: vbo_T)
 *       --modo vbo|imediato caminho de envio da geometria       (padrão: vbo)
 *       --variavel T|N      parâmetro que aumenta a carga        (padrão: T)
 *       --valores a,b,c...  lista grossa de valores crescentes
 *       --configs ABCDEF    configurações a medir
 *       --T n / --N n       valor fixo do outro parâmetro        (padrão: T=8, N=64)
 *       --estab s --medir s --reps n --parada fps --saida pasta
 *   --vsync liga o V-Sync (por padrão é desligado para não limitar o FPS).
 */
#include "benchmark.h"
#include "cena.h"
#include "gl_ext.h"
#include "monitor.h"

#include <GL/freeglut.h>

#ifdef _WIN32
#include <intrin.h>
#endif

#ifdef TEM_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Em notebooks com duas GPUs (NVIDIA Optimus / AMD PowerXpress), exportar estes símbolos pede
// ao driver que crie o contexto OpenGL na GPU dedicada. O alvo "desempenho_gl_integrada" é
// compilado SEM estes símbolos, para permitir a comparação com a GPU integrada.
#if defined(_WIN32) && defined(PREFERIR_GPU_DEDICADA)
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
static const bool PEDE_GPU_DEDICADA = true;
#else
static const bool PEDE_GPU_DEDICADA = false;
#endif

// ---------------------------------------------------------------------------
// Configuração
// ---------------------------------------------------------------------------
static const int LARGURA = 1280;  // resolução fixa de todos os experimentos
static const int ALTURA = 720;
static const int T_INICIAL = 32, N_INICIAL = 64;
static const char* DIR_CAPTURAS = "resultados/capturas";
static const int NUM_CONSULTAS = 16;  // conjunto de consultas GL_TIME_ELAPSED em uso simultâneo

// ---------------------------------------------------------------------------
// Estado global
// ---------------------------------------------------------------------------
static Cena cena;
static Monitor monitor;
static AcumuladorQuadros quadrosBench, quadrosPainel;
static ResultadoQuadros painel;  // FPS exibido (atualizado a cada 0,5 s)
static std::unique_ptr<Benchmark> benchmark;
static bool sairAoTerminarBenchmark = false;

static int janelaL = LARGURA, janelaA = ALTURA;
static bool mostrarPainel = true, pausado = false, vsync = false;
static double tempoAnim = 0, ultimoTempo = 0, ultimoConsole = 0;
static std::string rendererGL, vendorGL, versaoGL, versaoGLSL, ambiente;

static GLuint consultas[NUM_CONSULTAS] = {};
struct ConsultaPendente {
    int id;                 // índice em consultas[]
    std::uint64_t quadro;   // quadro em que foi emitida
};
static std::deque<ConsultaPendente> consultasPendentes;  // aguardando resultado (ordem de emissão)
static const std::uint64_t IDADE_MIN_CONSULTA = 3;  // só consulta resultados de >= 3 quadros atrás
static double ciclosPorMs = 0;  // frequência do contador de ciclos (TSC), calibrada na inicialização
static std::vector<int> consultasLivres;
static std::uint64_t numQuadro = 0;

static std::string capturaPendente;  // nome do arquivo a gravar no próximo quadro
static int contadorCapturas = 0;

// Modo --capturar: sequência de tomadas com a animação congelada no mesmo instante
struct Tomada {
    std::string nome;
    char config;
    int T, N;
    bool aramado;
};
static std::vector<Tomada> tomadas;
static size_t idxTomada = 0;
static double tTomada = 0;
static bool aguardandoGravacao = false;
static const double TEMPO_ANIM_CAPTURA = 2.6;

// Ciclos de CPU efetivamente executados pela thread atual (não conta o tempo bloqueado/esperando)
static std::uint64_t ciclosThread() {
#ifdef _WIN32
    ULONG64 c = 0;
    QueryThreadCycleTime(GetCurrentThread(), &c);
    return c;
#else
    return 0;
#endif
}

// QueryThreadCycleTime conta em ciclos do contador de tempo do processador (TSC), de frequência
// constante; ela é estimada comparando __rdtsc com o relógio de alta resolução por 200 ms.
static void calibrarCiclos() {
#ifdef _WIN32
    const auto r0 = std::chrono::steady_clock::now();
    const unsigned long long c0 = __rdtsc();
    while (std::chrono::steady_clock::now() - r0 < std::chrono::milliseconds(200)) {
    }
    const unsigned long long c1 = __rdtsc();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r0).count();
    ciclosPorMs = (c1 - c0) / ms;
#endif
}

static double agora() {
    static const auto inicio = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - inicio).count();
}

// ---------------------------------------------------------------------------
// Ambiente (registrado junto com os resultados)
// ---------------------------------------------------------------------------
static std::string descreverAmbiente() {
    std::ostringstream s;
    char linha[256];
    s << "Data/hora: " << agoraTexto() << "\n";
    s << "Sistema operacional: " << versaoWindows() << "\n";
    s << "CPU: " << nomeCPU() << " | " << nucleosFisicos() << " nucleos fisicos / " << processadoresLogicos()
      << " processadores logicos\n";
    std::snprintf(linha, sizeof(linha), "RAM: %.1f GB\n", memoriaRAMTotalGB());
    s << linha;
    s << "Adaptadores graficos (DXGI):\n";
    for (const auto& a : monitor.adaptadores()) {
        std::snprintf(linha, sizeof(linha), "  - %s | fabricante 0x%04X | VRAM dedicada %.0f MB | compartilhada %.0f MB | %s\n",
                      a.nome.c_str(), a.fabricante, a.vramDedicadaMB, a.memoriaCompartilhadaMB,
                      a.integrada() ? "integrada" : "dedicada");
        s << linha;
    }
    s << "OpenGL: " << versaoGL << "\n";
    s << "GLSL: " << versaoGLSL << "\n";
    s << "GL_VENDOR: " << vendorGL << "\n";
    s << "GL_RENDERER (dispositivo que atende a aplicacao): " << rendererGL << "\n";
    s << "Executavel pede GPU dedicada (NvOptimusEnablement): " << (PEDE_GPU_DEDICADA ? "sim" : "nao") << "\n";
    s << "V-Sync (intervalo de troca): " << consultarVSync()
      << (consultarVSync() == 0 ? " (desligado)" : consultarVSync() > 0 ? " (ligado)" : " (desconhecido)") << "\n";
    s << "Janela: " << janelaL << " x " << janelaA << " pixels\n";
    s << "Contador de ciclos da CPU (TSC): " << static_cast<int>(ciclosPorMs / 1000.0) << " MHz\n";
    s << "Consulta de tempo da GPU (GL_TIME_ELAPSED): " << (temConsultaTempoGPU() ? "sim" : "nao") << "\n";
    s << "Filtragem anisotropica: " << (temAnisotropia() ? "sim (8x)" : "nao") << "\n";
    s << "NVML: " << (monitor.temNVML() ? monitor.nomeGPUNVML() : std::string("indisponivel")) << "\n";
    std::snprintf(linha, sizeof(linha), "Texturas: 3 x 1024x1024 RGBA8 + mipmaps (%.1f MB)\n", cena.memoriaTexturasMB());
    s << linha;
    std::snprintf(linha, sizeof(linha), "Triangulos do cenario fixo (piso + no + lampada): %llu\n",
                  static_cast<unsigned long long>(cena.triangulosCenario()));
    s << linha;
    return s.str();
}

// ---------------------------------------------------------------------------
// Captura de tela (PNG com OpenCV; PPM sem ele)
// ---------------------------------------------------------------------------
static bool salvarCaptura(const std::string& caminhoSemExtensao) {
    std::filesystem::create_directories(std::filesystem::path(caminhoSemExtensao).parent_path());
    std::vector<unsigned char> px(static_cast<size_t>(janelaL) * janelaA * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, janelaL, janelaA, GL_RGB, GL_UNSIGNED_BYTE, px.data());
#ifdef TEM_OPENCV
    cv::Mat rgb(janelaA, janelaL, CV_8UC3, px.data()), bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);  // OpenGL: R,G,B | imwrite: B,G,R
    cv::flip(bgr, bgr, 0);                      // OpenGL: origem no canto inferior esquerdo
    const bool ok = cv::imwrite(caminhoSemExtensao + ".png", bgr);
#else
    std::ofstream f(caminhoSemExtensao + ".ppm", std::ios::binary);
    f << "P6\n" << janelaL << " " << janelaA << "\n255\n";
    for (int y = janelaA - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(&px[static_cast<size_t>(y) * janelaL * 3]), janelaL * 3);
    const bool ok = f.good();
#endif
    std::printf("Captura %s: %s\n", ok ? "salva" : "FALHOU", caminhoSemExtensao.c_str());
    return ok;
}

// ---------------------------------------------------------------------------
// Painel de informações (texto ASCII com fonte bitmap do GLUT)
// ---------------------------------------------------------------------------
static void texto(int x, int y, const std::string& s) {
    glRasterPos2i(x, y);
    glutBitmapString(GLUT_BITMAP_9_BY_15, reinterpret_cast<const unsigned char*>(s.c_str()));
}

static void desenharPainel() {
    const EstadoCena& e = cena.estado();
    const Metricas m = monitor.ultima();
    std::vector<std::string> linhas;
    char b[320];
    std::snprintf(b, sizeof(b), "Config %s | %s | V-Sync %s | %dx%d%s", descreverConfig(e.config).c_str(),
                  e.usarVBO ? "VBO" : "modo imediato", vsync ? "LIGADO" : "desligado", janelaL, janelaA,
                  pausado ? " | PAUSADO" : "");
    linhas.push_back(b);
    std::snprintf(b, sizeof(b), "FPS %.1f | quadro %.2f ms | emissao %.2f + swap %.2f ms | CPU ativa %.2f ms | GPU %.2f ms",
                  painel.fps, painel.quadroMs, painel.submissaoMs, painel.swapMs, painel.cpuAtivaMs, painel.gpuMs);
    linhas.push_back(b);
    std::snprintf(b, sizeof(b), "Triangulos/quadro %s | %d orbes x T=%d (%s tri/orbe) | chamadas %d",
                  comMilhares(cena.triangulosPorQuadro()).c_str(), e.N, e.T,
                  comMilhares(cena.triangulosPorOrbe()).c_str(), cena.chamadasDesenhoPorQuadro());
    linhas.push_back(b);
    std::snprintf(b, sizeof(b), "CPU: processo %.1f%% | thread render %.0f%% | thread mais ocupada %.0f%% (de 1 nucleo) | sistema %.0f%%",
                  m.cpuProcesso, m.cpuThreadRender, m.cpuThreadMax, m.cpuSistema);
    linhas.push_back(b);
    std::snprintf(b, sizeof(b), "GPU 3D (processo): NVIDIA %.0f%% | Intel %.0f%%   NVML: %.0f%% %.0f W %.0f C %.0f MHz",
                  m.gpu3dProcesso[GPU_NVIDIA], m.gpu3dProcesso[GPU_INTEL], m.nvUtil, m.nvPotenciaW, m.nvTempC, m.nvClockSM);
    linhas.push_back(b);
    linhas.push_back("Renderer: " + rendererGL);
    if (benchmark)
        linhas.push_back(benchmark->status());
    else
        linhas.push_back("1-6 config | l luz | t textura | +/- [ ] detalhe | * / orbes | v VBO | s vsync | m medir | c captura | r reset");

    const int alturaLinha = 17, margem = 8;
    const int altura = static_cast<int>(linhas.size()) * alturaLinha + 2 * margem;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, janelaL, 0, janelaA, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.62f);
    glBegin(GL_QUADS);  // fundo do painel (não contado: é interface, não cena)
    glVertex2i(0, janelaA - altura);
    glVertex2i(janelaL, janelaA - altura);
    glVertex2i(janelaL, janelaA);
    glVertex2i(0, janelaA);
    glEnd();
    glDisable(GL_BLEND);
    for (size_t i = 0; i < linhas.size(); ++i) {
        if (i == 1) glColor3f(1.0f, 0.9f, 0.3f);  // linha do FPS em destaque
        else glColor3f(0.92f, 0.92f, 0.92f);
        texto(margem, janelaA - margem - alturaLinha * static_cast<int>(i + 1) + 4, linhas[i]);
    }
}

// ---------------------------------------------------------------------------
// Laço de quadros
// ---------------------------------------------------------------------------
static void finalizarAplicacao() {
    monitor.parar();
    glutLeaveMainLoop();
}

static void aplicarTomada(const Tomada& t) {
    cena.definirConfig(t.config);
    cena.definirDetalhe(t.T);
    cena.definirObjetos(t.N);
    cena.definirAramado(t.aramado);
}

static void display() {
    const double t0 = agora();
    if (!pausado && tomadas.empty()) tempoAnim += t0 - ultimoTempo;
    ultimoTempo = t0;

    const std::uint64_t ciclos0 = ciclosThread();

    // Tempo de GPU: recolhe (sem bloquear) os resultados já prontos das consultas de quadros
    // anteriores, na ordem em que foram emitidas. A GPU executa com alguns quadros de atraso
    // em relação à CPU; consultas muito recentes não são verificadas, porque perguntar pela
    // disponibilidade de uma consulta ainda não enviada força o driver a esvaziar (flush) a
    // fila de comandos, o que alteraria o próprio desempenho medido.
    DadosQuadro d;
    int consultaAtual = -1;
    if (temConsultaTempoGPU()) {
        while (!consultasPendentes.empty() && consultasPendentes.front().quadro + IDADE_MIN_CONSULTA <= numQuadro) {
            const int id = consultasPendentes.front().id;
            GLint disponivel = 0;
            glGetQueryObjectiv(consultas[id], GL_QUERY_RESULT_AVAILABLE_X, &disponivel);
            if (!disponivel) break;
            std::uint64_t ns = 0;
            glGetQueryObjectui64v(consultas[id], GL_QUERY_RESULT_X, &ns);
            d.gpuSomaMs += ns / 1.0e6;
            ++d.gpuAmostras;
            consultasPendentes.pop_front();
            consultasLivres.push_back(id);
        }
        if (!consultasLivres.empty()) {  // sem consulta livre, este quadro não é cronometrado
            consultaAtual = consultasLivres.back();
            consultasLivres.pop_back();
            glBeginQuery(GL_TIME_ELAPSED_X, consultas[consultaAtual]);
        }
    }

    cena.desenhar(tomadas.empty() ? tempoAnim : TEMPO_ANIM_CAPTURA, janelaL, janelaA);
    if (mostrarPainel) desenharPainel();

    if (consultaAtual >= 0) {
        glEndQuery(GL_TIME_ELAPSED_X);
        consultasPendentes.push_back({consultaAtual, numQuadro});
    }
    const double t1 = agora();  // fim da emissão dos comandos pela CPU

    if (!capturaPendente.empty()) {
        salvarCaptura(capturaPendente);
        capturaPendente.clear();
    }
    glutSwapBuffers();
    const double t2 = agora();
    ++numQuadro;

    d.submissaoMs = (t1 - t0) * 1000.0;
    d.swapMs = (t2 - t1) * 1000.0;
    d.cpuAtivaMs = ciclosPorMs > 0 ? (ciclosThread() - ciclos0) / ciclosPorMs : 0;
    quadrosBench.adicionar(t2, d);
    quadrosPainel.adicionar(t2, d);
    if (t2 - quadrosPainel.inicio() >= 0.5) {
        painel = quadrosPainel.resultado();
        quadrosPainel.reiniciar(t2);
    }

    if (benchmark) {
        if (!benchmark->atualizar(t2)) {
            benchmark.reset();
            if (sairAoTerminarBenchmark) finalizarAplicacao();
        }
    } else if (!tomadas.empty()) {
        // Modo --capturar: espera 1,5 s em cada tomada (FPS do painel estabilizado), pede a
        // gravação (feita no quadro seguinte, antes do SwapBuffers) e então passa à próxima.
        if (aguardandoGravacao && capturaPendente.empty()) {
            aguardandoGravacao = false;
            if (++idxTomada >= tomadas.size()) {
                finalizarAplicacao();
                return;
            }
            aplicarTomada(tomadas[idxTomada]);
            tTomada = t2;
        } else if (!aguardandoGravacao && t2 - tTomada >= 1.5) {
            capturaPendente = std::string(DIR_CAPTURAS) + "/" + tomadas[idxTomada].nome;
            aguardandoGravacao = true;
        }
    } else if (t2 - ultimoConsole >= 2.0) {
        ultimoConsole = t2;
        const Metricas m = monitor.ultima();
        std::printf("[%c] T=%d N=%d tri=%s | %.1f FPS (%.2f ms) | GPU %.2f ms | CPU %.1f%% (thread %.0f%%) | "
                    "GPU nv %.0f%% intel %.0f%%\n",
                    cena.estado().config, cena.estado().T, cena.estado().N, comMilhares(cena.triangulosPorQuadro()).c_str(),
                    painel.fps, painel.quadroMs, painel.gpuMs, m.cpuProcesso, m.cpuThreadRender,
                    m.gpu3dProcesso[GPU_NVIDIA], m.gpu3dProcesso[GPU_INTEL]);
    }
}

static void ocioso() { glutPostRedisplay(); }

static void redimensionar(int l, int a) {
    janelaL = std::max(l, 1);
    janelaA = std::max(a, 1);
    // Os experimentos exigem resolução constante: durante o benchmark a janela é restaurada.
    if ((benchmark || !tomadas.empty()) && (l != LARGURA || a != ALTURA)) glutReshapeWindow(LARGURA, ALTURA);
}

// ---------------------------------------------------------------------------
// Teclado
// ---------------------------------------------------------------------------
static void restaurar() {
    cena.definirConfig('A');
    cena.definirDetalhe(T_INICIAL);
    cena.definirObjetos(N_INICIAL);
    cena.definirVBO(true);
    cena.definirAramado(false);
    vsync = false;
    definirVSync(false);
    pausado = false;
    mostrarPainel = true;
    std::printf("Configuracao inicial restaurada.\n");
}

static void iniciarMedicaoManual() {
    const EstadoCena& e = cena.estado();
    OpcoesBenchmark op;
    op.rotulo = "manual";
    op.configs = std::string(1, e.config);
    op.usarVBO = e.usarVBO;
    op.variavel = 'T';
    op.valores = {e.T};
    op.fixoN = e.N;
    op.apenasPontoAtual = true;
    benchmark = std::make_unique<Benchmark>(cena, monitor, quadrosBench, op, rendererGL, descreverAmbiente());
    benchmark->iniciar(agora());
}

static void teclado(unsigned char k, int, int) {
    const EstadoCena& e = cena.estado();
    if (k == 27 || k == 'q' || k == 'Q') {
        finalizarAplicacao();
        return;
    }
    if (benchmark) return;  // durante uma medição o estado da cena não pode mudar
    const int passo = e.T < 64 ? 4 : 16;
    switch (k) {
        case '1': case '2': case '3': case '4': case '5': case '6':
            cena.definirConfig(static_cast<char>('A' + (k - '1')));
            break;
        case 'l': case 'L': {
            const int luz = (static_cast<int>(iluminacaoDaConfig(e.config)) + 1) % 3;
            cena.definirConfig(configDe(static_cast<Iluminacao>(luz), texturaDaConfig(e.config)));
            break;
        }
        case 't': case 'T':
            cena.definirConfig(configDe(iluminacaoDaConfig(e.config), !texturaDaConfig(e.config)));
            break;
        case '+': case '=': cena.definirDetalhe(e.T + passo); break;
        case '-': case '_': cena.definirDetalhe(e.T - passo); break;
        case ']': cena.definirDetalhe(e.T + 1); break;
        case '[': cena.definirDetalhe(e.T - 1); break;
        case '*': cena.definirObjetos(e.N * 2); break;
        case '/': cena.definirObjetos(e.N / 2); break;
        case 'v': case 'V': cena.definirVBO(!e.usarVBO); break;
        case 's': case 'S':
            vsync = !vsync;
            std::printf("V-Sync: intervalo efetivo = %d\n", definirVSync(vsync));
            break;
        case 'w': case 'W': cena.definirAramado(!e.aramado); break;
        case 'p': case 'P': pausado = !pausado; break;
        case 'h': case 'H': mostrarPainel = !mostrarPainel; break;
        case 'c': case 'C': {
            char nome[128];
            std::snprintf(nome, sizeof(nome), "%s/captura_%c_T%d_N%d_%02d", DIR_CAPTURAS, e.config, e.T, e.N,
                          ++contadorCapturas);
            capturaPendente = nome;
            break;
        }
        case 'm': case 'M': iniciarMedicaoManual(); break;
        case 'r': case 'R': restaurar(); break;
        default: return;
    }
    const EstadoCena& n = cena.estado();
    std::printf("-> config %s | T=%d N=%d | %s triangulos/quadro | %s\n", descreverConfig(n.config).c_str(), n.T, n.N,
                comMilhares(cena.triangulosPorQuadro()).c_str(), n.usarVBO ? "VBO" : "imediato");
}

static void especial(int k, int, int) {
    if (k == GLUT_KEY_HOME && !benchmark) restaurar();
}

// ---------------------------------------------------------------------------
// Linha de comando
// ---------------------------------------------------------------------------
static std::vector<int> lerLista(const std::string& s) {
    std::vector<int> v;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) v.push_back(std::atoi(item.c_str()));
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetProcessDPIAware();  // garante que 1280x720 sejam pixels reais (sem ampliação pelo Windows)
#endif
    calibrarCiclos();
    bool modoBenchmark = false, modoCaptura = false, modoInfo = false;
    OpcoesBenchmark op;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto proximo = [&](void) -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--benchmark") modoBenchmark = true;
        else if (a == "--capturar") modoCaptura = true;
        else if (a == "--info") modoInfo = true;
        else if (a == "--vsync") vsync = true;
        else if (a == "--rotulo") op.rotulo = proximo();
        else if (a == "--modo") op.usarVBO = proximo() != "imediato";
        else if (a == "--variavel") op.variavel = static_cast<char>(std::toupper(proximo()[0]));
        else if (a == "--valores") op.valores = lerLista(proximo());
        else if (a == "--configs") op.configs = proximo();
        else if (a == "--T") op.fixoT = std::atoi(proximo().c_str());
        else if (a == "--N") op.fixoN = std::atoi(proximo().c_str());
        else if (a == "--estab") op.estabilizacao = std::atof(proximo().c_str());
        else if (a == "--medir") op.medicao = std::atof(proximo().c_str());
        else if (a == "--reps") op.repeticoes = std::atoi(proximo().c_str());
        else if (a == "--parada") op.fpsParada = std::atof(proximo().c_str());
        else if (a == "--saida") op.pastaSaida = proximo();
        else {
            std::fprintf(stderr, "Opcao desconhecida: %s\n", a.c_str());
            return 1;
        }
    }
    if (op.valores.empty())
        op.valores = op.variavel == 'T'
                         ? std::vector<int>{16, 32, 64, 96, 128, 192, 256, 320, 384, 448, 512, 640, 768, 896, 1024}
                         : std::vector<int>{64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(LARGURA, ALTURA);
    glutInitWindowPosition(40, 40);
    glutCreateWindow("Atividade 3 - Desempenho de CPU e GPU com OpenGL (Jardim de Orbes)");
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);

    std::string faltando;
    if (!carregarExtensoesGL(faltando)) {
        std::fprintf(stderr, "Funcoes OpenGL indisponiveis: %s\n", faltando.c_str());
        return 1;
    }
    vendorGL = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    rendererGL = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    versaoGL = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    versaoGLSL = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION_X));
    definirVSync(vsync);
    if (temConsultaTempoGPU()) {
        glGenQueries(NUM_CONSULTAS, consultas);
        for (int i = 0; i < NUM_CONSULTAS; ++i) consultasLivres.push_back(i);
    }

    std::string erro;
    if (!cena.inicializar(erro)) {
        std::fprintf(stderr, "Erro ao inicializar a cena: %s\n", erro.c_str());
        return 1;
    }
    cena.definirDetalhe(T_INICIAL);
    cena.definirObjetos(N_INICIAL);
    monitor.iniciar();
    ambiente = descreverAmbiente();
    std::printf("%s\n", ambiente.c_str());
    if (modoInfo) {
        monitor.parar();
        return 0;
    }

    glutDisplayFunc(display);
    glutIdleFunc(ocioso);
    glutReshapeFunc(redimensionar);
    glutKeyboardFunc(teclado);
    glutSpecialFunc(especial);

    if (modoBenchmark) {
        sairAoTerminarBenchmark = true;
        mostrarPainel = true;  // o painel faz parte do quadro em todos os experimentos
        benchmark = std::make_unique<Benchmark>(cena, monitor, quadrosBench, op, rendererGL, ambiente);
        benchmark->iniciar(agora());
    } else if (modoCaptura) {
        for (char c = 'A'; c <= 'F'; ++c) tomadas.push_back({std::string("cena_") + c, c, 48, 64, false});
        tomadas.push_back({"aramado_T8", 'A', 8, 64, true});
        tomadas.push_back({"aramado_T24", 'A', 24, 64, true});
        tomadas.push_back({"orbes_N1024_T16", 'E', 16, 1024, false});
        aplicarTomada(tomadas[0]);
        tTomada = agora() + 0.5;
    } else {
        std::printf("Modo interativo. Teclas: 1-6 config | l luz | t textura | +/- [ ] detalhe | * / orbes | "
                    "v VBO/imediato | s V-Sync | w arame | p pausa | h painel | c captura | m medir | r reset | ESC\n");
    }

    ultimoTempo = agora();
    quadrosPainel.reiniciar(ultimoTempo);
    quadrosBench.reiniciar(ultimoTempo);
    glutMainLoop();

    monitor.parar();
    if (temConsultaTempoGPU()) glDeleteQueries(NUM_CONSULTAS, consultas);
    cena.finalizar();
    return 0;
}
