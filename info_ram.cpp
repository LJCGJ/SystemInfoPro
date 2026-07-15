// ============================================================
//  SystemInfoPro - info_ram.cpp
//  Memoria RAM: modulos, tipo DDR, frequencia, largura de banda
// ============================================================
#include "common.h"
#include "wmi.h"

static std::wstring TipoMemoria(int smbios, int memType)
{
    switch (smbios)
    {
    case 20: return L"DDR";
    case 21: return L"DDR2";
    case 24: return L"DDR3";
    case 26: return L"DDR4";
    case 34: return L"DDR5";
    case 35: return L"LPDDR5";
    }
    switch (memType)   // fallback (campo antigo)
    {
    case 20: return L"DDR";
    case 21: return L"DDR2";
    case 24: return L"DDR3";
    }
    return L"Desconhecido";
}

static std::wstring FormFactor(int ff)
{
    switch (ff)
    {
    case 8:  return L"DIMM (Desktop)";
    case 12: return L"SODIMM (Notebook)";
    }
    return L"";
}

void LoadRAM()
{
    Wmi wmi;
    unsigned long long totalBytes = 0;
    double bandaTotal = 0;
    int modulo = 1;

    wmi.Query(L"SELECT Capacity, Speed, ConfiguredClockSpeed, ConfiguredVoltage, DataWidth, TotalWidth, "
              L"Manufacturer, PartNumber, SerialNumber, DeviceLocator, BankLabel, "
              L"SMBIOSMemoryType, MemoryType, FormFactor FROM Win32_PhysicalMemory",
    [&](IWbemClassObject* o)
    {
        AddSection(L"MODULO " + NumW(modulo));

        unsigned long long cap = Wmi::GetUint(o, L"Capacity");
        totalBytes += cap;
        AddRow(L"Capacidade", FormatBytes(cap));
        AddRow(L"Tecnologia", TipoMemoria((int)Wmi::GetUint(o, L"SMBIOSMemoryType"),
                                          (int)Wmi::GetUint(o, L"MemoryType")));

        std::wstring ff = FormFactor((int)Wmi::GetUint(o, L"FormFactor"));
        if (!ff.empty()) AddRow(L"Formato Fisico", ff);

        unsigned long long velNominal = Wmi::GetUint(o, L"Speed");
        unsigned long long velConfig  = Wmi::GetUint(o, L"ConfiguredClockSpeed");
        AddRow(L"Frequencia Nominal", NumW(velNominal) + L" MT/s");
        if (velConfig > 0 && velConfig != velNominal)
            AddRow(L"Frequencia Configurada (em uso)", NumW(velConfig) + L" MT/s");

        unsigned long long larg = Wmi::GetUint(o, L"DataWidth", 64);
        unsigned long long vel = (velConfig > 0) ? velConfig : velNominal;
        if (vel > 0 && larg > 0)
        {
            double bandaGBs = (double)vel * (double)larg / 8.0 / 1000.0;
            bandaTotal += bandaGBs;
            AddRow(L"Largura de Banda Maxima (modulo)", NumW(bandaGBs, 2) + L" GB/s");
        }

        unsigned long long volt = Wmi::GetUint(o, L"ConfiguredVoltage");
        if (volt > 0) AddRow(L"Tensao Configurada", NumW(volt / 1000.0, 2) + L" V");

        AddRow(L"Slot (Localizacao)", Wmi::GetStr(o, L"DeviceLocator") +
               L"  " + Wmi::GetStr(o, L"BankLabel"));
        AddRow(L"Fabricante", Trim(Wmi::GetStr(o, L"Manufacturer")));
        AddRow(L"Part Number", Trim(Wmi::GetStr(o, L"PartNumber")));
        std::wstring serie = Trim(Wmi::GetStr(o, L"SerialNumber"));
        if (!serie.empty()) AddRow(L"Numero de Serie", serie);
        AddBlank();
        modulo++;
    });

    AddSection(L"RESUMO");
    AddRow(L"Memoria RAM Total Instalada", FormatBytes(totalBytes));
    int canais = modulo - 1;
    if (canais >= 2)
        AddRow(L"Modulos Instalados", NumW(canais) + L" (possivel operacao multi-canal)");
    else
        AddRow(L"Modulos Instalados", NumW(canais));
    if (bandaTotal > 0)
        AddRow(L"Largura de Banda Teorica Combinada", NumW(bandaTotal, 2) + L" GB/s");

    // Slots totais da placa-mae
    Wmi wmi2;
    wmi2.Query(L"SELECT MemoryDevices FROM Win32_PhysicalMemoryArray",
    [](IWbemClassObject* o)
    {
        unsigned long long slots = Wmi::GetUint(o, L"MemoryDevices");
        if (slots > 0) AddRow(L"Slots de Memoria na Placa-Mae", NumW(slots));
    });
}
