# OBS Screen Mapper — fase 5

Fuente nativa C++17 para OBS Studio 32.x, Windows x64. Tipo INPUT; ID estable obs_screen_mapper. Fases 1–5 implementadas. No se ha instalado en el OBS del usuario.

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

Esquema mapping_schema = 3. Por zona se guardan identidad, visibilidad, destinos, UV, Polygon, filas/columnas, desplazamientos, suavizado, curvatura y parámetros de Soft Selection. Los triángulos derivados se reconstruyen. Se conserva compatibilidad con ajustes anteriores: sin zonas se inicia una; un array vacío permanece vacío; sin geometría adicional se mantiene el rectángulo anterior.

La duplicación conserva ambas geometrías, curvatura y Soft Selection, con un nuevo ID de zona. Los valores inválidos de malla muestran aviso al activarla. Los restablecimientos permiten recuperar una configuración válida.

## Mockup y siguientes fases

La referencia aprobada sigue en docs/references/approved-mockup.png: fuente/distribución/zonas a la izquierda, preview central grande, propiedades contextuales a la derecha. Las seis zonas son un ejemplo. El escenario es un fondo del editor, no contenido de salida. No se ha construido aún la ventana mostrada en esa imagen.

Pendiente: fase 6 máscaras; fase 7 editor visual; fase 8 presets; fase 9 benchmarks/optimización; fase 10 instalador/distribución. No se ha iniciado la fase 6. Sin Source Pool complejo, Twitch Core ni efectos superfluos. Crédito exacto en propiedades: a plugin from DjEmiMas, con enlace en DjEmiMas.

## Compilación y validación — 27/10/2026

Ejecutar build.ps1. Acepta Zig, ObsSdk y ObsInstall como parámetros, con rutas locales por defecto. No descarga herramientas. Zig 0.15.2 / Clang, C++17, O2, Wall/Wextra/Werror y encabezados/libobs 32.2.2.

Los cuatro ejecutables terminaron con código 0, sin comprobaciones fallidas:

- 312 comprobaciones del host de ciclo de vida.
- 132 comprobaciones del host D3D11.
- 13 comprobaciones de Polygon.
- 19 comprobaciones del motor de malla.

Los recuentos incluyen comprobaciones repetidas, no escenarios independientes. Registros en docs/validation/. Se conserva la regresión de fases anteriores: vida de fuentes, ciclos, persistencia, modos, doce zonas, contornos cóncavos, alfa y captura compartida.

Las nuevas pruebas geométricas cubren identidad, interpolación de controles, conservación afín, remuestreo, diferencia bicúbico/bilineal, radio/fuerza/caída suave, curvas, área teselada y rechazo de caras colapsadas o datos inválidos.

Las pruebas D3D11 sobre NVIDIA GeForce RTX 5070 verifican lectura de píxeles, neutralidad y alfa sin costuras de la malla inicial, curvaturas positivas/negativas, traslación mediante controles, cambio de densidad conservando un campo afín, callbacks de Soft Selection, curvatura y Polygon cóncavo combinados, perfiles, Split/Repeat/Custom UV, captura única, duplicación y guardado/carga OBS de todos los parámetros.

El host sin gráficos emite avisos de recursos sin contexto. Save/load mantiene viva la fuente original al cargar una copia, de donde proceden los avisos de nombre/UUID duplicados. No hay validación visual de una ventana real OBS, cámara/vídeo real, sesión prolongada, estabilidad en stream, fugas ni rendimiento.

SHA256 de la DLL de fase 5:
94DFA813F6E56A8EC2DDF6DC2EBEE39EC8785ABF4E78EC58C9B5B4657754CF20

Los documentos phase-*-history.md son históricos, no el estado actual. Código SPDX GPL-2.0-or-later; el texto completo y los avisos de licencia se incorporarán antes de distribución pública.
