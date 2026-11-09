/*
 * monitor.cpp
 * Coleta periódica de utilização de CPU, GPU e memória (ver monitor.h).
 */
#include "monitor.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <cwchar>

namespace {

std::string utf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::uint64_t paraU64(const FILETIME& f) {
    return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

// Lê todas as instâncias de um contador PDH com curinga "(*)".
struct ItemPDH {
    std::wstring nome;
    double valor;
};
std::vector<ItemPDH> lerArray(PDH_HCOUNTER c) {
    std::vector<ItemPDH> itens;
    if (!c) return itens;
    DWORD bytes = 0, n = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &n, nullptr);
    if (st != PDH_MORE_DATA || bytes == 0) return itens;
    std::vector<unsigned char> buf(bytes);
    auto* arr = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    st = PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &n, arr);
    if (st != ERROR_SUCCESS) return itens;
    itens.reserve(n);
    for (DWORD i = 0; i < n; ++i) {
        const DWORD cs = arr[i].FmtValue.CStatus;
        if (cs == PDH_CSTATUS_VALID_DATA || cs == PDH_CSTATUS_NEW_DATA)
            itens.push_back({arr[i].szName, arr[i].FmtValue.doubleValue});
    }
    return itens;
}

// Extrai pid e LUID de nomes como "pid_1234_luid_0x00000000_0x0000D1E6_phys_0_eng_3_engtype_3D"
bool lerPidLuid(const std::wstring& nome, unsigned& pid, std::uint64_t& luid) {
    unsigned alto = 0, baixo = 0;
#ifdef _MSC_VER
    if (swscanf_s(nome.c_str(), L"pid_%u_luid_0x%x_0x%x", &pid, &alto, &baixo) != 3) return false;
#else
    if (std::swscanf(nome.c_str(), L"pid_%u_luid_0x%x_0x%x", &pid, &alto, &baixo) != 3) return false;
#endif
    luid = (static_cast<std::uint64_t>(alto) << 32) | baixo;
    return true;
}

// Assinaturas mínimas da NVML (NVIDIA Management Library)
struct NvUtilizacao {
    unsigned gpu, memoria;
};
struct NvMemoria {
    unsigned long long total, livre, usada;
};
typedef int (*NvFnInit)();
typedef int (*NvFnHandle)(unsigned, void**);
typedef int (*NvFnUtil)(void*, NvUtilizacao*);
typedef int (*NvFnU32)(void*, unsigned*);
typedef int (*NvFnTipoU32)(void*, int, unsigned*);
typedef int (*NvFnMem)(void*, NvMemoria*);
typedef int (*NvFnU64)(void*, unsigned long long*);
typedef int (*NvFnNome)(void*, char*, unsigned);

}  // namespace

// ---------------------------------------------------------------------------
// Implementação
// ---------------------------------------------------------------------------
struct Monitor::Impl {
    std::vector<AdaptadorGPU> adaptadores;
    DWORD pid = GetCurrentProcessId();
    int logicos = 1;

    HANDLE threadRender = nullptr;
    std::thread trabalhador;
    bool rodando = false;
    mutable std::mutex mtx;
    std::condition_variable cv;
    Metricas soma, ultima;

    // PDH
    PDH_HQUERY consulta = nullptr;
    PDH_HCOUNTER cMotor = nullptr, cMemDed = nullptr, cMemComp = nullptr, cNucleos = nullptr;

    // Tempos da amostra anterior
    std::uint64_t procAnt = 0, threadAnt = 0, idleAnt = 0, kernAnt = 0, userAnt = 0;
    std::map<DWORD, std::uint64_t> tempoThreads;  // tempo de CPU acumulado de cada thread
    std::chrono::steady_clock::time_point relogioAnt;
    bool primeira = true;

    // NVML
    HMODULE nvml = nullptr;
    void* nvDisp = nullptr;
    std::string nvNome;
    NvFnUtil nvUtil = nullptr;
    NvFnU32 nvPotencia = nullptr;
    NvFnTipoU32 nvTemp = nullptr, nvClock = nullptr;
    NvFnMem nvMem = nullptr;
    NvFnU64 nvMotivos = nullptr;
    NvFnInit nvShutdown = nullptr;

    void enumerarAdaptadores() {
        IDXGIFactory1* fab = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&fab)))) return;
        IDXGIAdapter1* ad = nullptr;
        for (UINT i = 0; fab->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d{};
            ad->GetDesc1(&d);
            ad->Release();
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;  // ignora o "Microsoft Basic Render"
            AdaptadorGPU a;
            a.nome = utf8(d.Description);
            a.fabricante = d.VendorId;
            a.luid = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(d.AdapterLuid.HighPart)) << 32) |
                     d.AdapterLuid.LowPart;
            a.vramDedicadaMB = d.DedicatedVideoMemory / 1048576.0;
            a.memoriaCompartilhadaMB = d.SharedSystemMemory / 1048576.0;
            adaptadores.push_back(a);
        }
        fab->Release();
    }

    int grupoDoLuid(std::uint64_t luid) const {
        for (const auto& a : adaptadores)
            if (a.luid == luid) return grupoDoFabricante(a.fabricante);
        return GPU_OUTRA;
    }

    void abrirPDH() {
        if (PdhOpenQueryW(nullptr, 0, &consulta) != ERROR_SUCCESS) {
            consulta = nullptr;
            return;
        }
        PdhAddEnglishCounterW(consulta, L"\\GPU Engine(*)\\Utilization Percentage", 0, &cMotor);
        PdhAddEnglishCounterW(consulta, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &cMemDed);
        PdhAddEnglishCounterW(consulta, L"\\GPU Process Memory(*)\\Shared Usage", 0, &cMemComp);
        PdhAddEnglishCounterW(consulta, L"\\Processor(*)\\% Processor Time", 0, &cNucleos);
        PdhCollectQueryData(consulta);
    }

    void abrirNVML() {
        nvml = LoadLibraryW(L"nvml.dll");
        if (!nvml) nvml = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
        if (!nvml) return;
        auto fn = [this](const char* n) { return reinterpret_cast<void*>(GetProcAddress(nvml, n)); };
        auto init = reinterpret_cast<NvFnInit>(fn("nvmlInit_v2"));
        auto handle = reinterpret_cast<NvFnHandle>(fn("nvmlDeviceGetHandleByIndex_v2"));
        auto nome = reinterpret_cast<NvFnNome>(fn("nvmlDeviceGetName"));
        nvShutdown = reinterpret_cast<NvFnInit>(fn("nvmlShutdown"));
        nvUtil = reinterpret_cast<NvFnUtil>(fn("nvmlDeviceGetUtilizationRates"));
        nvPotencia = reinterpret_cast<NvFnU32>(fn("nvmlDeviceGetPowerUsage"));
        nvTemp = reinterpret_cast<NvFnTipoU32>(fn("nvmlDeviceGetTemperature"));
        nvClock = reinterpret_cast<NvFnTipoU32>(fn("nvmlDeviceGetClockInfo"));
        nvMem = reinterpret_cast<NvFnMem>(fn("nvmlDeviceGetMemoryInfo"));
        nvMotivos = reinterpret_cast<NvFnU64>(fn("nvmlDeviceGetCurrentClocksEventReasons"));
        if (!nvMotivos) nvMotivos = reinterpret_cast<NvFnU64>(fn("nvmlDeviceGetCurrentClocksThrottleReasons"));
        if (!init || !handle || init() != 0 || handle(0, &nvDisp) != 0) {
            nvDisp = nullptr;
            FreeLibrary(nvml);
            nvml = nullptr;
            return;
        }
        char buf[96] = {};
        if (nome && nome(nvDisp, buf, sizeof(buf)) == 0) nvNome = buf;
    }

    // Uma amostra de todas as fontes
    Metricas amostrar() {
        Metricas m;
        const auto agora = std::chrono::steady_clock::now();
        const double dt100ns = std::chrono::duration<double>(agora - relogioAnt).count() * 1e7;

        FILETIME c, e, k, u, idle, kern, user;
        GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
        const std::uint64_t proc = paraU64(k) + paraU64(u);
        std::uint64_t thr = threadAnt;
        if (threadRender && GetThreadTimes(threadRender, &c, &e, &k, &u)) thr = paraU64(k) + paraU64(u);
        GetSystemTimes(&idle, &kern, &user);  // o tempo de kernel inclui o tempo ocioso
        const std::uint64_t i0 = paraU64(idle), k0 = paraU64(kern), u0 = paraU64(user);

        // Todas as threads do processo: a mais ocupada indica saturação de um único núcleo
        std::map<DWORD, std::uint64_t> novos;
        const HANDLE foto = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (foto != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te{};
            te.dwSize = sizeof(te);
            for (BOOL ok = Thread32First(foto, &te); ok; ok = Thread32Next(foto, &te)) {
                if (te.th32OwnerProcessID != pid) continue;
                const HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
                if (!h) continue;
                if (GetThreadTimes(h, &c, &e, &k, &u)) {
                    const std::uint64_t t = paraU64(k) + paraU64(u);
                    novos[te.th32ThreadID] = t;
                    const auto it = tempoThreads.find(te.th32ThreadID);
                    if (!primeira && dt100ns > 0 && it != tempoThreads.end())
                        m.cpuThreadMax = std::max(m.cpuThreadMax, 100.0 * (t - it->second) / dt100ns);
                }
                CloseHandle(h);
            }
            CloseHandle(foto);
        }
        m.threadsProcesso = static_cast<double>(novos.size());
        tempoThreads.swap(novos);

        if (!primeira && dt100ns > 0) {
            m.cpuProcesso = 100.0 * (proc - procAnt) / (dt100ns * logicos);
            m.cpuThreadRender = 100.0 * (thr - threadAnt) / dt100ns;
            const double total = static_cast<double>((k0 - kernAnt) + (u0 - userAnt));
            const double ocioso = static_cast<double>(i0 - idleAnt);
            m.cpuSistema = total > 0 ? 100.0 * (total - ocioso) / total : 0;
        }
        procAnt = proc;
        threadAnt = thr;
        idleAnt = i0;
        kernAnt = k0;
        userAnt = u0;
        relogioAnt = agora;

        if (consulta && PdhCollectQueryData(consulta) == ERROR_SUCCESS) {
            // Núcleos lógicos
            for (const auto& it : lerArray(cNucleos))
                if (it.nome != L"_Total") m.cpuNucleoMax = std::max(m.cpuNucleoMax, std::min(it.valor, 100.0));
            // Motores das GPUs: soma por motor (todos os processos) e motor 3D deste processo
            std::map<std::wstring, std::pair<int, double>> porMotor;  // chave: "luid_..._eng_N_engtype_X"
            for (const auto& it : lerArray(cMotor)) {
                unsigned p = 0;
                std::uint64_t luid = 0;
                if (!lerPidLuid(it.nome, p, luid)) continue;
                const int g = grupoDoLuid(luid);
                const size_t pos = it.nome.find(L"luid_");
                auto& ref = porMotor[it.nome.substr(pos)];
                ref.first = g;
                ref.second += it.valor;
                if (p == pid && it.nome.find(L"engtype_3D") != std::wstring::npos)
                    m.gpu3dProcesso[g] = std::min(100.0, m.gpu3dProcesso[g] + it.valor);
            }
            for (const auto& [chave, gv] : porMotor)
                m.gpuSistema[gv.first] = std::max(m.gpuSistema[gv.first], std::min(100.0, gv.second));
            // Memória de vídeo do processo
            for (PDH_HCOUNTER cm : {cMemDed, cMemComp})
                for (const auto& it : lerArray(cm)) {
                    unsigned p = 0;
                    std::uint64_t luid = 0;
                    if (lerPidLuid(it.nome, p, luid) && p == pid)
                        m.gpuMemProcessoMB[grupoDoLuid(luid)] += it.valor / 1048576.0;
                }
        }

        if (nvDisp) {
            NvUtilizacao ut{};
            unsigned v = 0;
            NvMemoria mem{};
            unsigned long long motivos = 0;
            if (nvUtil && nvUtil(nvDisp, &ut) == 0) {
                m.nvUtil = ut.gpu;
                m.nvMemUtil = ut.memoria;
            }
            if (nvPotencia && nvPotencia(nvDisp, &v) == 0) m.nvPotenciaW = v / 1000.0;
            if (nvTemp && nvTemp(nvDisp, 0, &v) == 0) m.nvTempC = v;
            if (nvClock && nvClock(nvDisp, 1, &v) == 0) m.nvClockSM = v;
            if (nvClock && nvClock(nvDisp, 2, &v) == 0) m.nvClockMem = v;
            if (nvMem && nvMem(nvDisp, &mem) == 0) m.nvVramUsadaMB = mem.usada / 1048576.0;
            if (nvMotivos && nvMotivos(nvDisp, &motivos) == 0) m.nvMotivosReducao = motivos & ~1ull;  // sem "ociosa"
        }

        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
            m.ramProcessoMB = pmc.WorkingSetSize / 1048576.0;
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) m.ramSistemaPct = ms.dwMemoryLoad;
        return m;
    }

    void laco() {
        relogioAnt = std::chrono::steady_clock::now();
        amostrar();  // a primeira amostra só inicializa as referências
        primeira = false;
        std::unique_lock<std::mutex> trava(mtx);
        while (rodando) {
            cv.wait_for(trava, std::chrono::milliseconds(500), [this] { return !rodando; });
            if (!rodando) break;
            trava.unlock();
            const Metricas m = amostrar();
            trava.lock();
            ultima = m;
            acumularMetricas(soma, m);
        }
    }
};

Monitor::Monitor() : impl_(new Impl) {
    impl_->logicos = processadoresLogicos();
    impl_->enumerarAdaptadores();
}

Monitor::~Monitor() { parar(); }

void Monitor::iniciar() {
    if (impl_->rodando) return;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &impl_->threadRender, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    impl_->abrirPDH();
    impl_->abrirNVML();
    impl_->rodando = true;
    impl_->trabalhador = std::thread([this] { impl_->laco(); });
}

void Monitor::parar() {
    if (!impl_ || !impl_->rodando) return;
    {
        std::lock_guard<std::mutex> t(impl_->mtx);
        impl_->rodando = false;
    }
    impl_->cv.notify_all();
    if (impl_->trabalhador.joinable()) impl_->trabalhador.join();
    if (impl_->consulta) PdhCloseQuery(impl_->consulta);
    impl_->consulta = nullptr;
    if (impl_->nvml) {
        if (impl_->nvShutdown) impl_->nvShutdown();
        FreeLibrary(impl_->nvml);
        impl_->nvml = nullptr;
        impl_->nvDisp = nullptr;
    }
    if (impl_->threadRender) CloseHandle(impl_->threadRender);
    impl_->threadRender = nullptr;
}

void Monitor::reiniciarJanela() {
    std::lock_guard<std::mutex> t(impl_->mtx);
    impl_->soma = Metricas{};
}

Metricas Monitor::mediaJanela() const {
    std::lock_guard<std::mutex> t(impl_->mtx);
    return mediaMetricas(impl_->soma);
}

Metricas Monitor::ultima() const {
    std::lock_guard<std::mutex> t(impl_->mtx);
    return impl_->ultima;
}

const std::vector<AdaptadorGPU>& Monitor::adaptadores() const { return impl_->adaptadores; }
bool Monitor::temNVML() const { return impl_->nvDisp != nullptr; }
std::string Monitor::nomeGPUNVML() const { return impl_->nvNome; }

// ---------------------------------------------------------------------------
// Informações do sistema
// ---------------------------------------------------------------------------
std::string nomeCPU() {
    char buf[128] = {};
    DWORD tam = sizeof(buf);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     "ProcessorNameString", RRF_RT_REG_SZ, nullptr, buf, &tam) != ERROR_SUCCESS)
        return "desconhecida";
    std::string s = buf;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

int nucleosFisicos() {
    DWORD tam = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &tam);
    std::vector<unsigned char> buf(tam);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
                                          reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &tam))
        return 0;
    int n = 0;
    for (DWORD pos = 0; pos < tam;) {
        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + pos);
        ++n;
        pos += info->Size;
    }
    return n;
}

int processadoresLogicos() { return static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)); }

double memoriaRAMTotalGB() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    return ms.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
}

std::string versaoWindows() {
    typedef LONG(WINAPI * FnRtl)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    auto rtl = reinterpret_cast<FnRtl>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (rtl) rtl(&v);
    char edicao[64] = {}, versao[32] = {};
    DWORD t1 = sizeof(edicao), t2 = sizeof(versao);
    RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "EditionID", RRF_RT_REG_SZ,
                 nullptr, edicao, &t1);
    RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "DisplayVersion",
                 RRF_RT_REG_SZ, nullptr, versao, &t2);
    const int principal = (v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000) ? 11 : static_cast<int>(v.dwMajorVersion);
    return "Windows " + std::to_string(principal) + " " + edicao + " " + versao + " (build " +
           std::to_string(v.dwBuildNumber) + ")";
}

#else  // ---------------------------------------------------------------------------------
// Outras plataformas: o monitoramento interno não está disponível (use ferramentas externas)
struct Monitor::Impl {
    std::vector<AdaptadorGPU> adaptadores;
};
Monitor::Monitor() : impl_(new Impl) {}
Monitor::~Monitor() = default;
void Monitor::iniciar() {}
void Monitor::parar() {}
void Monitor::reiniciarJanela() {}
Metricas Monitor::mediaJanela() const { return {}; }
Metricas Monitor::ultima() const { return {}; }
const std::vector<AdaptadorGPU>& Monitor::adaptadores() const { return impl_->adaptadores; }
bool Monitor::temNVML() const { return false; }
std::string Monitor::nomeGPUNVML() const { return {}; }
std::string nomeCPU() { return "desconhecida"; }
int nucleosFisicos() { return static_cast<int>(std::thread::hardware_concurrency()); }
int processadoresLogicos() { return static_cast<int>(std::thread::hardware_concurrency()); }
double memoriaRAMTotalGB() { return 0; }
std::string versaoWindows() { return "nao-Windows"; }
#endif

void acumularMetricas(Metricas& a, const Metricas& b) {
    a.cpuProcesso += b.cpuProcesso;
    a.cpuThreadRender += b.cpuThreadRender;
    a.cpuThreadMax += b.cpuThreadMax;
    a.threadsProcesso += b.threadsProcesso;
    a.cpuSistema += b.cpuSistema;
    a.cpuNucleoMax += b.cpuNucleoMax;
    for (int g = 0; g < NUM_GRUPOS_GPU; ++g) {
        a.gpu3dProcesso[g] += b.gpu3dProcesso[g];
        a.gpuSistema[g] += b.gpuSistema[g];
        a.gpuMemProcessoMB[g] += b.gpuMemProcessoMB[g];
    }
    a.nvUtil += b.nvUtil;
    a.nvMemUtil += b.nvMemUtil;
    a.nvPotenciaW += b.nvPotenciaW;
    a.nvTempC += b.nvTempC;
    a.nvClockSM += b.nvClockSM;
    a.nvClockMem += b.nvClockMem;
    a.nvVramUsadaMB += b.nvVramUsadaMB;
    a.nvMotivosReducao |= b.nvMotivosReducao;  // motivos: união dos observados
    a.ramProcessoMB += b.ramProcessoMB;
    a.ramSistemaPct += b.ramSistemaPct;
    a.amostras += 1;
}

Metricas mediaMetricas(Metricas a) {
    if (a.amostras <= 0) return a;
    const double k = 1.0 / a.amostras;
    a.cpuProcesso *= k;
    a.cpuThreadRender *= k;
    a.cpuThreadMax *= k;
    a.threadsProcesso *= k;
    a.cpuSistema *= k;
    a.cpuNucleoMax *= k;
    for (int g = 0; g < NUM_GRUPOS_GPU; ++g) {
        a.gpu3dProcesso[g] *= k;
        a.gpuSistema[g] *= k;
        a.gpuMemProcessoMB[g] *= k;
    }
    a.nvUtil *= k;
    a.nvMemUtil *= k;
    a.nvPotenciaW *= k;
    a.nvTempC *= k;
    a.nvClockSM *= k;
    a.nvClockMem *= k;
    a.nvVramUsadaMB *= k;
    a.ramProcessoMB *= k;
    a.ramSistemaPct *= k;
    return a;
}

int Monitor::grupoDoFabricante(unsigned fabricante) {
    if (fabricante == 0x10DE) return GPU_NVIDIA;
    if (fabricante == 0x8086) return GPU_INTEL;
    return GPU_OUTRA;
}

std::string Monitor::descreverMotivosReducao(std::uint64_t m) {
    static const std::pair<std::uint64_t, const char*> nomes[] = {
        {0x2, "clocks-aplicacao"},   {0x4, "limite-potencia(SW)"}, {0x8, "desaceleracao-HW"},
        {0x10, "sync-boost"},        {0x20, "termico(SW)"},        {0x40, "termico(HW)"},
        {0x80, "freio-potencia(HW)"}, {0x100, "clock-display"}};
    std::string s;
    for (const auto& [bit, nome] : nomes)
        if (m & bit) s += (s.empty() ? "" : "+") + std::string(nome);
    return s.empty() ? "nenhum" : s;
}
