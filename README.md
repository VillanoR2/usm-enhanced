# Ultimate Spider-Man (PC) — Mod

Mod para la versión de PC de **Ultimate Spider-Man** (Activision, 2005,
versión 1.0) que mejora el juego con el estilo de la versión de PS2: voces en
inglés con textos en español, mandos modernos con sus iconos, ciudad más
lejana y viva, sombras estilo PS2, pantalla y resolución configurables y
correcciones de estabilidad. Incluye un instalador con ventana que detecta el
juego, analiza tu PC y te deja ajustar cada opción.

> Proyecto de aficionados, sin relación con Activision, Marvel, Treyarch ni
> Sony. Ultimate Spider-Man y sus marcas pertenecen a sus dueños.

## Qué es tuyo y qué es nuestro

**Este repositorio no contiene nada del juego.** Ni ejecutables, ni
texturas, ni modelos, ni audio, ni textos, ni datos extraídos del juego de PC
ni del de PS2. Todo lo que hay aquí es código propio o material de terceros
con licencia libre (ver [THIRD_PARTY.md](THIRD_PARTY.md)).

Lo que pone **cada usuario**, de su propia copia comprada legalmente:

| Recurso | ¿Obligatorio? | Para qué |
|---|---|---|
| Ultimate Spider-Man para PC 1.0 ya instalado | Sí | El mod modifica tu instalación: parchea tu `USM.exe` en tu disco y añade sus DLL. |
| Tu ISO de Ultimate Spider-Man de PS2 | No | Solo si quieres las dos texturas originales de sombras y máscaras de PS2. El instalador las extrae de tu ISO a tu carpeta del juego; la ISO no se modifica ni se copia. Sin ISO, el mod usa unas propias equivalentes. |

Lo que aporta **este proyecto**:

- **Código propio** (GPL-3.0): el proxy `binkw32.dll`, que aplica los cambios
  en memoria al arrancar el juego; `usm_native_input.dll`, con la entrada de
  mandos, los avisos, los iconos y las máscaras generadas; y el instalador.
- **Arte propio o libre**: las teclas de teclado y ratón se dibujan por
  código al arrancar; las máscaras sin ISO se generan con unos pocos
  parámetros de forma (no son copias de las de PS2); los iconos de mandos son
  de Kenney (CC0).
- **Parches**: el instalador cambia unos pocos bytes de **tu** `USM.exe`
  (voces en inglés, textos en español y uso de hasta 4 GB de memoria) y guarda
  antes una copia del original. El resto de cambios se aplica en memoria cada
  vez que arranca el juego, sin tocar más archivos.

Para no parchear nunca una versión equivocada, el código lleva **firmas**:
secuencias de unos pocos bytes de instrucciones (direcciones y valores
esperados) que se comparan con tu `USM.exe` antes de escribir nada, y sumas
SHA-256 que reconocen tu versión del juego y tu ISO. Es la práctica habitual
de los mods (FusionFix hace lo mismo); no son código ni datos del juego que se
puedan usar por sí solos.

## Instalar (usuarios)

1. Descarga la carpeta [`release/USM-PC-Mod`](release/USM-PC-Mod) (o el ZIP
   de la sección Releases, si existe).
2. Ejecuta `Instalar-USM-Mod.exe` y sigue los pasos. Pide permisos de
   administrador porque el juego suele estar en *Archivos de programa*.
3. Juega como siempre. Para cambiar ajustes o desinstalar, vuelve a abrir el
   instalador.

Detalles para usuarios: [docs/LEEME.txt](docs/LEEME.txt).

## Qué incluye

- Voces en inglés con textos en español (o textos originales). El instalador
  detecta si tu copia es la edición solo en inglés o la multilingüe y aplica
  únicamente lo que tus archivos permiten.
- Mandos DualSense, DualShock 4, Xbox y compatibles XInput, con la
  distribución de PS2 y los iconos de cada mando en los avisos; teclas
  dibujadas cuando se juega con teclado y ratón.
- Distancia de dibujado hasta x6 con aparición gradual, 32 zonas de la ciudad
  cargadas (8 en el original), más peatones y tráfico apareciendo más lejos.
- Ventana, ventana sin bordes o pantalla completa, con cualquier resolución.
- Sombras y máscaras estilo PS2, filtrado anisotrópico y menos parpadeo lejano.
- Correcciones de cierres que aparecen al ampliar distancias y multitudes
  (listas internas de tamaño fijo y reservas que se agotaban).

Notas técnicas de cada cambio: [docs/NOTAS-TECNICAS.md](docs/NOTAS-TECNICAS.md).

## Compilar

Requisitos: Windows, Python 3.9+ y Visual Studio 2019 o posterior con la
carga de trabajo **Desarrollo para el escritorio con C++** (herramientas MSVC
x86 y Windows SDK).

```
python build.py            # compila en build/ y genera build/USM-PC-Mod.zip
python build.py --release  # además actualiza release/
```

Los iconos de mandos ya vienen generados en `src/input/pad_icons.bin`. Para
regenerarlos, descarga el paquete de Kenney y usa `tools/make_pad_atlas.py`.

## Estructura

```
src/bootstrap/   binkw32.dll: proxy que parchea el juego en memoria (sin CRT)
src/input/       usm_native_input.dll: mandos, avisos, iconos, máscaras
src/installer/   instalador (ventana Win32 y línea de comandos)
tools/           generador del atlas de iconos de mandos
docs/            LEEME para usuarios y notas técnicas
release/         paquete listo para usar compilado desde estas fuentes
```

## Licencia

GPL-3.0 (ver [LICENSE](LICENSE)). Parte del código, la distancia de dibujado
y la ampliación de carga, se basa en *UltimateSpiderMan.FusionFix* de
ThirteenAG, que también es GPL-3.0. Los iconos de Kenney son CC0.
