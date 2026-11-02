# Localización

Catálogo UTF-8 compilado en las DLL: español, inglés, alemán, francés e italiano. La columna española es la clave estable de presentación y las otras cuatro columnas son traducciones; no se usan esos textos como claves de persistencia. Separador `|`. Los nombres de usuario, identificadores de modos, UUID, rutas y formato de presets no se traducen. El crédito exacto `a plugin from DjEmiMas` y su enlace se conservan en inglés.

`generate.ps1` genera `src/i18n.hpp` y `installer/messages.iss`, detecta claves duplicadas, campos vacíos y cambios de placeholders. `build.ps1` y `package.ps1` lo ejecutan. El lookup usa un catálogo ordenado y búsqueda binaria; no carga archivos ni consulta traductores externos durante el render.

OBS llama a `obs_module_set_locale` en el núcleo. El editor toma `obs_get_locale` al abrirse y configura también QLocale para sus campos numéricos. Prefijos regionales normalizados: es, en, de, fr, it; acepta mayúsculas y guion/guion bajo. Cualquier otro idioma usa inglés. Cerrar y reabrir el editor aplica el idioma actual; los datos ya nombrados se conservan. No se traduce una ventana ya abierta en caliente. Los diálogos del sistema operativo usan las convenciones del sistema y los controles estándar Qt dependen también de los traductores que cargue OBS.

`installer.txt` contiene los mensajes propios del instalador. Inno Setup aporta sus cinco catálogos estándar y cada idioma tiene una página de información previa. La licencia GPL se incluye en su texto original. Las guías de desarrollo/prueba siguen en español; no se presentan como documentación traducida a cinco idiomas.

La suite `locale-test` usa libobs/D3D11 y el editor real en un host aislado. Comprueba propiedades nativas, acciones, tabs, etiquetas, nuevos nombres por defecto, preservación de nombres personalizados, esquema portable, errores y cancelación. Genera renders internos del editor para variantes regionales y fallback; no son capturas del OBS del usuario.
