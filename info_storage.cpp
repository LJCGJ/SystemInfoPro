// ============================================================
//  SystemInfoPro - info_storage.cpp
//  Armazenamento: discos fisicos, SMART detalhado (ATA),
//  saude NVMe, temperatura de disco e volumes logicos.
//  OBS: SMART exige execucao como Administrador.
// ============================================================
#include "common.h"
#include "wmi.h"
#include <winioctl.h>

// ---------------- Nomes dos atributos SMART (ATA) ----------------
static std::wstring NomeAtributoSmart(BYTE id)
{
    switch (id)
    {
    case 1:   return L"Taxa de Erro de Leitura";
    case 3:   return L"Tempo de Giro (Spin-Up)";
    case 4:   return L"Contagem de Partidas/Paradas";
    case 5:   return L"Setores Realocados (CRITICO)";
    case 7:   return L"Taxa de Erro de Busca";
    case 9:   return L"Horas Ligado (Power-On Hours)";
    case 10:  return L"Tentativas de Giro (Spin Retry)";
    case 12:  return L"Ciclos de Liga/Desliga";
    case 173: return L"Desgaste Medio (SSD)";
    case 177: return L"Nivelamento de Desgaste (SSD)";
    case 179: return L"Blocos Reservados Usados (SSD)";
    case 181: return L"Erros de Programacao";
    case 182: return L"Erros de Apagamento";
    case 187: return L"Erros Incorrigiveis Reportados";
    case 188: return L"Timeouts de Comando";
    case 190: return L"Temperatura (Airflow)";
    case 194: return L"Temperatura do Disco";
    case 196: return L"Eventos de Realocacao";
    case 197: return L"Setores Pendentes (CRITICO)";
    case 198: return L"Setores Incorrigiveis (CRITICO)";
    case 199: return L"Erros CRC (Cabo/Interface)";
    case 231: return L"Vida Restante do SSD (%)";
    case 233: return L"Media Wearout Indicator (SSD)";
    case 241: return L"Total de Gravacoes do Host (LBA)";
    case 242: return L"Total de Leituras do Host (LBA)";
    default:
    {
        wchar_t b[48];
        swprintf_s(b, L"Atributo %u", (unsigned)id);
        return b;
    }
    }
}

// ---------------- SMART ATA via DeviceIoControl ----------------
static bool LerSmartAta(HANDLE h, BYTE numeroDisco, int* temperaturaSaida)
{
    GETVERSIONINPARAMS ver{};
    DWORD bytes = 0;
    if (!DeviceIoControl(h, SMART_GET_VERSION, nullptr, 0, &ver, sizeof(ver), &bytes, nullptr))
        return false;
    if (!(ver.fCapabilities & CAP_SMART_CMD))
        return false;

    const DWORD tamSaida = sizeof(SENDCMDOUTPARAMS) + READ_ATTRIBUTE_BUFFER_SIZE - 1;
    std::vector<BYTE> bufAttr(tamSaida), bufThr(tamSaida);

    auto ExecutarSmart = [&](BYTE subcomando, std::vector<BYTE>& saida) -> bool
    {
        SENDCMDINPARAMS in{};
        in.cBufferSize = READ_ATTRIBUTE_BUFFER_SIZE;
        in.irDriveRegs.bFeaturesReg = subcomando;        // 0xD0 attrs / 0xD1 thresholds
        in.irDriveRegs.bSectorCountReg = 1;
        in.irDriveRegs.bSectorNumberReg = 1;
        in.irDriveRegs.bCylLowReg = 0x4F;                // assinatura SMART
        in.irDriveRegs.bCylHighReg = 0xC2;
        in.irDriveRegs.bDriveHeadReg = 0xA0 | ((numeroDisco & 1) << 4);
        in.irDriveRegs.bCommandReg = SMART_CMD;          // 0xB0
        in.bDriveNumber = numeroDisco;
        DWORD ret = 0;
        return DeviceIoControl(h, SMART_RCV_DRIVE_DATA, &in, sizeof(in),
                               saida.data(), tamSaida, &ret, nullptr) != 0;
    };

    if (!ExecutarSmart(READ_ATTRIBUTES, bufAttr)) return false;
    bool temLimites = ExecutarSmart(READ_THRESHOLDS, bufThr);

    const BYTE* dados = ((SENDCMDOUTPARAMS*)bufAttr.data())->bBuffer;
    const BYTE* limites = temLimites ? ((SENDCMDOUTPARAMS*)bufThr.data())->bBuffer : nullptr;

    AddRow(L"  SMART", L"Suportado e habilitado - atributos abaixo:");
    AddRow(L"    [Atributo]", L"Valor Atual / Pior / Limite  |  Dado Bruto");

    bool algumCritico = false;
    for (int i = 0; i < 30; i++)
    {
        const BYTE* a = dados + 2 + i * 12;               // tabela comeca no offset 2
        BYTE id = a[0];
        if (id == 0) continue;
        BYTE atual = a[3], pior = a[4];
        unsigned long long bruto = 0;
        for (int b = 0; b < 6; b++) bruto |= ((unsigned long long)a[5 + b]) << (8 * b);

        BYTE limite = 0;
        if (limites)
        {
            for (int j = 0; j < 30; j++)
            {
                const BYTE* t = limites + 2 + j * 12;
                if (t[0] == id) { limite = t[1]; break; }
            }
        }

        std::wstring valor = NumW((int)atual) + L" / " + NumW((int)pior) + L" / " + NumW((int)limite) +
                             L"  |  " + NumW(bruto);

        if (id == 194 || id == 190)
        {
            int temp = (int)(bruto & 0xFF);
            valor += L"  (" + NumW(temp) + L" °C)";
            if (temperaturaSaida && id == 194) *temperaturaSaida = temp;
        }
        if ((id == 5 || id == 197 || id == 198) && bruto > 0)
        {
            valor += L"  << ATENCAO!";
            algumCritico = true;
        }

        AddRow(L"    " + NomeAtributoSmart(id), valor);
    }

    AddRow(L"  Avaliacao SMART",
           algumCritico ? L"ATENCAO: ha setores com problema - faca backup!" : L"Saudavel (sem setores realocados/pendentes)");
    return true;
}

// ---------------- Saude NVMe via Protocol Specific Query ----------------
static bool LerSaudeNvme(HANDLE h)
{
    const DWORD tamLog = 512;
    const DWORD tamBuf = FIELD_OFFSET(STORAGE_PROTOCOL_DATA_DESCRIPTOR, ProtocolSpecificData)
                       + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + tamLog;
    std::vector<BYTE> buf(max(tamBuf, (DWORD)4096), 0);

    STORAGE_PROPERTY_QUERY* q = (STORAGE_PROPERTY_QUERY*)buf.data();
    q->PropertyId = StorageDeviceProtocolSpecificProperty;
    q->QueryType = PropertyStandardQuery;

    STORAGE_PROTOCOL_SPECIFIC_DATA* pd = (STORAGE_PROTOCOL_SPECIFIC_DATA*)q->AdditionalParameters;
    pd->ProtocolType = ProtocolTypeNvme;
    pd->DataType = NVMeDataTypeLogPage;
    pd->ProtocolDataRequestValue = 0x02;      // Log SMART / Health Information
    pd->ProtocolDataRequestSubValue = 0;
    pd->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    pd->ProtocolDataLength = tamLog;

    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, buf.data(), (DWORD)buf.size(),
                         buf.data(), (DWORD)buf.size(), &ret, nullptr))
        return false;

    STORAGE_PROTOCOL_DATA_DESCRIPTOR* dd = (STORAGE_PROTOCOL_DATA_DESCRIPTOR*)buf.data();
    if (dd->ProtocolSpecificData.ProtocolDataLength < tamLog) return false;

    const BYTE* log = (const BYTE*)&dd->ProtocolSpecificData + dd->ProtocolSpecificData.ProtocolDataOffset;

    auto U64 = [&](int off) -> unsigned long long
    {
        unsigned long long v = 0;
        for (int i = 0; i < 8; i++) v |= ((unsigned long long)log[off + i]) << (8 * i);
        return v;
    };

    AddRow(L"  Protocolo", L"NVMe - Log de Saude (Health Information)");

    int tempK = log[1] | (log[2] << 8);
    if (tempK > 0)
        AddRow(L"    Temperatura do SSD", NumW(tempK - 273) + L" °C");

    AddRow(L"    Reserva Disponivel", NumW((int)log[3]) + L" %  (limite de alerta: " + NumW((int)log[4]) + L" %)");
    AddRow(L"    Desgaste Utilizado (Percentage Used)", NumW((int)log[5]) + L" %");

    // Unidades de dados = blocos de 512.000 bytes
    double lidoTB = (double)U64(32) * 512000.0 / 1e12;
    double gravadoTB = (double)U64(48) * 512000.0 / 1e12;
    AddRow(L"    Total Lido (vida util)", NumW(lidoTB, 2) + L" TB");
    AddRow(L"    Total Gravado (vida util)", NumW(gravadoTB, 2) + L" TB");
    AddRow(L"    Ciclos de Energia", NumW(U64(112)));
    AddRow(L"    Horas Ligado", NumW(U64(128)) + L" h");
    AddRow(L"    Desligamentos Inseguros", NumW(U64(144)));
    AddRow(L"    Erros de Midia", NumW(U64(160)));

    BYTE alerta = log[0];
    AddRow(L"    Avaliacao NVMe", alerta == 0 ? L"Saudavel (sem alertas criticos)" :
                                                L"ATENCAO: alerta critico ativo (codigo " + NumW((int)alerta) + L")");
    return true;
}

// ---------------- Discos fisicos ----------------
void LoadStorage()
{
    AddSection(L"DISCOS FISICOS (HARDWARE + SMART)");

    bool acessoNegado = false;
    for (int n = 0; n < 16; n++)
    {
        std::wstring caminho = L"\\\\.\\PhysicalDrive" + NumW(n);
        HANDLE h = CreateFileW(caminho.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_ACCESS_DENIED) acessoNegado = true;
            // tenta somente leitura (para modelo/tamanho)
            h = CreateFileW(caminho.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
        }

        // ----- Descritor do dispositivo (modelo, serial, barramento) -----
        STORAGE_PROPERTY_QUERY q{};
        q.PropertyId = StorageDeviceProperty;
        q.QueryType = PropertyStandardQuery;
        BYTE desc[1024] = {};
        DWORD ret = 0;
        bool ehNvme = false;

        AddRow(L"Disco Fisico " + NumW(n), caminho);

        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), desc, sizeof(desc), &ret, nullptr))
        {
            auto* d = (STORAGE_DEVICE_DESCRIPTOR*)desc;
            auto LerCampo = [&](DWORD off) -> std::wstring
            {
                if (off == 0 || off >= sizeof(desc)) return L"";
                std::string s((const char*)desc + off);
                wchar_t w[256];
                MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w, 256);
                return Trim(w);
            };
            std::wstring modelo = LerCampo(d->ProductIdOffset);
            std::wstring fabric = LerCampo(d->VendorIdOffset);
            std::wstring serie  = LerCampo(d->SerialNumberOffset);
            if (!fabric.empty()) modelo = fabric + L" " + modelo;
            AddRow(L"  Modelo", modelo.empty() ? L"(desconhecido)" : modelo);
            if (!serie.empty()) AddRow(L"  Numero de Serie", serie);

            std::wstring bus;
            switch (d->BusType)
            {
            case BusTypeAta: case BusTypeSata: bus = L"SATA"; break;
            case BusTypeNvme: bus = L"NVMe (PCI Express)"; ehNvme = true; break;
            case BusTypeUsb: bus = L"USB"; break;
            case BusTypeRAID: bus = L"RAID"; break;
            case BusTypeSas: bus = L"SAS"; break;
            case BusTypeSd: bus = L"Cartao SD"; break;
            default: bus = L"Outro (" + NumW((int)d->BusType) + L")"; break;
            }
            AddRow(L"  Barramento", bus);
        }

        // ----- Tamanho -----
        GET_LENGTH_INFORMATION len{};
        if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &len, sizeof(len), &ret, nullptr))
            AddRow(L"  Capacidade Bruta", FormatBytes((unsigned long long)len.Length.QuadPart));

        // ----- SSD ou HDD (penalidade de busca) -----
        {
            STORAGE_PROPERTY_QUERY q2{};
            q2.PropertyId = StorageDeviceSeekPenaltyProperty;
            q2.QueryType = PropertyStandardQuery;
            DEVICE_SEEK_PENALTY_DESCRIPTOR sp{};
            if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q2, sizeof(q2), &sp, sizeof(sp), &ret, nullptr))
                AddRow(L"  Tipo de Midia", sp.IncursSeekPenalty ? L"HDD (disco mecanico)" : L"SSD (estado solido)");
        }

        // ----- SMART / Saude -----
        int tempDisco = -1;
        bool leuSmart = false;
        if (ehNvme)
            leuSmart = LerSaudeNvme(h);
        if (!leuSmart)
            leuSmart = LerSmartAta(h, (BYTE)n, &tempDisco);
        if (!leuSmart)
            AddRow(L"  SMART", acessoNegado ?
                   L"Sem acesso - execute o programa como Administrador." :
                   L"Nao disponivel para este dispositivo (ex: USB/RAID).");

        AddBlank();
        CloseHandle(h);
    }

    // ---------------- Volumes logicos ----------------
    AddSection(L"PARTICOES LOGICAS (VOLUMES)");
    DWORD unidades = GetLogicalDrives();
    for (int letra = 0; letra < 26; letra++)
    {
        if (!(unidades & (1u << letra))) continue;
        wchar_t raiz[] = { (wchar_t)(L'A' + letra), L':', L'\\', 0 };
        UINT tipo = GetDriveTypeW(raiz);
        if (tipo != DRIVE_FIXED && tipo != DRIVE_REMOVABLE && tipo != DRIVE_REMOTE) continue;

        ULARGE_INTEGER livre{}, total{};
        if (!GetDiskFreeSpaceExW(raiz, &livre, &total, nullptr)) continue;

        wchar_t rotulo[64] = {}, fs[32] = {};
        GetVolumeInformationW(raiz, rotulo, 64, nullptr, nullptr, nullptr, fs, 32);

        std::wstring tipoTxt = tipo == DRIVE_FIXED ? L"Disco Local" :
                               tipo == DRIVE_REMOVABLE ? L"Removivel" : L"Rede";
        AddRow(std::wstring(L"Unidade ") + raiz,
               tipoTxt + L"  |  " + (rotulo[0] ? rotulo : L"(sem rotulo)") + L"  |  " + fs);
        AddRow(L"  Tamanho Total", FormatBytes(total.QuadPart));
        AddRow(L"  Espaco Livre", FormatBytes(livre.QuadPart) +
               L"  (" + NumW(total.QuadPart ? (livre.QuadPart * 100 / total.QuadPart) : 0) + L"% livre)");
        AddBlank();
    }
}
