// ============================================================
//  SystemInfoPro - info_cpu.cpp
//  Processador: WMI + CPUID (instrucoes suportadas) + caches L1/L2/L3
// ============================================================
#include "common.h"
#include "wmi.h"
#include <intrin.h>

// ---------- CPUID: nome e instrucoes ----------
static std::wstring CpuidNome()
{
    int regs[4] = {};
    char marca[49] = {};
    __cpuid(regs, 0x80000000);
    if ((unsigned)regs[0] >= 0x80000004)
    {
        __cpuid((int*)(marca + 0),  0x80000002);
        __cpuid((int*)(marca + 16), 0x80000003);
        __cpuid((int*)(marca + 32), 0x80000004);
        wchar_t w[64];
        MultiByteToWideChar(CP_ACP, 0, marca, -1, w, 64);
        return Trim(w);
    }
    return L"";
}

static std::wstring CpuidInstrucoes()
{
    int r1[4] = {}, r7[4] = {}, r81[4] = {};
    __cpuid(r1, 1);
    __cpuidex(r7, 7, 0);
    __cpuid(r81, 0x80000001);

    std::wstring s;
    auto Add = [&](bool tem, const wchar_t* nome) { if (tem) { if (!s.empty()) s += L", "; s += nome; } };

    Add((r1[3] >> 25) & 1, L"SSE");
    Add((r1[3] >> 26) & 1, L"SSE2");
    Add((r1[2] >> 0) & 1,  L"SSE3");
    Add((r1[2] >> 9) & 1,  L"SSSE3");
    Add((r1[2] >> 19) & 1, L"SSE4.1");
    Add((r1[2] >> 20) & 1, L"SSE4.2");
    Add((r1[2] >> 28) & 1, L"AVX");
    Add((r7[1] >> 5) & 1,  L"AVX2");
    Add((r7[1] >> 16) & 1, L"AVX-512F");
    Add((r1[2] >> 25) & 1, L"AES-NI");
    Add((r1[2] >> 1) & 1,  L"PCLMULQDQ");
    Add((r1[2] >> 12) & 1, L"FMA3");
    Add((r7[1] >> 29) & 1, L"SHA");
    Add((r1[2] >> 30) & 1, L"RDRAND");
    Add((r1[2] >> 5) & 1,  L"VT-x/Virtualizacao (VMX)");
    Add((r81[2] >> 2) & 1, L"AMD-V (SVM)");
    Add((r81[3] >> 29) & 1, L"x86-64 (64 bits)");
    return s;
}

// ---------- Caches via GetLogicalProcessorInformation ----------
static void CachesDetalhados()
{
    DWORD tam = 0;
    GetLogicalProcessorInformation(nullptr, &tam);
    if (tam == 0) return;
    std::vector<BYTE> buf(tam);
    auto* info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)buf.data();
    if (!GetLogicalProcessorInformation(info, &tam)) return;

    DWORD qtd = tam / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    unsigned long long l1d = 0, l1i = 0, l2 = 0, l3 = 0;
    int nL1d = 0, nL1i = 0, nL2 = 0, nL3 = 0;
    for (DWORD i = 0; i < qtd; i++)
    {
        if (info[i].Relationship != RelationCache) continue;
        const CACHE_DESCRIPTOR& c = info[i].Cache;
        if (c.Level == 1 && c.Type == CacheData)        { l1d = c.Size; nL1d++; }
        else if (c.Level == 1 && c.Type == CacheInstruction) { l1i = c.Size; nL1i++; }
        else if (c.Level == 2) { l2 = c.Size; nL2++; }
        else if (c.Level == 3) { l3 = c.Size; nL3++; }
    }
    if (nL1d) AddRow(L"Cache L1 de Dados", NumW(nL1d) + L" x " + FormatBytes(l1d));
    if (nL1i) AddRow(L"Cache L1 de Instrucoes", NumW(nL1i) + L" x " + FormatBytes(l1i));
    if (nL2)  AddRow(L"Cache L2", NumW(nL2) + L" x " + FormatBytes(l2));
    if (nL3)  AddRow(L"Cache L3", NumW(nL3) + L" x " + FormatBytes(l3));
}

void LoadCPU()
{
    Wmi wmi;
    int socket = 1;
    wmi.Query(L"SELECT Name, Manufacturer, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed, "
              L"CurrentClockSpeed, SocketDesignation, Description, ProcessorId, "
              L"L2CacheSize, L3CacheSize, VirtualizationFirmwareEnabled FROM Win32_Processor",
    [&socket](IWbemClassObject* o)
    {
        AddSection(L"PROCESSADOR FISICO " + NumW(socket));
        std::wstring nome = Wmi::GetStr(o, L"Name");
        std::wstring nomeCpuid = CpuidNome();
        AddRow(L"Modelo", !nomeCpuid.empty() ? nomeCpuid : Trim(nome));
        AddRow(L"Fabricante", Wmi::GetStr(o, L"Manufacturer"));
        AddRow(L"Identificacao (Stepping/Familia)", Wmi::GetStr(o, L"Description"));
        std::wstring pid = Wmi::GetStr(o, L"ProcessorId");
        if (!pid.empty()) AddRow(L"ProcessorID", pid);
        AddRow(L"Soquete (Socket)", Wmi::GetStr(o, L"SocketDesignation"));
        AddRow(L"Nucleos Fisicos", NumW(Wmi::GetUint(o, L"NumberOfCores")));
        AddRow(L"Processadores Logicos (Threads)", NumW(Wmi::GetUint(o, L"NumberOfLogicalProcessors")));
        AddRow(L"Frequencia Base / Maxima", NumW(Wmi::GetUint(o, L"CurrentClockSpeed")) + L" MHz / " +
                                            NumW(Wmi::GetUint(o, L"MaxClockSpeed")) + L" MHz");
        if (Wmi::HasValue(o, L"VirtualizationFirmwareEnabled"))
            AddRow(L"Virtualizacao Habilitada no BIOS",
                   Wmi::GetBool(o, L"VirtualizationFirmwareEnabled") ? L"Sim" : L"Nao");
        AddBlank();
        socket++;
    });

    AddSection(L"CACHES (LEITURA DIRETA DO PROCESSADOR)");
    CachesDetalhados();

    AddBlank();
    AddSection(L"CONJUNTOS DE INSTRUCOES SUPORTADOS (CPUID)");
    AddRow(L"Instrucoes", CpuidInstrucoes());
}
