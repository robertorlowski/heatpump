# Firmware CO — opis wersji bazowej przed zmianami

**Tag dokumentu:** `baseline-firmware-co-2026-09-10`  
**Data wersji bazowej:** `2026-09-10`  
**Data sporządzenia opisu:** `2026-09-20`

## 1. Punkt odniesienia

Ten dokument opisuje firmware znajdujący się w ostatnim zapisanym commicie przed zmianami wykonywanymi w katalogu roboczym:

- commit: `0d466b758b69da4b2a659d49b75e5cf92c9ffb06` (`0d466b7`),
- data commita: `2026-09-10 18:28:16 +0200`,
- opis commita: `fix - wyłączenie obiegu gorącego`,
- analizowane pliki: `co/src/main.cpp`, `co/src/utils.hpp`, `co/src/env.h` i `co/platformio.ini`.

Opis został przygotowany na podstawie `git show HEAD:...`, dlatego nie obejmuje niezapisanych zmian obecnych w katalogu roboczym. Wersję bazową można zawsze odtworzyć poleceniem:

```text
git show 0d466b7:co/src/main.cpp
git show 0d466b7:co/src/utils.hpp
git show 0d466b7:co/src/env.h
```

W commicie `0d466b7` firmware znajdował się w podkatalogu `co/`, dlatego powyższe ścieżki zawierają ten prefiks. Później projekt został przeniesiony do głównego katalogu repozytorium, a obecne odpowiedniki tych plików to `src/...` i `platformio.ini`.

Dokument opisuje rzeczywiste zachowanie kodu, również zachowania wyglądające na błędy. Są one istotne podczas oceny, czy późniejsza refaktoryzacja zmieniła działanie celowo, czy przypadkowo.

## 2. Rola urządzenia

Firmware działa na ESP32 i łączy pięć obszarów odpowiedzialności:

1. komunikację szeregową z pompą ciepła,
2. odczyt danych z instalacji PV,
3. lokalne sterowanie trybem pracy, harmonogramami i przekaźnikami,
4. komunikację z usługą `chpc-web.onrender.com`,
5. lokalny serwer HTTP i prezentację danych na wyświetlaczu TFT.

Kod nie posiadał rozdzielonych warstw. Stan urządzenia, sterowanie, telemetria, HTTP, WebSocket i UART były połączone głównie w `main.cpp` oraz `utils.hpp`.

## 3. Sprzęt i konfiguracja

### 3.1. Platforma

- PlatformIO,
- platforma `espressif32`,
- płytka `esp32dev`,
- framework Arduino,
- monitor portu szeregowego: `9600 baud`.

### 3.2. Piny

| Funkcja | Pin |
|---|---:|
| TFT DC | 12 |
| TFT CS | 13 |
| TFT MOSI | 14 |
| TFT CLK | 27 |
| TFT RST | 0 |
| przekaźnik HP CWU | 25 |
| przekaźnik HP CO | 26 |
| zasilanie `PWR` | 18 |
| przycisk zmiany trybu | 5 |
| OneWire / Dallas | 4 |

`PWR` był ustawiany na `HIGH` podczas uruchomienia. Piny przekaźników były konfigurowane jako wyjścia, ale w `setup()` nie ustawiano im jawnej wartości początkowej.

### 3.3. Identyfikatory komunikacji

- lokalny identyfikator urządzenia: `0x10`,
- identyfikator urządzenia PV: `0x69`,
- polecenia pompy ciepła rozpoczynały się bajtem `0x41`,
- bufor odbiorczy UART miał `1024` bajty.

### 3.4. Sieć

Wariant sieci był wybierany przez `#define BD`. Nazwa sieci i hasło były zapisane bezpośrednio w `env.h`. Po połączeniu firmware synchronizował RTC przez `pl.pool.ntp.org`, używając stałego przesunięcia `+7200` sekund.

## 4. Sekwencja uruchomienia

`setup()` wykonywało kolejno:

1. ustawienie bufora UART i uruchomienie `Serial` z prędkością 9600,
2. uruchomienie I2C, RTC i czujników Dallas,
3. konfigurację przekaźników, `PWR` oraz przycisku zmiany trybu,
4. inicjalizację TFT, Wi-Fi i synchronizację RTC przez NTP,
5. utworzenie pustych obiektów `HP` i `PV` w globalnym `jsonDocument`,
6. uruchomienie lokalnego serwera HTTP,
7. otwarcie przestrzeni NVS `hp` przez `Preferences`,
8. utworzenie brakujących wartości domyślnych,
9. odczyt zapisanego trybu pracy,
10. połączenie WebSocket z `chpc-web.onrender.com/ws`,
11. rozpoczęcie pomiaru temperatur Dallas.

Inicjalizacja Wi-Fi i NTP była blokująca. W przypadku braku Wi-Fi ekran pokazywał błąd i wykonywane było opóźnienie 5 sekund.

## 5. Ustawienia trwałe

Firmware używał `Preferences` w przestrzeni `hp`.

| Klucz NVS | Wartość domyślna |
|---|---:|
| `co_min` | 35°C |
| `co_max` | 45°C |
| `cwu_min` | 40°C |
| `cwu_max` | 47°C |
| `workMode` | `OFF` przy pierwszym uruchomieniu |

Wartości już istniejące w NVS miały pierwszeństwo przed domyślnymi. Operacja otrzymana z serwera mogła aktualizować wszystkie temperatury i `workMode`; wartości były od razu ponownie zapisywane do NVS.

## 6. Główna pętla

Każdy przebieg `loop()` wykonywał:

1. próbę odebrania danych z UART,
2. obsługę WebSocket,
3. obsługę lokalnego serwera HTTP,
4. sprawdzenie fizycznego przycisku zmiany trybu,
5. po upływie okresu odświeżania — pełny cykl sterowania i telemetrii.

Okres pełnego cyklu wynosił:

- 10 sekund, gdy `HP.HPS > 0`,
- 30 sekund w pozostałych przypadkach.

Opóźnienia w komunikacji szeregowej i sieciowej mogły znacząco wydłużyć ten czas.

Pełny cykl wykonywał operacje w następującej kolejności:

1. odczyt czasu RTC,
2. obliczenie lokalnych harmonogramów CO i CWU,
3. wyliczenie lokalnych stanów `co_pomp` i `cwu_pomp`,
4. uzupełnienie `jsonDocument`,
5. obliczenie COP,
6. sterowanie pompą na podstawie telemetrii HP, harmonogramów i trybu,
7. dobór temperatur CO albo CWU,
8. automatyczne wyjście z trybu ręcznego w określonym oknie czasowym,
9. ustawienie lokalnych przekaźników,
10. odświeżenie TFT,
11. wysłanie telemetrii do chmury i wykonanie otrzymanej operacji,
12. rozpoczęcie kolejnego odczytu HP albo PV.

Telemetria wysyłana do serwera zawierała więc przede wszystkim wynik poprzedniego odczytu UART. Nowy odczyt był zlecany dopiero po wysłaniu danych.

## 7. Lokalne harmonogramy

### 7.1. CO

- 23:00–04:00,
- 13:00–14:30.

Obsługiwane były przedziały przechodzące przez północ.

### 7.2. CWU

- 13:00–14:30,
- 04:30–05:30.

### 7.3. Automatyczne wyjście z `MANUAL`

W przedziale 23:00–23:05 tryb `MANUAL` był lokalnie zmieniany na `AUTO`. Jednocześnie wysyłano:

- `SET_HOT_POMP_OFF`,
- `SET_COLD_POMP_OFF`.

Ta automatyczna zmiana nie zapisywała nowego trybu do NVS. Po restarcie urządzenie mogło więc ponownie odczytać poprzednio zapisany `MANUAL`.

W cyklu, w którym następowała zmiana, przekaźniki były ustawiane według wartości wyliczonych jeszcze dla `MANUAL`. Dopiero następny pełny cykl przeliczał je dla `AUTO`.

## 8. Tryby pracy i przekaźniki

| Tryb | `co_pomp` | `cwu_pomp` | Profil temperatury |
|---|---|---|---|
| `OFF` | `false` | `false` | CWU |
| `MANUAL` | `true` | `true` | CO |
| `AUTO` | `schedule_co` | taka sama wartość jak `co_pomp` | CO, gdy harmonogram CO aktywny; poza nim CWU |
| `AUTO_PV` | `schedule_co || pv.pv_power` | taka sama wartość jak `co_pomp` | CO, gdy harmonogram lub PV aktywne; poza tym CWU |
| `CWU` | `false` | `false` | CWU |

Mimo nazw `co_pomp` i `cwu_pomp`, w `MANUAL`, `AUTO` i `AUTO_PV` oba przekaźniki były zwykle ustawiane identycznie.

### 8.1. Obsługa stanu `HP.CO`

Jeśli telemetria zawierała `HP.CO == 1`:

- w `OFF` wysyłano kolejno:
  - `SET_HP_CO_OFF`,
  - `SET_HP_FORCE_OFF`,
  - `SET_HOT_POMP_OFF`,
- w każdym innym trybie `force` ustawiano według lokalnego harmonogramu CWU:
  - aktywny `schedule_cwu` → `SET_HP_FORCE_ON`,
  - nieaktywny `schedule_cwu` → `SET_HP_FORCE_OFF`.

Jeśli `HP.CO` nie było równe `1`, to w każdym trybie innym niż `OFF` wysyłano `SET_HP_CO_ON`.

Oznacza to, że tryb `CWU` również mógł wysłać `SET_HP_CO_ON`, mimo że oba lokalne przekaźniki były w nim wyłączone.

### 8.2. Dobór temperatur

Jeżeli `co_pomp == true` i tryb nie był `CWU`, firmware porównywał `HP.Tmin/Tmax` z ustawieniami CO. Przy różnicy wysyłał:

- `SET_T_SETPOINT_CO = co_max`,
- `SET_T_DELTA_CO = co_max - co_min`.

W przeciwnym przypadku używał ustawień CWU:

- `SET_T_SETPOINT_CO = cwu_max`,
- `SET_T_DELTA_CO = cwu_max - cwu_min`.

Porównanie i ewentualne komendy były wykonywane w każdym pełnym cyklu.

## 9. Fizyczna zmiana trybu

Przycisk na pinie 5 był sprawdzany w każdym przebiegu `loop()`. Po wykryciu stanu wysokiego wykonywano blokujące opóźnienie 1 sekundy i ponowne sprawdzenie.

Kolejność trybów:

```text
MANUAL → AUTO_PV → AUTO → CWU → OFF → MANUAL
```

Nowy tryb był zapisywany do NVS, pokazywany na ekranie i wymuszał wcześniejsze wykonanie kolejnego pełnego cyklu.

## 10. Logika PV i `AUTO_PV`

Próg aktywnej produkcji wynosił `2000 W`.

Odczyt PV był wykonywany co dziesiąty cykl odczytowy. Składał się z dwóch zapytań binarnych. Każda odpowiedź była parsowana jako pięć rekordów PV, a wartości były sumowane.

Po odebraniu drugiej części firmware:

1. sprawdzał poprzednią wartość `pv.pv_power` oraz aktualne `pv.total_power`,
2. w `AUTO_PV` wysyłał `SET_HP_FORCE_ON`, jeśli oba warunki wskazywały produkcję,
3. w pozostałych przypadkach wysyłał `SET_HP_FORCE_OFF`,
4. dopiero potem ustawiał `pv.pv_power = (total_power >= 2000)`.

Skutkiem kolejności było opóźnienie `FORCE ON` o jeden pełny odczyt PV przy przejściu z produkcji poniżej progu na produkcję powyżej progu.

Niezależnie od tego mechanizmu, podczas każdego pełnego cyklu `force` było również ustawiane według `schedule_cwu`, jeżeli `HP.CO == 1`. Obie logiki mogły więc kolejno nadpisywać stan `force`.

## 11. Operacje otrzymywane z serwera

Operacja mogła pochodzić z:

- odpowiedzi `POST /api/hp/add`,
- komunikatu WebSocket `operation`, który uruchamiał kolejny `POST /api/hp/add`,
- lokalnego `POST /api/operation`.

Obsługiwane pola:

| Pole JSON | Zachowanie wersji bazowej |
|---|---|
| `work_mode` | ustawienie `M`, `A`, `PV`, `CWU`; każda inna wartość oznaczała `OFF`; zapis do NVS |
| `co_min` | zapis do NVS, gdy wartość była większa od zera |
| `co_max` | zapis do NVS, zakres większy od zera i nie większy niż 50 |
| `cwu_min` | zapis do NVS, gdy wartość była większa od zera |
| `cwu_max` | zapis do NVS, zakres większy od zera i nie większy niż 50 |
| `sump_heater` | natychmiastowe wysłanie ON albo OFF |
| `cold_pomp` | natychmiastowe wysłanie ON albo OFF |
| `hot_pomp` | natychmiastowe wysłanie ON albo OFF |
| `force` | natychmiastowe wysłanie ON albo OFF |
| `working_watt` | natychmiastowe wysłanie wartości |
| `eev_max_pulse_open` | natychmiastowe wysłanie wartości |
| `eev_setpoint` | natychmiastowe wysłanie wartości |

Pole `co_pomp` nie było obsługiwane przez `operationExecute()`.

Firmware nie przechowywał ostatniej wykonanej operacji. Każde obecne pole powodowało ponowny zapis lub ponowne wysłanie komendy, nawet jeżeli wartość nie uległa zmianie.

Nie wysyłano osobnego potwierdzenia wykonania operacji do serwera. Funkcja lokalnego endpointu zwracała jedynie otrzymany JSON.

Zmiana `work_mode` albo temperatur nie wysyłała natychmiast wszystkich wynikających z niej komend. Dalsze sterowanie odbywało się w następnym pełnym cyklu na podstawie globalnego `workMode`, NVS i telemetrii HP.

## 12. Telemetria i chmura

Docelowy host:

```text
https://chpc-web.onrender.com
```

W każdym pełnym cyklu wykonywano `POST /api/hp/add` z globalnym `jsonDocument`.

Telemetria zawierała między innymi:

- `time`,
- `co_pomp`,
- `cwu_pomp`,
- `pv_power`,
- `schedule_co`,
- `work_mode`,
- `co_min`, `co_max`, `cwu_min`, `cwu_max`,
- zagnieżdżony obiekt `HP`,
- zagnieżdżony obiekt `PV`,
- wyliczone `t_min`, `t_max` i `cop`, jeśli były dostępne.

Obiekt `PV` zawierał `total_power`, `total_prod`, `total_prod_today` i `temperature`. Flaga `pv_power` była wysyłana osobno w korzeniu dokumentu.

Co setny cykl odczytowy firmware:

1. wysyłał lokalne harmonogramy przez wywołanie `putDataToCloud("/settings/set", settings())`,
2. rozłączał WebSocket,
3. czekał 1 sekundę,
4. ponownie inicjalizował WebSocket.

Ponieważ `putDataToCloud()` dodawało prefiks `.../api/`, przekazanie ścieżki zaczynającej się od `/` tworzyło adres z podwójnym ukośnikiem: `/api//settings/set`.

Po połączeniu WebSocket urządzenie wysyłało tekst `ESP32`. Odebranie tekstu `operation` powodowało bezpośrednio w callbacku WebSocket wykonanie `putHpDataToCloud()`.

## 13. Lokalny serwer HTTP

Firmware udostępniał:

| Endpoint | Funkcja |
|---|---|
| `POST /api/operation` | wykonanie operacji JSON |
| `GET /api/settings` | zwrócenie lokalnych harmonogramów CO i CWU |
| `/api/hp` | zwrócenie całego `jsonDocument` |
| `/` | wbudowana aplikacja WWW |
| `/settings` | ta sama wbudowana aplikacja WWW |
| zasoby statyczne | pliki z `web/static_files.h` |

Polecenie przychodzące przez UART do urządzenia `0x10` również mogło pobrać:

- kod `0x01` — telemetrię,
- kod `0x02` — harmonogramy,
- kod `0x03` — brak operacji.

## 14. Protokół wyjściowy pompy

Polecenia pompy miały zwykle postać pięciu bajtów:

```text
0x41, kod, wartość główna, wartość dodatkowa, 0xFF
```

| Operacja | Kod |
|---|---:|
| pobranie danych HP | `0x01` |
| force | `0x03` |
| temperatura zadana | `0x04` |
| delta temperatury | `0x05` |
| EEV setpoint | `0x08` |
| pompa gorąca | `0x09` |
| pompa zimna | `0x0A` |
| grzałka misy | `0x0B` |
| CO | `0x0C` |
| CWU / EEV max pulses zależnie od operacji | `0x0D` |
| working watt | `0x0E` |

Każde `sendRequest()` wykonywało `delay(500)` przed transmisją i `delay(500)` po niej. Kilka pól w jednej operacji mogło zablokować program na wiele sekund.

Zmienna `serialOpertion` przechowywała zarówno ostatnią komendę sterującą, jak i rodzaj oczekiwanej odpowiedzi HP/PV.

## 15. Obliczanie COP

Przy zboczu `HPS: 0 → 1` firmware resetował zapamiętane minimum i maksimum `Ttarget`.

Podczas pracy aktualizował minimalną i maksymalną wartość `Ttarget`. Jeżeli dostępne były `lt_pow` i `lt_hp_on`, wyliczał:

```text
wymiennik = 1.166 × 300 × (t_max - t_min)
cop = wymiennik / lt_pow
```

Wartość `lt_hp_on` była wymagana przez warunek, ale nie była używana we wzorze. Nie było zabezpieczenia przed `lt_pow == 0`.

## 16. Wyświetlacz

TFT prezentował między innymi:

- datę i czas,
- skrót trybu pracy,
- moc PV, produkcję dzienną i temperaturę PV,
- flagę wymuszenia `F`,
- temperaturę docelową,
- zakres temperatur HP, CO i CWU,
- temperatury wymiennika,
- parametry EEV,
- moc,
- stany obiegów gorącego i zimnego.

Zmiana stanu przekaźnika powodowała ponowną inicjalizację TFT i blokujące opóźnienie 1 sekundy.

W funkcji `PrintMode()` przypadek `AUTO` nie miał `break`, dlatego po napisie `AUTO` wykonywała się również część przeznaczona dla `CWU`.

## 17. Znane cechy i problemy wersji bazowej

Poniższe punkty istniały przed refaktoryzacją i nie powinny być traktowane jako nowe regresje:

1. Brak pamiętania ostatniej operacji — identyczne wartości były wysyłane wielokrotnie.
2. Temperatury i tryb były wielokrotnie zapisywane do NVS.
3. `AUTO_PV` używało poprzedniej wartości `pv.pv_power` przed jej aktualizacją.
4. Lokalny harmonogram CWU i logika PV mogły wzajemnie nadpisywać `force`.
5. Jedna zmienna `serialOpertion` łączyła stan zapytania i ostatniej komendy.
6. Długość odebranych danych UART była obliczana, ale nieużywana; kod odczytywał bajty bez wcześniejszego sprawdzenia długości.
7. Bufor przekazywany do `deserializeJson()` nie był jawnie zakończony znakiem `\0` ani przekazywany razem z długością.
8. `sendRequest()`, inicjalizacja sieci, obsługa przycisku, TFT i ponowne łączenie WebSocket używały blokujących `delay()`.
9. Nieznana wartość `work_mode` była interpretowana jako `OFF`.
10. `HTTPClient::end()` było wywoływane przed `getString()`.
11. Callback WebSocket wykonywał blokujące żądanie HTTP.
12. Dane dostępowe Wi-Fi były zapisane w kodzie źródłowym.
13. Brakowało testów automatycznych; katalog `test` zawierał tylko domyślny plik README.
14. Część argumentów sprzętowych i `JsonDocument` była przekazywana przez wartość.
15. `PV::checkJson()` sprawdzało pola typu `long` jako `uint8_t`.

## 18. Checklista porównania po zmianach

Każdy punkt należy później oznaczyć jako: **zachowany**, **celowo zmieniony**, **usunięty** albo **regresja**.

### Sterowanie

- [ ] `OFF` wyłącza CO, force, pompę gorącą oraz lokalne przekaźniki.
- [ ] `MANUAL`, `AUTO`, `AUTO_PV`, `CWU` i `OFF` mają jawnie opisaną semantykę.
- [ ] Wiadomo, czy fizyczny przycisk nadal zmienia tryb.
- [ ] Wiadomo, czy przejście `MANUAL → AUTO` wykonuje urządzenie, czy serwer.
- [ ] Wiadomo, czy lokalne harmonogramy CO/CWU nadal istnieją.
- [ ] `AUTO_PV` reaguje na próg 2000 W i nie wysyła wielokrotnie tej samej komendy.
- [ ] Reguły `OFF` i `AUTO_PV` mają określone pierwszeństwo przed polami operacji serwera.
- [ ] Profil temperatur CO/CWU jest wybierany zgodnie z uzgodnionymi trybami.

### Operacje serwera

- [ ] Wszystkie pola z tabeli operacji mają świadomie zachowaną albo zmienioną obsługę.
- [ ] Operacja częściowa nie usuwa wcześniej zapamiętanych pól.
- [ ] Identyczna wartość nie powoduje ponownego wysłania komendy.
- [ ] Ustalono zachowanie dla nieznanego `work_mode`.
- [ ] Ustalono, czy `/api/operation` nadal może lokalnie sterować urządzeniem.
- [ ] Brak potwierdzenia wykonania do serwera jest świadomą decyzją.

### Stan i trwałość

- [ ] Określono, które ustawienia są tylko w RAM, a które są trwałe.
- [ ] Wartości domyślne CO 35–45°C i CWU 40–47°C są zachowane albo świadomie zmienione.
- [ ] Restart urządzenia ma zdefiniowane zachowanie przed otrzymaniem pierwszej operacji z serwera.

### UART i urządzenia

- [ ] Odczyty HP i PV nie są gubione przez równoczesne komendy sterujące.
- [ ] Zachowano format wszystkich potrzebnych komend szeregowych.
- [ ] Ramki są walidowane pod względem długości.
- [ ] Odczyt PV nadal sumuje oba bloki danych.
- [ ] Komunikacja nie blokuje WebSocket ani lokalnego HTTP przez wiele sekund.

### Chmura i telemetria

- [ ] `POST /api/hp/add` zawiera uzgodniony zestaw pól.
- [ ] WebSocket `operation` nadal powoduje pobranie aktualnej operacji.
- [ ] Puste `{}` z serwera nie kasuje lokalnego stanu.
- [ ] Ustalono, czy harmonogramy są nadal wysyłane przez `/settings/set`.
- [ ] Lokalne endpointy zwracają świadomie zdefiniowane dane.

### Prezentacja i diagnostyka

- [ ] TFT pokazuje tryb, PV, temperatury, EEV, moc i stany obiegów.
- [ ] Obliczenie COP ma zachowaną albo poprawioną definicję.
- [ ] Błędy komunikacji mogą zostać zdiagnozowane bez zmiany sterowania pompą.

## 19. Sposób późniejszego audytu

Zakres zmian kodu można uzyskać poleceniem:

```text
git diff -M 0d466b7 -- co/src co/platformio.ini src platformio.ini
```

Obie pary ścieżek są potrzebne, bo w wersji bazowej kod znajdował się w `co/`; opcja `-M` pokazuje przeniesione pliki jako zmiany nazw zamiast par usunięcie/dodanie.

Audyt powinien składać się z trzech części:

1. porównania strukturalnego plików i interfejsów,
2. przejścia przez checklistę zachowania z sekcji 18,
3. testów scenariuszy `OFF`, wszystkich trybów, progu PV, operacji częściowych i powtórzonych operacji.

Sama zgodność kompilacji nie potwierdza zgodności funkcjonalnej.
