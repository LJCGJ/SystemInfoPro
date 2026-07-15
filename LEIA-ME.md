# SystemInfoPro (C++ / Win32)

Versão em C++ nativo do seu painel de informações do sistema — estilo SIW, muito mais completa que a versão C#.

## Como compilar (Visual Studio 2019 ou 2022)

1. Instale o **Visual Studio Community** com a carga de trabalho **"Desenvolvimento para desktop com C++"** (inclui o SDK do Windows 10/11).
2. Coloque **todos os arquivos deste pacote na mesma pasta**.
3. Dê dois cliques em `SystemInfoPro.sln`.
4. Se o Visual Studio perguntar sobre "Retarget/Atualizar projetos", aceite (ele ajusta para o seu SDK/toolset instalado).
5. No topo, selecione **Release** e **x64**.
6. Menu **Compilar → Compilar Solução** (Ctrl+Shift+B).
7. O executável sai em `build\x64\Release\SystemInfoPro.exe`.

O programa **pede elevação de Administrador automaticamente** (manifesto embutido). Isso é necessário para ler SMART dos discos, chave do Windows e alguns sensores.

## O que ele mostra

**Sensores em Tempo Real**
- Painel de monitoramento (atualiza a cada 1s): CPU total e **por núcleo** (com barras gráficas), RAM física/virtual, **uso da GPU**, VRAM em uso, temperatura da GPU, clocks, consumo em watts (NVIDIA), **velocidade de download/upload ao vivo**, uptime e processos ativos.
- Sensores térmicos: GPU (NVML), zonas ACPI da placa-mãe, temperatura dos discos.

**Hardware**
- CPU: modelo via CPUID, caches L1/L2/L3 reais, **conjuntos de instruções** (SSE/AVX/AVX2/AVX-512/AES-NI...), virtualização.
- Placa-mãe/BIOS: fabricante, modelo, série, versão do BIOS, **UEFI vs Legacy**.
- GPU: adaptadores via **DXGI** (VRAM real mesmo acima de 4 GB, dedicada/compartilhada, uso atual), driver via WMI e, em placas **NVIDIA (NVML)**: temperatura, clocks, **largura do barramento** e **largura de banda da VRAM em GB/s**.
- Monitores: resolução, taxa de atualização (Hz), **modelo/fabricante via EDID**, ano de fabricação.
- RAM: módulos com tipo DDR3/4/5, frequência nominal x configurada, tensão, slot, **largura de banda por módulo e combinada**.
- Armazenamento: barramento (SATA/NVMe/USB), SSD ou HDD, **SMART completo** (setores realocados, horas ligado, temperatura, avaliação de saúde) e **saúde NVMe** (desgaste %, total lido/gravado em TB, desligamentos inseguros).
- USB, áudio, impressoras, bateria (saúde da célula: capacidade de fábrica x atual).

**Software**
- Windows (edição, build, chave de ativação, data de instalação), programas instalados (64/32 bits + por usuário), inicialização, processos (top 60 por RAM), serviços (status + modo), contas locais.

**Rede**
- Adaptadores: tipo (Ethernet/Wi-Fi/VPN), MAC, IPv4/IPv6, gateway, DNS, DHCP, MTU, velocidade do link.
- **Conexões TCP ativas** com nome do processo dono de cada conexão.

Botão **Exportar Relatório (.txt)** salva a tela atual em UTF-8.

## Limitações conhecidas (iguais às do SIW gratuito)

- **Temperatura por núcleo da CPU**: exige driver em modo kernel (como o HWiNFO usa). O programa mostra o que as APIs oficiais permitem (ACPI, NVML, SMART).
- **Sensores detalhados de GPU AMD/Intel**: o uso em tempo real funciona (contadores do Windows), mas clocks/banda detalhados só em NVIDIA (NVML). Suporte AMD (ADL) pode ser adicionado depois.

## Arquivos do projeto

| Arquivo | Conteúdo |
|---|---|
| `main.cpp` | Janela, árvore, lista, timer, exportação |
| `common.h` | Utilitários e declarações compartilhadas |
| `wmi.h/.cpp` | Ajudante de consultas WMI |
| `realtime.cpp` | Painel de monitoramento (PDH, GetIfTable2, NtQuerySystemInformation) |
| `info_cpu.cpp` | CPU (WMI + CPUID + caches) |
| `info_gpu.cpp` | GPU (DXGI + NVML + WMI) e monitores |
| `info_ram.cpp` | Memória RAM e largura de banda |
| `info_board.cpp` | Placa-mãe, BIOS, sensores, USB, áudio, impressoras, bateria |
| `info_storage.cpp` | Discos, SMART ATA, saúde NVMe, volumes |
| `info_network.cpp` | Adaptadores e conexões TCP |
| `info_software.cpp` | SO, programas, inicialização, processos, serviços, usuários |
