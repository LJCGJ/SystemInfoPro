// ============================================================
//  SystemInfoPro - wmi.cpp
// ============================================================
#include "wmi.h"
#include <cstdlib>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

Wmi::Wmi(const wchar_t* ns)
{
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, (LPVOID*)&m_loc);
    if (FAILED(hr) || !m_loc) return;

    hr = m_loc->ConnectServer(_bstr_t(ns), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &m_svc);
    if (FAILED(hr) || !m_svc) { m_svc = nullptr; return; }

    CoSetProxyBlanket(m_svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                      nullptr, EOAC_NONE);
}

Wmi::~Wmi()
{
    if (m_svc) m_svc->Release();
    if (m_loc) m_loc->Release();
}

bool Wmi::Query(const std::wstring& wql, const std::function<void(IWbemClassObject*)>& cb)
{
    if (!m_svc) return false;

    IEnumWbemClassObject* en = nullptr;
    HRESULT hr = m_svc->ExecQuery(_bstr_t(L"WQL"), _bstr_t(wql.c_str()),
                                  WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                  nullptr, &en);
    if (FAILED(hr) || !en) return false;

    for (;;)
    {
        IWbemClassObject* obj = nullptr;
        ULONG ret = 0;
        hr = en->Next(WBEM_INFINITE, 1, &obj, &ret);
        if (FAILED(hr) || ret == 0) break;
        cb(obj);
        obj->Release();
    }
    en->Release();
    return true;
}

std::wstring Wmi::GetStr(IWbemClassObject* o, const wchar_t* prop)
{
    VARIANT v; VariantInit(&v);
    std::wstring res;
    if (SUCCEEDED(o->Get(prop, 0, &v, nullptr, nullptr)) && v.vt != VT_NULL && v.vt != VT_EMPTY)
    {
        VARIANT vs; VariantInit(&vs);
        if (v.vt == VT_BSTR)
            res = v.bstrVal ? v.bstrVal : L"";
        else if (SUCCEEDED(VariantChangeType(&vs, &v, 0, VT_BSTR)))
        {
            res = vs.bstrVal ? vs.bstrVal : L"";
            VariantClear(&vs);
        }
    }
    VariantClear(&v);
    return res;
}

unsigned long long Wmi::GetUint(IWbemClassObject* o, const wchar_t* prop, unsigned long long padrao)
{
    VARIANT v; VariantInit(&v);
    unsigned long long res = padrao;
    if (SUCCEEDED(o->Get(prop, 0, &v, nullptr, nullptr)) && v.vt != VT_NULL && v.vt != VT_EMPTY)
    {
        VARIANT vs; VariantInit(&vs);
        if (SUCCEEDED(VariantChangeType(&vs, &v, 0, VT_UI8)))
        {
            res = vs.ullVal;
            VariantClear(&vs);
        }
        else if (v.vt == VT_BSTR && v.bstrVal)
        {
            res = _wtoi64(v.bstrVal);
        }
    }
    VariantClear(&v);
    return res;
}

long long Wmi::GetInt(IWbemClassObject* o, const wchar_t* prop, long long padrao)
{
    VARIANT v; VariantInit(&v);
    long long res = padrao;
    if (SUCCEEDED(o->Get(prop, 0, &v, nullptr, nullptr)) && v.vt != VT_NULL && v.vt != VT_EMPTY)
    {
        VARIANT vs; VariantInit(&vs);
        if (SUCCEEDED(VariantChangeType(&vs, &v, 0, VT_I8)))
        {
            res = vs.llVal;
            VariantClear(&vs);
        }
    }
    VariantClear(&v);
    return res;
}

double Wmi::GetDouble(IWbemClassObject* o, const wchar_t* prop, double padrao)
{
    VARIANT v; VariantInit(&v);
    double res = padrao;
    if (SUCCEEDED(o->Get(prop, 0, &v, nullptr, nullptr)) && v.vt != VT_NULL && v.vt != VT_EMPTY)
    {
        VARIANT vs; VariantInit(&vs);
        if (SUCCEEDED(VariantChangeType(&vs, &v, 0, VT_R8)))
        {
            res = vs.dblVal;
            VariantClear(&vs);
        }
        else if (v.vt == VT_BSTR && v.bstrVal)
        {
            res = _wtof(v.bstrVal);
        }
    }
    VariantClear(&v);
    return res;
}

bool Wmi::GetBool(IWbemClassObject* o, const wchar_t* prop, bool padrao)
{
    VARIANT v; VariantInit(&v);
    bool res = padrao;
    if (SUCCEEDED(o->Get(prop, 0, &v, nullptr, nullptr)) && v.vt == VT_BOOL)
        res = (v.boolVal != VARIANT_FALSE);
    VariantClear(&v);
    return res;
}

bool Wmi::HasValue(IWbemClassObject* o, const wchar_t* prop)
{
    VARIANT v; VariantInit(&v);
    bool res = false;
    if (SUCCEEDED(o->Get(prop, 0, &v, nullptr, nullptr)))
        res = (v.vt != VT_NULL && v.vt != VT_EMPTY);
    VariantClear(&v);
    return res;
}

std::wstring Wmi::GetU16Array(IWbemClassObject* o, const wchar_t* prop)
{
    VARIANT v; VariantInit(&v);
    std::wstring res;
    if (SUCCEEDED(o->Get(prop, 0, &v, nullptr, nullptr)) && (v.vt & VT_ARRAY) && v.parray)
    {
        LONG lo = 0, hi = -1;
        SafeArrayGetLBound(v.parray, 1, &lo);
        SafeArrayGetUBound(v.parray, 1, &hi);
        for (LONG i = lo; i <= hi; i++)
        {
            LONG cod = 0;
            // WmiMonitorID usa uint16; le como I4 para seguranca
            VARTYPE vt = v.vt & ~VT_ARRAY;
            if (vt == VT_I4 || vt == VT_UI4)
            {
                SafeArrayGetElement(v.parray, &i, &cod);
            }
            else
            {
                SHORT s = 0;
                SafeArrayGetElement(v.parray, &i, &s);
                cod = (LONG)(unsigned short)s;
            }
            if (cod == 0) break;
            res.push_back((wchar_t)cod);
        }
    }
    VariantClear(&v);
    return Trim(res);
}

std::wstring CimDateParaTexto(const std::wstring& cim)
{
    // Formato CIM: yyyymmddHHMMSS.mmmmmm+UUU
    if (cim.length() < 14) return cim;
    std::wstring ano = cim.substr(0, 4), mes = cim.substr(4, 2), dia = cim.substr(6, 2);
    std::wstring hh = cim.substr(8, 2), mi = cim.substr(10, 2), ss = cim.substr(12, 2);
    return dia + L"/" + mes + L"/" + ano + L" " + hh + L":" + mi + L":" + ss;
}
