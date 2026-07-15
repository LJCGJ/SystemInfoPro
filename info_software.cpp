// ============================================================
//  SystemInfoPro - info_software.cpp
//  SO, Programas Instalados, Inicializacao, Processos,
//  Servicos e Contas de Usuario
// ============================================================
#include "common.h"
#include "wmi.h"
#include <winsvc.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdlib>

#pragma comment(lib, "advapi32.lib")

// ---------------- Registro: helper ----------------
static std::wstring RegLerTexto(HKEY raiz, const wchar_t* subchave, const wchar_t* valor,
                                REGSAM extra = 0)
{
    HKEY k = nullptr;
    std::wstring res;
    if (RegOpenKeyExW(raiz, subchave, 0, KEY_READ | extra, &k) == ERROR_SUCCESS)
    {
        wchar_t buf[1024] = {};
        DWORD tam = sizeof(buf), tipo = 0;
        if (RegQueryValueExW(k, valor, nullptr, &tipo, (LPBYTE)buf, &tam) == ERROR_SUCCESS &&
            (tipo == REG_SZ || tipo == REG_EXPAND_SZ))
            res = buf;
        RegCloseKey(k);
    }
    return res;
}

// ================= Sistema Operacional =================
void LoadOS()
{
    Wmi wmi;
    wmi.Query(L"SELECT Caption, BuildNumber, InstallDate, SerialNumber, OSArchitecture, "
              L"SystemDirectory, LastBootUpTime FROM Win32_OperatingSystem",
    [](IWbemClassObject* o)
    {
        AddRow(L"Edicao do Windows", Trim(Wmi::GetStr(o, L"Caption")));
        AddRow(L"Arquitetura do Sistema", Wmi::GetStr(o, L"OSArchitecture"));
        AddRow(L"Numero da Compilacao (Build)", Wmi::GetStr(o, L"BuildNumber"));
        std::wstring inst = Wmi::GetStr(o, L"InstallDate");
        if (!inst.empty()) AddRow(L"Data de Instalacao", CimDateParaTexto(inst));
        AddRow(L"Numero de Serie do SO", Wmi::GetStr(o, L"SerialNumber"));
        AddRow(L"Diretorio do Sistema", Wmi::GetStr(o, L"SystemDirectory"));
        std::wstring boot = Wmi::GetStr(o, L"LastBootUpTime");
        if (!boot.empty()) AddRow(L"Ultima Inicializacao", CimDateParaTexto(boot));
    });

    std::wstring versao = RegLerTexto(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
    if (!versao.empty()) AddRow(L"Versao de Lancamento", versao);

    std::wstring chave = RegLerTexto(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\SoftwareProtectionPlatform",
        L"BackupProductKeyDefault");
    if (!chave.empty()) AddRow(L"Chave de Ativacao (Product Key)", chave);

    AddBlank();
    wchar_t nomePc[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD tam = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(nomePc, &tam);
    AddRow(L"Nome do Computador", nomePc);

    wchar_t usuario[256] = {};
    DWORD tamU = 256;
    GetUserNameW(usuario, &tamU);
    AddRow(L"Usuario Atual", usuario);

    ULONGLONG seg = GetTickCount64() / 1000;
    AddRow(L"Tempo Ligado (Uptime)",
        NumW(seg / 86400) + L" dias, " + NumW((seg % 86400) / 3600) + L" horas, " +
        NumW((seg % 3600) / 60) + L" minutos");
}

// ================= Programas Instalados =================
void LoadPrograms()
{
    struct Programa { std::wstring nome, detalhe; };
    std::vector<Programa> lista;

    auto VarrerChave = [&lista](HKEY raiz, const wchar_t* caminho, REGSAM visao)
    {
        HKEY base = nullptr;
        if (RegOpenKeyExW(raiz, caminho, 0, KEY_READ | visao, &base) != ERROR_SUCCESS) return;

        for (DWORD i = 0; ; i++)
        {
            wchar_t sub[256];
            DWORD tam = 256;
            if (RegEnumKeyExW(base, i, sub, &tam, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;

            HKEY prog = nullptr;
            if (RegOpenKeyExW(base, sub, 0, KEY_READ | visao, &prog) != ERROR_SUCCESS) continue;

            auto Ler = [&prog](const wchar_t* v) -> std::wstring
            {
                wchar_t buf[512] = {};
                DWORD t = sizeof(buf), tipo = 0;
                if (RegQueryValueExW(prog, v, nullptr, &tipo, (LPBYTE)buf, &t) == ERROR_SUCCESS &&
                    (tipo == REG_SZ || tipo == REG_EXPAND_SZ))
                    return buf;
                return L"";
            };

            std::wstring nome = Ler(L"DisplayName");
            if (!nome.empty())
            {
                std::wstring ver = Ler(L"DisplayVersion");
                std::wstring pub = Ler(L"Publisher");
                if (ver.empty()) ver = L"Desconhecida";
                if (pub.empty()) pub = L"Nao informado";
                lista.push_back({ nome, L"Versao: " + ver + L"  |  Desenvolvedor: " + pub });
            }
            RegCloseKey(prog);
        }
        RegCloseKey(base);
    };

    const wchar_t* caminho = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    VarrerChave(HKEY_LOCAL_MACHINE, caminho, KEY_WOW64_64KEY);   // 64 bits
    VarrerChave(HKEY_LOCAL_MACHINE, caminho, KEY_WOW64_32KEY);   // 32 bits
    VarrerChave(HKEY_CURRENT_USER,  caminho, 0);                 // por usuario

    std::sort(lista.begin(), lista.end(),
        [](const Programa& a, const Programa& b) { return _wcsicmp(a.nome.c_str(), b.nome.c_str()) < 0; });
    // remove duplicados exatos
    lista.erase(std::unique(lista.begin(), lista.end(),
        [](const Programa& a, const Programa& b) { return a.nome == b.nome && a.detalhe == b.detalhe; }),
        lista.end());

    AddSection(L"SOFTWARES INSTALADOS NO SISTEMA");
    AddRow(L"Total de Programas Encontrados", NumW((int)lista.size()));
    AddBlank();
    AddRow(L"NOME DO SOFTWARE", L"DETALHES (VERSAO E FABRICANTE)");
    for (const auto& p : lista) AddRow(p.nome, p.detalhe);
}

// ================= Programas de Inicializacao =================
void LoadStartup()
{
    AddSection(L"PROGRAMAS QUE INICIAM COM O WINDOWS");
    Wmi wmi;
    int contador = 1;
    wmi.Query(L"SELECT Name, Command, Location, User FROM Win32_StartupCommand",
    [&contador](IWbemClassObject* o)
    {
        std::wstring nome = Wmi::GetStr(o, L"Name");
        if (nome.empty()) return;
        AddRow(L"Entrada " + NumW(contador) + L": " + nome, Wmi::GetStr(o, L"Command"));
        std::wstring local = Wmi::GetStr(o, L"Location");
        if (!local.empty()) AddRow(L"  Origem do Gatilho", local);
        std::wstring usuario = Wmi::GetStr(o, L"User");
        if (!usuario.empty()) AddRow(L"  Escopo de Usuario", usuario);
        AddBlank();
        contador++;
    });
    if (contador == 1)
        AddRow(L"Status", L"Nenhum programa de inicializacao detectado.");
}

// ================= Processos =================
void LoadProcesses()
{
    struct Proc { std::wstring nome; DWORD pid; SIZE_T ram; DWORD threads; };
    std::vector<Proc> procs;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe))
        {
            do
            {
                Proc p{ pe.szExeFile, pe.th32ProcessID, 0, pe.cntThreads };
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (h)
                {
                    PROCESS_MEMORY_COUNTERS pmc{ sizeof(pmc) };
                    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
                        p.ram = pmc.WorkingSetSize;
                    CloseHandle(h);
                }
                procs.push_back(p);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    std::sort(procs.begin(), procs.end(),
        [](const Proc& a, const Proc& b) { return a.ram > b.ram; });

    AddRow(L"Total de Processos Ativos", NumW((int)procs.size()));
    AddBlank();
    AddRow(L"NOME DO PROCESSO (PID)", L"USO DE RECURSOS (RAM E THREADS)");
    int mostrados = 0;
    for (const auto& p : procs)
    {
        if (mostrados++ >= 60) break;
        AddRow(p.nome + L"  (" + NumW(p.pid) + L")",
               FormatBytes(p.ram) + L" RAM  |  " + NumW(p.threads) + L" Threads");
    }
}

// ================= Servicos =================
void LoadServices()
{
    AddSection(L"ESTATISTICAS DOS SERVICOS DO SISTEMA");

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm)
    {
        AddRow(L"Erro", L"Sem acesso ao Gerenciador de Servicos.");
        return;
    }

    DWORD bytesNecessarios = 0, qtd = 0, retomar = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          nullptr, 0, &bytesNecessarios, &qtd, &retomar, nullptr);
    std::vector<BYTE> buf(bytesNecessarios);
    if (!EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                               buf.data(), bytesNecessarios, &bytesNecessarios, &qtd, &retomar, nullptr))
    {
        CloseServiceHandle(scm);
        AddRow(L"Erro", L"Falha ao enumerar servicos.");
        return;
    }

    auto* servicos = (ENUM_SERVICE_STATUS_PROCESSW*)buf.data();
    int ativos = 0, inativos = 0;
    for (DWORD i = 0; i < qtd; i++)
        (servicos[i].ServiceStatusProcess.dwCurrentState == SERVICE_RUNNING) ? ativos++ : inativos++;

    AddRow(L"Servicos em Execucao (Ativos)", NumW(ativos));
    AddRow(L"Servicos Parados (Inativos)", NumW(inativos));
    AddBlank();
    AddRow(L"NOME DO SERVICO", L"STATUS E MODO DE INICIALIZACAO");

    for (DWORD i = 0; i < qtd; i++)
    {
        std::wstring estado;
        switch (servicos[i].ServiceStatusProcess.dwCurrentState)
        {
        case SERVICE_RUNNING: estado = L"Em Execucao"; break;
        case SERVICE_STOPPED: estado = L"Parado"; break;
        case SERVICE_PAUSED:  estado = L"Pausado"; break;
        case SERVICE_START_PENDING: estado = L"Iniciando"; break;
        case SERVICE_STOP_PENDING:  estado = L"Parando"; break;
        default: estado = L"Outro"; break;
        }

        // Modo de inicializacao
        std::wstring modo = L"?";
        SC_HANDLE sv = OpenServiceW(scm, servicos[i].lpServiceName, SERVICE_QUERY_CONFIG);
        if (sv)
        {
            DWORD tamCfg = 0;
            QueryServiceConfigW(sv, nullptr, 0, &tamCfg);
            if (tamCfg > 0)
            {
                std::vector<BYTE> cfgBuf(tamCfg);
                auto* cfg = (QUERY_SERVICE_CONFIGW*)cfgBuf.data();
                if (QueryServiceConfigW(sv, cfg, tamCfg, &tamCfg))
                {
                    switch (cfg->dwStartType)
                    {
                    case SERVICE_AUTO_START:   modo = L"Automatico"; break;
                    case SERVICE_DEMAND_START: modo = L"Manual"; break;
                    case SERVICE_DISABLED:     modo = L"Desativado"; break;
                    case SERVICE_BOOT_START:   modo = L"Boot"; break;
                    case SERVICE_SYSTEM_START: modo = L"Sistema"; break;
                    }
                }
            }
            CloseServiceHandle(sv);
        }

        AddRow(servicos[i].lpDisplayName, estado + L"  (Modo: " + modo + L")");
    }
    CloseServiceHandle(scm);
}

// ================= Contas de Usuario =================
void LoadUsers()
{
    AddSection(L"CONTAS DE USUARIO LOCAIS");
    Wmi wmi;
    int contador = 1;
    wmi.Query(L"SELECT Name, FullName, Disabled, PasswordRequired, Lockout, Status "
              L"FROM Win32_UserAccount WHERE LocalAccount=True",
    [&contador](IWbemClassObject* o)
    {
        AddRow(L"Conta Local " + NumW(contador), Wmi::GetStr(o, L"Name"));
        std::wstring completo = Wmi::GetStr(o, L"FullName");
        if (!completo.empty()) AddRow(L"  Nome Completo de Registro", completo);
        AddRow(L"  Status da Conta", Wmi::GetBool(o, L"Disabled") ? L"Inativa (Desabilitada)" : L"Ativa");
        AddRow(L"  Bloqueio por Erro de Senha", Wmi::GetBool(o, L"Lockout") ? L"Bloqueada" : L"Livre");
        AddRow(L"  Exigencia de Senha no Login", Wmi::GetBool(o, L"PasswordRequired") ? L"Sim" : L"Nao");
        std::wstring st = Wmi::GetStr(o, L"Status");
        if (!st.empty()) AddRow(L"  Condicao Geral", st);
        AddBlank();
        contador++;
    });
}
