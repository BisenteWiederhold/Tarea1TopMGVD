import csv
import matplotlib.pyplot as plt

ventanas = []
f_exacto = []
f_cms = []
f_cs = []

# Leer el Ground Truth
with open('exact_ddos.csv', 'r') as file:
    reader = csv.reader(file)
    next(reader)  
    for row in reader:
        f_exacto.append(float(row[6]))

# Leer Count-Min Sketch
with open('resultados_cms.csv', 'r') as file:
    reader = csv.reader(file)
    for row in reader:
        ventanas.append(int(row[0])) 
        f_cms.append(float(row[2]))  

# Leer CountSketch
with open('resultados_cs.csv', 'r') as file:
    reader = csv.reader(file)
    for row in reader:
        f_cs.append(float(row[2]))   

# Calcular el error absoluto de cada algoritmo
error_cms = [abs(cms - exacto) for cms, exacto in zip(f_cms, f_exacto)]
error_cs = [abs(cs - exacto) for cs, exacto in zip(f_cs, f_exacto)]

# Crear una figura con 2 subgráficos apilados verticalmente
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 10))

# Panel 1: Tráfico Absoluto (Escala Lineal)
ax1.plot(ventanas, f_exacto, label='Frecuencia Exacta', color='black', linestyle='--', linewidth=2)
ax1.plot(ventanas, f_cms, label='Count-Min Sketch', color='red', alpha=0.7)
ax1.plot(ventanas, f_cs, label='CountSketch', color='blue', alpha=0.7)
ax1.set_title('Detección de Ataque DDoS: Volumen de Tráfico')
ax1.set_ylabel('Paquetes hacia IP Víctima')
ax1.legend()
ax1.grid(True, linestyle=':', alpha=0.6)

# Panel 2: Error Absoluto (Escala Semi-Logarítmica)
ax2.plot(ventanas, error_cms, label='Error Absoluto CMS', color='red', alpha=0.8)
ax2.plot(ventanas, error_cs, label='Error Absoluto CS', color='blue', alpha=0.8)
ax2.set_title('Precisión de los Algoritmos (|Estimado - Exacto|)')
ax2.set_xlabel('Ventana Temporal (q)')
ax2.set_ylabel('Error en Paquetes (Escala Log)')
ax2.set_yscale('symlog', linthresh=10) # Soporta ceros en escala logarítmica
ax2.legend()
ax2.grid(True, linestyle=':', alpha=0.6)

plt.tight_layout()
plt.savefig('grafico_ddos_completo.png', dpi=300, bbox_inches='tight')
print("¡Gráfico generado exitosamente como grafico_ddos_completo.png!")