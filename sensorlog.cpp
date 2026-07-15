// ============================================================
//  SystemInfoPro - sensorlog.cpp
//  Registro historico de sensores em CSV: grava CPU, RAM, GPU,
//  temperatura e rede ao longo do tempo para analise posterior
//  (travamentos, superaquecimento em jogos, etc.)
// ============================================================
#include "common.h"
#include <commdlg.h>
#include <cstdio>

namespace
{
    HANDLE g_arquivo = INVALID_HANDLE_VALUE;
    std::wstring g_caminho;
    unsigned long long g_amostras = 0;
    ULONGLONG g_inicio = 0;

    void EscreverLinha(const std::string& s)
    {
        if (g_arquivo == INVALID_HANDLE_VALUE) return;
        DWORD escritos = 0;
        WriteFile(g_arquivo, s.data(), (DWORD)s.size(), &escritos, nullptr);
    }
}

bool SensorLogAtivo() { return g_arquivo != INVALID_HANDLE_VALUE; }

void SensorLogIniciar(HWND dono)
{
    if (SensorLogAtivo()) return;

    wchar_t caminho[MAX_PATH];
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(caminho, L"SensorLog_%04d%02d%02d_%02d%02d.csv",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dono;
    ofn.lpstrFilter = L"Planilha CSV (*.csv)\0*.csv\0";
    ofn.lpstrFile = caminho;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.lpstrTitle = L"Salvar Log de Sensores (grava continuamente)";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;

    g_arquivo = CreateFileW(caminho, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_arquivo == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(dono, L"Nao foi possivel criar o arquivo de log.", L"Erro", MB_ICONERROR);
        return;
    }

    g_caminho = caminho;
    g_amostras = 0;
    g_inicio = GetTickCount64();

    // Cabecalho CSV (com BOM para acentos no Excel)
    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD esc = 0;
    WriteFile(g_arquivo, bom, 3, &esc, nullptr);
    EscreverLinha("Tempo(s);Data/Hora;CPU(%);RAM(%);GPU(%);GPU_Temp(C);Download(Mbps);Upload(Mbps)\r\n");

    MessageBoxW(dono,
        L"Gravacao iniciada!\n\nO log registra os sensores a cada 5 segundos.\n"
        L"Deixe o programa aberto (pode minimizar para a bandeja) e\n"
        L"pare a gravacao quando quiser em 'Ferramentas'.\n\n"
        L"Abra o CSV no Excel depois para ver graficos.",
        L"Log de Sensores", MB_ICONINFORMATION);
}

void SensorLogParar()
{
    if (!SensorLogAtivo()) return;
    CloseHandle(g_arquivo);
    g_arquivo = INVALID_HANDLE_VALUE;
}

// Chamado pelo timer da bandeja (a cada 5s)
void SensorLogTick()
{
    if (!SensorLogAtivo()) return;

    Metricas m;
    AmostrarMetricas(m);

    unsigned int gpuTemp = 0;
    NvmlInfo nv;
    if (NvmlConsultar(nv)) gpuTemp = nv.temperaturaC;

    double segundos = (GetTickCount64() - g_inicio) / 1000.0;
    SYSTEMTIME st; GetLocalTime(&st);

    char linha[256];
    auto v = [](double x) { return x < 0 ? 0.0 : x; };
    sprintf_s(linha,
        "%.0f;%02d/%02d/%04d %02d:%02d:%02d;%.0f;%d;%.0f;%u;%.2f;%.2f\r\n",
        segundos,
        st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond,
        v(m.cpuPct), (m.ramPct < 0 ? 0 : m.ramPct), v(m.gpuPct), gpuTemp,
        v(m.downBps) / 1e6, v(m.upBps) / 1e6);

    EscreverLinha(linha);
    FlushFileBuffers(g_arquivo);   // garante que nao perde dados se travar
    g_amostras++;
}

void LoadSensorLog()
{
    AddSection(L"REGISTRO HISTORICO DE SENSORES (LOG CSV)");

    if (SensorLogAtivo())
    {
        double segundos = (GetTickCount64() - g_inicio) / 1000.0;
        AddRow(L"Status", L"🔴 GRAVANDO...");
        AddRow(L"Arquivo", g_caminho);
        AddRow(L"Amostras Registradas", NumW(g_amostras));
        AddRow(L"Tempo de Gravacao", NumW(segundos / 60.0, 1) + L" minutos");
        AddBlank();
        AddRow(L"Para Parar", L"Menu 'Ferramentas' → '⏹ Parar Log de Sensores'");
        AddRow(L"Dica", L"Pode minimizar para a bandeja - a gravacao continua.");
        return;
    }

    AddRow(L"Como Usar", L"Menu 'Ferramentas' → '⏺ Iniciar Log de Sensores'");
    AddBlank();
    AddRow(L"O que grava", L"CPU, RAM, GPU, temperatura da GPU e velocidade de rede, a cada 5 segundos.");
    AddRow(L"Formato", L"CSV (abre no Excel/LibreOffice para gerar graficos).");
    AddRow(L"Util para", L"Diagnosticar travamentos, superaquecimento em jogos e picos de uso.");
    AddRow(L"Dica", L"Para temperaturas da CPU no log, deixe o LibreHardwareMonitor aberto (via GPU por enquanto).");
}
