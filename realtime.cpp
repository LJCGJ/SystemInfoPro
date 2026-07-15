// ============================================================
//  SystemInfoPro - realtime.cpp
//  Painel de monitoramento em tempo real:
//  CPU total + por nucleo, RAM, GPU (uso/VRAM), Rede (down/up)
// ============================================================
#include "common.h"
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")

// ---------------- Estado persistente ----------------
static FILETIME g_idleAnt{}, g_kernAnt{}, g_userAnt{};
static bool g_temAmostraCpu = false;

// Por nucleo (NtQuerySystemInformation)
typedef struct _SIP_PERF
{
    LARGE_INTEGER IdleTime, KernelTime, UserTime, DpcTime, InterruptTime;
    ULONG InterruptCount;
} SIP_PERF;
typedef LONG(WINAPI* PFN_NtQSI)(ULONG, PVOID, ULONG, PULONG);
static std::vector<SIP_PERF> g_coreAnt;

// Rede
static unsigned long long g_rxAnt = 0, g_txAnt = 0;
static ULONGLONG g_tickAnt = 0;
static bool g_temAmostraRede = false;

// PDH para uso de GPU (contadores "GPU Engine")
static PDH_HQUERY g_pdhQuery = nullptr;
static PDH_HCOUNTER g_pdhGpu = nullptr;
static bool g_pdhOk = false;

// ---------------- Inicializacao / limpeza ----------------
static void PdhIniciar()
{
    if (g_pdhQuery) return;
    if (PdhOpenQueryW(nullptr, 0, &g_pdhQuery) != ERROR_SUCCESS) { g_pdhQuery = nullptr; return; }
    // PdhAddEnglishCounter funciona em qualquer idioma do Windows
    if (PdhAddEnglishCounterW(g_pdhQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_pdhGpu) != ERROR_SUCCESS)
        g_pdhGpu = nullptr;
    PdhCollectQueryData(g_pdhQuery);   // primeira coleta (base)
    g_pdhOk = (g_pdhGpu != nullptr);
}

void RealtimeReset()
{
    g_temAmostraCpu = false;
    g_temAmostraRede = false;
    g_coreAnt.clear();
    PdhIniciar();
}

void RealtimeShutdown()
{
    if (g_pdhQuery) { PdhCloseQuery(g_pdhQuery); g_pdhQuery = nullptr; g_pdhGpu = nullptr; }
}

// ---------------- Medidores ----------------
static int MedirCpuTotal()
{
    FILETIME idle, kern, user;
    if (!GetSystemTimes(&idle, &kern, &user)) return -1;

    auto Dif = [](const FILETIME& a, const FILETIME& b) -> unsigned long long
    {
        ULARGE_INTEGER ua{ a.dwLowDateTime, a.dwHighDateTime };
        ULARGE_INTEGER ub{ b.dwLowDateTime, b.dwHighDateTime };
        return ub.QuadPart - ua.QuadPart;
    };

    int pct = -1;
    if (g_temAmostraCpu)
    {
        unsigned long long i = Dif(g_idleAnt, idle);
        unsigned long long k = Dif(g_kernAnt, kern);
        unsigned long long u = Dif(g_userAnt, user);
        unsigned long long total = k + u;          // kernel ja inclui idle
        if (total > 0) pct = (int)(100 - (i * 100) / total);
    }
    g_idleAnt = idle; g_kernAnt = kern; g_userAnt = user;
    g_temAmostraCpu = true;
    return pct;
}

static std::vector<int> MedirCpuPorNucleo()
{
    std::vector<int> res;
    static PFN_NtQSI ntqsi = (PFN_NtQSI)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    if (!ntqsi) return res;

    SYSTEM_INFO si; GetSystemInfo(&si);
    ULONG nuc = si.dwNumberOfProcessors;
    std::vector<SIP_PERF> atual(nuc);
    ULONG retLen = 0;
    // 8 = SystemProcessorPerformanceInformation
    if (ntqsi(8, atual.data(), (ULONG)(sizeof(SIP_PERF) * nuc), &retLen) != 0) return res;

    if (g_coreAnt.size() == nuc)
    {
        for (ULONG c = 0; c < nuc; c++)
        {
            LONGLONG idle  = atual[c].IdleTime.QuadPart   - g_coreAnt[c].IdleTime.QuadPart;
            LONGLONG kern  = atual[c].KernelTime.QuadPart - g_coreAnt[c].KernelTime.QuadPart;
            LONGLONG user  = atual[c].UserTime.QuadPart   - g_coreAnt[c].UserTime.QuadPart;
            LONGLONG total = kern + user;
            int pct = (total > 0) ? (int)(100 - (idle * 100) / total) : 0;
            if (pct < 0) pct = 0; if (pct > 100) pct = 100;
            res.push_back(pct);
        }
    }
    g_coreAnt = atual;
    return res;
}

static double MedirGpuPdh()
{
    if (!g_pdhOk || !g_pdhQuery) return -1.0;
    if (PdhCollectQueryData(g_pdhQuery) != ERROR_SUCCESS) return -1.0;

    DWORD tam = 0, qtd = 0;
    PdhGetFormattedCounterArrayW(g_pdhGpu, PDH_FMT_DOUBLE, &tam, &qtd, nullptr);
    if (tam == 0) return -1.0;
    std::vector<BYTE> buf(tam);
    PDH_FMT_COUNTERVALUE_ITEM_W* itens = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
    if (PdhGetFormattedCounterArrayW(g_pdhGpu, PDH_FMT_DOUBLE, &tam, &qtd, itens) != ERROR_SUCCESS)
        return -1.0;

    double soma = 0;
    for (DWORD i = 0; i < qtd; i++)
        if (itens[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || itens[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
            soma += itens[i].FmtValue.doubleValue;
    if (soma > 100.0) soma = 100.0;
    return soma;
}

static void MedirRede(double& downBps, double& upBps)
{
    downBps = -1; upBps = -1;
    MIB_IF_TABLE2* tabela = nullptr;
    if (GetIfTable2(&tabela) != NO_ERROR || !tabela) return;

    unsigned long long rx = 0, tx = 0;
    for (ULONG i = 0; i < tabela->NumEntries; i++)
    {
        MIB_IF_ROW2& r = tabela->Table[i];
        // ignora loopback e interfaces desligadas
        if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (r.OperStatus != IfOperStatusUp) continue;
        rx += r.InOctets;
        tx += r.OutOctets;
    }
    FreeMibTable(tabela);

    ULONGLONG agora = GetTickCount64();
    if (g_temAmostraRede && agora > g_tickAnt)
    {
        double seg = (agora - g_tickAnt) / 1000.0;
        downBps = ((double)(rx - g_rxAnt) * 8.0) / seg;
        upBps   = ((double)(tx - g_txAnt) * 8.0) / seg;
        if (downBps < 0) downBps = 0;
        if (upBps < 0) upBps = 0;
    }
    g_rxAnt = rx; g_txAnt = tx; g_tickAnt = agora;
    g_temAmostraRede = true;
}

static std::wstring BarraTexto(int pct)
{
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    int blocos = pct / 5;                       // 20 blocos
    std::wstring b;
    for (int i = 0; i < 20; i++) b += (i < blocos) ? L'█' : L'░';
    return b + L"  " + NumW(pct) + L"%";
}

// ---------------- Tela de tempo real ----------------
void LoadRealtime()
{
    // --- CPU ---
    int cpu = MedirCpuTotal();
    SetRowByKey(L"SEC_CPU", L"--- PROCESSADOR ---", L"");
    SetRowByKey(L"USO_CPU", L"Uso Total do Processador (CPU)",
                (cpu >= 0) ? BarraTexto(cpu) : L"calculando...");

    std::vector<int> nucleos = MedirCpuPorNucleo();
    for (size_t c = 0; c < nucleos.size(); c++)
    {
        SetRowByKey(L"CORE_" + NumW((int)c),
                    L"  Nucleo Logico " + NumW((int)c),
                    BarraTexto(nucleos[c]));
    }

    // --- RAM ---
    MEMORYSTATUSEX mem{ sizeof(mem) };
    GlobalMemoryStatusEx(&mem);
    unsigned long long usada = mem.ullTotalPhys - mem.ullAvailPhys;
    SetRowByKey(L"SEC_RAM", L"--- MEMORIA ---", L"");
    SetRowByKey(L"USO_RAM", L"Uso da Memoria RAM",
        FormatBytes(usada) + L" em uso de " + FormatBytes(mem.ullTotalPhys) +
        L"  (" + NumW(mem.dwMemoryLoad) + L"%)");
    SetRowByKey(L"RAM_LIVRE", L"Memoria Fisica Disponivel", FormatBytes(mem.ullAvailPhys));
    SetRowByKey(L"RAM_VIRT", L"Memoria Virtual (Commit)",
        FormatBytes(mem.ullTotalPageFile - mem.ullAvailPageFile) + L" de " + FormatBytes(mem.ullTotalPageFile));

    // --- GPU ---
    SetRowByKey(L"SEC_GPU", L"--- PLACA DE VIDEO ---", L"");
    double gpu = MedirGpuPdh();
    SetRowByKey(L"USO_GPU", L"Uso Total da GPU (todos os motores)",
        (gpu >= 0) ? BarraTexto((int)(gpu + 0.5)) : L"indisponivel neste Windows");

    NvmlInfo nv;
    if (NvmlConsultar(nv))
    {
        SetRowByKey(L"NV_NOME", L"GPU NVIDIA Detectada", nv.nome);
        SetRowByKey(L"NV_TEMP", L"Temperatura da GPU", NumW(nv.temperaturaC) + L" °C");
        SetRowByKey(L"NV_VRAM", L"VRAM em Uso",
            FormatBytes(nv.vramUsada) + L" de " + FormatBytes(nv.vramTotal));
        SetRowByKey(L"NV_MEMCTL", L"Uso do Controlador de Memoria (Banda)", BarraTexto(nv.usoMemPct));
        SetRowByKey(L"NV_CLK", L"Clocks Atuais (Nucleo / Memoria)",
            NumW(nv.clockCoreMHz) + L" MHz / " + NumW(nv.clockMemMHz) + L" MHz");
        if (nv.potenciaMw > 0)
            SetRowByKey(L"NV_POW", L"Consumo de Energia da GPU", NumW(nv.potenciaMw / 1000.0, 1) + L" W");
    }

    // --- REDE ---
    double down = 0, up = 0;
    MedirRede(down, up);
    SetRowByKey(L"SEC_NET", L"--- REDE ---", L"");
    SetRowByKey(L"NET_DOWN", L"Velocidade de Download (agora)",
        (down >= 0) ? FormatSpeedBits(down) : L"calculando...");
    SetRowByKey(L"NET_UP", L"Velocidade de Upload (agora)",
        (up >= 0) ? FormatSpeedBits(up) : L"calculando...");

    // --- SISTEMA ---
    SetRowByKey(L"SEC_SYS", L"--- SISTEMA ---", L"");
    ULONGLONG ms = GetTickCount64();
    ULONGLONG seg = ms / 1000;
    SetRowByKey(L"UPTIME", L"Tempo Ligado (Uptime)",
        NumW(seg / 86400) + L" dias, " + NumW((seg % 86400) / 3600) + L" horas, " +
        NumW((seg % 3600) / 60) + L" minutos, " + NumW(seg % 60) + L" segundos");

    DWORD pids[2048]; DWORD bytes = 0;
    if (EnumProcesses(pids, sizeof(pids), &bytes))
        SetRowByKey(L"PROCS", L"Processos Ativos", NumW(bytes / sizeof(DWORD)));
}
