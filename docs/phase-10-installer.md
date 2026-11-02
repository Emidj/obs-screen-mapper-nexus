# Instalador de prueba — 0.1.0 preview

EXE de Windows x64 creado con Inno Setup 6.7.0. Destino normal:
`C:\ProgramData\obs-studio\plugins\obs-screen-mapper`.
Requiere administrador y OBS Studio 32.x en modo normal. Se detecta la carpeta de OBS por registro o ubicación habitual y permite corregirla. No abre OBS al terminar ni modifica escenas, perfiles o ajustes.

El destino compartido corresponde a la ruta que OBS 32.2.2 añade en `frontend/widgets/OBSBasic.cpp`, AddExtraModulePaths. Ese código omite la ruta en modo portable. Por ello esta preview rechaza los marcadores portable_mode/portable_mode.txt y documenta que no se debe iniciar OBS con --portable. No se ofrece instalación portable en este EXE.

Incluye únicamente dos DLL finales, guía, ejemplo de preset, licencia GPL, avisos y hashes. Las DLL coinciden con las comprobadas en fase 9. Sin archivos PDB/LIB, Qt/OBS redistribuidos, reproductores ni procesos auxiliares del producto. EXE sin firma digital. La interfaz requiere el Qt y runtime C++ que proporciona una instalación funcional de OBS; se probó con el entorno local de OBS 32.2.2.

Antes de copiar: comprobación WMI de obs64.exe/obs32.exe, versión principal 32 y arquitectura PE x64 de OBS, presencia de obs.dll/Qt6Widgets.dll, ausencia de una copia manual duplicada, extracción y SHA256 del payload. Si difiere un archivo propio existente, se guarda primero una copia con fecha y ruta relativa bajo backups. Reinstalar el mismo contenido no añade backups. Un fallo durante la preparación aborta la instalación. Se usa el mecanismo normal de recuperación de Inno Setup; no se ha simulado un corte eléctrico.

Desinstalador registrado en Windows y en uninstall/unins000.exe. También exige OBS cerrado. Elimina los archivos declarados por el paquete y solo directorios vacíos; conserva backups y archivos ajenos, y no toca configuración/presets de OBS. No borra la entrada OBS Screen Mapper de las escenas del usuario: conviene retirar esa fuente de las escenas antes de desinstalar si se va a dejar de usar.

## Evidencia

30 comprobaciones de instalación aislada superadas. Se compiló el mismo script con identidad de prueba, privilegios de usuario y destino en el workspace; no se instaló el EXE de producción en el OBS real. Se probaron ausencia/versión incorrecta de OBS, instalación incompleta, portable, duplicado manual, instalación Unicode, hashes de siete archivos, exclusión de artefactos de enlace, registro/desinstalador, detección de un proceso benigno propio llamado obs64.exe, bloqueo de instalación y desinstalación mientras existe, reemplazo/backup, reinstalación idempotente, conservación de JSON ajeno inválido, archivos ajenos y presets, retirada de ambas DLL y registro, preservación de backups y ausencia de cambios en el registro de producción.

La prueba de requisitos usa una copia del ejecutable de OBS únicamente para consultar su versión/arquitectura y archivos marcadores para la existencia de dependencias. No demuestra carga de dependencias ni integración del plugin. El proceso obs64.exe de prueba es un programa propio que solo espera; no se lanzó OBS. Los siete payloads se extrajeron además del EXE de producción para comparar sus hashes con staging. Los 699 controles del motor/editor son evidencia de la fase anterior, no pruebas dentro del OBS del usuario.

Registros: validation/installer/. Compilación reproducible: package.ps1; fixture: package.ps1 -TestBuild y tests/installer.ps1. La suite escribe un registro temporal de desinstalación HKCU propio y lo elimina al desinstalar; se ejecuta con autorización para esa prueba local. Los datos de prueba quedan dentro de build/installer/tests-* como evidencia.

No se ha publicado el paquete ni probado una emisión o estabilidad prolongada. El instalador está preparado para que el usuario realice la primera prueba dentro de OBS real.
