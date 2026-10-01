# Pendientes Tarea 1 — CMS y CountSketch en ventana deslizante

Entrega: **jueves 1 de octubre de 2026, 23:59 hrs.**
Estado revisado el 30-09-2026 contra `Tarea1_2026.md` y el código en `codigo_entregado/`.

## Traza de trabajo

Traza oficial del enunciado (§2), MAWI samplepoint-F, 3 de diciembre de 2018, 14:00:
**https://mawi.wide.ad.jp/mawi/samplepoint-F/2018/201812031400.pcap.gz**

- [ ] Confirmar que `traza.bin` salió de esa traza (`zcat 201812031400.pcap.gz | ./pcap2bin > traza.bin`). Los JSON indican `registros_base = 123383971`, que debería coincidir con lo que entrega `pcap2bin`.
- [ ] Poner la URL en el `README.md` (hoy sólo dice "descargar desde la plataforma del curso") y en el informe, junto con la semilla 42.
- [ ] Usar exactamente esa traza como base de las tres versiones: sin ataque, con DDoS y con scan.

## Lo que ya existe

- [x] Herramientas entregadas: `pcap2bin.cpp`, `exact_hh.cpp`, `inject_attack.py`, `Makefile`.
- [x] Trazas con ataque generadas (`gt_ddos.json` y `gt_scan.json`, semilla 42, inicio en 300 s, duración 30 s).
- [x] `exact_ddos.csv`: referencia exacta para la víctima DDoS `163.210.30.13`.
- [x] `tarea_cms.cpp` / `tarea_cs.cpp`: primera versión con un anillo de 6 sub-sketches (d=5, w=4096) y un anillo de N_j.
- [x] `graficos.py`: un gráfico de DDoS con exacto, CMS y CS para un solo w.

---

## 0. Bloqueante: el anillo está desalineado

La autoverificación obligatoria (§4) **falla**: el N_j de `resultados_cms.csv` difiere del `N` de `exact_ddos.csv`
en **18 de 84 ventanas** (por ejemplo, la ventana 0 da 8392145 contra 8392144, y la 73 da 8113643 contra 8113645).
Mientras esto no se corrija, todos los resultados posteriores son inválidos.

- [ ] Trabajar con timestamps enteros en µs (`uint64_t`), no con `double` en segundos (`ts_us / 1e6` pierde precisión en los bordes).
- [ ] Respetar la convención `(t0 + (q−1)p, t0 + q·p]`: un paquete justo en el borde pertenece a la subventana que **termina** ahí. El cálculo actual `floor((t−t0)/p) + 1` lo asigna a la siguiente. Usar `q = ceil((t − t0)/p)` en enteros, y decidir qué hacer con el paquete en `t0` exacto para que coincida con `exact_hh` (que usa `ts > tau − W` y `ts <= tau`).
- [ ] Evaluar en `τ_j = t0 + W + j·p` según el reloj, no "cuando llega el primer paquete de la subventana siguiente". Hoy la última ventana se pierde y el CSV sale numerado por `q` en vez de por `win`.
- [ ] Agregar la comparación automática de N_j contra la columna `N` de `exact_hh`: si una sola ventana no coincide, el programa debe abortar o avisar.

## 1. Actividad 1: implementación (2.5 pts)

### Requisitos que no se cumplen todavía
- [ ] **Sketch agregado `A` (obligatorio).** Hoy no existe: en cada evaluación se suman las 6 ranuras (`O(m·d)` por consulta) y la ranura que expira se limpia sin restarla. Hay que:
  - mantener `A` (7 arreglos de d×w por tipo de sketch en total);
  - en cada paquete, actualizar a la vez el sub-sketch actual y `A`;
  - al rotar, primero restar de `A` la ranura que expira, después limpiarla y reutilizarla.
- [ ] **Misma estructura de ventana para CMS y CS** (requisito 3). Hoy son dos programas copiados. Conviene un solo programa con una clase/plantilla `SlidingWindow<Sketch>` donde sólo cambien `update` y `estimate`, o ambos sketches calculados en la misma pasada.
- [ ] **d y w por línea de comandos** (requisito 4). Hoy están fijos en el código (`d=5`, `w=4096`).
- [ ] Parámetros por CLI también para: archivo de traza, clave (`--key src|dst`), IP a consultar (`--query`), semilla de hash y archivo de salida. Hoy la traza `traza_ddos.bin` y la IP víctima están fijas en el código, y no se puede analizar el scan.
- [ ] Revisar la familia de hash: `a*x` con `a < 2^31` y `x < 2^32` cabe en 64 bits, así que está bien, pero `% 2` sobre la misma familia lineal para el signo es débil. Documentarlo o usar un bit de un hash mejor (por ejemplo multiply-shift o murmur).

### Validación sin ataque (requisito 6)
- [ ] Generar o usar la traza **base sin ataques** (`traza.bin`).
- [ ] Elegir un conjunto de claves (por ejemplo, el top-k de `exact_hh --top` más algunas claves de frecuencia media y baja) y varias ventanas.
- [ ] Calcular el error absoluto y relativo de CMS y CS contra `exact_hh` para esas claves.

### Evaluación
- [ ] Correr con **d = 5 y al menos tres valores de w** (por ejemplo 1024, 4096 y 16384; ajustar para que la diferencia se vea).
- [ ] Reportar el error absoluto y relativo de CMS y CS para cada w.
- [ ] Reportar la memoria de los contadores: `7 · d · w · sizeof(contador)` por sketch (6 ranuras + A), más el anillo escalar.

## 2. Actividad 2: detección de ataques (2.5 pts)

- [ ] Correr `exact_hh` sobre `traza_scan.bin` con `--key src --query 198.18.0.7 --out-query exact_scan.csv`.
- [ ] Correr CMS y CS sobre **DDoS (clave dst)** y **scan (clave src)** para los tres w.
- [ ] Decisión heavy hitter por ventana: `f̂_j(x) ≥ ⌈φ·N_j⌉` con φ = 0.01, usando el N_j exacto del anillo. Compararla con `exact_hh` (columna `exact_hh`).
- [ ] Revisar `--pps`. En `resultados_cms.csv` el estimado de CMS antes del ataque ya es unas 4 a 5 veces el exacto (423 contra 90). Si con el ataque las curvas de los tres w se superponen, **bajar `--pps`** y regenerar las trazas (§2 del enunciado).
- [ ] **Figuras (2 en total):** una por ataque, desde 240 s hasta 390 s (60 s antes y 60 s después del ataque), con la curva exacta más CMS y CS para los 3 w superpuestos. El gráfico actual cubre toda la traza y un solo w.
- [ ] **MRE** sobre el conjunto J: ventanas con τ > 300 s (inicio del ataque) y con algún paquete del ataque todavía dentro de la ventana (τ < 330 + 60 s), con f_j(x) > 0. Son unas 8 ventanas.
- [ ] Reportar también el **error por ventana** y/o repetir con **2 o 3 semillas de hash** distintas (el MRE de CS puede no ser monótono en w).
- [ ] **Latencia de detección:** primera evaluación que cumple el criterio, menos 300 s (resolución de 10 s), para la referencia exacta, CMS y CS, en cada w.
- [ ] Reportar y explicar los **falsos positivos de CMS** con w chico (detecta una ventana antes que la referencia exacta). No corregirlos.
- [ ] Comparar CMS y CS en precisión, memoria y latencia, explicándolo por colisiones y por la diferencia entre el estimador mínimo y la mediana.

## 3. Estimación del cambio Δf_j(x) (§6.3)

- [ ] Δf exacto: columna `exact_delta` de `exact_hh` (o `f_j − f_{j−1}`).
- [ ] Construir `ΔA_j = S_entra − S_sale` **antes de limpiar la ranura que expira** (sin copiar `A_{j−1}`).
- [ ] Estimador CS habitual sobre ΔA_j (mediana de `s_i(x)·ΔA_j[i][h_i(x)]`, **sin truncar negativos**).
- [ ] Estimador **CMS-mediana** sobre ΔA_j (mediana de `ΔA_j[i][h_i(x)]`; no usar el mínimo).
- [ ] Contadores con signo para ΔA en CMS (hoy CMS usa `uint32_t`).
- [ ] Figura por ataque con Δf exacto, Δf̂ CS y Δf̂ CMS-mediana alrededor del inicio (300 s) y del término (330 s y la salida hacia 390 s).
- [ ] Identificar los mayores incrementos y decrementos y discutir qué estimador aproxima mejor.

## 4. Scripts de reproducibilidad

- [ ] Script único (`run_experimentos.sh` o el `Makefile` extendido) que compile, genere las trazas con ataque, corra `exact_hh` y los sketches para los 3 w y los 2 ataques, y produzca las figuras y tablas.
- [ ] Agregar los nuevos binarios al `Makefile` (hoy sólo están `pcap2bin` y `exact_hh`).
- [ ] Ampliar `graficos.py` (o separarlo en varios scripts): figura DDoS, figura scan, figuras de Δf, cálculo de MRE, latencia y tabla resumen.
- [ ] `requirements.txt`: agregar `matplotlib` (y `pandas` si se usa). Hoy sólo tiene `numpy`.
- [ ] Quitar las rutas absolutas de otro computador (`C:/Users/krkmo/...`) de los JSON o explicar que vienen del generador.

## 5. Informe (máximo 6 páginas sin anexos)

- [ ] Diseño de la ventana deslizante: el anillo, el agregado A, la convención de bordes, la precarga y la autoverificación de N_j.
- [ ] Resultados de la validación sin ataque (errores por w).
- [ ] Figuras de frecuencia exacta contra estimada para los 2 ataques.
- [ ] Figura(s) de Δf exacto, CS y CMS-mediana.
- [ ] **Tabla resumen** de error (MRE), memoria y latencia para CMS y CS en cada w y cada ataque.
- [ ] Preguntas obligatorias (§7):
  - [ ] 1. ¿Por qué la linealidad da un costo independiente del número de paquetes en la ventana? (2·d·w contadores por rotación)
  - [ ] 2. ¿Qué diferencias hay entre el error de CMS y el de CS al reducir w?
  - [ ] 3. ¿Qué ataque se detecta con más claridad y por qué la clave es distinta (dst para DDoS, src para scan)?
  - [ ] 4. ¿Qué información agrega Δf_j(x) frente a mirar sólo f_j(x)?
  - [ ] 5. Comparar Δf̂ CS con Δf̂ CMS-mediana. ¿Por qué CS mantiene su estimador y CMS pierde sus garantías?
- [ ] Indicar la **URL de la traza** (`https://mawi.wide.ad.jp/mawi/samplepoint-F/2018/201812031400.pcap.gz`) y la **semilla** de los ataques (42) y de los hash.

## 6. Presentación oral (10 min)

- [ ] Diapositivas sobre el diseño de la ventana deslizante, el uso de la linealidad, los resultados de ambos ataques y las conclusiones de CMS contra CS.

## 7. Repositorio y README

- [ ] Actualizar `README.md`: hoy sólo habla de DDoS con CMS/CS sin parámetros. Faltan el scan, los parámetros por CLI, cómo reproducir todo y la URL y semilla.
- [ ] Confirmar que no se suben trazas (`*.bin` y el pcap ya están en `.gitignore`; agregar también `*.pcap.gz`).
- [ ] Decidir si los CSV y PNG de resultados se versionan; si se versionan, regenerarlos con el código corregido.
- [ ] Decidir si `Tarea1_2026.md` se sube al repo (hoy está sin trackear).
