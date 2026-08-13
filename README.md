Conquer KayanK - Diário de Desenvolvimento (Status Fase 1)Visão GeralA KayanK é uma engine customizada desenvolvida em C++ (Win32) e DirectX 11. O objetivo é contornar o client legado da TQ Digital e renderizar o mundo isométrico do Conquer Online nativamente usando aceleração por hardware.Atualmente, o projeto está numa fase de prototipagem rápida. O código principal está um pouco monolítico no Conquer.cpp, mas já começamos a isolar as responsabilidades de Window, Graphics e Resource.1. Renderização (Graphics & D3D11)A engine abandonou o GDI/renderização de CPU e já opera um pipeline 3D real usando shaders (HLSL).Widescreen Nativo: O resize da janela está interceptando o evento do Windows e reconstruindo os buffers do DX11 em tempo real. A tela revela mais área do mapa ao invés de apenas achatar/esticar a imagem.Tratamento de Z-Buffer:Z-Write On: Usado no cenário e na malha base dos personagens para lidar com a profundidade isométrica.Z-Write Off (Z-Read Only): Implementado especificamente para as partículas e asas. Isso resolveu o problema de "recorte" onde objetos transparentes apagavam o que estava atrás deles.Blending: Suporte a Alpha Blend comum e Additive Blend (usado para o brilho do núcleo das partículas e magias não escurecer o fundo).2. Resource Manager (Desempacotador e Parser)O módulo de recursos faz a leitura direta dos arquivos binários do jogo original.Leitor de .WDF: Consegue montar o índice do pacote, buscar o hash e carregar os arquivos (DDS, C3, ANI) direto pra memória sem precisar extrair pro disco.Mapas (.DMAP / .PUL): Faz o parsing do grid matemático do jogo e traduz as coordenadas lógicas para a tela 2.5D.Malhas e Animações (.C3 / .MOT): Extrai vértices, normais, pesos e converte a trilha de quaternions da animação original para Matrizes de Rotação.Correção de Efeitos (3DEffect.ini / C3Ptcl): A engine já lê os scripts de magia. Implementamos uma correção no momento do load para aplicar um "Pitch" de 90 graus nas partículas (como as asas), resolvendo o problema dos modelos originais virem deitados no eixo Z.3. Gameplay Loop (Conquer.cpp)O loop principal controla o fluxo de dados entre o parser e o renderizador.Delta Time: FPS independente. A movimentação e os timers (pulo, animação) usam a diferença de tempo real, então o jogo roda na mesma velocidade a 60 ou 300 FPS.Física Pseudo-3D: Usamos a tela 2D para movimento, mas calculamos uma parábola matemática no eixo Z (jumpZ) para simular a altura do pulo por cima do grid.Attach Bones (Soquetes): Sistema que lê a hierarquia do osso. Conseguimos pendurar armas nas mãos e asas na coluna (Point04), fazendo com que o objeto herde a matriz de rotação e movimento do corpo em tempo real.Combate Base: Lógica de state machine rudimentar. O personagem persegue, aguarda cooldown, bate, levanta o dano flutuante e aplica o state de morte no monstro (com interpolação de Alpha para sumir).4. Gargalos e Próximos Passos (Fase 2)O protótipo provou que a renderização funciona, mas precisamos resolver alguns problemas arquiteturais para o jogo escalar:CPU Skinning: A interpolação das animações (multiplicação de matrizes dos ossos) está sendo calculada no processador a cada frame dentro do GetInterpolatedBone. Para renderizar dezenas de jogadores na tela, precisamos mover isso para um Vertex Shader (GPU Skinning).Refatoração do Main: O Conquer.cpp está gigantesco. Precisamos migrar as entidades e a lógica de combate para um ECS (Entity Component System) na dll de Game.Pathfinding Real (A):* O jogador anda em linha reta ignorando obstáculos. Precisamos cruzar a lógica do mouse com a matriz de colisão do arquivo .DMap usando o algoritmo A-Star.Network: Conectar as lógicas de spawn, dano e IDs de equipamento com os pacotes TCP da Network.dll.

5. Here are the shortcuts:
6. Number 1: You turn into a small woman
7. Number 2: You turn into a large woman
8. Number 3: You turn into a small man
9. Number 4: You turn into a large man
10. Number 5: You remove all his weapons
11. Number 6: You add a level 130 blade
12. Number 7: You add level 130 dual blades
13. Number 8: You add a blade and a shield
14. Number 9: You equip a bow
15. Number 0: You display the wings
16. For the letter 'E', you add an effect; there is a list of effects in the code.](https://github.com/YKayanK1/ConquerKayanK)

17.	![Texto Alternativo](x64/Debug/foto1.png)
18.	![Texto Alternativo](x64/Debug/foto2.png)
19.	![Texto Alternativo](x64/Debug/foto3.png)
20.	![Texto Alternativo](x64/Debug/foto4.png)
21.	![Texto Alternativo](x64/Debug/foto5.png)
22.	![Texto Alternativo](x64/Debug/foto6.png)
23.	![Texto Alternativo](x64/Debug/foto7.png)

](https://github.com/YKayanK1/ConquerKayanK)
