// ============================================================
//  SystemInfoPro - info_extra2.cpp
//  Drivers de Sistema, Central de Seguranca (Antivirus/Firewall/
//  TPM/Secure Boot), Tarefas Agendadas e DirectX/Codecs
// ============================================================
#include "common.h"
#include "wmi.h"
#include <taskschd.h>
#include <comdef.h>
#include <cstdlib>

// ---------------- Helpers de registro ----------------
static std::wstring RegTexto(HKEY raiz, const wchar_t* sub, const wchar_t* valor)
{
    HKEY k = nullptr;
    std::wstring res;
    if (RegOpenKeyExW(raiz, sub, 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        wchar_t buf[512] = {};
        DWORD tam = sizeof(buf), tipo = 0;
        if (RegQueryValueExW(k, valor, nullptr, &tipo, (LPBYTE)buf, &tam) == ERROR_SUCCESS &&
            (tipo == REG_SZ || tipo == REG_EXPAND_SZ))
            res = buf;
        RegCloseKey(k);
    }
    return res;
}

static bool RegDword(HKEY raiz, const wchar_t* sub, const wchar_t* valor, DWORD& saida)
{
    HKEY k = nullptr;
    bool ok = false;
    if (RegOpenKeyExW(raiz, sub, 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        DWORD tam = sizeof(DWORD), tipo = 0;
        if (RegQueryValueExW(k, valor, nullptr, &tipo, (LPBYTE)&saida, &tam) == ERROR_SUCCESS &&
            tipo == REG_DWORD)
            ok = true;
        RegCloseKey(k);
    }
    return ok;
}

// ================= Drivers de Sistema =================
void LoadDrivers()
{
    AddSection(L"DRIVERS DE SISTEMA (MODO KERNEL)");

    struct Drv { std::wstring nome, detalhe; bool rodando; };
    std::vector<Drv> lista;
    int rodando = 0;

    Wmi wmi;
    wmi.Query(L"SELECT Name, DisplayName, State, StartMode, PathName FROM Win32_SystemDriver",
    [&](IWbemClassObject* o)
    {
        Drv d;
        d.nome = Wmi::GetStr(o, L"DisplayName");
        if (d.nome.empty()) d.nome = Wmi::GetStr(o, L"Name");
        std::wstring estado = Wmi::GetStr(o, L"State");
        d.rodando = (estado == L"Running");
        if (d.rodando) rodando++;

        std::wstring caminho = Wmi::GetStr(o, L"PathName");
        d.detalhe = (d.rodando ? L"Em Execucao" : L"Parado") +
                    std::wstring(L"  (Inicio: ") + Wmi::GetStr(o, L"StartMode") + L")";
        if (!caminho.empty()) d.detalhe += L"  |  " + caminho;
        lista.push_back(d);
    });

    std::sort(lista.begin(), lista.end(),
        [](const Drv& a, const Drv& b) { return _wcsicmp(a.nome.c_str(), b.nome.c_str()) < 0; });

    AddRow(L"Total de Drivers", NumW((int)lista.size()));
    AddRow(L"Em Execucao Agora", NumW(rodando));
    AddBlank();
    AddRow(L"NOME DO DRIVER", L"STATUS, MODO DE INICIO E ARQUIVO");
    for (const auto& d : lista) AddRow(d.nome, d.detalhe);
}

// ================= Central de Seguranca =================
static void DecodificarProductState(unsigned long estado)
{
    // Decodificacao usual do productState do SecurityCenter2
    unsigned habilitado = (estado >> 8) & 0xFF;   // 0x10/0x11 = ativo
    unsigned assinatura = estado & 0xFF;          // 0x00 = atualizado

    AddRow(L"  Protecao em Tempo Real",
           (habilitado & 0x10) ? L"Ativada" : L"Desativada (ou desconhecida)");
    AddRow(L"  Definicoes de Virus",
           (assinatura == 0) ? L"Atualizadas" : L"Desatualizadas (ou desconhecidas)");
}

void LoadSecurity()
{
    AddSection(L"ANTIVIRUS REGISTRADOS (CENTRAL DE SEGURANCA)");
    {
        Wmi sec(L"ROOT\\SecurityCenter2");
        bool achou = false;
        sec.Query(L"SELECT displayName, productState, pathToSignedProductExe FROM AntiVirusProduct",
        [&achou](IWbemClassObject* o)
        {
            achou = true;
            AddRow(L"Antivirus", Wmi::GetStr(o, L"displayName"));
            DecodificarProductState((unsigned long)Wmi::GetUint(o, L"productState"));
            std::wstring exe = Wmi::GetStr(o, L"pathToSignedProductExe");
            if (!exe.empty()) AddRow(L"  Executavel", exe);
            AddBlank();
        });
        if (!achou)
            AddRow(L"Status", L"Nenhum antivirus registrado (ou Windows Server).");

        AddSection(L"FIREWALLS DE TERCEIROS REGISTRADOS");
        bool achouFw = false;
        sec.Query(L"SELECT displayName, productState FROM FirewallProduct",
        [&achouFw](IWbemClassObject* o)
        {
            achouFw = true;
            AddRow(L"Firewall", Wmi::GetStr(o, L"displayName"));
            AddBlank();
        });
        if (!achouFw)
            AddRow(L"Status", L"Nenhum firewall de terceiros (provavelmente usando o Firewall do Windows).");
    }

    AddBlank();
    AddSection(L"RECURSOS DE SEGURANCA DA PLATAFORMA");

    // Secure Boot
    DWORD sb = 0;
    if (RegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
                 L"UEFISecureBootEnabled", sb))
        AddRow(L"Secure Boot (UEFI)", sb ? L"Ativado" : L"Desativado");
    else
        AddRow(L"Secure Boot (UEFI)", L"Nao disponivel (BIOS Legado ou sem suporte)");

    // TPM
    {
        Wmi tpm(L"ROOT\\CIMV2\\Security\\MicrosoftTpm");
        bool achouTpm = false;
        tpm.Query(L"SELECT IsEnabled_InitialValue, IsActivated_InitialValue, SpecVersion FROM Win32_Tpm",
        [&achouTpm](IWbemClassObject* o)
        {
            achouTpm = true;
            AddRow(L"Chip TPM", Wmi::GetBool(o, L"IsEnabled_InitialValue") ? L"Presente e Habilitado" : L"Presente, porem Desabilitado");
            std::wstring spec = Wmi::GetStr(o, L"SpecVersion");
            if (!spec.empty()) AddRow(L"  Versao da Especificacao", spec);
        });
        if (!achouTpm)
            AddRow(L"Chip TPM", L"Nao detectado (ou sem permissao de leitura)");
    }

    // UAC
    DWORD uac = 0;
    if (RegDword(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableLUA", uac))
        AddRow(L"Controle de Conta de Usuario (UAC)", uac ? L"Ativado" : L"Desativado");
}

// ================= Tarefas Agendadas =================
static void EnumerarPastaTarefas(ITaskFolder* pasta, int& total, int& prontas,
                                 int& desativadas, int& executando, int nivel)
{
    if (!pasta || nivel > 6) return;

    // ---- Tarefas desta pasta ----
    IRegisteredTaskCollection* tarefas = nullptr;
    if (SUCCEEDED(pasta->GetTasks(TASK_ENUM_HIDDEN, &tarefas)) && tarefas)
    {
        LONG qtd = 0;
        tarefas->get_Count(&qtd);
        for (LONG i = 1; i <= qtd; i++)
        {
            IRegisteredTask* t = nullptr;
            if (FAILED(tarefas->get_Item(_variant_t(i), &t)) || !t) continue;

            BSTR nome = nullptr;
            TASK_STATE estado = TASK_STATE_UNKNOWN;
            t->get_Name(&nome);
            t->get_State(&estado);

            std::wstring estadoTxt;
            switch (estado)
            {
            case TASK_STATE_READY:    estadoTxt = L"Pronta";      prontas++; break;
            case TASK_STATE_RUNNING:  estadoTxt = L"Executando";  executando++; break;
            case TASK_STATE_DISABLED: estadoTxt = L"Desativada";  desativadas++; break;
            case TASK_STATE_QUEUED:   estadoTxt = L"Na Fila"; break;
            default:                  estadoTxt = L"Desconhecido"; break;
            }

            std::wstring detalhe = estadoTxt;
            DATE prox = 0;
            if (SUCCEEDED(t->get_NextRunTime(&prox)) && prox != 0)
            {
                SYSTEMTIME st{};
                if (VariantTimeToSystemTime(prox, &st))
                {
                    wchar_t b[64];
                    swprintf_s(b, L"%02d/%02d/%04d %02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
                    detalhe += std::wstring(L"  |  Proxima execucao: ") + b;
                }
            }

            BSTR caminhoPasta = nullptr;
            pasta->get_Path(&caminhoPasta);
            if (caminhoPasta && wcslen(caminhoPasta) > 1)
                detalhe += std::wstring(L"  |  Pasta: ") + caminhoPasta;
            if (caminhoPasta) SysFreeString(caminhoPasta);

            AddRow(nome ? nome : L"(sem nome)", detalhe);
            if (nome) SysFreeString(nome);
            total++;
            t->Release();
        }
        tarefas->Release();
    }

    // ---- Subpastas ----
    ITaskFolderCollection* subpastas = nullptr;
    if (SUCCEEDED(pasta->GetFolders(0, &subpastas)) && subpastas)
    {
        LONG qtd = 0;
        subpastas->get_Count(&qtd);
        for (LONG i = 1; i <= qtd; i++)
        {
            ITaskFolder* sub = nullptr;
            if (SUCCEEDED(subpastas->get_Item(_variant_t(i), &sub)) && sub)
            {
                EnumerarPastaTarefas(sub, total, prontas, desativadas, executando, nivel + 1);
                sub->Release();
            }
        }
        subpastas->Release();
    }
}

void LoadTasks()
{
    AddSection(L"TAREFAS AGENDADAS DO WINDOWS");

    ITaskService* servico = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(TaskScheduler), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(ITaskService), (void**)&servico);
    if (FAILED(hr) || !servico)
    {
        AddRow(L"Erro", L"Nao foi possivel acessar o Agendador de Tarefas.");
        return;
    }
    if (FAILED(servico->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t())))
    {
        servico->Release();
        AddRow(L"Erro", L"Falha ao conectar ao servico do Agendador.");
        return;
    }

    ITaskFolder* raiz = nullptr;
    if (FAILED(servico->GetFolder(_bstr_t(L"\\"), &raiz)) || !raiz)
    {
        servico->Release();
        AddRow(L"Erro", L"Pasta raiz de tarefas inacessivel.");
        return;
    }

    int total = 0, prontas = 0, desativadas = 0, executando = 0;

    AddRow(L"NOME DA TAREFA", L"ESTADO, PROXIMA EXECUCAO E PASTA");
    EnumerarPastaTarefas(raiz, total, prontas, desativadas, executando, 0);

    AddBlank();
    AddRow(L"Total de Tarefas", NumW(total));
    AddRow(L"Prontas / Executando / Desativadas",
           NumW(prontas) + L" / " + NumW(executando) + L" / " + NumW(desativadas));

    raiz->Release();
    servico->Release();
}

// ================= DirectX e Codecs =================
void LoadDirectX()
{
    AddSection(L"DIRECTX");

    DWORD build = 0;
    std::wstring buildTxt = RegTexto(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber");
    build = (DWORD)_wtoi(buildTxt.c_str());

    std::wstring dx;
    if (build >= 22000)      dx = L"DirectX 12 Ultimate (Windows 11)";
    else if (build >= 19041) dx = L"DirectX 12 Ultimate (Windows 10 2004+)";
    else if (build >= 10240) dx = L"DirectX 12 (Windows 10)";
    else if (build >= 9600)  dx = L"DirectX 11.2 (Windows 8.1)";
    else if (build >= 9200)  dx = L"DirectX 11.1 (Windows 8)";
    else                     dx = L"DirectX 11 ou anterior";
    AddRow(L"Versao Maxima Suportada pelo SO", dx);

    std::wstring versaoReg = RegTexto(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\DirectX", L"Version");
    if (!versaoReg.empty())
        AddRow(L"Versao do Runtime Legado (registro)", versaoReg);
    AddRow(L"Diagnostico Completo", L"Execute 'dxdiag' para o relatorio oficial da Microsoft.");

    AddBlank();
    AddSection(L"CODECS E DRIVERS DE MIDIA REGISTRADOS (Drivers32)");

    HKEY k = nullptr;
    int contador = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32", 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        for (DWORD i = 0; ; i++)
        {
            wchar_t nome[256], valor[512];
            DWORD tamNome = 256, tamValor = sizeof(valor), tipo = 0;
            LONG r = RegEnumValueW(k, i, nome, &tamNome, nullptr, &tipo, (LPBYTE)valor, &tamValor);
            if (r != ERROR_SUCCESS) break;
            if (tipo == REG_SZ && nome[0])
            {
                AddRow(nome, valor);
                contador++;
            }
        }
        RegCloseKey(k);
    }
    if (contador == 0)
        AddRow(L"Status", L"Nenhum codec legado registrado.");
    else
    {
        AddBlank();
        AddRow(L"Total de Entradas", NumW(contador));
    }
}
