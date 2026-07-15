// ============================================================
//  SystemInfoPro - snapshot.cpp
//  Snapshot do sistema: salva um inventario (programas, drivers,
//  inicializacao, servicos, atualizacoes) e compara depois para
//  mostrar exatamente o que mudou na maquina.
// ============================================================
#include "common.h"
#include "wmi.h"
#include <commdlg.h>
#include <set>
#include <fstream>

namespace
{
    typedef std::map<std::wstring, std::set<std::wstring>> Inventario;

    const wchar_t* SECOES[] = { L"Programas", L"Drivers", L"Inicializacao", L"Servicos", L"Atualizacoes" };

    // ---------------- Coletores ----------------
    void ColetarProgramas(std::set<std::wstring>& s)
    {
        auto Varrer = [&s](HKEY raiz, REGSAM visao)
        {
            HKEY base = nullptr;
            if (RegOpenKeyExW(raiz, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                              0, KEY_READ | visao, &base) != ERROR_SUCCESS) return;
            for (DWORD i = 0; ; i++)
            {
                wchar_t sub[256]; DWORD tam = 256;
                if (RegEnumKeyExW(base, i, sub, &tam, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                HKEY prog = nullptr;
                if (RegOpenKeyExW(base, sub, 0, KEY_READ | visao, &prog) != ERROR_SUCCESS) continue;
                wchar_t nome[512] = {}, ver[128] = {};
                DWORD t1 = sizeof(nome), t2 = sizeof(ver), tp = 0;
                RegQueryValueExW(prog, L"DisplayName", nullptr, &tp, (LPBYTE)nome, &t1);
                RegQueryValueExW(prog, L"DisplayVersion", nullptr, &tp, (LPBYTE)ver, &t2);
                if (nome[0])
                    s.insert(std::wstring(nome) + (ver[0] ? L"  (v" + std::wstring(ver) + L")" : L""));
                RegCloseKey(prog);
            }
            RegCloseKey(base);
        };
        Varrer(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
        Varrer(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
        Varrer(HKEY_CURRENT_USER, 0);
    }

    void ColetarDrivers(std::set<std::wstring>& s)
    {
        Wmi wmi;
        wmi.Query(L"SELECT Name, DisplayName FROM Win32_SystemDriver",
        [&s](IWbemClassObject* o)
        {
            std::wstring nome = Wmi::GetStr(o, L"DisplayName");
            if (nome.empty()) nome = Wmi::GetStr(o, L"Name");
            if (!nome.empty()) s.insert(nome);
        });
    }

    void ColetarInicializacao(std::set<std::wstring>& s)
    {
        Wmi wmi;
        wmi.Query(L"SELECT Name, Command FROM Win32_StartupCommand",
        [&s](IWbemClassObject* o)
        {
            std::wstring nome = Wmi::GetStr(o, L"Name");
            std::wstring cmd = Wmi::GetStr(o, L"Command");
            if (!nome.empty()) s.insert(nome + L"  →  " + cmd);
        });
    }

    void ColetarServicos(std::set<std::wstring>& s)
    {
        Wmi wmi;
        wmi.Query(L"SELECT DisplayName, StartMode FROM Win32_Service",
        [&s](IWbemClassObject* o)
        {
            std::wstring nome = Wmi::GetStr(o, L"DisplayName");
            if (!nome.empty())
                s.insert(nome + L"  [inicio: " + Wmi::GetStr(o, L"StartMode") + L"]");
        });
    }

    void ColetarAtualizacoes(std::set<std::wstring>& s)
    {
        Wmi wmi;
        wmi.Query(L"SELECT HotFixID FROM Win32_QuickFixEngineering",
        [&s](IWbemClassObject* o)
        {
            std::wstring kb = Wmi::GetStr(o, L"HotFixID");
            if (!kb.empty()) s.insert(kb);
        });
    }

    void ColetarTudo(Inventario& inv)
    {
        ColetarProgramas(inv[L"Programas"]);
        ColetarDrivers(inv[L"Drivers"]);
        ColetarInicializacao(inv[L"Inicializacao"]);
        ColetarServicos(inv[L"Servicos"]);
        ColetarAtualizacoes(inv[L"Atualizacoes"]);
    }

    // ---------------- Arquivo ----------------
    void EscreverUtf8Arq(HANDLE h, const std::wstring& w)
    {
        if (w.empty()) return;
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::vector<char> buf(n);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), buf.data(), n, nullptr, nullptr);
        DWORD escritos = 0;
        WriteFile(h, buf.data(), (DWORD)buf.size(), &escritos, nullptr);
    }

    bool LerArquivoUtf8(const std::wstring& caminho, std::wstring& saida)
    {
        HANDLE h = CreateFileW(caminho.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD tam = GetFileSize(h, nullptr);
        std::vector<char> bruto(tam + 1, 0);
        DWORD lidos = 0;
        ReadFile(h, bruto.data(), tam, &lidos, nullptr);
        CloseHandle(h);

        const char* p = bruto.data();
        int n = (int)lidos;
        if (n >= 3 && (BYTE)p[0] == 0xEF && (BYTE)p[1] == 0xBB && (BYTE)p[2] == 0xBF) { p += 3; n -= 3; }

        int wlen = MultiByteToWideChar(CP_UTF8, 0, p, n, nullptr, 0);
        saida.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, p, n, &saida[0], wlen);
        return true;
    }

    bool ParsearSnapshot(const std::wstring& conteudo, Inventario& inv, std::wstring& dataSnap)
    {
        std::wstringstream ss(conteudo);
        std::wstring linha, secao;
        bool cabecalhoOk = false;
        while (std::getline(ss, linha))
        {
            linha = Trim(linha);
            if (linha.empty()) continue;
            if (linha.find(L"#SystemInfoProSnapshot") == 0) { cabecalhoOk = true; continue; }
            if (linha.find(L"#Data:") == 0) { dataSnap = Trim(linha.substr(6)); continue; }
            if (linha[0] == L'[' && linha.back() == L']')
            {
                secao = linha.substr(1, linha.size() - 2);
                continue;
            }
            if (!secao.empty()) inv[secao].insert(linha);
        }
        return cabecalhoOk;
    }
}

// ================= Pagina informativa =================
void LoadSnapshotInfo()
{
    AddSection(L"SNAPSHOT E COMPARACAO DO SISTEMA");
    AddRow(L"O que e", L"Uma 'foto' do estado do sistema: programas, drivers, inicializacao, servicos e atualizacoes.");
    AddBlank();
    AddRow(L"1. Criar Snapshot", L"Menu 'Ferramentas' → '📸 Criar Snapshot do Sistema' (salva um arquivo .txt)");
    AddRow(L"2. Comparar Depois", L"Menu 'Ferramentas' → '🔍 Comparar com Snapshot' (mostra o que mudou)");
    AddBlank();
    AddRow(L"Usos Praticos", L"Descobrir o que uma instalacao alterou, detectar programas que se instalaram sozinhos,");
    AddRow(L"", L"auditar maquinas de clientes e documentar mudancas antes/depois de manutencoes.");
}

// ================= Criar snapshot =================
void SnapshotCriar(HWND dono)
{
    wchar_t caminho[MAX_PATH] = L"Snapshot_Sistema.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dono;
    ofn.lpstrFilter = L"Snapshot SystemInfoPro (*.txt)\0*.txt\0";
    ofn.lpstrFile = caminho;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.lpstrTitle = L"Salvar Snapshot do Sistema";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;

    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    Inventario inv;
    ColetarTudo(inv);

    HANDLE h = CreateFileW(caminho, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        MessageBoxW(dono, L"Nao foi possivel criar o arquivo.", L"Erro", MB_ICONERROR);
        return;
    }

    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD esc = 0;
    WriteFile(h, bom, 3, &esc, nullptr);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t data[64];
    swprintf_s(data, L"%02d/%02d/%04d %02d:%02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);

    EscreverUtf8Arq(h, L"#SystemInfoProSnapshot v1\r\n");
    EscreverUtf8Arq(h, std::wstring(L"#Data: ") + data + L"\r\n");

    int totalItens = 0;
    for (const wchar_t* sec : SECOES)
    {
        EscreverUtf8Arq(h, L"\r\n[" + std::wstring(sec) + L"]\r\n");
        for (const auto& item : inv[sec])
        {
            EscreverUtf8Arq(h, item + L"\r\n");
            totalItens++;
        }
    }
    CloseHandle(h);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));

    std::wstring msg = L"Snapshot criado com sucesso!\n\n" + NumW(totalItens) +
                       L" itens registrados.\n\nGuarde o arquivo e use 'Comparar com Snapshot' "
                       L"no futuro para ver o que mudou.";
    MessageBoxW(dono, msg.c_str(), L"Snapshot", MB_ICONINFORMATION);
}

// ================= Comparar =================
void SnapshotComparar(HWND dono)
{
    wchar_t caminho[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dono;
    ofn.lpstrFilter = L"Snapshot SystemInfoPro (*.txt)\0*.txt\0";
    ofn.lpstrFile = caminho;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Abrir Snapshot para Comparar";
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring conteudo;
    if (!LerArquivoUtf8(caminho, conteudo))
    {
        MessageBoxW(dono, L"Nao foi possivel ler o arquivo.", L"Erro", MB_ICONERROR);
        return;
    }

    Inventario antigo;
    std::wstring dataSnap;
    if (!ParsearSnapshot(conteudo, antigo, dataSnap))
    {
        MessageBoxW(dono, L"Este arquivo nao parece ser um snapshot do SystemInfoPro.", L"Erro", MB_ICONWARNING);
        return;
    }

    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    Inventario atual;
    ColetarTudo(atual);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));

    // ---- Renderiza o diff na lista ----
    ClearList();
    AddSection(L"COMPARACAO DE SNAPSHOT");
    AddRow(L"Snapshot de Referencia", dataSnap.empty() ? L"(sem data)" : dataSnap);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t agora[64];
    swprintf_s(agora, L"%02d/%02d/%04d %02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
    AddRow(L"Estado Atual de", agora);
    AddBlank();

    int totalMudancas = 0;
    for (const wchar_t* sec : SECOES)
    {
        const auto& sAntigo = antigo[sec];
        const auto& sAtual = atual[sec];

        std::vector<std::wstring> adicionados, removidos;
        for (const auto& item : sAtual)
            if (sAntigo.find(item) == sAntigo.end()) adicionados.push_back(item);
        for (const auto& item : sAntigo)
            if (sAtual.find(item) == sAtual.end()) removidos.push_back(item);

        if (adicionados.empty() && removidos.empty()) continue;

        AddSection(std::wstring(sec) + L"  (" + NumW((int)adicionados.size()) + L" novos, " +
                   NumW((int)removidos.size()) + L" removidos)");
        for (const auto& item : adicionados) { AddRow(L"➕ Novo", item); totalMudancas++; }
        for (const auto& item : removidos)   { AddRow(L"➖ Removido", item); totalMudancas++; }
        AddBlank();
    }

    if (totalMudancas == 0)
    {
        AddRow(L"Resultado", L"✅ Nenhuma mudanca detectada - o sistema esta identico ao snapshot.");
    }
    else
    {
        AddRow(L"Total de Mudancas", NumW(totalMudancas));
    }
}
