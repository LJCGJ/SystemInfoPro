// ============================================================
//  SystemInfoPro - Painel de Informacoes do Sistema (C++ / Win32)
//  common.h - Definicoes e utilitarios compartilhados
// ============================================================
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
// Evita que windows.h puxe o winsock.h antigo (conflita com winsock2.h)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <functional>

// ---------- ListView global (painel de detalhes) ----------
extern HWND g_hList;

// Adiciona uma linha "Propriedade | Valor"
void AddRow(const std::wstring& prop, const std::wstring& val);
// Adiciona um titulo de secao "--- TITULO ---"
void AddSection(const std::wstring& title);
// Linha em branco
void AddBlank();
// Limpa a lista (e o cache de chaves do tempo real)
void ClearList();
// Atualiza (ou insere) uma linha identificada por chave - usado no tempo real
void SetRowByKey(const std::wstring& key, const std::wstring& prop, const std::wstring& val);

// ---------- Formatacao ----------
std::wstring FormatBytes(unsigned long long bytes);          // ex: 1.23 GB
std::wstring FormatSpeedBits(double bitsPerSec);             // ex: 12.3 Mbps
std::wstring Trim(const std::wstring& s);

template <typename T>
std::wstring NumW(T v, int decimals = 0)
{
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(decimals) << (double)v;
    return ss.str();
}

// ---------- Modulos: Tempo Real ----------
void RealtimeReset();      // reinicia contadores (chamado ao entrar na tela)
void LoadRealtime();       // chamado a cada tick do timer
void RealtimeShutdown();   // libera PDH etc

// ---------- Modulos: Sensores ----------
void LoadTemperatures();

// ---------- Modulos: Hardware ----------
void LoadCPU();
void LoadBoard();          // placa-mae + BIOS
void LoadGPU();            // DXGI + WMI + NVML (uso, VRAM, largura de banda)
void LoadMonitors();
void LoadRAM();
void LoadStorage();        // discos fisicos + SMART/NVMe + volumes
void LoadUSB();
void LoadAudio();
void LoadPrinters();
void LoadBattery();

// ---------- Modulos: Software ----------
void LoadOS();
void LoadPrograms();
void LoadStartup();
void LoadProcesses();
void LoadServices();
void LoadUsers();

// ---------- Modulos: Rede ----------
void LoadNetwork();        // adaptadores (estatico)
void LoadTcpConnections(); // conexoes TCP ativas
void LoadShares();         // compartilhamentos de rede

// ---------- Modulos: Extras ----------
void LoadPCI();            // dispositivos PCI
void LoadHotfixes();       // atualizacoes do Windows
void LoadEnvVars();        // variaveis de ambiente
void LoadDrivers();        // drivers de sistema (kernel)
void LoadSecurity();       // antivirus, firewall, TPM, Secure Boot, UAC
void LoadTasks();          // tarefas agendadas
void LoadDirectX();        // DirectX e codecs

// ---------- Metricas compartilhadas (texto + graficos) ----------
struct Metricas
{
    int    cpuPct = -1;    // -1 = ainda sem amostra
    double gpuPct = -1;
    int    ramPct = -1;
    double downBps = -1;
    double upBps   = -1;
};
void AmostrarMetricas(Metricas& m);

// ---------- Janela de graficos ----------
HWND CriarJanelaGraficos(HWND pai);
void GraficosTick(HWND hGraficos);        // amostra + redesenha
void GraficosDefinirTema(bool escuro);
void GraficosResetar();

// ---------- NVML (compartilhado entre GPU / sensores / tempo real) ----------
struct NvmlInfo
{
    bool disponivel = false;
    std::wstring nome;
    unsigned int temperaturaC = 0;
    unsigned int usoGpuPct = 0;
    unsigned int usoMemPct = 0;
    unsigned int clockCoreMHz = 0;
    unsigned int clockMemMHz = 0;
    unsigned int larguraBusBits = 0;
    unsigned int potenciaMw = 0;
    unsigned int fanPct = 0;
    unsigned long long vramTotal = 0;
    unsigned long long vramUsada = 0;
    double bandaGBs = 0.0;   // largura de banda teorica da memoria
};
bool NvmlConsultar(NvmlInfo& saida);   // retorna false se nao houver GPU NVIDIA/driver
