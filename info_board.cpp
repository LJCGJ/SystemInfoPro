// ============================================================
//  SystemInfoPro - info_board.cpp
//  Placa-Mae/BIOS, USB, Audio, Impressoras, Bateria e Sensores Termicos
// ============================================================
#include "common.h"
#include "wmi.h"
#include <cstdlib>

// ================= Placa-Mae e BIOS =================
void LoadBoard()
{
    Wmi wmi;

    AddSection(L"ESPECIFICACOES DA PLACA-MAE");
    wmi.Query(L"SELECT Manufacturer, Product, SerialNumber, Version FROM Win32_BaseBoard",
    [](IWbemClassObject* o)
    {
        AddRow(L"Fabricante da Placa", Wmi::GetStr(o, L"Manufacturer"));
        AddRow(L"Modelo (Produto)", Wmi::GetStr(o, L"Product"));
        AddRow(L"Revisao / Versao", Wmi::GetStr(o, L"Version"));
        AddRow(L"Numero de Serie Fisico", Wmi::GetStr(o, L"SerialNumber"));
    });

    AddBlank();
    AddSection(L"ESPECIFICACOES DO BIOS / UEFI");
    wmi.Query(L"SELECT Manufacturer, Name, SMBIOSBIOSVersion, ReleaseDate FROM Win32_BIOS",
    [](IWbemClassObject* o)
    {
        AddRow(L"Desenvolvedor do BIOS", Wmi::GetStr(o, L"Manufacturer"));
        AddRow(L"Versao Instalada", Wmi::GetStr(o, L"Name"));
        std::wstring smb = Wmi::GetStr(o, L"SMBIOSBIOSVersion");
        if (!smb.empty()) AddRow(L"Versao SMBIOS", smb);
        std::wstring data = Wmi::GetStr(o, L"ReleaseDate");
        if (data.length() >= 8)
            AddRow(L"Data de Compilacao", data.substr(6, 2) + L"/" + data.substr(4, 2) + L"/" + data.substr(0, 4));
    });

    // Modo de boot (UEFI vs Legacy)
    AddBlank();
    AddSection(L"MODO DE INICIALIZACAO");
    {
        // Se a variavel de firmware puder ser lida, o sistema esta em UEFI
        FIRMWARE_TYPE tipo = FirmwareTypeUnknown;
        typedef BOOL(WINAPI* PFN_GetFw)(PFIRMWARE_TYPE);
        auto p = (PFN_GetFw)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetFirmwareType");
        if (p && p(&tipo))
        {
            AddRow(L"Tipo de Firmware", tipo == FirmwareTypeUefi ? L"UEFI" :
                                        tipo == FirmwareTypeBios ? L"BIOS Legado (CSM)" : L"Desconhecido");
        }
    }

    AddBlank();
    AddSection(L"CHASSI / GABINETE");
    wmi.Query(L"SELECT Manufacturer, ChassisTypes, SerialNumber FROM Win32_SystemEnclosure",
    [](IWbemClassObject* o)
    {
        std::wstring fab = Wmi::GetStr(o, L"Manufacturer");
        if (!fab.empty()) AddRow(L"Fabricante do Chassi", fab);
    });
}

// ================= Sensores Termicos =================
void LoadTemperatures()
{
    AddSection(L"SENSORES TERMICOS");
    int achados = 0;

    // 1) GPU NVIDIA via NVML (leitura real do silicio)
    NvmlInfo nv;
    if (NvmlConsultar(nv))
    {
        AddRow(L"GPU (" + nv.nome + L")", NumW(nv.temperaturaC) + L" °C");
        achados++;
    }

    // 2) Zonas termicas ACPI via WMI (placa-mae / CPU em muitos notebooks)
    Wmi wmi(L"ROOT\\WMI");
    wmi.Query(L"SELECT CurrentTemperature, InstanceName FROM MSAcpi_ThermalZoneTemperature",
    [&achados](IWbemClassObject* o)
    {
        if (!Wmi::HasValue(o, L"CurrentTemperature")) return;
        double kelvinDecimos = (double)Wmi::GetUint(o, L"CurrentTemperature");
        double celsius = (kelvinDecimos / 10.0) - 273.15;
        if (celsius > 0 && celsius < 150)
        {
            AddRow(L"Zona ACPI (" + Wmi::GetStr(o, L"InstanceName") + L")",
                   NumW(celsius, 1) + L" °C");
            achados++;
        }
    });

    // 3) LibreHardwareMonitor / OpenHardwareMonitor (deteccao automatica)
    //    Se um deles estiver aberto, expoe TODOS os sensores via WMI:
    //    temperatura por nucleo da CPU, tensoes, RPM dos coolers etc.
    bool lhmAtivo = false;
    const wchar_t* espacos[] = { L"ROOT\\LibreHardwareMonitor", L"ROOT\\OpenHardwareMonitor" };
    for (const wchar_t* ns : espacos)
    {
        Wmi lhm(ns);
        if (!lhm.Ok()) continue;

        bool primeiraLinha = true;
        auto Cabecalho = [&]()
        {
            if (primeiraLinha)
            {
                AddBlank();
                AddSection(L"SENSORES COMPLETOS (LibreHardwareMonitor detectado)");
                primeiraLinha = false;
                lhmAtivo = true;
            }
        };

        lhm.Query(L"SELECT Name, Parent, Value FROM Sensor WHERE SensorType='Temperature'",
        [&](IWbemClassObject* o)
        {
            Cabecalho();
            double v = (double)Wmi::GetInt(o, L"Value", -1);
            // Value e float no WMI do LHM; le como string se necessario
            std::wstring vs = Wmi::GetStr(o, L"Value");
            if (!vs.empty()) v = _wtof(vs.c_str());
            if (v > 0 && v < 150)
            {
                AddRow(Wmi::GetStr(o, L"Parent") + L" | " + Wmi::GetStr(o, L"Name"),
                       NumW(v, 1) + L" °C");
                achados++;
            }
        });

        lhm.Query(L"SELECT Name, Parent, Value FROM Sensor WHERE SensorType='Fan'",
        [&](IWbemClassObject* o)
        {
            Cabecalho();
            std::wstring vs = Wmi::GetStr(o, L"Value");
            double v = vs.empty() ? 0 : _wtof(vs.c_str());
            if (v > 0)
                AddRow(Wmi::GetStr(o, L"Parent") + L" | Cooler: " + Wmi::GetStr(o, L"Name"),
                       NumW(v, 0) + L" RPM");
        });

        lhm.Query(L"SELECT Name, Parent, Value FROM Sensor WHERE SensorType='Voltage'",
        [&](IWbemClassObject* o)
        {
            Cabecalho();
            std::wstring vs = Wmi::GetStr(o, L"Value");
            double v = vs.empty() ? 0 : _wtof(vs.c_str());
            if (v > 0)
                AddRow(Wmi::GetStr(o, L"Parent") + L" | Tensao: " + Wmi::GetStr(o, L"Name"),
                       NumW(v, 3) + L" V");
        });

        if (lhmAtivo) break;   // ja achou um dos dois programas
    }

    // 4) Temperatura de discos aparece na aba "Armazenamento e SMART"
    AddBlank();
    AddRow(L"Temperatura dos Discos", L"Veja a categoria 'Armazenamento e SMART' (atributo 194 / NVMe).");

    if (achados == 0)
    {
        AddBlank();
        AddRow(L"Status de Leitura Termica", L"Nenhum sensor acessivel pelas APIs padrao do Windows.");
        AddRow(L"Restricao Tecnica", L"A leitura direta do silicio da CPU exige um driver em modo kernel.");
    }
    if (!lhmAtivo)
    {
        AddBlank();
        AddRow(L"Dica para Sensores Completos",
               L"Abra o LibreHardwareMonitor (gratuito) junto com este programa: "
               L"as temperaturas por nucleo da CPU, tensoes e coolers aparecerao aqui automaticamente.");
    }
}

// ================= Dispositivos USB =================
void LoadUSB()
{
    AddSection(L"DISPOSITIVOS USB CONECTADOS");
    Wmi wmi;
    int contador = 1;
    wmi.Query(L"SELECT Caption, Manufacturer, DeviceID, Status FROM Win32_PnPEntity WHERE DeviceID LIKE '%USB%'",
    [&contador](IWbemClassObject* o)
    {
        std::wstring nome = Wmi::GetStr(o, L"Caption");
        if (nome.empty()) return;
        if (nome.find(L"Hub") != std::wstring::npos || nome.find(L"Root") != std::wstring::npos ||
            nome.find(L"Host Controller") != std::wstring::npos) return;

        AddRow(L"Dispositivo " + NumW(contador), nome);
        std::wstring fab = Wmi::GetStr(o, L"Manufacturer");
        if (!fab.empty() && fab.find(L"(Standard") == std::wstring::npos && fab.find(L"(Padrao") == std::wstring::npos)
            AddRow(L"  Fabricante", fab);
        std::wstring st = Wmi::GetStr(o, L"Status");
        if (!st.empty()) AddRow(L"  Status", st == L"OK" ? L"OK (Funcionando)" : st);
        AddBlank();
        contador++;
    });
    if (contador == 1)
        AddRow(L"Status", L"Nenhum periferico USB detectado no momento.");
}

// ================= Dispositivos de Audio =================
void LoadAudio()
{
    AddSection(L"DISPOSITIVOS DE AUDIO");
    Wmi wmi;
    int contador = 1;
    wmi.Query(L"SELECT Name, Manufacturer, Status FROM Win32_SoundDevice",
    [&contador](IWbemClassObject* o)
    {
        std::wstring nome = Wmi::GetStr(o, L"Name");
        if (nome.empty()) return;
        AddRow(L"Dispositivo " + NumW(contador), nome);
        std::wstring fab = Wmi::GetStr(o, L"Manufacturer");
        if (!fab.empty()) AddRow(L"  Fabricante", fab);
        std::wstring st = Wmi::GetStr(o, L"Status");
        if (!st.empty()) AddRow(L"  Status de Operacao", st);
        AddBlank();
        contador++;
    });
    if (contador == 1)
        AddRow(L"Status", L"Nenhuma controladora de audio encontrada.");
}

// ================= Impressoras =================
void LoadPrinters()
{
    AddSection(L"IMPRESSORAS E DISPOSITIVOS DE FAX");
    Wmi wmi;
    int contador = 1;
    wmi.Query(L"SELECT Name, Default, PortName, Shared, DriverName FROM Win32_Printer",
    [&contador](IWbemClassObject* o)
    {
        std::wstring nome = Wmi::GetStr(o, L"Name");
        if (nome.empty()) return;
        AddRow(L"Dispositivo " + NumW(contador), nome);
        AddRow(L"  Impressora Padrao", Wmi::GetBool(o, L"Default") ? L"Sim (Principal)" : L"Nao");
        std::wstring porta = Wmi::GetStr(o, L"PortName");
        if (!porta.empty()) AddRow(L"  Porta de Comunicacao", porta);
        std::wstring drv = Wmi::GetStr(o, L"DriverName");
        if (!drv.empty()) AddRow(L"  Driver", drv);
        AddRow(L"  Compartilhada na Rede", Wmi::GetBool(o, L"Shared") ? L"Sim" : L"Nao");
        AddBlank();
        contador++;
    });
    if (contador == 1)
        AddRow(L"Status", L"Nenhuma impressora instalada no sistema.");
}

// ================= Bateria e Energia =================
void LoadBattery()
{
    SYSTEM_POWER_STATUS energia{};
    if (GetSystemPowerStatus(&energia))
    {
        AddRow(L"Status da Fonte de Energia",
               energia.ACLineStatus == 1 ? L"Conectado (Na Tomada)" :
               energia.ACLineStatus == 0 ? L"Desconectado (Na Bateria)" : L"Desconhecido");

        if (energia.BatteryLifePercent <= 100)
            AddRow(L"Nivel de Carga Atual", NumW((int)energia.BatteryLifePercent) + L"%");
        else
            AddRow(L"Nivel de Carga Atual", L"Bateria nao detectada (Desktop)");

        if (energia.BatteryLifeTime != (DWORD)-1)
            AddRow(L"Autonomia Estimada", NumW(energia.BatteryLifeTime / 3600) + L" h " +
                   NumW((energia.BatteryLifeTime % 3600) / 60) + L" min");

        std::wstring flag;
        if (energia.BatteryFlag & 8) flag = L"Carregando";
        else if (energia.BatteryFlag & 128) flag = L"Sem bateria conectada";
        else if (energia.BatteryFlag & 4) flag = L"Critica";
        else if (energia.BatteryFlag & 2) flag = L"Baixa";
        else if (energia.BatteryFlag & 1) flag = L"Alta";
        if (!flag.empty()) AddRow(L"Estado de Carregamento", flag);
    }

    AddBlank();
    AddSection(L"ANALISE DE SAUDE DA BATERIA");

    // Capacidade de projeto e capacidade atual (ROOT\WMI, mais confiavel que Win32_Battery)
    unsigned long long capProjeto = 0, capAtual = 0;
    {
        Wmi wmiW(L"ROOT\\WMI");
        wmiW.Query(L"SELECT DesignedCapacity FROM BatteryStaticData",
        [&capProjeto](IWbemClassObject* o) { capProjeto = Wmi::GetUint(o, L"DesignedCapacity"); });
        wmiW.Query(L"SELECT FullChargedCapacity FROM BatteryFullChargedCapacity",
        [&capAtual](IWbemClassObject* o) { capAtual = Wmi::GetUint(o, L"FullChargedCapacity"); });
    }

    if (capProjeto > 0 && capAtual > 0)
    {
        AddRow(L"Capacidade Original de Fabrica", NumW(capProjeto) + L" mWh");
        AddRow(L"Capacidade Maxima Atual", NumW(capAtual) + L" mWh");
        double saude = ((double)capAtual / (double)capProjeto) * 100.0;
        if (saude > 100) saude = 100;
        std::wstring diag = saude >= 80 ? L"Boa (Saudavel)" :
                            saude >= 50 ? L"Atencao (Desgastada)" : L"Critica (Substituicao Recomendada)";
        AddRow(L"Saude Estimada da Celula", NumW(saude, 2) + L"% - Status: " + diag);
    }
    else
    {
        Wmi wmi;
        bool detectada = false;
        wmi.Query(L"SELECT Name, EstimatedChargeRemaining, Chemistry FROM Win32_Battery",
        [&detectada](IWbemClassObject* o)
        {
            detectada = true;
            AddRow(L"Bateria", Wmi::GetStr(o, L"Name"));
            AddRow(L"Carga Estimada", NumW(Wmi::GetUint(o, L"EstimatedChargeRemaining")) + L"%");
        });
        if (!detectada)
            AddRow(L"Diagnostico de Saude", L"Nao disponivel para este equipamento.");
    }
}
