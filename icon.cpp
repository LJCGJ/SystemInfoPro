// ============================================================
//  SystemInfoPro - icon.cpp
//  Gera o icone do aplicativo em tempo real com GDI (monitor com
//  grafico ascendente), sem precisar de um arquivo .ico externo.
//  Usado na janela, barra de tarefas e bandeja.
//  Tambem escreve um arquivo app.ico (multi-tamanho) para uso no
//  recurso do .exe e nos atalhos do instalador (Inno Setup).
// ============================================================
#include "common.h"
#include <vector>

// Desenha o icone no tamanho pedido (ex.: 32 ou 16) e devolve um HICON.
// O chamador deve liberar com DestroyIcon quando nao precisar mais.
HICON CriarIconeApp(int tam)
{
    if (tam < 8) tam = 8;
    HDC dcTela = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(dcTela);

    // Bitmap de cor 24 bits (sem canal alfa -> usa a mascara)
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = tam;
    bi.bmiHeader.biHeight = -tam;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmpColor = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, bmpColor);

    SetBkMode(dc, TRANSPARENT);
    double s = tam / 32.0;   // fator de escala (desenho pensado em 32x32)
    auto E = [s](double v) { return (int)(v * s + 0.5); };

    // ---- Fundo azul arredondado ----
    RECT full{ 0, 0, tam, tam };
    HBRUSH brFundo = CreateSolidBrush(RGB(20, 22, 28));   // "transparente" (vira mascara)
    FillRect(dc, &full, brFundo);
    DeleteObject(brFundo);

    HBRUSH brAzul = CreateSolidBrush(RGB(0, 122, 204));
    HGDIOBJ oldBr = SelectObject(dc, brAzul);
    HPEN penNula = (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ oldPen = SelectObject(dc, penNula);
    RoundRect(dc, E(1), E(1), E(31), E(31), E(9), E(9));
    SelectObject(dc, oldBr);
    DeleteObject(brAzul);

    // ---- Moldura branca do monitor ----
    HBRUSH brBranco = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(dc, brBranco);
    RoundRect(dc, E(5), E(7), E(27), E(22), E(3), E(3));

    // ---- Tela escura ----
    HBRUSH brTela = CreateSolidBrush(RGB(18, 18, 31));
    SelectObject(dc, brTela);
    RoundRect(dc, E(7), E(9), E(25), E(20), E(2), E(2));
    SelectObject(dc, oldBr);
    DeleteObject(brTela);

    // ---- Linha de grafico ascendente (verde) ----
    int esp = max(2, E(2));
    HPEN penGraf = CreatePen(PS_SOLID, esp, RGB(46, 230, 160));
    SelectObject(dc, penGraf);
    POINT pts[5] = {
        { E(8),  E(18) }, { E(12), E(14) }, { E(16), E(16) },
        { E(20), E(11) }, { E(24), E(10) }
    };
    Polyline(dc, pts, 5);
    SelectObject(dc, oldPen);
    DeleteObject(penGraf);

    // ---- Base do monitor ----
    SelectObject(dc, brBranco);
    SelectObject(dc, penNula);
    Rectangle(dc, E(14), E(22), E(18), E(25));
    RoundRect(dc, E(11), E(25), E(21), E(28), E(2), E(2));
    SelectObject(dc, oldBr);
    DeleteObject(brBranco);

    SelectObject(dc, oldBmp);
    SelectObject(dc, oldPen);

    // ---- Mascara: onde a cor == fundo "transparente", vira transparente ----
    HBITMAP bmpMask = CreateBitmap(tam, tam, 1, 1, nullptr);
    HDC dcM = CreateCompatibleDC(dcTela);
    HGDIOBJ oldM = SelectObject(dcM, bmpMask);
    // Preenche a mascara: redesenha a mesma forma arredondada como "opaca"
    RECT rcm{ 0, 0, tam, tam };
    FillRect(dcM, &rcm, (HBRUSH)GetStockObject(WHITE_BRUSH));   // 1 = transparente
    HBRUSH brPreto = (HBRUSH)GetStockObject(BLACK_BRUSH);       // 0 = opaco
    HGDIOBJ oldMbr = SelectObject(dcM, brPreto);
    HGDIOBJ oldMpen = SelectObject(dcM, GetStockObject(BLACK_PEN));
    RoundRect(dcM, E(1), E(1), E(31), E(31), E(9), E(9));
    SelectObject(dcM, oldMbr);
    SelectObject(dcM, oldMpen);
    SelectObject(dcM, oldM);
    DeleteDC(dcM);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = bmpColor;
    ii.hbmMask = bmpMask;
    HICON icone = CreateIconIndirect(&ii);

    DeleteObject(bmpColor);
    DeleteObject(bmpMask);
    DeleteDC(dc);
    ReleaseDC(nullptr, dcTela);
    return icone;
}

// ============================================================
//  Escrita de arquivo .ico (multi-tamanho) a partir do icone GDI
// ============================================================
namespace
{
    // Extrai os pixels BGRA (top-down) de um HICON no tamanho pedido.
    bool ExtrairBGRA(int tam, std::vector<unsigned char>& bgra)
    {
        HICON ic = CriarIconeApp(tam);
        if (!ic) return false;
        ICONINFO ii{};
        if (!GetIconInfo(ic, &ii)) { DestroyIcon(ic); return false; }

        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = tam;
        bi.bmiHeader.biHeight = -tam;   // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        std::vector<unsigned char> cor(tam * tam * 4), masc(tam * tam * 4);
        GetDIBits(dc, ii.hbmColor, 0, tam, cor.data(), &bi, DIB_RGB_COLORS);
        GetDIBits(dc, ii.hbmMask, 0, tam, masc.data(), &bi, DIB_RGB_COLORS);

        bgra.resize(tam * tam * 4);
        for (int i = 0; i < tam * tam; i++)
        {
            bool transparente = (masc[i * 4] != 0);   // mascara 1 = transparente
            bgra[i * 4 + 0] = transparente ? 0 : cor[i * 4 + 0];
            bgra[i * 4 + 1] = transparente ? 0 : cor[i * 4 + 1];
            bgra[i * 4 + 2] = transparente ? 0 : cor[i * 4 + 2];
            bgra[i * 4 + 3] = transparente ? 0 : 255;
        }

        DeleteDC(dc);
        DeleteObject(ii.hbmColor);
        DeleteObject(ii.hbmMask);
        DestroyIcon(ic);
        return true;
    }

    void Put16(std::vector<unsigned char>& b, unsigned short v)
    { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
    void Put32(std::vector<unsigned char>& b, unsigned int v)
    { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xFF); }
}

// Escreve um arquivo .ico com os tamanhos 16/32/48/256. Retorna true em sucesso.
bool SalvarIconeArquivo(const std::wstring& caminho)
{
    int tamanhos[] = { 16, 32, 48, 256 };
    struct Img { int tam; std::vector<unsigned char> blob; };
    std::vector<Img> imagens;

    for (int t : tamanhos)
    {
        std::vector<unsigned char> bgra;
        if (!ExtrairBGRA(t, bgra)) continue;

        // Monta o "blob" no formato BMP dentro do ICO:
        // BITMAPINFOHEADER (altura = 2*t) + cor (bottom-up) + mascara AND (1bpp)
        std::vector<unsigned char> blob;
        Put32(blob, 40);              // biSize
        Put32(blob, (unsigned)t);     // biWidth
        Put32(blob, (unsigned)(t * 2)); // biHeight (cor + mascara)
        Put16(blob, 1);               // biPlanes
        Put16(blob, 32);              // biBitCount
        Put32(blob, 0);               // biCompression
        Put32(blob, 0);               // biSizeImage
        Put32(blob, 0); Put32(blob, 0); Put32(blob, 0); Put32(blob, 0);

        // Cor: bottom-up
        for (int y = t - 1; y >= 0; y--)
            for (int x = 0; x < t; x++)
            {
                int i = (y * t + x) * 4;
                blob.push_back(bgra[i + 0]);
                blob.push_back(bgra[i + 1]);
                blob.push_back(bgra[i + 2]);
                blob.push_back(bgra[i + 3]);
            }

        // Mascara AND (1 bpp, bottom-up, linhas alinhadas a 4 bytes)
        int strideBits = ((t + 31) / 32) * 32;
        int strideBytes = strideBits / 8;
        for (int y = t - 1; y >= 0; y--)
        {
            std::vector<unsigned char> linha(strideBytes, 0);
            for (int x = 0; x < t; x++)
            {
                int i = (y * t + x) * 4;
                if (bgra[i + 3] == 0)   // transparente -> bit 1
                    linha[x / 8] |= (0x80 >> (x % 8));
            }
            blob.insert(blob.end(), linha.begin(), linha.end());
        }

        imagens.push_back({ t, std::move(blob) });
    }

    if (imagens.empty()) return false;

    // Cabecalho ICONDIR + entradas + blobs
    std::vector<unsigned char> arquivo;
    Put16(arquivo, 0);                       // reservado
    Put16(arquivo, 1);                       // tipo = icone
    Put16(arquivo, (unsigned short)imagens.size());

    unsigned int offset = 6 + 16 * (unsigned)imagens.size();
    for (const auto& im : imagens)
    {
        arquivo.push_back(im.tam >= 256 ? 0 : (unsigned char)im.tam);  // largura (0 = 256)
        arquivo.push_back(im.tam >= 256 ? 0 : (unsigned char)im.tam);  // altura
        arquivo.push_back(0);                // cores da paleta
        arquivo.push_back(0);                // reservado
        Put16(arquivo, 1);                   // planos
        Put16(arquivo, 32);                  // bits por pixel
        Put32(arquivo, (unsigned)im.blob.size());
        Put32(arquivo, offset);
        offset += (unsigned)im.blob.size();
    }
    for (const auto& im : imagens)
        arquivo.insert(arquivo.end(), im.blob.begin(), im.blob.end());

    HANDLE h = CreateFileW(caminho.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD escritos = 0;
    BOOL ok = WriteFile(h, arquivo.data(), (DWORD)arquivo.size(), &escritos, nullptr);
    CloseHandle(h);
    return ok && escritos == arquivo.size();
}
