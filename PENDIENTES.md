# Pendientes Tarea 1 — CMS y CountSketch en ventana deslizante

Entrega: **jueves 1 de octubre de 2026, 23:59 hrs.**
Estado revisado el 30-09-2026 contra `Tarea1_2026.md` y el código en `codigo_entregado/`.

## Traza de trabajo

Traza oficial del enunciado (§2), MAWI samplepoint-F, 3 de diciembre de 2018, 14:00:
**https://mawi.wide.ad.jp/mawi/samplepoint-F/2018/201812031400.pcap.gz**

- [x] Confirmar la traza local: gzip válido y conversión en streaming con `pcap2bin` = **123383971 registros IPv4** (2961215304 bytes), coincidente con el ground truth. No se dejó una copia de varios GB en el repo.
- [x] Poner la URL en el `README.md` (hoy sólo dice "descargar desde la plataforma del curso") y en el informe, junto con la semilla 42.
- [ ] Usar exactamente esa traza como base de las tres versiones: sin ataque, con DDoS y con scan.

## Lo que ya existe

- [x] Herramientas entregadas: `pcap2bin.cpp`, `exact_hh.cpp`, `inject_attack.py`, `Makefile`.
- [x] Trazas con ataque generadas (`gt_ddos.json` y `gt_scan.json`, semilla 42, inicio en 300 s, duración 30 s).
- [x] `exact_ddos.csv`: referencia exacta para la víctima DDoS `163.210.30.13`.
- [x] `tarea_cms.cpp` / `tarea_cs.cpp`: primera versión con un anillo de 6 sub-sketches (d=5, w=4096) y un anillo de N_j.
- [x] `graficos.py`: un gráfico de DDoS con exacto, CMS y CS para un solo w.

---

## 0. Bloqueante: el anillo está desalineado — RESUELTO

`sliding_sketch.cpp` reemplaza a `tarea_cms.cpp`/`tarea_cs.cpp`; con `--verify exact_ddos.csv` N_j coincide en las 84 ventanas.

La autoverificación obligatoria (§4) **falla**: el N_j de `resultados_cms.csv` difiere del `N` de `exact_ddos.csv`
en **18 de 84 ventanas** (por ejemplo, la ventana 0 da 8392145 contra 8392144, y la 73 da 8113643 contra 8113645).
Mientras esto no se corrija, todos los resultados posteriores son inválidos.

- [x] Trabajar con timestamps enteros en µs (`uint64_t`), no con `double` en segundos (`ts_us / 1e6` pierde precisión en los bordes).
- [x] Respetar la convención `(t0 + (q−1)p, t0 + q·p]`: un paquete justo en el borde pertenece a la subventana que **termina** ahí. El cálculo actual `floor((t−t0)/p) + 1` lo asigna a la siguiente. Usar `q = ceil((t − t0)/p)` en enteros, y decidir qué hacer con el paquete en `t0` exacto para que coincida con `exact_hh` (que usa `ts > tau − W` y `ts <= tau`).
- [x] Evaluar en `τ_j = t0 + W + j·p` según el reloj, no "cuando llega el primer paquete de la subventana siguiente". Hoy la última ventana se pierde y el CSV sale numerado por `q` en vez de por `win`.
- [x] Agregar la comparación automática de N_j contra la columna `N` de `exact_hh`: si una sola ventana no coincide, el programa debe abortar o avisar.

## 1. Actividad 1: implementación (2.5 pts)

### Requisitos que no se cumplen todavía
- [x] **Sketch agregado `A` (obligatorio).** Hoy no existe: en cada evaluación se suman las 6 ranuras (`O(m·d)` por consulta) y la ranura que expira se limpia sin restarla. Hay que:
  - mantener `A` (7 arreglos de d×w por tipo de sketch en total);
  - en cada paquete, actualizar a la vez el sub-sketch actual y `A`;
  - al rotar, primero restar de `A` la ranura que expira, después limpiarla y reutilizarla.
- [x] **Misma estructura de ventana para CMS y CS** (requisito 3). Hoy son dos programas copiados. Conviene un solo programa con una clase/plantilla `SlidingWindow<Sketch>` donde sólo cambien `update` y `estimate`, o ambos sketches calculados en la misma pasada.
- [x] **d y w por línea de comandos** (requisito 4). Hoy están fijos en el código (`d=5`, `w=4096`).
- [x] Parámetros por CLI también para: archivo de traza, clave (`--key src|dst`), IP a consultar (`--query`), semilla de hash y archivo de salida. Hoy la traza `traza_ddos.bin` y la IP víctima están fijas en el código, y no se puede analizar el scan.
- [x] Revisar la familia de hash: `a*x` con `a < 2^31` y `x < 2^32` cabe en 64 bits, así que está bien, pero `% 2` sobre la misma familia lineal para el signo es débil. Documentarlo o usar un bit de un hash mejor (por ejemplo multiply-shift o murmur).

### Validación sin ataque (requisito 6)
- [x] Generar o usar la traza **base sin ataques** (`traza.bin`).
- [x] Elegir un conjunto de claves (por ejemplo, el top-k de `exact_hh --top` más algunas claves de frecuencia media y baja) y varias ventanas.
- [x] Calcular el error absoluto y relativo de CMS y CS contra `exact_hh` para esas claves.

### Evaluación
- [x] Correr con **d = 5 y al menos tres valores de w** (por ejemplo 1024, 4096 y 16384; ajustar para que la diferencia se vea).
- [x] Reportar el error absoluto y relativo de CMS y CS para cada w.
- [x] Reportar la memoria de los contadores: `7 · d · w · sizeof(contador)` por sketch (6 ranuras + A), más el anillo escalar.

## 2. Actividad 2: detección de ataques (2.5 pts)

- [x] Correr `exact_hh` sobre `traza_scan.bin` con `--key src --query 198.18.0.7 --out-query exact_scan.csv`.
- [x] Correr CMS y CS sobre **DDoS (clave dst)** y **scan (clave src)** para los tres w.
- [x] Decisión heavy hitter por ventana: `f̂_j(x) ≥ ⌈φ·N_j⌉` con φ = 0.01, usando el N_j exacto del anillo. Compararla con `exact_hh` (columna `exact_hh`).
- [x] Revisar `--pps`. En `resultados_cms.csv` el estimado de CMS antes del ataque ya es unas 4 a 5 veces el exacto (423 contra 90). Si con el ataque las curvas de los tres w se superponen, **bajar `--pps`** y regenerar las trazas (§2 del enunciado).
- [x] **Figuras (2 en total):** una por ataque, desde 240 s hasta 390 s (60 s antes y 60 s después del ataque), con la curva exacta más CMS y CS para los 3 w superpuestos. El gráfico actual cubre toda la traza y un solo w.
- [x] **MRE** sobre el conjunto J: ventanas con τ > 300 s (inicio del ataque) y con algún paquete del ataque todavía dentro de la ventana (τ < 330 + 60 s), con f_j(x) > 0. Son unas 8 ventanas.
- [x] Reportar también el **error por ventana** y/o repetir con **2 o 3 semillas de hash** distintas (el MRE de CS puede no ser monótono en w).
- [x] **Latencia de detección:** primera evaluación que cumple el criterio, menos 300 s (resolución de 10 s), para la referencia exacta, CMS y CS, en cada w.
- [x] Reportar y explicar los **falsos positivos de CMS** con w chico (detecta una ventana antes que la referencia exacta). No corregirlos.
- [x] Comparar CMS y CS en precisión, memoria y latencia, explicándolo por colisiones y por la diferencia entre el estimador mínimo y la mediana.

## 3. Estimación del cambio Δf_j(x) (§6.3)

- [x] Δf exacto: columna `exact_delta` de `exact_hh` (o `f_j − f_{j−1}`).
- [x] Construir `ΔA_j = S_entra − S_sale` **antes de limpiar la ranura que expira** (sin copiar `A_{j−1}`).
- [x] Estimador CS habitual sobre ΔA_j (mediana de `s_i(x)·ΔA_j[i][h_i(x)]`, **sin truncar negativos**).
- [x] Estimador **CMS-mediana** sobre ΔA_j (mediana de `ΔA_j[i][h_i(x)]`; no usar el mínimo).
- [x] Contadores con signo para ΔA en CMS (hoy CMS usa `uint32_t`).
- [x] Figura por ataque con Δf exacto, Δf̂ CS y Δf̂ CMS-mediana alrededor del inicio (300 s) y del término (330 s y la salida hacia 390 s).
- [x] Identificar los mayores incrementos y decrementos y discutir qué estimador aproxima mejor.

## 4. Scripts de reproducibilidad

Hecho: `run_experimentos.sh` → `elegir_claves.py`, `exact_hh`, `sliding_sketch`, `analisis.py`; resultados en `codigo_entregado/resultados/`. Anchos usados: w = 256, 1024, 4096, 16384; semillas de hash 42, 7, 1234; variante de baja intensidad con `--pps 3000`.

- [x] Script único (`run_experimentos.sh` o el `Makefile` extendido) que compile, genere las trazas con ataque, corra `exact_hh` y los sketches para los 3 w y los 2 ataques, y produzca las figuras y tablas.
- [x] Agregar los nuevos binarios al `Makefile` (hoy sólo están `pcap2bin` y `exact_hh`).
- [x] Ampliar `graficos.py` (o separarlo en varios scripts): figura DDoS, figura scan, figuras de Δf, cálculo de MRE, latencia y tabla resumen.
- [x] `requirements.txt`: agregar `matplotlib` (y `pandas` si se usa). Hoy sólo tiene `numpy`.
- [x] Quitar las rutas absolutas de otro computador (`C:/Users/krkmo/...`) de los JSON o explicar que vienen del generador.

## 5. Informe (máximo 6 páginas sin anexos)

- [x] Informe redactado en `INFORME.md`: diseño, validación, figuras, resumen cuantitativo, análisis de Δf y respuestas a las cinco preguntas.

## 6. Presentación oral (10 min)

- [x] Deck de 9 diapositivas creado en `codigo_entregado/presentacion_tarea1.pptx`, con notas del presentador y los resultados de ambos ataques.

## 7. Repositorio y README

- [x] Actualizar `README.md`: hoy sólo habla de DDoS con CMS/CS sin parámetros. Faltan el scan, los parámetros por CLI, cómo reproducir todo y la URL y semilla.
- [x] Confirmar que no se suben trazas (`*.bin` y el pcap ya están en `.gitignore`; agregar también `*.pcap.gz`).
- [x] CSV y PNG de resultados versionados; ya están en Git y corresponden a las corridas reportadas.
- [x] `Tarea1_2026.md` está versionado en Git.

## Aún pendiente

- [ ] Preparar las diapositivas para la presentación oral de 10 minutos.
- [ ] Si el curso exige PDF o formato específico para el informe, exportar `INFORME.md` al formato solicitado y verificar que no supere seis páginas.
- [ ] Completar los nombres de integrantes en el informe y en la portada de las diapositivas.
