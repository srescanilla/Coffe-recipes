/*
  ============================================================================
  CrowPanel42.h — pantalla e-paper del CrowPanel 4,2" (ESP32-S3)
  REVISIO NOVA, la de l'ADHESIU VERD RODO al darrere.
  ============================================================================

  Per que existeix aquest fitxer
  ------------------------------
  Aquesta placa s'ha venut amb DUES revisions sota el mateix nom de producte:
    - antiga (sense adhesiu): controlador SSD1683, funciona amb la llibreria GxEPD2
    - nova (adhesiu verd):    un altre controlador, NO funciona amb GxEPD2 i a mes
                              necessita el pin 41 en alt com a segona alimentacio
  El manual d'Elecrow diu SSD1683 per a totes dues. Amb la revisio nova, GxEPD2
  dona "Busy Timeout!" a cada refresc i la pantalla no canvia mai, sense cap
  pista del motiu. Aquest fitxer porta la seqüencia bona ja resolta.

  Credit: els registres d'inicialitzacio, les taules d'ona GC i DU i la
  seqüencia d'engegada amb el pin 41 surten del codi d'exemple d'Elecrow,
  carpeta "arduino_A_green_circular_sticker_on_the_back" de:
  github.com/Elecrow-RD/CrowPanel-ESP32-4.2-E-paper-HMI-Display-with-400-300

  ----------------------------------------------------------------------------
  COM ES FA SERVIR (projecte nou, de zero)
  ----------------------------------------------------------------------------
    #include "CrowPanel42.h"          // aquest fitxer, al costat del .ino
    CrowPanel42 epd;

    void setup() {
      Serial.begin(115200);
      epd.begin();                    // alimentacio, SPI, init i pantalla en blanc
      epd.canvas.setTextSize(3);
      epd.canvas.setCursor(40, 100);
      epd.canvas.print("Hola");
      epd.show();                     // ho envia a la pantalla
    }

    void loop() {
      if (epd.premut(CP_OK)) {        // boto amb antirebot
        epd.canvas.fillScreen(CP_BLANC);
        epd.canvas.setCursor(40, 100);
        epd.canvas.print("Premut");
        epd.show();
      }
      epd.tasca();                    // deixa-ho sempre al final del loop
    }

  Dibuixa sempre sobre "epd.canvas", que es un GFXcanvas1 de 400x300 amb totes
  les funcions d'Adafruit_GFX (text, linies, rectangles, cercles, bitmaps).
  IMPORTANT: al canvas, 1 = BLANC i 0 = NEGRE (CP_BLANC i CP_NEGRE).

  ----------------------------------------------------------------------------
  CONFIGURACIO OBLIGATORIA A L'ARDUINO IDE
  ----------------------------------------------------------------------------
    Placa: "ESP32S3 Dev Module"   (no cap de les altres que porten S3 al nom;
                                   si a Tools no hi surt "PSRAM", no es aquesta)
    USB CDC On Boot ... Disabled      <- si no, el Serial Monitor surt buit
    Flash Mode ........ QIO 80MHz
    Flash Size ........ 8MB (64Mb)    <- amb 16MB la placa es reinicia en bucle
    Partition Scheme .. Huge APP (3MB No OTA/1MB SPIFFS)   <- cal si uses BLE/WiFi
    PSRAM ............. OPI PSRAM
    Upload Speed ...... 115200
  L'IDE 2.x recorda la placa PER FINESTRA: revisa-ho cada cop que obris un .ino nou.
  Llibreria necessaria: nomes "Adafruit GFX Library".
  ============================================================================
*/
#ifndef CROWPANEL42_H
#define CROWPANEL42_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>

// ---------------------------------------------------------------- pins fixos
#define CP_SCK    12
#define CP_MOSI   11
#define CP_RES    47
#define CP_DC     46
#define CP_CS     45
#define CP_BUSY   48
#define CP_PWR     7
#define CP_PWR2   41     // NOMES a la revisio de l'adhesiu verd

// Botons de la placa (tots actius en baix)
// Ronda 5: vam intercanviar aquests dos pins perque anaven al reves en
// maquinari real. Ronda 7: segueixen al reves — i com que el mateix
// parell de booleans up/down (llegits un sol cop per volta de loop() amb
// epd.premut(CP_UP)/epd.premut(CP_DOWN)) es passa tal qual tant a
// gestionaPantallaPrincipal() com a tots els pickers, no hi pot haver cap
// bug de codi que nomes afecti els submenus i deixi la pantalla principal
// bé — el mateix parell de pins fa totes dues coses. Conclusio: el canvi
// de la ronda 5 no era la direccio correcta (potser ja ho estava be abans
// i el vaig espatllar, o l'error real era un altre). Es torna a intercanviar
// als valors originals.
#define CP_DOWN    4
#define CP_OK      5
#define CP_UP      6
#define CP_MENU    2
#define CP_EXIT    1

// Pins lliures a la capçalera. Els de "strapping" els llegeix l'ESP32-S3 en
// arrencar: no hi pengis un LED o pot arrencar malament.
//   segurs:      IO8, IO15, IO16, IO17, IO18, IO21
//   strapping:   IO3, IO9, IO14      (evita'ls)
//   USB natiu:   IO19, IO20          (evita'ls si vols conservar el port)

#define CP_W 400
#define CP_H 300
#define CP_BYTES 15000          // 400*300/8

#define CP_BLANC 1              // al canvas, 1 = blanc
#define CP_NEGRE 0              // ... i 0 = negre

// Si la pantalla fa coses rares (taques aleatories que un refresc arregla),
// abaixa primer aixo a 2000000UL; si no, posa CP_HW_SPI a 0.
#ifndef CP_SPI_HZ
  #define CP_SPI_HZ 10000000UL
#endif
#ifndef CP_HW_SPI
  #define CP_HW_SPI 1
#endif

// Refrescos rapids seguits abans d'un de complet per netejar la imatge
// fantasma; i quin percentatge de pixels canviats forca ja un de complet.
// Ronda 5: [Suposant] vas reportar imatges fantasma amb els valors originals
// (8 rapids seguits, llindar 3%) — es baixen a 4 i 2% per forçar el refresc
// complet mes sovint (una mica mes de parpelleig a canvi de menys fantasma).
// Es una primera estimacio raonable, no una xifra provada al teu dispositiu:
// si encara veus fantasmes, baixa CP_MAX_RAPIDS fins a 2-3; si el parpelleig
// molesta massa, puja'l cap a 5-6. No hi ha manera de verificar-ho sense
// maquinari real.
#ifndef CP_MAX_RAPIDS
  #define CP_MAX_RAPIDS 4
#endif
#ifndef CP_LLINDAR_PC
  #define CP_LLINDAR_PC 2
#endif
#ifndef CP_SLEEP_MS
  #define CP_SLEEP_MS 6000
#endif
#ifndef CP_DEBOUNCE_MS
  #define CP_DEBOUNCE_MS 180
#endif
#ifndef CP_LONG_MS
  #define CP_LONG_MS 800
#endif

class CrowPanel42 {
public:
  GFXcanvas1 canvas;

  CrowPanel42() : canvas(CP_W, CP_H), spi(FSPI) {}

  // ---- engegada ----------------------------------------------------------
  void begin(bool esborra = true) {
    pinMode(CP_PWR2, OUTPUT); digitalWrite(CP_PWR2, HIGH);   // clau a la rev. verda
    pinMode(CP_PWR,  OUTPUT); digitalWrite(CP_PWR,  HIGH);
    pinMode(CP_RES, OUTPUT); pinMode(CP_DC, OUTPUT); pinMode(CP_CS, OUTPUT);
    digitalWrite(CP_CS, HIGH);
    pinMode(CP_BUSY, INPUT);
#if CP_HW_SPI
    spi.begin(CP_SCK, -1, CP_MOSI, -1);
#else
    pinMode(CP_SCK, OUTPUT); pinMode(CP_MOSI, OUTPUT);
#endif
    for (int i = 0; i < 5; i++) { pinMode(pinsBotons[i], INPUT_PULLUP); botoEstat[i] = HIGH; }

    if (esborra) { clear(); }
    else { reset(); init(); despert = true; }
    canvas.fillScreen(CP_BLANC);
  }

  // ---- dibuix ------------------------------------------------------------
  // show()      -> decideix sol si cal refresc rapid o complet
  // show(true)  -> forca refresc complet (net, parpelleja, ~3 s)
  void show(bool complet = false) {
    desperta();
    uint32_t canviats = diferencia();
    memcpy(anterior, canvas.getBuffer(), CP_BYTES);
    hiHaAnterior = true;
    bool gran = canviats > (uint32_t)(CP_W * CP_H) * CP_LLINDAR_PC / 100;
    bool ple  = complet || gran || (rapidsSeguits >= CP_MAX_RAPIDS);
    if (ple) rapidsSeguits = 0; else rapidsSeguits++;

    unsigned long t0 = millis();
    escriu(0x50); dada(0xD7);
    escriu(0x13); bloc(canvas.getBuffer(), CP_BYTES);
    if (ple) lutGC(); else lutDU();
    escriu(0x17); dada(0xA5);
    esperaBusy();
    ultimRefresc = millis();
    Serial.print(F("[EPD] ")); Serial.print(ple ? F("complet") : F("rapid"));
    Serial.print(F(" - ")); Serial.print(millis() - t0); Serial.println(F(" ms"));
  }

  // Pantalla tota blanca, amb neteja profunda
  void clear() {
    static uint8_t blanc[250];
    memset(blanc, 0xFF, sizeof(blanc));
    reset(); init();
    escriu(0x10); for (int k = 0; k < 60; k++) bloc(blanc, sizeof(blanc));
    escriu(0x13); for (int k = 0; k < 60; k++) bloc(blanc, sizeof(blanc));
    lutGC(); escriu(0x17); dada(0xA5); esperaBusy();
    despert = true; ultimRefresc = millis();
    hiHaAnterior = false;
    canvas.fillScreen(CP_BLANC);
  }

  void sleep() {
    if (!despert) return;
    escriu(0x07); dada(0xA5); delay(50);
    despert = false;
  }

  // ---- botons ------------------------------------------------------------
  // premut(pin): true un sol cop per pulsacio, amb antirebot.
  // Crida'l SEMPRE fora d'un "||": si no, pot no arribar a executar-se i la
  // pulsacio es perd.
  bool premut(uint8_t pin) {
    int i = idx(pin); if (i < 0) return false;
    bool ara = digitalRead(pin);
    bool disparat = false;
    if (ara != botoEstat[i] && millis() - botoCanvi[i] > CP_DEBOUNCE_MS) {
      botoCanvi[i] = millis();
      if (ara == LOW) disparat = true;
      botoEstat[i] = ara;
    }
    return disparat;
  }

  // Pulsacio llarga d'un boto (per exemple un menu secundari)
  bool premutLlarg(uint8_t pin) {
    int i = idx(pin); if (i < 0) return false;
    bool ara = (digitalRead(pin) == LOW);
    bool disparat = false;
    if (ara && !llargAvall[i]) { llargAvall[i] = true; llargDes[i] = millis(); llargFet[i] = false; }
    else if (ara && llargAvall[i] && !llargFet[i] && millis() - llargDes[i] > CP_LONG_MS) {
      llargFet[i] = true; disparat = true;
    } else if (!ara && llargAvall[i]) llargAvall[i] = false;
    return disparat;
  }

  // ---- manteniment -------------------------------------------------------
  // Posa-ho al final del loop(): adorm la pantalla sola quan fa estona que
  // no canvia res, per estalviar bateria.
  void tasca() {
    if (despert && millis() - ultimRefresc > CP_SLEEP_MS) sleep();
  }

  bool estaDespert() const { return despert; }

private:
  SPIClass spi;
  bool despert = false;
  unsigned long ultimRefresc = 0;
  uint8_t rapidsSeguits = 0;
  uint8_t anterior[CP_BYTES];
  bool hiHaAnterior = false;

  const uint8_t pinsBotons[5] = {CP_DOWN, CP_OK, CP_UP, CP_MENU, CP_EXIT};
  bool botoEstat[5]; unsigned long botoCanvi[5] = {0,0,0,0,0};
  bool llargAvall[5] = {0,0,0,0,0}; unsigned long llargDes[5] = {0,0,0,0,0};
  bool llargFet[5] = {0,0,0,0,0};

  int idx(uint8_t pin) {
    for (int i = 0; i < 5; i++) if (pinsBotons[i] == pin) return i;
    return -1;
  }

  uint32_t diferencia() {
    if (!hiHaAnterior) return 0xFFFFFFFF;
    const uint8_t* b = canvas.getBuffer();
    uint32_t c = 0;
    for (size_t i = 0; i < CP_BYTES; i++) c += __builtin_popcount((unsigned)(b[i] ^ anterior[i]));
    return c;
  }

  void byteOut(uint8_t d) {
#if CP_HW_SPI
    spi.beginTransaction(SPISettings(CP_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(CP_CS, LOW); spi.transfer(d); digitalWrite(CP_CS, HIGH);
    spi.endTransaction();
#else
    digitalWrite(CP_CS, LOW);
    for (uint8_t i = 0; i < 8; i++) {
      digitalWrite(CP_SCK, LOW);
      digitalWrite(CP_MOSI, (d & 0x80) ? HIGH : LOW);
      digitalWrite(CP_SCK, HIGH);
      d <<= 1;
    }
    digitalWrite(CP_CS, HIGH);
#endif
  }
  void escriu(uint8_t reg) { digitalWrite(CP_DC, LOW);  byteOut(reg); digitalWrite(CP_DC, HIGH); }
  void dada(uint8_t d)     { digitalWrite(CP_DC, HIGH); byteOut(d); }

  void bloc(const uint8_t* buf, size_t n) {
#if CP_HW_SPI
    digitalWrite(CP_DC, HIGH);
    spi.beginTransaction(SPISettings(CP_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(CP_CS, LOW); spi.writeBytes(buf, n); digitalWrite(CP_CS, HIGH);
    spi.endTransaction();
#else
    for (size_t i = 0; i < n; i++) dada(buf[i]);
#endif
  }

  void esperaBusy() {
    unsigned long t0 = millis();
    while (digitalRead(CP_BUSY) == 1) {
      delay(1);
      if (millis() - t0 > 15000) { Serial.println(F("[EPD] BUSY no baixa: driver o revisio equivocats?")); return; }
    }
  }

  void reset() {
    digitalWrite(CP_RES, HIGH); delay(10);
    digitalWrite(CP_RES, LOW);  delay(100);
    digitalWrite(CP_RES, HIGH); delay(100);
  }

  void init() {
    escriu(0x00); dada(0x3F); dada(0x4D);
    escriu(0x01); dada(0x03); dada(0x10); dada(0x3F); dada(0x3F); dada(0x03);
    escriu(0x06); dada(0x96); dada(0x96); dada(0x29);
    escriu(0x30); dada(0x09);
    escriu(0x61); dada(0x01); dada(0x90); dada(0x01); dada(0x2C);   // 400 x 300
    escriu(0x82); dada(0x05);
    escriu(0x50); dada(0x97);
    escriu(0x60); dada(0x22);
    escriu(0xE3); dada(0x88);
  }

  void desperta() {
    if (despert) return;
    reset(); delay(20); init(); delay(50);
    despert = true;
  }

  void taula(uint8_t reg, const uint8_t* t) {
    escriu(reg); for (int i = 0; i < 42; i++) dada(t[i]);
  }
  void lutGC() {
    static const uint8_t a[42]={0x01,0x14,0x0A,0x14,0x00,0x01,0x01};
    static const uint8_t b[42]={0x01,0x54,0x0A,0x94,0x00,0x01,0x01};
    static const uint8_t c[42]={0x01,0x54,0x0A,0x94,0x00,0x01,0x01};
    static const uint8_t d[42]={0x01,0x94,0x0A,0x54,0x00,0x01,0x01};
    static const uint8_t e[42]={0x01,0x94,0x0A,0x54,0x00,0x01,0x01};
    taula(0x20,a); taula(0x21,b); taula(0x22,c); taula(0x23,d); taula(0x24,e);
  }
  void lutDU() {
    static const uint8_t a[42]={0x01,0x14,0x00,0x00,0x00,0x01,0x00};
    static const uint8_t b[42]={0x01,0x14,0x00,0x00,0x00,0x01,0x00};
    static const uint8_t c[42]={0x01,0x94,0x00,0x00,0x00,0x01,0x00};
    static const uint8_t d[42]={0x01,0x54,0x00,0x00,0x00,0x01,0x00};
    static const uint8_t e[42]={0x01,0x14,0x00,0x00,0x00,0x01,0x00};
    taula(0x20,a); taula(0x21,b); taula(0x22,c); taula(0x23,d); taula(0x24,e);
  }
};

#endif // CROWPANEL42_H
