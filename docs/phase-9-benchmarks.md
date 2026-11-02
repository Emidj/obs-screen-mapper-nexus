# OBS Screen Mapper — fase 9

Fecha: 27/10/2026. Mediciones reales en un host aislado de libobs 32.2.2 / D3D11, Windows x64, AMD Ryzen 7 9700X, NVIDIA RTX 5070, controlador 32.0.16.2101. No se instaló el plugin en el OBS del usuario ni se modificaron sus perfiles.

## Cambios

- Buffers de vértices GPU persistentes por zona. Se reconstruyen al cambiar destino, UV, tamaño del canvas o geometría; se conservan ante cambios de nombre, selección y máscara. El borrado libera los buffers de zonas eliminadas; el cierre libera todos. Un fallo de creación permite reintentar en el siguiente render.
- Superficies trianguladas inmutables compartidas: los cambios de metadatos conservan los cálculos de malla. La comparación incluye parámetros, controles, Polygon y errores de validación.
- La consulta del zoom/desplazamiento del editor devuelve solo esos valores y evita copiar el layout completo. Este cambio no tiene una medición separada de latencia de interfaz.
- Exportación de presets: cada ruta de imagen se lee y valida una vez por operación, conservando deduplicación por contenido y validación del formato según su uso. No se mantiene caché entre guardados; los archivos modificados o ausentes se vuelven a comprobar.

## Método y límites

Comparación con las DLL de fase 8 conservadas antes de editar. Tres rondas de cada versión, las rondas 2 y 3 alternadas; otra ronda comprueba el núcleo final. Doce casos: 1/6/24/96 rectángulos, malla, Polygon + malla, máscara elíptica, salida 1080p/2160p y dos renders de una fuente. Controles de malla 8 × 6; el caso Polygon + malla de 24 zonas genera 225792 vértices. La entrada sintética es una única fuente OBS de cuatro colores con alfa parcial, de 1920 × 1080. No incluye decodificación, cámaras, captura, filtros de terceros, codificación ni streaming.

Por caso y ronda: primer render separado, 32 renders de calentamiento y 120 muestras en bloques de ocho. CPU es el tiempo alrededor de obs_source_video_render, excluyendo la preparación del destino externo. GPU es el intervalo de timestamps D3D11 alrededor de esa llamada: puede incluir huecos de envío de CPU; no representa porcentaje de uso GPU ni trabajo exclusivamente ocupado. Los resultados no predicen FPS de una escena real.

Las actualizaciones se solicitan 36 veces, con seis de calentamiento y 30 muestras. Se alterna la selección sin cambiar geometría. La métrica tick→update mide desde el callback global de tick hasta la señal update de la fuente; incluye recorrido del motor y actualización, pero excluye la espera hasta el siguiente tick. No mide edición de geometría ni arrastre completo. Las optimizaciones reducen trabajo repetido; la serialización y lectura de configuraciones siguen creciendo con el número de zonas.

La tabla usa la mediana de los p95 de las tres rondas, en milisegundos. No se han establecido intervalos de confianza ni condiciones térmicas controladas. CSV y logs íntegros en benchmarks/phase9/.

| Caso | CPU p95 base → optimizada | Intervalo GPU p95 base → optimizado | Tick→update p95 base → optimizada |
|---|---:|---:|---:|
| rect_1_1080 | 0.0653 → 0.0206 | 0.0647 → 0.0333 | 0.0551 → 0.0477 |
| rect_6_1080 | 0.1056 → 0.0109 | 0.1547 → 0.0336 | 0.1881 → 0.1709 |
| rect_24_1080 | 0.2705 → 0.0236 | 0.3575 → 0.0461 | 0.5525 → 0.5331 |
| rect_96_1080 | 1.1525 → 0.0428 | 1.2633 → 0.0585 | 2.1794 → 2.4793 |
| mesh_6_1080 | 0.4851 → 0.0056 | 0.5786 → 0.0428 | 1.0782 → 0.3229 |
| mesh_24_1080 | 2.0169 → 0.0198 | 3.5016 → 0.0640 | 4.8261 → 1.1711 |
| polygon_mesh_6_1080 | 0.9070 → 0.0057 | 0.9834 → 0.0462 | 2.0022 → 0.2821 |
| polygon_mesh_24_1080 | 3.5335 → 0.0216 | 6.8300 → 0.0685 | 8.1621 → 1.2501 |
| mesh_mask_24_1080 | 1.9574 → 0.0226 | 3.1637 → 0.0625 | 4.7405 → 1.1056 |
| rect_6_2160 | 0.0692 → 0.0133 | 0.1477 → 0.0921 | 0.1686 → 0.1403 |
| polygon_mesh_24_2160 | 3.5473 → 0.0197 | 6.7301 → 0.1700 | 8.2726 → 1.2318 |
| two_views_mesh_24 | 3.9984 → 0.0309 | 4.0112 → 0.1234 | 4.7521 → 1.1223 |

## Costes y riesgos conservados

Los buffers trasladan trabajo al primer render o a una edición de geometría. En 24 zonas Polygon + malla a 1080p, la mediana del primer render pasa de 3,524 a 9,390 ms; no se promete una mejora de ese primer render. Los datos de vértices de ese caso requieren aproximadamente 6,89 MiB de payload GPU adicional (225792 × 32 bytes); es una estimación por estructura, excluye copias CPU, alineación y memoria del controlador. No se ha medido VRAM total ni demostrado ausencia de fugas.

Las actualizaciones sin malla no mejoran de forma uniforme: en 96 rectángulos el p95 mediano pasa de 2,179 a 2,479 ms. Se conserva este resultado y no se presenta toda operación como acelerada. La caché sigue siendo local a cada instancia. Dos vistas vuelven a renderizar la entrada dos veces; no se añade caché global por fotograma.

En la biblioteca se midieron 24/96 zonas, 100 entradas, una máscara PNG compartida de 512 × 512 y controles 8 × 6. Por operación: tres calentamientos y 30 muestras; una serie antes y otra después. Exportar baja de 51,670 a 4,733 ms p95 en 24 zonas y de 208,011 a 12,995 ms en 96. Validar/materializar/cargar no se han optimizado: fluctúan alrededor de 3–7,4 ms; listar 100 entradas queda por debajo de 0,4 ms p95. El cache de archivos puede influir. No representa bibliotecas enormes, imágenes de 4096 píxeles ni el límite de 96 MiB. Las operaciones siguen siendo síncronas y pueden bloquear la interfaz con otros datos.

## Validación

Los siete ejecutables funcionales terminaron correctamente: 312 comprobaciones de ciclo de vida, 224 GPU, 13 Polygon, 19 malla, 15 imágenes/máscaras, 74 editor y 42 presets: 699 comprobaciones en total. Incluyen comprobaciones auxiliares repetidas, no 699 escenarios independientes. Se añadieron regresiones de reutilización de buffers, cambios UV/curvatura, metadatos, borrado y reutilización de identidad de zona.

Cada caso optimizado compara todos los canales de su imagen con la referencia anterior: cero diferencias en los 12 casos, también con el binario final. Se comprueban exactamente 153 renders de entrada por caso de una vista y 306 en el de dos vistas; se conserva una captura de la fuente existente por render del mapper.

Pendiente: prueba visual y de vídeo/cámara dentro de OBS real, streaming/codificación, HDR, otros equipos y controladores, distintos DPI, cambios de dispositivo gráfico y sesiones prolongadas. Los resultados del host aislado no sustituyen esas pruebas. No se ha creado instalador en esta fase.

## Reproducción

Ejecutar build.ps1 y build-ui.ps1 con las dependencias locales configuradas. Después, benchmark.ps1 -Label optimized y benchmark-presets.ps1. El primer script acepta -Module para otra DLL y -Label baseline para generar referencias PNG; debe ejecutarse primero esa referencia antes de comparar una versión optimizada. No usar baseline para etiquetar una versión optimizada si se desea conservar la referencia anterior.

Para reconstruir la base, el paquete incluye src/ y ui/ de fase 8 en docs/benchmarks/phase9/baseline-source/, además de los hashes de las DLL medidas. Los benchmarks son herramientas de desarrollo externas al funcionamiento del plugin. No se distribuyen como procesos auxiliares del producto.

