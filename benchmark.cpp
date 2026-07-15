// ============================================================
//  SystemInfoPro - benchmark.cpp
//  Benchmark integrado: CPU (single/multi-thread), RAM e Disco.
//  Roda em thread separada para nao travar a interface.
// ============================================================
#include "common.h"
#include <thread>
#include <atomic>
#include <random>

namespace
{
    std::vector<std::pair<std::wstring, std::wstring>> g_resultados;
    std::wstring g_dataUltimo;
    std::atomic<long> g_rodando{ 0 };
    std::atomic<int>  g_etapaAtual{ 0 };
    HWND g_notificar = nullptr;
    std::thread g_thread;   // mantida para join no encerramento

    double Agora()
    {
        LARGE_INTEGER f, t;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t);
        return (double)t.QuadPart / (double)f.QuadPart;
    }

    // ---------- CPU: operacoes inteiras + ponto flutuante ----------
    unsigned long long TrabalhoCpu(double duracaoSeg, double& tempoReal)
    {
        double t0 = Agora();
        unsigned long long ops = 0;
        unsigned long long x = 88172645463325252ULL;
        volatile double acc = 1.000001;
        for (;;)
        {
            for (int i = 0; i < 1000000; i++)
            {
                x ^= x << 13; x ^= x >> 7; x ^= x << 17;
                acc = acc * 1.0000001 + (double)(x & 0xFF) * 1e-9;
            }
            ops += 1000000;
            tempoReal = Agora() - t0;
            if (tempoReal >= duracaoSeg) break;
        }
        return ops;
    }

    double CpuSingleThread()
    {
        double tempo = 0;
        unsigned long long ops = TrabalhoCpu(1.5, tempo);
        return (double)ops / tempo / 1e6;   // MOps/s
    }

    double CpuMultiThread(unsigned int& threadsUsadas)
    {
        threadsUsadas = std::thread::hardware_concurrency();
        if (threadsUsadas == 0) threadsUsadas = 4;

        std::atomic<unsigned long long> totalOps{ 0 };
        std::vector<std::thread> ths;
        double t0 = Agora();
        for (unsigned int i = 0; i < threadsUsadas; i++)
        {
            ths.emplace_back([&totalOps]()
            {
                double tempo = 0;
                unsigned long long ops = TrabalhoCpu(1.5, tempo);
                totalOps += ops;
            });
        }
        for (auto& t : ths) t.join();
        double decorrido = Agora() - t0;
        return (double)totalOps.load() / decorrido / 1e6;   // MOps/s agregado
    }

    // ---------- RAM: copia de memoria ----------
    double RamCopiaGBs()
    {
        SIZE_T tam = 256ull * 1024 * 1024;
        BYTE* a = (BYTE*)VirtualAlloc(nullptr, tam, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        BYTE* b = (BYTE*)VirtualAlloc(nullptr, tam, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!a || !b)
        {
            if (a) VirtualFree(a, 0, MEM_RELEASE);
            if (b) VirtualFree(b, 0, MEM_RELEASE);
            tam = 64ull * 1024 * 1024;
            a = (BYTE*)VirtualAlloc(nullptr, tam, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            b = (BYTE*)VirtualAlloc(nullptr, tam, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!a || !b) return -1;
        }
        memset(a, 0xA5, tam);   // toca as paginas

        double t0 = Agora();
        unsigned long long bytes = 0;
        do
        {
            memcpy(b, a, tam);
            bytes += tam;
        } while (Agora() - t0 < 1.5);
        double decorrido = Agora() - t0;

        VirtualFree(a, 0, MEM_RELEASE);
        VirtualFree(b, 0, MEM_RELEASE);
        return (double)bytes / decorrido / 1e9;   // GB/s
    }

    // ---------- Disco ----------
    std::wstring ArquivoTemp()
    {
        wchar_t dir[MAX_PATH];
        GetTempPathW(MAX_PATH, dir);
        return std::wstring(dir) + L"sysinfopro_bench.tmp";
    }

    const DWORD CHUNK = 4 * 1024 * 1024;      // 4 MB
    const int   NCHUNKS = 64;                 // total 256 MB

    double DiscoEscritaMBs(const std::wstring& caminho, BYTE* buf)
    {
        HANDLE h = CreateFileW(caminho.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) return -1;

        double t0 = Agora();
        for (int i = 0; i < NCHUNKS; i++)
        {
            DWORD escritos = 0;
            if (!WriteFile(h, buf, CHUNK, &escritos, nullptr)) { CloseHandle(h); return -1; }
        }
        double decorrido = Agora() - t0;
        CloseHandle(h);
        return (double)CHUNK * NCHUNKS / decorrido / 1e6;   // MB/s
    }

    double DiscoLeituraMBs(const std::wstring& caminho, BYTE* buf)
    {
        HANDLE h = CreateFileW(caminho.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) return -1;

        double t0 = Agora();
        for (int i = 0; i < NCHUNKS; i++)
        {
            DWORD lidos = 0;
            if (!ReadFile(h, buf, CHUNK, &lidos, nullptr)) { CloseHandle(h); return -1; }
        }
        double decorrido = Agora() - t0;
        CloseHandle(h);
        return (double)CHUNK * NCHUNKS / decorrido / 1e6;
    }

    double Disco4kIops(const std::wstring& caminho, BYTE* buf)
    {
        HANDLE h = CreateFileW(caminho.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_NO_BUFFERING | FILE_FLAG_RANDOM_ACCESS, nullptr);
        if (h == INVALID_HANDLE_VALUE) return -1;

        std::mt19937_64 rng(0xC0FFEE);
        unsigned long long tamArq = (unsigned long long)CHUNK * NCHUNKS;
        const int LEITURAS = 2000;

        double t0 = Agora();
        for (int i = 0; i < LEITURAS; i++)
        {
            unsigned long long off = (rng() % (tamArq / 4096)) * 4096;
            LARGE_INTEGER li; li.QuadPart = (LONGLONG)off;
            SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
            DWORD lidos = 0;
            if (!ReadFile(h, buf, 4096, &lidos, nullptr)) { CloseHandle(h); return -1; }
        }
        double decorrido = Agora() - t0;
        CloseHandle(h);
        return (double)LEITURAS / decorrido;   // IOPS
    }

    void Etapa(int n)
    {
        g_etapaAtual = n;
        if (g_notificar) PostMessageW(g_notificar, WM_APP_BENCH_ETAPA, (WPARAM)n, 0);
    }

    // ---------- Thread principal do benchmark ----------
    void RodarBenchmark()
    {
        std::vector<std::pair<std::wstring, std::wstring>> r;

        Etapa(1);
        double single = CpuSingleThread();

        Etapa(2);
        unsigned int nThreads = 0;
        double multi = CpuMultiThread(nThreads);

        Etapa(3);
        double ram = RamCopiaGBs();

        Etapa(4);
        std::wstring tmp = ArquivoTemp();
        BYTE* buf = (BYTE*)VirtualAlloc(nullptr, CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        double escrita = -1, leitura = -1, iops = -1;
        if (buf)
        {
            memset(buf, 0x5A, CHUNK);
            escrita = DiscoEscritaMBs(tmp, buf);

            Etapa(5);
            if (escrita > 0) leitura = DiscoLeituraMBs(tmp, buf);

            Etapa(6);
            if (escrita > 0) iops = Disco4kIops(tmp, buf);

            VirtualFree(buf, 0, MEM_RELEASE);
        }
        DeleteFileW(tmp.c_str());

        // ---- Monta resultados ----
        r.push_back({ L"--- PROCESSADOR ---", L"" });
        r.push_back({ L"CPU Single-Thread", NumW(single, 0) + L" MOps/s" });
        r.push_back({ L"CPU Multi-Thread (" + NumW(nThreads) + L" threads)", NumW(multi, 0) + L" MOps/s" });
        if (single > 0)
            r.push_back({ L"Escala Multi/Single", NumW(multi / single, 2) + L"x" });
        r.push_back({ L"", L"" });

        r.push_back({ L"--- MEMORIA RAM ---", L"" });
        r.push_back({ L"Velocidade de Copia (memcpy)",
                      ram > 0 ? NumW(ram, 2) + L" GB/s" : L"Falhou (memoria insuficiente)" });
        r.push_back({ L"", L"" });

        r.push_back({ L"--- DISCO (unidade do sistema, arquivo de 256 MB) ---", L"" });
        r.push_back({ L"Escrita Sequencial",
                      escrita > 0 ? NumW(escrita, 0) + L" MB/s" : L"Falhou" });
        r.push_back({ L"Leitura Sequencial",
                      leitura > 0 ? NumW(leitura, 0) + L" MB/s" : L"Falhou" });
        r.push_back({ L"Leitura Aleatoria 4K",
                      iops > 0 ? NumW(iops, 0) + L" IOPS  (" + NumW(iops * 4096 / 1e6, 1) + L" MB/s)" : L"Falhou" });
        r.push_back({ L"", L"" });

        r.push_back({ L"--- REFERENCIAS TIPICAS PARA COMPARACAO ---", L"" });
        r.push_back({ L"HD mecanico 7200 RPM", L"~120-180 MB/s sequencial | ~100-200 IOPS 4K" });
        r.push_back({ L"SSD SATA", L"~450-550 MB/s sequencial | ~8.000+ IOPS 4K" });
        r.push_back({ L"SSD NVMe Gen3 / Gen4", L"~2.000-3.500 / ~5.000-7.000 MB/s sequencial" });
        r.push_back({ L"RAM DDR4 dual-channel tipica", L"~15-30 GB/s em copia (memcpy)" });

        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t data[64];
        swprintf_s(data, L"%02d/%02d/%04d %02d:%02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);

        g_resultados = std::move(r);
        g_dataUltimo = data;
        g_rodando = 0;
        g_etapaAtual = 0;
        if (g_notificar) PostMessageW(g_notificar, WM_APP_BENCH_FIM, 0, 0);
    }
}

// ================= API publica =================
bool BenchmarkEmExecucao() { return g_rodando.load() != 0; }

const wchar_t* BenchmarkNomeEtapa(int etapa)
{
    switch (etapa)
    {
    case 1: return L"CPU Single-Thread";
    case 2: return L"CPU Multi-Thread";
    case 3: return L"Memoria RAM";
    case 4: return L"Disco - Escrita Sequencial";
    case 5: return L"Disco - Leitura Sequencial";
    case 6: return L"Disco - Leitura Aleatoria 4K";
    }
    return L"";
}

void BenchmarkIniciar(HWND janelaNotificar)
{
    long esperado = 0;
    if (!g_rodando.compare_exchange_strong(esperado, 1)) return;   // ja rodando
    g_notificar = janelaNotificar;
    if (g_thread.joinable()) g_thread.join();   // limpa a anterior
    g_thread = std::thread(RodarBenchmark);
}

// Chamado no encerramento do app: espera a thread terminar com seguranca.
void BenchmarkShutdown()
{
    g_notificar = nullptr;   // evita PostMessage para janela ja destruida
    if (g_thread.joinable()) g_thread.join();
}

void LoadBenchmark()
{
    AddSection(L"BENCHMARK DO SISTEMA");

    if (BenchmarkEmExecucao())
    {
        int e = g_etapaAtual.load();
        AddRow(L"Status", L"⏳ Executando...");
        AddRow(L"Etapa Atual", NumW(e) + L" de 6 - " + std::wstring(BenchmarkNomeEtapa(e)));
        AddBlank();
        AddRow(L"Aguarde", L"O resultado aparece automaticamente ao terminar (~10 a 20 segundos).");
        return;
    }

    if (g_resultados.empty())
    {
        AddRow(L"Como Executar", L"Menu 'Ferramentas' → '🚀 Executar Benchmark'");
        AddBlank();
        AddRow(L"O que sera medido", L"CPU (single e multi-thread), velocidade real da RAM e do disco.");
        AddRow(L"Duracao", L"Cerca de 10 a 20 segundos.");
        AddRow(L"Dica", L"Feche programas pesados antes, para um resultado mais fiel.");
        return;
    }

    AddRow(L"Ultimo Teste Executado em", g_dataUltimo);
    AddBlank();
    for (const auto& linha : g_resultados)
    {
        if (linha.first.empty() && linha.second.empty()) AddBlank();
        else AddRow(linha.first, linha.second);
    }
}
