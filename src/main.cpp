#include <Arduino.h>
#include <Wire.h>

#define JC3248W535
#include <Arduino_GFX_Library.h>

#define GFX_BL    1
#define LCD_CS    45
#define LCD_SCLK  47
#define LCD_D0    21
#define LCD_D1    48
#define LCD_D2    40
#define LCD_D3    39
#define LCD_RST   GFX_NOT_DEFINED

#define TOUCH_SDA 4
#define TOUCH_SCL 8
#define TOUCH_I2C_ADDR 0x3B

#define SCREEN_W  320
#define SCREEN_H  480

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX *output_display = new Arduino_AXS15231B(bus, LCD_RST, 0, false, SCREEN_W, SCREEN_H);
Arduino_Canvas *gfx = new Arduino_Canvas(SCREEN_W, SCREEN_H, output_display, 0, 0, 0);

#define COLOR_BG      0x10A2
#define COLOR_WHITE   0xFFFF
#define COLOR_BLACK   0x0000
#define COLOR_ORANGE  0xFD20
#define COLOR_PINK    0xF81F
#define COLOR_CYAN    0x07FF
#define COLOR_GREEN   0x07E0
#define COLOR_YELLOW  0xFFE0
#define COLOR_PURPLE  0x915C
#define COLOR_GOLD    0xFEA0

#define TOTAL_SPECIES 20

struct AFCPetInfo {
  const char* name;
  const char* role;
  uint16_t primaryColor;
  uint16_t secondaryColor;
};

const AFCPetInfo ROSTER[TOTAL_SPECIES] = {
  { "Guardian Gate X", "Light Tank",      COLOR_CYAN,   COLOR_WHITE  },
  { "TVM Master",      "Light Support",   COLOR_GOLD,   COLOR_WHITE  },
  { "Titan Hopper",    "Earth Tank",      COLOR_ORANGE, COLOR_GREEN  },
  { "Validator Prime", "Wind Striker",    COLOR_CYAN,   COLOR_PURPLE },
  { "Reader Hunter",   "Dark Assassin",   COLOR_PURPLE, COLOR_GREEN  },
  { "Quantum Scanner", "Wind Scout",      COLOR_CYAN,   COLOR_WHITE  },
  { "Merchant King",   "Gold Buffer",     COLOR_GOLD,   COLOR_ORANGE },
  { "Digital Oracle",  "Data Support",    COLOR_CYAN,   COLOR_PURPLE },
  { "Core Nexus",      "Energy Battery",  COLOR_CYAN,   COLOR_YELLOW },
  { "AFC Overmind",    "Master CCHS",     COLOR_PURPLE, COLOR_CYAN   },
  { "SAM Guardian",    "Crypto Guard",    COLOR_CYAN,   COLOR_WHITE  },
  { "Sky Nexus",       "Maglev Speed",    COLOR_CYAN,   COLOR_WHITE  },
  { "Network Dragon",  "Fiber Mage",      COLOR_CYAN,   COLOR_PURPLE },
  { "Station Alpha",   "Terminal Titan",  COLOR_WHITE,  COLOR_ORANGE },
  { "Fortress Master", "Switch Basher",   COLOR_PURPLE, COLOR_ORANGE },
  { "Quantum Gateway", "Portal Ring",     COLOR_CYAN,   COLOR_YELLOW },
  { "Sentinel Eye",    "AI Surveillance", COLOR_PURPLE, COLOR_CYAN   },
  { "Thunder Core",    "Catenary Spark",  COLOR_YELLOW, COLOR_CYAN   },
  { "Print Forge",     "Thermal Burner",  COLOR_ORANGE, COLOR_WHITE  },
  { "Workshop Titan",  "Depot Heavy",     COLOR_ORANGE, COLOR_GOLD   }
};

enum GameState { STATE_HATCHING, STATE_PLAYING };
GameState currentState = STATE_HATCHING;

int eggTaps = 0;
int activePetIndex = 0;

struct PetStatus {
  int hunger;
  int happiness;
  int energy;
  int cleanliness;
  bool isSleeping;
};

PetStatus pet = { 80, 85, 100, 100, false };
unsigned long lastTick = 0;
unsigned long lastBlinkTime = 0;
int bounceOffset = 0;
bool isBlinking = false;
int actionHeartTimer = 0;

bool readTouch(int *x, int *y) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)8);
  if (Wire.available() >= 8) {
    uint8_t d[8];
    for (int i = 0; i < 8; i++) data[i] = Wire.read();
    if ((data[2] & 0x0F) > 0) {
      *x = ((data[3] & 0x0F) << 8) | data[4];
      *y = ((data[5] & 0x0F) << 8) | data[6];
      return true;
    }
  }
  return false;
}

void drawBar(int x, int y, int w, int h, const char* label, int val, uint16_t color) {
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_WHITE, COLOR_BG);
  gfx->setCursor(x, y - 10);
  gfx->print(label);
  gfx->drawRoundRect(x, y, w, h, 3, COLOR_WHITE);
  int fillW = (val * (w - 4)) / 100;
  if (fillW > 0) gfx->fillRoundRect(x + 2, y + 2, fillW, h - 4, 2, color);
}

void drawButtons() {
  const char* labels[] = { "FEED", "PLAY", "BATH", "SLEEP" };
  uint16_t colors[] = { COLOR_ORANGE, COLOR_PINK, COLOR_CYAN, COLOR_YELLOW };
  for (int i = 0; i < 4; i++) {
    int bx = 10 + i * 76;
    int by = 412;
    gfx->fillRoundRect(bx, by, 70, 52, 8, colors[i]);
    gfx->drawRoundRect(bx, by, 70, 52, 8, COLOR_WHITE);
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_BLACK, colors[i]);
    gfx->setCursor(bx + 12, by + 18);
    gfx->print(labels[i]);
  }
}

void drawEyes(int x1, int x2, int y, uint16_t color) {
  if (isBlinking || pet.isSleeping) {
    gfx->drawLine(x1 - 4, y, x1 + 4, y, color);
    gfx->drawLine(x2 - 4, y, x2 + 4, y, color);
  } else {
    gfx->fillCircle(x1, y, 4, color);
    gfx->fillCircle(x2, y, 4, color);
    gfx->fillCircle(x1 - 1, y - 1, 1, COLOR_WHITE);
    gfx->fillCircle(x2 - 1, y - 1, 1, COLOR_WHITE);
  }
}

void drawPetSprite(int cx, int cy, int index, int bounce, bool sleeping) {
  int y = cy + bounce;
  const AFCPetInfo &p = ROSTER[index];

  if (sleeping) {
    gfx->fillCircle(cx, y, 42, COLOR_CYAN);
    drawEyes(cx - 15, cx + 15, y - 2, COLOR_BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_YELLOW, COLOR_BG);
    gfx->setCursor(cx + 35, y - 35);
    gfx->print("Zzz");
    return;
  }

  if (actionHeartTimer > 0) {
    gfx->fillCircle(cx - 4, y - 55, 5, COLOR_PINK);
    gfx->fillCircle(cx + 4, y - 55, 5, COLOR_PINK);
    gfx->fillTriangle(cx - 9, y - 54, cx + 9, y - 54, cx, y - 44, COLOR_PINK);
  }

  switch (index) {
    case 0:
      gfx->fillRoundRect(cx - 30, y - 40, 60, 70, 10, p.primaryColor);
      gfx->fillRoundRect(cx - 20, y - 25, 40, 32, 6, COLOR_BLACK);
      gfx->drawRoundRect(cx - 45, y - 20, 15, 45, 4, p.secondaryColor);
      gfx->drawRoundRect(cx + 30, y - 20, 15, 45, 4, p.secondaryColor);
      drawEyes(cx - 8, cx + 8, y - 10, COLOR_CYAN);
      break;
    case 1:
      gfx->fillRoundRect(cx - 32, y - 45, 64, 80, 10, p.primaryColor);
      gfx->fillRect(cx - 22, y - 35, 44, 25, COLOR_BLACK);
      gfx->fillRect(cx - 18, y + 5, 36, 12, p.secondaryColor);
      drawEyes(cx - 10, cx + 10, y - 16, COLOR_YELLOW);
      break;
    case 2:
      gfx->fillRoundRect(cx - 35, y - 25, 70, 50, 18, p.primaryColor);
      gfx->fillCircle(cx - 30, y + 5, 14, p.secondaryColor);
      gfx->fillCircle(cx + 10, y - 30, 12, COLOR_GOLD);
      drawEyes(cx - 35, cx - 25, y + 3, COLOR_BLACK);
      break;
    case 3:
      gfx->fillCircle(cx, y - 10, 28, p.primaryColor);
      gfx->fillRoundRect(cx - 15, y - 22, 30, 20, 4, COLOR_BLACK);
      gfx->fillTriangle(cx + 20, y - 5, cx + 45, y + 10, cx + 15, y + 10, p.primaryColor);
      drawEyes(cx - 6, cx + 6, y - 12, COLOR_GREEN);
      break;
    case 4:
      gfx->fillCircle(cx, y - 5, 30, p.primaryColor);
      gfx->fillTriangle(cx - 24, y - 30, cx - 10, y - 40, cx - 6, y - 18, p.secondaryColor);
      gfx->fillTriangle(cx + 24, y - 30, cx + 10, y - 40, cx + 6, y - 18, p.secondaryColor);
      drawEyes(cx - 12, cx + 12, y - 8, COLOR_GREEN);
      break;
    default:
      gfx->fillRoundRect(cx - 32, y - 32, 64, 64, 14, p.primaryColor);
      gfx->drawRoundRect(cx - 32, y - 32, 64, 64, 14, p.secondaryColor);
      drawEyes(cx - 12, cx + 12, y - 10, COLOR_WHITE);
      break;
  }
}

void renderHatchingScreen() {
  gfx->fillScreen(COLOR_BG);
  gfx->setTextSize(3);
  gfx->setTextColor(COLOR_YELLOW, COLOR_BG);
  gfx->setCursor(35, 30);
  gfx->print("MYSTERY EGG");

  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_WHITE, COLOR_BG);
  gfx->setCursor(65, 75);
  gfx->print("Tap the Egg 3 times to Hatch!");

  int cx = 160, cy = 240 + bounceOffset;
  gfx->fillEllipse(cx, cy, 60, 80, COLOR_GOLD);
  gfx->drawEllipse(cx, cy, 60, 80, COLOR_WHITE);
  gfx->fillEllipse(cx - 15, cy - 25, 15, 25, COLOR_WHITE);

  if (eggTaps >= 1) {
    gfx->drawLine(cx, cy - 40, cx - 15, cy - 10, COLOR_BLACK);
    gfx->drawLine(cx - 15, cy - 10, cx + 10, cy + 15, COLOR_BLACK);
  }
  if (eggTaps >= 2) {
    gfx->drawLine(cx + 10, cy + 15, cx - 20, cy + 40, COLOR_BLACK);
    gfx->drawLine(cx, cy - 40, cx + 25, cy - 20, COLOR_BLACK);
  }

  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_CYAN, COLOR_BG);
  gfx->setCursor(110, 370);
  gfx->printf("CRACK: %d/3", eggTaps);
  gfx->flush();
}

void renderMainGameScreen() {
  gfx->fillScreen(COLOR_BG);
  gfx->setTextSize(3);
  gfx->setTextColor(COLOR_YELLOW, COLOR_BG);
  gfx->setCursor(30, 12);
  gfx->print("AFC PET SAGA");

  gfx->fillRoundRect(240, 48, 70, 24, 4, 0x4208);
  gfx->drawRoundRect(240, 48, 70, 24, 4, COLOR_WHITE);
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_YELLOW, 0x4208);
  gfx->setCursor(248, 56);
  gfx->print("RE-ROLL");

  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE, COLOR_BG);
  const char* pName = ROSTER[activePetIndex].name;
  int nameX = max(10, 160 - (int)(strlen(pName) * 6));
  gfx->setCursor(nameX, 55);
  gfx->print(pName);

  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_CYAN, COLOR_BG);
  gfx->setCursor(30, 80);
  gfx->printf("No.%02d/20  Role: %s", activePetIndex + 1, ROSTER[activePetIndex].role);

  drawBar(25, 110, 80, 12, "HUNGER", pet.hunger, COLOR_ORANGE);
  drawBar(120, 110, 80, 12, "HAPPY", pet.happiness, COLOR_PINK);
  drawBar(215, 110, 80, 12, "ENERGY", pet.energy, COLOR_GREEN);

  gfx->fillEllipse(160, 310, 75, 20, 0x2124);
  gfx->drawEllipse(160, 310, 75, 20, ROSTER[activePetIndex].primaryColor);

  drawPetSprite(160, 240, activePetIndex, bounceOffset, pet.isSleeping);
  drawButtons();
  gfx->flush();
}

void setup() {
  Serial.begin(115200);
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  gfx->begin();

  randomSeed(analogRead(0) + micros());
  currentState = STATE_HATCHING;
  renderHatchingScreen();
}

void loop() {
  int tx = 0, ty = 0;
  bool touched = readTouch(&tx, &ty);

  if (currentState == STATE_HATCHING) {
    if (touched && tx >= 80 && tx <= 240 && ty >= 150 && ty <= 330) {
      eggTaps++;
      bounceOffset = -8;
      renderHatchingScreen();
      delay(150);
      bounceOffset = 0;
      renderHatchingScreen();

      if (eggTaps >= 3) {
        activePetIndex = random(0, TOTAL_SPECIES);
        delay(400);
        gfx->fillScreen(COLOR_WHITE);
        gfx->flush();
        delay(200);
        currentState = STATE_PLAYING;
        renderMainGameScreen();
      }
    }
    delay(20);
    return;
  }

  if (currentState == STATE_PLAYING) {
    if (touched) {
      if (tx >= 230 && tx <= 315 && ty >= 40 && ty <= 80) {
        currentState = STATE_HATCHING;
        eggTaps = 0;
        renderHatchingScreen();
        delay(200);
        return;
      }

      if (tx >= 80 && tx <= 240 && ty >= 160 && ty <= 320) {
        pet.happiness = min(100, pet.happiness + 5);
        actionHeartTimer = 2;
        bounceOffset = -12;
        renderMainGameScreen();
        delay(150);
      }

      if (ty >= 400 && ty <= 470) {
        int btnIndex = (tx - 10) / 76;
        if (btnIndex >= 0 && btnIndex < 4) {
          switch (btnIndex) {
            case 0: pet.hunger = min(100, pet.hunger + 25); pet.happiness = min(100, pet.happiness + 5); actionHeartTimer = 2; break;
            case 1: if (pet.energy >= 15 && !pet.isSleeping) { pet.happiness = min(100, pet.happiness + 20); pet.energy = max(0, pet.energy - 15); bounceOffset = -15; } break;
            case 2: pet.cleanliness = 100; pet.happiness = min(100, pet.happiness + 10); break;
            case 3: pet.isSleeping = !pet.isSleeping; break;
          }
          renderMainGameScreen();
          delay(180);
        }
      }
    }

    if (millis() - lastBlinkTime > (unsigned long)random(2000, 3500)) {
      lastBlinkTime = millis();
      isBlinking = true;
      renderMainGameScreen();
      delay(80);
      isBlinking = false;
      renderMainGameScreen();
    }

    if (millis() - lastTick > 3000) {
      lastTick = millis();
      bounceOffset = (bounceOffset == 0) ? -5 : 0;
      if (actionHeartTimer > 0) actionHeartTimer--;
      if (!pet.isSleeping) {
        if (pet.hunger > 0) pet.hunger -= 2;
        if (pet.cleanliness > 0) pet.cleanliness -= 1;
        if (pet.hunger < 20 && pet.happiness > 0) pet.happiness -= 3;
      } else {
        if (pet.energy < 100) pet.energy += 15;
      }
      renderMainGameScreen();
    }
  }

  delay(20);
}
