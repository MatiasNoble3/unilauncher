# UniLauncher

Una sola ventana para lanzar **Leonida**, **XValkyrie** y **VIctory** sin
abrir tres terminales distintas.

## Qué es y qué no es

**Qué es:** un launcher. Detecta qué backends tenés compilados, escanea
carpetas tuyas en busca de juegos (carpetas con `MicrosoftGame.config`), y
arma la línea de comandos documentada de cada proyecto para lanzarlo. Tiene
un botón **Stop** que mata todo el árbol de procesos con un *Job Object* de
Windows, así nada queda corriendo en segundo plano si algo se cuelga.

**Qué NO es:** no modifica, arregla ni fusiona el código de los tres
proyectos. Cada uno sigue siendo un proyecto independiente, con su propio
estado de avance:

- **Leonida** — el juego todavía no bootea el ejecutable real.
- **XValkyrie** — arranca títulos y dibuja frames, pero la imagen todavía no
  sale correcta.
- **VIctory** — es el que más lejos llega (bootea, menús, carga el mundo),
  pero al entrar al mundo se pierde el device de GPU y la pantalla queda
  negra.

Si alguno de los tres se cuelga, falla o no carga el juego, **eso sigue
pasando igual** con este launcher — lo único que cambia es que lo ves en el
log y podés cortarlo con un clic en vez de que quede trabado.

## Requisitos para compilar

- Windows 10/11 x64
- Visual Studio 2022 con "Desktop development with C++"
- CMake 3.20+

## Compilar

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

El ejecutable queda en `build\Release\unilauncher.exe`.

## Uso

1. Compilá los backends que quieras usar (ver sus propios READMEs):
   - Leonida: `Build.bat` → deja `gta6rt.exe` en `Documents\Leonida` (o donde
     hayas puesto `--out`).
   - XValkyrie: `build.cmd` → deja `xvboot.exe` en `build\win\Release`.
   - VIctory: `cargo build --release -p victory` → deja `victory.exe` en
     `target\release`.
2. Abrí `unilauncher.exe`. Al iniciar intenta detectar automáticamente los
   tres ejecutables de arriba, buscando en carpetas hermanas típicas (por
   ejemplo si clonaste los tres repos uno al lado del otro). Si no encuentra
   alguno, lo marca como "(no encontrado)" en el selector de backend.
3. Si el auto-detect no encontró algo, configuralo a mano desde
   **Opciones**, o compilalo y usá **Opciones > Auto-detectar backends** de
   nuevo.
4. **Archivo > Agregar carpeta de biblioteca...** para indicar dónde tenés
   tus copias de juegos (carpetas que contienen `MicrosoftGame.config`). Se
   escanean hasta 2 niveles de profundidad, sin seguir symlinks, para que el
   escaneo nunca se cuelgue.
5. Elegí un juego de la lista, elegí el backend, y apretá **Run**. El log de
   ese proceso aparece a la derecha en tiempo real.
6. **Stop** corta el proceso (y cualquier proceso hijo que haya lanzado) de
   inmediato.

### Notas sobre cada backend

- **Leonida** no necesita que elijas un juego en la lista: `gta6rt.exe` ya
  trae su propio `game_path.txt` y `shader_path.txt` generados por
  `make_game.py` en la misma carpeta de salida. UniLauncher simplemente
  corre `gta6rt.exe --run` ahí.
- **XValkyrie** recibe la carpeta del título directamente:
  `xvboot.exe exec "<carpeta>"`.
- **VIctory** necesita el `.exe` del título, no la carpeta. UniLauncher
  intenta leerlo del atributo `Executable/Name` de `MicrosoftGame.config`
  (con una búsqueda de texto simple, no un parser XML completo); si no lo
  encuentra, usa el primer `.exe` suelto en la carpeta como respaldo.

## Dónde queda la configuración

`%APPDATA%\UniLauncher\config.ini` — rutas de los tres backends y las
carpetas de biblioteca agregadas. Se puede editar a mano si hace falta.

## Licencia

Este launcher es código nuevo, sin relación de licencia con Leonida (GPLv3),
XValkyrie (GPLv3) ni VIctory (GPL-3.0-or-later) más allá de que invoca sus
ejecutables como procesos externos. Usalo, cambialo, lo que quieras.
