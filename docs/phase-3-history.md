# OBS Screen Mapper — fase 3

Fuente nativa C++17 para OBS Studio 32.x, Windows x64. Tipo INPUT; ID estable: obs_screen_mapper. Fases 1–3 implementadas. No se ha instalado en el OBS del usuario.

## Zonas y modos

Se empieza por una zona dinámica. Se pueden añadir, duplicar, eliminar, renombrar, ocultar y reordenar zonas desde las propiedades nativas. No hay límite de seis ni máximo fijo en el modelo; el límite práctico del equipo se medirá en fase 9. Se puede eliminar la última zona y conservar el estado vacío.

- Split: divide la entrada en franjas horizontales iguales según el orden de las zonas. Las ocultas conservan su franja para no desplazar las demás.
- Repeat: muestra la entrada completa en cada destino.
- Custom UV: recorte individual normalizado (X, Y, ancho, alto entre 0 y 1). Los recortes se conservan al cambiar de modo.

Los destinos son rectángulos normalizados del canvas. Se reparten inicialmente en horizontal. Añadir, duplicar, eliminar o reordenar recalcula ese reparto mientras sea automático. Editar coordenadas de destino desactiva el reparto automático hasta elegir «Distribuir destinos en horizontal».

En colocación manual, duplicar conserva el destino y puede superponer zonas. Se componen en orden de lista: las últimas encima de las anteriores. «Acción de zona» ejecuta una operación y vuelve a «Sin acción». Esta interfaz nativa es provisional; el editor visual pertenece a la fase 7.

## Entrada, render y persistencia

La entrada es una fuente OBS existente referenciada por UUID. Renombrarla conserva el enlace. Se declaran dependencias activas y se rechazan ciclos directos e indirectos. Sin entrada válida o sin zonas visibles, la salida es transparente. El canvas empieza en 1920 × 1080, configurable entre 16 y 16384 por dimensión, sujeto a la capacidad real de la GPU.

La instancia existente se renderiza una vez por render del mapper a una textura GPU RGBA reutilizable. Todas las zonas muestrean esa textura. No hay nuevos decodificadores, reproductor propio ni lectura CPU de píxeles en el plugin. Dos renders del mapper en el mismo frame pueden volver a capturar la entrada; todavía no hay caché global por frame ni benchmark.

Se conserva el alfa premultiplicado con ONE / INVSRCALPHA. El modelo se publica como un estado inmutable sin copiar el vector de zonas por frame. La textura cambia de tamaño con la entrada.

El array zones guarda IDs, nombres, visibilidad, destinos y UV. También se guardan distribución, colocación automática y selección. Esquema inicial mapping_schema = 1. Los ajustes de fases 1/2 sin array se abren con una zona; un array vacío se conserva vacío. Las coordenadas se acotan y los IDs ausentes o duplicados se reparan al cargar.

Crédito exacto de propiedades: a plugin from DjEmiMas, con enlace en DjEmiMas.

## Mockup aprobado

La referencia vigente es docs/references/approved-mockup.png, aportada el 26/10/2026: fuente/distribución/zonas a la izquierda, preview central grande, propiedades contextuales a la derecha y salida debajo. Seis zonas son un ejemplo. Polygon y Smooth Mesh deberán ser combinables. El escenario será un fondo de referencia del editor, no contenido de la salida. La imagen no autoriza reproductor ni decodificación propios.

## Compilación y validación — 27/10/2026

Ejecutar build.ps1. Acepta parámetros Zig, ObsSdk y ObsInstall con rutas locales por defecto. No descarga herramientas. Compilación C++17 con Zig 0.15.2 / Clang, O2, Wall/Wextra/Werror y encabezados OBS 32.2.2. Los procesos de compilación y prueba no forman parte de la ejecución del plugin.

Ambos ejecutables terminaron con código 0: 312 comprobaciones del host de ciclo de vida y 53 del host D3D11, sin fallos. Incluyen comprobaciones repetidas; no son 365 escenarios independientes. Los registros están en docs/validation/.

El host de ciclo de vida repite 50 veces creación, propiedades, tamaños, guardado/carga y destrucción; también prueba referencias y ciclos. Al correr sin gráficos, libobs emite avisos de recursos sin contexto. Los avisos de nombre/UUID duplicado durante save/load provienen de mantener viva la fuente original al cargar una copia.

El host gráfico inicializa D3D11 en NVIDIA GeForce RTX 5070 y comprueba:

- Lectura RGBA en canvas 64 × 64: transparencia y alfa parcial premultiplicado.
- Patrón de cuatro cuadrantes para distinguir Split, Repeat y Custom UV.
- Zona oculta sin redistribuir las restantes y márgenes transparentes en destino manual.
- Añadir, duplicar, eliminar, reordenar y renombrar mediante callbacks de propiedades.
- Persistencia JSON y guardado/carga nativo OBS, incluida la entrada existente.
- Doce zonas con una sola captura, sin huecos ni alfa duplicado en reparto automático.
- Eliminar todas las zonas y volver a añadir una.
- Rechazo de ciclos directos e indirectos al cambiar la entrada durante la ejecución.
- Desconexión de una entrada eliminada y cierre del host sin fallo.

Límites: pruebas aisladas con libobs y GPU SDR. No validan visualmente la ventana OBS, cámara/vídeo real, HDR, streaming prolongado ni rendimiento. No se afirma todavía ausencia de fugas ni estabilidad prolongada.

SHA256 de la DLL:
140045269D172DA0E8560BD36D8754520FEA4E61DD547DCAA81F613630248C71

## Fases pendientes

4. Polygon.
5. Smooth Mesh, curvatura rápida y Soft Selection.
6. Máscaras.
7. Editor visual de tres paneles.
8. Presets y persistencia ampliada.
9. Benchmarks y optimización.
10. Instalador y distribución.

Sin Source Pool complejo, Twitch Core, efectos superfluos ni procesos externos de ejecución. Código SPDX GPL-2.0-or-later; se añadirá el texto completo de licencia y los avisos antes de distribución pública. docs/phase-2-history.md conserva la documentación histórica anterior y no describe el estado actual.
