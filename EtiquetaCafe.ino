/*
  ============================================================================
  EtiquetaCafe.ino — etiqueta de presets de cafe sobre CrowPanel 4,2"
  ============================================================================

  Que fa
  ------
  Mostra 4 caselles: TIPUS DE CAFE, GRAMS, NUM. MOLTA i NOTES.

    - OK      -> mou el focus a la casella seguent (NOM -> GRAMS -> MOLTA ->
                 NOTES -> NOM...) i desa a la memoria (NVS) qualsevol canvi
                 pendent de la casella que deixes.
    - AMUNT / AVALL -> canvien el valor de la casella seleccionada:
                 · NOM: passa al cafe seguent/anterior (ordre alfabetic) i
                   carrega automaticament els seus grams/molta/notes.
                 · GRAMS / MOLTA: suma o resta 0.5 al valor d'aquell cafe
                   (grams 0-60, molta 0-100 — cal marge per anar de rangs
                   d'espresso fins a V60).
                 · NOTES: passa a la seguent nota ja feta servir en algun cafe.
    - MENU    -> obre una llista de la casella activa:
                 · NOM: tots els cafes desats.
                 · GRAMS: valors estandard de 15 a 38 g, sense decimals.
                 · MOLTA: valors estandard agrupats en dos blocs, ESPRESSO
                   (10-25) i V60 (55-80), sense decimals.
                 · NOTES: una llista curada de notes de tast habituals. Aqui
                   OK marca/desmarca (fins a 3 alhora) i MENU aplica la
                   seleccio — es l'unica llista de seleccio multiple.
    - EXIT    -> dins la llista, la tanca sense triar res. A la pantalla
                 principal, desa qualsevol canvi pendent.

  Els noms de cafe NOUS s'afegeixen des del mobil per Bluetooth (BLE) amb
  control.html — al dispositiu nomes es pot navegar i ajustar cafes que ja
  existeixen.

  Perque nomes es pot afegir un cafe nou des del mobil: amb 5 botons no hi ha
  manera comoda d'escriure text al dispositiu. Vegeu control.html i el README.

  Llibreries necessaries (totes venen amb el paquet de placa ESP32, nomes cal
  instal·lar "Adafruit GFX Library" a mes — els tipus de lletra Free Fonts
  que fem servir per als textos venen dins d'aquesta mateixa llibreria):
    - Adafruit GFX Library     (Library Manager)
    - Preferences, BLEDevice, BLEServer, BLEUtils, BLE2902   (nucli ESP32)

  Configuracio de l'Arduino IDE: la mateixa taula de NOTES-CrowPanel42.md.
  IMPORTANT: com que fem servir BLE, "Partition Scheme" ha de ser
  "Huge APP (3MB No OTA/1MB SPIFFS)" — amb l'esquema petit per defecte el
  sketch no cap ("Sketch too big").
  ============================================================================
*/
#include "CrowPanel42.h"
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>

// ---------------------------------------------------------------- dades
#define MAX_PRESETS 30
#define LEN_NOM     24
#define LEN_NOTES   40
#define MAX_PICKER  48   // llistes fixes (molta: 2 capcaleres + 42 valors)

struct Preset {
  char  nom[LEN_NOM];
  float grams;
  float molta;
  char  notes[LEN_NOTES];
};

Preset presets[MAX_PRESETS];
int  numPresets = 0;
int  selIndex   = -1;      // -1 = cap cafe seleccionat (encara no n'hi ha cap)
bool brut       = false;   // hi ha canvis del cafe seleccionat sense desar

Preferences prefs;
CrowPanel42 epd;

enum Camp { CAMP_NOM = 0, CAMP_GRAMS = 1, CAMP_MOLTA = 2, CAMP_NOTES = 3, N_CAMPS = 4 };
int campActiu = CAMP_NOM;

// ---- llista (picker) que obre el boto MENU ----
bool pickerObert = false;
int  pickerN = 0;
int  pickerSel = 0;
int  pickerScroll = 0;
char pickerEtiquetes[MAX_PICKER][LEN_NOTES];
bool pickerEsCapcalera[MAX_PICKER];   // fila de separador ("-- ESPRESSO --"), no seleccionable
bool pickerMarcat[MAX_PICKER];        // nomes es fa servir a NOTES (seleccio multiple)

bool calRedibuixar = true;

// Notes de tast habituals — llista curada perque no calgui escriure-les.
// (ASCII pla directament: veure aFolreASCII mes avall sobre per que.)
const char *NOTES_CURADES[] = {
  "Xocolata", "Caramel", "Nous", "Ametlla", "Mel", "Sucre moreno",
  "Citrics", "Llimona", "Taronja", "Poma", "Maduixa", "Nabius",
  "Fruita tropical", "Raim", "Gessami", "Rosa", "Herbaci",
  "Especiat", "Terros", "Vainilla"
};
const int N_NOTES_CURADES = sizeof(NOTES_CURADES) / sizeof(NOTES_CURADES[0]);

// ------------------------------------------------------------- icona (96x60)
// Generada a partir del bitmap real que vas passar (epd_bitmap_.bin, un
// dibuix de la teva maquina a 1480x1384 px, 1 bit, format image2cpp).
// Retallada al contingut, escalada mantenint proporcio dins de 96x60 i
// convertida amb difuminat (dithering) perque es reconegui a mida petita
// en lloc de perdre els trets fins amb un simple llindar. 1 = pinta negre.
const uint8_t ICONA_CAFETERA[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0x7F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x5F, 0xFF, 0xFF, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x0F, 0xFF, 0xF4, 0x01, 0x80, 0x00, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x1D, 0x00, 0x02, 0xAB, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x18, 0xD5, 0xB7, 0x81, 0xFF, 0xEA, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x1F, 0x00, 0x04, 0x01, 0xFF, 0x81, 0x50, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x18, 0x00, 0xF4, 0x01, 0xFF, 0xC0, 0x0B, 0x40, 0x00, 0x00,
  0x00, 0x00, 0x18, 0x00, 0x84, 0x01, 0xFF, 0x80, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x15, 0x5B, 0x54, 0x03, 0x7F, 0x80, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x1C, 0x00, 0x05, 0x5B, 0xFB, 0xC0, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x30, 0x00, 0x05, 0x03, 0x73, 0x80, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x30, 0x00, 0x04, 0x93, 0xFF, 0xC0, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x34, 0x00, 0x0B, 0x4B, 0xFF, 0x80, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x30, 0x00, 0x04, 0x03, 0xFF, 0xC0, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x30, 0x00, 0x08, 0x57, 0xFB, 0x80, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x30, 0x00, 0x0B, 0x03, 0xF3, 0xC0, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x2A, 0x55, 0x54, 0x06, 0xFF, 0x80, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x6D, 0x6B, 0x48, 0x03, 0xFF, 0x80, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x60, 0x00, 0x00, 0x06, 0xFF, 0xC0, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x70, 0x82, 0x10, 0x07, 0xFB, 0x80, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x60, 0x42, 0x08, 0x07, 0xF3, 0xC0, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x60, 0x80, 0x10, 0x06, 0xFF, 0x80, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFD, 0xFF, 0x80, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x2A, 0xAF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1F, 0xFF, 0xFF, 0xFF, 0xC0, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x0F, 0x7F, 0xFF, 0xFF, 0xED, 0xD5, 0x42, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x0F, 0x80, 0x07, 0xFF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x07, 0x80, 0x07, 0xBF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x20, 0x17, 0xEA, 0xBF, 0xBF, 0xC0, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0xD0, 0x1F, 0xE8, 0x1F, 0xFF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x01, 0x2B, 0xC0, 0x24, 0xBF, 0xBF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x9C, 0x80, 0x2F, 0xFF, 0xBF, 0xC0, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x01, 0x49, 0x40, 0xDF, 0xFF, 0xBF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0xAD, 0x07, 0xFF, 0xFF, 0xBF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x4D, 0xDF, 0xFF, 0xFF, 0xBF, 0xC0, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x22, 0x87, 0xFF, 0xFF, 0x5F, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x30, 0x07, 0xFF, 0xFF, 0x1F, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x20, 0x07, 0xFF, 0xFF, 0xFF, 0xC0, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x20, 0x07, 0xFF, 0xFF, 0xFF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x40, 0x07, 0xFF, 0xFF, 0xFF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x20, 0x07, 0xFF, 0xFF, 0xFF, 0xC0, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x40, 0x07, 0xFF, 0xFF, 0xFF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x60, 0x07, 0xFF, 0xFF, 0xFF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x07, 0xFF, 0xFF, 0xFF, 0xC0, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x07, 0xFF, 0xFF, 0xFF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3E, 0xFB, 0xA0, 0x1F, 0xFF, 0xC0, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3F, 0xFE, 0xFF, 0xF4, 0x3D, 0x40, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0xFF, 0xC0, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0xFF, 0x00, 0x40, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0xFF, 0x00, 0x41, 0x40, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0xFF, 0x00, 0x5E, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFE, 0x05, 0x48, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x0B, 0xFF, 0x50, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
#define ICONA_W 96
#define ICONA_H 60

// ---------------------------------------------------------------- BLE
#define SERVEI_UUID    "a1e8f350-5b3e-4c2c-9a1a-2f7f6a9d1001"
#define CARACT_RX_UUID "a1e8f350-5b3e-4c2c-9a1a-2f7f6a9d1002"   // mobil -> placa
#define CARACT_TX_UUID "a1e8f350-5b3e-4c2c-9a1a-2f7f6a9d1003"   // placa -> mobil

BLECharacteristic *txChar = nullptr;
bool bleConnectat = false;

char   bleBuf[256];
size_t bleLen = 0;
volatile bool bleLiniaPendent = false;
char   bleLiniaProcessar[256];

class ElMeuServidor : public BLEServerCallbacks {
  void onConnect(BLEServer *s) { bleConnectat = true;  calRedibuixar = true; }
  void onDisconnect(BLEServer *s) {
    bleConnectat = false; calRedibuixar = true;
    BLEDevice::startAdvertising();   // torna a ser visible per a la propera connexio
  }
};

// IMPORTANT (NOTES-CrowPanel42.md #9): cap feina lenta dins d'un callback BLE.
// Aqui nomes acumulem bytes; tot el proces real es fa des del loop().
class RxCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    // NOTA: getValue() torna Arduino String en aquesta versio de la
    // llibreria BLE (no std::string com en altres versions del nucli ESP32).
    String v = c->getValue();
    for (size_t i = 0; i < v.length(); i++) {
      char ch = v[i];
      if (ch == '\n' || ch == '\r') {
        if (bleLen > 0 && !bleLiniaPendent) {
          bleBuf[bleLen] = '\0';
          strncpy(bleLiniaProcessar, bleBuf, sizeof(bleLiniaProcessar) - 1);
          bleLiniaProcessar[sizeof(bleLiniaProcessar) - 1] = '\0';
          bleLiniaPendent = true;
          bleLen = 0;
        }
      } else if (bleLen < sizeof(bleBuf) - 1) {
        bleBuf[bleLen++] = ch;
      }
    }
  }
};

// ---------------------------------------------------------------- prototips
// (primitius nomes a les signatures — evita el parany de NOTES-CrowPanel42.md #3)
void   carregaPresets();
void   desaNVS();
int    trobaPerNom(const char *nom);
int    insereixOrdenat(const char *nom);
void   reordenaPerNom();
void   aFolreASCII(char *dst, const char *src, size_t cap);
char   mapejaAccent(unsigned int cp);
const char* formataNum(float v, const char *unitat);
void   configuraBLE();
void   enviaBLE(const char *linia);
void   processaComandaBLE(char *linia);
void   canviaSeleccio(int dir);
void   ciclaNotes(int dir);
void   afegeixFila(const char *text, bool capcalera);
int    trobaValorProper(int valor);
void   marcaPreseleccio();
void   obrePicker();
void   aplicaPicker();
void   mouSeleccioPicker(int dir);
const char* titolPicker();
void   gestionaPantallaPrincipal(bool up, bool down, bool ok, bool menu, bool exit_);
void   gestionaPicker(bool up, bool down, bool ok, bool menu, bool exit_);
void   centraText(int x, int y, int w, int h, const char *text);
void   dibuixa();
void   dibuixaFocus(int x, int y, int w, int h);
void   requadreObertDalt(int x, int y, int w, int h, int r, int marge, uint16_t color);
void   fillArrodonitDalt(int x, int y, int w, int h, int r, uint16_t color);
void   dibuixaCaixaValor(int x, int yCapcalera, int w, int hCapcalera, int hValor, const char *etiqueta, const char *valor, bool focus);
void   dibuixaValorNotes(int x, int y, int w, int h, const char *text);
void   dibuixaPeu();
void   dibuixaPicker();

// ================================================================== setup
void setup() {
  Serial.begin(115200);
  epd.begin();
  carregaPresets();
  configuraBLE();
  calRedibuixar = true;
}

// =================================================================== loop
void loop() {
  bool up    = epd.premut(CP_UP);
  bool down  = epd.premut(CP_DOWN);
  bool ok    = epd.premut(CP_OK);
  bool menu  = epd.premut(CP_MENU);
  bool exit_ = epd.premut(CP_EXIT);

  if (bleLiniaPendent) {
    processaComandaBLE(bleLiniaProcessar);
    bleLiniaPendent = false;
    calRedibuixar = true;
  }

  if (pickerObert) {
    gestionaPicker(up, down, ok, menu, exit_);
  } else {
    gestionaPantallaPrincipal(up, down, ok, menu, exit_);
  }

  if (calRedibuixar) {
    dibuixa();
    epd.show();
    calRedibuixar = false;
  }
  epd.tasca();
}

// ============================================================ navegacio UI
void gestionaPantallaPrincipal(bool up, bool down, bool ok, bool menu, bool exit_) {
  if (ok) {
    if (brut) desaNVS();
    campActiu = (campActiu + 1) % N_CAMPS;
    calRedibuixar = true;
  }
  if (exit_) {
    if (brut) desaNVS();
    calRedibuixar = true;
  }
  if (menu && numPresets > 0) {
    obrePicker();
    if (pickerN > 0) pickerObert = true;
    calRedibuixar = true;
  }
  if ((up || down) && numPresets > 0) {
    int dir = up ? 1 : -1;
    switch (campActiu) {
      case CAMP_NOM:
        canviaSeleccio(dir);
        break;
      case CAMP_GRAMS:
        presets[selIndex].grams += dir * 0.5f;
        if (presets[selIndex].grams < 0)  presets[selIndex].grams = 0;
        if (presets[selIndex].grams > 60) presets[selIndex].grams = 60;
        brut = true;
        break;
      case CAMP_MOLTA:
        presets[selIndex].molta += dir * 0.5f;
        if (presets[selIndex].molta < 0)   presets[selIndex].molta = 0;
        if (presets[selIndex].molta > 100) presets[selIndex].molta = 100;
        brut = true;
        break;
      case CAMP_NOTES:
        ciclaNotes(dir);
        break;
    }
    calRedibuixar = true;
  }
}

// Dins la llista: NOM/GRAMS/MOLTA es seleccio unica (OK tria i tanca). NOTES
// es seleccio multiple (OK marca/desmarca, MENU aplica el que hi hagi marcat).
void gestionaPicker(bool up, bool down, bool ok, bool menu, bool exit_) {
  if (pickerN == 0) { pickerObert = false; calRedibuixar = true; return; }

  if (up)   { mouSeleccioPicker(-1); calRedibuixar = true; }
  if (down) { mouSeleccioPicker(1);  calRedibuixar = true; }

  if (campActiu == CAMP_NOTES) {
    if (ok) {
      int marcades = 0;
      for (int i = 0; i < pickerN; i++) if (pickerMarcat[i]) marcades++;
      if (pickerMarcat[pickerSel]) pickerMarcat[pickerSel] = false;
      else if (marcades < 3) pickerMarcat[pickerSel] = true;
      calRedibuixar = true;
    }
    if (menu) { aplicaPicker(); pickerObert = false; calRedibuixar = true; }
  } else {
    if (ok) { aplicaPicker(); pickerObert = false; calRedibuixar = true; }
  }
  if (exit_) { pickerObert = false; calRedibuixar = true; }
}

void mouSeleccioPicker(int dir) {
  if (pickerN == 0) return;
  int intents = 0;
  do {
    pickerSel = (pickerSel + dir + pickerN) % pickerN;
    intents++;
  } while (pickerEsCapcalera[pickerSel] && intents <= pickerN);
}

void canviaSeleccio(int dir) {
  if (numPresets == 0) return;
  if (brut) desaNVS();
  selIndex = (selIndex + dir + numPresets) % numPresets;
  prefs.putString("sel", presets[selIndex].nom);
}

void ciclaNotes(int dir) {
  char llista[MAX_PRESETS][LEN_NOTES];
  int n = 0;
  for (int i = 0; i < numPresets; i++) {
    if (strlen(presets[i].notes) == 0) continue;
    bool trobat = false;
    for (int k = 0; k < n; k++) if (strcmp(llista[k], presets[i].notes) == 0) { trobat = true; break; }
    if (!trobat) { strncpy(llista[n], presets[i].notes, LEN_NOTES - 1); llista[n][LEN_NOTES - 1] = '\0'; n++; }
  }
  if (n == 0) return;
  int idx = 0;
  for (int i = 0; i < n; i++) if (strcmp(llista[i], presets[selIndex].notes) == 0) { idx = i; break; }
  idx = (idx + dir + n) % n;
  strncpy(presets[selIndex].notes, llista[idx], LEN_NOTES - 1);
  presets[selIndex].notes[LEN_NOTES - 1] = '\0';
  brut = true;
}

// ---------------------------------------------------------------- picker
void afegeixFila(const char *text, bool capcalera) {
  if (pickerN >= MAX_PICKER) return;
  strncpy(pickerEtiquetes[pickerN], text, LEN_NOTES - 1);
  pickerEtiquetes[pickerN][LEN_NOTES - 1] = '\0';
  pickerEsCapcalera[pickerN] = capcalera;
  pickerMarcat[pickerN] = false;
  pickerN++;
}

// Troba, entre les files NO capcalera, la que te el valor numeric mes proper
// (fa servir atoi, que ja para al primer caracter no numeric — be per "17g").
int trobaValorProper(int valor) {
  int millor = -1, millorDif = 999999;
  for (int i = 0; i < pickerN; i++) {
    if (pickerEsCapcalera[i]) continue;
    int v = atoi(pickerEtiquetes[i]);
    int dif = abs(v - valor);
    if (dif < millorDif) { millorDif = dif; millor = i; }
  }
  return millor < 0 ? 0 : millor;
}

// Pre-marca a la llista curada de NOTES les entrades que ja formen part de
// la nota actual del cafe seleccionat (si en torna a obrir el submenu, veu
// el que ja tenia triat).
void marcaPreseleccio() {
  char copia[LEN_NOTES];
  strncpy(copia, presets[selIndex].notes, LEN_NOTES - 1);
  copia[LEN_NOTES - 1] = '\0';
  char *tros = strtok(copia, ",");
  while (tros) {
    while (*tros == ' ') tros++;
    for (int i = 0; i < pickerN; i++) {
      if (strcasecmp(pickerEtiquetes[i], tros) == 0) { pickerMarcat[i] = true; break; }
    }
    tros = strtok(NULL, ",");
  }
}

void obrePicker() {
  pickerN = 0;
  pickerScroll = 0;

  if (campActiu == CAMP_NOM) {
    for (int i = 0; i < numPresets; i++) afegeixFila(presets[i].nom, false);
    pickerSel = (selIndex >= 0) ? selIndex : 0;

  } else if (campActiu == CAMP_GRAMS) {
    for (int g = 15; g <= 38; g++) {
      char buf[8]; snprintf(buf, sizeof(buf), "%dg", g);
      afegeixFila(buf, false);
    }
    pickerSel = trobaValorProper((int)roundf(presets[selIndex].grams));

  } else if (campActiu == CAMP_MOLTA) {
    afegeixFila("-- ESPRESSO --", true);
    for (int m = 10; m <= 25; m++) { char buf[8]; snprintf(buf, sizeof(buf), "%d", m); afegeixFila(buf, false); }
    afegeixFila("-- V60 --", true);
    for (int m = 55; m <= 80; m++) { char buf[8]; snprintf(buf, sizeof(buf), "%d", m); afegeixFila(buf, false); }
    pickerSel = trobaValorProper((int)roundf(presets[selIndex].molta));

  } else if (campActiu == CAMP_NOTES) {
    for (int i = 0; i < N_NOTES_CURADES; i++) afegeixFila(NOTES_CURADES[i], false);
    marcaPreseleccio();
    pickerSel = 0;
  }
}

const char *titolPicker() {
  switch (campActiu) {
    case CAMP_NOM:   return "TIPUS DE CAFE";
    case CAMP_GRAMS: return "GRAMS (15-38g)";
    case CAMP_MOLTA: return "NUM. MOLTA";
    default:         return "NOTES (tria fins a 3)";
  }
}

void aplicaPicker() {
  if (campActiu == CAMP_NOM) {
    if (brut) desaNVS();
    selIndex = pickerSel;
    prefs.putString("sel", presets[selIndex].nom);

  } else if (campActiu == CAMP_GRAMS) {
    presets[selIndex].grams = (float)atoi(pickerEtiquetes[pickerSel]);
    brut = true;

  } else if (campActiu == CAMP_MOLTA) {
    presets[selIndex].molta = (float)atoi(pickerEtiquetes[pickerSel]);
    brut = true;

  } else if (campActiu == CAMP_NOTES) {
    char resultat[LEN_NOTES] = "";
    for (int i = 0; i < pickerN; i++) {
      if (!pickerMarcat[i]) continue;
      size_t len = strlen(resultat);
      size_t afegit = strlen(pickerEtiquetes[i]) + (len > 0 ? 2 : 0);
      if (len + afegit >= LEN_NOTES) break;   // no hi cap mes, ho deixem estar
      if (len > 0) strcat(resultat, ", ");
      strcat(resultat, pickerEtiquetes[i]);
    }
    strncpy(presets[selIndex].notes, resultat, LEN_NOTES - 1);
    presets[selIndex].notes[LEN_NOTES - 1] = '\0';
    brut = true;
  }
}

// ---------------------------------------------------------------- desat NVS
// Valors com a text (nom del cafe), no posicions dins la llista — si la
// llista canvia (una addicio o un esborrat des del mobil), el que es mostra
// no salta a un altre cafe sense voler-ho. (NOTES-CrowPanel42.md #9)
void carregaPresets() {
  prefs.begin("cafe", false);
  numPresets = prefs.getInt("n", 0);
  if (numPresets > MAX_PRESETS) numPresets = MAX_PRESETS;
  size_t llegit = prefs.getBytes("ps", presets, sizeof(Preset) * MAX_PRESETS);
  if (llegit != sizeof(Preset) * MAX_PRESETS) numPresets = 0;   // primer arrencada

  selIndex = -1;
  if (numPresets > 0) {
    String selDesat = prefs.getString("sel", "");
    selIndex = 0;
    for (int i = 0; i < numPresets; i++) {
      if (selDesat.equals(presets[i].nom)) { selIndex = i; break; }
    }
  }
}

void desaNVS() {
  prefs.putBytes("ps", presets, sizeof(Preset) * MAX_PRESETS);
  prefs.putInt("n", numPresets);
  if (selIndex >= 0) prefs.putString("sel", presets[selIndex].nom);
  brut = false;
}

int trobaPerNom(const char *nom) {
  for (int i = 0; i < numPresets; i++) if (strcmp(presets[i].nom, nom) == 0) return i;
  return -1;
}

int insereixOrdenat(const char *nom) {
  int pos = numPresets;
  for (int i = 0; i < numPresets; i++) if (strcasecmp(nom, presets[i].nom) < 0) { pos = i; break; }
  for (int i = numPresets; i > pos; i--) presets[i] = presets[i - 1];
  strncpy(presets[pos].nom, nom, LEN_NOM - 1);
  presets[pos].nom[LEN_NOM - 1] = '\0';
  numPresets++;
  if (selIndex >= pos) selIndex++;
  return pos;
}

void reordenaPerNom() {
  char nomSel[LEN_NOM] = "";
  if (selIndex >= 0) { strncpy(nomSel, presets[selIndex].nom, LEN_NOM - 1); nomSel[LEN_NOM - 1] = '\0'; }
  for (int i = 1; i < numPresets; i++) {
    Preset tmp = presets[i];
    int j = i - 1;
    while (j >= 0 && strcasecmp(presets[j].nom, tmp.nom) > 0) { presets[j + 1] = presets[j]; j--; }
    presets[j + 1] = tmp;
  }
  selIndex = trobaPerNom(nomSel);
}

// -------------------------------------------------------- text ASCII segur
// Els tipus de lletra namescos nomes cobreixen ASCII 32-126 (NOTES-CrowPanel42.md
// #7): qualsevol accent catala/castella arribat per BLE es converteix aqui
// abans de desar-lo, perque sempre es pugui ensenyar sense brossa a la pantalla.
void aFolreASCII(char *dst, const char *src, size_t cap) {
  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j < cap - 1; ) {
    unsigned char c = (unsigned char)src[i];
    if (c < 0x80) {
      dst[j++] = (char)c; i++;
    } else if ((c & 0xE0) == 0xC0 && src[i + 1] != '\0') {
      unsigned char c2 = (unsigned char)src[i + 1];
      unsigned int cp = ((c & 0x1F) << 6) | (c2 & 0x3F);
      dst[j++] = mapejaAccent(cp);
      i += 2;
    } else {
      i++;   // seqüencia multibyte no esperada: la saltem
    }
  }
  dst[j] = '\0';
}

char mapejaAccent(unsigned int cp) {
  switch (cp) {
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return 'a';
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return 'A';
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return 'E';
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'I';
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return 'O';
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return 'U';
    case 0xE7: return 'c';
    case 0xC7: return 'C';
    case 0xF1: return 'n';
    case 0xD1: return 'N';
    case 0xB7: return '.';   // punt volat, l·l
    default: return '?';
  }
}

const char *formataNum(float v, const char *unitat) {
  static char buf[16];
  snprintf(buf, sizeof(buf), "%.1f%s", v, unitat);
  return buf;
}

// =================================================================== BLE
// Protocol de text, una linia per comanda/resposta acabada en '\n'.
// Cada linia s'envia partida en trossos petits (~18 bytes) perque cap sempre
// dins l'MTU minim que qualsevol telefon garanteix, sense dependre que
// s'hagi negociat un MTU mes gran (NOTES-CrowPanel42.md #9).
//
//   Mobil -> placa (escriptura a CARACT_RX_UUID):
//     ADD|nom|grams|molta|notes   -> crea el cafe si no existeix, o
//                                     actualitza grams/molta/notes si ja hi era
//     DEL|nom                     -> esborra un cafe
//     REN|nomVell|nomNou          -> el reanomena
//     SYNC                        -> demana la llista sencera actual
//     PING                        -> comprovacio de connexio
//
//   Placa -> mobil (notificacions a CARACT_TX_UUID):
//     OK|ADD|nom  / ERR|ADD|motiu   (motiu: FORMAT, NOM_BUIT, PLE)
//     OK|DEL|nom  / ERR|DEL|NOTFOUND
//     OK|REN|nom  / ERR|REN|NOTFOUND|DUP
//     CNT|n, despres n linies ITEM|nom|grams|molta|notes, despres DONE|SYNC
//     PONG|1.0
void configuraBLE() {
  BLEDevice::init("EtiquetaCafe");
  BLEServer *servidor = BLEDevice::createServer();
  servidor->setCallbacks(new ElMeuServidor());
  BLEService *servei = servidor->createService(SERVEI_UUID);

  BLECharacteristic *rxChar = servei->createCharacteristic(
      CARACT_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setCallbacks(new RxCallback());

  txChar = servei->createCharacteristic(CARACT_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

  servei->start();

  // El paquet d'anunci principal nomes te 31 bytes. Un UUID de servei de 128
  // bits (18 bytes) mes el nom "EtiquetaCafe" no hi caben junts: si els
  // poses tots dos al mateix paquet, la pila en descarta un en silenci -
  // normalment el nom, i aleshores el mobil no el troba mai pel filtre
  // namePrefix encara que la placa s'estigui anunciant perfectament. El nom
  // va al paquet principal (imprescindible per trobar-lo); el UUID del
  // servei, al scan response, que te el seu propi marge de 31 bytes.
  BLEAdvertisementData advData;
  advData.setFlags(0x06);   // LE General Discoverable, sense BR/EDR
  advData.setName("EtiquetaCafe");

  BLEAdvertisementData scanData;
  scanData.setCompleteServices(BLEUUID(SERVEI_UUID));

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println(F("[BLE] anunciant-se com a EtiquetaCafe"));
}

void enviaBLE(const char *linia) {
  if (!bleConnectat || !txChar) return;
  char buf[240];
  size_t total = snprintf(buf, sizeof(buf), "%s\n", linia);
  const size_t MIDA_TROS = 18;
  size_t pos = 0;
  while (pos < total) {
    size_t n = min(MIDA_TROS, total - pos);
    txChar->setValue((uint8_t *)(buf + pos), n);
    txChar->notify();
    pos += n;
    delay(8);
  }
}

void processaComandaBLE(char *linia) {
  char *guarda;
  char *cmd = strtok_r(linia, "|", &guarda);
  if (!cmd) return;

  if (strcmp(cmd, "PING") == 0) {
    enviaBLE("PONG|1.0");

  } else if (strcmp(cmd, "SYNC") == 0) {
    char capcalera[16];
    snprintf(capcalera, sizeof(capcalera), "CNT|%d", numPresets);
    enviaBLE(capcalera);
    for (int i = 0; i < numPresets; i++) {
      char buf[128], g[12], m[12];
      dtostrf(presets[i].grams, 0, 1, g);
      dtostrf(presets[i].molta, 0, 1, m);
      snprintf(buf, sizeof(buf), "ITEM|%s|%s|%s|%s", presets[i].nom, g, m, presets[i].notes);
      enviaBLE(buf);
    }
    enviaBLE("DONE|SYNC");

  } else if (strcmp(cmd, "ADD") == 0) {
    char *nomRaw   = strtok_r(NULL, "|", &guarda);
    char *gramsS   = strtok_r(NULL, "|", &guarda);
    char *moltaS   = strtok_r(NULL, "|", &guarda);
    char *notesRaw = strtok_r(NULL, "|", &guarda);
    if (!nomRaw || !gramsS || !moltaS) { enviaBLE("ERR|ADD|FORMAT"); return; }

    char nom[LEN_NOM], notes[LEN_NOTES];
    aFolreASCII(nom, nomRaw, sizeof(nom));
    aFolreASCII(notes, notesRaw ? notesRaw : "", sizeof(notes));
    if (strlen(nom) == 0) { enviaBLE("ERR|ADD|NOM_BUIT"); return; }

    int idx = trobaPerNom(nom);
    if (idx < 0) {
      if (numPresets >= MAX_PRESETS) { enviaBLE("ERR|ADD|PLE"); return; }
      idx = insereixOrdenat(nom);
    }
    presets[idx].grams = atof(gramsS);
    presets[idx].molta = atof(moltaS);
    strncpy(presets[idx].notes, notes, LEN_NOTES - 1);
    presets[idx].notes[LEN_NOTES - 1] = '\0';
    desaNVS();
    if (selIndex < 0) { selIndex = idx; prefs.putString("sel", presets[idx].nom); }
    char resp[64]; snprintf(resp, sizeof(resp), "OK|ADD|%s", nom);
    enviaBLE(resp);

  } else if (strcmp(cmd, "DEL") == 0) {
    char *nomRaw = strtok_r(NULL, "|", &guarda);
    if (!nomRaw) { enviaBLE("ERR|DEL|FORMAT"); return; }
    char nom[LEN_NOM]; aFolreASCII(nom, nomRaw, sizeof(nom));
    int idx = trobaPerNom(nom);
    if (idx < 0) { enviaBLE("ERR|DEL|NOTFOUND"); return; }
    for (int i = idx; i < numPresets - 1; i++) presets[i] = presets[i + 1];
    numPresets--;
    if (selIndex == idx) selIndex = (numPresets > 0) ? 0 : -1;
    else if (selIndex > idx) selIndex--;
    desaNVS();
    char resp[64]; snprintf(resp, sizeof(resp), "OK|DEL|%s", nom);
    enviaBLE(resp);

  } else if (strcmp(cmd, "REN") == 0) {
    char *vellRaw = strtok_r(NULL, "|", &guarda);
    char *nouRaw  = strtok_r(NULL, "|", &guarda);
    if (!vellRaw || !nouRaw) { enviaBLE("ERR|REN|FORMAT"); return; }
    char vell[LEN_NOM], nou[LEN_NOM];
    aFolreASCII(vell, vellRaw, sizeof(vell));
    aFolreASCII(nou, nouRaw, sizeof(nou));
    int idx = trobaPerNom(vell);
    if (idx < 0) { enviaBLE("ERR|REN|NOTFOUND"); return; }
    if (trobaPerNom(nou) >= 0) { enviaBLE("ERR|REN|DUP"); return; }
    strncpy(presets[idx].nom, nou, LEN_NOM - 1);
    presets[idx].nom[LEN_NOM - 1] = '\0';
    reordenaPerNom();
    desaNVS();
    char resp[64]; snprintf(resp, sizeof(resp), "OK|REN|%s", nou);
    enviaBLE(resp);

  } else {
    enviaBLE("ERR|CMD|DESCONEGUDA");
  }
}

// ================================================================== dibuix
// Tot el text centrat dins la seva caixa (getTextBounds ja compensa la
// linia de base diferent que fan servir els Free Fonts respecte al tipus
// de lletra classic d'Adafruit_GFX).
void centraText(int x, int y, int w, int h, const char *text) {
  int16_t x1, y1; uint16_t tw, th;
  epd.canvas.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
  int cx = x + ((int)w - (int)tw) / 2 - x1;
  int cy = y + ((int)h - (int)th) / 2 - y1;
  epd.canvas.setCursor(cx, cy);
  epd.canvas.print(text);
}

void dibuixa() {
  epd.canvas.fillScreen(CP_BLANC);
  epd.canvas.setTextColor(CP_NEGRE);
  epd.canvas.setTextWrap(false);
  epd.canvas.setTextSize(1);

  epd.canvas.drawBitmap(2, 2, ICONA_CAFETERA, ICONA_W, ICONA_H, CP_NEGRE);

  epd.canvas.setFont(&FreeSansBold12pt7b);
  epd.canvas.setCursor(108, 38);
  epd.canvas.print("RECEPTES DE CAFE");

  epd.canvas.setFont(&FreeSansBold9pt7b);
  if (bleConnectat) epd.canvas.fillCircle(388, 12, 4, CP_NEGRE);
  else              epd.canvas.drawCircle(388, 12, 4, CP_NEGRE);
  if (brut) { epd.canvas.setCursor(370, 18); epd.canvas.print("*"); }

  epd.canvas.drawFastHLine(0, 66, CP_W, CP_NEGRE);

  if (pickerObert) { dibuixaPicker(); return; }

  if (numPresets == 0) {
    epd.canvas.setFont(&FreeSansBold12pt7b);
    centraText(0, 130, CP_W, 24, "Cap cafe desat");
    epd.canvas.setFont(&FreeSansBold9pt7b);
    centraText(0, 170, CP_W, 20, "Afegeix-ne un des del mobil (BLE)");
    dibuixaPeu();
    return;
  }

  // "TIPUS DE CAFE" — etiqueta sense requadre, a sobre de la caixa del nom
  epd.canvas.setFont(&FreeSansBold9pt7b);
  centraText(8, 70, 384, 14, "TIPUS DE CAFE");

  // Caixa NOM — nomes el valor, la caixa es el focus d'aquest camp
  epd.canvas.drawRoundRect(8, 86, 384, 40, 8, CP_NEGRE);
  epd.canvas.setFont(&FreeSansBold18pt7b);
  centraText(8, 86, 384, 40, presets[selIndex].nom);
  if (campActiu == CAMP_NOM) dibuixaFocus(6, 84, 388, 44);

  // Requadre gran que envolta tota la graella GRAMS/MOLTA/NOTES, enganxat
  // per dalt a les capcaleres negres (sense linia superior — "obert" per
  // dalt, nomes vores i cantonada arrodonida per baix).
  requadreObertDalt(6, 132, 388, 84, 12, 8, CP_NEGRE);

  // Caselles GRAMS / MOLTA / NOTES — capcalera negra + valor a sota
  dibuixaCaixaValor(8,   132, 122, 20, 56, "GRAMS", formataNum(presets[selIndex].grams, "g"), campActiu == CAMP_GRAMS);
  dibuixaCaixaValor(139, 132, 122, 20, 56, "MOLTA", formataNum(presets[selIndex].molta, ""),  campActiu == CAMP_MOLTA);
  dibuixaCaixaValor(270, 132, 122, 20, 56, "NOTES", presets[selIndex].notes,                  campActiu == CAMP_NOTES);

  dibuixaPeu();
}

void dibuixaFocus(int x, int y, int w, int h) {
  epd.canvas.drawRoundRect(x, y, w, h, 10, CP_NEGRE);
  epd.canvas.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, CP_NEGRE);
}

// Requadre com drawRoundRect pero sense la vora superior (nomes cantonades
// arrodonides a baix) — per fer-lo servir com a "contenidor" que sembla
// enganxat al que hi ha per sobre (les capcaleres negres). "marge" es quants
// px es deixen sense dibuixar a dalt de les vores laterals, perque no
// sobresurtin per sobre de la cantonada arrodonida de les capcaleres negres
// que hi ha enganxades (aquestes ja fan la seva propia vora ahi).
void requadreObertDalt(int x, int y, int w, int h, int r, int marge, uint16_t color) {
  epd.canvas.drawFastVLine(x,         y + marge, h - marge - r, color);
  epd.canvas.drawFastVLine(x + w - 1, y + marge, h - marge - r, color);
  epd.canvas.drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
  epd.canvas.drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);
  epd.canvas.drawCircleHelper(x + r,         y + h - r - 1, r, 8, color);
}

// Rectangle ple amb nomes les cantonades de dalt arrodonides (les de baix
// es tornen a quadrar) — les capcaleres negres (GRAMS/MOLTA/NOTES) toquen
// per baix amb la caixa del valor, aixi que nomes cal arrodonir per dalt.
void fillArrodonitDalt(int x, int y, int w, int h, int r, uint16_t color) {
  epd.canvas.fillRoundRect(x, y, w, h, r, color);
  epd.canvas.fillRect(x, y + h - r, w, r, color);
}

// Capcalera en negre amb text blanc ("GRAMS"/"MOLTA"/"NOTES") i, a sota, el
// valor real dins d'una caixa blanca. El focus (quan aquest es el camp
// actiu) nomes envolta la caixa del valor, no la capcalera negra — evitar
// invertir una caixa gran sencera (NOTES-CrowPanel42.md #5).
void dibuixaCaixaValor(int x, int yCapcalera, int w, int hCapcalera, int hValor,
                        const char *etiqueta, const char *valor, bool focus) {
  fillArrodonitDalt(x, yCapcalera, w, hCapcalera, 6, CP_NEGRE);
  epd.canvas.setTextColor(CP_BLANC);
  centraText(x, yCapcalera, w, hCapcalera, etiqueta);
  epd.canvas.setTextColor(CP_NEGRE);

  int yValor = yCapcalera + hCapcalera + 2;
  epd.canvas.drawRoundRect(x, yValor, w, hValor, 8, CP_NEGRE);
  bool esNotes = (strcmp(etiqueta, "NOTES") == 0);
  if (esNotes) {
    dibuixaValorNotes(x, yValor, w, hValor, valor);
  } else {
    epd.canvas.setFont(&FreeSansBold18pt7b);
    centraText(x, yValor, w, hValor, valor);
    epd.canvas.setFont(&FreeSansBold9pt7b);
  }
  if (focus) dibuixaFocus(x - 2, yValor - 2, w + 4, hValor + 4);
}

// Les notes poden ser fins a 3 paraules separades per ", " i no sempre
// caben en una sola linia dins dels 122 px de la caixa: si no hi cap,
// les partim en dues linies (la primera es queda amb la meitat de les
// paraules, arrodonint cap amunt) en lloc de tallar-les per la vora.
void dibuixaValorNotes(int x, int y, int w, int h, const char *text) {
  epd.canvas.setFont(&FreeSansBold9pt7b);
  int16_t x1, y1; uint16_t tw, th;
  epd.canvas.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
  if ((int)tw <= w - 6) {
    centraText(x, y, w, h, text);
    return;
  }

  char copia[LEN_NOTES];
  strncpy(copia, text, sizeof(copia) - 1);
  copia[sizeof(copia) - 1] = '\0';

  char *tokens[3];
  int n = 0;
  char *tok = strtok(copia, ",");
  while (tok && n < 3) {
    while (*tok == ' ') tok++;
    tokens[n++] = tok;
    tok = strtok(NULL, ",");
  }

  char linia1[LEN_NOTES] = "";
  char linia2[LEN_NOTES] = "";
  if (n <= 1) {
    strncpy(linia1, text, sizeof(linia1) - 1);
  } else {
    int split = (n + 1) / 2;   // arrodonit cap amunt: 3 notes -> 2 + 1
    for (int i = 0; i < split; i++) {
      if (i > 0) strcat(linia1, ", ");
      strcat(linia1, tokens[i]);
    }
    for (int i = split; i < n; i++) {
      if (i > split) strcat(linia2, ", ");
      strcat(linia2, tokens[i]);
    }
  }

  int hLinia = h / 2;
  centraText(x, y, w, hLinia, linia1);
  if (linia2[0]) centraText(x, y + hLinia, w, hLinia, linia2);
}

void dibuixaPeu() {
  epd.canvas.drawFastHLine(0, 240, CP_W, CP_NEGRE);
  epd.canvas.setFont(&FreeSansBold9pt7b);
  epd.canvas.setCursor(8, 254);
  epd.canvas.print("OK:seg.camp  AMUNT/AVALL:valor");
  epd.canvas.setCursor(8, 270);
  epd.canvas.print("MENU:llista  EXIT:desa canvis");
}

void dibuixaPicker() {
  epd.canvas.setFont(&FreeSansBold9pt7b);
  centraText(0, 76, CP_W, 18, titolPicker());
  epd.canvas.drawFastHLine(0, 96, CP_W, CP_NEGRE);

  const int filaH = 22;
  const int visibles = 6;
  if (pickerSel < pickerScroll) pickerScroll = pickerSel;
  if (pickerSel >= pickerScroll + visibles) pickerScroll = pickerSel - visibles + 1;

  epd.canvas.setFont(&FreeSansBold12pt7b);
  for (int fila = 0; fila < visibles; fila++) {
    int i = pickerScroll + fila;
    if (i >= pickerN) break;
    int y = 100 + fila * filaH;

    char linia[LEN_NOTES + 6];
    if (pickerEsCapcalera[i]) {
      snprintf(linia, sizeof(linia), "%s", pickerEtiquetes[i]);
    } else if (campActiu == CAMP_NOTES) {
      snprintf(linia, sizeof(linia), "%s %s", pickerMarcat[i] ? "[X]" : "[ ]", pickerEtiquetes[i]);
    } else {
      snprintf(linia, sizeof(linia), "%s", pickerEtiquetes[i]);
    }
    epd.canvas.setCursor(16, y + 17);
    epd.canvas.print(linia);
    if (i == pickerSel && !pickerEsCapcalera[i]) epd.canvas.drawRoundRect(6, y, 388, filaH - 2, 4, CP_NEGRE);
  }

  epd.canvas.drawFastHLine(0, 244, CP_W, CP_NEGRE);
  epd.canvas.setFont(&FreeSansBold9pt7b);
  if (campActiu == CAMP_NOTES) {
    epd.canvas.setCursor(8, 258);
    epd.canvas.print("OK:marca/desmarca (max 3)");
    epd.canvas.setCursor(8, 274);
    epd.canvas.print("MENU:aplica seleccio  EXIT:cancela");
  } else {
    epd.canvas.setCursor(8, 258);
    epd.canvas.print("OK:tria aquest valor");
    epd.canvas.setCursor(8, 274);
    epd.canvas.print("EXIT:tanca sense triar");
  }
}
