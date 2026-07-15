// ============================================================
//  SystemInfoPro - graphs.cpp
//  Graficos de desempenho em tempo real (GDI com double-buffer)
//  Curvas: CPU, GPU, RAM e Rede - historico de 120 segundos
// ============================================================
#include "common.h"
#include <deque>

namespace
{
    const int MAX_AMOSTRAS = 120;

    std::deque<double> g_cpu, g_gpu, g_ram, g_down, g_up;
    bool g_escuro = false;

    struct Cores
    {
        COLORREF fundo, painel, grade, texto, tituloTxt;
    };
    Cores CoresAtuais()
    {
        if (g_escuro)
            return { RGB(23,23,26), RGB(30,30,34), RGB(55,55,62), RGB(225,225,225), RGB(160,160,168) };
        return { RGB(245,246,248), RGB(255,255,255), RGB(225,228,232), RGB(30,30,30), RGB(110,115,122) };
    }

    const COLORREF COR_CPU  = RGB(0, 120, 215);
    const COLORREF COR_GPU  = RGB(16, 160, 90);
    const COLORREF COR_RAM  = RGB(170, 80, 200);
    const COLORREF COR_DOWN = RGB(0, 165, 80);
    const COLORREF COR_UP   = RGB(235, 140, 0);

    void Empurrar(std::deque<double>& d, double v)
    {
        d.push_back(v);
        while ((int)d.size() > MAX_AMOSTRAS) d.pop_front();
    }

    // Desenha uma serie dentro do retangulo (valores 0..escala)
    void DesenharSerie(HDC dc, const RECT& rc, const std::deque<double>& dados,
                       double escala, COLORREF cor, int espessura = 2)
    {
        if (dados.size() < 2 || escala <= 0) return;

        HPEN pen = CreatePen(PS_SOLID, espessura, cor);
        HGDIOBJ old = SelectObject(dc, pen);

        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        int n = (int)dados.size();

        for (int i = 0; i < n; i++)
        {
            double v = dados[i];
            if (v < 0) v = 0;
            if (v > escala) v = escala;
            int x = rc.left + (int)((double)i * w / (MAX_AMOSTRAS - 1));
            int y = rc.bottom - (int)(v / escala * (h - 2)) - 1;
            if (i == 0) MoveToEx(dc, x, y, nullptr);
            else        LineTo(dc, x, y);
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }

    void DesenharPainel(HDC dc, RECT rc, const wchar_t* titulo, const std::wstring& valorAtual,
                        COLORREF corTitulo, HFONT fTitulo, HFONT fValor)
    {
        Cores c = CoresAtuais();

        HBRUSH br = CreateSolidBrush(c.painel);
        FillRect(dc, &rc, br);
        DeleteObject(br);

        // Grade horizontal (25/50/75%)
        HPEN penGrade = CreatePen(PS_SOLID, 1, c.grade);
        HGDIOBJ old = SelectObject(dc, penGrade);
        int h = rc.bottom - rc.top;
        for (int i = 1; i <= 3; i++)
        {
            int y = rc.top + h * i / 4;
            MoveToEx(dc, rc.left, y, nullptr);
            LineTo(dc, rc.right, y);
        }
        SelectObject(dc, old);
        DeleteObject(penGrade);

        // Moldura
        HPEN penBorda = CreatePen(PS_SOLID, 1, c.grade);
        old = SelectObject(dc, penBorda);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(dc, old);
        DeleteObject(penBorda);

        // Titulo e valor atual
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, corTitulo);
        SelectObject(dc, fTitulo);
        TextOutW(dc, rc.left + 8, rc.top + 5, titulo, (int)wcslen(titulo));

        SetTextColor(dc, c.texto);
        SelectObject(dc, fValor);
        SIZE sz{};
        GetTextExtentPoint32W(dc, valorAtual.c_str(), (int)valorAtual.size(), &sz);
        TextOutW(dc, rc.right - sz.cx - 10, rc.top + 5, valorAtual.c_str(), (int)valorAtual.size());
    }

    LRESULT CALLBACK GraficosProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;   // evita flicker (pintamos tudo no WM_PAINT)

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dcTela = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            int W = rc.right, H = rc.bottom;
            if (W < 10 || H < 10) { EndPaint(h, &ps); return 0; }

            // Double buffer
            HDC dc = CreateCompatibleDC(dcTela);
            HBITMAP bmp = CreateCompatibleBitmap(dcTela, W, H);
            HGDIOBJ oldBmp = SelectObject(dc, bmp);

            Cores c = CoresAtuais();
            HBRUSH brFundo = CreateSolidBrush(c.fundo);
            RECT rTudo{ 0, 0, W, H };
            FillRect(dc, &rTudo, brFundo);
            DeleteObject(brFundo);

            HFONT fTitulo = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HFONT fValor = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

            const int margem = 10;
            const int espac = 8;
            int alturaPainel = (H - 2 * margem - 3 * espac) / 4;

            auto Ultimo = [](const std::deque<double>& d) -> double
            { return d.empty() ? -1 : d.back(); };

            auto PctTexto = [&](double v) -> std::wstring
            { return v < 0 ? L"..." : NumW(v, 0) + L" %"; };

            // ---- CPU ----
            RECT r1{ margem, margem, W - margem, margem + alturaPainel };
            DesenharPainel(dc, r1, L"Processador (CPU)", PctTexto(Ultimo(g_cpu)), COR_CPU, fTitulo, fValor);
            RECT r1g{ r1.left + 1, r1.top + 28, r1.right - 1, r1.bottom - 1 };
            DesenharSerie(dc, r1g, g_cpu, 100.0, COR_CPU);

            // ---- GPU ----
            RECT r2{ margem, r1.bottom + espac, W - margem, r1.bottom + espac + alturaPainel };
            DesenharPainel(dc, r2, L"Placa de Video (GPU)", PctTexto(Ultimo(g_gpu)), COR_GPU, fTitulo, fValor);
            RECT r2g{ r2.left + 1, r2.top + 28, r2.right - 1, r2.bottom - 1 };
            DesenharSerie(dc, r2g, g_gpu, 100.0, COR_GPU);

            // ---- RAM ----
            RECT r3{ margem, r2.bottom + espac, W - margem, r2.bottom + espac + alturaPainel };
            DesenharPainel(dc, r3, L"Memoria RAM", PctTexto(Ultimo(g_ram)), COR_RAM, fTitulo, fValor);
            RECT r3g{ r3.left + 1, r3.top + 28, r3.right - 1, r3.bottom - 1 };
            DesenharSerie(dc, r3g, g_ram, 100.0, COR_RAM);

            // ---- Rede (escala automatica) ----
            double maxRede = 1024.0 * 1024.0;   // minimo 1 Mbps
            for (double v : g_down) if (v > maxRede) maxRede = v;
            for (double v : g_up)   if (v > maxRede) maxRede = v;
            maxRede *= 1.15;

            std::wstring valRede = L"↓ " + (g_down.empty() || g_down.back() < 0 ? L"..." : FormatSpeedBits(g_down.back()))
                                 + L"   ↑ " + (g_up.empty() || g_up.back() < 0 ? L"..." : FormatSpeedBits(g_up.back()));
            RECT r4{ margem, r3.bottom + espac, W - margem, H - margem };
            DesenharPainel(dc, r4, L"Rede (Download / Upload)", valRede, COR_DOWN, fTitulo, fValor);
            RECT r4g{ r4.left + 1, r4.top + 28, r4.right - 1, r4.bottom - 1 };
            DesenharSerie(dc, r4g, g_down, maxRede, COR_DOWN);
            DesenharSerie(dc, r4g, g_up, maxRede, COR_UP);

            // Legenda pequena da escala de rede
            SetTextColor(dc, c.tituloTxt);
            SelectObject(dc, fValor);
            std::wstring escalaTxt = L"escala: " + FormatSpeedBits(maxRede);
            TextOutW(dc, r4.left + 8, r4.bottom - 22, escalaTxt.c_str(), (int)escalaTxt.size());

            DeleteObject(fTitulo);
            DeleteObject(fValor);

            BitBlt(dcTela, 0, 0, W, H, dc, 0, 0, SRCCOPY);
            SelectObject(dc, oldBmp);
            DeleteObject(bmp);
            DeleteDC(dc);
            EndPaint(h, &ps);
            return 0;
        }
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
}

HWND CriarJanelaGraficos(HWND pai)
{
    static bool registrado = false;
    if (!registrado)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = GraficosProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SIP_Graficos";
        RegisterClassW(&wc);
        registrado = true;
    }
    return CreateWindowExW(0, L"SIP_Graficos", L"", WS_CHILD,
                           0, 0, 100, 100, pai, nullptr, GetModuleHandleW(nullptr), nullptr);
}

void GraficosTick(HWND hGraficos)
{
    Metricas m;
    AmostrarMetricas(m);
    if (m.cpuPct >= 0)  Empurrar(g_cpu, m.cpuPct);
    if (m.gpuPct >= 0)  Empurrar(g_gpu, m.gpuPct);
    if (m.ramPct >= 0)  Empurrar(g_ram, m.ramPct);
    if (m.downBps >= 0) Empurrar(g_down, m.downBps);
    if (m.upBps >= 0)   Empurrar(g_up, m.upBps);
    if (hGraficos) InvalidateRect(hGraficos, nullptr, FALSE);
}

void GraficosDefinirTema(bool escuro) { g_escuro = escuro; }

void GraficosResetar()
{
    g_cpu.clear(); g_gpu.clear(); g_ram.clear(); g_down.clear(); g_up.clear();
}
