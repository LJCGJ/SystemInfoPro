// ============================================================
//  SystemInfoPro - manage.cpp
//  Gerenciamento ativo: finalizar processo, iniciar/parar
//  servico e desabilitar item de inicializacao.
//  Todas as acoes pedem confirmacao explicita do usuario.
// ============================================================
#include "common.h"
#include <winsvc.h>

#pragma comment(lib, "advapi32.lib")

// ================= Finalizar processo =================
bool GerenciarFinalizarProcesso(HWND dono, DWORD pid, const std::wstring& nomeCompleto)
{
    if (pid == 0) return false;

    // A linha vem como "nome.exe  (PID)" - extrai so o nome
    std::wstring nome = nomeCompleto;
    size_t par = nome.find(L"  (");
    if (par != std::wstring::npos) nome = nome.substr(0, par);
    nome = Trim(nome);

    // Protege processos criticos do sistema
    const wchar_t* criticos[] = { L"System", L"csrss.exe", L"wininit.exe", L"services.exe",
                                  L"smss.exe", L"lsass.exe", L"winlogon.exe", L"svchost.exe" };
    for (const wchar_t* c : criticos)
    {
        if (_wcsicmp(nome.c_str(), c) == 0)
        {
            MessageBoxW(dono,
                (L"'" + nome + L"' e um processo critico do Windows.\n"
                 L"Finaliza-lo pode travar ou reiniciar o computador.\n\n"
                 L"Por seguranca, esta acao foi bloqueada.").c_str(),
                L"Acao bloqueada", MB_ICONWARNING);
            return false;
        }
    }

    std::wstring msg = L"Deseja realmente FINALIZAR o processo?\n\n" + nome +
                       L"  (PID " + NumW(pid) + L")\n\n"
                       L"⚠ Dados nao salvos neste programa serao perdidos.";
    if (MessageBoxW(dono, msg.c_str(), L"Confirmar finalizacao",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return false;

    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h)
    {
        MessageBoxW(dono, L"Nao foi possivel abrir o processo.\n"
                          L"Talvez precise executar como Administrador.", L"Erro", MB_ICONERROR);
        return false;
    }
    bool ok = TerminateProcess(h, 1) != 0;
    CloseHandle(h);

    if (ok)
        MessageBoxW(dono, (nome + L" foi finalizado.").c_str(), L"Sucesso", MB_ICONINFORMATION);
    else
        MessageBoxW(dono, L"Falha ao finalizar o processo.", L"Erro", MB_ICONERROR);
    return ok;
}

// ================= Iniciar / parar servico =================
// Recebe o DisplayName (mostrado na lista) e encontra o nome interno.
static std::wstring NomeInternoServico(const std::wstring& displayName)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return L"";

    DWORD bytes = 0, qtd = 0, retomar = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          nullptr, 0, &bytes, &qtd, &retomar, nullptr);
    std::vector<BYTE> buf(bytes);
    std::wstring interno;
    if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                              buf.data(), bytes, &bytes, &qtd, &retomar, nullptr))
    {
        auto* svc = (ENUM_SERVICE_STATUS_PROCESSW*)buf.data();
        for (DWORD i = 0; i < qtd; i++)
        {
            if (svc[i].lpDisplayName && displayName == svc[i].lpDisplayName)
            {
                interno = svc[i].lpServiceName;
                break;
            }
        }
    }
    CloseServiceHandle(scm);
    return interno;
}

bool GerenciarServico(HWND dono, const std::wstring& nomeExibicao, bool iniciar)
{
    std::wstring interno = NomeInternoServico(nomeExibicao);
    if (interno.empty())
    {
        MessageBoxW(dono, L"Nao foi possivel identificar o servico.", L"Erro", MB_ICONERROR);
        return false;
    }

    std::wstring acao = iniciar ? L"INICIAR" : L"PARAR";
    std::wstring msg = L"Deseja " + acao + L" o servico?\n\n" + nomeExibicao +
                       L"\n(interno: " + interno + L")";
    if (!iniciar)
        msg += L"\n\n⚠ Parar servicos do sistema pode afetar recursos do Windows.";
    if (MessageBoxW(dono, msg.c_str(), L"Confirmar acao no servico",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return false;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
    {
        MessageBoxW(dono, L"Sem acesso ao Gerenciador de Servicos.\nExecute como Administrador.",
                    L"Erro", MB_ICONERROR);
        return false;
    }

    SC_HANDLE sv = OpenServiceW(scm, interno.c_str(),
                                iniciar ? SERVICE_START : (SERVICE_STOP | SERVICE_QUERY_STATUS));
    bool ok = false;
    if (sv)
    {
        if (iniciar)
        {
            ok = StartServiceW(sv, 0, nullptr) != 0;
            if (!ok && GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) ok = true;
        }
        else
        {
            SERVICE_STATUS status{};
            ok = ControlService(sv, SERVICE_CONTROL_STOP, &status) != 0;
        }
        CloseServiceHandle(sv);
    }
    CloseServiceHandle(scm);

    if (ok)
        MessageBoxW(dono, (L"Servico " + acao + L" com sucesso.\n"
                          L"Atualize a categoria (F5) para ver o novo estado.").c_str(),
                    L"Sucesso", MB_ICONINFORMATION);
    else
        MessageBoxW(dono, L"Falha na operacao.\nVerifique se ha permissao de Administrador.",
                    L"Erro", MB_ICONERROR);
    return ok;
}

// ================= Desabilitar item de inicializacao =================
// Move a entrada da chave Run para uma chave de "desabilitados" do proprio app.
bool GerenciarDesabilitarStartup(HWND dono, const std::wstring& nome)
{
    std::wstring msg = L"Deseja DESABILITAR este item de inicializacao?\n\n" + nome +
                       L"\n\nEle deixara de iniciar junto com o Windows.\n"
                       L"(Funciona para entradas do registro Run do usuario/maquina.)";
    if (MessageBoxW(dono, msg.c_str(), L"Confirmar desabilitacao",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return false;

    const wchar_t* chavesRun[] = {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
    };
    HKEY raizes[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };

    for (int i = 0; i < 2; i++)
    {
        HKEY k = nullptr;
        if (RegOpenKeyExW(raizes[i], chavesRun[i], 0, KEY_READ | KEY_WRITE, &k) != ERROR_SUCCESS)
            continue;

        // O 'nome' pode vir como "Entrada N: NomeReal" - extrai a parte apos ": "
        std::wstring alvo = nome;
        size_t pos = alvo.find(L": ");
        if (pos != std::wstring::npos) alvo = alvo.substr(pos + 2);

        wchar_t valor[1024] = {};
        DWORD tam = sizeof(valor), tipo = 0;
        if (RegQueryValueExW(k, alvo.c_str(), nullptr, &tipo, (LPBYTE)valor, &tam) == ERROR_SUCCESS)
        {
            // Guarda backup em subchave e apaga do Run
            HKEY kbak = nullptr;
            RegCreateKeyExW(raizes[i], (std::wstring(chavesRun[i]) + L"_SystemInfoPro_Desabilitados").c_str(),
                            0, nullptr, 0, KEY_WRITE, nullptr, &kbak, nullptr);
            if (kbak)
            {
                RegSetValueExW(kbak, alvo.c_str(), 0, tipo, (LPBYTE)valor, tam);
                RegCloseKey(kbak);
            }
            RegDeleteValueW(k, alvo.c_str());
            RegCloseKey(k);
            MessageBoxW(dono, (alvo + L" foi desabilitado.\n"
                              L"Uma copia de seguranca foi guardada no registro.\n"
                              L"Atualize a categoria (F5).").c_str(),
                        L"Sucesso", MB_ICONINFORMATION);
            return true;
        }
        RegCloseKey(k);
    }

    MessageBoxW(dono,
        L"Este item nao esta nas chaves 'Run' do registro\n"
        L"(pode ser da pasta Inicializar ou de Tarefas Agendadas).\n\n"
        L"Use o Gerenciador de Tarefas do Windows para esses casos.",
        L"Nao foi possivel desabilitar", MB_ICONINFORMATION);
    return false;
}

// ================= Reabilitar item desabilitado =================
// Move de volta da chave de backup do app para a chave Run original.
bool GerenciarReabilitarStartup(HWND dono, const std::wstring& nome)
{
    std::wstring msg = L"Deseja REABILITAR este item de inicializacao?\n\n" + nome +
                       L"\n\nEle voltara a iniciar junto com o Windows.";
    if (MessageBoxW(dono, msg.c_str(), L"Confirmar reabilitacao",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return false;

    HKEY raizes[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    const wchar_t* backup = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run_SystemInfoPro_Desabilitados";
    const wchar_t* run = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

    for (int i = 0; i < 2; i++)
    {
        HKEY kbak = nullptr;
        if (RegOpenKeyExW(raizes[i], backup, 0, KEY_READ | KEY_WRITE, &kbak) != ERROR_SUCCESS)
            continue;

        wchar_t valor[1024] = {};
        DWORD tam = sizeof(valor), tipo = 0;
        if (RegQueryValueExW(kbak, nome.c_str(), nullptr, &tipo, (LPBYTE)valor, &tam) == ERROR_SUCCESS)
        {
            HKEY krun = nullptr;
            if (RegOpenKeyExW(raizes[i], run, 0, KEY_WRITE, &krun) == ERROR_SUCCESS)
            {
                RegSetValueExW(krun, nome.c_str(), 0, tipo, (LPBYTE)valor, tam);
                RegCloseKey(krun);
            }
            RegDeleteValueW(kbak, nome.c_str());
            RegCloseKey(kbak);
            MessageBoxW(dono, (nome + L" foi reabilitado.\nAtualize a categoria (F5).").c_str(),
                        L"Sucesso", MB_ICONINFORMATION);
            return true;
        }
        RegCloseKey(kbak);
    }

    MessageBoxW(dono, L"Item nao encontrado na lista de desabilitados.", L"Aviso", MB_ICONINFORMATION);
    return false;
}

// ================= Pagina: itens desabilitados pelo app =================
void LoadStartupDisabled()
{
    AddSection(L"ITENS DE INICIALIZACAO DESABILITADOS");
    AddRow(L"Dica", L"Clique com o botao direito em um item para reabilita-lo.");
    AddBlank();

    HKEY raizes[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    const wchar_t* nomesRaiz[] = { L"Usuario (HKCU)", L"Sistema (HKLM)" };
    const wchar_t* backup = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run_SystemInfoPro_Desabilitados";

    int total = 0;
    for (int i = 0; i < 2; i++)
    {
        HKEY k = nullptr;
        if (RegOpenKeyExW(raizes[i], backup, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;

        for (DWORD idx = 0; ; idx++)
        {
            wchar_t nome[256]; DWORD tamNome = 256;
            wchar_t valor[1024]; DWORD tamVal = sizeof(valor), tipo = 0;
            LONG r = RegEnumValueW(k, idx, nome, &tamNome, nullptr, &tipo, (LPBYTE)valor, &tamVal);
            if (r != ERROR_SUCCESS) break;
            if (nome[0])
            {
                AddActionRow(nome, std::wstring(L"[") + nomesRaiz[i] + L"]  " + valor, 4, nome);
                total++;
            }
        }
        RegCloseKey(k);
    }

    if (total == 0)
        AddRow(L"Status", L"Nenhum item desabilitado por este programa.");
}
