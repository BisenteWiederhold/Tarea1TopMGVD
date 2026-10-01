# Tarea 1: CMS y CountSketch sobre ventana deslizante

Tópicos en Manejo de Grandes Volúmenes de Datos (UdeC), 2026.

Se estima la frecuencia de una IP en una ventana deslizante de W = 60 s, que avanza cada p = 10 s, con **Count-Min Sketch (CMS)** y **CountSketch (CS)**. Con esa estimación se detectan dos ataques sintéticos inyectados en una traza real de MAWI: un **DDoS** (clave: IP de destino) y un **scan** (clave: IP de origen). El enunciado completo está en `Tarea1_2026.md`.

## Datos

| | |
|---|---|
| Traza base | MAWI samplepoint-F, 3 de diciembre de 2018, 14:00 — <https://mawi.wide.ad.jp/mawi/samplepoint-F/2018/201812031400.pcap.gz> |
| Registros | `traza.bin`: 123 383 971 (900 s de tráfico) |
| Ataques | Inicio en 300 s, duración 30 s, semilla **42** (`inject_attack.py --seed 42`) |
| DDoS | Víctima `163.210.30.13`, 10 000 pps, 4000 fuentes (`gt_ddos.json`) |
| Scan | Atacante `198.18.0.7`, 8000 pps, 60 000 destinos (`gt_scan.json`) |
| Variante de baja intensidad | Los mismos ataques con 3000 pps (`gt_*_bajo.json`), para que el error de los sketches se note (§2 del enunciado) |
| Semillas de hash | 42 (principal), 7 y 1234 |

Las trazas (`*.bin`, `*.pcap.gz`, alrededor de 3 GB cada una) **no se suben al repositorio**. `preparar_trazas.sh` las descarga y las genera. Los `gt_*.json` contienen rutas absolutas del computador donde se generaron (campos `traza_base` y `traza_salida`); son sólo informativas.

Si el pcap ya está en `~/Downloads/201812031400.pcap.gz`, `preparar_trazas.sh` lo usa directamente y evita descargar otra copia. También se puede indicar una ruta distinta con `PCAP_SOURCE=/ruta/al/201812031400.pcap.gz`. La conversión de `pcap2bin` funciona en macOS/Linux además de Windows.

El borrador del informe está en [`INFORME.md`](INFORME.md); los puntos aún abiertos, incluida la presentación, están en [`PENDIENTES.md`](PENDIENTES.md).
La presentación oral de 10 minutos está en [`codigo_entregado/presentacion_tarea1.pptx`](codigo_entregado/presentacion_tarea1.pptx); reemplazar el marcador de integrantes antes de exponer.

## Requisitos

- `g++` con C++17. En Windows: MSYS2 UCRT64, ejecutando los scripts desde Git Bash o MSYS2.
- `exact_hh` usa `mmap`, así que en Windows se compila y corre en **WSL** con g++ (`wsl -u root -e sh -c "apt update && apt install -y g++"`). En Linux corre de forma nativa.
- Python 3 con `pip install -r codigo_entregado/requirements.txt` (numpy, matplotlib y pandas).
- `curl` y `gzip`, sólo si hay que descargar la traza.

## Reproducir todo

```bash
cd codigo_entregado
bash run_experimentos.sh          # FORCE=1 bash run_experimentos.sh rehace todo
```

El script es idempotente: omite lo que ya existe. Hace lo siguiente:

1. Compila `sliding_sketch` y `exact_hh`. Si `exact_hh` no compila de forma nativa, lo compila en WSL.
2. Ejecuta `preparar_trazas.sh`, que descarga el pcap, lo convierte con `pcap2bin` a `traza.bin`, inyecta los ataques y verifica el número de registros. Después genera las trazas de baja intensidad.
3. Elige las claves de validación (`elegir_claves.py`) y corre `exact_hh` como referencia exacta para cada experimento.
4. Corre `sliding_sketch` con d = 5 y w = 256, 1024, 4096 y 16384, con cada semilla de hash. Cada corrida **verifica N_j contra `exact_hh`** y aborta si alguna ventana no coincide.
5. Ejecuta `analisis.py`, que genera las figuras y tablas en `resultados/`.

Con las trazas ya generadas tarda unos 10 minutos: alrededor de 1 minuto por cada corrida de `exact_hh` en WSL y unos 13 s por cada corrida de `sliding_sketch`.

## Archivos (`codigo_entregado/`)

| Archivo | Contenido |
|---|---|
| `sliding_sketch.cpp` | **Implementación de la tarea**: CMS y CS sobre una misma ventana deslizante (`SlidingWindow<Sketch>`) |
| `run_experimentos.sh` | Script único de reproducción |
| `analisis.py` | Figuras y tablas (MRE, latencia, falsos positivos, memoria, Δf) |
| `elegir_claves.py` | Claves de validación sin ataque: frecuencia alta, ~1000, ~100 y ~10 en la primera ventana |
| `claves_dst.txt`, `claves_src.txt` | Claves elegidas (IP, frecuencia en la ventana 0, grupo) |
| `preparar_trazas.sh` | Descarga, conversión e inyección de ataques |
| `exact_hh.cpp`, `pcap2bin.cpp`, `inject_attack.py` | Herramientas entregadas por el curso, sin modificar |
| `Makefile` | Compila `pcap2bin`, `exact_hh` y `sliding_sketch` (en MinGW usar `make LDFLAGS=-static`) |

### Resultados (`codigo_entregado/resultados/`)

Los experimentos se llaman `base_dst` y `base_src` (validación sin ataque), `ddos`, `scan`, `ddos_bajo` y `scan_bajo`.

| Archivo | Contenido |
|---|---|
| `fig_<ataque>.png` | Frecuencia exacta y estimada (CMS y CS, los 4 anchos) entre 240 y 390 s, con un panel de error |
| `fig_delta_<ataque>.png` | Δf exacto contra el estimado con CS y con CMS-mediana, con paneles de error por ancho |
| `resumen.md/.csv` | Tabla resumen: MRE sobre J (semilla 42, y media ± desviación entre semillas), memoria, latencia, FP y FN |
| `validacion.md/.csv` | Error absoluto, relativo y sesgo sin ataque, por grupo de claves y por ancho |
| `delta_extremos.md/.csv` | Mayores incrementos y decrementos de Δf y su error, más el MAE de Δf |
| `error_por_ventana.csv` | Error relativo de cada ventana de J, para cada ataque, sketch, ancho y semilla |
| `resumen_por_semilla.csv` | Igual que `resumen`, pero con una fila por semilla |
| `exact_<exp>.csv` | Salida `--out-query` de `exact_hh` |
| `sk_<exp>_s<semilla>.csv` | Salida de `sliding_sketch` |

## `sliding_sketch`

```bash
g++ -O2 -march=native -std=c++17 -static -o sliding_sketch sliding_sketch.cpp

./sliding_sketch traza_ddos.bin --key dst --query 163.210.30.13 \
    -d 5 -w 256,1024,4096,16384 --seed 42 \
    --out sk_ddos.csv --verify resultados/exact_ddos.csv
```

| Opción | Descripción (valor por defecto) |
|---|---|
| `--key src\|dst` | Clave que se cuenta (`dst`) |
| `--query IP` | IP que se estima; se puede repetir |
| `--query-file ARCH` | Archivo con una IP por línea; se ignoran las columnas extra |
| `-d D` | Filas de cada sketch (5) |
| `-w W[,W2,...]` | Ancho o anchos; todos se calculan en una sola pasada (4096) |
| `--seed S` | Semilla de las funciones hash (42) |
| `-W`, `--delta` | Ancho de la ventana y de la subventana en segundos (60 y 10); W debe ser múltiplo de p |
| `--phi F` | Umbral de heavy hitter (0.01) |
| `--out ARCH` | CSV de salida (salida estándar) |
| `--verify ARCH` | CSV de `exact_hh` con columnas `win` y `N`: compara N_j ventana por ventana y termina con código 3 si alguna difiere |

Columnas de salida: `win, tau_us, t_rel_s, key, w, N, threshold, cms_f, cms_hh, cs_f, cs_hh, cms_med_delta, cs_delta`. Los dos campos de Δ quedan vacíos en la ventana 0. Por la salida de errores imprime un resumen que incluye la memoria de los contadores.

### Diseño

- **Anillo de m = 6 sub-sketches y agregado `A`** por cada sketch y cada ancho: 7 arreglos de d×w contadores de 32 bits, es decir, 7·d·w·4 B (35 KB con w = 256 y 2,24 MB con w = 16384). Cada paquete actualiza la subventana actual y `A`. Al rotar, la ranura que expira se resta de `A`, se limpia y se reutiliza, lo que toca un número de contadores proporcional a d·w e independiente de cuántos paquetes hay en la ventana.
- **Convención temporal idéntica a `exact_hh`**, con enteros en µs:
  - La subventana q cubre `(t0 + (q−1)p, t0 + q·p]` y ocupa la ranura `(q−1) mod m`.
  - Las evaluaciones se hacen en `τ_j = t0 + W + j·p` mientras `τ_j ≤ t_fin`.
  - Los paquetes con `ts = t0` quedan fuera de toda ventana, como en `exact_hh`.
- **N_j exacto** con un anillo de 6 contadores escalares. El umbral es `⌈φ·N_j⌉`.
- **Hash**: Carter-Wegman `(a·x + b) mod (2^61 − 1)`. El signo de CS usa una función independiente de la misma familia (su bit menos significativo). CMS y CS usan las mismas posiciones, de modo que la diferencia entre ambos se debe sólo al estimador.
- **Δf (§6.3)**: al expirar una ranura se guarda `dA = −S_sale` antes de limpiarla. Al evaluar se le suma `S_entra`, de modo que `dA = A_j − A_{j−1}` sin copiar `A_{j−1}`. Sobre `dA` se aplican el estimador habitual de CS y la mediana para CMS (CMS-mediana).
