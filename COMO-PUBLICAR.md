# Como compilar e publicar o SystemInfoPro

Guia completo do código-fonte até o instalador na sua página.

---

## Passo 1 — Compilar o programa

1. Abra `SystemInfoPro.sln` no **Visual Studio 2019/2022** (com a carga "Desenvolvimento para desktop com C++").
2. No topo, selecione **Release** e **x64**.
3. Menu **Compilar → Compilar Solução** (Ctrl+Shift+B).
4. O executável fica em: `build\x64\Release\SystemInfoPro.exe`

Teste esse `.exe` antes de empacotar — ele já funciona sozinho (portátil).

---

## Passo 2 — Embutir o ícone no .exe (opcional, mas recomendado)

Sem esse passo, o `.exe` funciona e mostra o ícone na janela e barra de tarefas, mas no **Windows Explorer** aparece um ícone genérico. Para o ícone próprio aparecer em todo lugar:

1. Rode o `SystemInfoPro.exe` uma vez.
2. Menu **Ajuda → 🎨 Gerar arquivo de ícone (app.ico)** e salve como `app.ico` **na pasta do projeto** (junto do `app.rc`).
3. Abra `app.rc` e **descomente** a linha:
   ```
   IDI_APP ICON "app.ico"
   ```
   (remova as duas barras `//` do início)
4. **Recompile** (Ctrl+Shift+B). Agora o `.exe` tem o ícone embutido.

---

## Passo 3 — Criar o instalador com o Inno Setup

1. Baixe e instale o **Inno Setup**: https://jrsoftware.org/isdl.php
2. Coloque o arquivo `installer.iss` **na pasta raiz do projeto** (onde estão `LICENSE`, `LEIA-ME.md` e a pasta `build`).
3. Abra `installer.iss` no Inno Setup.
4. Edite no topo do script (opcional):
   - `MyAppVersion` — a versão (ex.: "2.2")
   - `MyAppURL` — o link do seu GitHub/página
   - Se gerou o `app.ico`, descomente a linha `SetupIconFile=app.ico`
5. Clique em **Build → Compile** (ou F9).
6. O instalador pronto sai em: `Output\SystemInfoPro_Setup.exe`

Esse é o arquivo único que você sobe para a sua página. Ao rodar, ele instala o programa, cria atalhos no Menu Iniciar e (opcionalmente) na Área de Trabalho, e adiciona ao "Adicionar ou Remover Programas".

---

## Passo 4 — Publicar

Suba o `SystemInfoPro_Setup.exe` para a sua página de download (ou nas "Releases" do GitHub).

### ⚠ Sobre o aviso do Windows SmartScreen

Como o instalador **não é assinado digitalmente**, o Windows pode mostrar um aviso azul "O Windows protegeu o computador" na primeira execução. Isso é normal para todo software gratuito sem certificado. O usuário clica em **"Mais informações → Executar assim mesmo"**.

Para remover esse aviso definitivamente seria preciso um **certificado de assinatura de código** (pago, ~US$ 100-400/ano de uma autoridade como Sectigo/DigiCert). Para um projeto open source gratuito, a maioria dos autores simplesmente documenta isso e deixa o código aberto no GitHub para gerar confiança.

---

## Checklist rápido

- [ ] Compilou em Release / x64 e testou o `.exe`
- [ ] (Opcional) Gerou `app.ico`, descomentou no `app.rc` e recompilou
- [ ] Colocou `installer.iss` na raiz do projeto
- [ ] Ajustou versão e URL no `installer.iss`
- [ ] Compilou o instalador no Inno Setup (F9)
- [ ] Testou o `SystemInfoPro_Setup.exe` numa máquina limpa
- [ ] Subiu para a sua página / GitHub Releases

---

## Estrutura de pastas esperada

```
SystemInfoPro\
├── SystemInfoPro.sln
├── SystemInfoPro.vcxproj
├── installer.iss           <- script do Inno Setup
├── LICENSE
├── LEIA-ME.md
├── app.rc
├── app.ico                 <- gerado pelo app (passo 2)
├── main.cpp, icon.cpp, ... (todos os .cpp e .h)
└── build\x64\Release\SystemInfoPro.exe   <- gerado pela compilacao
```
