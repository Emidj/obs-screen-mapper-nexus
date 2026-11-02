# Diez presets de ejemplo

Presets portables de 1920 × 1080, importables con el formato existente. No cambian la fuente de entrada actual ni crean reproductores. Todos están construidos con geometría y máscaras nativas, sin recursos externos ni decodificadores adicionales. Los nombres de estos ejemplos son datos del preset y permanecen en español, como cualquier nombre personalizado, al cambiar el idioma del editor.

| Ejemplo | Zonas | Distribución y geometría |
|---|---:|---|
| Panorámica curva | 1 | Repeat, malla suave con arco |
| Díptico | 2 | Split, curvaturas opuestas |
| Tríptico curvo | 3 | Custom UV, tres franjas de la entrada |
| Escenario 3 × 2 | 6 | Repeat, seis pantallas curvas |
| Videowall 4 × 3 | 12 | Custom UV, imagen reconstruida con separaciones |
| Abanico | 5 | Custom UV y contornos trapezoidales Polygon |
| Tres rombos | 3 | Repeat, Polygon y malla combinados |
| Orbital | 5 | Repeat y máscaras elípticas suaves |
| Ondas | 4 | Custom UV y curvatura con perfil de onda |
| Cabina DJ | 5 | Repeat, pantalla central, laterales y franjas inferiores |

## Uso

En el editor abre Presets → Importar y selecciona un archivo .mapper.json. Se añade a la biblioteca local y se carga en la copia de trabajo; Aplicar confirma el diseño. Puedes deshacer la carga y Cancelar conserva el diseño anterior. La entrada OBS elegida se mantiene.

Los diez ejemplos se incluyen en el instalador bajo:
`C:\ProgramData\obs-studio\plugins\obs-screen-mapper\data\examples\starter`.
También se entregan juntos en OBS-Screen-Mapper-10-Presets.zip. No se añaden automáticamente al selector de biblioteca: se importan los que quieras usar. No se modifica la biblioteca ni el perfil real del usuario durante la generación o las pruebas.

## Evidencia

Generados por ui/preset-gallery.cpp a través de MapperPresets::encode; se validan, materializan de nuevo y se renderiza el layout reimportado con obs_screen_mapper en libobs 32.2.2 / D3D11 aislado. Se comprueba la topología válida, número de zonas tras roundtrip, omisión del UUID de entrada y salida no vacía en los diez ejemplos.

Las vistas individuales son lecturas del render GPU del plugin. La comparativa compone esos resultados sobre damero para mostrar transparencia y añade nombres; no es una captura de la interfaz OBS ni una imagen conceptual. Se usa el mismo fotograma, extraído en el segundo 4 del vídeo de Tokio de F:, para comparar los diseños. El vídeo o fotograma no se incrusta en los presets ni en el instalador.

Se conservan el inicio normal con una zona y la ausencia de un máximo fijo de seis. No cambian el núcleo ni el módulo de editor compilados; se añaden archivos de datos y una herramienta de desarrollo. La versión del paquete sigue siendo 0.1.0 preview. Esta validación de diez diseños no implica una nueva prueba prolongada de vídeo/streaming para cada uno.
