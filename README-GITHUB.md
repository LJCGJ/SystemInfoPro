# 💻 SystemInfoPro

**[Português](#português) | [English](#english)**

---

## Português

Painel completo de informações e diagnóstico do sistema para Windows, escrito em **C++ nativo (Win32)**. Gratuito e de código aberto (MIT). Sem instalador, sem telemetria, sem anúncios — um único executável leve.

### ✨ Recursos

- **Gráficos em tempo real** de CPU (por núcleo), GPU, RAM e rede — estilo Gerenciador de Tarefas
- **Benchmark integrado**: CPU single/multi-thread, velocidade real da RAM e do disco (sequencial + 4K aleatório)
- **Snapshot e comparação**: salve uma "foto" do sistema e descubra depois o que mudou (programas, drivers, serviços, inicialização, atualizações)
- **SMART completo** dos discos (ATA e NVMe): saúde, desgaste do SSD, TB gravados, temperatura
- **GPU a fundo**: VRAM real via DXGI, e em placas NVIDIA (NVML) clocks, temperatura, consumo e **largura de banda da VRAM**
- **Sensores térmicos**: ACPI, GPU, discos — e integração automática com o **LibreHardwareMonitor** para temperaturas por núcleo da CPU, tensões e coolers
- **Ícone na bandeja com alertas**: RAM cheia, GPU quente, disco quase sem espaço
- **Inventário completo**: hardware, programas, processos, serviços, tarefas agendadas, segurança (antivírus/TPM/Secure Boot), rede com conexões TCP por processo
- **Interface moderna**: tema escuro, busca em todas as categorias, ordenação por coluna, exportação em TXT e **HTML formatado** (categoria atual ou relatório completo)

### 🛠 Compilando

1. Instale o [Visual Studio Community](https://visualstudio.microsoft.com/pt-br/) com a carga **"Desenvolvimento para desktop com C++"**
2. Abra `SystemInfoPro.sln`
3. Selecione **Release / x64** e compile (Ctrl+Shift+B)
4. Executável em `build\x64\Release\SystemInfoPro.exe`

O programa solicita elevação de Administrador (necessário para SMART e sensores).

### 📄 Licença

[MIT](LICENSE) — use, modifique e distribua livremente.

---

## English

Complete system information and diagnostics panel for Windows, written in **native C++ (Win32)**. Free and open source (MIT). No installer, no telemetry, no ads — a single lightweight executable.

### ✨ Features

- **Real-time graphs** for CPU (per core), GPU, RAM and network — Task Manager style
- **Built-in benchmark**: single/multi-thread CPU, real RAM bandwidth and disk speed (sequential + 4K random)
- **Snapshot & compare**: save a system "photo" and later see exactly what changed (programs, drivers, services, startup, updates)
- **Full SMART** for disks (ATA and NVMe): health, SSD wear, TB written, temperature
- **Deep GPU info**: real VRAM via DXGI, plus NVIDIA (NVML) clocks, temperature, power draw and **VRAM bandwidth**
- **Thermal sensors**: ACPI, GPU, disks — with automatic **LibreHardwareMonitor** integration for per-core CPU temperatures, voltages and fans
- **Tray icon with alerts**: high RAM usage, hot GPU, low disk space
- **Complete inventory**: hardware, programs, processes, services, scheduled tasks, security (antivirus/TPM/Secure Boot), network with per-process TCP connections
- **Modern UI**: dark theme, search in every category, column sorting, TXT and **formatted HTML** export (current category or full report)

### 🛠 Building

1. Install [Visual Studio Community](https://visualstudio.microsoft.com/) with the **"Desktop development with C++"** workload
2. Open `SystemInfoPro.sln`
3. Select **Release / x64** and build (Ctrl+Shift+B)
4. Executable at `build\x64\Release\SystemInfoPro.exe`

The app requests Administrator elevation (required for SMART and sensors).

### 📄 License

[MIT](LICENSE) — use, modify and distribute freely.
