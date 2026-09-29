# Tarea 1: Detección de Ataques DDoS con Sketches

Proyecto para la asignatura Tópicos en Manejos de Grandes Volúmenes de Datos (UdeC). 

El objetivo es procesar una traza de red pesada para encontrar un ataque DDoS dirigido a una IP específica. Para no colapsar la RAM del computador, se implementaron dos algoritmos probabilísticos: **Count-Min Sketch** y **CountSketch**.

## Qué necesitas descargar para probarlo

Para correr los experimentos en tu máquina, asegúrate de tener lo siguiente:

*   **La traza binaria (`traza_ddos.bin`):** Como pesa alrededor de 3 GB, no está subida a GitHub. Debes descargarla desde la plataforma del curso y guardarla en la misma carpeta que los códigos fuente.
*   **Compilador C++:** Necesitas `g++` para compilar los scripts. En Windows, lo ideal es usar el entorno MSYS2 (MinGW).

# Carpeta
*   `tarea_cms.cpp`: Implementación del Count-Min Sketch.
*   `tarea_cs.cpp`: Implementación del CountSketch (usa el signo y la mediana para limpiar el ruido).
*   `exact_hh.cpp`: Código base para sacar la frecuencia real (Ground Truth) directamente de la traza.
*   `graficos.py`: Script de Python que lee los CSV y arma el gráfico final comparativo.

*Nota: El archivo original de la traza (`traza_ddos.bin`) no está subido porque supera el límite de tamaño de GitHub. Debes colocarlo en la misma carpeta antes de correr los códigos.*

## Cómo probarlo

### 1. Obtener la frecuencia exacta (Ground Truth)
Primero compilamos el archivo de referencia para extraer los paquetes exactos dirigidos a la IP víctima:
```bash
g++ -O2 -o exact_hh exact_hh.cpp
./exact_hh traza_ddos.bin --key dst -W 60 --delta 10 --phi 0.01 --query 163.210.30.13 --out-query exact_ddos.csv
```

#Ejecucion de algoritmos

Count-Min Sketch
```bash
g++ -O2 -o tarea_cms tarea_cms.cpp
./tarea_cms
```

# CountSketch
```bash
g++ -O2 -o tarea_cs tarea_cs.cpp
./tarea_cs
```



