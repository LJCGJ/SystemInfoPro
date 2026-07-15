// ============================================================
//  SystemInfoPro - info_network.cpp
//  Rede: adaptadores (IP, DNS, gateway, MAC, link) e conexoes TCP
// ============================================================
#include "common.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tcpmib.h>
#include <tlhelp32.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

// ---------------- Adaptadores ----------------
static void GarantirWinsock()
{
    static bool iniciado = false;
    if (!iniciado)
    {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        iniciado = true;
    }
}

void LoadNetwork()
{
    GarantirWinsock();
    ULONG tam = 32 * 1024;
    std::vector<BYTE> buf(tam);
    IP_ADAPTER_ADDRESSES* lista = (IP_ADAPTER_ADDRESSES*)buf.data();
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_PREFIX;

    if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, lista, &tam) == ERROR_BUFFER_OVERFLOW)
    {
        buf.resize(tam);
        lista = (IP_ADAPTER_ADDRESSES*)buf.data();
    }
    if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, lista, &tam) != NO_ERROR)
    {
        AddRow(L"Erro", L"Nao foi possivel enumerar adaptadores de rede.");
        return;
    }

    for (IP_ADAPTER_ADDRESSES* a = lista; a; a = a->Next)
    {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        std::wstring status = (a->OperStatus == IfOperStatusUp) ? L"Conectado" : L"Desconectado";
        AddSection(std::wstring(a->FriendlyName) + L"  [" + status + L"]");
        AddRow(L"Descricao", a->Description);

        std::wstring tipo;
        switch (a->IfType)
        {
        case IF_TYPE_ETHERNET_CSMACD: tipo = L"Ethernet (cabo)"; break;
        case IF_TYPE_IEEE80211:       tipo = L"Wi-Fi (sem fio)"; break;
        case IF_TYPE_PPP:             tipo = L"PPP / Discada"; break;
        case IF_TYPE_TUNNEL:          tipo = L"Tunel (VPN)"; break;
        default:                      tipo = L"Outro"; break;
        }
        AddRow(L"Tipo de Interface", tipo);

        if (a->PhysicalAddressLength > 0)
        {
            std::wstring mac;
            for (ULONG i = 0; i < a->PhysicalAddressLength; i++)
            {
                wchar_t b[4];
                swprintf_s(b, L"%02X", a->PhysicalAddress[i]);
                if (!mac.empty()) mac += L"-";
                mac += b;
            }
            AddRow(L"Endereco Fisico (MAC)", mac);
        }

        if (a->OperStatus == IfOperStatusUp && a->TransmitLinkSpeed > 0)
            AddRow(L"Velocidade do Link", FormatSpeedBits((double)a->TransmitLinkSpeed));

        // Enderecos IP
        for (IP_ADAPTER_UNICAST_ADDRESS* ip = a->FirstUnicastAddress; ip; ip = ip->Next)
        {
            wchar_t txt[64] = {};
            DWORD tamTxt = 64;
            if (WSAAddressToStringW(ip->Address.lpSockaddr, ip->Address.iSockaddrLength,
                                    nullptr, txt, &tamTxt) == 0)
            {
                if (ip->Address.lpSockaddr->sa_family == AF_INET)
                    AddRow(L"Endereco IPv4", std::wstring(txt) + L" / prefixo " + NumW((int)ip->OnLinkPrefixLength));
                else
                    AddRow(L"Endereco IPv6", txt);
            }
        }

        // Gateway
        for (IP_ADAPTER_GATEWAY_ADDRESS* gw = a->FirstGatewayAddress; gw; gw = gw->Next)
        {
            wchar_t txt[64] = {};
            DWORD tamTxt = 64;
            if (WSAAddressToStringW(gw->Address.lpSockaddr, gw->Address.iSockaddrLength,
                                    nullptr, txt, &tamTxt) == 0)
                AddRow(L"Gateway Padrao", txt);
        }

        // DNS
        for (IP_ADAPTER_DNS_SERVER_ADDRESS* dns = a->FirstDnsServerAddress; dns; dns = dns->Next)
        {
            wchar_t txt[64] = {};
            DWORD tamTxt = 64;
            if (WSAAddressToStringW(dns->Address.lpSockaddr, dns->Address.iSockaddrLength,
                                    nullptr, txt, &tamTxt) == 0)
                AddRow(L"Servidor DNS", txt);
        }

        AddRow(L"DHCP Habilitado", (a->Dhcpv4Enabled) ? L"Sim" : L"Nao");
        if (a->Mtu > 0 && a->Mtu != (ULONG)-1) AddRow(L"MTU", NumW(a->Mtu) + L" bytes");
        AddBlank();
    }
}

// ---------------- Conexoes TCP ativas ----------------
static std::map<DWORD, std::wstring> MapaProcessos()
{
    std::map<DWORD, std::wstring> m;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return m;
    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(snap, &pe))
    {
        do { m[pe.th32ProcessID] = pe.szExeFile; } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return m;
}

static std::wstring EstadoTcp(DWORD st)
{
    switch (st)
    {
    case MIB_TCP_STATE_ESTAB:     return L"ESTABELECIDA";
    case MIB_TCP_STATE_LISTEN:    return L"ESCUTANDO";
    case MIB_TCP_STATE_TIME_WAIT: return L"TIME_WAIT";
    case MIB_TCP_STATE_CLOSE_WAIT:return L"CLOSE_WAIT";
    case MIB_TCP_STATE_SYN_SENT:  return L"SYN_SENT";
    case MIB_TCP_STATE_FIN_WAIT1: return L"FIN_WAIT1";
    case MIB_TCP_STATE_FIN_WAIT2: return L"FIN_WAIT2";
    case MIB_TCP_STATE_CLOSED:    return L"FECHADA";
    default:                      return L"OUTRO";
    }
}

void LoadTcpConnections()
{
    GarantirWinsock();
    AddSection(L"CONEXOES TCP (IPv4)");

    DWORD tam = 0;
    GetExtendedTcpTable(nullptr, &tam, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (tam == 0) { AddRow(L"Erro", L"Tabela TCP indisponivel."); return; }
    std::vector<BYTE> buf(tam);
    if (GetExtendedTcpTable(buf.data(), &tam, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR)
    {
        AddRow(L"Erro", L"Falha ao ler a tabela TCP.");
        return;
    }

    auto mapa = MapaProcessos();
    MIB_TCPTABLE_OWNER_PID* tabela = (MIB_TCPTABLE_OWNER_PID*)buf.data();

    int estab = 0, escuta = 0;
    for (DWORD i = 0; i < tabela->dwNumEntries; i++)
    {
        DWORD st = tabela->table[i].dwState;
        if (st == MIB_TCP_STATE_ESTAB) estab++;
        else if (st == MIB_TCP_STATE_LISTEN) escuta++;
    }
    AddRow(L"Total de Conexoes", NumW(tabela->dwNumEntries));
    AddRow(L"Estabelecidas", NumW(estab));
    AddRow(L"Portas em Escuta", NumW(escuta));
    AddBlank();
    AddRow(L"PROCESSO (PID)", L"LOCAL  ->  REMOTO   [ESTADO]");

    auto IpTexto = [](DWORD ip) -> std::wstring
    {
        wchar_t b[24];
        swprintf_s(b, L"%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
        return b;
    };

    for (DWORD i = 0; i < tabela->dwNumEntries; i++)
    {
        const MIB_TCPROW_OWNER_PID& r = tabela->table[i];
        DWORD st = r.dwState;
        if (st != MIB_TCP_STATE_ESTAB && st != MIB_TCP_STATE_LISTEN) continue;

        std::wstring proc = L"PID " + NumW(r.dwOwningPid);
        auto it = mapa.find(r.dwOwningPid);
        if (it != mapa.end()) proc = it->second + L" (" + NumW(r.dwOwningPid) + L")";

        USHORT portaLocal = ntohs((USHORT)r.dwLocalPort);
        USHORT portaRem   = ntohs((USHORT)r.dwRemotePort);

        std::wstring detalhe = IpTexto(r.dwLocalAddr) + L":" + NumW(portaLocal);
        if (st == MIB_TCP_STATE_ESTAB)
            detalhe += L"  ->  " + IpTexto(r.dwRemoteAddr) + L":" + NumW(portaRem);
        detalhe += L"   [" + EstadoTcp(st) + L"]";

        AddRow(proc, detalhe);
    }
}
