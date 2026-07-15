// ============================================================
//  SystemInfoPro - netdiag.cpp
//  Diagnostico de rede: ping/latencia para varios alvos,
//  resolucao DNS, IP externo e avaliacao da qualidade.
//  Roda em thread para nao travar a interface.
// ============================================================
#include "common.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <windns.h>
#include <wininet.h>
#include <thread>
#include <atomic>
#include <cstdio>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "dnsapi.lib")
#pragma comment(lib, "wininet.lib")

namespace
{
    std::vector<std::pair<std::wstring, std::wstring>> g_resultado;
    std::atomic<long> g_rodando{ 0 };
    HWND g_notificar = nullptr;
    std::thread g_thread;   // mantida para join no encerramento

    struct Alvo { const wchar_t* nome; const wchar_t* host; };

    // Faz 4 pings e devolve latencia media (ms), perdas e IP resolvido
    bool PingAlvo(const wchar_t* host, double& latMedia, int& perdas, std::wstring& ipTxt)
    {
        latMedia = 0; perdas = 0; ipTxt.clear();

        // Resolve o host para IPv4
        ADDRINFOW hints{}; hints.ai_family = AF_INET;
        ADDRINFOW* res = nullptr;
        if (GetAddrInfoW(host, nullptr, &hints, &res) != 0 || !res) return false;

        sockaddr_in* sa = (sockaddr_in*)res->ai_addr;
        IPAddr destino = sa->sin_addr.S_un.S_addr;
        wchar_t ipbuf[64] = {};
        InetNtopW(AF_INET, &sa->sin_addr, ipbuf, 64);
        ipTxt = ipbuf;
        FreeAddrInfoW(res);

        HANDLE hIcmp = IcmpCreateFile();
        if (hIcmp == INVALID_HANDLE_VALUE) return false;

        char dados[32] = "SystemInfoPro-ping";
        BYTE respBuf[sizeof(ICMP_ECHO_REPLY) + 64] = {};
        double somaLat = 0;
        int respostas = 0;
        const int TENTATIVAS = 4;

        for (int i = 0; i < TENTATIVAS; i++)
        {
            DWORD ret = IcmpSendEcho(hIcmp, destino, dados, sizeof(dados),
                                     nullptr, respBuf, sizeof(respBuf), 1000);
            if (ret > 0)
            {
                ICMP_ECHO_REPLY* r = (ICMP_ECHO_REPLY*)respBuf;
                if (r->Status == IP_SUCCESS)
                {
                    somaLat += r->RoundTripTime;
                    respostas++;
                }
                else perdas++;
            }
            else perdas++;
        }
        IcmpCloseHandle(hIcmp);

        if (respostas > 0) latMedia = somaLat / respostas;
        return respostas > 0;
    }

    std::wstring ObterIpExterno()
    {
        // Servico simples que devolve apenas o IP em texto
        HINTERNET net = InternetOpenW(L"SystemInfoPro", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!net) return L"";
        HINTERNET url = InternetOpenUrlW(net, L"http://api.ipify.org", nullptr, 0,
                                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        std::wstring ip;
        if (url)
        {
            char buf[128] = {};
            DWORD lidos = 0;
            if (InternetReadFile(url, buf, sizeof(buf) - 1, &lidos) && lidos > 0)
            {
                buf[lidos] = 0;
                wchar_t w[128];
                MultiByteToWideChar(CP_ACP, 0, buf, -1, w, 128);
                ip = w;
            }
            InternetCloseHandle(url);
        }
        InternetCloseHandle(net);
        return ip;
    }

    void RodarDiag()
    {
        std::vector<std::pair<std::wstring, std::wstring>> r;

        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);

        r.push_back({ L"--- TESTE DE LATENCIA (PING) ---", L"" });

        Alvo alvos[] = {
            { L"Google DNS (8.8.8.8)",     L"8.8.8.8" },
            { L"Cloudflare (1.1.1.1)",     L"1.1.1.1" },
            { L"Google (google.com)",      L"google.com" },
            { L"YouTube (youtube.com)",    L"youtube.com" },
            { L"Cloudflare DNS (nome)",    L"one.one.one.one" },
        };

        double melhorLat = 99999; int totalPerdas = 0; int totalTestes = 0; bool algumOk = false;
        for (const auto& a : alvos)
        {
            double lat = 0; int perdas = 0; std::wstring ip;
            bool ok = PingAlvo(a.host, lat, perdas, ip);
            totalTestes++;
            totalPerdas += perdas;
            if (ok)
            {
                algumOk = true;
                if (lat < melhorLat) melhorLat = lat;
                std::wstring det = NumW(lat, 0) + L" ms";
                if (perdas > 0) det += L"  (" + NumW(perdas) + L"/4 perdidos)";
                if (!ip.empty()) det += L"  →  " + ip;
                r.push_back({ a.nome, det });
            }
            else
            {
                r.push_back({ a.nome, L"❌ Sem resposta (bloqueado ou offline)" });
            }
        }

        r.push_back({ L"", L"" });
        r.push_back({ L"--- RESOLUCAO DNS ---", L"" });

        const wchar_t* dominios[] = { L"google.com", L"microsoft.com", L"github.com" };
        for (const wchar_t* d : dominios)
        {
            ADDRINFOW hints{}; hints.ai_family = AF_INET;
            ADDRINFOW* res = nullptr;
            double t0 = (double)GetTickCount64();
            if (GetAddrInfoW(d, nullptr, &hints, &res) == 0 && res)
            {
                double ms = (double)GetTickCount64() - t0;
                sockaddr_in* sa = (sockaddr_in*)res->ai_addr;
                wchar_t ipbuf[64] = {};
                InetNtopW(AF_INET, &sa->sin_addr, ipbuf, 64);
                r.push_back({ std::wstring(L"DNS: ") + d, std::wstring(ipbuf) + L"  (" + NumW(ms, 0) + L" ms)" });
                FreeAddrInfoW(res);
            }
            else
            {
                r.push_back({ std::wstring(L"DNS: ") + d, L"❌ Falha na resolucao" });
            }
        }

        r.push_back({ L"", L"" });
        r.push_back({ L"--- CONEXAO EXTERNA ---", L"" });
        std::wstring ipExt = ObterIpExterno();
        r.push_back({ L"Seu IP Publico (externo)", ipExt.empty() ? L"Nao foi possivel obter (sem internet?)" : ipExt });

        // Avaliacao de qualidade
        r.push_back({ L"", L"" });
        r.push_back({ L"--- AVALIACAO DA CONEXAO ---", L"" });
        if (!algumOk)
        {
            r.push_back({ L"Status", L"❌ Sem conectividade - verifique cabo/Wi-Fi e roteador." });
        }
        else
        {
            std::wstring qualidade;
            if (melhorLat < 20)      qualidade = L"✅ Excelente (otima para jogos e chamadas)";
            else if (melhorLat < 50) qualidade = L"✅ Boa (adequada para a maioria dos usos)";
            else if (melhorLat < 100) qualidade = L"⚠ Regular (pode haver atraso em jogos online)";
            else                     qualidade = L"⚠ Alta latencia (conexao lenta ou congestionada)";
            r.push_back({ L"Menor Latencia Medida", NumW(melhorLat, 0) + L" ms" });
            r.push_back({ L"Qualidade Geral", qualidade });
            if (totalPerdas > 0)
                r.push_back({ L"Perda de Pacotes", L"⚠ " + NumW(totalPerdas) + L" pacotes perdidos - conexao instavel." });
            else
                r.push_back({ L"Perda de Pacotes", L"✅ Nenhuma - conexao estavel." });
        }

        WSACleanup();

        g_resultado = std::move(r);
        g_rodando = 0;
        if (g_notificar) PostMessageW(g_notificar, WM_APP_NETDIAG_FIM, 0, 0);
    }
}

bool NetDiagEmExecucao() { return g_rodando.load() != 0; }

void NetDiagIniciar(HWND janelaNotificar)
{
    long esperado = 0;
    if (!g_rodando.compare_exchange_strong(esperado, 1)) return;
    g_notificar = janelaNotificar;
    if (g_thread.joinable()) g_thread.join();
    g_thread = std::thread(RodarDiag);
}

void NetDiagShutdown()
{
    g_notificar = nullptr;
    if (g_thread.joinable()) g_thread.join();
}

void LoadNetDiag()
{
    AddSection(L"DIAGNOSTICO DE REDE");

    if (NetDiagEmExecucao())
    {
        AddRow(L"Status", L"⏳ Testando conexao... (aguarde ~5 a 10 segundos)");
        AddRow(L"Executando", L"Ping, resolucao DNS e verificacao do IP externo.");
        return;
    }

    if (g_resultado.empty())
    {
        AddRow(L"Como Usar", L"Menu 'Ferramentas' → '🌐 Diagnostico de Rede'");
        AddBlank();
        AddRow(L"O que testa", L"Latencia (ping) para varios servidores, DNS, IP publico e qualidade geral.");
        AddRow(L"Util para", L"Diagnosticar internet lenta, quedas e problemas de DNS.");
        return;
    }

    for (const auto& linha : g_resultado)
    {
        if (linha.first.empty() && linha.second.empty()) AddBlank();
        else if (linha.first.size() > 3 && linha.first.substr(0, 3) == L"---")
            AddSection(Trim(linha.first.substr(3, linha.first.size() - 6)));
        else AddRow(linha.first, linha.second);
    }
}
