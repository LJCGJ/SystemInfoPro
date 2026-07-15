// ============================================================
//  SystemInfoPro - Painel de Informacoes do Sistema (C++ / Win32)
//  main.cpp - Janela principal, arvore, lista, busca, temas,
//             graficos e exportacao TXT/HTML
// ============================================================
#include "common.h"
#include <commdlg.h>
#include <objbase.h>
#include <uxtheme.h>
#include <dwmapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

// Ativa o estilo visual moderno dos controles (Common Controls v6)
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------- Identificadores de pagina ----------------
enum PageId
{
    PAGE_NONE = 0,
    // Sensores
    PAGE_GRAPHS, PAGE_REALTIME, PAGE_TEMPS,
    // Hardware
    PAGE_CPU, PAGE_BOARD, PAGE_GPU, PAGE_MONITORS, PAGE_RAM,
    PAGE_STORAGE, PAGE_PCI, PAGE_DRIVERS, PAGE_USB, PAGE_AUDIO, PAGE_PRINTERS, PAGE_BATTERY,
    // Software
    PAGE_OS, PAGE_PROGRAMS, PAGE_STARTUP, PAGE_PROCESSES, PAGE_USERS,
    PAGE_SERVICES, PAGE_HOTFIX, PAGE_ENV, PAGE_SECURITY, PAGE_TASKS, PAGE_DIRECTX,
    // Rede
    PAGE_NETWORK, PAGE_TCP, PAGE_SHARES
};

// ---------------- IDs de controles ----------------
enum
{
    ID_TREE = 101, ID_LIST = 102, ID_BTN_TXT = 103, ID_STATUS = 104,
    ID_BTN_HTML = 105, ID_BTN_TEMA = 106, ID_BUSCA = 107
};

// ---------------- Globais ----------------
HWND g_hList = nullptr;
static HWND g_hMain = nullptr, g_hTree = nullptr, g_hBusca = nullptr, g_hGraf = nullptr;
static HWND g_hBtnTxt = nullptr, g_hBtnHtml = nullptr, g_hBtnTema = nullptr, g_hStatus = nullptr;
static HFONT g_hFont = nullptr, g_hFontBusca = nullptr;
static PageId g_paginaAtual = PAGE_NONE;
static std::map<std::wstring, int> g_chavesTempoReal;
static std::vector<std::pair<std::wstring, std::wstring>> g_dados;  // cache p/ busca
static bool g_escuro = false;
static bool g_ignorarBusca = false;
static HBRUSH g_brFundoEscuro = nullptr, g_brFundoClaro = nullptr;
static const UINT TIMER_REALTIME = 1;

// Cores do tema
static COLORREF CorFundo()   { return g_escuro ? RGB(32, 32, 36)    : RGB(255, 255, 255); }
static COLORREF CorTexto()   { return g_escuro ? RGB(230, 230, 230) : RGB(20, 20, 20); }
static COLORREF CorJanela()  { return g_escuro ? RGB(23, 23, 26)    : RGB(240, 240, 240); }

// ================= Utilitarios do ListView =================
static void ListInsert(const std::wstring& c0, const std::wstring& c1)
{
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = ListView_GetItemCount(g_hList);
    it.pszText = const_cast<LPWSTR>(c0.c_str());
    int idx = ListView_InsertItem(g_hList, &it);
    ListView_SetItemText(g_hList, idx, 1, const_cast<LPWSTR>(c1.c_str()));
}

void AddRow(const std::wstring& prop, const std::wstring& val)
{
    g_dados.push_back({ prop, val });
    ListInsert(prop, val);
}
void AddSection(const std::wstring& title) { AddRow(L"--- " + title + L" ---", L""); }
void AddBlank() { AddRow(L"", L""); }

void ClearList()
{
    ListView_DeleteAllItems(g_hList);
    g_chavesTempoReal.clear();
    g_dados.clear();
}

void SetRowByKey(const std::wstring& key, const std::wstring& prop, const std::wstring& val)
{
    auto it = g_chavesTempoReal.find(key);
    if (it != g_chavesTempoReal.end())
    {
        ListView_SetItemText(g_hList, it->second, 1, const_cast<LPWSTR>(val.c_str()));
    }
    else
    {
        int idx = ListView_GetItemCount(g_hList);
        ListInsert(prop, val);
        g_chavesTempoReal[key] = idx;
    }
}

// ================= Busca / filtro =================
static std::wstring Minusculas(std::wstring s)
{
    for (auto& c : s) c = towlower(c);
    return s;
}

static void RefiltrarLista()
{
    wchar_t filtro[256] = {};
    GetWindowTextW(g_hBusca, filtro, 256);
    std::wstring f = Minusculas(Trim(filtro));

    SendMessageW(g_hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hList);
    for (const auto& linha : g_dados)
    {
        if (f.empty() ||
            Minusculas(linha.first).find(f) != std::wstring::npos ||
            Minusculas(linha.second).find(f) != std::wstring::npos)
        {
            if (f.empty() || !linha.first.empty() || !linha.second.empty())
                ListInsert(linha.first, linha.second);
        }
    }
    SendMessageW(g_hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hList, nullptr, TRUE);

    wchar_t txt[128];
    swprintf_s(txt, L"  %d itens exibidos", ListView_GetItemCount(g_hList));
    SendMessageW(g_hStatus, SB_SETTEXTW, 1, (LPARAM)txt);
}

// ================= Ordenacao por coluna =================
static int  g_colOrdenada = -1;
static bool g_ordemAsc = true;

static void OrdenarPorColuna(int coluna)
{
    if (g_paginaAtual == PAGE_REALTIME || g_paginaAtual == PAGE_GRAPHS) return;
    if (g_dados.empty()) return;

    g_ordemAsc = (g_colOrdenada == coluna) ? !g_ordemAsc : true;
    g_colOrdenada = coluna;

    std::stable_sort(g_dados.begin(), g_dados.end(),
        [coluna](const std::pair<std::wstring, std::wstring>& a,
                 const std::pair<std::wstring, std::wstring>& b)
    {
        // linhas vazias sempre para o fim
        bool va = a.first.empty() && a.second.empty();
        bool vb = b.first.empty() && b.second.empty();
        if (va != vb) return vb;
        const std::wstring& ca = (coluna == 0) ? a.first : a.second;
        const std::wstring& cb = (coluna == 0) ? b.first : b.second;
        int cmp = _wcsicmp(ca.c_str(), cb.c_str());
        return g_ordemAsc ? (cmp < 0) : (cmp > 0);
    });
    RefiltrarLista();
}

// ================= Menu de contexto (copiar) =================
static void CopiarParaClipboard(const std::wstring& texto)
{
    if (!OpenClipboard(g_hMain)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (texto.size() + 1) * sizeof(wchar_t));
    if (mem)
    {
        wchar_t* p = (wchar_t*)GlobalLock(mem);
        wcscpy_s(p, texto.size() + 1, texto.c_str());
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
}

static void MenuContextoLista()
{
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (sel < 0 ? MF_GRAYED : 0), 1, L"Copiar Linha");
    AppendMenuW(menu, MF_STRING | (sel < 0 ? MF_GRAYED : 0), 2, L"Copiar Somente o Valor");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Copiar Tudo (categoria atual)");

    POINT pt; GetCursorPos(&pt);
    int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, g_hMain, nullptr);
    DestroyMenu(menu);
    if (cmd == 0) return;

    wchar_t c0[512], c1[1024];
    if (cmd == 1 && sel >= 0)
    {
        c0[0] = 0; c1[0] = 0;
        ListView_GetItemText(g_hList, sel, 0, c0, 512);
        ListView_GetItemText(g_hList, sel, 1, c1, 1024);
        CopiarParaClipboard(std::wstring(c0) + L": " + c1);
    }
    else if (cmd == 2 && sel >= 0)
    {
        c1[0] = 0;
        ListView_GetItemText(g_hList, sel, 1, c1, 1024);
        CopiarParaClipboard(c1);
    }
    else if (cmd == 3)
    {
        std::wstring tudo;
        int total = ListView_GetItemCount(g_hList);
        for (int i = 0; i < total; i++)
        {
            c0[0] = 0; c1[0] = 0;
            ListView_GetItemText(g_hList, i, 0, c0, 512);
            ListView_GetItemText(g_hList, i, 1, c1, 1024);
            std::wstring prop = c0;
            if (prop.empty() && c1[0] == 0) { tudo += L"\r\n"; continue; }
            tudo += prop + L": " + c1 + L"\r\n";
        }
        CopiarParaClipboard(tudo);
    }
}

// ================= Formatacao =================
std::wstring FormatBytes(unsigned long long b)
{
    const wchar_t* uni[] = { L"Bytes", L"KB", L"MB", L"GB", L"TB", L"PB" };
    double v = (double)b; int i = 0;
    while (v >= 1024.0 && i < 5) { v /= 1024.0; i++; }
    return NumW(v, (i >= 3) ? 2 : (i > 0 ? 1 : 0)) + L" " + uni[i];
}

std::wstring FormatSpeedBits(double bps)
{
    const wchar_t* uni[] = { L"bps", L"Kbps", L"Mbps", L"Gbps" };
    int i = 0;
    while (bps >= 1000.0 && i < 3) { bps /= 1000.0; i++; }
    return NumW(bps, 1) + L" " + uni[i];
}

std::wstring Trim(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n\0");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n\0");
    return s.substr(a, b - a + 1);
}

// ================= Tema =================
static void AplicarTema()
{
    ListView_SetBkColor(g_hList, CorFundo());
    ListView_SetTextBkColor(g_hList, CorFundo());
    ListView_SetTextColor(g_hList, CorTexto());

    TreeView_SetBkColor(g_hTree, CorFundo());
    TreeView_SetTextColor(g_hTree, CorTexto());

    // Barras de rolagem escuras (Windows 10 1809+; inofensivo em versoes antigas)
    SetWindowTheme(g_hList, g_escuro ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SetWindowTheme(g_hTree, g_escuro ? L"DarkMode_Explorer" : L"Explorer", nullptr);

    // Barra de titulo escura
    BOOL escuro = g_escuro ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(g_hMain, 20, &escuro, sizeof(escuro))))
        DwmSetWindowAttribute(g_hMain, 19, &escuro, sizeof(escuro));

    GraficosDefinirTema(g_escuro);
    SetWindowTextW(g_hBtnTema, g_escuro ? L"Tema Claro" : L"Tema Escuro");

    InvalidateRect(g_hMain, nullptr, TRUE);
    InvalidateRect(g_hTree, nullptr, TRUE);
    InvalidateRect(g_hList, nullptr, TRUE);
    InvalidateRect(g_hBusca, nullptr, TRUE);
    if (g_hGraf) InvalidateRect(g_hGraf, nullptr, TRUE);
}

// ================= Exportacao =================
static bool PedirCaminho(wchar_t* caminho, const wchar_t* filtro, const wchar_t* ext)
{
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMain;
    ofn.lpstrFilter = filtro;
    ofn.lpstrFile = caminho;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = ext;
    ofn.lpstrTitle = L"Salvar Relatorio do Sistema";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&ofn) != 0;
}

static void EscreverUtf8(HANDLE h, const std::wstring& w)
{
    if (w.empty()) return;
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::vector<char> buf(n);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), buf.data(), n, nullptr, nullptr);
    DWORD escritos = 0;
    WriteFile(h, buf.data(), (DWORD)buf.size(), &escritos, nullptr);
}

static std::wstring EscapeHtml(const std::wstring& s)
{
    std::wstring r;
    for (wchar_t c : s)
    {
        if (c == L'<') r += L"&lt;";
        else if (c == L'>') r += L"&gt;";
        else if (c == L'&') r += L"&amp;";
        else r += c;
    }
    return r;
}

static void ExportarRelatorio(bool html)
{
    int total = ListView_GetItemCount(g_hList);
    if (total == 0)
    {
        MessageBoxW(g_hMain, L"Selecione uma categoria com dados antes de exportar.", L"Exportar", MB_ICONINFORMATION);
        return;
    }

    wchar_t caminho[MAX_PATH];
    wcscpy_s(caminho, html ? L"Relatorio_Sistema.html" : L"Relatorio_Sistema.txt");
    if (!PedirCaminho(caminho,
        html ? L"Pagina HTML (*.html)\0*.html\0" : L"Arquivo de Texto (*.txt)\0*.txt\0",
        html ? L"html" : L"txt")) return;

    HANDLE h = CreateFileW(caminho, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(g_hMain, L"Nao foi possivel criar o arquivo.", L"Erro", MB_ICONERROR);
        return;
    }

    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD esc = 0;
    WriteFile(h, bom, 3, &esc, nullptr);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t data[64];
    swprintf_s(data, L"%02d/%02d/%04d %02d:%02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);

    wchar_t c0[512], c1[1024];

    if (html)
    {
        EscreverUtf8(h,
            L"<!DOCTYPE html>\n<html lang=\"pt-BR\">\n<head>\n<meta charset=\"utf-8\">\n"
            L"<title>Relatorio do Sistema - SystemInfoPro</title>\n<style>\n"
            L"body{font-family:'Segoe UI',sans-serif;background:#1b1b1f;color:#e4e4e4;margin:24px}\n"
            L"h1{font-size:22px;border-bottom:2px solid #0078d7;padding-bottom:8px}\n"
            L"h3{color:#4db2ff;margin:20px 0 6px 0}\n"
            L".meta{color:#9a9aa2;margin-bottom:16px}\n"
            L"table{border-collapse:collapse;width:100%;margin-bottom:10px}\n"
            L"td{border:1px solid #3a3a42;padding:6px 10px;font-size:14px;vertical-align:top}\n"
            L"td:first-child{width:38%;color:#bcd6ff}\n"
            L"tr:nth-child(even){background:#232329}\n"
            L"</style>\n</head>\n<body>\n<h1>Relatorio do Sistema - SystemInfoPro</h1>\n"
            L"<div class=\"meta\">Gerado em " + std::wstring(data) + L"</div>\n<table>\n");

        for (int i = 0; i < total; i++)
        {
            c0[0] = 0; c1[0] = 0;
            ListView_GetItemText(g_hList, i, 0, c0, 512);
            ListView_GetItemText(g_hList, i, 1, c1, 1024);
            std::wstring prop = c0, val = c1;

            if (prop.empty() && val.empty()) continue;
            if (prop.size() > 6 && prop.substr(0, 4) == L"--- ")
            {
                std::wstring titulo = prop.substr(4);
                size_t fim = titulo.rfind(L" ---");
                if (fim != std::wstring::npos) titulo = titulo.substr(0, fim);
                EscreverUtf8(h, L"</table>\n<h3>" + EscapeHtml(titulo) + L"</h3>\n<table>\n");
            }
            else
            {
                EscreverUtf8(h, L"<tr><td>" + EscapeHtml(prop) + L"</td><td>" +
                                EscapeHtml(val) + L"</td></tr>\n");
            }
        }
        EscreverUtf8(h, L"</table>\n</body>\n</html>\n");
    }
    else
    {
        EscreverUtf8(h, L"=== RELATORIO DO SISTEMA - SystemInfoPro ===\r\n");
        EscreverUtf8(h, std::wstring(L"Data: ") + data + L"\r\n");
        EscreverUtf8(h, L"============================================\r\n\r\n");
        for (int i = 0; i < total; i++)
        {
            c0[0] = 0; c1[0] = 0;
            ListView_GetItemText(g_hList, i, 0, c0, 512);
            ListView_GetItemText(g_hList, i, 1, c1, 1024);
            std::wstring prop = c0, val = c1;
            if (prop.empty() && val.empty()) { EscreverUtf8(h, L"\r\n"); continue; }
            if (prop.size() < 48) prop.resize(48, L' ');
            EscreverUtf8(h, prop + L": " + val + L"\r\n");
        }
    }

    CloseHandle(h);
    MessageBoxW(g_hMain, L"Relatorio salvo com sucesso!", L"Sucesso", MB_ICONINFORMATION);
}

// ================= Ajuste de colunas =================
static void AjustarColunas()
{
    RECT rc; GetClientRect(g_hList, &rc);
    int w = rc.right - rc.left;
    int wProp = 360;
    ListView_SetColumnWidth(g_hList, 0, wProp);
    if (w > wProp + 20) ListView_SetColumnWidth(g_hList, 1, w - wProp - 4);
}

// ================= Carregamento de paginas =================
static void CarregarPagina(PageId p)
{
    KillTimer(g_hMain, TIMER_REALTIME);
    SetCursor(LoadCursor(nullptr, IDC_WAIT));

    // Limpa a busca sem disparar refiltragem
    g_ignorarBusca = true;
    SetWindowTextW(g_hBusca, L"");
    g_ignorarBusca = false;

    bool graficos = (p == PAGE_GRAPHS);
    bool tempoReal = (p == PAGE_REALTIME);
    ShowWindow(g_hGraf, graficos ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hList, graficos ? SW_HIDE : SW_SHOW);
    ShowWindow(g_hBusca, (graficos || tempoReal) ? SW_HIDE : SW_SHOW);

    SendMessageW(g_hList, WM_SETREDRAW, FALSE, 0);
    ClearList();
    g_paginaAtual = p;

    switch (p)
    {
    case PAGE_GRAPHS:   RealtimeReset(); GraficosTick(g_hGraf); SetTimer(g_hMain, TIMER_REALTIME, 1000, nullptr); break;
    case PAGE_REALTIME: RealtimeReset(); LoadRealtime(); SetTimer(g_hMain, TIMER_REALTIME, 1000, nullptr); break;
    case PAGE_TEMPS:    LoadTemperatures(); break;
    case PAGE_CPU:      LoadCPU(); break;
    case PAGE_BOARD:    LoadBoard(); break;
    case PAGE_GPU:      LoadGPU(); break;
    case PAGE_MONITORS: LoadMonitors(); break;
    case PAGE_RAM:      LoadRAM(); break;
    case PAGE_STORAGE:  LoadStorage(); break;
    case PAGE_PCI:      LoadPCI(); break;
    case PAGE_DRIVERS:  LoadDrivers(); break;
    case PAGE_USB:      LoadUSB(); break;
    case PAGE_AUDIO:    LoadAudio(); break;
    case PAGE_PRINTERS: LoadPrinters(); break;
    case PAGE_BATTERY:  LoadBattery(); break;
    case PAGE_OS:       LoadOS(); break;
    case PAGE_PROGRAMS: LoadPrograms(); break;
    case PAGE_STARTUP:  LoadStartup(); break;
    case PAGE_PROCESSES:LoadProcesses(); break;
    case PAGE_USERS:    LoadUsers(); break;
    case PAGE_SERVICES: LoadServices(); break;
    case PAGE_HOTFIX:   LoadHotfixes(); break;
    case PAGE_ENV:      LoadEnvVars(); break;
    case PAGE_SECURITY: LoadSecurity(); break;
    case PAGE_TASKS:    LoadTasks(); break;
    case PAGE_DIRECTX:  LoadDirectX(); break;
    case PAGE_NETWORK:  LoadNetwork(); break;
    case PAGE_TCP:      LoadTcpConnections(); break;
    case PAGE_SHARES:   LoadShares(); break;
    default: break;
    }

    SendMessageW(g_hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hList, nullptr, TRUE);
    AjustarColunas();
    SetCursor(LoadCursor(nullptr, IDC_ARROW));

    wchar_t txt[128];
    swprintf_s(txt, L"  %d itens carregados", ListView_GetItemCount(g_hList));
    SendMessageW(g_hStatus, SB_SETTEXTW, 1, (LPARAM)txt);
}

// ================= Montagem da arvore =================
static HTREEITEM TreeAdd(HTREEITEM pai, const wchar_t* texto, PageId pagina, bool negrito = false)
{
    TVINSERTSTRUCTW tvi{};
    tvi.hParent = pai;
    tvi.hInsertAfter = TVI_LAST;
    tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvi.item.pszText = const_cast<LPWSTR>(texto);
    tvi.item.lParam = (LPARAM)pagina;
    if (negrito)
    {
        tvi.item.mask |= TVIF_STATE;
        tvi.item.state = TVIS_BOLD;
        tvi.item.stateMask = TVIS_BOLD;
    }
    return TreeView_InsertItem(g_hTree, &tvi);
}

static void MontarArvore()
{
    HTREEITEM sensores = TreeAdd(TVI_ROOT, L"Sensores em Tempo Real", PAGE_NONE, true);
    TreeAdd(sensores, L"Graficos de Desempenho", PAGE_GRAPHS);
    TreeAdd(sensores, L"Painel de Monitoramento (Detalhado)", PAGE_REALTIME);
    TreeAdd(sensores, L"Sensores Termicos (Temperaturas)", PAGE_TEMPS);

    HTREEITEM hw = TreeAdd(TVI_ROOT, L"Hardware", PAGE_NONE, true);
    TreeAdd(hw, L"Processador (CPU)", PAGE_CPU);
    TreeAdd(hw, L"Placa-Mae e BIOS", PAGE_BOARD);
    TreeAdd(hw, L"Placa de Video (GPU)", PAGE_GPU);
    TreeAdd(hw, L"Telas e Monitores", PAGE_MONITORS);
    TreeAdd(hw, L"Memoria RAM", PAGE_RAM);
    TreeAdd(hw, L"Armazenamento e SMART", PAGE_STORAGE);
    TreeAdd(hw, L"Dispositivos PCI", PAGE_PCI);
    TreeAdd(hw, L"Drivers de Sistema", PAGE_DRIVERS);
    TreeAdd(hw, L"Dispositivos USB", PAGE_USB);
    TreeAdd(hw, L"Dispositivos de Audio", PAGE_AUDIO);
    TreeAdd(hw, L"Impressoras e Fax", PAGE_PRINTERS);
    TreeAdd(hw, L"Bateria e Energia", PAGE_BATTERY);

    HTREEITEM sw = TreeAdd(TVI_ROOT, L"Software", PAGE_NONE, true);
    TreeAdd(sw, L"Sistema Operacional", PAGE_OS);
    TreeAdd(sw, L"Programas Instalados", PAGE_PROGRAMS);
    TreeAdd(sw, L"Programas de Inicializacao", PAGE_STARTUP);
    TreeAdd(sw, L"Processos em Execucao", PAGE_PROCESSES);
    TreeAdd(sw, L"Contas de Usuario", PAGE_USERS);
    TreeAdd(sw, L"Servicos do Sistema", PAGE_SERVICES);
    TreeAdd(sw, L"Atualizacoes do Windows (KB)", PAGE_HOTFIX);
    TreeAdd(sw, L"Variaveis de Ambiente", PAGE_ENV);
    TreeAdd(sw, L"Seguranca (Antivirus / TPM)", PAGE_SECURITY);
    TreeAdd(sw, L"Tarefas Agendadas", PAGE_TASKS);
    TreeAdd(sw, L"DirectX e Codecs", PAGE_DIRECTX);

    HTREEITEM rede = TreeAdd(TVI_ROOT, L"Rede", PAGE_NONE, true);
    TreeAdd(rede, L"Adaptadores de Conexao", PAGE_NETWORK);
    TreeAdd(rede, L"Conexoes TCP Ativas", PAGE_TCP);
    TreeAdd(rede, L"Compartilhamentos de Rede", PAGE_SHARES);

    HTREEITEM raiz = TreeView_GetRoot(g_hTree);
    while (raiz)
    {
        TreeView_Expand(g_hTree, raiz, TVE_EXPAND);
        raiz = TreeView_GetNextSibling(g_hTree, raiz);
    }
}

// ================= Layout =================
static void Redimensionar()
{
    RECT rc; GetClientRect(g_hMain, &rc);
    SendMessageW(g_hStatus, WM_SIZE, 0, 0);
    RECT rcSt; GetWindowRect(g_hStatus, &rcSt);
    int hStatus = rcSt.bottom - rcSt.top;

    int larguraArvore = max(280, (rc.right - rc.left) / 4);
    int hBtn = 34;
    int hUtil = rc.bottom - hStatus;
    int hBusca = 28;

    MoveWindow(g_hTree, 0, 0, larguraArvore, hUtil - 3 * hBtn, TRUE);
    MoveWindow(g_hBtnTxt,  0, hUtil - 3 * hBtn, larguraArvore, hBtn, TRUE);
    MoveWindow(g_hBtnHtml, 0, hUtil - 2 * hBtn, larguraArvore, hBtn, TRUE);
    MoveWindow(g_hBtnTema, 0, hUtil - 1 * hBtn, larguraArvore, hBtn, TRUE);

    int xDir = larguraArvore + 3;
    int wDir = rc.right - xDir;

    bool buscaVisivel = IsWindowVisible(g_hBusca);
    int topoLista = buscaVisivel ? hBusca + 2 : 0;
    MoveWindow(g_hBusca, xDir, 0, wDir, hBusca, TRUE);
    MoveWindow(g_hList, xDir, topoLista, wDir, hUtil - topoLista, TRUE);
    MoveWindow(g_hGraf, xDir, 0, wDir, hUtil, TRUE);
    AjustarColunas();
}

// ================= Procedimento da janela =================
static LRESULT CALLBACK JanelaProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hMain = h;

        g_hFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_hFontBusca = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_brFundoEscuro = CreateSolidBrush(RGB(32, 32, 36));
        g_brFundoClaro = CreateSolidBrush(RGB(255, 255, 255));

        g_hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 100, 100, h, (HMENU)(INT_PTR)ID_TREE, nullptr, nullptr);

        g_hBusca = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 100, 26, h, (HMENU)(INT_PTR)ID_BUSCA, nullptr, nullptr);
        SendMessageW(g_hBusca, EM_SETCUEBANNER, TRUE, (LPARAM)L"Pesquisar nesta categoria...");

        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
            0, 0, 100, 100, h, (HMENU)(INT_PTR)ID_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 360; col.pszText = const_cast<LPWSTR>(L"Propriedade");
        ListView_InsertColumn(g_hList, 0, &col);
        col.cx = 500; col.pszText = const_cast<LPWSTR>(L"Valor");
        ListView_InsertColumn(g_hList, 1, &col);

        g_hGraf = CriarJanelaGraficos(h);

        g_hBtnTxt = CreateWindowW(L"BUTTON", L"Exportar TXT",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 100, 34, h, (HMENU)(INT_PTR)ID_BTN_TXT, nullptr, nullptr);
        g_hBtnHtml = CreateWindowW(L"BUTTON", L"Exportar HTML",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 100, 34, h, (HMENU)(INT_PTR)ID_BTN_HTML, nullptr, nullptr);
        g_hBtnTema = CreateWindowW(L"BUTTON", L"Tema Escuro",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 100, 34, h, (HMENU)(INT_PTR)ID_BTN_TEMA, nullptr, nullptr);

        g_hStatus = CreateWindowW(STATUSCLASSNAMEW, L"",
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, h, (HMENU)(INT_PTR)ID_STATUS, nullptr, nullptr);
        int partes[2] = { 320, -1 };
        SendMessageW(g_hStatus, SB_SETPARTS, 2, (LPARAM)partes);
        SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)L"  SystemInfoPro - pronto");

        // Fonte moderna em todos os controles
        HWND ctls[] = { g_hTree, g_hList, g_hBtnTxt, g_hBtnHtml, g_hBtnTema, g_hStatus };
        for (HWND c : ctls) SendMessageW(c, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hBusca, WM_SETFONT, (WPARAM)g_hFontBusca, TRUE);

        MontarArvore();
        AplicarTema();
        return 0;
    }
    case WM_SIZE:
        Redimensionar();
        return 0;

    case WM_ERASEBKGND:
    {
        HDC dc = (HDC)wp;
        RECT rc; GetClientRect(h, &rc);
        HBRUSH br = CreateSolidBrush(CorJanela());
        FillRect(dc, &rc, br);
        DeleteObject(br);
        return 1;
    }

    case WM_CTLCOLOREDIT:
        if ((HWND)lp == g_hBusca)
        {
            HDC dc = (HDC)wp;
            SetTextColor(dc, CorTexto());
            SetBkColor(dc, CorFundo());
            return (LRESULT)(g_escuro ? g_brFundoEscuro : g_brFundoClaro);
        }
        break;

    case WM_NOTIFY:
    {
        LPNMHDR nm = (LPNMHDR)lp;
        if (nm->hwndFrom == g_hTree && nm->code == TVN_SELCHANGEDW)
        {
            LPNMTREEVIEWW tv = (LPNMTREEVIEWW)lp;
            PageId p = (PageId)tv->itemNew.lParam;
            if (p != PAGE_NONE) { g_colOrdenada = -1; CarregarPagina(p); Redimensionar(); }
        }
        else if (nm->hwndFrom == g_hList && nm->code == LVN_COLUMNCLICK)
        {
            LPNMLISTVIEW lv = (LPNMLISTVIEW)lp;
            OrdenarPorColuna(lv->iSubItem);
        }
        else if (nm->hwndFrom == g_hList && nm->code == NM_RCLICK)
        {
            MenuContextoLista();
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_BTN_TXT)  ExportarRelatorio(false);
        else if (LOWORD(wp) == ID_BTN_HTML) ExportarRelatorio(true);
        else if (LOWORD(wp) == ID_BTN_TEMA) { g_escuro = !g_escuro; AplicarTema(); }
        else if (LOWORD(wp) == ID_BUSCA && HIWORD(wp) == EN_CHANGE && !g_ignorarBusca)
            RefiltrarLista();
        return 0;

    case WM_TIMER:
        if (wp == TIMER_REALTIME)
        {
            if (g_paginaAtual == PAGE_REALTIME) LoadRealtime();
            else if (g_paginaAtual == PAGE_GRAPHS) GraficosTick(g_hGraf);
        }
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 900;
        mmi->ptMinTrackSize.y = 560;
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, TIMER_REALTIME);
        RealtimeShutdown();
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBusca) DeleteObject(g_hFontBusca);
        if (g_brFundoEscuro) DeleteObject(g_brFundoEscuro);
        if (g_brFundoClaro) DeleteObject(g_brFundoClaro);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ================= Ponto de entrada =================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmd)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr))
    {
        CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr, EOAC_NONE, nullptr);
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = JanelaProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // pintado no WM_ERASEBKGND (suporta tema)
    wc.lpszClassName = L"SystemInfoProJanela";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
        L"SystemInfoPro - Painel de Informacoes do Sistema",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1250, 740,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
