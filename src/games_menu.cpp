#include "games_menu.h"
#include "snake_game.h"
#include "pong_game.h"
#include "flappy_game.h"
#include "space_game.h"
#include "dino_game.h"
#include "encoder_input.h"
#include "settings_menu.h"
#include <U8g2lib.h>
#include <math.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

int gameSelection = 0;
int activeGame = 0; // 0: Menu, 1..10: Juegos Arcade

int getArcadeHighScore(int idx) {
    if (idx < 0 || idx >= 10) return 0;
    return g_settings.arcadeHi[idx];
}

static void saveArcadeHighScore(int idx, int score) {
    if (idx < 0 || idx >= 10) return;
    if (score > g_settings.arcadeHi[idx]) {
        g_settings.arcadeHi[idx] = score;
        settingsSave();
    }
}

// ============================================================================
// JUEGO 6: BREAKOUT (ARKANOID)
// ============================================================================
static float s_brkPaddleX = 48.0f;
static float s_brkBallX = 64.0f, s_brkBallY = 48.0f;
static float s_brkVX = 1.6f, s_brkVY = -1.6f;
static uint8_t s_brkBricks[3][8];
static int s_brkScore = 0;
static bool s_brkGameOver = false;

static void breakoutSetup() {
    s_brkPaddleX = 50.0f;
    s_brkBallX = 64.0f;
    s_brkBallY = 46.0f;
    s_brkVX = 1.65f;
    s_brkVY = -1.65f;
    s_brkScore = 0;
    s_brkGameOver = false;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 8; c++) {
            s_brkBricks[r][c] = 1;
        }
    }
    encClear();
}

static void breakoutLoop(bool &inGameRef) {
    if (encBack()) {
        saveArcadeHighScore(5, s_brkScore);
        inGameRef = false;
        encClear();
        return;
    }

    if (s_brkGameOver) {
        saveArcadeHighScore(5, s_brkScore);
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(22, 20, "BREAKOUT FIN!");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(20, 34);
        u8g2.printf("Puntos: %d (HI: %d)", s_brkScore, g_settings.arcadeHi[5]);
        u8g2.drawStr(14, 50, "[OK] Reintentar [HOLD] Salir");
        u8g2.sendBuffer();
        if (encOk()) breakoutSetup();
        return;
    }

    int d = encStep();
    if (d != 0) {
        s_brkPaddleX = constrain(s_brkPaddleX + d * 9.0f, 2.0f, 102.0f);
    }

    s_brkBallX += s_brkVX;
    s_brkBallY += s_brkVY;

    if (s_brkBallX <= 2.0f || s_brkBallX >= 125.0f) s_brkVX = -s_brkVX;
    if (s_brkBallY <= 11.0f) s_brkVY = -s_brkVY;

    if (s_brkVY > 0 && s_brkBallY >= 56.0f && s_brkBallY <= 60.0f) {
        if (s_brkBallX >= s_brkPaddleX - 2.0f && s_brkBallX <= s_brkPaddleX + 26.0f) {
            s_brkVY = -fabsf(s_brkVY);
            float hitOffset = (s_brkBallX - (s_brkPaddleX + 12.0f)) / 12.0f;
            s_brkVX = constrain(hitOffset * 2.3f, -2.4f, 2.4f);
            if (fabsf(s_brkVX) < 0.6f) s_brkVX = (s_brkVX >= 0) ? 0.8f : -0.8f;
        }
    }

    int remaining = 0;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 8; c++) {
            if (!s_brkBricks[r][c]) continue;
            remaining++;
            int bx = 4 + c * 15;
            int by = 14 + r * 6;
            if (s_brkBallX >= bx && s_brkBallX <= bx + 13 &&
                s_brkBallY >= by && s_brkBallY <= by + 4) {
                s_brkBricks[r][c] = 0;
                s_brkVY = -s_brkVY;
                s_brkScore += 10;
            }
        }
    }

    if (remaining == 0) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 8; c++) s_brkBricks[r][c] = 1;
        s_brkBallY = 44.0f;
        s_brkVY = -1.85f;
    }

    if (s_brkBallY > 63.0f) {
        s_brkGameOver = true;
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 8);
    u8g2.printf("BREAKOUT:%d HI:%d", s_brkScore, max(s_brkScore, g_settings.arcadeHi[5]));
    u8g2.drawHLine(0, 10, 128);

    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 8; c++) {
            if (s_brkBricks[r][c]) {
                u8g2.drawBox(4 + c * 15, 14 + r * 6, 13, 4);
            }
        }
    }

    u8g2.drawRBox((int)s_brkPaddleX, 58, 24, 4, 1);
    u8g2.drawDisc((int)s_brkBallX, (int)s_brkBallY, 2);
    u8g2.sendBuffer();
    delay(14);
}

// ============================================================================
// JUEGO 7: SIMON MEMORIA
// ============================================================================
static uint8_t s_simonSeq[32];
static int s_simonLen = 1;
static int s_simonUserPos = 0;
static int s_simonCursor = 0;
static bool s_simonShowing = true;
static bool s_simonOver = false;

static void simonSetup() {
    for (int i = 0; i < 32; i++) s_simonSeq[i] = random(0, 4);
    s_simonLen = 1;
    s_simonUserPos = 0;
    s_simonCursor = 0;
    s_simonShowing = true;
    s_simonOver = false;
    encClear();
}

static void drawSimonBoard(int highlightIdx, const char *statusMsg) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 8);
    int sc = s_simonLen - 1;
    u8g2.printf("SIMON NVL:%d HI:%d", s_simonLen, max(sc, g_settings.arcadeHi[6]));
    u8g2.drawHLine(0, 10, 128);

    const char *labels[4] = {"1:NO", "2:NE", "3:SO", "4:SE"};
    int coords[4][2] = {{10, 14}, {68, 14}, {10, 34}, {68, 34}};

    for (int i = 0; i < 4; i++) {
        int x = coords[i][0];
        int y = coords[i][1];
        if (i == highlightIdx) {
            u8g2.drawRBox(x, y, 48, 16, 3);
            u8g2.setDrawColor(0);
            u8g2.drawStr(x + 12, y + 11, labels[i]);
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawRFrame(x, y, 48, 16, 3);
            if (!s_simonShowing && i == s_simonCursor) {
                u8g2.drawFrame(x + 2, y + 2, 44, 12);
            }
            u8g2.drawStr(x + 12, y + 11, labels[i]);
        }
    }

    u8g2.drawHLine(0, 53, 128);
    u8g2.drawStr(4, 62, statusMsg);
    u8g2.sendBuffer();
}

static void simonLoop(bool &inGameRef) {
    if (encBack()) {
        saveArcadeHighScore(6, s_simonLen - 1);
        inGameRef = false;
        encClear();
        return;
    }

    if (s_simonOver) {
        int sc = s_simonLen - 1;
        saveArcadeHighScore(6, sc);
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(16, 20, "MEMORIA FALLIDA!");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(18, 35);
        u8g2.printf("Rondas: %d (HI: %d)", sc, g_settings.arcadeHi[6]);
        u8g2.drawStr(10, 52, "[OK] Otra vez [HOLD] Salir");
        u8g2.sendBuffer();
        if (encOk()) simonSetup();
        return;
    }

    if (s_simonShowing) {
        drawSimonBoard(-1, "Observa la secuencia...");
        delay(350);
        for (int i = 0; i < s_simonLen; i++) {
            encoderUpdate();
            if (encBack()) { inGameRef = false; return; }
            drawSimonBoard(s_simonSeq[i], "Memoriza...");
            delay(360);
            drawSimonBoard(-1, "Memoriza...");
            delay(140);
        }
        s_simonShowing = false;
        s_simonUserPos = 0;
        encClear();
        return;
    }

    int d = encStep();
    if (d != 0) {
        s_simonCursor = (s_simonCursor + d) % 4;
        if (s_simonCursor < 0) s_simonCursor += 4;
    }

    if (encOk()) {
        drawSimonBoard(s_simonCursor, "Verificando...");
        delay(160);
        if (s_simonCursor == s_simonSeq[s_simonUserPos]) {
            s_simonUserPos++;
            if (s_simonUserPos >= s_simonLen) {
                saveArcadeHighScore(6, s_simonLen);
                if (s_simonLen < 31) s_simonLen++;
                s_simonShowing = true;
            }
        } else {
            s_simonOver = true;
        }
        encClear();
        return;
    }

    drawSimonBoard(-1, "Gira:Elegir  OK:Marcar");
}

// ============================================================================
// JUEGO 8: TETRIS (Gira=Mover/Rotar con SW+Giro, OK=Rotar, Caída automática)
// ============================================================================
static uint16_t s_tetBoard[16];
static int s_tetPiece = 0;
static int s_tetRot = 0;
static int s_tetX = 3;
static int s_tetY = 0;
static int s_tetScore = 0;
static bool s_tetOver = false;
static uint32_t s_tetLastDropMs = 0;

static const uint16_t kTetShapes[7][4] = {
    {0x0F00, 0x2222, 0x0F00, 0x2222}, // I
    {0x0660, 0x0660, 0x0660, 0x0660}, // O
    {0x0E40, 0x4C40, 0x4E00, 0x4640}, // T
    {0x06C0, 0x8C40, 0x06C0, 0x8C40}, // S
    {0x0C60, 0x4C80, 0x0C60, 0x4C80}, // Z
    {0x0E20, 0x44C0, 0x8E00, 0x6440}, // J
    {0x0E80, 0xC440, 0x2E00, 0x4460}  // L
};

static bool tetCollide(int px, int py, int piece, int rot) {
    uint16_t mask = kTetShapes[piece][rot & 3];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (mask & (0x8000 >> (r * 4 + c))) {
                int bx = px + c;
                int by = py + r;
                if (bx < 0 || bx >= 10 || by >= 16) return true;
                if (by >= 0 && (s_tetBoard[by] & (1 << bx))) return true;
            }
        }
    }
    return false;
}

static void tetSpawn() {
    s_tetPiece = random(0, 7);
    s_tetRot = 0;
    s_tetX = 3;
    s_tetY = 0;
    if (tetCollide(s_tetX, s_tetY, s_tetPiece, s_tetRot)) {
        s_tetOver = true;
    }
}

static void tetrisSetup() {
    memset(s_tetBoard, 0, sizeof(s_tetBoard));
    s_tetScore = 0;
    s_tetOver = false;
    s_tetLastDropMs = millis();
    tetSpawn();
    encClear();
}

static void tetrisLoop(bool &inGameRef) {
    if (encBack()) {
        saveArcadeHighScore(7, s_tetScore);
        inGameRef = false;
        encClear();
        return;
    }

    if (s_tetOver) {
        saveArcadeHighScore(7, s_tetScore);
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(24, 20, "TETRIS OVER!");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(18, 35);
        u8g2.printf("Score: %d (HI: %d)", s_tetScore, g_settings.arcadeHi[7]);
        u8g2.drawStr(10, 52, "[OK] Reintentar [HOLD] Salir");
        u8g2.sendBuffer();
        if (encOk()) tetrisSetup();
        return;
    }

    int d = encStep();
    if (d != 0) {
        int stepDir = (d > 0) ? 1 : -1;
        if (!tetCollide(s_tetX + stepDir, s_tetY, s_tetPiece, s_tetRot)) {
            s_tetX += stepDir;
        }
    }

    if (encOk()) {
        int nextRot = (s_tetRot + 1) & 3;
        if (!tetCollide(s_tetX, s_tetY, s_tetPiece, nextRot)) {
            s_tetRot = nextRot;
        }
    }

    uint32_t dropInterval = encSwPressed() ? 70 : 360;
    if (millis() - s_tetLastDropMs >= dropInterval) {
        s_tetLastDropMs = millis();
        if (!tetCollide(s_tetX, s_tetY + 1, s_tetPiece, s_tetRot)) {
            s_tetY++;
        } else {
            uint16_t mask = kTetShapes[s_tetPiece][s_tetRot & 3];
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    if (mask & (0x8000 >> (r * 4 + c))) {
                        int bx = s_tetX + c;
                        int by = s_tetY + r;
                        if (by >= 0 && by < 16 && bx >= 0 && bx < 10) {
                            s_tetBoard[by] |= (1 << bx);
                        }
                    }
                }
            }
            int linesCleared = 0;
            for (int r = 15; r >= 0; r--) {
                if ((s_tetBoard[r] & 0x03FF) == 0x03FF) {
                    linesCleared++;
                    for (int k = r; k > 0; k--) s_tetBoard[k] = s_tetBoard[k - 1];
                    s_tetBoard[0] = 0;
                    r++;
                }
            }
            if (linesCleared > 0) s_tetScore += linesCleared * 25;
            else s_tetScore += 2;
            tetSpawn();
        }
    }

    u8g2.clearBuffer();
    u8g2.drawFrame(6, 0, 42, 64);

    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 10; c++) {
            if (s_tetBoard[r] & (1 << c)) {
                u8g2.drawBox(7 + c * 4, r * 4, 3, 3);
            }
        }
    }

    uint16_t mask = kTetShapes[s_tetPiece][s_tetRot & 3];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (mask & (0x8000 >> (r * 4 + c))) {
                int bx = s_tetX + c;
                int by = s_tetY + r;
                if (by >= 0 && by < 16 && bx >= 0 && bx < 10) {
                    u8g2.drawBox(7 + bx * 4, by * 4, 3, 3);
                }
            }
        }
    }

    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(54, 12, "TETRIS");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(54, 25);
    u8g2.printf("PTS: %d", s_tetScore);
    u8g2.setCursor(54, 35);
    u8g2.printf("HI:  %d", max(s_tetScore, g_settings.arcadeHi[7]));
    u8g2.drawStr(54, 48, "Gira:Mover");
    u8g2.drawStr(54, 57, "OK:Rotar");
    u8g2.sendBuffer();
    delay(15);
}

// ============================================================================
// JUEGO 9: ASTEROIDS (Gira=Rotar nave, OK=Disparar láser)
// ============================================================================
struct AsteroidRock {
    float x, y, vx, vy;
    int radius;
    bool active;
};
struct LaserShot {
    float x, y, vx, vy;
    int life;
};

static float s_astAngle = 0.0f;
static AsteroidRock s_rocks[5];
static LaserShot s_shots[4];
static int s_astScore = 0;
static bool s_astOver = false;

static void spawnRock(int idx) {
    s_rocks[idx].active = true;
    s_rocks[idx].x = (random(0, 2) == 0) ? random(2, 24) : random(104, 125);
    s_rocks[idx].y = random(12, 60);
    float ang = (float)random(0, 628) / 100.0f;
    s_rocks[idx].vx = cosf(ang) * 0.85f;
    s_rocks[idx].vy = sinf(ang) * 0.85f;
    s_rocks[idx].radius = random(4, 7);
}

static void asteroidsSetup() {
    s_astAngle = -1.57f;
    s_astScore = 0;
    s_astOver = false;
    for (int i = 0; i < 4; i++) s_shots[i].life = 0;
    for (int i = 0; i < 5; i++) spawnRock(i);
    encClear();
}

static void asteroidsLoop(bool &inGameRef) {
    if (encBack()) {
        saveArcadeHighScore(8, s_astScore);
        inGameRef = false;
        encClear();
        return;
    }

    if (s_astOver) {
        saveArcadeHighScore(8, s_astScore);
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(18, 20, "NAVE DESTRUIDA!");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(18, 35);
        u8g2.printf("Puntos: %d (HI: %d)", s_astScore, g_settings.arcadeHi[8]);
        u8g2.drawStr(10, 52, "[OK] Reintentar [HOLD] Salir");
        u8g2.sendBuffer();
        if (encOk()) asteroidsSetup();
        return;
    }

    int d = encStep();
    if (d != 0) {
        s_astAngle += d * 0.32f;
    }

    if (encOk() || encOkDown()) {
        for (int i = 0; i < 4; i++) {
            if (s_shots[i].life <= 0) {
                s_shots[i].x = 64.0f + cosf(s_astAngle) * 5.0f;
                s_shots[i].y = 36.0f + sinf(s_astAngle) * 5.0f;
                s_shots[i].vx = cosf(s_astAngle) * 3.0f;
                s_shots[i].vy = sinf(s_astAngle) * 3.0f;
                s_shots[i].life = 24;
                break;
            }
        }
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 8);
    u8g2.printf("ASTEROIDS:%d HI:%d", s_astScore, max(s_astScore, g_settings.arcadeHi[8]));
    u8g2.drawHLine(0, 10, 128);

    int nx = 64 + (int)(cosf(s_astAngle) * 6.0f);
    int ny = 36 + (int)(sinf(s_astAngle) * 6.0f);
    int lx = 64 + (int)(cosf(s_astAngle + 2.5f) * 5.0f);
    int ly = 36 + (int)(sinf(s_astAngle + 2.5f) * 5.0f);
    int rx = 64 + (int)(cosf(s_astAngle - 2.5f) * 5.0f);
    int ry = 36 + (int)(sinf(s_astAngle - 2.5f) * 5.0f);
    u8g2.drawTriangle(nx, ny, lx, ly, rx, ry);

    for (int i = 0; i < 4; i++) {
        if (s_shots[i].life > 0) {
            s_shots[i].x += s_shots[i].vx;
            s_shots[i].y += s_shots[i].vy;
            s_shots[i].life--;
            if (s_shots[i].x < 0 || s_shots[i].x > 127 || s_shots[i].y < 11 || s_shots[i].y > 63) {
                s_shots[i].life = 0;
            } else {
                u8g2.drawDisc((int)s_shots[i].x, (int)s_shots[i].y, 1);
            }
        }
    }

    for (int r = 0; r < 5; r++) {
        if (!s_rocks[r].active) continue;
        s_rocks[r].x += s_rocks[r].vx;
        s_rocks[r].y += s_rocks[r].vy;
        if (s_rocks[r].x < 2) s_rocks[r].x = 125;
        if (s_rocks[r].x > 126) s_rocks[r].x = 3;
        if (s_rocks[r].y < 13) s_rocks[r].y = 62;
        if (s_rocks[r].y > 62) s_rocks[r].y = 13;

        u8g2.drawCircle((int)s_rocks[r].x, (int)s_rocks[r].y, s_rocks[r].radius);

        float dxShip = s_rocks[r].x - 64.0f;
        float dyShip = s_rocks[r].y - 36.0f;
        if (sqrtf(dxShip * dxShip + dyShip * dyShip) < (s_rocks[r].radius + 3)) {
            s_astOver = true;
        }

        for (int s = 0; s < 4; s++) {
            if (s_shots[s].life > 0) {
                float dx = s_rocks[r].x - s_shots[s].x;
                float dy = s_rocks[r].y - s_shots[s].y;
                if (sqrtf(dx * dx + dy * dy) <= (s_rocks[r].radius + 2)) {
                    s_shots[s].life = 0;
                    s_astScore += 15;
                    spawnRock(r);
                    break;
                }
            }
        }
    }

    u8g2.sendBuffer();
    delay(16);
}

// ============================================================================
// JUEGO 10: 2048 PUZZLE (Gira=Elegir Dirección N/E/S/O, OK=Deslizar)
// ============================================================================
static uint16_t s_grid2048[4][4];
static int s_score2048 = 0;
static int s_dir2048 = 0;

static void addRandomTile2048() {
    int emptyCells[16][2];
    int count = 0;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (s_grid2048[r][c] == 0) {
                emptyCells[count][0] = r;
                emptyCells[count][1] = c;
                count++;
            }
        }
    }
    if (count > 0) {
        int pick = random(0, count);
        s_grid2048[emptyCells[pick][0]][emptyCells[pick][1]] = (random(0, 10) == 0) ? 4 : 2;
    }
}

static void game2048Setup() {
    memset(s_grid2048, 0, sizeof(s_grid2048));
    s_score2048 = 0;
    s_dir2048 = 0;
    addRandomTile2048();
    addRandomTile2048();
    encClear();
}

static bool slideLine2048(uint16_t line[4]) {
    uint16_t old[4];
    memcpy(old, line, sizeof(old));
    uint16_t compact[4] = {0, 0, 0, 0};
    int p = 0;
    for (int i = 0; i < 4; i++) {
        if (line[i] != 0) compact[p++] = line[i];
    }
    for (int i = 0; i < 3; i++) {
        if (compact[i] != 0 && compact[i] == compact[i + 1]) {
            compact[i] *= 2;
            s_score2048 += compact[i];
            for (int j = i + 1; j < 3; j++) compact[j] = compact[j + 1];
            compact[3] = 0;
        }
    }
    memcpy(line, compact, sizeof(compact));
    return memcmp(old, line, sizeof(old)) != 0;
}

static void moveGrid2048(int dir) {
    bool changed = false;
    uint16_t line[4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (dir == 0) line[j] = s_grid2048[j][i];
            else if (dir == 2) line[j] = s_grid2048[3 - j][i];
            else if (dir == 3) line[j] = s_grid2048[i][j];
            else line[j] = s_grid2048[i][3 - j];
        }
        if (slideLine2048(line)) changed = true;
        for (int j = 0; j < 4; j++) {
            if (dir == 0) s_grid2048[j][i] = line[j];
            else if (dir == 2) s_grid2048[3 - j][i] = line[j];
            else if (dir == 3) s_grid2048[i][j] = line[j];
            else s_grid2048[i][3 - j] = line[j];
        }
    }
    if (changed) {
        addRandomTile2048();
        saveArcadeHighScore(9, s_score2048);
    }
}

static void game2048Loop(bool &inGameRef) {
    if (encBack()) {
        saveArcadeHighScore(9, s_score2048);
        inGameRef = false;
        encClear();
        return;
    }

    int d = encStep();
    if (d != 0) {
        s_dir2048 = (s_dir2048 + d) % 4;
        if (s_dir2048 < 0) s_dir2048 += 4;
    }

    if (encOk()) {
        moveGrid2048(s_dir2048);
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_4x6_tr);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int x = 2 + c * 19;
            int y = 2 + r * 15;
            u8g2.drawFrame(x, y, 18, 14);
            if (s_grid2048[r][c] > 0) {
                char buf[6];
                snprintf(buf, sizeof(buf), "%u", s_grid2048[r][c]);
                u8g2.drawStr(x + 2, y + 9, buf);
            }
        }
    }

    const char *dirNames[4] = {"^ ARRIBA", "> DERECHA", "v ABAJO", "< IZQUIERDA"};
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(80, 10, "2048");
    u8g2.setCursor(80, 21);
    u8g2.printf("P:%d", s_score2048);
    u8g2.setCursor(80, 31);
    u8g2.printf("H:%d", max(s_score2048, g_settings.arcadeHi[9]));
    u8g2.drawRBox(79, 36, 48, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(81, 45, dirNames[s_dir2048]);
    u8g2.setDrawColor(1);
    u8g2.drawStr(80, 59, "OK:Mover");
    u8g2.sendBuffer();
}

void gamesMenuLoop() {
    static bool inGame = false;
    const int kTotalGames = 10;

    if (!inGame) {
        int d = encStep();
        if (d != 0) {
            gameSelection = (gameSelection + d) % kTotalGames;
            if (gameSelection < 0) gameSelection += kTotalGames;
        }

        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x12_tr);
        u8g2.drawStr(8, 11, "> ARCADE 10-IN-1 <");
        u8g2.drawHLine(0, 13, 128);

        const char* games[kTotalGames] = {
            "1. SNAKE",
            "2. PONG",
            "3. FLAPPY",
            "4. INVADERS",
            "5. DINO RUN",
            "6. BREAKOUT",
            "7. SIMON MEMORIA",
            "8. TETRIS",
            "9. ASTEROIDS",
            "10. 2048 PUZZLE"
        };

        int firstVis = gameSelection - 2;
        if (firstVis > kTotalGames - 4) firstVis = kTotalGames - 4;
        if (firstVis < 0) firstVis = 0;

        u8g2.setFont(u8g2_font_5x7_tr);
        for (int row = 0; row < 4; row++) {
            int i = firstVis + row;
            int y = 24 + (row * 10);
            if (i == gameSelection) {
                u8g2.drawRBox(6, y - 7, 116, 9, 2);
                u8g2.setDrawColor(0);
                u8g2.drawStr(12, y, games[i]);
                u8g2.setDrawColor(1);
            } else {
                u8g2.drawStr(12, y, games[i]);
            }
        }

        u8g2.drawHLine(0, 55, 128);
        u8g2.setCursor(6, 63);
        u8g2.printf("HI:%d | OK:Jugar HOLD:Salir", g_settings.arcadeHi[gameSelection]);

        if (encOk()) {
            inGame = true;
            activeGame = gameSelection + 1;
            if (activeGame == 1) resetSnake();
            if (activeGame == 2) pongSetup();
            if (activeGame == 3) flappySetup();
            if (activeGame == 4) spaceSetup();
            if (activeGame == 5) dinoSetup();
            if (activeGame == 6) breakoutSetup();
            if (activeGame == 7) simonSetup();
            if (activeGame == 8) tetrisSetup();
            if (activeGame == 9) asteroidsSetup();
            if (activeGame == 10) game2048Setup();
            encClear();
        }

        if (encBack()) {
            runningApp = false;
            encClear();
        }

        u8g2.sendBuffer();
    } else {
        if (activeGame >= 1 && activeGame <= 5 && encBack()) {
            inGame = false;
            encClear();
            return;
        }
        if (activeGame == 1) snakeLoop();
        else if (activeGame == 2) pongLoop();
        else if (activeGame == 3) flappyLoop();
        else if (activeGame == 4) spaceLoop();
        else if (activeGame == 5) dinoLoop();
        else if (activeGame == 6) breakoutLoop(inGame);
        else if (activeGame == 7) simonLoop(inGame);
        else if (activeGame == 8) tetrisLoop(inGame);
        else if (activeGame == 9) asteroidsLoop(inGame);
        else if (activeGame == 10) game2048Loop(inGame);
    }
}
