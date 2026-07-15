// ============================================================
//  SystemInfoPro - diskanalyzer.cpp
//  Analisador de espaco em disco (estilo WinDirStat):
//  escaneia uma pasta/unidade e lista as maiores pastas e
//  arquivos, com tamanho e percentual do total.
//  O scan roda em thread separada para NAO travar a interface.
// ============================================================
#include "common.h"
#include <shlobj.h>
#include <thread>
#include <atomic>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    struct Entrada { std::wstring nome; unsigned long long tamanho; };
    std::vector<Entrada> g_topPastas;
    std::vector<Entrada> g_topArquivos;
    std::wstring g_raizEscaneada;
    unsigned long long g_totalEscaneado = 0;
    unsigned long long g_totalArquivos = 0;
    bool g_temResultado = false;

    std::atomic<long> g_escaneando{ 0 };
    std::thread g_thread;
    HWND g_notificar = nullptr;
    const int PROFUNDIDADE_MAX = 256;

    // Prefixa com \\?\ para suportar caminhos acima de MAX_PATH (260).
    std::wstring PrefixoLongo(const std::wstring& caminho)
    {
        if (caminho.size() >= 4 && caminho.compare(0, 4, L"\\\\?\\") == 0) return caminho;
        if (caminho.size() >= 2 && caminho[0] == L'\\' && caminho[1] == L'\\')
            return L"\\\\?\\UNC\\" + caminho.substr(2);   // rede
        return L"\\\\?\\" + caminho;
    }

    // Escaneia recursivamente; retorna o tamanho total da pasta.
    unsigned long long EscanearPasta(const std::wstring& caminho,
                                     std::vector<Entrada>& arquivos,
                                     unsigned long long& contagemArquivos,
                                     int profundidade)
    {
        if (profundidade > PROFUNDIDADE_MAX) return 0;   // blindagem contra estouro de pilha

        unsigned long long total = 0;
        std::wstring busca = PrefixoLongo(caminho) + L"\\*";
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(busca.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return 0;

        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;   // evita loops

            std::wstring completo = caminho + L"\\" + fd.cFileName;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                total += EscanearPasta(completo, arquivos, contagemArquivos, profundidade + 1);
            }
            else
            {
                unsigned long long tam = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                total += tam;
                contagemArquivos++;
                if (tam > 10ull * 1024 * 1024)
                    arquivos.push_back({ completo, tam });
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return total;
    }

    std::wstring EscolherPasta(HWND dono)
    {
        std::wstring resultado;
        IFileDialog* dlg = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_IFileDialog, (void**)&dlg)))
        {
            DWORD opc = 0;
            dlg->GetOptions(&opc);
            dlg->SetOptions(opc | FOS_PICKFOLDERS);
            dlg->SetTitle(L"Escolha a pasta ou unidade para analisar");
            if (SUCCEEDED(dlg->Show(dono)))
            {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dlg->GetResult(&item)) && item)
                {
                    PWSTR caminho = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &caminho)))
                    {
                        resultado = caminho;
                        CoTaskMemFree(caminho);
                    }
                    item->Release();
                }
            }
            dlg->Release();
        }
        return resultado;
    }

    // Roda em thread separada
    void RodarScan(std::wstring raiz)
    {
        std::vector<Entrada> pastas, arquivos;
        unsigned long long total = 0, contArquivos = 0;

        std::wstring busca = PrefixoLongo(raiz) + L"\\*";
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(busca.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

                std::wstring completo = raiz + L"\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    unsigned long long tam = EscanearPasta(completo, arquivos, contArquivos, 1);
                    total += tam;
                    pastas.push_back({ fd.cFileName, tam });
                }
                else
                {
                    unsigned long long tam = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                    total += tam;
                    contArquivos++;
                    if (tam > 10ull * 1024 * 1024)
                        arquivos.push_back({ completo, tam });
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }

        std::sort(pastas.begin(), pastas.end(),
            [](const Entrada& a, const Entrada& b) { return a.tamanho > b.tamanho; });
        std::sort(arquivos.begin(), arquivos.end(),
            [](const Entrada& a, const Entrada& b) { return a.tamanho > b.tamanho; });
        if (pastas.size() > 25) pastas.resize(25);
        if (arquivos.size() > 25) arquivos.resize(25);

        g_topPastas = std::move(pastas);
        g_topArquivos = std::move(arquivos);
        g_totalEscaneado = total;
        g_totalArquivos = contArquivos;
        g_temResultado = true;
        g_escaneando = 0;
        if (g_notificar) PostMessageW(g_notificar, WM_APP_DISK_FIM, 0, 0);
    }
}

bool DiskAnalyzerEmExecucao() { return g_escaneando.load() != 0; }

void DiskAnalyzerEscanear(HWND dono)
{
    if (DiskAnalyzerEmExecucao()) return;

    std::wstring raiz = EscolherPasta(dono);   // dialogo COM na thread da UI
    if (raiz.empty()) return;

    g_raizEscaneada = raiz;
    g_notificar = dono;
    g_escaneando = 1;
    if (g_thread.joinable()) g_thread.join();
    g_thread = std::thread(RodarScan, raiz);
}

void DiskAnalyzerShutdown()
{
    g_notificar = nullptr;
    if (g_thread.joinable()) g_thread.join();
}

static std::wstring BarraProporcao(unsigned long long parte, unsigned long long todo)
{
    if (todo == 0) return L"";
    int pct = (int)(parte * 100 / todo);
    int blocos = pct / 5;
    std::wstring b;
    for (int i = 0; i < 20; i++) b += (i < blocos) ? L'█' : L'░';
    return b + L"  " + NumW(pct) + L"%";
}

void LoadDiskAnalyzer()
{
    AddSection(L"ANALISADOR DE ESPACO EM DISCO");

    if (DiskAnalyzerEmExecucao())
    {
        AddRow(L"Status", L"⏳ Escaneando " + g_raizEscaneada + L" ...");
        AddRow(L"Aguarde", L"O resultado aparece automaticamente ao terminar.");
        AddRow(L"Dica", L"Pode continuar usando o programa normalmente enquanto escaneia.");
        return;
    }

    if (!g_temResultado)
    {
        AddRow(L"Como Usar", L"Menu 'Ferramentas' → '📊 Analisar Espaco em Disco'");
        AddBlank();
        AddRow(L"O que faz", L"Escaneia uma pasta ou unidade e mostra as maiores pastas e arquivos.");
        AddRow(L"Util para", L"Descobrir o que esta ocupando espaco e liberar o disco rapidamente.");
        AddRow(L"Dica", L"Escanear 'C:\\' inteiro pode levar de alguns segundos a alguns minutos.");
        return;
    }

    AddRow(L"Pasta Analisada", g_raizEscaneada);
    AddRow(L"Tamanho Total", FormatBytes(g_totalEscaneado));
    AddRow(L"Total de Arquivos", NumW(g_totalArquivos));
    AddBlank();

    AddSection(L"MAIORES PASTAS");
    if (g_topPastas.empty()) AddRow(L"(nenhuma subpasta)", L"");
    for (const auto& e : g_topPastas)
        AddRow(L"📁 " + e.nome, FormatBytes(e.tamanho) + L"   " + BarraProporcao(e.tamanho, g_totalEscaneado));

    AddBlank();
    AddSection(L"MAIORES ARQUIVOS (acima de 10 MB)");
    if (g_topArquivos.empty()) AddRow(L"(nenhum arquivo grande)", L"");
    for (const auto& e : g_topArquivos)
    {
        std::wstring nome = e.nome;
        if (nome.find(g_raizEscaneada) == 0 && nome.size() > g_raizEscaneada.size())
            nome = nome.substr(g_raizEscaneada.size() + 1);
        AddRow(L"📄 " + nome, FormatBytes(e.tamanho) + L"   " + BarraProporcao(e.tamanho, g_totalEscaneado));
    }
}
