# Posición y tamaño de máscaras básicas

Cada zona permite ajustar el centro X/Y y el ancho/alto de una máscara rectangular o elíptica. La elipse añade un radio uniforme. Valores de centro y tamaño entre 0 y 100 %, con centro inicial 50/50, ancho/alto 100 y radio 50. Ese radio corresponde a la mitad de cada dimensión local de la zona: el tamaño inicial conserva la máscara anterior. La apariencia circular o elíptica también depende de la proporción física de la zona y de ancho/alto.

Los controles recortan la cobertura del vídeo sin cambiar sus UV de contenido ni su geometría. Invertir oculta el interior de la forma y conserva el exterior. Intensidad cero omite el recorte. Ancho/alto o radio cero producen una forma vacía; con inversión se conserva toda la zona. Suavizado de borde e intensidad siguen disponibles. Restablecer forma restaura posición, tamaño y radio conservando el tipo, la inversión y los otros ajustes.

En el editor se muestran estos controles para rectángulo/elipse y solo el radio para la elipse. Los controles de archivo PNG/SVG aparecen al elegir una máscara de imagen. Se conservan deshacer/rehacer, Aplicar/Cancelar y los cinco idiomas. Los controles numéricos avanzados nativos de OBS tienen los mismos parámetros, en unidades 0–1.

Persistencia: esquema de mapping 5, con campos mask_center_x, mask_center_y, mask_width, mask_height y mask_radius por zona. Los esquemas anteriores conservan sus valores por defecto y el comportamiento previo. Los presets nuevos marcan esquema 5 para que un plugin antiguo no aplique silenciosamente un recorte diferente. Se mantiene el contenedor portable de versión 1. Se rechazan tamaños fuera de rango en la importación. Las máscaras de imagen no se escalan ni desplazan con estos controles.

Validación aislada con libobs 32.2.2 / D3D11: radios reducidos, centros independientes, escalado de ejes, rectángulos pequeños, inversión, intensidad cero, tamaños cero y fuera de rango, reset y propiedades nativas. Se comprueba alfa premultiplicado, persistencia portable, valores predeterminados antiguos, deshacer y etiquetas de los cinco idiomas. La imagen del editor procede del renderer y los controles Qt del host de pruebas, no de una nueva sesión real de OBS. No se ha instalado este cambio en el OBS habitual del usuario.

La versión pública del paquete sigue siendo 0.1.0 preview. Los informes anteriores conservan sus resultados e imágenes históricas. La compilación actual y sus hashes constan en el paquete actualizado.
