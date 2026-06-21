# Documentación Técnica: Sistema de Clasificación Acústica en Tiempo Real Mediante Machine Learning Edge y Arquitectura Multinúcleo (ESP32-S3)

---

## 1. Resumen del Sistema

El programa desarrollado es un firmware embebido para el microcontrolador **ESP32-S3** diseñado para capturar, procesar y clasificar señales de audio en tiempo real utilizando Inteligencia Artificial (**Edge Impulse**). El sistema es capaz de discriminar entre eventos acústicos específicos (**Tos** y **Ronquido**) de manera local (**Edge Computing**), sin dependencia de servidores en la nube. 

Para garantizar que el sistema no se bloquee durante el pesado procesamiento de la red neuronal, el firmware se ha estructurado bajo un sistema operativo en tiempo real (**FreeRTOS**), dividiendo el trabajo de forma asíncrona entre los dos núcleos del procesador.

---

## 2. Arquitectura de Software (Multiprocesamiento con FreeRTOS)

El firmware exprime la arquitectura *Dual-Core* del ESP32-S3 mediante la creación de dos tareas (`Tasks`) principales asignadas a núcleos físicos distintos:

### Núcleo 0: Tarea de Audio e Inferencia (`readAudioTask`)
* **Prioridad:** Alta (2).
* **Función:** Encargada de la captura crítica de datos del micrófono por hardware y de la ejecución de los algoritmos matemáticos de la red neuronal. 
* **Razón del núcleo:** El muestreo de audio no puede perder muestras (*underflow*). Al asignarse en exclusiva al Núcleo 0, los cálculos de la IA no interrumpen la continuidad de la escucha.

### Núcleo 1: Tarea de Interfaz Gráfica (`drawDisplayTask`)
* **Prioridad:** Media-Baja (1).
* **Función:** Encargada de la lógica de refresco de la pantalla y de preparar el búfer visual para el osciloscopio en vivo.
* **Mecanismo de Seguridad:** Utiliza un **Mutex (`audioMutex`)**. Dado que ambas tareas intentan acceder al mismo búfer de datos de audio simultáneamente, el Mutex actúa como un semáforo que evita la corrupción de datos en la memoria RAM compartida.

---

## 3. Bloques de Procesamiento y Lógica Operacional

### A. Adquisición de Datos por Protocolo I2S
El micrófono digital (**INMP441**) transmite el audio en bruto a través del protocolo **I2S** en formato de 32 bits de alta fidelidad. El programa configura el periférico por hardware para realizar una lectura constante y limpia, convirtiendo posteriormente las muestras necesarias a floats y enteros de 16 bits requeridos por el clasificador.

### B. Algoritmo de Activación por Persistencia (Filtro Anti-Spam)
Para evitar que el ruido blanco de la habitación o siseos breves saturen el monitor serie con falsas alertas, el código implementa un **filtro inteligente de persistencia**:
1. El sistema evalúa constantemente la amplitud pico del sonido contra un `UMBRAL_ACTIVACION`.
2. Si un ruido supera el umbral, un contador incrementa. Si cae el silencio, decrementa.
3. **La IA solo se dispara si el ruido se mantiene fuerte durante un número consecutivo de bloques mínimos.** Esto asegura que el sistema solo reaccione ante sonidos deliberados y sostenidos (como una tos real o un vídeo de prueba).
4. Tras ejecutar una inferencia, se aplica un bloqueo temporal de **1.5 segundos** (`vTaskDelay`) para ignorar ecos residuales o el final del propio sonido analizado.

### C. Clasificación de Audio (Machine Learning)
Una vez superado el filtro de persistencia, se toman las muestras de audio equivalentes al tamaño de ventana de la IA (`EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE`). Los datos se inyectan en el clasificador de Edge Impulse (`run_classifier`). El modelo procesa la firma acústica y devuelve en el Monitor Serie las probabilidades exactas en porcentaje de las etiquetas entrenadas (`Tos` y `Ronquido`).

### D. Procesamiento Gráfico (Osciloscopio)
Independientemente de la IA, el código reduce la tasa de muestreo del búfer original de audio (*downsampling*) para adaptarlo proporcionalmente al ancho de una pantalla estándar (`SCREEN_WIDTH = 128`). Esto genera un array de coordenadas listo para ser pintado en forma de onda senoidal en tiempo real.

---

## 4. Flujo de Trabajo del Programa (Paso a Paso)

```text
[Inicio / Setup] 
       │
       ▼
[Inicializar Periféricos e I2S] ───► [Creación de Mutex y Tasks (FreeRTOS)]
                                                      │
              ┌───────────────────────────────────────┴──────────────────────────────────────┐
              ▼ (Núcleo 0)                                                                   ▼ (Núcleo 1)
   [Lectura de Micrófono I2S]                                                       [Lectura de Buffer de Audio]
              │                                                                              │ (Bloqueado por Mutex)
              ▼                                                                              ▼
   ¿Supera Umbral Acústico? ──(NO)──► [Bucle]                                        [Procesar Escala del Gráfico]
              │ (SÍ)                                                                         │
              ▼                                                                              ▼
   ¿Ruido Sostenido? (Persistencia) ──(NO)──► [Bucle]                             [Mantener Pantalla Retroiluminada]
              │ (SÍ)
              ▼
   [Capturar Ventana Completa (1s)]
              │
              ▼
   [Ejecutar run_classifier()]
              │
              ▼
   [Imprimir Resultados en Consola]
              │
              ▼
   [Cooldown de Seguridad (1.5s)] ──► [Reiniciar Escucha]
