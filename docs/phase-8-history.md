# OBS Screen Mapper — fase 8

Fuente nativa C++17 para OBS Studio 32.x, Windows x64. Tipo INPUT; ID estable obs_screen_mapper. Fases 1–8 implementadas y compiladas. Validación actual: libobs 32.2.2, D3D11 y host Qt aislado. No se ha instalado ni modificado el OBS del usuario.

## Editor visual

Se abre desde las propiedades de la fuente con «Abrir Mapper Editor…». Las propiedades normales muestran entrada, tamaño del canvas y acceso al editor; los controles numéricos anteriores se pueden desplegar como opción avanzada.

La referencia aprobada está en docs/references/approved-mockup.png. Se conserva su estructura de tres paneles y el estilo oscuro con acentos azules:

- Izquierda: fuente OBS existente, Split/Repeat/Custom UV y lista dinámica de zonas. Añadir, duplicar, eliminar, renombrar, ocultar y reordenar.
- Centro: preview GPU en vivo, selección por contorno, movimiento de zonas, edición de vértices Polygon y puntos Smooth Mesh, zoom, ajuste al lienzo, desplazamiento del lienzo y cuadrícula. Las zonas se numeran y el punto seleccionado se resalta.
- Derecha: nombre/visibilidad y pestañas Geometría, Máscara y Región UV. Se muestran los controles de geometría que corresponden a la herramienta activa.
- Abajo: tamaño del canvas, indicación de salida transparente, Cancelar y Aplicar.

El editor comienza con el estado de la fuente y usa una copia de trabajo y una fuente privada de preview. Aplicar guarda los cambios en la fuente original; Cancelar los descarta. El fondo de referencia solo existe en el editor. La sesión de deshacer/rehacer admite hasta cien estados y agrupa cada gesto de arrastre. Ctrl+Z/Ctrl+Y actúan como deshacer/rehacer; Supr elimina la zona cuando el foco está en la lista o el preview. Los campos de texto conservan su edición normal.

Las herramientas Polygon y Editar malla habilitan la geometría correspondiente si aún no está activada. Las dos geometrías son independientes y se pueden combinar. Al arrastrar Polygon con malla activa se aproxima la transformación inversa local para conservar la correspondencia entre cursor y punto. La selección suave usa radio, fuerza y caída smoothstep. Los sliders de curvatura se suman a los desplazamientos manuales.

El preview reutiliza la instancia OBS elegida. La referencia puede activarse para que la entrada produzca vídeo, como sucede con una previsualización de OBS; no se crea otro reproductor ni otro decodificador. La actualización de la copia de trabajo se limita a unos 30 envíos por segundo. No es un benchmark de rendimiento.

El fondo de referencia, zoom, desplazamiento, cuadrícula, herramienta y selección del editor se conservan al Aplicar. La cabecera ofrece biblioteca de presets, Guardar y Cargar; el menú Presets añade Guardar como, Importar y Exportar. Seis zonas solo se usan como ejemplo en la vista de pruebas: el inicio normal sigue siendo una zona y no hay máximo fijo de seis.

## Motor de fuente

Entrada por UUID con dependencias activas declaradas a OBS y prevención de referencias circulares directas e indirectas. Se incluyen fuentes de vídeo y escenas en los selectores. Sin entrada válida o sin zonas visibles: salida transparente.

Canvas 1920 × 1080 por defecto, entre 16 y 16384 por dimensión, sujeto a capacidad real de la GPU. La fuente existente se captura una vez por render del mapper. Todas las zonas reutilizan la textura de entrada. El resultado se compone en una textura del tamaño del canvas antes de entrar en la escena: las deformaciones no dibujan fuera de sus límites. No hay caché global por frame ni benchmark de consumo todavía.

Alfa premultiplicado ONE / INVSRCALPHA. Modelo y triángulos publicados como estado inmutable; reconstrucción al editar/cargar y render GPU. No hay lectura de píxeles a CPU en el flujo normal. El método de lectura GPU del preview solo se invoca desde la prueba aislada para generar evidencia.

## Zonas y geometría

Una zona inicial; se puede eliminar la última y volver a añadir una. Añadir, duplicar, ocultar, eliminar y reordenar conserva las identidades y los ajustes individuales. No hay máximo fijo de zonas; el límite práctico se medirá en fase 9.

- Split: franjas horizontales iguales de la entrada por orden, incluidas las zonas ocultas para evitar desplazamientos del contenido.
- Repeat: entrada completa por zona.
- Custom UV: recorte individual normalizado, conservado al cambiar de modo.

Los destinos se reparten automáticamente en horizontal hasta editar sus coordenadas o mover una zona. «Distribuir» recupera el reparto automático. En colocación manual, duplicar conserva el destino y puede superponer zonas; las últimas se componen encima.

Polygon: contornos simples convexos/cóncavos con posición y UV por vértice, inserción, borrado con mínimo tres y ambos sentidos del contorno. Los contornos cruzados o degenerados se conservan para corregirlos y dejan transparente la zona.

Smooth Mesh: inicialmente 4 × 4 controles, ajustables entre 2 y 16 filas/columnas para acotar la reconstrucción. Interpolación Catmull–Rom bicúbica o bilineal. Cambiar densidad remuestrea el campo; conserva campos afines y aproxima los no lineales. La geometría se aproxima con entre 8 y 32 subdivisiones por arista de cada triángulo base. Polygon y malla se deforman juntos y conservan las UV.

Curvatura horizontal desplaza Y según X; vertical desplaza X según Y. Perfiles arco 4t(1-t) y onda sin(2πt), con intensidad entre -0.5 y 0.5. Soft Selection usa distancia local de cuadrícula, radio 0–1.5 y fuerza 0–1. La fuerza también escala el movimiento del control seleccionado. El restablecimiento de curvatura conserva las ediciones de puntos.

Las caras invertidas/colapsadas detectadas en la teselación se rechazan. Es una comprobación local, no una garantía contra toda superposición global extrema. HDR y antialiasing avanzado no se han validado.

## Máscaras y persistencia

Máscara por zona: ninguna, elipse, rectángulo o imagen PNG/SVG. Coordenadas locales previas a la deformación, independientes de las UV de contenido. Alfa o luminancia × alfa, inversión e intensidad. Bordes suaves para formas básicas; en imágenes se usa su alfa.

PNG mediante Windows Imaging Component, de 1 a 4096 píxeles por dimensión. SVG mediante Direct2D/WARP transitorio durante la carga, a 1024 × 1024, respetando viewBox y tamaños explícitos. Compatibilidad limitada al SVG admitido por Direct2D; no se garantiza soporte completo de documentos complejos, tipografías externas o filtros avanzados.

Los recursos se comparten por ruta/revisión dentro de cada instancia; se cargan al cambiar o recargar, no por fotograma. Elegir Recargar relee el archivo en esa zona. La carga es síncrona y su latencia queda pendiente de medición. Fallos de carga producen transparencia y se registran en OBS. Intensidad cero omite la máscara.

Esquema mapping_schema = 4, compatible con las fases anteriores. Se guardan zonas, identidad, visibilidad, destinos, UV, ambas geometrías, controles, curvatura, Soft Selection y máscaras. Los triángulos se reconstruyen. Las imágenes de máscara se guardan como rutas externas: no se incrustan. Un array de zonas vacío se conserva vacío.

## Compilación

1. Ejecutar build.ps1: núcleo Zig 0.15.2 / Clang, C++17, O2, Wall/Wextra/Werror, encabezados OBS 32.2.2; construye y ejecuta las regresiones del motor.
2. Ejecutar build-ui.ps1: MSVC 19.44 / Visual Studio 2022, Qt Widgets del SDK local y CMake. Construye el módulo de editor y el host de pruebas, y ejecuta sus comprobaciones.

Ambos scripts admiten rutas de herramientas/dependencias como parámetros y no descargan nada. Los valores por defecto corresponden al entorno local detectado. El host de editor se ha probado con las DLL Qt 6.11.1 que acompañan al OBS instalado, compilando contra el SDK Qt 6.8.3 disponible.

Se entregan dos DLL: obs-screen-mapper.dll y obs-screen-mapper-ui.dll, juntas en dist/obs-screen-mapper/bin/64bit/. El núcleo carga el editor al solicitarlo y lo mantiene cargado durante el proceso. Cada ventana libera su preview y sus referencias al cerrar. La interfaz usa el Qt que proporciona OBS; no se redistribuyen DLL Qt ni se ejecutan procesos auxiliares en el flujo normal. El host de prueba es una herramienta de desarrollo, no parte del funcionamiento del plugin.

## Evidencia — 27/10/2026

Los siete ejecutables de validación terminaron con código 0:

- 312 comprobaciones de ciclo de vida libobs.
- 201 comprobaciones D3D11 del motor, incluido recorte de una fuente deformada dentro de una escena mayor.
- 13 comprobaciones de Polygon, 19 de malla y 15 de carga de máscaras.
- 74 comprobaciones del editor Qt con motor, presets y preview GPU.
- 42 comprobaciones de formato portable, biblioteca y persistencia.

Los recuentos incluyen comprobaciones repetidas y verificaciones de controles, no escenarios independientes. Registros en docs/validation/.

La prueba del editor comprueba copia de trabajo, añadir zonas, edición por arrastre Polygon/malla y Soft Selection, deshacer/rehacer, renombrado Unicode, máscaras y UV contextuales, reordenar, ocultar, más de seis zonas, fondo de referencia sin alterar la fuente, Cancelar, Aplicar con tamaño de canvas, prevención de ciclos con escenas, propiedades compactas y apertura de la DLL del editor desde el botón nativo de la fuente. También verifica frames del display, lectura del renderer y contenido de vídeo presente en ese frame, además del cierre limpio del host.

La vista docs/validation/editor-phase8.png está generada por el programa de pruebas: combina el render de controles Qt con el frame leído de su propio renderer D3D11. No es una captura del OBS del usuario. Usa una fuente procedural de prueba y seis zonas de ejemplo. La evidencia visual procede del renderer de la aplicación de prueba y de sus controles Qt.

Límites: validación aislada con fuente de prueba, libobs y GPU SDR. Falta validar la integración visual dentro de OBS real con vídeo/cámara, sesiones prolongadas, diferentes DPI/monitores, todos los gestos de arrastrar filas en la lista y consumo GPU/CPU. El host aislado registra un intento no disponible de cargar libobs-winrt; no usa captura WinRT ni una fuente de pantalla. El host sin gráficos conserva los avisos de contexto documentados en fases anteriores; los save/load de copias vivas producen avisos de nombre/UUID duplicados. No se ha demostrado ausencia de fugas ni estabilidad prolongada.

SHA256 núcleo:
B25CC4742401BEB2851D6F00EED62B7AA95DC4D0A64B7FFC4CCB031F5309E55D

SHA256 editor:
91457131EDC7F45985B292AD87E0F08440A91E7E4FFF5752FFCD38DC8025A611

## Presets y persistencia ampliada — fase 8

Guardar crea o actualiza el preset seleccionado; Guardar como crea una identidad nueva. Cargar reemplaza solo la copia de trabajo. Cargar se puede deshacer/rehacer y Cancelar conserva la configuración original de la fuente. Guardar o Importar en la biblioteca son operaciones explícitas sobre archivos: sus resultados permanecen en la biblioteca aunque después se cancele la edición de la fuente.

El selector de biblioteca no carga automáticamente al elegir un nombre; Cargar confirma esa elección. Los nombres duplicados reciben un sufijo para diferenciarlos. No se ha añadido eliminación de presets ni limpieza automática de revisiones.

El formato portable es un único archivo .mapper.json con marcador obs-screen-mapper-preset, versión 1. Guarda canvas, distribución, zonas, nombres/visibilidad, destinos, UV, Polygon, Smooth Mesh, controles, curvatura, Soft Selection, máscaras y preferencias del editor. Mantiene la fuente OBS actual del destino: no exporta ni aplica UUID de entrada ni crea reproductores. La identidad del preset en una biblioteca local tampoco se exporta.

Las máscaras PNG/SVG y el fondo PNG/JPG/BMP se incluyen como bytes Base64 con SHA256. Se conservan los archivos originales y se deduplican los recursos repetidos. Las rutas absolutas originales se sustituyen por identificadores de recurso. Al importar, primero se valida el documento completo y después se extraen los recursos en una carpeta assets controlada por el plugin. Las configuraciones aplicadas usan esas rutas del cache, por lo que siguen disponibles al mover los archivos originales. Al volver a exportar se incluyen de nuevo los bytes de esos recursos.

La biblioteca normal reside en la carpeta de configuración del módulo OBS, subcarpeta presets. El núcleo transmite esa ruta a la interfaz mediante una entrada ABI v2; deben usarse juntas las dos DLL de este paquete. Las pruebas usan OBS_MAPPER_PRESET_DIR para aislar todos los datos dentro del proyecto. Ese valor es una opción de diagnóstico; no se han escrito presets en el perfil real de OBS.

Cada guardado escribe una revisión de datos independiente cuyo nombre incorpora su hash. La sustitución atómica del índice library.json es el punto de confirmación. Si falla la escritura del índice, este sigue apuntando a la última revisión confirmada. Las revisiones anteriores y los recursos extraídos se conservan; no se borran automáticamente. Si se corrompe el índice se informa del error y no se sobrescribe silenciosamente; los archivos .mapper.json siguen en disco para importarlos manualmente a una biblioteca reparada.

Importación/exportación validan marcador, versiones, tipos, rangos, identidades de zona, topología de controles, integridad y formato de imágenes, referencias y presupuesto de geometría. No se aceptan rutas arbitrarias dentro de los identificadores de recursos ni imágenes incluidas sin referencias. Los contornos geométricamente inválidos pero editables pueden guardarse si sus datos numéricos son válidos; el motor mantiene sus avisos y su salida transparente para permitir corregirlos.

Límites del intercambio portable: archivo JSON de hasta 96 MiB; imágenes de hasta 32 MiB cada una y 64 MiB en conjunto; SVG hasta 16 MiB; imágenes raster hasta 4096 × 4096; contornos de hasta 512 vértices; presupuesto acumulado de un millón de puntos de datos y dos millones de vértices derivados. Estos presupuestos de importación no son un máximo de seis zonas ni un benchmark de zonas admitidas por el motor. Los límites de soporte SVG/SDR de la fase 6 siguen vigentes.

Si falta una imagen seleccionada o falla su validación, la exportación o el guardado se detienen con un mensaje y el preset confirmado anterior se conserva. Las versiones futuras no admitidas se rechazan. Los esquemas de mapping anteriores y los diseños con cero zonas tienen pruebas de compatibilidad. Un archivo de importación inválido no altera la copia de trabajo ni inicia la extracción de imágenes.

La carga y validación de presets es síncrona. Todavía no se ha medido su latencia con bibliotecas grandes ni se ha optimizado su uso de memoria; estas mediciones corresponden a fase 9. Se ha probado un fallo de escritura con el índice en solo lectura; no se ha simulado una pérdida física de alimentación.

## Evidencia adicional de fase 8

Las pruebas del editor guardan una configuración con Polygon + malla + máscara + Custom UV y preferencias de vista, la exportan, la vuelven a cargar con deshacer/rehacer, mueven las imágenes originales e importan de nuevo desde los bytes incluidos. Verifican conservación de la fuente de entrada, cancelación, aplicación y recuperación de geometría, zoom, pan, cuadrícula y fondo al reabrir. También comprueban que un guardado con una imagen inexistente no sustituye el preset anterior.

Las pruebas de biblioteca/formato verifican Unicode, deduplicación, hashes, nombres diferenciados, persistencia al reabrir, actualización frente a Guardar como, fallo de guardado atómico, fallo de confirmación del índice, rechazo de JSON/versiones/rangos/topologías/recursos no válidos, bloqueo de referencias que salgan del cache, importación sin modificar datos ante errores y compatibilidad con layouts antiguos o vacíos.

El ejemplo docs/examples/presets/escenario-curvo-6-zonas.mapper.json ha sido exportado por la API real del editor. Es un escenario de muestra y requiere que el usuario elija su propia fuente OBS. No cambia el inicio normal de una zona ni establece límite de zonas. La vista del editor incluida muestra ese ejemplo con una fuente procedural de prueba, no una sesión real de OBS.
## Fases pendientes

9. Benchmarks y optimización.
10. Instalador y distribución.

La fase 9 no ha comenzado. Sin Source Pool complejo, Twitch Core ni efectos superfluos. Crédito exacto: a plugin from DjEmiMas, enlazando solo DjEmiMas a su Discord. Los documentos phase-*-history.md son históricos. Código SPDX GPL-2.0-or-later; el texto completo y los avisos de licencia se incorporarán antes de distribución pública.

