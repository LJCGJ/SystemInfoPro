// ============================================================
//  SystemInfoPro - Painel de Informacoes do Sistema (C++ / Win32)
//  main.cpp - Interface estilo VS Code:
//    - Barra de menus escura no topo (Arquivo / Exibir / Ajuda)
//    - Tema escuro por padrao (alternavel)
//    - Icones nas categorias da arvore
//    - Faixa de titulo da categoria + busca integrada
// ============================================================
#include "common.h"
#include <windowsx.h>
#include <commdlg.h>
#include <objbase.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------- Identificadores de pagina ----------------
enum PageId
{
    PAGE_NONE = 0,
    PAGE_RESUMO,
    PAGE_GRAPHS, PAGE_REALTIME, PAGE_TEMPS,
    PAGE_CPU, PAGE_BOARD, PAGE_GPU, PAGE_MONITORS, PAGE_RAM,
    PAGE_STORAGE, PAGE_PCI, PAGE_DRIVERS, PAGE_USB, PAGE_AUDIO, PAGE_PRINTERS, PAGE_BATTERY,
    PAGE_OS, PAGE_PROGRAMS, PAGE_STARTUP, PAGE_PROCESSES, PAGE_USERS,
    PAGE_SERVICES, PAGE_HOTFIX, PAGE_ENV, PAGE_SECURITY, PAGE_TASKS, PAGE_DIRECTX, PAGE_STARTUP_DIS,
    PAGE_NETWORK, PAGE_TCP, PAGE_SHARES,
    PAGE_BENCH, PAGE_SNAP, PAGE_DISK, PAGE_NETDIAG, PAGE_SENSORLOG
};

// ---------------- IDs de comandos ----------------
enum
{
    ID_TREE = 101, ID_LIST = 102, ID_STATUS = 104, ID_BUSCA = 107, ID_TITULO = 108,
    IDM_EXPORT_TXT = 201, IDM_EXPORT_HTML = 202, IDM_SAIR = 203, IDM_EXPORT_FULL = 204,
    IDM_TEMA = 210, IDM_ATUALIZAR = 211,
    IDM_SOBRE = 220,
    IDM_BENCH = 230, IDM_SNAP_CRIAR = 231, IDM_SNAP_COMP = 232,
    IDM_DISK = 233, IDM_NETDIAG = 234, IDM_LOG_INICIAR = 235, IDM_LOG_PARAR = 236,
    IDM_INICIAR_WIN = 250,
    IDM_TRAY_RESTAURAR = 240, IDM_TRAY_SAIR = 241
};

#define WM_TRAYICON (WM_APP + 10)

// ---------------- Globais ----------------
HWND g_hList = nullptr;
static HWND g_hMain = nullptr, g_hTree = nullptr, g_hBusca = nullptr, g_hGraf = nullptr;
static HWND g_hBarra = nullptr, g_hTitulo = nullptr, g_hStatus = nullptr;
static HFONT g_hFont = nullptr, g_hFontBusca = nullptr, g_hFontTitulo = nullptr, g_hFontBarra = nullptr;
static PageId g_paginaAtual = PAGE_NONE;
static std::map<std::wstring, int> g_chavesTempoReal;

// Cada linha guarda propriedade, valor e (opcional) uma acao de gerenciamento.
struct LinhaDado
{
    std::wstring prop, val;
    int tipoAcao = 0;          // 0=nenhuma, 1=processo, 2=servico, 3=startup
    std::wstring meta;         // PID / DisplayName / nome
};
static std::vector<LinhaDado> g_dados;
static std::map<int, std::wstring> g_titulos;     // PageId -> rotulo (com icone)
static bool g_escuro = true;                      // tema escuro por padrao
static bool g_ignorarBusca = false;
static HBRUSH g_brEdit = nullptr, g_brTitulo = nullptr;
static const UINT TIMER_REALTIME = 1;
static const UINT TIMER_TRAY = 2;

// Bandeja / alertas
static NOTIFYICONDATAW g_nid{};
static bool g_trayCriado = false, g_avisoTrayMostrado = false;
static ULONGLONG g_alertaRam = 0, g_alertaTemp = 0, g_alertaDisco = 0;
static const int ALTURA_BARRA = 38;
static const int ALTURA_TITULO = 40;

// ---------------- Cores do tema ----------------
static COLORREF CorFundo()      { return g_escuro ? RGB(30, 30, 34)    : RGB(255, 255, 255); }
static COLORREF CorTexto()      { return g_escuro ? RGB(225, 225, 228) : RGB(25, 25, 25); }
static COLORREF CorJanela()     { return g_escuro ? RGB(24, 24, 27)    : RGB(243, 243, 243); }
static COLORREF CorBarra()      { return g_escuro ? RGB(40, 40, 46)    : RGB(235, 235, 238); }
static COLORREF CorBarraHot()   { return g_escuro ? RGB(62, 62, 70)    : RGB(215, 218, 222); }
static COLORREF CorDestaque()   { return RGB(0, 122, 204); }   // azul VS Code
static COLORREF CorTituloTxt()  { return g_escuro ? RGB(240, 240, 242) : RGB(20, 20, 20); }

// ---------------- Menus escuros (API nao documentada do uxtheme) ----------------
static void AtualizarModoMenus()
{
    typedef int (WINAPI* PFN_SetPreferredAppMode)(int);   // ordinal 135 (Win10 1809+)
    typedef void (WINAPI* PFN_FlushMenuThemes)();          // ordinal 136
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) return;
    auto setMode = (PFN_SetPreferredAppMode)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    auto flush = (PFN_FlushMenuThemes)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (setMode) setMode(g_escuro ? 2 /*ForceDark*/ : 3 /*ForceLight*/);
    if (flush) flush();
}

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
    g_dados.push_back({ prop, val, 0, L"" });
    ListInsert(prop, val);
}
void AddActionRow(const std::wstring& prop, const std::wstring& val,
                  int tipoAcao, const std::wstring& meta)
{
    g_dados.push_back({ prop, val, tipoAcao, meta });
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
            Minusculas(linha.prop).find(f) != std::wstring::npos ||
            Minusculas(linha.val).find(f) != std::wstring::npos)
        {
            if (f.empty() || !linha.prop.empty() || !linha.val.empty())
                ListInsert(linha.prop, linha.val);
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
        [coluna](const LinhaDado& a, const LinhaDado& b)
    {
        bool va = a.prop.empty() && a.val.empty();
        bool vb = b.prop.empty() && b.val.empty();
        if (va != vb) return vb;
        const std::wstring& ca = (coluna == 0) ? a.prop : a.val;
        const std::wstring& cb = (coluna == 0) ? b.prop : b.val;
        int cmp = _wcsicmp(ca.c_str(), cb.c_str());
        return g_ordemAsc ? (cmp < 0) : (cmp > 0);
    });
    RefiltrarLista();
}

// ================= Menu de contexto (copiar) =================
static void CarregarPagina(PageId p);   // forward

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

// Encontra a linha em g_dados que corresponde ao item selecionado (texto identico)
static const LinhaDado* AcharLinhaSelecionada(int sel)
{
    if (sel < 0) return nullptr;
    wchar_t c0[512] = {}, c1[1024] = {};
    ListView_GetItemText(g_hList, sel, 0, c0, 512);
    ListView_GetItemText(g_hList, sel, 1, c1, 1024);
    std::wstring p = c0, v = c1;
    for (const auto& l : g_dados)
        if (l.prop == p && l.val == v) return &l;
    return nullptr;
}

static void MenuContextoLista()
{
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    const LinhaDado* linha = AcharLinhaSelecionada(sel);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (sel < 0 ? MF_GRAYED : 0), 1, L"📄  Copiar Linha");
    AppendMenuW(menu, MF_STRING | (sel < 0 ? MF_GRAYED : 0), 2, L"✂  Copiar Somente o Valor");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"📋  Copiar Tudo (categoria atual)");

    // Acoes de gerenciamento ativo, conforme o tipo da linha
    if (linha && linha->tipoAcao != 0)
    {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        if (linha->tipoAcao == 1)
            AppendMenuW(menu, MF_STRING, 10, L"🛑  Finalizar este Processo");
        else if (linha->tipoAcao == 2)
        {
            AppendMenuW(menu, MF_STRING, 11, L"▶  Iniciar Servico");
            AppendMenuW(menu, MF_STRING, 12, L"⏹  Parar Servico");
        }
        else if (linha->tipoAcao == 3)
            AppendMenuW(menu, MF_STRING, 13, L"🚫  Desabilitar na Inicializacao");
        else if (linha->tipoAcao == 4)
            AppendMenuW(menu, MF_STRING, 14, L"✅  Reabilitar na Inicializacao");
    }

    POINT pt; GetCursorPos(&pt);
    int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, g_hMain, nullptr);
    DestroyMenu(menu);
    if (cmd == 0) return;

    // ---- Acoes de gerenciamento ----
    if (cmd >= 10 && linha)
    {
        bool mudou = false;
        if (cmd == 10)      mudou = GerenciarFinalizarProcesso(g_hMain, (DWORD)_wtoi(linha->meta.c_str()), linha->prop);
        else if (cmd == 11) mudou = GerenciarServico(g_hMain, linha->meta, true);
        else if (cmd == 12) mudou = GerenciarServico(g_hMain, linha->meta, false);
        else if (cmd == 13) mudou = GerenciarDesabilitarStartup(g_hMain, linha->meta);
        else if (cmd == 14) mudou = GerenciarReabilitarStartup(g_hMain, linha->meta);
        if (mudou && g_paginaAtual != PAGE_NONE) CarregarPagina(g_paginaAtual);
        return;
    }

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

    SetWindowTheme(g_hList, g_escuro ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SetWindowTheme(g_hTree, g_escuro ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    HWND hdr = ListView_GetHeader(g_hList);
    if (hdr) SetWindowTheme(hdr, g_escuro ? L"DarkMode_ItemsView" : L"ItemsView", nullptr);

    BOOL escuro = g_escuro ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(g_hMain, 20, &escuro, sizeof(escuro))))
        DwmSetWindowAttribute(g_hMain, 19, &escuro, sizeof(escuro));

    if (g_brEdit) DeleteObject(g_brEdit);
    if (g_brTitulo) DeleteObject(g_brTitulo);
    g_brEdit = CreateSolidBrush(g_escuro ? RGB(45, 45, 52) : RGB(255, 255, 255));
    g_brTitulo = CreateSolidBrush(CorJanela());

    GraficosDefinirTema(g_escuro);
    AtualizarModoMenus();

    InvalidateRect(g_hMain, nullptr, TRUE);
    InvalidateRect(g_hBarra, nullptr, TRUE);
    InvalidateRect(g_hTree, nullptr, TRUE);
    InvalidateRect(g_hList, nullptr, TRUE);
    InvalidateRect(g_hTitulo, nullptr, TRUE);
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
            L"h1{font-size:22px;border-bottom:2px solid #007acc;padding-bottom:8px}\n"
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
// Chama o modulo de cada pagina (exceto as de tempo real)
static void ChamarLoader(PageId p)
{
    switch (p)
    {
    case PAGE_RESUMO:   LoadResumo(); break;
    case PAGE_STARTUP_DIS: LoadStartupDisabled(); break;
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
    case PAGE_BENCH:    LoadBenchmark(); break;
    case PAGE_SNAP:     LoadSnapshotInfo(); break;
    case PAGE_DISK:     LoadDiskAnalyzer(); break;
    case PAGE_NETDIAG:  LoadNetDiag(); break;
    case PAGE_SENSORLOG:LoadSensorLog(); break;
    default: break;
    }
}

static void CarregarPagina(PageId p)
{
    KillTimer(g_hMain, TIMER_REALTIME);
    SetCursor(LoadCursor(nullptr, IDC_WAIT));

    g_ignorarBusca = true;
    SetWindowTextW(g_hBusca, L"");
    g_ignorarBusca = false;
    g_colOrdenada = -1;

    bool graficos = (p == PAGE_GRAPHS);
    bool tempoReal = (p == PAGE_REALTIME);
    ShowWindow(g_hGraf, graficos ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hList, graficos ? SW_HIDE : SW_SHOW);
    EnableWindow(g_hBusca, !(graficos || tempoReal));

    // Titulo da categoria
    auto itTit = g_titulos.find((int)p);
    SetWindowTextW(g_hTitulo, itTit != g_titulos.end() ? itTit->second.c_str() : L"");

    SendMessageW(g_hList, WM_SETREDRAW, FALSE, 0);
    ClearList();
    g_paginaAtual = p;

    if (p == PAGE_GRAPHS)
    {
        RealtimeReset(); GraficosTick(g_hGraf);
        SetTimer(g_hMain, TIMER_REALTIME, 1000, nullptr);
    }
    else if (p == PAGE_REALTIME)
    {
        RealtimeReset(); LoadRealtime();
        SetTimer(g_hMain, TIMER_REALTIME, 1000, nullptr);
    }
    else
    {
        ChamarLoader(p);
    }

    SendMessageW(g_hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hList, nullptr, TRUE);
    AjustarColunas();
    SetCursor(LoadCursor(nullptr, IDC_ARROW));

    wchar_t txt[128];
    swprintf_s(txt, L"  %d itens carregados", ListView_GetItemCount(g_hList));
    SendMessageW(g_hStatus, SB_SETTEXTW, 1, (LPARAM)txt);
}

// ================= Relatorio completo (todas as categorias) =================
static void ExportarRelatorioCompleto()
{
    wchar_t caminho[MAX_PATH];
    wcscpy_s(caminho, L"Relatorio_Completo_Sistema.html");
    if (!PedirCaminho(caminho, L"Pagina HTML (*.html)\0*.html\0", L"html")) return;

    HANDLE h = CreateFileW(caminho, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(g_hMain, L"Nao foi possivel criar o arquivo.", L"Erro", MB_ICONERROR);
        return;
    }

    const PageId paginas[] = {
        PAGE_OS, PAGE_CPU, PAGE_BOARD, PAGE_GPU, PAGE_MONITORS, PAGE_RAM,
        PAGE_STORAGE, PAGE_PCI, PAGE_DRIVERS, PAGE_USB, PAGE_AUDIO, PAGE_PRINTERS,
        PAGE_BATTERY, PAGE_TEMPS, PAGE_PROGRAMS, PAGE_STARTUP, PAGE_PROCESSES,
        PAGE_USERS, PAGE_SERVICES, PAGE_HOTFIX, PAGE_ENV, PAGE_SECURITY,
        PAGE_TASKS, PAGE_DIRECTX, PAGE_NETWORK, PAGE_TCP, PAGE_SHARES
    };

    PageId paginaAnterior = g_paginaAtual;
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    KillTimer(g_hMain, TIMER_REALTIME);

    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD esc = 0;
    WriteFile(h, bom, 3, &esc, nullptr);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t data[64];
    swprintf_s(data, L"%02d/%02d/%04d %02d:%02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);

    EscreverUtf8(h,
        L"<!DOCTYPE html>\n<html lang=\"pt-BR\">\n<head>\n<meta charset=\"utf-8\">\n"
        L"<title>Relatorio Completo do Sistema - SystemInfoPro</title>\n<style>\n"
        L"body{font-family:'Segoe UI',sans-serif;background:#1b1b1f;color:#e4e4e4;margin:24px;max-width:1100px}\n"
        L"h1{font-size:24px;border-bottom:3px solid #007acc;padding-bottom:10px}\n"
        L"h2{font-size:19px;color:#ffffff;background:#007acc;padding:8px 14px;border-radius:6px;margin-top:34px}\n"
        L"h3{color:#4db2ff;margin:18px 0 6px 0}\n"
        L".meta{color:#9a9aa2;margin-bottom:20px}\n"
        L"table{border-collapse:collapse;width:100%;margin-bottom:10px}\n"
        L"td{border:1px solid #3a3a42;padding:6px 10px;font-size:14px;vertical-align:top}\n"
        L"td:first-child{width:38%;color:#bcd6ff}\n"
        L"tr:nth-child(even){background:#232329}\n"
        L"</style>\n</head>\n<body>\n<h1>💻 Relatorio Completo do Sistema - SystemInfoPro</h1>\n"
        L"<div class=\"meta\">Gerado em " + std::wstring(data) + L"</div>\n");

    for (PageId p : paginas)
    {
        std::wstring titulo = g_titulos.count((int)p) ? g_titulos[(int)p] : L"Categoria";
        std::wstring statusTxt = L"  Coletando: " + titulo + L"...";
        SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)statusTxt.c_str());
        UpdateWindow(g_hStatus);

        ClearList();
        ChamarLoader(p);

        EscreverUtf8(h, L"<h2>" + EscapeHtml(titulo) + L"</h2>\n<table>\n");
        for (const auto& linha : g_dados)
        {
            const std::wstring& prop = linha.prop;
            const std::wstring& val = linha.val;
            if (prop.empty() && val.empty()) continue;
            if (prop.size() > 6 && prop.substr(0, 4) == L"--- ")
            {
                std::wstring t = prop.substr(4);
                size_t fim = t.rfind(L" ---");
                if (fim != std::wstring::npos) t = t.substr(0, fim);
                EscreverUtf8(h, L"</table>\n<h3>" + EscapeHtml(t) + L"</h3>\n<table>\n");
            }
            else
            {
                EscreverUtf8(h, L"<tr><td>" + EscapeHtml(prop) + L"</td><td>" +
                                EscapeHtml(val) + L"</td></tr>\n");
            }
        }
        EscreverUtf8(h, L"</table>\n");
    }

    EscreverUtf8(h, L"</body>\n</html>\n");
    CloseHandle(h);

    SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)L"  SystemInfoPro - pronto");
    CarregarPagina(paginaAnterior != PAGE_NONE ? paginaAnterior : PAGE_OS);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    MessageBoxW(g_hMain, L"Relatorio completo salvo com sucesso!\n\nAbra o arquivo HTML em qualquer navegador.",
                L"Sucesso", MB_ICONINFORMATION);
}

// ================= Bandeja e alertas =================
static void TrayCriar()
{
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"SystemInfoPro");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_trayCriado = true;
}

static void TrayRemover()
{
    if (g_trayCriado) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayCriado = false;
}

static void TrayTooltip(const std::wstring& texto)
{
    if (!g_trayCriado) return;
    wcsncpy_s(g_nid.szTip, texto.c_str(), _TRUNCATE);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayBalao(const wchar_t* titulo, const std::wstring& msg, DWORD icone = NIIF_WARNING)
{
    if (!g_trayCriado) return;
    g_nid.uFlags = NIF_INFO;
    wcscpy_s(g_nid.szInfoTitle, titulo);
    wcsncpy_s(g_nid.szInfo, msg.c_str(), _TRUNCATE);
    g_nid.dwInfoFlags = icone;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayTick()
{
    Metricas m;
    AmostrarMetricas(m);

    std::wstring tip = L"SystemInfoPro";
    if (m.cpuPct >= 0) tip += L"  |  CPU " + NumW(m.cpuPct) + L"%";
    if (m.ramPct >= 0) tip += L"  |  RAM " + NumW(m.ramPct) + L"%";
    TrayTooltip(tip);

    ULONGLONG agora = GetTickCount64();

    // RAM acima de 95% (aviso a cada 10 min no maximo)
    if (m.ramPct >= 95 && agora - g_alertaRam > 10 * 60 * 1000)
    {
        g_alertaRam = agora;
        TrayBalao(L"⚠ Memoria RAM quase cheia",
                  L"Uso atual: " + NumW(m.ramPct) + L"%. Feche programas para evitar travamentos.");
    }

    // GPU acima de 85 C (NVIDIA)
    NvmlInfo nv;
    if (NvmlConsultar(nv) && nv.temperaturaC >= 85 && agora - g_alertaTemp > 10 * 60 * 1000)
    {
        g_alertaTemp = agora;
        TrayBalao(L"🌡 Temperatura alta na GPU",
                  nv.nome + L" esta a " + NumW(nv.temperaturaC) + L" °C. Verifique a ventilacao.");
    }

    // Disco do sistema com menos de 5% livre (aviso a cada 30 min)
    if (agora - g_alertaDisco > 30 * 60 * 1000)
    {
        wchar_t winDir[MAX_PATH];
        GetWindowsDirectoryW(winDir, MAX_PATH);
        wchar_t raiz[4] = { winDir[0], L':', L'\\', 0 };
        ULARGE_INTEGER livre{}, total{};
        if (GetDiskFreeSpaceExW(raiz, &livre, &total, nullptr) && total.QuadPart > 0)
        {
            int pctLivre = (int)(livre.QuadPart * 100 / total.QuadPart);
            if (pctLivre < 5)
            {
                g_alertaDisco = agora;
                TrayBalao(L"💾 Disco do sistema quase cheio",
                          std::wstring(L"Unidade ") + raiz + L" com apenas " +
                          FormatBytes(livre.QuadPart) + L" livres (" + NumW(pctLivre) + L"%).");
            }
        }
    }
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
    if (pagina != PAGE_NONE) g_titulos[(int)pagina] = texto;
    return TreeView_InsertItem(g_hTree, &tvi);
}

static void MontarArvore()
{
    TreeAdd(TVI_ROOT, L"🏠 Resumo do Sistema", PAGE_RESUMO, true);

    HTREEITEM sensores = TreeAdd(TVI_ROOT, L"⚡ Sensores em Tempo Real", PAGE_NONE, true);
    TreeAdd(sensores, L"📈 Graficos de Desempenho", PAGE_GRAPHS);
    TreeAdd(sensores, L"📟 Painel de Monitoramento", PAGE_REALTIME);
    TreeAdd(sensores, L"🌡 Sensores Termicos", PAGE_TEMPS);

    HTREEITEM hw = TreeAdd(TVI_ROOT, L"🔧 Hardware", PAGE_NONE, true);
    TreeAdd(hw, L"⚙ Processador (CPU)", PAGE_CPU);
    TreeAdd(hw, L"🧩 Placa-Mae e BIOS", PAGE_BOARD);
    TreeAdd(hw, L"🎮 Placa de Video (GPU)", PAGE_GPU);
    TreeAdd(hw, L"🖥 Telas e Monitores", PAGE_MONITORS);
    TreeAdd(hw, L"🧠 Memoria RAM", PAGE_RAM);
    TreeAdd(hw, L"💾 Armazenamento e SMART", PAGE_STORAGE);
    TreeAdd(hw, L"🎛 Dispositivos PCI", PAGE_PCI);
    TreeAdd(hw, L"🛠 Drivers de Sistema", PAGE_DRIVERS);
    TreeAdd(hw, L"🔌 Dispositivos USB", PAGE_USB);
    TreeAdd(hw, L"🔊 Dispositivos de Audio", PAGE_AUDIO);
    TreeAdd(hw, L"🖨 Impressoras e Fax", PAGE_PRINTERS);
    TreeAdd(hw, L"🔋 Bateria e Energia", PAGE_BATTERY);

    HTREEITEM sw = TreeAdd(TVI_ROOT, L"📦 Software", PAGE_NONE, true);
    TreeAdd(sw, L"💻 Sistema Operacional", PAGE_OS);
    TreeAdd(sw, L"📦 Programas Instalados", PAGE_PROGRAMS);
    TreeAdd(sw, L"🚀 Programas de Inicializacao", PAGE_STARTUP);
    TreeAdd(sw, L"📋 Processos em Execucao", PAGE_PROCESSES);
    TreeAdd(sw, L"👤 Contas de Usuario", PAGE_USERS);
    TreeAdd(sw, L"🧰 Servicos do Sistema", PAGE_SERVICES);
    TreeAdd(sw, L"🩹 Atualizacoes do Windows (KB)", PAGE_HOTFIX);
    TreeAdd(sw, L"📑 Variaveis de Ambiente", PAGE_ENV);
    TreeAdd(sw, L"🛡 Seguranca (Antivirus / TPM)", PAGE_SECURITY);
    TreeAdd(sw, L"⏰ Tarefas Agendadas", PAGE_TASKS);
    TreeAdd(sw, L"🎬 DirectX e Codecs", PAGE_DIRECTX);
    TreeAdd(sw, L"🚫 Inicializacao Desabilitada", PAGE_STARTUP_DIS);

    HTREEITEM rede = TreeAdd(TVI_ROOT, L"🌐 Rede", PAGE_NONE, true);
    TreeAdd(rede, L"📡 Adaptadores de Conexao", PAGE_NETWORK);
    TreeAdd(rede, L"🔗 Conexoes TCP Ativas", PAGE_TCP);
    TreeAdd(rede, L"📁 Compartilhamentos de Rede", PAGE_SHARES);

    HTREEITEM ferr = TreeAdd(TVI_ROOT, L"🧪 Ferramentas", PAGE_NONE, true);
    TreeAdd(ferr, L"🚀 Benchmark do Sistema", PAGE_BENCH);
    TreeAdd(ferr, L"📊 Analisador de Disco", PAGE_DISK);
    TreeAdd(ferr, L"🌐 Diagnostico de Rede", PAGE_NETDIAG);
    TreeAdd(ferr, L"📸 Snapshot e Comparacao", PAGE_SNAP);
    TreeAdd(ferr, L"⏺ Log de Sensores (CSV)", PAGE_SENSORLOG);

    HTREEITEM raiz = TreeView_GetRoot(g_hTree);
    while (raiz)
    {
        TreeView_Expand(g_hTree, raiz, TVE_EXPAND);
        raiz = TreeView_GetNextSibling(g_hTree, raiz);
    }
}

// ============================================================
//  Barra de menus superior (estilo VS Code)
// ============================================================
namespace
{
    struct ItemBarra { std::wstring rotulo; RECT rc; };
    std::vector<ItemBarra> g_itensBarra = { { L"Arquivo", {} }, { L"Exibir", {} }, { L"Ferramentas", {} }, { L"Ajuda", {} } };
    int g_hotBarra = -1;

    void AbrirMenuBarra(int indice)
    {
        HMENU menu = CreatePopupMenu();
        if (indice == 0)   // Arquivo
        {
            AppendMenuW(menu, MF_STRING, IDM_EXPORT_TXT, L"📄  Exportar Categoria como TXT...");
            AppendMenuW(menu, MF_STRING, IDM_EXPORT_HTML, L"🌐  Exportar Categoria como HTML...");
            AppendMenuW(menu, MF_STRING, IDM_EXPORT_FULL, L"🗂  Exportar Relatorio Completo (tudo)...");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_SAIR, L"❌  Sair");
        }
        else if (indice == 1)   // Exibir
        {
            AppendMenuW(menu, MF_STRING, IDM_TEMA,
                g_escuro ? L"☀  Mudar para Tema Claro" : L"🌙  Mudar para Tema Escuro");
            AppendMenuW(menu, MF_STRING, IDM_ATUALIZAR, L"🔄  Atualizar Categoria\tF5");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING | (AppIniciaComWindows() ? MF_CHECKED : 0),
                        IDM_INICIAR_WIN, L"🔁  Iniciar com o Windows");
        }
        else if (indice == 2)   // Ferramentas
        {
            AppendMenuW(menu, MF_STRING | (BenchmarkEmExecucao() ? MF_GRAYED : 0),
                        IDM_BENCH, L"🚀  Executar Benchmark");
            AppendMenuW(menu, MF_STRING | (DiskAnalyzerEmExecucao() ? MF_GRAYED : 0),
                        IDM_DISK, L"📊  Analisar Espaco em Disco...");
            AppendMenuW(menu, MF_STRING | (NetDiagEmExecucao() ? MF_GRAYED : 0),
                        IDM_NETDIAG, L"🌐  Diagnostico de Rede");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_SNAP_CRIAR, L"📸  Criar Snapshot do Sistema...");
            AppendMenuW(menu, MF_STRING, IDM_SNAP_COMP, L"🔍  Comparar com Snapshot...");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            if (SensorLogAtivo())
                AppendMenuW(menu, MF_STRING, IDM_LOG_PARAR, L"⏹  Parar Log de Sensores");
            else
                AppendMenuW(menu, MF_STRING, IDM_LOG_INICIAR, L"⏺  Iniciar Log de Sensores...");
        }
        else                    // Ajuda
        {
            AppendMenuW(menu, MF_STRING, IDM_SOBRE, L"ℹ  Sobre o SystemInfoPro");
        }

        RECT rc = g_itensBarra[indice].rc;
        POINT pt{ rc.left, rc.bottom };
        ClientToScreen(g_hBarra, &pt);
        int cmd = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                                 pt.x, pt.y, 0, g_hBarra, nullptr);
        DestroyMenu(menu);
        if (cmd) PostMessageW(g_hMain, WM_COMMAND, cmd, 0);
    }

    LRESULT CALLBACK BarraProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dcTela = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            int W = rc.right, H = rc.bottom;

            HDC dc = CreateCompatibleDC(dcTela);
            HBITMAP bmp = CreateCompatibleBitmap(dcTela, W, H);
            HGDIOBJ oldBmp = SelectObject(dc, bmp);

            HBRUSH br = CreateSolidBrush(CorBarra());
            FillRect(dc, &rc, br);
            DeleteObject(br);

            // Linha azul de destaque na base (marca visual do app)
            RECT rlinha{ 0, H - 2, W, H };
            HBRUSH brAzul = CreateSolidBrush(CorDestaque());
            FillRect(dc, &rlinha, brAzul);
            DeleteObject(brAzul);

            SetBkMode(dc, TRANSPARENT);

            // Logo / nome do app
            SelectObject(dc, g_hFontTitulo);
            SetTextColor(dc, CorDestaque());
            const wchar_t* logo = L"💻 SystemInfoPro";
            TextOutW(dc, 12, (H - 22) / 2 - 2, logo, (int)wcslen(logo));

            SIZE szLogo{};
            GetTextExtentPoint32W(dc, logo, (int)wcslen(logo), &szLogo);

            // Itens de menu
            SelectObject(dc, g_hFontBarra);
            int x = 12 + szLogo.cx + 28;
            for (size_t i = 0; i < g_itensBarra.size(); i++)
            {
                SIZE sz{};
                GetTextExtentPoint32W(dc, g_itensBarra[i].rotulo.c_str(),
                                      (int)g_itensBarra[i].rotulo.size(), &sz);
                RECT r{ x - 10, 4, x + sz.cx + 10, H - 6 };
                g_itensBarra[i].rc = r;

                if ((int)i == g_hotBarra)
                {
                    HBRUSH hot = CreateSolidBrush(CorBarraHot());
                    FillRect(dc, &r, hot);
                    DeleteObject(hot);
                }
                SetTextColor(dc, CorTexto());
                TextOutW(dc, x, (H - sz.cy) / 2 - 1,
                         g_itensBarra[i].rotulo.c_str(), (int)g_itensBarra[i].rotulo.size());
                x += sz.cx + 30;
            }

            BitBlt(dcTela, 0, 0, W, H, dc, 0, 0, SRCCOPY);
            SelectObject(dc, oldBmp);
            DeleteObject(bmp);
            DeleteDC(dc);
            EndPaint(h, &ps);
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int novo = -1;
            for (size_t i = 0; i < g_itensBarra.size(); i++)
                if (PtInRect(&g_itensBarra[i].rc, pt)) { novo = (int)i; break; }
            if (novo != g_hotBarra)
            {
                g_hotBarra = novo;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_hotBarra != -1) { g_hotBarra = -1; InvalidateRect(h, nullptr, FALSE); }
            return 0;

        case WM_LBUTTONDOWN:
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            for (size_t i = 0; i < g_itensBarra.size(); i++)
                if (PtInRect(&g_itensBarra[i].rc, pt)) { AbrirMenuBarra((int)i); return 0; }
            return 0;
        }
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    HWND CriarBarra(HWND pai)
    {
        static bool reg = false;
        if (!reg)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = BarraProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = L"SIP_Barra";
            RegisterClassW(&wc);
            reg = true;
        }
        return CreateWindowExW(0, L"SIP_Barra", L"", WS_CHILD | WS_VISIBLE,
                               0, 0, 100, ALTURA_BARRA, pai, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
}

// ================= Layout =================
static void Redimensionar()
{
    RECT rc; GetClientRect(g_hMain, &rc);
    SendMessageW(g_hStatus, WM_SIZE, 0, 0);
    RECT rcSt; GetWindowRect(g_hStatus, &rcSt);
    int hStatus = rcSt.bottom - rcSt.top;

    int W = rc.right, H = rc.bottom;
    int larguraArvore = max(290, W / 4);
    int hUtil = H - hStatus;

    MoveWindow(g_hBarra, 0, 0, W, ALTURA_BARRA, TRUE);
    MoveWindow(g_hTree, 0, ALTURA_BARRA, larguraArvore, hUtil - ALTURA_BARRA, TRUE);

    int xDir = larguraArvore + 3;
    int wDir = W - xDir;

    // Faixa de titulo: texto a esquerda, busca a direita
    int wBusca = min(340, wDir / 2);
    MoveWindow(g_hTitulo, xDir + 12, ALTURA_BARRA + 8, wDir - wBusca - 30, ALTURA_TITULO - 14, TRUE);
    MoveWindow(g_hBusca, xDir + wDir - wBusca - 8, ALTURA_BARRA + 7, wBusca, 26, TRUE);

    int topoConteudo = ALTURA_BARRA + ALTURA_TITULO;
    MoveWindow(g_hList, xDir, topoConteudo, wDir, hUtil - topoConteudo, TRUE);
    MoveWindow(g_hGraf, xDir, topoConteudo, wDir, hUtil - topoConteudo, TRUE);
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
        g_hFontBusca = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_hFontTitulo = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_hFontBarra = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

        g_hBarra = CriarBarra(h);

        g_hTree = CreateWindowExW(0, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_TRACKSELECT,
            0, 0, 100, 100, h, (HMENU)(INT_PTR)ID_TREE, nullptr, nullptr);
        TreeView_SetItemHeight(g_hTree, 26);
        TreeView_SetIndent(g_hTree, 14);

        g_hTitulo = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0, 0, 100, 28, h, (HMENU)(INT_PTR)ID_TITULO, nullptr, nullptr);

        g_hBusca = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 100, 26, h, (HMENU)(INT_PTR)ID_BUSCA, nullptr, nullptr);
        SendMessageW(g_hBusca, EM_SETCUEBANNER, TRUE, (LPARAM)L"🔎 Pesquisar nesta categoria...");

        g_hList = CreateWindowExW(0, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
            0, 0, 100, 100, h, (HMENU)(INT_PTR)ID_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 360; col.pszText = const_cast<LPWSTR>(L"Propriedade");
        ListView_InsertColumn(g_hList, 0, &col);
        col.cx = 500; col.pszText = const_cast<LPWSTR>(L"Valor");
        ListView_InsertColumn(g_hList, 1, &col);

        g_hGraf = CriarJanelaGraficos(h);

        g_hStatus = CreateWindowW(STATUSCLASSNAMEW, L"",
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, h, (HMENU)(INT_PTR)ID_STATUS, nullptr, nullptr);
        int partes[2] = { 320, -1 };
        SendMessageW(g_hStatus, SB_SETPARTS, 2, (LPARAM)partes);
        SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)L"  SystemInfoPro - pronto");

        HWND ctls[] = { g_hTree, g_hList, g_hStatus };
        for (HWND c : ctls) SendMessageW(c, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hBusca, WM_SETFONT, (WPARAM)g_hFontBusca, TRUE);
        SendMessageW(g_hTitulo, WM_SETFONT, (WPARAM)g_hFontTitulo, TRUE);

        MontarArvore();
        AplicarTema();
        TrayCriar();
        SetTimer(h, TIMER_TRAY, 5000, nullptr);
        // Abre no Resumo do Sistema (seleciona o primeiro item da arvore)
        {
            HTREEITEM raiz = TreeView_GetRoot(g_hTree);
            if (raiz) TreeView_SelectItem(g_hTree, raiz);
        }
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
            SetBkColor(dc, g_escuro ? RGB(45, 45, 52) : RGB(255, 255, 255));
            return (LRESULT)g_brEdit;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == g_hTitulo)
        {
            HDC dc = (HDC)wp;
            SetTextColor(dc, CorTituloTxt());
            SetBkColor(dc, CorJanela());
            return (LRESULT)g_brTitulo;
        }
        if ((HWND)lp == g_hBusca)   // edit desabilitado tambem passa por aqui
        {
            HDC dc = (HDC)wp;
            SetTextColor(dc, CorTexto());
            SetBkColor(dc, g_escuro ? RGB(45, 45, 52) : RGB(255, 255, 255));
            return (LRESULT)g_brEdit;
        }
        break;

    case WM_NOTIFY:
    {
        LPNMHDR nm = (LPNMHDR)lp;
        if (nm->hwndFrom == g_hTree && nm->code == TVN_SELCHANGEDW)
        {
            LPNMTREEVIEWW tv = (LPNMTREEVIEWW)lp;
            PageId p = (PageId)tv->itemNew.lParam;
            if (p != PAGE_NONE) { CarregarPagina(p); Redimensionar(); }
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
        switch (LOWORD(wp))
        {
        case IDM_EXPORT_TXT:  ExportarRelatorio(false); break;
        case IDM_EXPORT_HTML: ExportarRelatorio(true); break;
        case IDM_EXPORT_FULL: ExportarRelatorioCompleto(); break;
        case IDM_SAIR:        DestroyWindow(h); break;
        case IDM_TEMA:        g_escuro = !g_escuro; AplicarTema(); break;
        case IDM_INICIAR_WIN:
        {
            bool novo = !AppIniciaComWindows();
            AppDefinirIniciarComWindows(novo);
            MessageBoxW(h, novo ?
                L"SystemInfoPro agora inicia junto com o Windows\n(minimizado na bandeja)." :
                L"SystemInfoPro nao inicia mais automaticamente com o Windows.",
                L"Iniciar com o Windows", MB_ICONINFORMATION);
            break;
        }
        case IDM_ATUALIZAR:   if (g_paginaAtual != PAGE_NONE) CarregarPagina(g_paginaAtual); break;
        case IDM_BENCH:
            CarregarPagina(PAGE_BENCH);
            BenchmarkIniciar(h);
            CarregarPagina(PAGE_BENCH);   // mostra "executando..."
            break;
        case IDM_SNAP_CRIAR:  SnapshotCriar(h); break;
        case IDM_SNAP_COMP:
            CarregarPagina(PAGE_SNAP);
            SnapshotComparar(h);
            AjustarColunas();
            break;
        case IDM_DISK:
            DiskAnalyzerEscanear(h);
            CarregarPagina(PAGE_DISK);
            break;
        case IDM_NETDIAG:
            CarregarPagina(PAGE_NETDIAG);
            NetDiagIniciar(h);
            CarregarPagina(PAGE_NETDIAG);
            break;
        case IDM_LOG_INICIAR:
            SensorLogIniciar(h);
            if (g_paginaAtual == PAGE_SENSORLOG) CarregarPagina(PAGE_SENSORLOG);
            break;
        case IDM_LOG_PARAR:
            SensorLogParar();
            if (g_paginaAtual == PAGE_SENSORLOG) CarregarPagina(PAGE_SENSORLOG);
            break;
        case IDM_TRAY_RESTAURAR:
            ShowWindow(h, SW_SHOW);
            ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
            break;
        case IDM_TRAY_SAIR:   DestroyWindow(h); break;
        case IDM_SOBRE:
            MessageBoxW(h,
                L"SystemInfoPro 2.0\n\n"
                L"Painel completo de informacoes do sistema em C++ nativo (Win32).\n\n"
                L"Recursos: sensores em tempo real, graficos de desempenho,\n"
                L"SMART/NVMe, largura de banda de GPU e RAM, rede ao vivo,\n"
                L"seguranca, tarefas agendadas e muito mais.\n\n"
                L"Dica: abra o LibreHardwareMonitor junto para ver\n"
                L"temperaturas por nucleo da CPU, tensoes e coolers.",
                L"Sobre o SystemInfoPro", MB_ICONINFORMATION);
            break;
        case ID_BUSCA:
            if (HIWORD(wp) == EN_CHANGE && !g_ignorarBusca) RefiltrarLista();
            break;
        }
        return 0;

    case WM_TIMER:
        if (wp == TIMER_REALTIME)
        {
            if (g_paginaAtual == PAGE_REALTIME) LoadRealtime();
            else if (g_paginaAtual == PAGE_GRAPHS) GraficosTick(g_hGraf);
        }
        else if (wp == TIMER_TRAY)
        {
            TrayTick();
            SensorLogTick();
            if (g_paginaAtual == PAGE_SENSORLOG) CarregarPagina(PAGE_SENSORLOG);
        }
        return 0;

    case WM_TRAYICON:
        if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK)
        {
            ShowWindow(h, SW_SHOW);
            ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
        }
        else if (lp == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, IDM_TRAY_RESTAURAR, L"🖥  Restaurar SystemInfoPro");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_TRAY_SAIR, L"❌  Sair");
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(h);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr);
            DestroyMenu(menu);
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_MINIMIZE)
        {
            ShowWindow(h, SW_HIDE);
            if (!g_avisoTrayMostrado)
            {
                g_avisoTrayMostrado = true;
                TrayBalao(L"SystemInfoPro continua ativo",
                          L"Monitorando na bandeja. Clique no icone para restaurar.", NIIF_INFO);
            }
            return 0;
        }
        break;

    case WM_APP_BENCH_ETAPA:
    {
        std::wstring txt = L"  ⏳ Benchmark: etapa " + NumW((int)wp) + L" de 6 - " +
                           std::wstring(BenchmarkNomeEtapa((int)wp));
        SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)txt.c_str());
        if (g_paginaAtual == PAGE_BENCH) CarregarPagina(PAGE_BENCH);
        return 0;
    }
    case WM_APP_BENCH_FIM:
        SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)L"  SystemInfoPro - pronto");
        if (g_paginaAtual == PAGE_BENCH) CarregarPagina(PAGE_BENCH);
        TrayBalao(L"✅ Benchmark concluido",
                  L"Os resultados estao na categoria 'Benchmark do Sistema'.", NIIF_INFO);
        return 0;

    case WM_APP_NETDIAG_FIM:
        if (g_paginaAtual == PAGE_NETDIAG) CarregarPagina(PAGE_NETDIAG);
        return 0;

    case WM_APP_DISK_FIM:
        if (g_paginaAtual == PAGE_DISK) CarregarPagina(PAGE_DISK);
        TrayBalao(L"✅ Analise de disco concluida",
                  L"Os resultados estao na categoria 'Analisador de Disco'.", NIIF_INFO);
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 920;
        mmi->ptMinTrackSize.y = 560;
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, TIMER_REALTIME);
        KillTimer(h, TIMER_TRAY);
        SensorLogParar();
        BenchmarkShutdown();     // espera threads de trabalho terminarem
        NetDiagShutdown();
        DiskAnalyzerShutdown();
        TrayRemover();
        RealtimeShutdown();
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBusca) DeleteObject(g_hFontBusca);
        if (g_hFontTitulo) DeleteObject(g_hFontTitulo);
        if (g_hFontBarra) DeleteObject(g_hFontBarra);
        if (g_brEdit) DeleteObject(g_brEdit);
        if (g_brTitulo) DeleteObject(g_brTitulo);
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
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"SystemInfoProJanela";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
        L"SystemInfoPro - Painel de Informacoes do Sistema",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    // Atalho F5 = atualizar categoria atual
    ACCEL acc[] = { { FVIRTKEY, VK_F5, IDM_ATUALIZAR } };
    HACCEL hAccel = CreateAcceleratorTableW(acc, 1);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (!TranslateAcceleratorW(hwnd, hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyAcceleratorTable(hAccel);
    CoUninitialize();
    return (int)msg.wParam;
}
