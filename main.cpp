// ============================================================
//  SystemInfoPro - Painel de Informacoes do Sistema (C++ / Win32)
//  main.cpp - Janela principal, TreeView, ListView, exportacao
// ============================================================
#include "common.h"
#include <commdlg.h>
#include <objbase.h>
#include <fstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")

// Ativa o estilo visual moderno dos controles (Common Controls v6)
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------- Identificadores de pagina ----------------
enum PageId
{
    PAGE_NONE = 0,
    // Sensores
    PAGE_REALTIME, PAGE_TEMPS,
    // Hardware
    PAGE_CPU, PAGE_BOARD, PAGE_GPU, PAGE_MONITORS, PAGE_RAM,
    PAGE_STORAGE, PAGE_USB, PAGE_AUDIO, PAGE_PRINTERS, PAGE_BATTERY,
    // Software
    PAGE_OS, PAGE_PROGRAMS, PAGE_STARTUP, PAGE_PROCESSES, PAGE_USERS, PAGE_SERVICES,
    // Rede
    PAGE_NETWORK, PAGE_TCP
};

// ---------------- Globais ----------------
HWND g_hList  = nullptr;
static HWND g_hMain  = nullptr;
static HWND g_hTree  = nullptr;
static HWND g_hBtn   = nullptr;
static HWND g_hStatus = nullptr;
static PageId g_paginaAtual = PAGE_NONE;
static std::map<std::wstring, int> g_chavesTempoReal;   // chave -> indice da linha
static const UINT TIMER_REALTIME = 1;

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

void AddRow(const std::wstring& prop, const std::wstring& val) { ListInsert(prop, val); }
void AddSection(const std::wstring& title) { ListInsert(L"--- " + title + L" ---", L""); }
void AddBlank() { ListInsert(L"", L""); }

void ClearList()
{
    ListView_DeleteAllItems(g_hList);
    g_chavesTempoReal.clear();
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

// ================= Ajuste de colunas =================
static void AjustarColunas()
{
    RECT rc; GetClientRect(g_hList, &rc);
    int w = rc.right - rc.left;
    int wProp = 360;
    ListView_SetColumnWidth(g_hList, 0, wProp);
    if (w > wProp + 20) ListView_SetColumnWidth(g_hList, 1, w - wProp - 4);
}

// ================= Exportar relatorio =================
static void ExportarRelatorio()
{
    int total = ListView_GetItemCount(g_hList);
    if (total == 0)
    {
        MessageBoxW(g_hMain, L"Selecione uma categoria antes de exportar.", L"Exportar", MB_ICONINFORMATION);
        return;
    }

    wchar_t caminho[MAX_PATH] = L"Relatorio_Sistema.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMain;
    ofn.lpstrFilter = L"Arquivo de Texto (*.txt)\0*.txt\0";
    ofn.lpstrFile = caminho;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.lpstrTitle = L"Salvar Relatorio do Sistema";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;

    HANDLE h = CreateFileW(caminho, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(g_hMain, L"Nao foi possivel criar o arquivo.", L"Erro", MB_ICONERROR);
        return;
    }

    auto EscreverUtf8 = [&](const std::wstring& w)
    {
        if (w.empty()) return;
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::vector<char> buf(n);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), buf.data(), n, nullptr, nullptr);
        DWORD escritos = 0;
        WriteFile(h, buf.data(), (DWORD)buf.size(), &escritos, nullptr);
    };

    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD escritos = 0;
    WriteFile(h, bom, 3, &escritos, nullptr);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t data[64];
    swprintf_s(data, L"%02d/%02d/%04d %02d:%02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);

    EscreverUtf8(L"=== RELATORIO DO SISTEMA - SystemInfoPro ===\r\n");
    EscreverUtf8(std::wstring(L"Data: ") + data + L"\r\n");
    EscreverUtf8(L"============================================\r\n\r\n");

    wchar_t c0[512], c1[1024];
    for (int i = 0; i < total; i++)
    {
        c0[0] = 0; c1[0] = 0;
        ListView_GetItemText(g_hList, i, 0, c0, 512);
        ListView_GetItemText(g_hList, i, 1, c1, 1024);
        std::wstring prop = c0, val = c1;
        std::wstring linha;
        if (prop.empty() && val.empty()) linha = L"\r\n";
        else
        {
            if (prop.size() < 48) prop.resize(48, L' ');
            linha = prop + L": " + val + L"\r\n";
        }
        EscreverUtf8(linha);
    }
    CloseHandle(h);
    MessageBoxW(g_hMain, L"Relatorio salvo com sucesso!", L"Sucesso", MB_ICONINFORMATION);
}

// ================= Carregamento de paginas =================
static void CarregarPagina(PageId p)
{
    KillTimer(g_hMain, TIMER_REALTIME);
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    SendMessageW(g_hList, WM_SETREDRAW, FALSE, 0);
    ClearList();
    g_paginaAtual = p;

    switch (p)
    {
    case PAGE_REALTIME: RealtimeReset(); LoadRealtime(); SetTimer(g_hMain, TIMER_REALTIME, 1000, nullptr); break;
    case PAGE_TEMPS:    LoadTemperatures(); break;
    case PAGE_CPU:      LoadCPU(); break;
    case PAGE_BOARD:    LoadBoard(); break;
    case PAGE_GPU:      LoadGPU(); break;
    case PAGE_MONITORS: LoadMonitors(); break;
    case PAGE_RAM:      LoadRAM(); break;
    case PAGE_STORAGE:  LoadStorage(); break;
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
    case PAGE_NETWORK:  LoadNetwork(); break;
    case PAGE_TCP:      LoadTcpConnections(); break;
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
static HTREEITEM TreeAdd(HTREEITEM pai, const wchar_t* texto, PageId pagina)
{
    TVINSERTSTRUCTW tvi{};
    tvi.hParent = pai;
    tvi.hInsertAfter = TVI_LAST;
    tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvi.item.pszText = const_cast<LPWSTR>(texto);
    tvi.item.lParam = (LPARAM)pagina;
    return TreeView_InsertItem(g_hTree, &tvi);
}

static void MontarArvore()
{
    HTREEITEM sensores = TreeAdd(TVI_ROOT, L"Sensores em Tempo Real", PAGE_NONE);
    TreeAdd(sensores, L"Painel de Monitoramento (CPU, RAM, GPU, Rede)", PAGE_REALTIME);
    TreeAdd(sensores, L"Sensores Termicos (Temperaturas)", PAGE_TEMPS);

    HTREEITEM hw = TreeAdd(TVI_ROOT, L"Hardware", PAGE_NONE);
    TreeAdd(hw, L"Processador (CPU)", PAGE_CPU);
    TreeAdd(hw, L"Placa-Mae e BIOS", PAGE_BOARD);
    TreeAdd(hw, L"Placa de Video (GPU)", PAGE_GPU);
    TreeAdd(hw, L"Telas e Monitores", PAGE_MONITORS);
    TreeAdd(hw, L"Memoria RAM", PAGE_RAM);
    TreeAdd(hw, L"Armazenamento e SMART", PAGE_STORAGE);
    TreeAdd(hw, L"Dispositivos USB", PAGE_USB);
    TreeAdd(hw, L"Dispositivos de Audio", PAGE_AUDIO);
    TreeAdd(hw, L"Impressoras e Fax", PAGE_PRINTERS);
    TreeAdd(hw, L"Bateria e Energia", PAGE_BATTERY);

    HTREEITEM sw = TreeAdd(TVI_ROOT, L"Software", PAGE_NONE);
    TreeAdd(sw, L"Sistema Operacional", PAGE_OS);
    TreeAdd(sw, L"Programas Instalados", PAGE_PROGRAMS);
    TreeAdd(sw, L"Programas de Inicializacao", PAGE_STARTUP);
    TreeAdd(sw, L"Processos em Execucao", PAGE_PROCESSES);
    TreeAdd(sw, L"Contas de Usuario", PAGE_USERS);
    TreeAdd(sw, L"Servicos do Sistema", PAGE_SERVICES);

    HTREEITEM rede = TreeAdd(TVI_ROOT, L"Rede", PAGE_NONE);
    TreeAdd(rede, L"Adaptadores de Conexao", PAGE_NETWORK);
    TreeAdd(rede, L"Conexoes TCP Ativas", PAGE_TCP);

    // Expande tudo
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

    int larguraArvore = max(260, (rc.right - rc.left) / 4);
    int hBtn = 40;
    int hUtil = rc.bottom - hStatus;

    MoveWindow(g_hTree, 0, 0, larguraArvore, hUtil - hBtn, TRUE);
    MoveWindow(g_hBtn,  0, hUtil - hBtn, larguraArvore, hBtn, TRUE);
    MoveWindow(g_hList, larguraArvore + 3, 0, rc.right - larguraArvore - 3, hUtil, TRUE);
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

        g_hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 100, 100, h, (HMENU)(INT_PTR)101, nullptr, nullptr);

        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
            0, 0, 100, 100, h, (HMENU)(INT_PTR)102, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 360; col.pszText = const_cast<LPWSTR>(L"Propriedade");
        ListView_InsertColumn(g_hList, 0, &col);
        col.cx = 500; col.pszText = const_cast<LPWSTR>(L"Valor");
        ListView_InsertColumn(g_hList, 1, &col);

        g_hBtn = CreateWindowW(L"BUTTON", L"Exportar Relatorio (.txt)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 100, 40, h, (HMENU)(INT_PTR)103, nullptr, nullptr);

        g_hStatus = CreateWindowW(STATUSCLASSNAMEW, L"",
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, h, (HMENU)(INT_PTR)104, nullptr, nullptr);
        int partes[2] = { 300, -1 };
        SendMessageW(g_hStatus, SB_SETPARTS, 2, (LPARAM)partes);
        SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)L"  SystemInfoPro - pronto");

        MontarArvore();
        return 0;
    }
    case WM_SIZE:
        Redimensionar();
        return 0;

    case WM_NOTIFY:
    {
        LPNMHDR nm = (LPNMHDR)lp;
        if (nm->hwndFrom == g_hTree && nm->code == TVN_SELCHANGEDW)
        {
            LPNMTREEVIEWW tv = (LPNMTREEVIEWW)lp;
            PageId p = (PageId)tv->itemNew.lParam;
            if (p != PAGE_NONE) CarregarPagina(p);
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 103) ExportarRelatorio();
        return 0;

    case WM_TIMER:
        if (wp == TIMER_REALTIME && g_paginaAtual == PAGE_REALTIME) LoadRealtime();
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 850;
        mmi->ptMinTrackSize.y = 550;
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, TIMER_REALTIME);
        RealtimeShutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ================= Ponto de entrada =================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmd)
{
    // COM para WMI
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
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SystemInfoProJanela";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
        L"SystemInfoPro - Painel de Informacoes do Sistema",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1200, 720,
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
