// ============================================================
//  SystemInfoPro - info_gpu.cpp
//  Placa de Video: DXGI (VRAM real) + WMI (driver) + NVML (sensores,
//  clocks, largura de banda de memoria) - similar ao SIW
// ============================================================
#include "common.h"
#include "wmi.h"
#include <dxgi1_4.h>

#pragma comment(lib, "dxgi.lib")

// =====================================================================
//  NVML (NVIDIA Management Library) - carregada dinamicamente.
//  Se nao houver GPU NVIDIA ou driver, tudo continua funcionando.
// =====================================================================
namespace
{
    typedef int (*PFN_nvmlInit)();
    typedef int (*PFN_nvmlDeviceGetHandleByIndex)(unsigned int, void**);
    typedef int (*PFN_nvmlDeviceGetName)(void*, char*, unsigned int);
    typedef int (*PFN_nvmlDeviceGetTemperature)(void*, int, unsigned int*);
    typedef int (*PFN_nvmlDeviceGetUtilizationRates)(void*, void*);
    typedef int (*PFN_nvmlDeviceGetClockInfo)(void*, int, unsigned int*);
    typedef int (*PFN_nvmlDeviceGetMemoryBusWidth)(void*, unsigned int*);
    typedef int (*PFN_nvmlDeviceGetMemoryInfo)(void*, void*);
    typedef int (*PFN_nvmlDeviceGetPowerUsage)(void*, unsigned int*);
    typedef int (*PFN_nvmlDeviceGetFanSpeed)(void*, unsigned int*);

    struct NvmlUtil { unsigned int gpu; unsigned int mem; };
    struct NvmlMem  { unsigned long long total, livre, usada; };

    struct NvmlEstado
    {
        bool tentado = false;
        bool ok = false;
        HMODULE dll = nullptr;
        void* dev = nullptr;
        PFN_nvmlDeviceGetName            pGetName = nullptr;
        PFN_nvmlDeviceGetTemperature     pGetTemp = nullptr;
        PFN_nvmlDeviceGetUtilizationRates pGetUtil = nullptr;
        PFN_nvmlDeviceGetClockInfo       pGetClock = nullptr;
        PFN_nvmlDeviceGetMemoryBusWidth  pGetBus = nullptr;
        PFN_nvmlDeviceGetMemoryInfo      pGetMem = nullptr;
        PFN_nvmlDeviceGetPowerUsage      pGetPow = nullptr;
        PFN_nvmlDeviceGetFanSpeed        pGetFan = nullptr;
    };
    NvmlEstado g_nvml;

    void NvmlIniciar()
    {
        if (g_nvml.tentado) return;
        g_nvml.tentado = true;

        // O driver NVIDIA instala nvml.dll no System32
        g_nvml.dll = LoadLibraryW(L"nvml.dll");
        if (!g_nvml.dll)
            g_nvml.dll = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
        if (!g_nvml.dll) return;

        auto pInit = (PFN_nvmlInit)GetProcAddress(g_nvml.dll, "nvmlInit_v2");
        if (!pInit) pInit = (PFN_nvmlInit)GetProcAddress(g_nvml.dll, "nvmlInit");
        auto pByIdx = (PFN_nvmlDeviceGetHandleByIndex)GetProcAddress(g_nvml.dll, "nvmlDeviceGetHandleByIndex_v2");
        if (!pByIdx) pByIdx = (PFN_nvmlDeviceGetHandleByIndex)GetProcAddress(g_nvml.dll, "nvmlDeviceGetHandleByIndex");

        g_nvml.pGetName  = (PFN_nvmlDeviceGetName)GetProcAddress(g_nvml.dll, "nvmlDeviceGetName");
        g_nvml.pGetTemp  = (PFN_nvmlDeviceGetTemperature)GetProcAddress(g_nvml.dll, "nvmlDeviceGetTemperature");
        g_nvml.pGetUtil  = (PFN_nvmlDeviceGetUtilizationRates)GetProcAddress(g_nvml.dll, "nvmlDeviceGetUtilizationRates");
        g_nvml.pGetClock = (PFN_nvmlDeviceGetClockInfo)GetProcAddress(g_nvml.dll, "nvmlDeviceGetClockInfo");
        g_nvml.pGetBus   = (PFN_nvmlDeviceGetMemoryBusWidth)GetProcAddress(g_nvml.dll, "nvmlDeviceGetMemoryBusWidth");
        g_nvml.pGetMem   = (PFN_nvmlDeviceGetMemoryInfo)GetProcAddress(g_nvml.dll, "nvmlDeviceGetMemoryInfo");
        g_nvml.pGetPow   = (PFN_nvmlDeviceGetPowerUsage)GetProcAddress(g_nvml.dll, "nvmlDeviceGetPowerUsage");
        g_nvml.pGetFan   = (PFN_nvmlDeviceGetFanSpeed)GetProcAddress(g_nvml.dll, "nvmlDeviceGetFanSpeed");

        if (!pInit || !pByIdx) return;
        if (pInit() != 0) return;                       // NVML_SUCCESS = 0
        if (pByIdx(0, &g_nvml.dev) != 0) return;
        g_nvml.ok = true;
    }
}

bool NvmlConsultar(NvmlInfo& s)
{
    NvmlIniciar();
    if (!g_nvml.ok) return false;
    s.disponivel = true;

    char nome[128] = {};
    if (g_nvml.pGetName && g_nvml.pGetName(g_nvml.dev, nome, sizeof(nome)) == 0)
    {
        wchar_t w[128];
        MultiByteToWideChar(CP_ACP, 0, nome, -1, w, 128);
        s.nome = w;
    }
    if (g_nvml.pGetTemp)  g_nvml.pGetTemp(g_nvml.dev, 0 /*NVML_TEMPERATURE_GPU*/, &s.temperaturaC);
    if (g_nvml.pGetUtil)
    {
        NvmlUtil u{};
        if (g_nvml.pGetUtil(g_nvml.dev, &u) == 0) { s.usoGpuPct = u.gpu; s.usoMemPct = u.mem; }
    }
    if (g_nvml.pGetClock)
    {
        g_nvml.pGetClock(g_nvml.dev, 0 /*NVML_CLOCK_GRAPHICS*/, &s.clockCoreMHz);
        g_nvml.pGetClock(g_nvml.dev, 2 /*NVML_CLOCK_MEM*/, &s.clockMemMHz);
    }
    if (g_nvml.pGetBus) g_nvml.pGetBus(g_nvml.dev, &s.larguraBusBits);
    if (g_nvml.pGetMem)
    {
        NvmlMem m{};
        if (g_nvml.pGetMem(g_nvml.dev, &m) == 0) { s.vramTotal = m.total; s.vramUsada = m.usada; }
    }
    if (g_nvml.pGetPow) g_nvml.pGetPow(g_nvml.dev, &s.potenciaMw);
    if (g_nvml.pGetFan) g_nvml.pGetFan(g_nvml.dev, &s.fanPct);

    // Largura de banda teorica: clockMem (MHz, ja com DDR aplicado 1x) * 2 * bus/8
    if (s.clockMemMHz > 0 && s.larguraBusBits > 0)
        s.bandaGBs = (double)s.clockMemMHz * 2.0 * (s.larguraBusBits / 8.0) / 1000.0;

    return true;
}

// =====================================================================
//  Pagina: Placa de Video (GPU)
// =====================================================================
void LoadGPU()
{
    // ---------- 1) Adaptadores via DXGI (VRAM real, mesmo > 4GB) ----------
    AddSection(L"ADAPTADORES DE VIDEO (DXGI)");
    IDXGIFactory1* fabrica = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fabrica)) && fabrica)
    {
        UINT i = 0;
        IDXGIAdapter1* adap = nullptr;
        int contador = 1;
        while (fabrica->EnumAdapters1(i, &adap) != DXGI_ERROR_NOT_FOUND)
        {
            DXGI_ADAPTER_DESC1 d{};
            adap->GetDesc1(&d);
            if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                AddRow(L"GPU " + NumW(contador) + L" - Modelo", d.Description);

                std::wstring fabricante = L"Desconhecido";
                if (d.VendorId == 0x10DE) fabricante = L"NVIDIA";
                else if (d.VendorId == 0x1002 || d.VendorId == 0x1022) fabricante = L"AMD";
                else if (d.VendorId == 0x8086) fabricante = L"Intel";
                AddRow(L"  Fabricante do Chip", fabricante +
                    L"  (VendorID: 0x" + [&]{ wchar_t b[8]; swprintf_s(b, L"%04X", d.VendorId); return std::wstring(b); }() + L")");

                AddRow(L"  VRAM Dedicada", FormatBytes(d.DedicatedVideoMemory));
                AddRow(L"  Memoria de Sistema Dedicada", FormatBytes(d.DedicatedSystemMemory));
                AddRow(L"  Memoria de Sistema Compartilhada", FormatBytes(d.SharedSystemMemory));

                // Uso de VRAM em tempo real (Windows 10+, IDXGIAdapter3)
                IDXGIAdapter3* adap3 = nullptr;
                if (SUCCEEDED(adap->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&adap3)) && adap3)
                {
                    DXGI_QUERY_VIDEO_MEMORY_INFO vm{};
                    if (SUCCEEDED(adap3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vm)))
                        AddRow(L"  VRAM em Uso Agora", FormatBytes(vm.CurrentUsage) +
                               L" de " + FormatBytes(vm.Budget) + L" disponiveis");
                    adap3->Release();
                }
                AddBlank();
                contador++;
            }
            adap->Release();
            i++;
        }
        fabrica->Release();
    }

    // ---------- 2) Driver e modo de video via WMI ----------
    AddSection(L"DRIVER E MODO DE VIDEO (WMI)");
    {
        Wmi wmi;
        wmi.Query(L"SELECT Name, DriverVersion, DriverDate, VideoProcessor, CurrentBitsPerPixel, "
                  L"CurrentHorizontalResolution, CurrentVerticalResolution, CurrentRefreshRate, "
                  L"VideoModeDescription FROM Win32_VideoController",
        [](IWbemClassObject* o)
        {
            AddRow(L"Controladora", Wmi::GetStr(o, L"Name"));
            std::wstring proc = Wmi::GetStr(o, L"VideoProcessor");
            if (!proc.empty()) AddRow(L"  Processador Grafico", proc);
            AddRow(L"  Versao do Driver", Wmi::GetStr(o, L"DriverVersion"));
            std::wstring dataDrv = Wmi::GetStr(o, L"DriverDate");
            if (!dataDrv.empty()) AddRow(L"  Data do Driver", CimDateParaTexto(dataDrv));
            unsigned long long hres = Wmi::GetUint(o, L"CurrentHorizontalResolution");
            if (hres > 0)
            {
                AddRow(L"  Resolucao Atual", NumW(hres) + L" x " + NumW(Wmi::GetUint(o, L"CurrentVerticalResolution")));
                AddRow(L"  Taxa de Atualizacao", NumW(Wmi::GetUint(o, L"CurrentRefreshRate")) + L" Hz");
                AddRow(L"  Profundidade de Cores", NumW(Wmi::GetUint(o, L"CurrentBitsPerPixel")) + L" Bits");
            }
            AddBlank();
        });
    }

    // ---------- 3) Sensores e banda via NVML (NVIDIA) ----------
    AddSection(L"SENSORES E LARGURA DE BANDA (NVIDIA NVML)");
    NvmlInfo nv;
    if (NvmlConsultar(nv))
    {
        AddRow(L"GPU", nv.nome);
        AddRow(L"Temperatura Atual", NumW(nv.temperaturaC) + L" °C");
        AddRow(L"Uso da GPU", NumW(nv.usoGpuPct) + L" %");
        AddRow(L"Uso do Controlador de Memoria", NumW(nv.usoMemPct) + L" %");
        AddRow(L"Clock do Nucleo (atual)", NumW(nv.clockCoreMHz) + L" MHz");
        AddRow(L"Clock da Memoria (atual)", NumW(nv.clockMemMHz) + L" MHz");
        AddRow(L"Largura do Barramento de Memoria", NumW(nv.larguraBusBits) + L" bits");
        if (nv.bandaGBs > 0)
            AddRow(L"Largura de Banda da VRAM (teorica)", NumW(nv.bandaGBs, 1) + L" GB/s");
        AddRow(L"VRAM Total / Em Uso", FormatBytes(nv.vramTotal) + L" / " + FormatBytes(nv.vramUsada));
        if (nv.potenciaMw > 0) AddRow(L"Consumo de Energia", NumW(nv.potenciaMw / 1000.0, 1) + L" W");
        if (nv.fanPct > 0) AddRow(L"Velocidade do Cooler", NumW(nv.fanPct) + L" %");
    }
    else
    {
        AddRow(L"Status", L"NVML indisponivel (GPU nao-NVIDIA ou driver ausente).");
        AddRow(L"Observacao", L"Em GPUs AMD/Intel, o uso em tempo real aparece no Painel de Monitoramento.");
    }
}

// =====================================================================
//  Pagina: Telas e Monitores
// =====================================================================
static BOOL CALLBACK MonitorCb(HMONITOR hMon, HDC, LPRECT, LPARAM contadorPtr)
{
    int* contador = (int*)contadorPtr;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi))
    {
        std::wstring principal = (mi.dwFlags & MONITORINFOF_PRIMARY) ? L" (Tela Principal)" : L"";
        AddRow(L"Monitor " + NumW(*contador) + principal, mi.szDevice);
        AddRow(L"  Resolucao Fisica Atual",
            NumW(mi.rcMonitor.right - mi.rcMonitor.left) + L" x " + NumW(mi.rcMonitor.bottom - mi.rcMonitor.top) + L" Pixels");
        AddRow(L"  Area de Trabalho Util",
            NumW(mi.rcWork.right - mi.rcWork.left) + L" x " + NumW(mi.rcWork.bottom - mi.rcWork.top) + L" Pixels");

        DEVMODEW dm{}; dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
            AddRow(L"  Taxa de Atualizacao", NumW(dm.dmDisplayFrequency) + L" Hz");
        AddBlank();
        (*contador)++;
    }
    return TRUE;
}

void LoadMonitors()
{
    AddSection(L"MONITORES CONECTADOS E RECONHECIDOS");
    int contador = 1;
    EnumDisplayMonitors(nullptr, nullptr, MonitorCb, (LPARAM)&contador);

    AddSection(L"ESPECIFICACOES DO FABRICANTE (EDID / WMI)");
    Wmi wmi(L"ROOT\\WMI");
    bool achou = false;
    wmi.Query(L"SELECT ManufacturerName, UserFriendlyName, ProductCodeID, SerialNumberID, YearOfManufacture "
              L"FROM WmiMonitorID",
    [&achou](IWbemClassObject* o)
    {
        std::wstring nome = Wmi::GetU16Array(o, L"UserFriendlyName");
        std::wstring fab  = Wmi::GetU16Array(o, L"ManufacturerName");
        if (!nome.empty()) { AddRow(L"Modelo do Painel", nome); achou = true; }
        if (!fab.empty())  AddRow(L"  Fabricante (codigo EDID)", fab);
        std::wstring serie = Wmi::GetU16Array(o, L"SerialNumberID");
        if (!serie.empty()) AddRow(L"  Numero de Serie", serie);
        unsigned long long ano = Wmi::GetUint(o, L"YearOfManufacture");
        if (ano > 1990) AddRow(L"  Ano de Fabricacao", NumW(ano));
        AddBlank();
    });
    if (!achou) AddRow(L"Status", L"Nenhuma informacao EDID disponivel.");
}
