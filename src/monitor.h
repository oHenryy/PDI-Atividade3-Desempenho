/*
 * monitor.h
 * Monitoramento da utilização de CPU, GPU e memória durante a execução (Windows).
 *
 * Uma thread auxiliar coleta amostras a cada 500 ms (fora da thread de renderização, para
 * não perturbar os quadros) a partir de:
 *   - GetProcessTimes / GetThreadTimes / GetSystemTimes: CPU do processo, da thread de
 *     renderização (a thread principal, onde roda o FreeGLUT) e do sistema;
 *   - Toolhelp32 + GetThreadTimes: tempo de CPU de CADA thread do processo, para achar a
 *     thread mais ocupada (o driver OpenGL cria threads próprias de processamento);
 *   - PDH "\Processor(*)\% Processor Time": utilização de cada núcleo lógico;
 *   - PDH "\GPU Engine(*)\Utilization Percentage": utilização de cada motor de cada GPU,
 *     por processo -- é a mesma fonte usada pelo Gerenciador de Tarefas do Windows. Permite
 *     saber QUAL GPU executa a aplicação e acompanhar separadamente cada GPU;
 *   - PDH "\GPU Process Memory(*)\Dedicated/Shared Usage": memória de vídeo do processo;
 *   - NVML (nvml.dll, carregada dinamicamente, opcional): utilização, controlador de
 *     memória, potência, temperatura, clocks e motivos de redução de clock da GPU NVIDIA;
 *   - DXGI: lista de adaptadores gráficos (nome, fabricante, VRAM, LUID).
 *
 * As amostras são acumuladas numa "janela de medição" (reiniciarJanela / mediaJanela),
 * usada pelo benchmark para obter a média de cada medição de 10 s.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct AdaptadorGPU {
    std::string nome;
    unsigned fabricante = 0;          // 0x10DE NVIDIA, 0x8086 Intel, 0x1002 AMD
    std::uint64_t luid = 0;           // identificador do adaptador (HighPart << 32 | LowPart)
    double vramDedicadaMB = 0;        // memória de vídeo dedicada
    double memoriaCompartilhadaMB = 0;
    bool integrada() const { return fabricante == 0x8086 && vramDedicadaMB < 1024; }
};

// Índices fixos usados nas métricas por GPU
enum { GPU_NVIDIA = 0, GPU_INTEL = 1, GPU_OUTRA = 2, NUM_GRUPOS_GPU = 3 };

struct Metricas {
    int amostras = 0;
    // CPU (%)
    double cpuProcesso = 0;      // fração da CPU inteira (todos os núcleos lógicos) usada pelo processo
    double cpuThreadRender = 0;  // fração de UM núcleo lógico usada pela thread de renderização
    double cpuThreadMax = 0;     // fração de UM núcleo usada pela thread MAIS ocupada do processo
                                 // (pode ser uma thread do driver OpenGL, e não a thread principal)
    double threadsProcesso = 0;  // número de threads do processo
    double cpuSistema = 0;       // utilização total do sistema
    double cpuNucleoMax = 0;     // maior utilização entre os núcleos lógicos
    // GPU (%) por grupo (NVIDIA / Intel / outra)
    double gpu3dProcesso[NUM_GRUPOS_GPU] = {};  // motor 3D usado por ESTE processo
    double gpuSistema[NUM_GRUPOS_GPU] = {};     // motor mais ocupado da GPU (todos os processos)
    double gpuMemProcessoMB[NUM_GRUPOS_GPU] = {};  // memória de vídeo (dedicada + compartilhada) do processo
    // NVML (GPU NVIDIA)
    double nvUtil = 0, nvMemUtil = 0, nvPotenciaW = 0, nvTempC = 0;
    double nvClockSM = 0, nvClockMem = 0, nvVramUsadaMB = 0;
    std::uint64_t nvMotivosReducao = 0;  // OU bit a bit dos motivos de redução de clock observados
    // Memória do sistema
    double ramProcessoMB = 0;    // working set do processo
    double ramSistemaPct = 0;    // carga de memória do sistema
};

class Monitor {
public:
    Monitor();
    ~Monitor();
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    // Deve ser chamado pela thread de renderização (registra o handle dessa thread).
    void iniciar();
    void parar();

    void reiniciarJanela();          // zera os acumuladores
    Metricas mediaJanela() const;    // médias desde o último reiniciarJanela()
    Metricas ultima() const;         // amostra mais recente (para o painel na tela)

    const std::vector<AdaptadorGPU>& adaptadores() const;
    bool temNVML() const;
    std::string nomeGPUNVML() const;

    static int grupoDoFabricante(unsigned fabricante);
    static std::string descreverMotivosReducao(std::uint64_t mascara);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Soma de amostras e média (usadas pela janela de medição e pelas repetições do benchmark)
void acumularMetricas(Metricas& soma, const Metricas& m);  // soma campo a campo; amostras += 1
Metricas mediaMetricas(Metricas soma);                     // divide pela quantidade de amostras

// Informações estáticas do sistema (para o registro dos experimentos)
std::string nomeCPU();
int nucleosFisicos();
int processadoresLogicos();
double memoriaRAMTotalGB();
std::string versaoWindows();
