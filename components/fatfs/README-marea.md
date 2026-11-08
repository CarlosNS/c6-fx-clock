# fatfs (copia del componente de ESP-IDF 5.5)

Copia del componente `fatfs` de ESP-IDF 5.5, sin sus pruebas. ESP-IDF usa la de esta carpeta en lugar de la suya porque está en `components/` del proyecto.

Por qué existe: ESP-IDF compila FatFs con exFAT desactivado (`FF_FS_EXFAT 0`) y no tiene ninguna opción para activarlo. Las microSD de más de 32 GB vienen en exFAT y el dueño quiere que el reloj formatee en exFAT, que entienden Windows, macOS, Linux y los móviles.

Cambios respecto al original (buscar `Marea:` en el código):

- `src/ffconf.h`: `FF_FS_EXFAT 1`, y `FF_USE_LABEL` a 0 cuando `CONFIG_FATFS_USE_LABEL` no está puesta (el código de exFAT la usa en expresiones de C, y una opción `bool` desactivada no está definida).
- `vfs/vfs_fat_sdmmc.c`: al formatear se usa `FM_EXFAT` en vez de `FM_ANY` (dos sitios).

Al cambiar de versión de ESP-IDF, vuelve a copiar el componente nuevo y repite estos dos cambios. FatFs es de ChaN, con licencia BSD de una cláusula (ver `src/00readme.txt` y THIRD-PARTY-NOTICES.md).
