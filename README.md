# Sobrevivendo Em GS — protótipo PSP 0.1

Protótipo homebrew nativo para PSP/PPSSPP, em C usando PSPSDK. Inclui:
- cena 3D básica com terreno, estrada, casas, árvores e personagem provisório;
- movimentação pelo direcional;
- rotação de câmera com L/R;
- variáveis iniciais de vida, fome e dia;
- ações provisórias nos botões X e O.

## Requisitos

- PC Windows, Linux ou máquina virtual Linux;
- PSPSDK instalado e configurado (compilador `psp-gcc`, `psp-config`);
- PPSSPP para testar `EBOOT.PBP`.

O projeto não gera uma ISO pronta sem o toolchain. O alvo normal é `EBOOT.PBP`, formato homebrew que o PPSSPP executa diretamente.

## Compilar

Abra um terminal na pasta do projeto e execute:

```sh
make clean
make
```

Se a compilação terminar, será criado `EBOOT.PBP`.

## Testar no PPSSPP (Android)

1. Copie a pasta `Sobrevivendo_Em_GS` para `PSP/GAME/` no armazenamento do aparelho.
2. Coloque `EBOOT.PBP` dentro dessa pasta.
3. Abra PPSSPP > Jogos > PSP/GAME/Sobrevivendo_Em_GS.
4. Selecione o jogo.

Estrutura esperada:

```text
PSP/
  GAME/
    Sobrevivendo_Em_GS/
      EBOOT.PBP
```

## Sobre ISO/CSO

ISO/CSO são imagens de disco UMD. Um `EBOOT.PBP` homebrew não pode ser transformado em ISO/CSO simplesmente renomeando ou compactando o arquivo. O caminho recomendado para este projeto é executar o PBP diretamente no PPSSPP. Um pacote ISO de disco requer estrutura UMD, metadados e executável PSP apropriado; isso não é produzido por este Makefile.

## Controles

- Direcional: andar
- L / R: girar a câmera
- X: ação provisória de procurar suprimentos
- O: descansar
- START: sair

## Limitações desta versão

É um primeiro protótipo técnico, não um jogo completo. Ainda faltam colisões com prédios, inimigos, inventário real, coleta de recursos, crafting, salvamento, áudio, animações e otimização de cenário. O HUD de texto usa a tela de depuração do PSP e será substituído por uma interface gráfica na próxima versão.
