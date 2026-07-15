// ============================================================
//  SystemInfoPro - wmi.h
//  Ajudante de consultas WMI (equivalente ao ManagementObjectSearcher do C#)
// ============================================================
#pragma once
#include "common.h"
#include <wbemidl.h>
#include <comdef.h>

class Wmi
{
public:
    // ns exemplos: L"ROOT\\CIMV2" (padrao) ou L"ROOT\\WMI"
    explicit Wmi(const wchar_t* ns = L"ROOT\\CIMV2");
    ~Wmi();

    bool Ok() const { return m_svc != nullptr; }

    // Executa uma consulta WQL e chama o callback para cada objeto retornado.
    // Retorna false em erro.
    bool Query(const std::wstring& wql, const std::function<void(IWbemClassObject*)>& cb);

    // ---- Leitura de propriedades de um objeto WMI ----
    static std::wstring GetStr (IWbemClassObject* o, const wchar_t* prop);
    static unsigned long long GetUint(IWbemClassObject* o, const wchar_t* prop, unsigned long long padrao = 0);
    static long long GetInt (IWbemClassObject* o, const wchar_t* prop, long long padrao = 0);
    static double GetDouble(IWbemClassObject* o, const wchar_t* prop, double padrao = 0.0);
    static bool GetBool(IWbemClassObject* o, const wchar_t* prop, bool padrao = false);
    static bool HasValue(IWbemClassObject* o, const wchar_t* prop);
    // Array de uint16 (usado por WmiMonitorID: nomes vem como array de codigos)
    static std::wstring GetU16Array(IWbemClassObject* o, const wchar_t* prop);

private:
    IWbemLocator*  m_loc = nullptr;
    IWbemServices* m_svc = nullptr;
};

// Converte data CIM (yyyymmddHHMMSS...) para dd/mm/yyyy HH:MM:SS
std::wstring CimDateParaTexto(const std::wstring& cim);
