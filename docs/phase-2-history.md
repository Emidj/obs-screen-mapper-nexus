# OBS Screen Mapper — fase 2

Plugin nativo C++17, Windows x64, registrado como fuente de entrada OBS
(`OBS_SOURCE_TYPE_INPUT`, ID estable `obs_screen_mapper`).

## Implementado

- Registro, creación y destrucción de instancias independientes.
- Propiedades compactas de ancho y alto del canvas: 1920 × 1080 por defecto,
  límites defensivos entre 16 y 16384 por dimensión. Estos límites no garantizan
  que una GPU admita cualquier tamaño futuro de textura.
- Render vacío: no dibuja ni borra el destino de OBS; salida concebida transparente.
- Persistencia del tamaño mediante los ajustes nativos de OBS.
- Crédito exacto `a plugin from DjEmiMas`, con enlace en DjEmiMas.

## Entrada existente — fase 2

- Selector de fuentes OBS de vídeo existentes, con nombres visibles y UUID persistido.
- Referencia a la instancia existente, renderizada mediante `obs_source_video_render`;
  no crea reproductores, decodificadores ni copias de la fuente.
- Entrada declarada como dependencia activa para que OBS propague activación y visibilidad.
- Enumeración completa de dependencias y rechazo de ciclos con la API de OBS.
- Comprobación directa de autorreferencia y protección adicional de profundidad de render.
- Escalado de la entrada al canvas completo; preservación del alfa premultiplicado.
- Entrada inexistente, eliminada o rechazada: salida transparente. La selección se
  conserva por UUID y se reintenta una vez por segundo para permitir carga tardía.
- Cambiar el nombre de la entrada no rompe el enlace. Una fuente nueva con el
  mismo nombre tiene otro UUID y no se conecta de forma implícita.

No hay todavía zonas, editor, presets ni instalador.
No se ha instalado en el OBS del usuario.

## Referencia visual aprobada

`docs/references/approved-mockup.png`, aportada por el usuario el 26/10/2026,
es la referencia visual vigente. Sustituye los mockups complejos anteriores.

El editor de fase 7 tendrá fuente/distribución/zonas a la izquierda, preview
grande central y propiedades contextuales a la derecha; controles de canvas y
salida en la parte inferior. Seis zonas ilustran un preset, no un límite.
El modelo comenzará por una zona dinámica. Polygon y Smooth Mesh deben poder
combinarse aunque la imagen los represente como botones. El escenario es un
fondo del editor, no contenido añadido a la salida transparente.
La entrada será una fuente OBS existente: el mockup no autoriza un reproductor
ni decodificación propios. El control Reproducir de la imagen se concretará en
su fase según las capacidades de la fuente elegida.

## Compilación y validación realizadas

26/10/2026: DLL compilada con Zig 0.15.2 / Clang, C++17, optimización O2,
avisos Wall/Wextra/Werror, usando encabezados OBS 32.2.2.
`build.ps1` acepta rutas Zig, ObsSdk y ObsInstall como parámetros; los valores
por defecto corresponden al entorno local detectado. No descarga herramientas.

La prueba `tests/lifecycle.cpp` carga la DLL mediante libobs 32.2.2 instalado,
inicializa el módulo y ejecuta 50 ciclos: creación, tipo INPUT, tamaño por
defecto, propiedades, modificación de ajustes y guardado/carga con tamaño
2560 × 1440, límites con ajustes inválidos y destrucción. Terminó con código 0.

Limitaciones: prueba sin contexto gráfico ni ventana OBS. libobs emitió avisos
de recursos gráficos sin contexto y de nombre/UUID duplicado al recargar una
fuente mientras la original seguía viva. No se ha validado la transparencia
por lectura de píxeles, la actualización diferida en el hilo de vídeo, la UI
de OBS, consumo GPU ni comportamiento en stream. El render transparente se
deduce de que el callback no dibuja; no es una medición visual.

DLL SHA256:
`28BF4982F99C9D4747002815EE616FDBFACA01888900548B32E648F4C0E813AE`

## Fases restantes (no iniciadas)

2. Referencia a fuente existente y prevención de ciclos directos e indirectos.
3. Zonas dinámicas y Split / Repeat / Custom UV.
4. Polygon.
5. Smooth Mesh, curvatura rápida y Soft Selection.
6. Máscaras por zona.
7. Editor visual simplificado según el mockup aprobado.
8. Presets y persistencia ampliada.
9. Benchmarks y optimización GPU.
10. Instalador y distribución.

Arquitectura prevista: fuente OBS independiente, entrada compartida sin
decodificación duplicada, render GPU, sin Source Pool complejo, reproductor
propio, Twitch Core, efectos superfluos ni procesos externos de ejecución.

## Validación de fase 2 — 26/10/2026

DLL final compilada con C++17, O2 y Wall/Wextra/Werror usando OBS 32.2.2.
Prueba de ciclo de vida anterior repetida, más conexión a una escena real de
libobs, renombrado, cadena de dos mappers, UUID ausente y rechazo de ciclos.
El host sin gráficos conserva los avisos de contexto documentados arriba.

Prueba adicional con contexto D3D11 real en NVIDIA GeForce RTX 5070:

- Fuente de prueba existente 32 × 16 renderizada en canvas 64 × 64.
- Lectura de los 4096 píxeles: escala completa y alfa parcial premultiplicado.
- Los 4096 píxeles transparentes sin entrada, tras selección circular rechazada
  y después de eliminar la entrada.
- Cambios de entrada procesados por el hilo de vídeo de libobs.
- Rechazo de selección de sí mismo y de ciclo indirecto entre dos mappers.
- Liberación de fuentes y cierre de libobs sin fallo.

Ambos ejecutables terminaron con código 0. Registros reproducibles en
`build/validation.log` y `build/gpu-validation.log` tras ejecutar `build.ps1`.
La API obsoleta para localizar shaders solo se usa en el host de pruebas aislado,
con supresión local de su aviso; no se usa en el plugin.

Estos resultados son pruebas aisladas con libobs y GPU. No constituyen prueba
de la interfaz de OBS, reproducción multimedia real, cámara, streaming prolongado
ni benchmark de rendimiento. La llamada reutiliza la instancia existente;
no se ha medido todavía el coste de renderizar una fuente en varios lugares.

SHA256 de la DLL final de fase 2:
`3D190B1BE3A63057015909FECDD398C02DB8090C7E6043897A447E11236EE125`

La fase 2 está implementada y validada con los límites anteriores.
La fase 3 (motor de zonas) no ha comenzado.

Código identificado con SPDX GPL-2.0-or-later. Antes de distribución pública
se incorporará el texto completo de licencia y los avisos correspondientes.
