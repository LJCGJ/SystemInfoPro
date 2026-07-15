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
| `graphs.cpp` | Gráficos de desempenho em tempo real (GDI) |
| `info_extra.cpp` | PCI, hotfixes, variáveis de ambiente, compartilhamentos |
| `info_extra2.cpp` | Drivers, segurança (AV/TPM/Secure Boot), tarefas agendadas, DirectX/codecs |
| `benchmark.cpp` | Benchmark de CPU, RAM e disco (roda em thread separada) |
| `snapshot.cpp` | Snapshot do sistema e comparação (o que mudou) |
| `diskanalyzer.cpp` | Analisador de espaço em disco (maiores pastas/arquivos) |
| `netdiag.cpp` | Diagnóstico de rede (ping, DNS, IP externo, qualidade) |
| `sensorlog.cpp` | Log histórico de sensores em CSV |
| `manage.cpp` | Gerenciamento ativo (finalizar processo, serviços, inicialização, reabilitar) |
| `info_summary.cpp` | Resumo do Sistema (dashboard) e opção "Iniciar com o Windows" |
| `sensors_all.cpp` | Central de Sensores completa (todos os componentes via LibreHardwareMonitor) |
| `icon.cpp` | Ícone do app desenhado em tempo real com GDI (janela, barra de tarefas, bandeja) |
| `icon.svg` | Versão vetorial do ícone (para o repositório GitHub) |

## Novidades da versão 2.2

- **🎨 Ícone próprio**: monitor com gráfico ascendente em azul, desenhado em tempo real — aparece na janela, na barra de tarefas e na bandeja, sem precisar de arquivo externo. Uma versão SVG acompanha o projeto.
- **🎛 Central de Sensores (Todos)**: nova categoria que lista **todos** os sensores de **todos** os componentes — temperatura do processador (por núcleo), chipset, VRM, memória (quando há sensor), tensões, ventoinhas, potência, clocks e uso — agrupados por peça. Requer o LibreHardwareMonitor aberto (gratuito), e o app lê tudo dele via WMI sem instalar driver próprio.

### Por que preciso do LibreHardwareMonitor para as temperaturas?

O Windows **não expõe** as temperaturas de CPU, chipset, VRM e memória por API oficial — lê-las exige um driver em modo kernel assinado (o mesmo motivo pelo qual HWiNFO e AIDA64 instalam um driver). Como o SystemInfoPro é open source e não assina driver, ele lê esses sensores do LibreHardwareMonitor (também gratuito e open source) quando ele está rodando. Basta abri-lo como Administrador e todos os sensores aparecem automaticamente aqui.

## Novidades da versão 2.1

- **🏠 Resumo do Sistema**: dashboard aberto ao iniciar, com SO, CPU, RAM, GPU, disco, uptime e atalhos.
- **🚫 Inicialização Desabilitada**: lista os itens desabilitados pelo app; botão direito para reabilitar (o "desabilitar" agora tem volta).
- **🔁 Iniciar com o Windows**: opção no menu Exibir para o próprio app iniciar minimizado na bandeja.
- **Correções de estabilidade**: o Analisador de Disco agora roda em segundo plano (não trava mais a janela), com suporte a caminhos longos e proteção contra recursão profunda; benchmark, rede e disco encerram suas threads com segurança ao fechar o programa.

## Ferramentas ativas (menu Ferramentas + categoria "Ferramentas")

- **🚀 Benchmark**: CPU single/multi-thread, RAM e disco, com referências para comparar.
- **📊 Analisador de Disco**: escaneia uma pasta/unidade e mostra as maiores pastas e arquivos.
- **🌐 Diagnóstico de Rede**: ping/latência a vários servidores, DNS, IP público e avaliação da conexão.
- **📸 Snapshot**: salve o estado do sistema e compare depois (o que mudou).
- **⏺ Log de Sensores**: grava CPU/RAM/GPU/temperatura/rede em CSV ao longo do tempo.
- **Gerenciamento ativo**: clique com o botão direito em processos (finalizar), serviços (iniciar/parar) e itens de inicialização (desabilitar). Todas as ações pedem confirmação e há proteção para processos críticos do Windows.
| `app.rc` | Informações de versão do executável |
| `LICENSE` / `README-GITHUB.md` | Kit para publicar no GitHub (MIT, bilíngue) |

## Recursos da interface

- **Busca**: campo no topo filtra a categoria atual em tempo real.
- **Ordenação**: clique no cabeçalho das colunas para ordenar (clique de novo inverte).
- **Copiar**: botão direito em qualquer linha → copiar linha, valor ou tudo.
- **Tema Escuro/Claro**: botão na parte inferior esquerda.
- **Exportar**: TXT ou HTML formatado.
- **Sensores completos**: se o LibreHardwareMonitor estiver aberto, as temperaturas por núcleo da CPU, tensões e RPM dos coolers aparecem automaticamente em "Sensores Térmicos".
