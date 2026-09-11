# Etiqueta de cafè — CrowPanel 4,2"

[Suposant] Disseny fet a partir de la plantilla que vas passar i de les
decisions que vam acordar (nom nou de cafè per BLE des del mòbil, grams/molta
amb decimals de 0.5, navegació OK=camp següent / AMUNT-AVALL=valor /
MENU=llista). No l'he pogut provar en maquinari real — és un primer muntatge
que caldrà polir un cop el vegis a la pantalla.

## Fitxers

- `EtiquetaCafe/EtiquetaCafe.ino` + `EtiquetaCafe/CrowPanel42.h` — el sketch,
  ja amb el header del projecte a dins de la carpeta (tal com demana l'Arduino
  IDE, un `.h` al costat del `.ino`).
- `control.html` — pàgina de control per Bluetooth des del mòbil, per afegir,
  editar, reanomenar i esborrar cafès.

## Com funciona a la pantalla

Quatre caselles: **TIPUS DE CAFÈ, GRAMS, NÚM. MOLTA, NOTES**.

- **OK** — passa a la casella següent i desa a la memòria (NVS) qualsevol
  canvi pendent de la que deixes enrere.
- **AMUNT / AVALL** — canvien el valor de la casella activa:
  - a TIPUS DE CAFÈ, passen al cafè següent/anterior (ordre alfabètic) i
    carreguen automàticament els seus grams, molta i notes;
  - a GRAMS (0-60) i MOLTA (0-100), sumen/resten 0.5 al cafè seleccionat;
  - a NOTES, passen a la següent nota ja feta servir en algun altre cafè.
- **MENU** — obre una llista de la casella activa:
  - TIPUS DE CAFÈ: tots els cafès desats.
  - GRAMS: valors estàndard de 15 a 38 g, sense decimals.
  - MOLTA: valors estàndard agrupats en dos blocs — ESPRESSO (10-25) i V60
    (55-80) —, sense decimals.
  - NOTES: una llista curada de notes de tast habituals. **Aquesta és
    l'única llista de selecció múltiple**: OK marca/desmarca una nota (fins
    a 3 alhora) i MENU aplica el que hagis marcat; EXIT cancel·la sense
    aplicar res.
  A NOM/GRAMS/MOLTA, OK tria l'entrada ressaltada i tanca la llista de seguida.
- **EXIT** — dins la llista, la tanca sense triar res; a la pantalla
  principal, desa qualsevol canvi pendent (i és l'acció "desar" explícita).

Un punt petit a dalt a la dreta indica si hi ha un mòbil connectat per BLE; un
asterisc hi apareix quan hi ha un canvi sense desar encara.

[Suposant] Vaig ampliar el rang de MOLTA de 0-15 a 0-100 perquè m'has dit que
els números de molta reals van d'espresso (10-25) a V60 (55-80) — el rang
petit d'abans era una suposició equivocada meva sobre com és la roda del
molinet.

## Per què els cafès nous s'afegeixen des del mòbil

Amb només 5 botons (AMUNT, AVALL, OK, MENU, EXIT) no hi ha manera còmoda
d'escriure un nom nou al dispositiu. Vam decidir fer-ho des de `control.html`
per Bluetooth, seguint el mateix esquema que ja vas fer servir al projecte de
la claqueta ("Etiqueta Camera"): pantalla per preconfigurar des del mòbil,
dispositiu per navegar durant l'ús real sense necessitat del mòbil.

Al dispositiu només pots **navegar i ajustar** cafès que ja existeixen
(grams, molta, notes ja usades). Per crear-ne un de nou, reanomenar-lo o
esborrar-lo, cal `control.html`.

## Publicar `control.html`

Web Bluetooth **només funciona per HTTPS** (o `localhost`) — obrir el fitxer
directament des de la carpeta de descàrregues del mòbil no connectarà mai.
Si ja tens el repositori de GitHub Pages del projecte de la claqueta, la
manera més ràpida és penjar-hi aquest fitxer també (com a pàgina nova o
substituint-la); si no, un repositori públic nou amb GitHub Pages activat
n'hi ha prou.

**Safari a l'iPhone no suporta Web Bluetooth en cap versió** — cal l'app
Bluefy. A Android, Chrome funciona directament.

## Protocol BLE

Nom anunciat: `EtiquetaCafe`. Servei i característiques (UUID propis,
definits tant a `EtiquetaCafe.ino` com a `control.html` — si en canvies un,
canvia'l als dos fitxers):

```
Servei:            a1e8f350-5b3e-4c2c-9a1a-2f7f6a9d1001
RX (mòbil->placa): a1e8f350-5b3e-4c2c-9a1a-2f7f6a9d1002  (escriptura)
TX (placa->mòbil): a1e8f350-5b3e-4c2c-9a1a-2f7f6a9d1003  (notificació)
```

Línies de text acabades en `\n`, cada una partida en trossos de com a molt
18 bytes en totes dues direccions — no depenem que el mòbil negociï un MTU
gran, seguint la lliçó apresa al projecte anterior (NOTES-CrowPanel42.md
§9).

| Comanda (mòbil→placa)         | Resposta (placa→mòbil)                         |
|---|---|
| `ADD\|nom\|grams\|molta\|notes` | `OK\|ADD\|nom` o `ERR\|ADD\|FORMAT\|NOM_BUIT\|PLE` |
| `DEL\|nom`                     | `OK\|DEL\|nom` o `ERR\|DEL\|NOTFOUND`           |
| `REN\|nomVell\|nomNou`         | `OK\|REN\|nomNou` o `ERR\|REN\|NOTFOUND\|DUP`   |
| `SYNC`                         | `CNT\|n`, després n línies `ITEM\|...`, després `DONE\|SYNC` |
| `PING`                         | `PONG\|1.0`                                     |

`ADD` amb un nom que ja existeix actualitza els seus valors (no en crea un
duplicat) — així `control.html` fa servir la mateixa comanda per crear i per
editar.

Els accents que escriguis al mòbil (à, é, ç, ñ...) es converteixen a ASCII
pla a la placa abans de desar-los, perquè els tipus de lletra de la pantalla
només cobreixen els caràcters 32–126 (la mateixa limitació que ja vas
documentar a `NOTES-CrowPanel42.md` §7). Si vols veure exactament com
quedarà un nom, pensa'l ja sense accents.

## Emmagatzematge

Es desa a la memòria NVS de l'ESP32 amb `Preferences`, namespace `cafe`, fins
a `MAX_PRESETS = 30` cafès (`nom` 23 car., `notes` 39 car., `grams`/`molta`
amb decimals). El "cafè seleccionat" es recorda pel seu **nom**, no per la
posició a la llista, així que afegir o esborrar cafès des del mòbil no fa
saltar la pantalla a un cafè diferent del que tenies triat sense voler-ho.

## Configuració obligatòria a l'Arduino IDE

La mateixa taula de `NOTES-CrowPanel42.md`, amb un afegit important: com que
aquest sketch fa servir Bluetooth, **"Partition Scheme" ha de ser "Huge APP
(3MB No OTA/1MB SPIFFS)"** — amb l'esquema petit per defecte et trobaràs
"Sketch too big" en compilar.

## Disseny visual (cantonades arrodonides, text centrat, tipografia, icona)

- **Cantonades arrodonides**: totes les caixes fan servir `drawRoundRect` en
  lloc de `drawRect`.
- **Text centrat**: cada etiqueta i cada valor es calcula amb
  `getTextBounds` i es centra dins la seva caixa (funció `centraText`),
  tant en horitzontal com en vertical.
- **Tipografia**: en lloc del tipus de lletra per defecte d'Adafruit GFX
  (el 5x7 clàssic, bàsic i escalat), faig servir els **Free Fonts** que ja
  porta inclosos Adafruit GFX Library (no cal instal·lar res més):
  `FreeSansBold9pt7b` per a etiquetes i peus de pàgina, `FreeSansBold12pt7b`
  per al títol, `FreeSansBold18pt7b` per als valors grans (nom del cafè,
  grams, molta). Són sans-serif negreta, molt més llegibles a la distància
  que el 5x7. Si en vols un altre estil, Adafruit GFX també porta
  `FreeSerifBold` (amb serifs, més "de diari") i `FreeMonoBold` (monoespai,
  tot alineat); és tan senzill com canviar el `#include` i el nom del tipus
  de lletra a cada `setFont(...)`.
- **Icona de la cafetera**: [Suposant] refeta a partir de la foto que em vas
  passar de la teva màquina real, a 96x60 píxels (abans era una genèrica de
  72x48), empaquetada com a mapa de bits d'1 bit dins del mateix `.ino`
  (`ICONA_CAFETERA`). Manté: la pantalla tàctil inclinada amb graella (2
  línies verticals + 1 horitzontal, sense el text "Kafmasino" tal com vas
  demanar), el cos amb 3 botons rodons, el grup/broquet amb el seu tub cap
  avall, i la safata amb 3 càpsules a sobre. **Simplificacions per les
  limitacions de 96x60 a 1 bit**: la inclinació de la pantalla és un
  quadrilàter ajustat a mà (no una rotació real, perquè a aquesta mida girar
  de veritat generava un contorn facetat/poc net), els botons són cercles
  simples sense relleu ni etiquetes, i el tub del broquet és una línia recta
  amb un cercle al final en lloc d'un grup detallat. No l'he vist renderitzada
  al maquinari real — un cop la vegis a la pantalla física és fàcil retocar
  qualsevol proporció que no acabi de quadrar.

## El que penso que caldrà retocar un cop ho vegis a la pantalla

[Suposant] No he pogut renderitzar-ho de veritat, així que hi ha coses que
probablement necessitaran un ajust fi:

- Mides i posicions exactes de les caixes i de la icona (calculades sobre
  paper per a 400x300 px).
- La caixa de NOTES fa servir sempre la lletra petita (9pt) perquè hi pot
  haver fins a 3 notes juntes ("Xocolata, Nabius, Mel"); amb 3 notes llargues
  pot arribar a no caber tota dins dels 122 px d'ample de la caixa.
- No hi ha ajust de línia (word-wrap): un nom o una combinació de notes massa
  llargs es tallaran per la vora de la caixa en lloc de saltar de línia.
