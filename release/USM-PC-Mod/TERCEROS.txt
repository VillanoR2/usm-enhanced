# Terceros

Este proyecto solo contiene código propio, más el material de terceros que
se lista aquí, todo con licencias que permiten redistribuirlo.

## UltimateSpiderMan.FusionFix (ThirteenAG)

- Proyecto: WidescreenFixesPack, carpeta `source/UltimateSpiderMan.FusionFix`
  (https://github.com/ThirteenAG/WidescreenFixesPack).
- Licencia: GNU General Public License v3.0.
- Uso aquí: las direcciones y el enfoque de la distancia de dibujado
  (`src/bootstrap/draw_distance.hpp`) y de la ampliación de carga de la ciudad
  (`src/bootstrap/streaming.hpp`, adaptación de `ExtremeDrawDistance.ixx`)
  se basan en ese proyecto. Por eso todo este repositorio se distribuye bajo
  GPL-3.0.

## Kenney – Input Prompts 1.5

- Autor: Kenney (https://kenney.nl/assets/input-prompts).
- Licencia: Creative Commons Zero (CC0 1.0), dominio público.
- Uso aquí: los iconos de PlayStation y Xbox de `src/input/pad_icons.bin`,
  generados con `tools/make_pad_atlas.py` (recortados, escalados a 64 px y con
  un contorno añadido). No es obligatorio citar a Kenney, pero lo agradecemos.

## Lo que NO se incluye

Ningún archivo de Ultimate Spider-Man (PC ni PS2): ni ejecutables, ni
texturas, ni audio, ni textos, ni datos extraídos. Ver "Qué es tuyo y qué es
nuestro" en el README.
