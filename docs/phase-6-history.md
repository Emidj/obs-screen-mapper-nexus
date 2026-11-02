# OBS Screen Mapper — fase 6

Fuente nativa C++17 para OBS Studio 32.x, Windows x64. Tipo INPUT; ID estable obs_screen_mapper. Fases 1–6 implementadas. No se ha instalado en el OBS del usuario.

## Entrada y salida

Se selecciona una instancia OBS existente y se persiste por UUID. Cambiar su nombre conserva el enlace. Se declaran las dependencias activas a OBS, con rechazo de ciclos directos e indirectos. Sin entrada válida o sin zonas visibles, la salida es transparente.

Canvas configurable, 1920 × 1080 por defecto, de 16 a 16384 por dimensión sujeto a capacidad GPU. La instancia se captura una vez por render del mapper a una textura RGBA reutilizable. Todas las zonas reutilizan esa textura, sin nuevos decodificadores, reproductor propio ni lectura CPU de píxeles. Aún no existe caché global por frame ni benchmark de consumo.

El render usa alfa premultiplicado ONE / INVSRCALPHA. El modelo de zonas y sus triángulos se publican como un estado inmutable. La geometría se reconstruye al editar/cargar, no en cada frame. Los procesos auxiliares son solo de compilación y pruebas, no de ejecución del plugin.

## Zonas y distribución

Una zona inicial. Añadir, duplicar, eliminar, renombrar, ocultar y reordenar desde propiedades nativas. Se puede eliminar la última y recuperar una después. No hay máximo fijo de zonas en el modelo ni límite de seis; el límite práctico se medirá en fase 9.

- Split: franjas horizontales iguales de la entrada por orden. Las ocultas conservan su franja.
- Repeat: entrada completa en cada destino.
- Custom UV: recorte individual normalizado. Se conserva al cambiar de modo.

Destinos rectangulares normalizados. El reparto automático horizontal se recalcula al añadir, duplicar, eliminar o reordenar. Editar destinos lo desactiva hasta elegir «Distribuir destinos en horizontal». En colocación manual, duplicar puede superponer zonas; las últimas se dibujan encima.

## Polygon y Smooth Mesh

Las dos activaciones son independientes por zona. Pueden usarse por separado o juntas. Polygon define contorno y UV locales; la malla deforma esa geometría conservando la asignación de UV.

Polygon admite contornos simples convexos o cóncavos en ambos sentidos. Permite seleccionar, editar, insertar y eliminar vértices (mínimo tres), con coordenadas X/Y y U/V entre 0 y 1. Contornos cruzados o degenerados se conservan para corregirlos, muestran un aviso y dejan transparente la zona.

Smooth Mesh tiene inicialmente 4 × 4 puntos de control. Filas y columnas se ajustan entre 2 y 16, límites de esta implementación para acotar el coste de reconstrucción, independientes de la cantidad de zonas. Cambiar la resolución remuestrea el campo de desplazamientos; conserva campos afines y aproxima campos no lineales.

Se interpolan desplazamientos mediante Catmull–Rom bicúbico, con extrapolación lineal en los bordes. Desactivar «Interpolación suave» usa interpolación bilineal. Las UV se interpolan en la superficie teselada. El GPU dibuja una aproximación triangular de la curva, no una superficie analítica infinita: entre 8 y 32 subdivisiones por arista de cada triángulo base según la densidad de controles.

Cada control se selecciona por fila/columna. Sus desplazamientos X/Y están entre -1 y 1 en unidades locales. La deformación puede extenderse fuera del destino original y se recorta por el canvas. Esta interfaz es provisional; la manipulación por arrastre corresponde al editor de fase 7.

## Curvatura rápida y Soft Selection

La curvatura se suma a los desplazamientos manuales, con perfil de arco o de onda y controles horizontal/vertical independientes entre -0.5 y 0.5:

- Horizontal: desplaza Y según la posición X.
- Vertical: desplaza X según la posición Y.
- Arco: 4t(1-t). Onda: sin(2πt).

Soft Selection distribuye el movimiento de un control a sus vecinos mediante caída smoothstep por distancia en las coordenadas locales de la cuadrícula original. Radio entre 0 y 1.5; fuerza entre 0 y 1. El radio no se mide en píxeles. Con fuerza inferior a 1, el control seleccionado también recibe solo esa fracción del movimiento solicitado; el campo vuelve a mostrar su desplazamiento resultante. Radio cero afecta solo al seleccionado. Fuera del radio no se modifica ningún punto.

Se pueden restablecer puntos, curvatura o toda la malla por separado. Restablecer curvatura conserva las ediciones de puntos. Desactivar cualquiera de las dos geometrías conserva sus datos.

La reconstrucción rechaza caras invertidas o colapsadas de la superficie teselada: muestra un aviso y deja transparente la zona. Es una comprobación local de triángulos; no garantiza detectar todas las posibles superposiciones globales de una deformación extrema. El render es SDR; HDR y antialiasing avanzado siguen sin validarse.

## Persistencia

Esquema mapping_schema = 4. Por zona se guardan identidad, visibilidad, destinos, UV, Polygon, filas/columnas, desplazamientos, suavizado, curvatura y parámetros de Soft Selection. Los triángulos derivados se reconstruyen. Se conserva compatibilidad con ajustes anteriores: sin zonas se inicia una; un array vacío permanece vacío; sin geometría adicional se mantiene el rectángulo anterior.

La duplicación conserva ambas geometrías, curvatura y Soft Selection, con un nuevo ID de zona. Los valores inválidos de malla muestran aviso al activarla. Los restablecimientos permiten recuperar una configuración válida.

## Máscaras por zona — fase 6

Cada zona admite Sin máscara, Elipse, Rectángulo o Imagen PNG/SVG. La máscara usa coordenadas locales previas a la deformación, independientes de las UV de contenido. Sigue a Polygon y Smooth Mesh cuando se combinan, también con Split, Repeat y Custom UV.

Las imágenes se interpretan por alfa, o por luminancia RGB multiplicada por alfa al activar esa opción. Se puede invertir la cobertura. La intensidad mezcla entre la imagen original (0) y la cobertura completa (1). El resultado multiplica tanto RGB premultiplicado como alfa una sola vez. Elipse y Rectángulo admiten borde suave normalizado entre 0 y 0.25; para imágenes, el borde suave procede del alfa incluido en el archivo.

PNG se decodifica con Windows Imaging Component a RGBA recto; admite entre 1 y 4096 píxeles por dimensión. SVG usa el rasterizador nativo de Direct2D, con dispositivo WARP transitorio durante la carga y rasterización a 1024 × 1024. Se respetan el viewBox y los tamaños explícitos del documento. El SVG rasterizado se convierte de BGRA premultiplicado a RGBA recto antes de subirlo como textura de máscara. No se distribuyen bibliotecas nuevas de terceros ni se lanzan procesos externos durante la ejecución.

SVG está limitado a las capacidades de Direct2D del sistema. Se han probado paths, rectángulos, opacidad, tamaños explícitos y viewBox; no se ha establecido compatibilidad con todas las características SVG, tipografía externa, filtros avanzados ni documentos complejos de herramientas de diseño.

Los recursos se cargan al aplicar ajustes, se comparten entre zonas de una misma instancia por ruta/revisión y se mantienen en GPU mientras se usan. No hay lectura de archivos ni rasterización de máscaras en cada fotograma. Cambiar el contenido de un archivo conservando su ruta requiere elegir «Recargar archivo» en la zona; otras zonas pueden conservar su revisión anterior. La carga es síncrona al cambiar o recargar el recurso, y su latencia queda pendiente de medición en fase 9.

La persistencia guarda tipo, ruta, modo alfa/luminancia, inversión, intensidad, suavizado y revisión. No incrusta el archivo: si se mueve o desaparece habrá que volver a enlazarlo. Duplicar conserva todos los ajustes. Las fuentes de fases anteriores se abren sin máscara.

Si el archivo falta, está corrupto, supera los límites o no se puede crear su recurso gráfico, la zona queda transparente. Los errores de carga se registran en OBS. Intensidad cero omite la máscara, incluso si el archivo no está disponible. Desactivarla conserva sus ajustes.

## Evidencia adicional de fase 6

La prueba de carga nativa verifica PNG con ruta Unicode, alfa recto, SVG rasterizado con escala correcta y alfa parcial, archivos inexistentes/corruptos, formatos no admitidos y PNG sobredimensionado. La prueba GPU verifica elipse/rectángulo, inversión, intensidad, borde parcial, PNG y SVG, luminancia × alfa, caché y recarga explícita, máscaras independientes por zona y su combinación con Polygon, Smooth Mesh y los modos de distribución. Una fuente guardada y recargada recrea sus texturas y produce exactamente los mismos píxeles que la fuente original.

La DLL final y los cinco ejecutables terminaron su compilación/validación con éxito el 27/10/2026. Los recuentos incluyen comprobaciones repetidas. Se conservan los límites anteriores: pruebas aisladas SDR con libobs/D3D11; falta validación visual en OBS con vídeo/cámara real y sesión prolongada. La fase 7 no ha comenzado.

Ejemplos de máscaras probadas en docs/examples/masks/. Los registros reproducibles están en docs/validation/.
## Mockup y siguientes fases

La referencia aprobada sigue en docs/references/approved-mockup.png: fuente/distribución/zonas a la izquierda, preview central grande, propiedades contextuales a la derecha. Las seis zonas son un ejemplo. El escenario es un fondo del editor, no contenido de salida. No se ha construido aún la ventana mostrada en esa imagen.

Pendiente: fase 7 editor visual; fase 8 presets; fase 9 benchmarks/optimización; fase 10 instalador/distribución. No se ha iniciado la fase 7. Sin Source Pool complejo, Twitch Core ni efectos superfluos. Crédito exacto en propiedades: a plugin from DjEmiMas, con enlace en DjEmiMas.

## Compilación y validación — 27/10/2026

Ejecutar build.ps1. Acepta Zig, ObsSdk y ObsInstall como parámetros, con rutas locales por defecto. No descarga herramientas. Zig 0.15.2 / Clang, C++17, O2, Wall/Wextra/Werror y encabezados/libobs 32.2.2.

Los cinco ejecutables terminaron con código 0, sin comprobaciones fallidas:

- 312 comprobaciones del host de ciclo de vida.
- 198 comprobaciones del host D3D11.
- 13 comprobaciones de Polygon.
- 19 comprobaciones del motor de malla.
- 15 comprobaciones de carga nativa de máscaras.

Los recuentos incluyen comprobaciones repetidas, no escenarios independientes. Registros en docs/validation/. Se conserva la regresión de fases anteriores: vida de fuentes, ciclos, persistencia, modos, doce zonas, contornos cóncavos, alfa y captura compartida.

Las nuevas pruebas geométricas cubren identidad, interpolación de controles, conservación afín, remuestreo, diferencia bicúbico/bilineal, radio/fuerza/caída suave, curvas, área teselada y rechazo de caras colapsadas o datos inválidos.

Las pruebas D3D11 sobre NVIDIA GeForce RTX 5070 verifican lectura de píxeles, neutralidad y alfa sin costuras de la malla inicial, curvaturas positivas/negativas, traslación mediante controles, cambio de densidad conservando un campo afín, callbacks de Soft Selection, curvatura y Polygon cóncavo combinados, perfiles, Split/Repeat/Custom UV, captura única, duplicación y guardado/carga OBS de todos los parámetros.

El host sin gráficos emite avisos de recursos sin contexto. Save/load mantiene viva la fuente original al cargar una copia, de donde proceden los avisos de nombre/UUID duplicados. No hay validación visual de una ventana real OBS, cámara/vídeo real, sesión prolongada, estabilidad en stream, fugas ni rendimiento.

SHA256 de la DLL de fase 6:
E018727368B07F1964DAF014F7622222F367BC1FD6989FFD959F485F0F41A869

Los documentos phase-*-history.md son históricos, no el estado actual. Código SPDX GPL-2.0-or-later; el texto completo y los avisos de licencia se incorporarán antes de distribución pública.

