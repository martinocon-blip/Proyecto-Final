# Proyecto-Final

Estado del Proyecto C.05: Detector de Ronquidos (ML Edge)
Contexto rápido: Hemos tenido que pivotar la estrategia de visualización. Intentamos mandar los datos del micrófono por WiFi/WebSockets a una página web local, pero el micro I2S captura 16.000 muestras por segundo. Intentar mandar eso por WiFi saturaba la cola de mensajes del ESP32-S3 y la placa cortaba la conexión por seguridad. Para tener un proyecto robusto que soporte la IA más adelante, hemos pasado a una arquitectura profesional Dual Core con FreeRTOS y visualización en pantalla OLED física.

✅ Fases Completadas (Lo que ya tenemos)
[x] Configuración Base: Entorno montado en PlatformIO para la ESP32-S3-DevKitC1.

[x] Captura de Audio (Hardware): Micrófono INMP441 conectado y configurado por bus I2S. Estamos leyendo datos crudos en contenedores de 32 bits a 16 kHz.

[x] Arquitectura Multihilo (FreeRTOS): Hemos separado las tareas para no bloquear el procesador:

Núcleo 0: Dedicado exclusivamente a leer el micrófono I2S mediante DMA (acceso directo a memoria).

Núcleo 1: Dedicado a actualizar la interfaz visual de forma segura usando semáforos (Mutex).

[x] Código de Visualización: Implementación de la librería de Adafruit para dibujar la onda de audio en tiempo real en una pantalla OLED 0.96" (I2C).

⏳ Próximos Pasos (Lo que toca hacer ahora)
[ ] 1. Pruebas de Hardware (OLED): * Comentario: Hay que conectar físicamente la pantalla OLED a los pines I2C (SDA = 8, SCL = 9 en el código actual, modificables si es necesario) y subir el último código generado para verificar que la onda se dibuja a unos 30 FPS de forma fluida.

[ ] 2. Procesamiento de Señal (FFT):

Comentario: Ahora mismo solo vemos la onda en crudo (dominio del tiempo). El siguiente paso técnico es integrar la librería arduinoFFT dentro del Núcleo 0. Hay que convertir esos bloques de audio en bandas de frecuencia (dominio de la frecuencia) para poder identificar patrones acústicos.

[ ] 3. Modelo de Machine Learning (TinyML):

Comentario: Una vez tengamos las frecuencias claras, hay que capturar muestras (ronquidos vs. silencio/ruido de fondo) para entrenar una pequeña red neuronal. Luego, usaremos TensorFlow Lite Micro para correr ese modelo dentro del ESP32-S3 y que clasifique el sonido en tiempo real.

[ ] 4. Conectividad Final (Opcional/Según Rúbrica):

Comentario: Una vez el ML Edge detecte un "ronquido", usar la radio WiFi/Bluetooth del ESP32 para mandar un simple flag o alerta (esto ya no satura la red, porque solo se envía un mensaje tipo "Ronquido Detectado", en lugar de miles de números por segundo).
