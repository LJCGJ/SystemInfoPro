// ============================================================
//  SystemInfoPro - info_summary.cpp
//  Resumo do Sistema (dashboard): visao geral em uma tela so -
//  a primeira coisa que o usuario ve ao abrir o programa.
//  Tambem: iniciar o app com o Windows.
// ============================================================
#include "common.h"
#include "wmi.h"

// ================= Resumo do Sistema =================
void LoadResumo()
{
    AddSection(L"VISAO GERAL DO SISTEMA");

    // ---- Computador / SO ----
    Wmi wmi;
    std::wstring fabricante, modelo;
    wmi.Query(L"SELECT Manufacturer, Model FROM Win32_ComputerSystem",
    [&](IWbemClassObject* o)
    {
        fabricante = Wmi::GetStr(o, L"Manufacturer");
        modelo = Wmi::GetStr(o, L"Model");
    });
    if (!modelo.empty())
        AddRow(L"💻 Computador", Trim(fabricante + L" " + modelo));

    wmi.Query(L"SELECT Caption, BuildNumber FROM Win32_OperatingSystem",
    [&](IWbemClassObject* o)
    {
        AddRow(L"🪟 Sistema Operacional",
               Trim(Wmi::GetStr(o, L"Caption")) + L"  (build " + Wmi::GetStr(o, L"BuildNumber") + L")");
    });

    wchar_t nomePc[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD tamPc = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(nomePc, &tamPc);
    wchar_t usuario[256] = {}; DWORD tamU = 256;
    GetUserNameW(usuario, &tamU);
    AddRow(L"🏷 Nome / Usuario", std::wstring(nomePc) + L"  \\  " + usuario);

    AddBlank();

    // ---- CPU ----
    wmi.Query(L"SELECT Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed FROM Win32_Processor",
    [&](IWbemClassObject* o)
    {
        AddRow(L"⚙ Processador", Trim(Wmi::GetStr(o, L"Name")));
        AddRow(L"   Nucleos / Threads",
               NumW(Wmi::GetUint(o, L"NumberOfCores")) + L" nucleos, " +
               NumW(Wmi::GetUint(o, L"NumberOfLogicalProcessors")) + L" threads @ " +
               NumW(Wmi::GetUint(o, L"MaxClockSpeed")) + L" MHz");
    });

    // ---- RAM ----
    MEMORYSTATUSEX mem{ sizeof(mem) };
    GlobalMemoryStatusEx(&mem);
    unsigned long long usada = mem.ullTotalPhys - mem.ullAvailPhys;
    AddRow(L"🧠 Memoria RAM",
           FormatBytes(mem.ullTotalPhys) + L" total  |  em uso: " +
           FormatBytes(usada) + L" (" + NumW(mem.dwMemoryLoad) + L"%)");

    // ---- GPU ----
    wmi.Query(L"SELECT Name FROM Win32_VideoController",
    [&](IWbemClassObject* o)
    {
        std::wstring nome = Wmi::GetStr(o, L"Name");
        if (!nome.empty()) AddRow(L"🎮 Placa de Video", nome);
    });
    NvmlInfo nv;
    if (NvmlConsultar(nv))
        AddRow(L"   GPU (NVIDIA) agora",
               L"uso " + NumW(nv.usoGpuPct) + L"%, " + NumW(nv.temperaturaC) + L" °C, VRAM " +
               FormatBytes(nv.vramUsada) + L" / " + FormatBytes(nv.vramTotal));

    AddBlank();

    // ---- Disco do sistema ----
    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    wchar_t raiz[4] = { winDir[0], L':', L'\\', 0 };
    ULARGE_INTEGER livre{}, total{};
    if (GetDiskFreeSpaceExW(raiz, &livre, &total, nullptr))
    {
        int pctUsado = total.QuadPart ? (int)((total.QuadPart - livre.QuadPart) * 100 / total.QuadPart) : 0;
        AddRow(std::wstring(L"💾 Disco do Sistema (") + raiz + L")",
               FormatBytes(total.QuadPart) + L" total  |  livre: " +
               FormatBytes(livre.QuadPart) + L"  (" + NumW(pctUsado) + L"% usado)");
    }

    // ---- Rede ----
    Metricas m;
    AmostrarMetricas(m);   // primeira amostra (velocidade vira 0, mas mostra que ha rede)

    // ---- Uptime ----
    ULONGLONG seg = GetTickCount64() / 1000;
    AddRow(L"⏱ Tempo Ligado",
           NumW(seg / 86400) + L" dias, " + NumW((seg % 86400) / 3600) + L" h, " +
           NumW((seg % 3600) / 60) + L" min");

    AddBlank();
    AddSection(L"ATALHOS RAPIDOS");
    AddRow(L"📈 Ver desempenho ao vivo", L"Sensores em Tempo Real → Graficos de Desempenho");
    AddRow(L"🚀 Testar o computador", L"Ferramentas → Benchmark do Sistema");
    AddRow(L"📊 Liberar espaco", L"Ferramentas → Analisador de Disco");
    AddRow(L"🗂 Exportar tudo", L"Arquivo → Exportar Relatorio Completo");
}

// ================= Iniciar o proprio app com o Windows =================
static const wchar_t* CHAVE_RUN = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* VALOR_RUN = L"SystemInfoPro";

bool AppIniciaComWindows()
{
    HKEY k = nullptr;
    bool existe = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CHAVE_RUN, 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        wchar_t buf[MAX_PATH] = {};
        DWORD tam = sizeof(buf), tipo = 0;
        existe = (RegQueryValueExW(k, VALOR_RUN, nullptr, &tipo, (LPBYTE)buf, &tam) == ERROR_SUCCESS);
        RegCloseKey(k);
    }
    return existe;
}

void AppDefinirIniciarComWindows(bool ativar)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CHAVE_RUN, 0, KEY_WRITE, &k) != ERROR_SUCCESS)
        return;

    if (ativar)
    {
        wchar_t caminho[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, caminho, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(caminho) + L"\"";
        RegSetValueExW(k, VALOR_RUN, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                       (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(k, VALOR_RUN);
    }
    RegCloseKey(k);
}
