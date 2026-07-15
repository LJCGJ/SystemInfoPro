// ============================================================
//  SystemInfoPro - sensors_all.cpp
//  Central de Sensores: le TODOS os sensores de TODOS os
//  componentes (CPU, placa-mae, memoria, GPU, discos...) via
//  LibreHardwareMonitor / OpenHardwareMonitor, agrupados por peca.
//
//  Isso traz temperatura do processador, do chipset, VRM, nucleos,
//  memoria (quando ha sensor), tensoes, ventoinhas, potencia etc.
//  Requer o LibreHardwareMonitor aberto (gratuito, open source).
// ============================================================
#include "common.h"
#include "wmi.h"
#include <cstdlib>

namespace
{
    struct Sensor
    {
        std::wstring nome;
        std::wstring tipo;
        double valor;
        double maximo;
    };

    // Traduz o tipo de hardware para um rotulo com icone
    std::wstring RotuloHardware(const std::wstring& tipo, const std::wstring& nome)
    {
        std::wstring icone = L"🔧";
        if (tipo.find(L"Cpu") != std::wstring::npos) icone = L"⚙";
        else if (tipo.find(L"Gpu") != std::wstring::npos) icone = L"🎮";
        else if (tipo.find(L"Memory") != std::wstring::npos) icone = L"🧠";
        else if (tipo.find(L"Motherboard") != std::wstring::npos) icone = L"🧩";
        else if (tipo.find(L"Storage") != std::wstring::npos || tipo.find(L"HDD") != std::wstring::npos) icone = L"💾";
        else if (tipo.find(L"Network") != std::wstring::npos) icone = L"🌐";
        else if (tipo.find(L"Cooler") != std::wstring::npos || tipo.find(L"SuperIO") != std::wstring::npos) icone = L"🌀";
        else if (tipo.find(L"Battery") != std::wstring::npos) icone = L"🔋";
        return icone + L" " + nome;
    }

    // Formata valor + unidade conforme o tipo de sensor
    std::wstring FormatarSensor(const std::wstring& tipo, double v)
    {
        if (tipo == L"Temperature") return NumW(v, 1) + L" °C";
        if (tipo == L"Voltage")     return NumW(v, 3) + L" V";
        if (tipo == L"Current")     return NumW(v, 3) + L" A";
        if (tipo == L"Power")       return NumW(v, 1) + L" W";
        if (tipo == L"Clock")       return NumW(v, 0) + L" MHz";
        if (tipo == L"Load")        return NumW(v, 1) + L" %";
        if (tipo == L"Control")     return NumW(v, 1) + L" %";
        if (tipo == L"Level")       return NumW(v, 1) + L" %";
        if (tipo == L"Fan")         return NumW(v, 0) + L" RPM";
        if (tipo == L"Flow")        return NumW(v, 1) + L" L/h";
        if (tipo == L"Data")        return NumW(v, 2) + L" GB";
        if (tipo == L"SmallData")   return NumW(v, 1) + L" MB";
        if (tipo == L"Throughput")  return NumW(v / 1e6, 2) + L" MB/s";
        if (tipo == L"Energy")      return NumW(v, 0) + L" mWh";
        if (tipo == L"Frequency")   return NumW(v, 1) + L" Hz";
        return NumW(v, 2);
    }

    std::wstring TituloTipo(const std::wstring& tipo)
    {
        if (tipo == L"Temperature") return L"🌡 Temperaturas";
        if (tipo == L"Load")        return L"📊 Uso / Carga";
        if (tipo == L"Clock")       return L"⏱ Frequencias (Clocks)";
        if (tipo == L"Fan")         return L"🌀 Ventoinhas (Coolers)";
        if (tipo == L"Voltage")     return L"⚡ Tensoes";
        if (tipo == L"Power")       return L"🔌 Potencia";
        if (tipo == L"Control")     return L"🎚 Controle de Ventoinha";
        if (tipo == L"Current")     return L"🔋 Corrente";
        if (tipo == L"Data")        return L"💾 Dados";
        if (tipo == L"SmallData")   return L"💾 Dados";
        if (tipo == L"Throughput")  return L"📈 Taxa de Transferencia";
        if (tipo == L"Level")       return L"📏 Niveis";
        if (tipo == L"Energy")      return L"🔋 Energia";
        return tipo;
    }

    // Ordem de exibicao dos tipos dentro de cada componente
    int OrdemTipo(const std::wstring& t)
    {
        const wchar_t* ordem[] = { L"Temperature", L"Load", L"Clock", L"Fan",
                                   L"Control", L"Voltage", L"Power", L"Current",
                                   L"Throughput", L"Data", L"SmallData", L"Level", L"Energy" };
        for (int i = 0; i < 13; i++) if (t == ordem[i]) return i;
        return 99;
    }

    bool LerTodosSensores(const wchar_t* ns)
    {
        Wmi wmi(ns);
        if (!wmi.Ok()) return false;

        // 1) Hardware: identifier -> (nome, tipo)
        struct HW { std::wstring nome, tipo; };
        std::map<std::wstring, HW> hardware;
        wmi.Query(L"SELECT Identifier, Name, HardwareType FROM Hardware",
        [&](IWbemClassObject* o)
        {
            std::wstring id = Wmi::GetStr(o, L"Identifier");
            if (id.empty()) return;
            hardware[id] = { Wmi::GetStr(o, L"Name"), Wmi::GetStr(o, L"HardwareType") };
        });

        if (hardware.empty()) return false;

        // 2) Sensores agrupados por hardware pai
        std::map<std::wstring, std::vector<Sensor>> porHw;
        int totalSensores = 0;
        wmi.Query(L"SELECT Name, SensorType, Parent, Value, Max FROM Sensor",
        [&](IWbemClassObject* o)
        {
            std::wstring pai = Wmi::GetStr(o, L"Parent");
            std::wstring tipo = Wmi::GetStr(o, L"SensorType");
            std::wstring nome = Wmi::GetStr(o, L"Name");
            if (pai.empty() || tipo.empty()) return;
            double v = Wmi::GetDouble(o, L"Value");
            double mx = Wmi::GetDouble(o, L"Max");
            porHw[pai].push_back({ nome, tipo, v, mx });
            totalSensores++;
        });

        if (totalSensores == 0) return false;

        // 3) Renderiza por componente, na ordem dos identifiers
        for (const auto& par : hardware)
        {
            const std::wstring& id = par.first;
            const HW& hw = par.second;
            auto it = porHw.find(id);
            if (it == porHw.end() || it->second.empty()) continue;

            AddBlank();
            AddSection(RotuloHardware(hw.tipo, hw.nome));

            // ordena sensores por tipo e depois por nome
            std::vector<Sensor> lista = it->second;
            std::sort(lista.begin(), lista.end(),
                [](const Sensor& a, const Sensor& b)
                {
                    int oa = OrdemTipo(a.tipo), ob = OrdemTipo(b.tipo);
                    if (oa != ob) return oa < ob;
                    return _wcsicmp(a.nome.c_str(), b.nome.c_str()) < 0;
                });

            std::wstring tipoAtual;
            for (const auto& s : lista)
            {
                if (s.tipo != tipoAtual)
                {
                    tipoAtual = s.tipo;
                    AddRow(L"  " + TituloTipo(s.tipo), L"");
                }
                std::wstring valor = FormatarSensor(s.tipo, s.valor);
                if (s.tipo == L"Temperature" && s.maximo > 0 && s.maximo < 150)
                    valor += L"   (max: " + NumW(s.maximo, 0) + L" °C)";
                AddRow(L"      " + s.nome, valor);
            }
        }
        return true;
    }
}

void LoadAllSensors()
{
    AddSection(L"CENTRAL DE SENSORES - TODOS OS COMPONENTES");

    bool ok = LerTodosSensores(L"ROOT\\LibreHardwareMonitor");
    if (!ok) ok = LerTodosSensores(L"ROOT\\OpenHardwareMonitor");

    if (!ok)
    {
        AddRow(L"Status", L"⚠ Nenhuma fonte completa de sensores ativa.");
        AddBlank();
        AddRow(L"Por que?", L"O Windows nao expoe as temperaturas de CPU, chipset, VRM e memoria por API oficial.");
        AddRow(L"", L"Ler o silicio exige um driver em modo kernel assinado (como HWiNFO/AIDA64 usam).");
        AddBlank();
        AddSection(L"COMO ATIVAR (GRATUITO)");
        AddRow(L"1. Baixe o LibreHardwareMonitor", L"https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases");
        AddRow(L"2. Execute-o como Administrador", L"(ele instala um driver de leitura de sensores)");
        AddRow(L"3. Deixe-o aberto e volte aqui", L"Todos os sensores aparecerao automaticamente:");
        AddRow(L"", L"temperatura por nucleo da CPU, chipset, VRM, tensoes, ventoinhas, potencia e mais.");
        AddBlank();
        AddRow(L"Observacao", L"O SystemInfoPro le os dados diretamente do LibreHardwareMonitor via WMI,");
        AddRow(L"", L"sem precisar instalar driver proprio - mantendo o app 100% open source.");
        return;
    }

    // Rodape com dica
    AddBlank();
    AddSection(L"INFORMACAO");
    AddRow(L"Fonte dos Dados", L"LibreHardwareMonitor (leitura ao vivo via WMI)");
    AddRow(L"Atualizar", L"Pressione F5 para reler os valores mais recentes.");
}
