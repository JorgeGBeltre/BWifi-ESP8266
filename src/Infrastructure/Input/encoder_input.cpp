#include "encoder_input.h"

// ============================================================================
//  DRIVER DE PRECISIÓN PARA ROTARY ENCODER HW-040 (KY-040) EN ESP8266
//  - Decodificación por máquina de estados Gray-code (Ben Buxton) en IRAM_ATTR
//  - Inmune a rebotes mecánicos sin requerir capacitores externos
//  - Aceleración adaptativa en giros rápidos + captura ISR de pulsador SW
//  - Pulsador SW multifunción: Clic corto (OK), Pulsación larga (BACK),
//    Giro con eje presionado (AUX / Acción secundaria)
// ============================================================================

#define R_START     0x0
#define R_CW_FINAL  0x1
#define R_CW_BEGIN  0x2
#define R_CW_NEXT   0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT  0x6

#define DIR_NONE    0x00
#define DIR_CW      0x10
#define DIR_CCW     0x20

static const uint8_t kBuxtonTable[7][4] = {
    // R_START (00)
    {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
    // R_CW_FINAL (01)
    {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
    // R_CW_BEGIN (02)
    {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
    // R_CW_NEXT (03)
    {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
    // R_CCW_BEGIN (04)
    {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
    // R_CCW_FINAL (05)
    {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
    // R_CCW_NEXT (06)
    {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
};

static volatile uint8_t  s_encState       = R_START;
static volatile uint8_t  s_lastPinState   = 0x03;
static volatile int16_t  s_accumSteps     = 0;
static volatile uint32_t s_lastStepTime   = 0;
static volatile int8_t   s_lastStepDir    = 0;
static volatile uint32_t s_lastCwMs       = 0;
static volatile uint32_t s_lastCcwMs      = 0;
static volatile uint32_t s_lastActivityMs = 0;
static volatile bool     s_swRotated      = false;
static volatile bool     s_invertDir      = (ENC_INVERT_DIR != 0);

// Captura por interrupción del flanco de bajada de SW (evita perder toques rápidos)
static volatile bool     s_swIsrEdgePending = false;
static volatile uint32_t s_swIsrEdgeUs      = 0;

// Estado del pulsador SW del eje
static bool     s_swRawLast     = HIGH;
static bool     s_swStable      = HIGH;
static uint32_t s_swDebounceMs  = 0;
static uint32_t s_swPressMs     = 0;
static bool     s_swLongHandled = false;

// Banderas de eventos listos para consumir
static bool s_okEvent     = false;
static bool s_okDownEvent = false;
static bool s_backEvent   = false;
static bool s_auxEvent    = false;

IRAM_ATTR void encoderIsr() {
  uint8_t clk = (uint8_t)digitalRead(ENC_CLK);
  uint8_t dt  = (uint8_t)digitalRead(ENC_DT);
  uint8_t pinState = (uint8_t)((dt << 1) | clk);

  if (pinState == s_lastPinState) {
    return;
  }
  s_lastPinState = pinState;

  s_encState = kBuxtonTable[s_encState & 0x07][pinState];
  uint8_t result = s_encState & 0x30;

  if (result == DIR_CW || result == DIR_CCW) {
    uint32_t now = millis();
    uint32_t dtMs = now - s_lastStepTime;

    int8_t dir = (result == DIR_CW) ? 1 : -1;
    if (s_invertDir) {
      dir = -dir;
    }

    // Filtro de asentamiento mecánico (< 5 ms mismo sentido = rebote residual)
    if (dir == s_lastStepDir && dtMs < 5) {
      return;
    }

    s_lastStepTime = now;
    s_lastStepDir  = dir;
    s_lastActivityMs = now;

    // Aceleración suave en giros rápidos (solo en mismo sentido consecutivo)
    int8_t weight = 1;
    if (dir == s_lastStepDir && dtMs > 6 && dtMs < 24) {
      weight = 2;
    }

    if (digitalRead(ENC_SW) == LOW) {
      s_swRotated = true;
    }

    s_accumSteps = (int16_t)(s_accumSteps + (dir * weight));
    if (dir > 0) {
      s_lastCwMs = now;
    } else {
      s_lastCcwMs = now;
    }
  }
}

IRAM_ATTR void swIsr() {
  uint32_t nowUs = micros();
  if ((nowUs - s_swIsrEdgeUs) > 15000U) { // 15 ms anti-rebote en µs
    s_swIsrEdgeUs = nowUs;
    s_swIsrEdgePending = true;
    s_lastActivityMs = millis();
  }
}

void encoderInit() {
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);

  uint8_t clk = (uint8_t)digitalRead(ENC_CLK);
  uint8_t dt  = (uint8_t)digitalRead(ENC_DT);
  s_lastPinState = (uint8_t)((dt << 1) | clk);
  s_encState = R_START;

  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encoderIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT),  encoderIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_SW),  swIsr,      FALLING);

  s_lastActivityMs = millis();
  encClear();
}

void encoderUpdate() {
  uint32_t now = millis();
  bool rawSw = (digitalRead(ENC_SW) == HIGH);

  // Si hubo un pulso ultrarrápido en SW capturado por ISR durante un frame pesado
  noInterrupts();
  bool fastEdge = s_swIsrEdgePending;
  s_swIsrEdgePending = false;
  interrupts();

  if (fastEdge && rawSw == HIGH && s_swStable == HIGH) {
    s_okDownEvent = true;
    s_okEvent     = true;
    s_lastActivityMs = now;
  }

  if (rawSw != s_swRawLast) {
    s_swDebounceMs = now;
    s_swRawLast = rawSw;
  }

  if ((now - s_swDebounceMs) >= ENC_DEBOUNCE_MS) {
    if (rawSw != s_swStable) {
      s_swStable = rawSw;
      s_lastActivityMs = now;

      if (s_swStable == LOW) {
        // Flanco de bajada (botón presionado)
        s_swPressMs     = now;
        s_swLongHandled = false;
        s_swRotated     = false;
        s_okDownEvent   = true;
      } else {
        // Flanco de subida (botón liberado)
        if (!s_swLongHandled && !s_swRotated) {
          s_okEvent = true;
        }
        s_swRotated = false;
      }
    }
  }

  // Mientras el eje permanece presionado
  if (s_swStable == LOW) {
    if (s_swRotated && !s_swLongHandled) {
      s_auxEvent      = true;
      s_swLongHandled = true;
      s_lastActivityMs = now;
    } else if (!s_swLongHandled && (now - s_swPressMs) >= ENC_LONG_PRESS_MS) {
      s_backEvent     = true;
      s_swLongHandled = true;
      s_lastActivityMs = now;
    }
  }
}

void encClear() {
  noInterrupts();
  s_accumSteps       = 0;
  s_swRotated        = false;
  s_swIsrEdgePending = false;
  interrupts();

  s_okEvent     = false;
  s_okDownEvent = false;
  s_backEvent   = false;
  s_auxEvent    = false;

  if (digitalRead(ENC_SW) == LOW) {
    s_swLongHandled = true;
  }
}

int encStep() {
  encoderUpdate();
  noInterrupts();
  int16_t steps = s_accumSteps;
  s_accumSteps = 0;
  interrupts();
  return (int)steps;
}

bool encUp() {
  encoderUpdate();
  noInterrupts();
  if (s_accumSteps < 0) {
    s_accumSteps++;
    interrupts();
    return true;
  }
  interrupts();
  return false;
}

bool encDown() {
  encoderUpdate();
  noInterrupts();
  if (s_accumSteps > 0) {
    s_accumSteps--;
    interrupts();
    return true;
  }
  interrupts();
  return false;
}

bool encUpHeld(uint32_t windowMs) {
  bool stepped = encUp();
  return stepped || ((millis() - s_lastCcwMs) <= windowMs && s_lastCcwMs != 0);
}

bool encDownHeld(uint32_t windowMs) {
  bool stepped = encDown();
  return stepped || ((millis() - s_lastCwMs) <= windowMs && s_lastCwMs != 0);
}

bool encOk() {
  encoderUpdate();
  if (s_okEvent) {
    s_okEvent = false;
    return true;
  }
  return false;
}

bool encOkDown() {
  encoderUpdate();
  if (s_okDownEvent) {
    s_okDownEvent = false;
    return true;
  }
  return false;
}

bool encBack() {
  encoderUpdate();
  if (s_backEvent) {
    s_backEvent = false;
    return true;
  }
  return false;
}

bool encAux() {
  encoderUpdate();
  if (s_auxEvent) {
    s_auxEvent = false;
    return true;
  }
  return false;
}

bool encSwPressed() {
  return (s_swStable == LOW);
}

void encSetInvertDir(bool invert) {
  s_invertDir = invert;
}

bool encGetInvertDir() {
  return s_invertDir;
}

uint32_t encLastActivityMs() {
  return s_lastActivityMs;
}

void encTouchActivity() {
  s_lastActivityMs = millis();
}
