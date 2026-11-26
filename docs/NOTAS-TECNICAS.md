# Notas técnicas

Todas las direcciones son de `USM.exe` 1.0 de PC (base 0x400000). Cada parche
comprueba los bytes originales antes de escribir; si no coinciden, no se
aplica y se anota en `%TEMP%\usm_mod.log`.

## Cómo se carga

- `binkw32.dll` (proxy, `src/bootstrap`): el juego la importa al arrancar.
  Reenvía las funciones a `binkw32_original.dll` y en `DllMain` aplica los
  parches en memoria. No usa la CRT.
- `LoadLibraryA("…\dinput8.dll")` se redirige a `usm_native_input.dll`
  (`src/input`), que sustituye a DirectInput: teclado, ratón, mandos Sony por
  Raw Input/HID y mandos XInput.
- Ajustes: `usm_mod.ini` `[Graficos]` junto a `USM.exe`. Las variables de
  entorno `USM_PC_*` tienen prioridad (útil para pruebas).

## Parches en el archivo (instalador)

| Dirección | Cambio |
|---|---|
| 0x50EF30 | Idioma de voces fijo en inglés. |
| 0x62EF5E | Textos en español sin depender del idioma de voces (opcional). |
| Cabecera PE | `LARGE_ADDRESS_AWARE` (hasta 4 GB en Windows de 64 bits). |

Antes de parchear, el instalador revisa qué idiomas trae la copia: voces en
`Data\sound\pc\STREAMS\STREAMS_VOICE_XX.WBK` y textos en
`Data\packs\pc\GLOBALTEXT_<IDIOMA>.PCPACK` + `Data\usm_lt?.usm`. Forzar un idioma
cuyos archivos faltan deja el juego parado en la primera pantalla de carga, así
que cada parche solo se aplica si sus archivos existen (la edición de EE. UU.
solo trae inglés).

## Distancia y carga (`draw_distance.hpp`, `streaming.hpp`)

- Tablas de distancia por clase 0x921E48 / 0x921E88: se dejan intactas (las
  usa el cargador para asignar clases, 0x4CB45E) y los operandos de dibujo
  0x53D1B2, 0x51E8B9, 0x51E8E6 y 0x53A2E7 apuntan a copias escaladas.
- Distancia de malla 167 (0x88A430, 0x53CF0A, 0x53D381) y radio 167²
  (0x88A46C) escalados; tope x7.
- Fundido: 0x53D1B6 (fundido fijo de 10 unidades) pasa a ser proporcional,
  `clamp(0.25·L, 10, 150)`.
- Zonas residentes 8 → 32 (0x921E40, 0x9222D4+0x10, 0x55544C), ranuras de
  streaming x4, caché de regiones cargadas y tablas por distrito ampliadas, y
  radio de búsqueda de paquetes hasta 1200 (0x54EC50). Basado en
  `ExtremeDrawDistance.ixx` de FusionFix.

## Población (`population.hpp`)

- Radios de aparición 0x937FA4 / 0x937FA8 (inicializados en 0x6BC126,
  0x6D1CF2 y 0x6D1CFC) y el corte de 55 m (0x8A5AF4).
- Peatones (`cmp ebp,0Ah` en 0x6D1A98) y coches (`mov esi,1Dh` en 0x6D1D49,
  tope densidad×30 en 0x6D81DA).

## Estabilidad (`stability.hpp`, `streaming.hpp`)

- 0x52A130 copiaba todos los personajes activos a una tabla fija de 32
  (0x95CB50) sin comprobar el límite; con más gente se desbordaba sobre su
  contador. 0x565BA0 hacía lo mismo con una de 15 (0x95D420). Se trasladan a
  tablas de 512 y 256 y los anexos comprueban la capacidad.
- 0x4D6050: guarda para esqueletos sin pose calculada (puntero nulo).
- Reservas con un solo bloque (0x544300, 0x502F60) pueden crecer.

## Pantalla (`bootstrap.cpp`, `d3d9_refresh.hpp`)

- `ModoPantalla` 0/1/2 (ventana, sin bordes, pantalla completa) y
  `Resolucion`. Se intercepta la lectura de `Settings\Resolution` (llamadas
  desde 0x41CED0 / 0x41CF15) y Direct3D 9 se crea en ventana o en modo
  exclusivo según el ajuste. Profundidad D16 → D24S8, plano cercano y
  filtrado anisotrópico.

## Avisos e iconos (`src/input`)

- 0x60A4A0 sustituye fichas como `~cross` por nombres de tecla; se interceptan
  sus 12 llamadas a 0x609580 para devolver fichas en mayúsculas, que 0x609BF0
  convierte en caracteres de la fuente de botones. Al dibujar (vtable
  0x48EBB4 y llamada 0x37E778), cada carácter se asocia a una acción y se
  dibuja el icono del dispositivo en uso: tecla dibujada, PS5, PS2 en color o
  Xbox.
- El aviso de pausa fijo (`RESUME` de `Data\usm_lt?.usm`, 11 llamadas a
  0x81C580) se cambia por un icono.

## Máscaras PS2 (`mask_ab.hpp`, `mask_generated.hpp`)

- Si existen `mask-0dc218aa.bgra` / `mask-6064aa59.bgra` (extraídas de la ISO
  del usuario), se usan. Si no, se generan: anillo y agujero redondeado
  descritos con unos pocos parámetros, con el nivel 32×32 dibujado aparte como
  en PS2 y el resto de niveles promediados.
