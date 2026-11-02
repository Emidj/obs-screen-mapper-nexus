# Transformación 3D y cuatro esquinas

Cada zona tiene una transformación independiente, posterior a Polygon, malla y máscara. La pestaña Transformación incorpora X/Y, profundidad Z, rotaciones X/Y/Z, escala uniforme y pivote X/Y, además de ajuste proyectivo por cuatro esquinas. La herramienta Cuatro esquinas activa el ajuste y permite arrastrar sus cuatro tiradores amarillos en el preview. También se puede seleccionar una esquina y escribir sus coordenadas.

## Para los dos carteles de una fotografía

1. Coloca la fotografía como fuente de imagen debajo del mapper en la escena OBS. Puedes cargarla también como fondo de referencia del editor para trabajar cómodamente.
2. Selecciona la fuente de vídeo existente y crea dos zonas en Repeat si ambas deben mostrar el mismo vídeo completo. Para vídeos diferentes usa dos fuentes Screen Mapper, cada una con su entrada; no se introduce un Source Pool.
3. Selecciona una zona, pulsa Cuatro esquinas y arrastra los cuatro puntos hasta las esquinas interiores de su cartel. Repite en la otra zona. Conviene empezar sin malla ni Polygon para ajustar un cartel plano; se pueden combinar después.
4. Ajusta posición, rotación o profundidad cuando lo necesites. Aplicar confirma; Cancelar conserva el diseño anterior. Los gestos y cambios numéricos se pueden deshacer y rehacer.

El archivo carteles-dos-pantallas.mapper.json entregado por separado contiene un encaje inicial de las dos pantallas de la imagen del usuario, con canvas 1391 × 720. No incluye esa fotografía ni ningún vídeo y mantiene la entrada OBS seleccionada. Debe colocarse la fotografía debajo del mapper para que forme parte de la salida final. La imagen usada como referencia del editor nunca se añade automáticamente a la salida del plugin.

## Qué representa cada control

- X/Y: desplazamiento adicional respecto a Destino X/Y de Geometría, expresado como porcentaje del ancho/alto del canvas. Cero conserva el destino original. Admite −1000 a +1000 %; no modifica el contorno ni sus UV.
- Z: profundidad respecto a una cámara común fija. Positivo aleja y reduce el tamaño aparente; negativo acerca. Unidades porcentuales de la dimensión mayor del canvas; rango −100 a +1000 %. La cámara está a dos veces esa dimensión y no se ofrece un editor de cámaras.
- Rotaciones: grados, de −360 a +360. Se aplican X, después Y y después Z. X inclina arriba/abajo, Y hacia los lados y Z en el plano. Las traslaciones se aplican después de las rotaciones.
- Escala: uniforme, de 0 a 1000 %. Cien conserva el tamaño. Cero hace desaparecer la superficie sin borrarla.
- Pivote: coordenadas locales 0–100 %, inicialmente 50/50. Si hay cuatro esquinas, se proyecta el pivote junto con la zona, de modo que cada cartel gira alrededor de su propio centro y no alrededor del centro del canvas.
- Esquina X/Y: coordenadas relativas al rectángulo de destino, inicialmente (0,0), (100,0), (100,100), (0,100), con rango −400 a +400 %. No son las UV de contenido.
- Restablecer transformación: elimina posición adicional, rotaciones, profundidad, escala y pinning, conservando geometría, máscara y fuente.

Las esquinas deben formar un cuadrilátero convexo con orden superior izquierda → superior derecha → inferior derecha → inferior izquierda. Cruces, caras colapsadas o valores no válidos producen salida transparente para esa zona y aviso en el editor. Los valores numéricos permiten corregirla o restablecerla. El arrastre rechaza movimientos que crucen las esquinas.

## Salida y editor

La salida conserva siempre el tamaño del canvas. Lo exterior queda recortado, aunque la posición de la zona sea negativa o mayor que el lienzo. El preview del editor dibuja también el exterior y marca el borde del canvas. Zoom y Mover lienzo permiten recuperar las zonas apartadas; la lista de zonas sigue disponible. Mover lienzo desplaza solo la vista; Seleccionar permite arrastrar la zona, modificando su transformación X/Y.

El modo exterior solo se habilita en la fuente privada de preview y no se guarda en el original ni en el preset. Los puntos, contornos, números, selección y arrastres usan la misma proyección que el vídeo. Las zonas que atraviesan el plano cercano de la cámara se recortan; las partes situadas detrás no tienen tiradores proyectables. No se implementa un motor de escenas 3D, iluminación ni oclusión por profundidad: los solapamientos siguen el orden de zonas de la lista.

## Motor y persistencia

Cuatro esquinas usa una homografía, con coordenadas homogéneas para interpolación de textura y máscara. Se combina con la cámara 3D conservando la corrección de perspectiva entre triángulos. Se reutilizan buffers de geometría existentes; cambiar la transformación actualiza uniforms del shader y no crea otro reproductor. El caso identidad sigue usando el camino de render previo.

Esquema de mapping 6, con valores identidad al cargar esquemas anteriores. El formato portable sigue en versión 1 y las versiones antiguas del plugin rechazan el esquema nuevo para evitar perder transformaciones silenciosamente. Se conservan los cinco idiomas y la versión del paquete 0.1.0 preview. La API de apertura del editor sigue siendo v2; se deben instalar juntas las dos DLL de este paquete.

## Validación y límites

Las pruebas GPU verifican traslación negativa y fuera de canvas, dimensiones de salida constantes, profundidad, pivote, rotación Z de cuadrantes, coincidencia entre proyección CPU/GPU con rotaciones X/Y, muestreo proyectivo de ambos triángulos, combinación de cuatro esquinas y 3D, máscaras, cuadriláteros inválidos y reset exacto. Las pruebas de editor comprueban controles, arrastre, deshacer/rehacer, exportación, Cancelar y render/selección fuera del canvas. Se prueba persistencia portable, valores antiguos identidad y traducciones de la nueva pestaña y herramienta.

El render con los dos carteles usa los controles Qt y el renderer GPU del host aislado, con la fotografía como referencia y una fuente procedural de prueba. No es una captura de una nueva sesión OBS ni una prueba de emisión. Sigue pendiente probar la nueva transformación con vídeos reales durante una sesión prolongada y otros equipos/DPI. No se instala ni se altera el OBS habitual del usuario durante estas pruebas.
