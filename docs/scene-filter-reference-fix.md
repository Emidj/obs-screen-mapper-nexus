# Corrección de la referencia de escena con filtros — 0.5 Beta

El usuario confirmó que desactivar el filtro Trazo de la escena permitía abrir
«Ver escena completa». La composición privada duplicaba también sus filtros;
un filtro de escena puede devolver dimensiones nulas antes de renderizar o
modificar las coordenadas usadas por el editor.

La referencia privada omite ahora los filtros de los contenedores de escena y
grupo que copia. Muestra un aviso localizado cuando esto ocurre. No desactiva,
elimina ni actualiza filtros de la escena original. Las fuentes de vídeo siguen
compartidas, incluidos sus propios filtros. La salida real de OBS conserva los
efectos de la escena; el editor muestra la referencia geométrica sin ellos.

La regresión usa un filtro sintético que informa tamaño cero: verifica la
apertura del contexto, el vídeo renderizado, el aviso, el filtro original
activo y la conservación de los elementos y transformaciones originales.
Se mantienen las pruebas de grupos anidados, selección inversa y Apply/Cancel.

Esta prueba de GPU y host Qt aislado no sustituye comprobar la escena del
usuario con el filtro Trazo real después de actualizar el plugin.
