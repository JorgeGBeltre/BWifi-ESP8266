#include "encoder_input.h"

// Lectura rápida del registro GPIO en RAM para ESP8266 (GPIO 0..15)
static inline uint8_t IRAM_ATTR fastReadPin(uint8_t pin) {
  if (pin < 16) {
    return (uint8_t)((GPI >> pin) & 0x01U);
  }
  return (uint8_t)digitalRead(pin);
}

// --- MÁQUINA DE ESTADOS DE CÓDIGO GRAY DE 7 ESTADOS (HW-040 / KY-040) ---
#define R_START     0x00
#define R_CW_FINAL  0x01
#define R_CW_BEGIN  0x02
#define R_CW_NEXT   0x03
#define R_CCW_BEGIN 0x04
#define R_CCW_FINAL 0x05
#define R_CCW_NEXT  0x06

#define DIR_NONE 0x00
#define DIR_CW   0x10
#define DIR_CCW  0x20

static const uint8_t kStateTable[7][4] = {
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

static volatile int32_t s_isrQuarterSteps = 0;
static volatile uint8_t s_encState = R_START;
static volatile bool    s_rotatedWhileSwLow = false;
static volatile bool    s_invertDirRuntime = (ENC_INVERT_DIR != 0);

static int32_t  s_pendingSteps   = 0;
static uint32_t s_lastUpStepMs   = 0;
static uint32_t s_lastDownStepMs = 0;
static uint32_t s_lastActivityMs = 0;

static bool     s_swStable       = true;
static bool     s_swLastRaw      = true;
static uint32_t s_swDebounceMs   = 0;
static uint32_t s_swPressStartMs = 0;
static bool     s_swLongFired    = false;
static bool     s_swRotated      = false;

static bool s_okEvent     = false;
static bool s_okDownEvent = false;
static bool s_backEvent   = false;
static bool s_auxEvent    = false;

static inline void IRAM_ATTR processEncoderPins() {
  uint8_t clk = fastReadPin(ENC_CLK);
  uint8_t dt  = fastReadPin(ENC_DT);
  uint8_t pinState = (uint8_t)((clk << 1) | dt);

  s_encState = kStateTable[s_encState & 0x0F][pinState];
  uint8_t result = s_encState & 0x30;

  if (result != DIR_NONE) {
    int8_t step = (result == DIR_CW) ? 1 : -1;
    if (s_invertDirRuntime) {
      step = -step;
    }
    s_isrQuarterSteps += (int32_t)step * 4;
    if (fastReadPin(ENC_SW) == 0) {
      s_rotatedWhileSwLow = true;
    }
  }
}

void IRAM_ATTR encoderISR() {
  processEncoderPins();
}

void encoderInit() {
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);

  s_encState = R_START;
  s_swStable  = (fastReadPin(ENC_SW) != 0);
  s_swLastRaw = s_swStable;
  s_lastActivityMs = millis();

  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT),  encoderISR, CHANGE);
}

void encoderUpdate() {
  uint32_t now = millis();

  processEncoderPins();

  noInterrupts();
  int32_t q = s_isrQuarterSteps;
  int32_t whole = q / 4;
  if (whole != 0) {
    s_isrQuarterSteps -= whole * 4;
  }
  bool rotWhilePressed = s_rotatedWhileSwLow;
  s_rotatedWhileSwLow = false;
  interrupts();

  if (rotWhilePressed) {
    s_swRotated = true;
    s_auxEvent  = true;
    s_lastActivityMs = now;
  }

  if (whole != 0) {
    s_pendingSteps += whole;
    s_lastActivityMs = now;
    if (whole < 0) {
      s_lastUpStepMs = now;
    } else {
      s_lastDownStepMs = now;
    }
  }

  bool rawSw = (fastReadPin(ENC_SW) != 0);
  if (rawSw != s_swLastRaw) {
    s_swLastRaw    = rawSw;
    s_swDebounceMs = now;
  }

  if ((now - s_swDebounceMs) >= ENC_DEBOUNCE_MS) {
    if (rawSw != s_swStable) {
      s_swStable = rawSw;
      s_lastActivityMs = now;
      if (!s_swStable) {
        s_swPressStartMs = now;
        s_swLongFired    = false;
        s_swRotated      = false;
        s_okDownEvent    = true;
      } else {
        if (!s_swLongFired && !s_swRotated) {
          s_okEvent = true;
        }
      }
    }
  }

  if (!s_swStable && !s_swLongFired && !s_swRotated) {
    if ((now - s_swPressStartMs) >= ENC_LONG_PRESS_MS) {
      s_swLongFired = true;
      s_backEvent   = true;
      s_lastActivityMs = now;
    }
  }
}

void encClear() {
  noInterrupts();
  s_isrQuarterSteps   = 0;
  s_rotatedWhileSwLow = false;
  interrupts();

  s_pendingSteps = 0;
  s_okEvent      = false;
  s_okDownEvent  = false;
  s_backEvent    = false;
  s_auxEvent     = false;
  if (!s_swStable) {
    s_swLongFired = true;
  }
}

int encStep() {
  int s = (int)s_pendingSteps;
  s_pendingSteps = 0;
  return s;
}

bool encUp() {
  if (s_pendingSteps < 0) {
    s_pendingSteps++;
    return true;
  }
  return false;
}

bool encDown() {
  if (s_pendingSteps > 0) {
    s_pendingSteps--;
    return true;
  }
  return false;
}

bool encUpHeld(uint32_t windowMs) {
  if (s_pendingSteps < 0) {
    s_pendingSteps = 0;
    return true;
  }
  return (s_lastUpStepMs != 0) && ((millis() - s_lastUpStepMs) <= windowMs);
}

bool encDownHeld(uint32_t windowMs) {
  if (s_pendingSteps > 0) {
    s_pendingSteps = 0;
    return true;
  }
  return (s_lastDownStepMs != 0) && ((millis() - s_lastDownStepMs) <= windowMs);
}

bool encOk() {
  if (s_okEvent) {
    s_okEvent = false;
    return true;
  }
  return false;
}

bool encOkDown() {
  if (s_okDownEvent) {
    s_okDownEvent = false;
    return true;
  }
  return false;
}

bool encBack() {
  if (s_backEvent) {
    s_backEvent = false;
    return true;
  }
  return false;
}

bool encAux() {
  if (s_auxEvent) {
    s_auxEvent = false;
    return true;
  }
  return false;
}

bool encSwPressed() {
  return !s_swStable;
}

void encSetInvertDir(bool invert) {
  s_invertDirRuntime = invert;
}

bool encGetInvertDir() {
  return s_invertDirRuntime;
}

uint32_t encLastActivityMs() {
  return s_lastActivityMs;
}

void encTouchActivity() {
  s_lastActivityMs = millis();
}

