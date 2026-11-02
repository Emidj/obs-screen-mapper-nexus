# Cinco idiomas — 0.1.0 preview

Español, inglés, alemán, francés e italiano implementados sin cambiar la versión. Catálogo de 257 entradas de presentación y 14 mensajes propios del instalador en cada idioma, más las traducciones estándar de Inno Setup. Propiedades nativas, editor, acciones, tabs, avisos, errores de geometría, máscaras y presets siguen el idioma de OBS. El instalador permite seleccionar idioma; su selección no cambia el idioma de OBS.

Las variantes regionales se resuelven por prefijo, sin distinguir mayúsculas ni guion/guion bajo. Idiomas desconocidos usan inglés. El editor toma el idioma al abrirse, incluidos separadores numéricos mediante QLocale. Reabrirlo aplica el idioma nuevo; no se implementa traducción instantánea de una ventana ya abierta. Diálogos del sistema y controles estándar de Qt pueden seguir la configuración/traductores del entorno. Se mantienen las marcas, crédito exacto en inglés y nombres personalizados de zonas y presets.

Evidencia del binario actualizado:

- 699 comprobaciones anteriores del núcleo, GPU, geometría, máscaras, editor y presets superadas tras recompilar.
- 152 comprobaciones de localización con el editor real y libobs/D3D11 aislado. Variantes es-MX, en-GB, de-AT, fr-CA, it-CH; fallback pt-BR y pruebas de normalización/null. Se verifican textos de propiedades, acciones, tabs, entrada vacía, nombres automáticos, conservación de nombres personalizados y claves de persistencia, errores y Cancelar. Los recuentos incluyen verificaciones auxiliares.
- Cinco suites de instalador: 31 comprobaciones por idioma, 155 en total. Incluyen verificar el mensaje propio realmente registrado en el idioma elegido, además de instalación, bloqueos, requisitos, Unicode, hashes, backup, reinstalación y desinstalación con preservación de archivos y configuración.
- Catálogos generados con comprobación de campos, claves duplicadas y placeholders. Inspección de renders internos de los cinco idiomas en la geometría de ventana de prueba; se acortó la etiqueta alemana de añadir a «Neu» para evitar su recorte. No acredita otros tamaños o DPI.
- Payload extraído del instalador de producción y comparado por SHA256 con staging antes de entregar. No incluye artefactos de enlace ni cambia los binarios probados al empaquetarlos.

Registros y renders en docs/validation/localization/. Son imágenes generadas por los controles del host y su renderer, no capturas de OBS del usuario. La fuente de estos renders no tiene entrada seleccionada; el damero representa la salida transparente. Los nombres «Mi zona ñ — Benutzer» de las pruebas se mantienen deliberadamente en todos los idiomas para comprobar que no se traducen datos personales.

La prueba del instalador usa una compilación con identidad de prueba, permisos de usuario y destino dentro del workspace. No se instaló en OBS real ni se validó una emisión. Sigue pendiente la primera prueba del usuario en OBS. Los informes de fases anteriores conservan sus hashes y resultados históricos; los paquetes actuales se identifican por el SHA256SUMS actualizado junto al EXE.

Para regenerar: build.ps1, build-ui.ps1, package.ps1 y package.ps1 -TestBuild. Ejecutar tests/installer.ps1 -Language spanish/english/german/french/italian de forma secuencial. El proyecto incorpora translations/ y los generadores al paquete fuente. Las guías de desarrollo permanecen en español; la licencia conserva su texto original.
