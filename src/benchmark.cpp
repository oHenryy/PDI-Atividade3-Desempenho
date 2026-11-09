/*
 * benchmark.cpp
 * Medição de quadros e protocolo experimental automático (ver benchmark.h).
 */
#include "benchmark.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <sstream>

// ---------------------------------------------------------------------------
// Utilitários
// ---------------------------------------------------------------------------
std::string agoraTexto() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string comMilhares(std::uint64_t n) {
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ".");
    return s;
}

// ---------------------------------------------------------------------------
// AcumuladorQuadros
// ---------------------------------------------------------------------------
void AcumuladorQuadros::reiniciar(double agora) {
    inicio_ = ultimo_ = anterior_ = agora;
    quadros_ = quadrosGpu_ = 0;
    somaSub_ = somaSwap_ = somaAtiva_ = somaGpu_ = maxQuadro_ = 0;
}

void AcumuladorQuadros::adicionar(double agora, const DadosQuadro& d) {
    ++quadros_;
    maxQuadro_ = std::max(maxQuadro_, (agora - anterior_) * 1000.0);
    anterior_ = ultimo_ = agora;
    somaSub_ += d.submissaoMs;
    somaSwap_ += d.swapMs;
    somaAtiva_ += d.cpuAtivaMs;
    somaGpu_ += d.gpuSomaMs;
    quadrosGpu_ += static_cast<std::uint64_t>(d.gpuAmostras);
}

ResultadoQuadros AcumuladorQuadros::resultado() const {
    ResultadoQuadros r;
    r.quadros = quadros_;
    r.segundos = ultimo_ - inicio_;
    if (quadros_ == 0 || r.segundos <= 0) return r;
    r.fps = quadros_ / r.segundos;
    r.quadroMs = 1000.0 / r.fps;
    r.quadroMaxMs = maxQuadro_;
    r.submissaoMs = somaSub_ / quadros_;
    r.swapMs = somaSwap_ / quadros_;
    r.cpuAtivaMs = somaAtiva_ / quadros_;
    r.gpuMs = quadrosGpu_ ? somaGpu_ / quadrosGpu_ : -1;
    return r;
}

// ---------------------------------------------------------------------------
// Escrita dos CSV
// ---------------------------------------------------------------------------
namespace {

const char* CAB_METRICAS =
    "fps,quadro_ms,quadro_max_ms,submissao_ms,swap_ms,cpu_ativa_ms,gpu_ms,"
    "cpu_proc,cpu_thread,cpu_thread_max,threads,cpu_sis,cpu_nucleo_max,"
    "gpu_nv_proc,gpu_nv_sis,gpu_intel_proc,gpu_intel_sis,"
    "nv_util,nv_mem_util,nv_pot_w,nv_temp_c,nv_clk_sm,nv_clk_mem,nv_vram_mb,"
    "gpu_mem_proc_nv_mb,gpu_mem_proc_intel_mb,ram_proc_mb,ram_sis_pct,motivos_reducao";

std::string metricasCSV(const ResultadoQuadros& q, const Metricas& m) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "%.3f,%.4f,%.3f,%.4f,%.4f,%.4f,%.4f,"
                  "%.2f,%.2f,%.2f,%.1f,%.2f,%.2f,"
                  "%.2f,%.2f,%.2f,%.2f,"
                  "%.1f,%.1f,%.2f,%.1f,%.0f,%.0f,%.1f,"
                  "%.1f,%.1f,%.1f,%.1f,%s",
                  q.fps, q.quadroMs, q.quadroMaxMs, q.submissaoMs, q.swapMs, q.cpuAtivaMs, q.gpuMs,
                  m.cpuProcesso, m.cpuThreadRender, m.cpuThreadMax, m.threadsProcesso, m.cpuSistema, m.cpuNucleoMax,
                  m.gpu3dProcesso[GPU_NVIDIA], m.gpuSistema[GPU_NVIDIA], m.gpu3dProcesso[GPU_INTEL],
                  m.gpuSistema[GPU_INTEL],
                  m.nvUtil, m.nvMemUtil, m.nvPotenciaW, m.nvTempC, m.nvClockSM, m.nvClockMem, m.nvVramUsadaMB,
                  m.gpuMemProcessoMB[GPU_NVIDIA], m.gpuMemProcessoMB[GPU_INTEL], m.ramProcessoMB, m.ramSistemaPct,
                  Monitor::descreverMotivosReducao(m.nvMotivosReducao).c_str());
    return buf;
}

// Média de um campo double de um vetor de structs
template <class T, class F>
double media(const std::vector<T>& v, F campo) {
    double s = 0;
    for (const T& x : v) s += campo(x);
    return v.empty() ? 0 : s / v.size();
}

}  // namespace

// ---------------------------------------------------------------------------
// Benchmark
// ---------------------------------------------------------------------------
Benchmark::Benchmark(Cena& cena, Monitor& monitor, AcumuladorQuadros& quadros, const OpcoesBenchmark& op,
                     const std::string& rendererGL, const std::string& ambiente)
    : cena_(cena), monitor_(monitor), quadros_(quadros), op_(op), renderer_(rendererGL) {
    std::filesystem::create_directories(op_.pastaSaida);
    const std::string base = op_.pastaSaida + "/" + op_.rotulo;
    // Medições manuais são acrescentadas ao mesmo arquivo; o protocolo completo recria os arquivos.
    const auto modo = op_.apenasPontoAtual ? std::ios::app : std::ios::trunc;
    const bool novo = !op_.apenasPontoAtual || !std::filesystem::exists(base + "_pontos.csv");
    csvReps_.open(base + "_repeticoes.csv", std::ios::out | modo);
    csvPontos_.open(base + "_pontos.csv", std::ios::out | modo);
    if (!op_.apenasPontoAtual) csvResumo_.open(base + "_resumo.csv", std::ios::out | std::ios::trunc);
    if (novo) escreverCabecalhos();
    std::ofstream amb(base + "_ambiente.txt", std::ios::out | modo);
    amb << ambiente << "\n";
}

void Benchmark::escreverCabecalhos() {
    const std::string id =
        "rotulo,modo,renderer,config,iluminacao,textura,variavel,objetos,T,tri_por_objeto,triangulos,chamadas,etapa,";
    csvReps_ << id << "rep,quadros,segundos," << CAB_METRICAS << ",inicio\n";
    csvPontos_ << id << "reps,fps_dp,fps_min,fps_max," << CAB_METRICAS << ",fim\n";
    if (csvResumo_.is_open())
        csvResumo_ << "rotulo,modo,config,iluminacao,textura,situacao,"
                      "tri_ultimo_acima,valor_ultimo_acima,fps_ultimo_acima,"
                      "tri_primeiro_abaixo,valor_primeiro_abaixo,fps_primeiro_abaixo,fps_dp_primeiro_abaixo,"
                      "quadro_ms,gpu_ms,submissao_ms,swap_ms,cpu_ativa_ms,cpu_proc,cpu_thread,cpu_thread_max,cpu_nucleo_max,"
                      "gpu_nv_proc,gpu_intel_proc,nv_util,nv_pot_w,nv_clk_sm,motivos_reducao,"
                      "tri_maior_testado,fps_maior_testado,pontos_medidos\n";
}

std::uint64_t Benchmark::triangulosPara(int valor) const {
    return op_.variavel == 'T' ? Cena::triangulosPara(valor, op_.fixoN) : Cena::triangulosPara(op_.fixoT, valor);
}

void Benchmark::iniciar(double agora) {
    ultimoAgora_ = agora;
    idxConfig_ = 0;
    std::printf("\n=== Benchmark '%s' | modo %s | variavel %c | configs %s | %d x %.0f s (+%.0f s estab.) ===\n",
                op_.rotulo.c_str(), op_.usarVBO ? "VBO" : "imediato", op_.variavel, op_.configs.c_str(),
                op_.repeticoes, op_.medicao, op_.estabilizacao);
    iniciarConfig();
}

void Benchmark::iniciarConfig() {
    config_ = op_.configs[idxConfig_];
    lo_ = hi_ = -1;
    cruzou_ = refinando_ = encerrarApos_ = false;
    pontosConfig_.clear();
    idxGrosso_ = 0;
    valorAtual_ = op_.valores.front();
    etapaAtual_ = "grossa";
    fase_ = Fase::Aplicar;
    std::printf("\n--- Configuracao %s ---\n", descreverConfig(config_).c_str());
}

void Benchmark::aplicarPonto() {
    cena_.definirConfig(config_);
    cena_.definirVBO(op_.usarVBO);
    cena_.definirAramado(false);
    if (op_.variavel == 'T') {
        cena_.definirObjetos(op_.fixoN);
        cena_.definirDetalhe(valorAtual_);
    } else {
        cena_.definirDetalhe(op_.fixoT);
        cena_.definirObjetos(valorAtual_);
    }
    rep_ = 0;
    repsQ_.clear();
    repsM_.clear();
}

bool Benchmark::atualizar(double agora) {
    if (terminado_) return false;
    // Proteção: um intervalo anormal entre quadros (janela minimizada -- o FreeGLUT deixa de
    // desenhar --, sistema suspenso, etc.) invalida a repetição em andamento, que é descartada
    // e recomeça pela estabilização. O maior quadro legítimo dos experimentos é < 0,2 s.
    const double intervalo = agora - ultimoAgora_;
    ultimoAgora_ = agora;
    if (intervalo > 2.0 && fase_ != Fase::Aplicar) {
        std::printf("  [!] intervalo anormal de %.1f s entre quadros (janela minimizada?): repeticao %d "
                    "descartada e reiniciada\n", intervalo, rep_ + 1);
        std::fflush(stdout);
        fase_ = Fase::Estabilizar;
        tFase_ = agora;
        return true;
    }
    switch (fase_) {
        case Fase::Aplicar:
            aplicarPonto();  // pode recriar a malha: o quadro seguinte é descartado na estabilização
            tInicioPonto_ = agora;
            fase_ = Fase::Estabilizar;
            tFase_ = agora;
            break;
        case Fase::Estabilizar:
            if (agora - tFase_ >= op_.estabilizacao) {
                quadros_.reiniciar(agora);
                monitor_.reiniciarJanela();
                fase_ = Fase::Medir;
                tFase_ = agora;
            }
            break;
        case Fase::Medir:
            if (agora - tFase_ >= op_.medicao) concluirRepeticao(agora);
            break;
    }
    return !terminado_;
}

void Benchmark::concluirRepeticao(double agora) {
    const ResultadoQuadros q = quadros_.resultado();
    const Metricas m = monitor_.mediaJanela();
    repsQ_.push_back(q);
    repsM_.push_back(m);
    const EstadoCena& e = cena_.estado();
    csvReps_ << op_.rotulo << ',' << (op_.usarVBO ? "VBO" : "imediato") << ",\"" << renderer_ << "\"," << config_
             << ',' << nomeIluminacao(iluminacaoDaConfig(config_)) << ',' << (texturaDaConfig(config_) ? 1 : 0) << ','
             << op_.variavel << ',' << e.N << ',' << e.T << ',' << cena_.triangulosPorOrbe() << ','
             << cena_.triangulosPorQuadro() << ',' << cena_.chamadasDesenhoPorQuadro() << ',' << etapaAtual_ << ','
             << (rep_ + 1) << ',' << q.quadros << ',' << q.segundos << ',' << metricasCSV(q, m) << ','
             << agoraTexto() << '\n';
    csvReps_.flush();
    std::printf("  [%c] %s=%-5d tri=%-12s rep %d/%d: %8.2f FPS  %7.3f ms  (emissao %.3f ms, CPU ativa %.3f ms, GPU %.3f ms)  "
                "CPU %.1f%% thr %.0f%% thrMax %.0f%%  GPU nv %.0f%% intel %.0f%%\n",
                config_, op_.variavel == 'T' ? "T" : "N", valorAtual_, comMilhares(cena_.triangulosPorQuadro()).c_str(),
                rep_ + 1, op_.repeticoes, q.fps, q.quadroMs, q.submissaoMs, q.cpuAtivaMs, q.gpuMs, m.cpuProcesso, m.cpuThreadRender,
                m.cpuThreadMax, m.gpu3dProcesso[GPU_NVIDIA], m.gpu3dProcesso[GPU_INTEL]);
    std::fflush(stdout);
    if (++rep_ < op_.repeticoes) {
        fase_ = Fase::Estabilizar;  // nova estabilização antes de cada repetição
        tFase_ = agora;
    } else {
        concluirPonto();
    }
}

void Benchmark::concluirPonto() {
    Ponto p;
    p.valor = valorAtual_;
    p.triangulos = cena_.triangulosPorQuadro();
    p.etapa = etapaAtual_;
    // Médias das repetições (o critério de < 60 FPS usa o FPS médio das repetições)
    p.q.quadros = static_cast<std::uint64_t>(media(repsQ_, [](const ResultadoQuadros& r) { return double(r.quadros); }));
    p.q.segundos = media(repsQ_, [](const ResultadoQuadros& r) { return r.segundos; });
    p.q.fps = media(repsQ_, [](const ResultadoQuadros& r) { return r.fps; });
    p.q.quadroMs = media(repsQ_, [](const ResultadoQuadros& r) { return r.quadroMs; });
    p.q.quadroMaxMs = media(repsQ_, [](const ResultadoQuadros& r) { return r.quadroMaxMs; });
    p.q.submissaoMs = media(repsQ_, [](const ResultadoQuadros& r) { return r.submissaoMs; });
    p.q.swapMs = media(repsQ_, [](const ResultadoQuadros& r) { return r.swapMs; });
    p.q.cpuAtivaMs = media(repsQ_, [](const ResultadoQuadros& r) { return r.cpuAtivaMs; });
    p.q.gpuMs = media(repsQ_, [](const ResultadoQuadros& r) { return r.gpuMs; });
    double s2 = 0;
    p.fpsMin = 1e30;
    p.fpsMax = 0;
    for (const auto& r : repsQ_) {
        s2 += (r.fps - p.q.fps) * (r.fps - p.q.fps);
        p.fpsMin = std::min(p.fpsMin, r.fps);
        p.fpsMax = std::max(p.fpsMax, r.fps);
    }
    p.fpsDesvio = repsQ_.size() > 1 ? std::sqrt(s2 / (repsQ_.size() - 1)) : 0;  // desvio-padrão amostral
    Metricas soma;
    for (const auto& m : repsM_) acumularMetricas(soma, m);
    p.m = mediaMetricas(soma);  // média das médias de cada repetição
    pontosConfig_[p.valor] = p;

    const EstadoCena& e = cena_.estado();
    char extra[128];
    std::snprintf(extra, sizeof(extra), "%zu,%.3f,%.3f,%.3f,", repsQ_.size(), p.fpsDesvio, p.fpsMin, p.fpsMax);
    csvPontos_ << op_.rotulo << ',' << (op_.usarVBO ? "VBO" : "imediato") << ",\"" << renderer_ << "\"," << config_
               << ',' << nomeIluminacao(iluminacaoDaConfig(config_)) << ',' << (texturaDaConfig(config_) ? 1 : 0)
               << ',' << op_.variavel << ',' << e.N << ',' << e.T << ',' << cena_.triangulosPorOrbe() << ','
               << p.triangulos << ',' << cena_.chamadasDesenhoPorQuadro() << ',' << p.etapa << ',' << extra
               << metricasCSV(p.q, p.m) << ',' << agoraTexto() << '\n';
    csvPontos_.flush();
    std::printf("  => [%c] %s=%d (%s) tri=%s: FPS medio %.2f +- %.2f %s\n", config_, op_.variavel == 'T' ? "T" : "N",
                p.valor, p.etapa.c_str(), comMilhares(p.triangulos).c_str(), p.q.fps, p.fpsDesvio,
                p.q.fps < op_.fpsLimite ? "(< 60)" : "");
    std::fflush(stdout);
    decidirProximo(p);
}

bool Benchmark::precisaRefinar() const {
    if (lo_ < 0 || hi_ < 0 || hi_ - lo_ <= 1) return false;
    return static_cast<double>(triangulosPara(hi_)) / triangulosPara(lo_) - 1.0 > op_.tolerancia;
}

void Benchmark::decidirProximo(const Ponto& p) {
    if (op_.apenasPontoAtual) {
        terminado_ = true;
        return;
    }
    const bool abaixo = p.q.fps < op_.fpsLimite;
    if (!abaixo) {
        if (p.valor > lo_) lo_ = p.valor;
    } else if (hi_ < 0 || p.valor < hi_) {
        hi_ = p.valor;
    }

    if (refinando_) {
        if (precisaRefinar()) {
            valorAtual_ = (lo_ + hi_) / 2;  // bisseção
            fase_ = Fase::Aplicar;
            return;
        }
        refinando_ = false;
        if (encerrarApos_) {
            concluirConfig();
            return;
        }
    } else {
        encerrarApos_ = p.q.fps < op_.fpsParada;
        if (abaixo && !cruzou_) {
            cruzou_ = true;
            if (precisaRefinar()) {
                refinando_ = true;
                etapaAtual_ = "refino";
                valorAtual_ = (lo_ + hi_) / 2;
                fase_ = Fase::Aplicar;
                return;
            }
        }
        if (encerrarApos_) {
            concluirConfig();
            return;
        }
    }
    // Próximo valor da lista grossa ainda não medido
    while (++idxGrosso_ < op_.valores.size() && pontosConfig_.count(op_.valores[idxGrosso_])) {
    }
    if (idxGrosso_ >= op_.valores.size()) {
        concluirConfig();
        return;
    }
    valorAtual_ = op_.valores[idxGrosso_];
    etapaAtual_ = "grossa";
    fase_ = Fase::Aplicar;
}

void Benchmark::concluirConfig() {
    // Menor carga testada com FPS médio < 60 e maior carga testada (abaixo dela) com FPS >= 60
    const Ponto* abaixo = nullptr;
    const Ponto* acima = nullptr;
    const Ponto* maior = nullptr;
    for (const auto& [v, p] : pontosConfig_) {
        if (p.q.fps < op_.fpsLimite && (!abaixo || p.triangulos < abaixo->triangulos)) abaixo = &p;
        if (!maior || p.triangulos > maior->triangulos) maior = &p;
    }
    for (const auto& [v, p] : pontosConfig_)
        if (p.q.fps >= op_.fpsLimite && (!abaixo || p.triangulos < abaixo->triangulos) &&
            (!acima || p.triangulos > acima->triangulos))
            acima = &p;
    const char* situacao = !abaixo ? "limite nao alcancado"
                           : !acima ? "abaixo de 60 ja na menor carga"
                                    : "limite encontrado";
    const Ponto vazio;
    const Ponto& a = acima ? *acima : vazio;
    const Ponto& b = abaixo ? *abaixo : vazio;
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "%s,%s,%c,%s,%d,%s,%llu,%d,%.3f,%llu,%d,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
                  "%.1f,%.2f,%.0f,%s,%llu,%.3f,%zu\n",
                  op_.rotulo.c_str(), op_.usarVBO ? "VBO" : "imediato", config_,
                  nomeIluminacao(iluminacaoDaConfig(config_)).c_str(), texturaDaConfig(config_) ? 1 : 0, situacao,
                  static_cast<unsigned long long>(a.triangulos), a.valor, a.q.fps,
                  static_cast<unsigned long long>(b.triangulos), b.valor, b.q.fps, b.fpsDesvio, b.q.quadroMs,
                  b.q.gpuMs, b.q.submissaoMs, b.q.swapMs, b.q.cpuAtivaMs, b.m.cpuProcesso, b.m.cpuThreadRender, b.m.cpuThreadMax,
                  b.m.cpuNucleoMax,
                  b.m.gpu3dProcesso[GPU_NVIDIA], b.m.gpu3dProcesso[GPU_INTEL], b.m.nvUtil, b.m.nvPotenciaW,
                  b.m.nvClockSM, Monitor::descreverMotivosReducao(b.m.nvMotivosReducao).c_str(),
                  static_cast<unsigned long long>(maior ? maior->triangulos : 0), maior ? maior->q.fps : 0.0,
                  pontosConfig_.size());
    csvResumo_ << buf;
    csvResumo_.flush();
    std::printf("--- Config %c: %s | ultimo >= 60: %s tri (%.1f FPS) | primeiro < 60: %s tri (%.1f FPS)\n", config_,
                situacao, comMilhares(a.triangulos).c_str(), a.q.fps, comMilhares(b.triangulos).c_str(), b.q.fps);
    std::fflush(stdout);
    if (++idxConfig_ >= op_.configs.size()) {
        terminado_ = true;
        std::printf("\n=== Benchmark '%s' concluido. Resultados em %s/ ===\n", op_.rotulo.c_str(),
                    op_.pastaSaida.c_str());
        return;
    }
    iniciarConfig();
}

std::string Benchmark::status() const {
    if (terminado_) return "benchmark concluido";
    char buf[256];
    const double dt = ultimoAgora_ - tFase_;
    const char* fase = fase_ == Fase::Medir ? "medindo" : "estabilizando";
    const double total = fase_ == Fase::Medir ? op_.medicao : op_.estabilizacao;
    std::snprintf(buf, sizeof(buf), "[%s] config %c | %c=%d (%s) | rep %d/%d | %s %.1f/%.0f s", op_.rotulo.c_str(),
                  config_, op_.variavel, valorAtual_, etapaAtual_.c_str(), std::min(rep_ + 1, op_.repeticoes),
                  op_.repeticoes, fase, std::min(dt, total), total);
    return buf;
}
