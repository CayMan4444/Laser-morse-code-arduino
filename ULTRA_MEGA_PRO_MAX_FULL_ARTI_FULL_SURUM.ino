#include <Usb.h>
#include <usbhub.h>
#include <hidboot.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#define LASER_PIN 6
#define LDR_PIN A0
#define THRESHOLD 500

LiquidCrystal_I2C lcd(0x27, 16, 2);

USB Usb;
HIDBoot<USB_HID_PROTOCOL_KEYBOARD> Keyboard(&Usb);

const uint16_t MAX_INPUT_LENGTH = 33;
char inputBuffer[MAX_INPUT_LENGTH] = { 0 };
uint16_t inputIndex = 0;
const uint8_t LCD_COLUMNS = 16;
const uint8_t LCD_ROWS = 2;
uint16_t scrollOffset = 0;
uint8_t currentRow = 0;
uint8_t currentCol = 0;
const int dotDuration = 200;
bool enterPressed = false;
bool messageReceiving = false;
unsigned long lightStart = 0;
unsigned long darkStart = 0;
bool isLightOn = false;
String currentSymbol = "";
String messageBuffer = "";

bool isValidCharacter(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == ' ');
}

struct MorseCode {
  char letter;
  const char* code;
};

MorseCode morseTable[] = {
  {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
  {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
  {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
  {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
  {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
  {'Z', "--.."},
  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
  {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
  {' ', " "}, // boşluk özel işlenecek
};
const int morseTableSize = sizeof(morseTable) / sizeof(morseTable[0]);


const char ASCIIKeys[128] = {
  0, 0, 0, 0, 'A', 'B', 'C', 'D',          // 0-7
  'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',  // 8-15
  'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',  // 16-23
  'U', 'V', 'W', 'X', 'Y', 'Z', '1', '2',  // 24-31
  '3', '4', '5', '6', '7', '8', '9', '0',  // 32-39 (boşluk ve semboller çıkarıldı)
  0, 0, 0, 0, ' ', 0, 0, 0,                // 40-47
  0, 0, 0, 0, 0, 0, 0, 0,                  // 48-55 (rakamlar)
  0, 0, 0, 0, 0, 0, 0, 0,                  // 56-63
  0, 0, 0, 0, 0, 0, 0, 0,                  // 64-71 (büyük harfler)
  0, 0, 0, 0, 0, 0, 0, 0,                  // 72-79
  0, 0, 0, 0, 0, 0, 0, 0,                  // 80-87
  0, '1', '2', '3', '4', '5', '6', '7',                  // 88-95
  '8', '9', '0', 0, 0, 0, 0, 0,                  // 96-103 (küçük harfler)
  0, 0, 0, 0, 0, 0, 0, 0,                  // 104-111
  0, 0, 0, 0, 0, 0, 0, 0,                  // 112-119
  0, 0, 0, 0, 0, 0, 0, 0,                  // 120-127
};

char asciiToChar(uint8_t asciiCode) {
  if (asciiCode < 128) {
    char c = ASCIIKeys[asciiCode];
    if (c != 0) {
      return c;
    }
  }
  return '?';  // Desteklenmeyen karakter için ?
}

char decodeMorse(String morseCode) {
  if (morseCode.length() == 0) return '?'; // boşsa anlamlı bir şey yok

  for (int i = 0; i < morseTableSize; i++) {
    if (String(morseTable[i].code) == morseCode) {
      return morseTable[i].letter;
    }
  }

  return '?'; // tanınmayan karakter
}

void laserOn() {
  digitalWrite(LASER_PIN, HIGH);
}

void laserOff() {
  digitalWrite(LASER_PIN, LOW);
}

const char* getMorseCode(char c) {
  c = toupper(c);
  for (int i = 0; i < morseTableSize; i++) {
    if (morseTable[i].letter == c) {
      return morseTable[i].code;
    }
  }
  return "";
}

void sendMorse(const char* text) {
  Serial.print("Mors gönderiliyor: ");
  Serial.println(text);

  for (uint16_t i = 0; i < strlen(text); i++) {
    char c = toupper(text[i]);
    const char* morse = getMorseCode(c);
    if (morse != NULL) {
      for (uint16_t j = 0; j < strlen(morse); j++) {
        if (morse[j] == '.') {
          laserOn();
          delay(dotDuration);
          laserOff();
        } else if (morse[j] == '-') {
          laserOn();
          delay(dotDuration * 3);
          laserOff();
        }
        delay(dotDuration); // karakterler arası boşluk
      }
      delay(dotDuration * 3); // harfler arası ekstra boşluk
    } else {
      delay(dotDuration * 7); // bilinmeyen karakterler için boşluk
    }
  }
  laserOff(); // Lazer mutlaka kapanmalı
}

void redrawLCD() {
  if (strlen(inputBuffer) == 0) {
    lcd.clear(); // Ekranı temizle
    return;      // Fonksiyondan çık
  }
  lcd.clear();
  currentRow = 0;
  currentCol = 0;
  uint16_t i = scrollOffset;

  while (inputBuffer[i] != 0 && currentRow < LCD_ROWS) {
    char word[32] = {0};
    uint8_t wordLength = 0;

    // Kelimeyi oku
    uint16_t j = i;
    while (inputBuffer[j] != ' ' && inputBuffer[j] != 0 && wordLength < sizeof(word) - 1) {
      word[wordLength++] = inputBuffer[j++];
    }

    // Eğer kelime satıra sığmazsa yeni satıra geç
    if (currentCol + wordLength > LCD_COLUMNS) {
      currentRow++;
      currentCol = 0;
      if (currentRow >= LCD_ROWS) break;
      lcd.setCursor(currentCol, currentRow);
    }

    // Kelimeyi yaz
    lcd.setCursor(currentCol, currentRow);
    for (uint8_t k = 0; k < wordLength; k++) {
      lcd.print(word[k]);
      currentCol++;
    }

    i = j;

    // Eğer boşluk varsa yaz
    if (inputBuffer[i] == ' ') {
      if (currentCol < LCD_COLUMNS) {
        lcd.print(' ');
        currentCol++;
        i++;
      } else {
        currentRow++;
        currentCol = 0;
        if (currentRow < LCD_ROWS) {
          lcd.setCursor(currentCol, currentRow);
        }
      }
    }
  }

  // Scroll gerekiyorsa sonraki başlangıcı hesapla
  if (needsScrolling()) {
    scrollOffset = findNextWordStart(scrollOffset);
  }
}

uint16_t findPreviousWordStart(uint16_t currentOffset) {
  if (currentOffset == 0) return 0;

  int i = currentOffset - 1;
  while (i > 0 && inputBuffer[i] == ' ') i--;
  while (i > 0 && inputBuffer[i - 1] != ' ') i--;
  return i;
}




class KeyboardRptParser : public KeyboardReportParser {
    void OnKeyDown(uint8_t mod, uint8_t key) override {
      char character = asciiToChar(key);
      Serial.print("ASCII Code: ");
      Serial.print(key);
      Serial.print(" -> Character: ");
      Serial.println(character);

      if (key == 42) {  // Backspace ASCII kodu
        if (inputIndex > 0) {
          inputIndex--;                 // İmleci geri al
          inputBuffer[inputIndex] = 0;
          redrawLCD();  // Son karakteri sil
          Serial.println("Karakter silindi.");
          Serial.println(inputBuffer);
        }
      } else if (isValidCharacter(character)) {
        if (inputIndex < MAX_INPUT_LENGTH - 1) {
          inputBuffer[inputIndex++] = character;
          inputBuffer[inputIndex] = 0;
          redrawLCD();  // Null-terminate
          Serial.print("Guncel Yazi: ");
          Serial.println(inputBuffer);
        } else {
          Serial.println("Hata: Maksimum karakter sayısına ulaşıldı!");
        }
      } else {
        // Geçersiz karakter geldiğinde istersen burada bir uyarı verebilirsin.
        Serial.println("Gecersiz karakter!");
      }
      if (key == 82) {  // Yukarı ok tuşu
        if (scrollOffset > 0) {
          scrollOffset = findPreviousWordStart(scrollOffset);
          redrawLCD();
        }
        return;
      } else if (key == 81) {  // Aşağı ok tuşu
        uint16_t nextOffset = findNextWordStart(scrollOffset);
        if (nextOffset != scrollOffset) {
          scrollOffset = nextOffset;
          redrawLCD();
        }
        return;
      }
      else if (key == 40) {  // Enter tuşu
        Serial.println("Enter tusuna basildi. Mors kodu gonderiliyor:");
        Serial.println(inputBuffer);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("->GONDERILIYOR<-");
        sendMorse(inputBuffer);  // Mors kodu lazerle gönder
        inputIndex = 0;
        inputBuffer[0] = { 0 };
        redrawLCD();
        lcd.print("-->GONDERILDI<--");
        delay(1500);
        lcd.clear();
      }
    }
};

bool needsScrolling() {
  if (strlen(inputBuffer) == 0) return false;
  uint8_t tempRow = 0;
  uint8_t tempCol = 0;
  uint16_t i = scrollOffset;

  while (inputBuffer[i] != 0) {
    char word[32] = {0};
    uint8_t wordLength = 0;
    uint16_t j = i;

    while (inputBuffer[j] != ' ' && inputBuffer[j] != 0 && wordLength < sizeof(word) - 1) {
      word[wordLength++] = inputBuffer[j++];
    }

    if (tempCol + wordLength > LCD_COLUMNS) {
      tempRow++;
      tempCol = 0;
    }

    tempCol += wordLength;
    i = j;

    if (inputBuffer[i] == ' ') {
      tempCol++;
      i++;
    }

    if (tempRow >= LCD_ROWS) return true;
  }

  return false;
}


uint16_t findNextWordStart(uint16_t currentOffset) {
  uint16_t i = currentOffset;

  // Kelimeyi geç
  while (inputBuffer[i] != ' ' && inputBuffer[i] != 0) i++;

  // Boşluğu geç
  if (inputBuffer[i] == ' ') i++;

  return (inputBuffer[i] != 0) ? i : currentOffset;
}



KeyboardRptParser keyboardEvents;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting USB Host...");

  if (Usb.Init() == -1) {
    Serial.println("USB Host Shield init failed!");
    while (1)
      ;  // Hata varsa burada kal
  }
  Serial.println("USB Host Shield initialized.");

  Keyboard.SetReportParser(0, &keyboardEvents);
  pinMode(LASER_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  laserOff();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  Serial.println("Yazmaya basla:");
}

void loop() {
  Usb.Task();
  if (Serial.available()) {
    delay(10); // Verinin tamamını bekle
    int len = Serial.readBytesUntil('\n', inputBuffer, sizeof(inputBuffer) - 1);
    inputBuffer[len] = '\0';
    scrollOffset = 0;
    redrawLCD();
  }

  // Scroll test (isteğe bağlı)
  static unsigned long lastScrollTime = 0;
  if (millis() - lastScrollTime > 800) {
    lastScrollTime = millis();
    if (strlen(inputBuffer) > LCD_COLUMNS * LCD_ROWS) {
      scrollOffset++;
      if (scrollOffset > strlen(inputBuffer)) scrollOffset = 0;
      redrawLCD();
    }
  }
  if (!messageReceiving) {
    int ldr = analogRead(LDR_PIN);
    if (ldr > THRESHOLD) {
      messageReceiving = true;
      Serial.println(">> Mesaj algilandi. Dinlemeye geciliyor...");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MESAJ ALGILANDI!");
      lcd.setCursor(0, 1);
      lcd.print("--> OKUNUYOR <--");
    }
  }

  if (messageReceiving) {
    readMorseMessage();  // sadece bu fonksiyon aktif
  }

  // Buraya diğer fonksiyonlar gelebilir, ama mesaj varken çalışmaz.
  if (!messageReceiving) {
    // örneğin led yanabilir, lcd yazabilir, vs.
    // digitalWrite(LED_PIN, HIGH);
  }
}

void readMorseMessage() {
  unsigned long now = millis();
  int lightLevel = analogRead(LDR_PIN);
  bool lightDetected = lightLevel > THRESHOLD;

  static bool prevLight = false;
  static unsigned long stateChangeTime = 0;

  if (lightDetected != prevLight) {
    unsigned long duration = now - stateChangeTime;
    stateChangeTime = now;

    if (lightDetected) {
      // Işık açıldı (önceki durum karanlıktı): boşluk süresi
      if (duration >= dotDuration * 6) {
        // Kelime arası boşluk
        if (currentSymbol.length() > 0) {
          messageBuffer += decodeMorse(currentSymbol);
          currentSymbol = "";
        }
        messageBuffer += ' ';
      } else if (duration >= dotDuration * 2.5) {
        // Harf arası boşluk
        if (currentSymbol.length() > 0) {
          messageBuffer += decodeMorse(currentSymbol);
          currentSymbol = "";
        }
      }
    } else {
      // Işık kapandı (önceki durum aydınlıktı): ışık süresi
      if (duration <= dotDuration * 1.5) {
        currentSymbol += '.';
      } else {
        currentSymbol += '-';
      }
    }

    prevLight = lightDetected;
  }

  // Mesajın bittiğini algıla (uzun karanlık)
  if (!lightDetected && (now - stateChangeTime > dotDuration * 12) && messageBuffer.length() > 0) {
    if (currentSymbol.length() > 0) {
      messageBuffer += decodeMorse(currentSymbol);
      currentSymbol = "";
    }

    Serial.println(">> Mesaj tamamlandi:");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("->MESAJ OKUNDU<-");
    Serial.print("<< Gelen Mesaj:  ");
    Serial.println(messageBuffer);
    delay(2000);
    lcd.clear();

    inputBuffer[0] = { 0 };
    messageBuffer.trim();
    strncpy(inputBuffer, messageBuffer.c_str(), sizeof(inputBuffer) - 1);
    inputBuffer[sizeof(inputBuffer) - 1] = '\0';
    scrollOffset = 0;
    messageBuffer = "";
    messageReceiving = false;
    redrawLCD();
    delay(500);
  }
}
