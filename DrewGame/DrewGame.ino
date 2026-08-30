// DrewGame — TTGO T-Display held phone-style (USB-C + buttons at the BOTTOM).
// Catch the falling dot with the bar. It speeds up as you score. 3 lives, then
// GAME OVER. High score saved to flash. Hold BOTH buttons to pause/resume (and
// to restart after game over).
//   GPIO 0 = LEFT   |   GPIO 35 = RIGHT   (both active-LOW)
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Preferences.h>

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;

#define BTN_LEFT   0
#define BTN_RIGHT  35

const int SCR_W = 135, SCR_H = 240;
const int TOP_Y = 40;                 // play area top (below title/score)

// bar
const int BAR_W = 32, BAR_H = 12, BAR_Y = 210, BAR_SPEED = 4;
int barX = (SCR_W - BAR_W) / 2, barPrevX = barX;

// ball
const int BALL_R = 5;
float ballX, ballY, ballVX, ballVY;
int ballPrevX, ballPrevY;

// game
enum State { PLAYING, PAUSED, GAMEOVER };
State state = PLAYING, prevState = GAMEOVER;
int score = 0, lives = 3, highScore = 0;
bool prevBoth = false;
uint32_t f = 0;

// pause "DVD logo" screensaver
const int LOGO_W = 100, LOGO_H = 42;
float pLogoX, pLogoY, pVX, pVY, pHue;
int pPrevX, pPrevY;

uint16_t hsv565(float h, float s, float v) {
  h = fmodf(h, 360.0f); if (h < 0) h += 360.0f;
  float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = v - c;
  float r = 0, g = 0, b = 0;
  if (h < 60)      { r = c; g = x; }
  else if (h < 120){ r = x; g = c; }
  else if (h < 180){ g = c; b = x; }
  else if (h < 240){ g = x; b = c; }
  else if (h < 300){ r = x; b = c; }
  else             { r = c; b = x; }
  return tft.color565((r + m) * 255, (g + m) * 255, (b + m) * 255);
}

float ballSpeed() { float s = 2.4f + score * 0.12f; return s > 6.5f ? 6.5f : s; }

void spawnBall() {
  ballX = random(BALL_R, SCR_W - BALL_R);
  ballY = TOP_Y + BALL_R + 2;
  ballVX = (random(0, 2) ? 1 : -1) * (1.2f + random(0, 12) / 10.0f);
  ballVY = ballSpeed();
  ballPrevX = (int)ballX; ballPrevY = (int)ballY;
}

void resetGame() {
  score = 0; lives = 3;
  barX = (SCR_W - BAR_W) / 2; barPrevX = barX;
  spawnBall();
}

void drawHud() {
  tft.setTextFont(1); tft.setTextSize(3); tft.setTextDatum(MC_DATUM);
  tft.setTextColor(hsv565(f * 2.0f, 1.0f, 1.0f), TFT_BLACK);
  tft.drawString("Drew", SCR_W / 2, 24);

  tft.setTextSize(1); char buf[24];
  tft.setTextDatum(TL_DATUM); tft.setTextColor(TFT_GREEN, TFT_BLACK);
  sprintf(buf, "Score %d", score); tft.drawString(buf, 4, 4);
  tft.setTextDatum(TR_DATUM); tft.setTextColor(TFT_RED, TFT_BLACK);
  sprintf(buf, "Lives %d", lives); tft.drawString(buf, SCR_W - 4, 4);
}

void drawCentered(const char *s, int y, int size, uint16_t color) {
  tft.setTextFont(1); tft.setTextSize(size); tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(s, SCR_W / 2, y);
}

// The mythical perfect corner: rapid colour flashes + confetti + "PERFECT CORNER!"
void celebrateCorner() {
  for (int k = 0; k < 4; k++) { tft.fillScreen(hsv565(random(0, 360), 1.0f, 1.0f)); delay(55); }
  for (int frame = 0; frame < 30; frame++) {
    tft.fillScreen(TFT_BLACK);
    for (int i = 0; i < 70; i++)
      tft.fillCircle(random(0, SCR_W), random(0, SCR_H), random(2, 5),
                     hsv565(random(0, 360), 1.0f, 1.0f));
    tft.setTextFont(1); tft.setTextDatum(MC_DATUM);
    tft.setTextSize(3); tft.setTextColor(hsv565(frame * 22.0f, 1.0f, 1.0f), TFT_BLACK);
    tft.drawString("PERFECT", SCR_W / 2, SCR_H / 2 - 14);
    tft.setTextSize(2); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("CORNER!", SCR_W / 2, SCR_H / 2 + 16);
    delay(40);
  }
  tft.fillScreen(TFT_BLACK);
  pPrevX = (int)pLogoX; pPrevY = (int)pLogoY;   // don't erase a stale rect next frame
}

// Bouncing "DREW IS THE BEST" logo, Pioneer-DVD-screensaver style: drifts
// slowly, ricochets off the edges, changes colour on every bounce, and throws
// a party if it ever nails a corner.
void animatePause() {
  tft.fillRect(pPrevX, pPrevY, LOGO_W, LOGO_H, TFT_BLACK);   // erase old
  pLogoX += pVX; pLogoY += pVY;
  bool hitX = false, hitY = false;
  if (pLogoX < 0)              { pLogoX = 0;              pVX = -pVX; hitX = true; }
  if (pLogoX > SCR_W - LOGO_W) { pLogoX = SCR_W - LOGO_W; pVX = -pVX; hitX = true; }
  if (pLogoY < 0)              { pLogoY = 0;              pVY = -pVY; hitY = true; }
  if (pLogoY > SCR_H - LOGO_H) { pLogoY = SCR_H - LOGO_H; pVY = -pVY; hitY = true; }
  if (hitX || hitY) pHue += 53;

  // corner = both axes bounce together, or one bounces while snug against the other edge
  const int CT = 8;
  bool nearX = (pLogoX <= CT) || (pLogoX >= SCR_W - LOGO_W - CT);
  bool nearY = (pLogoY <= CT) || (pLogoY >= SCR_H - LOGO_H - CT);
  if ((hitX && hitY) || (hitX && nearY) || (hitY && nearX)) { celebrateCorner(); return; }

  pPrevX = (int)pLogoX; pPrevY = (int)pLogoY;

  uint16_t col = hsv565(pHue, 1.0f, 1.0f);
  int cx = (int)pLogoX + LOGO_W / 2;
  tft.setTextFont(1); tft.setTextSize(2); tft.setTextDatum(MC_DATUM);
  tft.setTextColor(col, TFT_BLACK);
  tft.drawString("DREW IS", cx, (int)pLogoY + 11);
  tft.drawString("THE BEST", cx, (int)pLogoY + 31);
}

void drawGameOverScreen() {
  char buf[24];
  drawCentered("GAME", 80, 4, TFT_RED);
  drawCentered("OVER", 116, 4, TFT_RED);
  sprintf(buf, "Score %d", score); drawCentered(buf, 150, 2, TFT_WHITE);
  sprintf(buf, "Best %d", highScore); drawCentered(buf, 172, 2, TFT_YELLOW);
  drawCentered("hold both to restart", 205, 1, TFT_DARKGREY);
}

void setup() {
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT);
  randomSeed(esp_random());
  prefs.begin("drewgame", false);
  highScore = prefs.getInt("hi", 0);
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  resetGame();
}

void loop() {
  f++;

  // buttons + "both" edge detection
  bool L = digitalRead(BTN_LEFT) == LOW;
  bool R = digitalRead(BTN_RIGHT) == LOW;
  bool both = L && R;
  bool bothEdge = both && !prevBoth;
  prevBoth = both;

  if (bothEdge) {
    if (state == PLAYING) state = PAUSED;
    else if (state == PAUSED) state = PLAYING;
    else if (state == GAMEOVER) { resetGame(); state = PLAYING; }
  }

  // on state change, repaint the screen once
  if (state != prevState) {
    tft.fillScreen(TFT_BLACK);
    if (state == PAUSED) {
      pLogoX = (SCR_W - LOGO_W) / 2; pLogoY = (SCR_H - LOGO_H) / 2;
      pVX = 0.8f; pVY = 1.0f; pHue = random(0, 360);
      pPrevX = (int)pLogoX; pPrevY = (int)pLogoY;
    } else if (state == GAMEOVER) {
      drawGameOverScreen();
    }
    barPrevX = barX; ballPrevX = (int)ballX; ballPrevY = (int)ballY;
    prevState = state;
  }

  if (state == PAUSED)   { animatePause(); delay(25); return; }  // screensaver
  if (state == GAMEOVER) { delay(30); return; }                 // static screen

  drawHud();

  // move bar (ignore movement when both held — that's the pause gesture)
  if (!both) {
    if (L) barX -= BAR_SPEED;
    if (R) barX += BAR_SPEED;
  }
  if (barX < 0) barX = 0;
  if (barX > SCR_W - BAR_W) barX = SCR_W - BAR_W;

  // move ball
  ballX += ballVX; ballY += ballVY;
  if (ballX - BALL_R < 0)     { ballX = BALL_R;         ballVX = -ballVX; }
  if (ballX + BALL_R > SCR_W) { ballX = SCR_W - BALL_R; ballVX = -ballVX; }
  if (ballY - BALL_R < TOP_Y) { ballY = TOP_Y + BALL_R; ballVY = fabsf(ballVY); }

  // catch on the bar
  if (ballVY > 0 &&
      ballY + BALL_R >= BAR_Y && ballY + BALL_R <= BAR_Y + BAR_H &&
      ballX >= barX - BALL_R && ballX <= barX + BAR_W + BALL_R) {
    ballY = BAR_Y - BALL_R;
    score++;
    float hit = (ballX - (barX + BAR_W / 2.0f)) / (BAR_W / 2.0f);
    ballVX += hit * 1.2f;
    if (ballVX > 4) ballVX = 4; if (ballVX < -4) ballVX = -4;
    ballVY = -ballSpeed();     // speed scales with score
  }

  // missed
  if (ballY - BALL_R > SCR_H) {
    lives--;
    if (lives <= 0) {
      if (score > highScore) { highScore = score; prefs.putInt("hi", highScore); }
      state = GAMEOVER;
    } else {
      spawnBall();
    }
  }

  // draw ball
  tft.fillCircle(ballPrevX, ballPrevY, BALL_R, TFT_BLACK);
  ballPrevX = (int)ballX; ballPrevY = (int)ballY;
  tft.fillCircle((int)ballX, (int)ballY, BALL_R, hsv565(f * 5.0f, 1.0f, 1.0f));

  // draw bar
  if (barX != barPrevX) { tft.fillRect(barPrevX, BAR_Y, BAR_W, BAR_H, TFT_BLACK); barPrevX = barX; }
  tft.fillRoundRect(barX, BAR_Y, BAR_W, BAR_H, 4, hsv565(f * 4.0f + 120.0f, 1.0f, 1.0f));

  delay(15);
}
