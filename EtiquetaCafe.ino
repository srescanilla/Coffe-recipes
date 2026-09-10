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
                 · GRAMS / MOLTA: suma o resta 0.5 al valor d'aquell cafe.
                 · NOTES: passa a la seguent nota ja feta servir en algun cafe.
    - MENU    -> obre una llista de la casella activa (tots els cafes si
                 estas a NOM; els valors ja usats si estas a GRAMS, MOLTA o
                 NOTES). AMUNT/AVALL mouen la seleccio, OK l'aplica.
    - EXIT    -> dins la llista, la tanca sense triar res. A la pantalla
                 principal, desa qualsevol canvi pendent.

  Els noms de cafe NOUS s'afegeixen des del mobil per Bluetooth (BLE) amb
  control.html — al dispositiu nomes es pot navegar i ajustar cafes que ja
  existeixen. Grams i molta admeten decimals de 0.5 en 0.5.

  Perque nomes es pot afegir un cafe nou des del mobil: amb 5 botons no hi ha
  manera comoda d'escriure text al dispositiu. Vegeu control.html i el README.

  Llibreries necessaries (totes venen amb el paquet de placa ESP32, nomes cal
  instal·lar "Adafruit GFX Library" a mes):
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
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>

// ---------------------------------------------------------------- dades
#define MAX_PRESETS 30
#define LEN_NOM     24
#define LEN_NOTES   40

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
char pickerEtiquetes[MAX_PRESETS][LEN_NOTES];

bool calRedibuixar = true;

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
int    distintsNum(bool esGrams);
int    distintsNotesFn();
void   obrePicker();
int    trobaEtiqueta(const char *valor);
void   aplicaPicker();
void   gestionaPantallaPrincipal(bool up, bool down, bool ok, bool menu, bool exit_);
void   gestionaPicker(bool up, bool down, bool ok, bool exit_);
void   dibuixa();
void   dibuixaFocus(int x, int y, int w, int h);
void   dibuixaCaixaValor(int x, int y, int w, int h, const char *etiqueta, const char *valor, bool focus);
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
    gestionaPicker(up, down, ok, exit_);
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
        if (presets[selIndex].molta < 0)  presets[selIndex].molta = 0;
        if (presets[selIndex].molta > 15) presets[selIndex].molta = 15;
        brut = true;
        break;
      case CAMP_NOTES:
        ciclaNotes(dir);
        break;
    }
    calRedibuixar = true;
  }
}

void gestionaPicker(bool up, bool down, bool ok, bool exit_) {
  if (pickerN == 0) { pickerObert = false; calRedibuixar = true; return; }
  if (up)   { pickerSel = (pickerSel - 1 + pickerN) % pickerN; calRedibuixar = true; }
  if (down) { pickerSel = (pickerSel + 1) % pickerN;           calRedibuixar = true; }
  if (ok)   { aplicaPicker(); pickerObert = false; calRedibuixar = true; }
  if (exit_) { pickerObert = false; calRedibuixar = true; }
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
void obrePicker() {
  pickerN = 0;
  pickerScroll = 0;
  if (campActiu == CAMP_NOM) {
    for (int i = 0; i < numPresets; i++) {
      strncpy(pickerEtiquetes[pickerN], presets[i].nom, LEN_NOTES - 1);
      pickerEtiquetes[pickerN][LEN_NOTES - 1] = '\0';
      pickerN++;
    }
    pickerSel = (selIndex >= 0) ? selIndex : 0;
  } else if (campActiu == CAMP_GRAMS) {
    pickerN = distintsNum(true);
    pickerSel = trobaEtiqueta(formataNum(presets[selIndex].grams, "g"));
  } else if (campActiu == CAMP_MOLTA) {
    pickerN = distintsNum(false);
    pickerSel = trobaEtiqueta(formataNum(presets[selIndex].molta, ""));
  } else if (campActiu == CAMP_NOTES) {
    pickerN = distintsNotesFn();
    pickerSel = trobaEtiqueta(presets[selIndex].notes);
  }
  if (pickerSel < 0) pickerSel = 0;
}

int trobaEtiqueta(const char *valor) {
  for (int i = 0; i < pickerN; i++) if (strcmp(pickerEtiquetes[i], valor) == 0) return i;
  return 0;
}

void aplicaPicker() {
  if (campActiu == CAMP_NOM) {
    if (brut) desaNVS();
    selIndex = pickerSel;
    prefs.putString("sel", presets[selIndex].nom);
  } else if (campActiu == CAMP_GRAMS) {
    presets[selIndex].grams = atof(pickerEtiquetes[pickerSel]);
    brut = true;
  } else if (campActiu == CAMP_MOLTA) {
    presets[selIndex].molta = atof(pickerEtiquetes[pickerSel]);
    brut = true;
  } else if (campActiu == CAMP_NOTES) {
    strncpy(presets[selIndex].notes, pickerEtiquetes[pickerSel], LEN_NOTES - 1);
    presets[selIndex].notes[LEN_NOTES - 1] = '\0';
    brut = true;
  }
}

int distintsNum(bool esGrams) {
  float vals[MAX_PRESETS];
  int n = 0;
  for (int i = 0; i < numPresets; i++) {
    float v = esGrams ? presets[i].grams : presets[i].molta;
    bool trobat = false;
    for (int k = 0; k < n; k++) if (fabs(vals[k] - v) < 0.01f) { trobat = true; break; }
    if (!trobat) vals[n++] = v;
  }
  for (int i = 1; i < n; i++) {
    float tmp = vals[i]; int j = i - 1;
    while (j >= 0 && vals[j] > tmp) { vals[j + 1] = vals[j]; j--; }
    vals[j + 1] = tmp;
  }
  for (int i = 0; i < n; i++) {
    const char *s = formataNum(vals[i], esGrams ? "g" : "");
    strncpy(pickerEtiquetes[i], s, LEN_NOTES - 1);
    pickerEtiquetes[i][LEN_NOTES - 1] = '\0';
  }
  return n;
}

int distintsNotesFn() {
  int n = 0;
  for (int i = 0; i < numPresets; i++) {
    if (strlen(presets[i].notes) == 0) continue;
    bool trobat = false;
    for (int k = 0; k < n; k++) if (strcmp(pickerEtiquetes[k], presets[i].notes) == 0) { trobat = true; break; }
    if (!trobat) {
      strncpy(pickerEtiquetes[n], presets[i].notes, LEN_NOTES - 1);
      pickerEtiquetes[n][LEN_NOTES - 1] = '\0';
      n++;
    }
  }
  for (int i = 1; i < n; i++) {
    char tmp[LEN_NOTES]; strcpy(tmp, pickerEtiquetes[i]);
    int j = i - 1;
    while (j >= 0 && strcasecmp(pickerEtiquetes[j], tmp) > 0) { strcpy(pickerEtiquetes[j + 1], pickerEtiquetes[j]); j--; }
    strcpy(pickerEtiquetes[j + 1], tmp);
  }
  return n;
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
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVEI_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
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
void dibuixa() {
  epd.canvas.fillScreen(CP_BLANC);
  epd.canvas.setTextColor(CP_NEGRE);

  epd.canvas.setTextSize(2);
  epd.canvas.setCursor(8, 6);
  epd.canvas.print("RECEPTES DE CAFE");
  epd.canvas.drawFastHLine(0, 26, CP_W, CP_NEGRE);

  if (bleConnectat) epd.canvas.fillCircle(388, 10, 4, CP_NEGRE);
  else              epd.canvas.drawCircle(388, 10, 4, CP_NEGRE);
  if (brut) {
    epd.canvas.setTextSize(1);
    epd.canvas.setCursor(368, 6);
    epd.canvas.print("*");
  }

  if (pickerObert) { dibuixaPicker(); return; }

  if (numPresets == 0) {
    epd.canvas.setTextSize(2);
    epd.canvas.setCursor(30, 100);
    epd.canvas.print("Cap cafe desat");
    epd.canvas.setTextSize(1);
    epd.canvas.setCursor(30, 130);
    epd.canvas.print("Afegeix-ne un des del mobil (BLE)");
    dibuixaPeu();
    return;
  }

  // Caixa NOM
  epd.canvas.drawRect(8, 34, 384, 66, CP_NEGRE);
  epd.canvas.setTextSize(1);
  epd.canvas.setCursor(14, 40);
  epd.canvas.print("TIPUS DE CAFE");
  epd.canvas.setTextSize(3);
  epd.canvas.setCursor(14, 64);
  epd.canvas.print(presets[selIndex].nom);
  if (campActiu == CAMP_NOM) dibuixaFocus(6, 32, 388, 70);

  // Caixes GRAMS / MOLTA / NOTES
  dibuixaCaixaValor(8,   112, 122, 130, "GRAMS",      formataNum(presets[selIndex].grams, "g"), campActiu == CAMP_GRAMS);
  dibuixaCaixaValor(139, 112, 122, 130, "NUM. MOLTA",  formataNum(presets[selIndex].molta, ""),  campActiu == CAMP_MOLTA);
  dibuixaCaixaValor(270, 112, 122, 130, "NOTES",       presets[selIndex].notes,                  campActiu == CAMP_NOTES);

  dibuixaPeu();
}

void dibuixaFocus(int x, int y, int w, int h) {
  epd.canvas.drawRect(x, y, w, h, CP_NEGRE);
  epd.canvas.drawRect(x + 1, y + 1, w - 2, h - 2, CP_NEGRE);
}

void dibuixaCaixaValor(int x, int y, int w, int h, const char *etiqueta, const char *valor, bool focus) {
  epd.canvas.drawRect(x, y, w, h, CP_NEGRE);
  epd.canvas.setTextSize(1);
  epd.canvas.setCursor(x + 6, y + 6);
  epd.canvas.print(etiqueta);
  bool esNotes = (strcmp(etiqueta, "NOTES") == 0);
  epd.canvas.setTextSize(esNotes ? 1 : 3);
  epd.canvas.setCursor(x + 6, y + 34);
  epd.canvas.print(valor);
  if (focus) dibuixaFocus(x - 2, y - 2, w + 4, h + 4);
}

void dibuixaPeu() {
  epd.canvas.drawFastHLine(0, 250, CP_W, CP_NEGRE);
  epd.canvas.setTextSize(1);
  epd.canvas.setCursor(8, 256);
  epd.canvas.print("OK:seg.camp  AMUNT/AVALL:valor");
  epd.canvas.setCursor(8, 272);
  epd.canvas.print("MENU:llista  EXIT:desa canvis");
}

void dibuixaPicker() {
  epd.canvas.setTextSize(1);
  epd.canvas.setCursor(8, 34);
  const char *titol = "TIPUS DE CAFE";
  if (campActiu == CAMP_GRAMS)      titol = "GRAMS (valors ja usats)";
  else if (campActiu == CAMP_MOLTA) titol = "NUM. MOLTA (valors ja usats)";
  else if (campActiu == CAMP_NOTES) titol = "NOTES (valors ja usats)";
  epd.canvas.print(titol);
  epd.canvas.drawFastHLine(0, 46, CP_W, CP_NEGRE);

  const int filaH = 24;
  const int visibles = 8;
  if (pickerSel < pickerScroll) pickerScroll = pickerSel;
  if (pickerSel >= pickerScroll + visibles) pickerScroll = pickerSel - visibles + 1;

  for (int fila = 0; fila < visibles; fila++) {
    int i = pickerScroll + fila;
    if (i >= pickerN) break;
    int y = 50 + fila * filaH;
    epd.canvas.setTextSize(2);
    epd.canvas.setCursor(14, y + 4);
    epd.canvas.print(pickerEtiquetes[i]);
    if (i == pickerSel) epd.canvas.drawRect(6, y, 388, filaH - 2, CP_NEGRE);
  }

  epd.canvas.drawFastHLine(0, 250, CP_W, CP_NEGRE);
  epd.canvas.setTextSize(1);
  epd.canvas.setCursor(8, 256);
  epd.canvas.print("OK:tria aquest valor");
  epd.canvas.setCursor(8, 272);
  epd.canvas.print("EXIT:tanca sense triar");
}
