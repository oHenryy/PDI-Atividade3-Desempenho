/*
 * benchmark.h
 * Medição de quadros e protocolo experimental automático.
 *
 * AcumuladorQuadros: soma, numa janela de tempo, o número de quadros e os tempos de cada
 * quadro: tempo de relógio gasto emitindo os comandos OpenGL ("emissão", pode incluir esperas
 * dentro do driver), tempo bloqueado no SwapBuffers, tempo de CPU efetivamente executado pela
 * thread principal (ciclos, exclui esperas) e tempo de GPU medido com GL_TIME_ELAPSED.
 *   FPS médio da janela = quadros / duração da janela;
 *   tempo médio por quadro = 1000 / FPS médio (ms).
 *
 * Benchmark: para cada configuração (A..F), executa o procedimento da seção 2.4:
 *   1) começa pela menor carga da lista ("varredura grossa");
 *   2) aumenta a carga progressivamente;
 *   3) em cada ponto: estabilização + medição (>= 10 s), repetida >= 3 vezes; registra FPS,
 *      tempos, CPU, GPU e memória (médias);
 *   4) ao encontrar o primeiro ponto com FPS médio < 60, faz bisseção entre o último ponto
 *      >= 60 e o primeiro < 60 até que as cargas difiram menos que a tolerância (refino);
 *   5) continua a varredura grossa até o FPS cair abaixo do limiar de parada (para mostrar a
 *      tendência) ou a lista acabar;
 *   6) registra no resumo a MENOR carga testada com FPS médio < 60 e a situação do teste.
 */
#pragma once

#include "cena.h"
#include "monitor.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Quadros
// ---------------------------------------------------------------------------
struct DadosQuadro {
    double submissaoMs = 0;  // CPU: início do quadro até o fim da emissão dos comandos
    double swapMs = 0;       // CPU bloqueada em glutSwapBuffers
    double cpuAtivaMs = 0;   // ciclos de CPU realmente executados pela thread principal no quadro
    double gpuSomaMs = 0;    // soma dos tempos de GPU (GL_TIME_ELAPSED) que ficaram prontos
    int gpuAmostras = 0;     // neste quadro (resultados de quadros anteriores)
};

struct ResultadoQuadros {
    std::uint64_t quadros = 0;
    double segundos = 0;
    double fps = 0;
    double quadroMs = 0;  // tempo médio por quadro
    double quadroMaxMs = 0;
    double submissaoMs = 0, swapMs = 0, cpuAtivaMs = 0, gpuMs = -1;
};

class AcumuladorQuadros {
public:
    void reiniciar(double agora);
    void adicionar(double agora, const DadosQuadro& d);
    ResultadoQuadros resultado() const;
    double inicio() const { return inicio_; }

private:
    double inicio_ = 0, ultimo_ = 0, anterior_ = 0;
    std::uint64_t quadros_ = 0, quadrosGpu_ = 0;
    double somaSub_ = 0, somaSwap_ = 0, somaAtiva_ = 0, somaGpu_ = 0, maxQuadro_ = 0;
};

// ---------------------------------------------------------------------------
// Protocolo automático
// ---------------------------------------------------------------------------
struct OpcoesBenchmark {
    std::string rotulo = "vbo_T";      // prefixo dos arquivos de saída
    std::string pastaSaida = "resultados/dados";
    std::string configs = "ABCDEF";
    bool usarVBO = true;
    char variavel = 'T';               // 'T' = detalhe dos orbes, 'N' = número de orbes
    std::vector<int> valores;          // lista grossa (crescente)
    int fixoT = 8, fixoN = 64;         // valor do parâmetro que não varia
    double estabilizacao = 2.0;        // s, descartados antes de cada medição
    double medicao = 10.0;             // s, por repetição
    int repeticoes = 3;
    double fpsLimite = 60.0;
    double fpsParada = 35.0;           // encerra a varredura grossa abaixo deste FPS
    double tolerancia = 0.05;          // refino até tri(hi)/tri(lo) - 1 <= tolerância
    bool apenasPontoAtual = false;     // medição manual (tecla m): um ponto, sem varredura
};

class Benchmark {
public:
    Benchmark(Cena& cena, Monitor& monitor, AcumuladorQuadros& quadros, const OpcoesBenchmark& op,
              const std::string& rendererGL, const std::string& ambiente);

    void iniciar(double agora);
    // Chamado a cada quadro, depois do SwapBuffers. Retorna false quando o protocolo termina.
    bool atualizar(double agora);
    bool terminado() const { return terminado_; }
    std::string status() const;

private:
    enum class Fase { Aplicar, Estabilizar, Medir };
    struct Ponto {
        int valor = 0;
        std::uint64_t triangulos = 0;
        std::string etapa;  // "grossa" ou "refino"
        ResultadoQuadros q;  // médias das repetições
        Metricas m;
        double fpsDesvio = 0, fpsMin = 0, fpsMax = 0;
    };

    Cena& cena_;
    Monitor& monitor_;
    AcumuladorQuadros& quadros_;
    OpcoesBenchmark op_;
    std::string renderer_;

    std::ofstream csvReps_, csvPontos_, csvResumo_;
    bool terminado_ = false;

    // Posição no protocolo
    size_t idxConfig_ = 0;
    char config_ = 'A';
    size_t idxGrosso_ = 0;
    int valorAtual_ = 0;
    std::string etapaAtual_ = "grossa";
    int lo_ = -1, hi_ = -1;         // maior valor >= limite e menor valor < limite já medidos
    bool cruzou_ = false, refinando_ = false, encerrarApos_ = false;
    std::map<int, Ponto> pontosConfig_;

    // Repetições do ponto atual
    Fase fase_ = Fase::Aplicar;
    double tFase_ = 0;
    int rep_ = 0;
    std::vector<ResultadoQuadros> repsQ_;
    std::vector<Metricas> repsM_;
    double tInicioPonto_ = 0;
    double ultimoAgora_ = 0;

    std::uint64_t triangulosPara(int valor) const;
    void aplicarPonto();
    void concluirRepeticao(double agora);
    void concluirPonto();
    void decidirProximo(const Ponto& p);
    void iniciarConfig();
    void concluirConfig();
    bool precisaRefinar() const;
    void escreverCabecalhos();
};

// Formatação auxiliar
std::string agoraTexto();                      // data e hora local "AAAA-MM-DD HH:MM:SS"
std::string comMilhares(std::uint64_t n);      // 1234567 -> "1.234.567"
