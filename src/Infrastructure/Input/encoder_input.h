#ifndef ENCODER_INPUT_H
#define ENCODER_INPUT_H

#include <Arduino.h>

// --- PINES ROTARY ENCODER HW-040 (KY-040) EN ESP8266 (NodeMCU / Wemos D1 Mini) ---
// GPIO 0 (D3) y GPIO 2 (D4) tienen pull-up de arranque seguro y soportan ISR en RAM
// GPIO 3 (RX) se usa para el pulsador SW (Serial opera en modo TX-only por GPIO 1)
#ifndef ENC_CLK
#define ENC_CLK 0               // D3 -> Fase A / CLK del HW-040
#endif

#ifndef ENC_DT
#define ENC_DT 2                // D4 -> Fase B / DT del HW-040
#endif

#ifndef ENC_SW
#define ENC_SW 3                // RX (GPIO 3) -> Pulsador integrado SW del HW-040
#endif

// Sentido del giro: 1 invierte CW/CCW, 0 deja el sentido nativo
#ifndef ENC_INVERT_DIR
#define ENC_INVERT_DIR 1
#endif

// Tiempos de respuesta del pulsador SW del encoder (ms)
#ifndef ENC_LONG_PRESS_MS
#define ENC_LONG_PRESS_MS 450
#endif

#ifndef ENC_DEBOUNCE_MS
#define ENC_DEBOUNCE_MS 12
#endif

// Compatibilidad de macros heredadas
#define BTN_BACK_PIN ENC_SW
#define BTN_AUX_PIN  ENC_SW

void encoderInit();
void encoderUpdate();
void encClear();

int  encStep();
bool encUp();
bool encDown();
bool encUpHeld(uint32_t windowMs = 180);
bool encDownHeld(uint32_t windowMs = 180);

bool encOk();
bool encOkDown();
bool encBack();
bool encAux();
bool encSwPressed();

void encSetInvertDir(bool invert);
bool encGetInvertDir();
uint32_t encLastActivityMs();
void encTouchActivity();

#endif

