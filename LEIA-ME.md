# MangaDock 0.1 — protótipo para PSP 3000

**Estado desta entrega:** código-fonte do leitor para PSP + importador funcional em Python + receita de compilação. **Não inclui EBOOT.PBP pronto.** O leitor ainda não foi compilado nem executado em PSP/PPSSPP nesta entrega. Não basta copiar este ZIP para o PSP. O importador passou por 9 testes automatizados no Linux; sua interface Windows e a importação RAR ainda precisam de teste.

## O que foi implementado

- Biblioteca por pastas, ordenação numérica natural (2 antes de 10) e marcação de favoritos com estrela.
- Leitura de imagens soltas ou páginas dentro de ZIP/CBZ, carregando uma página por vez.
- Última página salva por volume; abrir qualquer imagem de uma pasta retoma o volume dessa pasta.
- Ajuste à tela ou à largura, zoom de 1 a 3 vezes e rotação em passos de 90 graus.
- Deslocamento com direcional/analógico; modo manga inicia páginas ampliadas pelo lado direito.
- Interface em português, fonte com acentos e leitura offline.
- Importador de vários volumes no computador, preservando os originais e recusando sobrescrever um CBZ existente.

Favoritos são marcadores visuais nesta versão: ainda não existe filtro de favoritos. Também não há download de mangás, busca por título, capas, catálogo online, sincronização ou OCR. A biblioteca não exclui nem move seus arquivos.

## Compatibilidade implementada

| Entrada | Leitor PSP (código ainda não validado no aparelho) | Importador no computador |
|---|---|---|
| CBZ / ZIP | Direto, com imagens suportadas | Reorganiza e otimiza as imagens |
| CDZ | Apenas se o conteúdo for realmente ZIP | Mesmo critério; não é um decodificador CDZ genérico |
| JPEG / PNG / BMP / TGA | Direto, em pastas ou ZIP | Converte para JPEG |
| GIF | Primeira imagem, sem animação | Primeiro quadro |
| PDF | Não abre diretamente | Renderiza cada página para JPEG e gera CBZ |
| CBR / RAR | Não abre diretamente | Requer pacote rarfile e ferramenta UnRAR instalada |
| CBT / TAR | Não abre diretamente | Importa imagens regulares do arquivo |
| WebP / TIFF | Não abre diretamente | Converte; TIFF multipágina preserva páginas |
| CB7 / 7Z / EPUB / DJVU | Não implementado | Não implementado |
| Arquivos com senha / DRM | Não suportados | Não suportados |

CBZ é um ZIP de imagens. Renomear um PDF para CBZ não converte seu conteúdo. ZIP com WebP deve passar pelo importador antes de ir ao PSP. Fontes incomuns de PDF, PDFs danificados e variações de formatos podem causar erro: não há promessa de compatibilidade universal.

## 1. Preparar os mangás no Windows

1. Extraia todo este ZIP para uma pasta do computador.
2. Instale Python 3.11 ou superior de https://www.python.org/downloads/windows/ com o **Python Launcher** e o suporte a Tcl/Tk.
3. Abra a pasta `desktop` e dê dois cliques em `ABRIR_WINDOWS.bat`. Na primeira execução ele cria um ambiente Python e baixa as dependências; precisa de internet no computador.
4. Clique em **Escolher pasta de saída**. Pode escolher uma pasta local ou a pasta `MANGA` na raiz do cartão do PSP conectado por USB.
5. Mantenha a largura em **960**, que preserva detalhes para o zoom. Use 720 ou 480 para arquivos menores. A altura máxima é 2200 pixels; páginas muito compridas serão reduzidas, sem corte em blocos nesta versão.
6. Clique em **Importar arquivos** para selecionar PDF, CBZ etc., ou **Importar pasta** para um diretório de páginas.
7. Aguarde o resultado. Cada entrada vira um CBZ separado. Se aparecer erro, o diálogo informa qual volume falhou. Os volumes concluídos continuam disponíveis.
8. Caso tenha exportado para o computador, copie os CBZ para `MANGA` na raiz do cartão e ejete o dispositivo antes de desconectar.

Para CBR/RAR, além de `rarfile` (instalado pelo BAT), é preciso instalar o UnRAR oficial e deixar seu executável disponível no PATH. Essa rota não foi testada aqui. Uma alternativa é extrair o CBR no computador com um programa compatível e usar **Importar pasta**.

Não encerre à força o importador nem desconecte o cartão durante a escrita. Se uma transferência for interrompida pelo sistema, remova apenas o CBZ incompleto de saída e importe novamente; os arquivos de origem não são alterados.

Uso opcional pelo terminal:

```powershell
python desktop/importer.py "C:\Mangas\Volume 1.pdf" --out "E:\MANGA" --width 960
```

## 2. Gerar o aplicativo do PSP

Esta é a etapa pendente que impede a instalação imediata. O ambiente desta entrega não tinha PSPDEV e o acesso ao download do compilador expirou. A receita abaixo foi preparada, mas ainda precisa executar com sucesso.

### Pelo GitHub Actions

1. Crie um repositório seu no GitHub.
2. Coloque **o conteúdo** da pasta MangaDock na raiz desse repositório, incluindo a pasta `.github`. O arquivo `.github/workflows/build.yml` deve ficar exatamente nesse caminho, e `psp` deve estar na raiz.
3. Abra a aba **Actions**, selecione **Build PSP** e clique em **Run workflow**. O workflow também executa quando houver mudanças em `psp`.
4. Aguarde o resultado. Se a compilação falhar, abra **Compile with PSPDEV** e guarde o log: a receita pode precisar de ajustes à versão do SDK.
5. Se terminar com sucesso, baixe o artefato **MangaDock-PSP**, disponível no final da página dessa execução. Ele deverá conter `PSP/GAME/MangaDock/EBOOT.PBP` e a fonte.

Nenhum repositório foi criado e nenhuma compilação remota foi executada por esta entrega. A receita usa `pspdev/pspdev:latest`, conforme o guia oficial; a imagem deve ser fixada por digest depois da primeira compilação validada para permitir reprodução exata.

### Alternativa: Docker no computador

Com Docker instalado e iniciado, abra o PowerShell **na pasta MangaDock** e execute:

```powershell
docker run --rm -v "${PWD}:/source" -w /source/psp pspdev/pspdev:latest sh -c "psp-cmake -S . -B build && cmake --build build -j2"
```

O resultado esperado é `psp/build/EBOOT.PBP`. A compilação usa C++17, SDL2, SDL2_ttf, libzip e stb_image do PSPDEV. A pasta contém também uma configuração para compilação de desenvolvimento no desktop, mas isso não substitui um teste no console.

## 3. Instalar depois de compilar

O alvo inicial é um PSP 3000 configurado para executar homebrew. A compatibilidade com seu firmware precisa ser confirmada. Este pacote não instala desbloqueio nem altera o firmware.

1. Conecte o PSP ao computador por USB.
2. Na raiz do cartão, abra `PSP`, depois `GAME`, e crie `MangaDock`.
3. Coloque nessa pasta o `EBOOT.PBP` compilado e os arquivos `font.ttf` e `FONT-LICENSE.txt` da pasta `psp`.
4. Na raiz do cartão, crie `MANGA`. Coloque ali CBZ/ZIP ou subpastas com imagens.
5. Ejete o dispositivo, desconecte e procure **MangaDock** em **Jogo → Memory Stick**.

A fonte precisa ficar ao lado do EBOOT. O programa cria a pasta `state` no diretório de execução para progresso e favoritos. Mantenha os nomes e caminhos dos volumes para preservar a associação ao progresso salvo.

## Controles

| Botão | Biblioteca | Leitura |
|---|---|---|
| Direcional / analógico | Cima e baixo selecionam | Move a página ampliada |
| X | Abre pasta ou volume | Alterna zoom 1×, 1,5×, 2×, 2,5×, 3× |
| Círculo | Volta uma pasta | Volta à biblioteca |
| Triângulo | Marca/desmarca favorito | Gira 90° |
| Quadrado | — | Alterna tela inteira / largura |
| L / R | — | Página anterior / próxima |
| SELECT | — | Alterna início à direita / esquerda |
| START | — | Oculta ou mostra barras |
| HOME | Sair | Sair |

L/R mantêm anterior/próxima nos dois modos. O modo manga altera a posição inicial de páginas ampliadas; não reordena os arquivos. A ordem é numérica por nome. Renomeie páginas ambíguas antes de importar.

## Limites e testes pendentes

- Leitor: até 3 milhões de pixels por página, cada dimensão até 4096 pixels e arquivo de imagem até 12 MiB. São limites conservadores escolhidos neste protótipo, não garantias de que todas as imagens nesses limites abrirão.
- Importador: até 40 milhões de pixels por imagem, 64 MiB por entrada de imagem e 10.000 páginas por volume. A saída padrão fica em até 960 × 2200, preservando proporção.
- A renderização é por software para evitar depender de texturas de página inteira na GPU do PSP. Velocidade, consumo de memória e suavidade do zoom precisam de medição no PSP 3000.
- Arquivos ZIP são lidos em memória por página; o programa não extrai caminhos do arquivo no cartão.
- O importador não recorta automaticamente webtoons longos. Não preserva texto selecionável nem hiperlinks de PDFs.
- Rotação, zoom, retomada, HOME, USB, falta de espaço, suspensão/retorno e comportamento do cartão precisam de testes reais. Nesta versão, somente página, favorito, direção e rotação são persistidos; zoom e deslocamento não são.
- Teste primeiro com um volume pequeno. O próximo marco é obter um EBOOT compilado, validar em emulador e depois testar no seu PSP com arquivos representativos.

## Validação realizada nesta entrega

`python -m unittest discover -s tests -v`: **9 testes passaram**.

Cobrem PDF com várias páginas e dimensão limitada, ordenação numérica em CBZ, arquivos ocultos, proteção contra sobrescrita, limpeza após falha, TIFF multipágina/transparência, leitura TAR sem extração de caminhos, recusa de PDF com senha, pasta/WebP e formatos vazios ou não implementados.

Não realizados: compilação C++/PSP, interface gráfica Windows, backend UnRAR, PSP real ou PPSSPP. Portanto, a tabela descreve as rotas implementadas no código, não certificação de compatibilidade.

## Fontes técnicas e dependências

- PSPDEV / SDK: https://pspdev.github.io/
- Exemplo oficial de geração e instalação de EBOOT: https://pspdev.github.io/basic_programs.html
- Compilação via Docker: https://pspdev.github.io/installation/docker.html
- Dependências disponibilizadas pelo SDK: https://pspdev.github.io/psp-packages/

O código próprio deste projeto usa a licença MIT, no arquivo LICENSE. Dependências preservam suas próprias licenças. A fonte DejaVu acompanha sua licença em `psp/FONT-LICENSE.txt`. PyMuPDF é uma dependência separada, com licenciamento AGPL/comercial; consulte sua licença antes de redistribuir um executável que o incorpore. O importador instala as dependências pelo pip, não distribui esses pacotes dentro deste ZIP.
