#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

// ========================================================
// --- 1. INCLUYE AQUÍ TU RED NEURONAL ---
// ========================================================
#include <Snore_detection_and_clasification_inferencing.h> // <-- ¡CAMBIA ESTO!
#include "pantalla.h"

// ========================================================
// --- 2. CONFIGURACIÓN DE PINES ---
// ========================================================
// Pines del micrófono I2S (INMP441)
#define I2S_WS 4
#define I2S_BCK 5
#define I2S_SD 6
#define I2S_PORT I2S_NUM_0

// Pines de la Pantalla OLED SPI
#define OLED_MOSI   11  // Pin SI
#define OLED_CLK    12  // Pin SCL
#define OLED_DC     13  // Pin RS
#define OLED_CS     10  // Pin CS
#define OLED_RESET  15  // Pin RSE

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Inicialización de la pantalla SPI
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

// ========================================================
// --- 3. VARIABLES GLOBALES (Las que te daban error) ---
// ========================================================
#define BUFFER_SIZE 512 
int32_t i2s_buffer[BUFFER_SIZE];
int16_t display_buffer[SCREEN_WIDTH]; 

SemaphoreHandle_t audioMutex;
TaskHandle_t AudioTask;
TaskHandle_t DisplayTask;
TaskHandle_t ReportTask;

unsigned long inicioSueno = 0;

int totalRonquidos = 0;
int totalTos = 0;

const int32_t UMBRAL_ACTIVACION = 1800000; // Sube un pelín desde el millón para ignorar el ruido base del aire
const double SAMPLING_FREQUENCY = 16000;  

// Buffer para la IA (Normalmente 1 segundo = 16000 muestras)
float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

// ========================================================
// --- 4. FUNCIONES AUXILIARES ---
// ========================================================
void imprimirTimestamp() {
    unsigned long total_ms = millis();
    unsigned int ms = total_ms % 1000;
    unsigned int segundos = (total_ms / 1000) % 60;
    unsigned int minutos = ((total_ms / 1000) / 60) % 60;
    unsigned int horas = ((total_ms / 1000) / 3600);
    Serial.printf("[%02u:%02u:%02u.%03u] ", horas, minutos, segundos, ms);
}

void configure_i2s() {
    const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = (uint32_t)SAMPLING_FREQUENCY,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = BUFFER_SIZE,
        .use_apll = false
    };

    const i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_start(I2S_PORT);
}

void mostrarResumenSueno()
{
    float horasDormidas =
        (millis() - inicioSueno) / 3600000.0;

    if(horasDormidas < 0.01)
    {
        horasDormidas = 0.01;
    }

    float ronquidosPorHora =
        totalRonquidos / horasDormidas;

    float nota = 10.0;

    nota -= ronquidosPorHora * 0.15;

    if(nota < 0)
    {
        nota = 0;
    }

    Serial.println();
    Serial.println("================================");
    Serial.println("RESUMEN DEL SUENO");
    Serial.println("================================");

    Serial.print("Horas dormidas: ");
    Serial.println(horasDormidas, 2);

    Serial.print("Ronquidos detectados: ");
    Serial.println(totalRonquidos);

    Serial.print("Tos detectada: ");
    Serial.println(totalTos);

    Serial.print("Ronquidos por hora: ");
    Serial.println(ronquidosPorHora, 2);

    Serial.print("Calidad del sueno: ");
    Serial.print(nota, 1);
    Serial.println("/10");

    Serial.println("================================");

    mostrarResumenPantalla(
        horasDormidas,
        totalRonquidos,
        totalTos,
        nota
    );
}

// ========================================================
// --- 5. NÚCLEO 0: CAPTURA DE AUDIO Y RED NEURONAL (MODIFICADO) ---
// ========================================================
void readAudioTask(void *pvParameters) {
    configure_i2s();
    size_t bytesRead = 0;
    int contador_persistencia = 0;
    const int MUESTRAS_CONSECUTIVAS_REQUERIDAS = 3; // El ruido debe durar un mínimo para activarse

    while (true) {
        esp_err_t result = i2s_read(I2S_PORT, &i2s_buffer, sizeof(i2s_buffer), &bytesRead, portMAX_DELAY);

        if (result == ESP_OK && bytesRead > 0) {
            int muestras_leidas = bytesRead / sizeof(int32_t);
            int32_t max_amplitud = 0;

            for (int i = 0; i < muestras_leidas; i++) {
                if (abs(i2s_buffer[i]) > max_amplitud) {
                    max_amplitud = abs(i2s_buffer[i]);
                }
            }

            // ¿Supera el umbral de sonido?
            if (max_amplitud > UMBRAL_ACTIVACION) {
                contador_persistencia++; // Registramos que el ruido continúa
            } else {
                if (contador_persistencia > 0) contador_persistencia--; // Si cae el silencio, restamos
            }

            // Solo disparamos la IA si el ruido es sostenido (deliberado)
            if (contador_persistencia >= MUESTRAS_CONSECUTIVAS_REQUERIDAS) {
                contador_persistencia = 0; // Reseteamos el filtro
                int feature_index = 0;
                
                // 1. Llenamos el buffer de la IA
                while (feature_index < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
                    i2s_read(I2S_PORT, &i2s_buffer, sizeof(i2s_buffer), &bytesRead, portMAX_DELAY);
                    int muestras = bytesRead / sizeof(int32_t);
                    
                    for (int i = 0; i < muestras && feature_index < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE; i++) {
                        int16_t sample_16bit = i2s_buffer[i] >> 14; 
                        features[feature_index++] = (float)sample_16bit;
                    }
                }

                // 2. Preparamos la señal para la Red Neuronal
                signal_t features_signal;
                int err = numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &features_signal);
                
                if (err == 0) {
                    ei_impulse_result_t ei_result = { 0 };
                    
                    // 3. ¡EJECUTAMOS LA IA!
                    EI_IMPULSE_ERROR res = run_classifier(&features_signal, &ei_result, false);
                    
                    if (res == EI_IMPULSE_OK) {
                        imprimirTimestamp();
                        Serial.println("--- Análisis de IA completado ---");

                                for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++){
                            
                                if(
                                    strcmp(ei_result.classification[i].label, "Ronquidos") == 0 &&
                                    ei_result.classification[i].value > 0.90
                                )
                                {
                                    totalRonquidos++;
                                }

                                if(
                                    strcmp(ei_result.classification[i].label, "Tos") == 0 &&
                                    ei_result.classification[i].value > 0.90
                                )
                                {
                                    totalTos++;
                                }
                            }
                        
                        for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
                            Serial.printf("  %s: %.2f%%\n", ei_result.classification[i].label, ei_result.classification[i].value * 100.0);
                        }
                        Serial.println("---------------------------------");
                    }
                }
                // Pausa de 1.5 segundos tras analizar para ignorar ecos finales del ruido
                vTaskDelay(pdMS_TO_TICKS(1500)); 
            }

            // Actualización del buffer visual (OLED)
            if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                int step = muestras_leidas / SCREEN_WIDTH;
                if (step == 0) step = 1;
                for (int i = 0; i < SCREEN_WIDTH; i++) {
                    display_buffer[i] = i2s_buffer[i * step] >> 14; 
                }
                xSemaphoreGive(audioMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5)); 
    }
}

// ========================================================
// --- 6. NÚCLEO 1: PANTALLA OLED SPI ---
// ========================================================
void drawDisplayTask(void *pvParameters) {
    // Inicialización para pantalla SPI
    if (!display.begin(SSD1306_SWITCHCAPVCC)) { 
        Serial.println("Fallo al iniciar la OLED SPI");
        while (true) vTaskDelay(100);
    }
    
    display.setTextColor(WHITE);
    display.setTextSize(1);
    
    while (true) {
        display.clearDisplay();
        
        display.setCursor(0, 0);
        display.print("Ronquidos C05 - IA");
        display.drawLine(0, 10, SCREEN_WIDTH, 10, WHITE);
        
        int center_y = 12 + (SCREEN_HEIGHT - 12) / 2;
        for(int i=0; i<SCREEN_WIDTH; i+=4) display.drawPixel(i, center_y, WHITE); 

        if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
            int32_t max_val = 100; 
            for (int i = 0; i < SCREEN_WIDTH; i++) {
                if (abs(display_buffer[i]) > max_val) max_val = abs(display_buffer[i]);
            }

            for (int i = 0; i < SCREEN_WIDTH - 1; i++) {
                int y1 = map(display_buffer[i], -max_val, max_val, SCREEN_HEIGHT - 1, 12);
                int y2 = map(display_buffer[i+1], -max_val, max_val, SCREEN_HEIGHT - 1, 12);
                
                y1 = constrain(y1, 12, SCREEN_HEIGHT - 1);
                y2 = constrain(y2, 12, SCREEN_HEIGHT - 1);
                display.drawLine(i, y1, i + 1, y2, WHITE);
            }
            xSemaphoreGive(audioMutex); 
        }
        display.display();
        vTaskDelay(pdMS_TO_TICKS(33)); // ~30 FPS
    }
}

void reportTask(void *pvParameters)
{
    while(true)
    {
        if(Serial.available())
        {
            char c = Serial.read();

            if(c == 'R' || c == 'r')
            {
                mostrarResumenSueno();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ========================================================
// --- 7. ARRANQUE DEL SISTEMA ---
// ========================================================
void setup() {
    inicioSueno = millis();
    Serial.begin(115200);
    audioMutex = xSemaphoreCreateMutex();

    // Damos más memoria a la tarea de Audio
    xTaskCreatePinnedToCore(readAudioTask, "AudioTask", 15000, NULL, 2, &AudioTask, 0);
    xTaskCreatePinnedToCore(drawDisplayTask, "DisplayTask", 10000, NULL, 1, &DisplayTask, 1);
    xTaskCreatePinnedToCore(reportTask,"ReportTask", 4000,NULL,1,&ReportTask,1);
    
  }

void loop() {
    vTaskDelete(NULL); // Eliminamos el loop por defecto, usamos FreeRTOS
}