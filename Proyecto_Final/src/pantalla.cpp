#include "pantalla.h"

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

extern Adafruit_SSD1306 display;

void mostrarResumenPantalla(
    float horasDormidas,
    int ronquidos,
    int tos,
    float nota
)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("RESUMEN SUENO");

    display.setCursor(0,16);
    display.print("Horas:");
    display.println(horasDormidas,1);

    display.setCursor(0,28);
    display.print("Ronq:");
    display.println(ronquidos);

    display.setCursor(0,40);
    display.print("Tos:");
    display.println(tos);

    display.setCursor(0,52);
    display.print("Nota:");
    display.print(nota,1);
    display.print("/10");

    display.display();
}