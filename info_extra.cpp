// ============================================================
//  SystemInfoPro - info_extra.cpp
//  Dispositivos PCI, Atualizacoes do Windows (Hotfixes),
//  Variaveis de Ambiente e Compartilhamentos de Rede
// ============================================================
#include "common.h"
#include "wmi.h"

// ================= Dispositivos PCI =================
void LoadPCI()
{
    AddSection(L"DISPOSITIVOS NO BARRAMENTO PCI / PCI EXPRESS");
    Wmi wmi;
    int contador = 1;
    wmi.Query(L"SELECT Caption, Manufacturer, DeviceID, Status FROM Win32_PnPEntity "
              L"WHERE DeviceID LIKE 'PCI%'",
    [&contador](IWbemClassObject* o)
    {
        std::wstring nome = Wmi::GetStr(o, L"Caption");
        if (nome.empty()) return;

        AddRow(L"Dispositivo " + NumW(contador), nome);
        std::wstring fab = Wmi::GetStr(o, L"Manufacturer");
        if (!fab.empty()) AddRow(L"  Fabricante", fab);

        // Extrai VEN/DEV do DeviceID (ex: PCI\VEN_10DE&DEV_2504&...)
        std::wstring id = Wmi::GetStr(o, L"DeviceID");
        size_t ven = id.find(L"VEN_");
        size_t dev = id.find(L"DEV_");
        if (ven != std::wstring::npos && dev != std::wstring::npos &&
            ven + 8 <= id.size() && dev + 8 <= id.size())
        {
            AddRow(L"  Identificacao PCI",
                   L"VendorID: 0x" + id.substr(ven + 4, 4) + L"  |  DeviceID: 0x" + id.substr(dev + 4, 4));
        }
        std::wstring st = Wmi::GetStr(o, L"Status");
        if (!st.empty()) AddRow(L"  Status", st == L"OK" ? L"OK (Funcionando)" : st);
        AddBlank();
        contador++;
    });
    if (contador == 1)
        AddRow(L"Status", L"Nenhum dispositivo PCI enumerado.");
}

// ================= Atualizacoes do Windows =================
void LoadHotfixes()
{
    AddSection(L"ATUALIZACOES INSTALADAS (HOTFIXES / KB)");
    Wmi wmi;
    int contador = 0;

    struct Fix { std::wstring kb, desc, data, quem; };
    std::vector<Fix> lista;

    wmi.Query(L"SELECT HotFixID, Description, InstalledOn, InstalledBy FROM Win32_QuickFixEngineering",
    [&lista](IWbemClassObject* o)
    {
        Fix f;
        f.kb   = Wmi::GetStr(o, L"HotFixID");
        f.desc = Wmi::GetStr(o, L"Description");
        f.data = Wmi::GetStr(o, L"InstalledOn");
        f.quem = Wmi::GetStr(o, L"InstalledBy");
        if (!f.kb.empty()) lista.push_back(f);
    });

    AddRow(L"Total de Atualizacoes Encontradas", NumW((int)lista.size()));
    AddBlank();
    AddRow(L"ATUALIZACAO (KB)", L"DETALHES");
    for (const auto& f : lista)
    {
        std::wstring det = f.desc.empty() ? L"Update" : f.desc;
        if (!f.data.empty()) det += L"  |  Instalada em: " + f.data;
        AddRow(f.kb, det);
        contador++;
    }
    if (contador == 0)
        AddRow(L"Status", L"Nenhuma atualizacao listada pelo WMI.");
}

// ================= Variaveis de Ambiente =================
void LoadEnvVars()
{
    AddSection(L"VARIAVEIS DE AMBIENTE (SESSAO ATUAL)");

    LPWCH bloco = GetEnvironmentStringsW();
    if (!bloco)
    {
        AddRow(L"Erro", L"Nao foi possivel ler as variaveis de ambiente.");
        return;
    }

    int contador = 0;
    for (LPWCH p = bloco; *p; )
    {
        std::wstring linha = p;
        p += linha.size() + 1;

        size_t igual = linha.find(L'=');
        if (igual == std::wstring::npos || igual == 0) continue;   // ignora "=C:" etc

        std::wstring nome = linha.substr(0, igual);
        std::wstring valor = linha.substr(igual + 1);

        // Quebra o PATH em linhas para facilitar a leitura
        if (_wcsicmp(nome.c_str(), L"Path") == 0)
        {
            AddRow(L"Path", L"(lista abaixo)");
            std::wstringstream ss(valor);
            std::wstring item;
            while (std::getline(ss, item, L';'))
                if (!item.empty()) AddRow(L"  ", item);
        }
        else
        {
            AddRow(nome, valor);
        }
        contador++;
    }
    FreeEnvironmentStringsW(bloco);

    AddBlank();
    AddRow(L"Total de Variaveis", NumW(contador));
}

// ================= Compartilhamentos de Rede =================
void LoadShares()
{
    AddSection(L"COMPARTILHAMENTOS DE REDE DESTA MAQUINA");
    Wmi wmi;
    int contador = 1;
    wmi.Query(L"SELECT Name, Path, Description, Type FROM Win32_Share",
    [&contador](IWbemClassObject* o)
    {
        std::wstring nome = Wmi::GetStr(o, L"Name");
        if (nome.empty()) return;

        unsigned long long tipo = Wmi::GetUint(o, L"Type");
        std::wstring tipoTxt;
        switch (tipo & 0x7FFFFFFF)
        {
        case 0: tipoTxt = L"Pasta de Disco"; break;
        case 1: tipoTxt = L"Fila de Impressao"; break;
        case 2: tipoTxt = L"Dispositivo"; break;
        case 3: tipoTxt = L"IPC (Comunicacao)"; break;
        default: tipoTxt = L"Outro"; break;
        }
        if (tipo & 0x80000000) tipoTxt += L" (Administrativo/Oculto)";

        AddRow(L"Compartilhamento " + NumW(contador), nome);
        AddRow(L"  Tipo", tipoTxt);
        std::wstring caminho = Wmi::GetStr(o, L"Path");
        if (!caminho.empty()) AddRow(L"  Caminho Local", caminho);
        std::wstring desc = Wmi::GetStr(o, L"Description");
        if (!desc.empty()) AddRow(L"  Descricao", desc);
        AddBlank();
        contador++;
    });
    if (contador == 1)
        AddRow(L"Status", L"Nenhum compartilhamento ativo.");
}
