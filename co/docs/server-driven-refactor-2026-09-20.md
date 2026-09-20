# Refaktoryzacja sterowania z serwera — 2026-09-20

Dokument opisuje stan aplikacji po zmianach wykonanych względem wersji bazowej
`0d466b7` z 2026-09-10. Opis stanu sprzed zmian znajduje się w
`docs/baseline-before-server-driven-refactor.md`.

## 1. Cel

Firmware ma wykonywać ustawienia otrzymane z serwera, pamiętać ich ostatni
stan w RAM i nie wysyłać ponownie komendy, jeżeli jej efektywna wartość się
nie zmieniła. Wyjątkami są reguły bezpieczeństwa trybu `OFF` i lokalna reguła
`AUTO_PV`.

Urządzenie nie potwierdza serwerowi wykonania operacji. Za zastosowanie zmiany
uznaje się poprawne dodanie komendy do lokalnej kolejki UART albo zmianę stanu
lokalnego przekaźnika.

## 2. Podział odpowiedzialności

### `CloudClient`

- utrzymuje połączenie WebSocket,
- po komunikacie `operation` zgłasza potrzebę wykonania `POST /api/hp/add`,
- realizuje żądania HTTP i zwraca treść odpowiedzi,
- nie wykonuje żądania HTTP bezpośrednio z callbacku WebSocket.

### `OperationController`

- przechowuje pełny, ostatni stan ustawień otrzymanych z serwera,
- scala częściowe operacje bez kasowania pominiętych pól,
- przechowuje ostatnie wartości skierowane do pompy,
- wylicza efektywny stan z uwzględnieniem `OFF` i `AUTO_PV`,
- dodaje do kolejki tylko zmienione komendy,
- steruje logicznym stanem lokalnych przekaźników,
- przechowuje cały obiekt `HpPreferences` w RAM.

### `SerialBus`

- koduje komendy protokołu pompy i PV,
- scala oczekujące komendy dotyczące tego samego ustawienia,
- dzieli kolejkę na bezpieczeństwo, kontynuację odczytu PV i zwykłe komendy,
- pilnuje odstępu 500 ms między transmisjami bez blokującego `delay(500)`,
- rozróżnia oczekiwanie na HP, pierwszy blok PV i drugi blok PV,
- po 3 sekundach zwalnia oczekiwanie na brakującą odpowiedź,
- składa ramkę do przerwy między bajtami i sprawdza CRC odpowiedzi PV,
- udostępnia liczniki przepełnień i timeoutów do telemetrii.

### `OperationParser`

- jest jedynym miejscem zamieniającym JSON operacji na struktury domenowe,
- akceptuje jawne wartości logiczne i liczbowe,
- odrzuca nieprawidłowe typy oraz wartości poza zakresem protokołu,
- zlicza odrzucone pola w telemetrii.

### `main.cpp`

- orkiestruje cykl aplikacji,
- zamienia JSON operacji na `ServerOperationState`,
- przekazuje stan do kontrolera,
- zbiera telemetrię, aktualizuje TFT i publikuje dane w chmurze.

### `utils.cpp`

- zawiera implementację prezentacji, inicjalizacji i konwersji JSON,
- `device_io.hpp` udostępnia wyłącznie deklaracje.

## 3. Przepływ operacji

```text
odpowiedź serwera JSON
        |
        v
ServerOperationState (patch)
        |
        v
scalenie z zapamiętanym stanem serwera
        |
        v
wyliczenie efektywnego stanu przez OperationController
        |
        +--> zmienione przekaźniki lokalne
        |
        +--> zmienione komendy w kolejce SerialBus
                         |
                         v
                       UART
```

Pusty obiekt `{}` kończy przetwarzanie i nie modyfikuje stanu. Nieznany
`work_mode` jest zliczany jako błąd i pomijany; nie przełącza pompy na `OFF`.

## 4. Stan serwera i porównywanie

`ServerOperationState` obsługuje pola:

- `work_mode`,
- `co_min`, `co_max`, `cwu_min`, `cwu_max`,
- `co_pomp`,
- `sump_heater`, `cold_pomp`, `hot_pomp`,
- `force`,
- `working_watt`, `eev_max_pulse_open`, `eev_setpoint`.

Każde pole ma znacznik `present`. Dzięki temu brak pola w częściowej operacji
różni się od jawnego ustawienia `false` albo `0`.

Kontroler nie usuwa wartości żądanej przez serwer. Oddzielnie przechowuje
ostatnią wartość skierowaną do pompy. Pozwala to jednocześnie:

- nie wykonywać drugi raz tej samej komendy,
- zachować pełny stan serwera,
- czasowo nadpisać `force` przez `OFF` lub `AUTO_PV`,
- przywrócić wartość serwera po wyjściu z trybu nadrzędnego.

## 5. Preferencje

`HpPreferences` jest jednym obiektem w RAM:

| Pole | Wartość domyślna |
|---|---:|
| `workMode` | `OFF` |
| `coMin` | 35°C |
| `coMax` | 45°C |
| `cwuMin` | 40°C |
| `cwuMax` | 47°C |

Nie ma odczytu, zapisu ani migracji NVS. Operacja serwera tworzy kopię całego
obiektu, zmienia obecne pola i przypisuje kompletny obiekt jedną operacją.
Temperatury z serwera są zaokrąglane do wartości całkowitych.

Po restarcie obowiązują wartości domyślne do czasu odebrania ustawień z
serwera.

## 6. Semantyka trybów

| Tryb | HP CO | Profil temperatur | Lokalne przekaźniki | `force` |
|---|---|---|---|---|
| `MANUAL` | ON | CO | CO aktywny, chyba że serwer poda `co_pomp` | z serwera |
| `AUTO` | ON | CO | CO aktywny, chyba że serwer poda `co_pomp` | z serwera |
| `AUTO_PV` | ON | CO | CO aktywny, chyba że serwer poda `co_pomp` | z produkcji PV |
| `CWU` | ON | CWU | wyłączone | z serwera |
| `OFF` | OFF | nie jest wysyłany | wyłączone | zawsze OFF |

Lokalne harmonogramy CO/CWU zostały usunięte. Przycisk na pinie 5 przełącza
niezależny tryb sterownika:
`OFF -> CLOUD -> MANUAL_CO -> MANUAL_CWU -> OFF`.

- `OFF` lokalnie wyłącza pompę i przekaźniki, ale nie zmienia `work_mode`
  otrzymanego z chmury.
- `CLOUD` stosuje sterowanie i ustawienia z serwera.
- `MANUAL_CO` nie wysyła komend sterujących do pompy, włącza lokalny
  przekaźnik CO, wyłącza przekaźnik CWU i pozostawia aktywne odczyty oraz
  telemetrię.
- `MANUAL_CWU` nie wysyła komend sterujących do pompy, wyłącza lokalny
  przekaźnik CO, włącza przekaźnik CWU i pozostawia aktywne odczyty oraz
  telemetrię.

Pole `controller_mode` w telemetrii odróżnia lokalny stan sterownika od
serwerowego `work_mode`.

### `OFF`

Przy wejściu w `OFF` kontroler umieszcza w klasie bezpieczeństwa sekwencję:

1. `SET_HP_CO_OFF`,
2. `SET_HP_FORCE_OFF`,
3. `SET_HOT_POMP_OFF`,
4. `SET_COLD_POMP_OFF`.

Następnie ustawia lokalne przekaźniki na `false`. Pola `force=true` i
`hot_pomp=true` lub `cold_pomp=true` z operacji serwera nie mogą nadpisać
reguły `OFF`. Grzałka miski pozostaje sterowana przez serwer.

Powtórzenie tej samej operacji `OFF` nie wysyła sekwencji ponownie.

### `AUTO_PV`

W `AUTO_PV` efektywne `force` wynosi:

```text
pv.pv_power && pv.total_power >= 2000 W
```

Komenda ON/OFF jest wysyłana tylko przy zmianie wyniku warunku. Pole `force`
serwera pozostaje zapamiętane, lecz w tym trybie ma niższy priorytet. Po
wyjściu z `AUTO_PV` ponownie obowiązuje wartość serwera.

## 7. UART i odczyty

Stara zmienna łącząca typ ostatniej komendy z oczekiwaną odpowiedzią została
usunięta. Komendy ustawiające nie zmieniają stanu oczekiwanego odczytu.
Oczekujące komendy tego samego typu są zastępowane najnowszą wartością.
Nieprzyjęta komenda jest ponawiana po zwolnieniu miejsca w kolejce.

Odbiór nie używa opóźnienia pomiędzy bajtami. Dane są gromadzone w buforze
2048 B do wykrycia przerwy międzyramkowej. Kod sprawdza minimalną długość i CRC
ramki PV. Odczyt HP używa wariantu `deserializeJson` z długością bufora.

## 8. Chmura

- `POST /api/hp/add` nadal wysyła telemetrię i pobiera obiekt `operation`.
- Komunikat WebSocket `operation` powoduje wcześniejsze wykonanie tego POST-a.
- Lokalny serwer HTTP, wszystkie lokalne endpointy API, interfejs WWW oraz
  osadzone pliki statyczne zostały usunięte z firmware.
- Dane harmonogramu nie są już wysyłane, ponieważ harmonogramów lokalnych nie
  ma.

## 9. Dodatkowe poprawki techniczne

- naprawiono brak `break` po trybie `AUTO` na ekranie,
- odpowiedź HTTP jest pobierana przed `HTTPClient::end()`,
- callback WebSocket nie wykonuje blokującego POST-a,
- usunięto nieużywane zależności AsyncTCP, ESPAsyncWebServer,
  DallasTemperature i OneWire,
- estymator COP kończy obliczenie dopiero po zatrzymaniu pompy i pomija
  dzielenie przez zero,
- parser PV używa bajtów bez znaku przy składaniu liczb,
- usunięto sekundowe opóźnienie po zmianie lokalnego przekaźnika,
- po zmianie przekaźników TFT jest ponownie odrysowywany,
- Wi-Fi ma automatyczne ponowne łączenie, a NTP jest ponawiane co 5 minut po
  błędzie i odświeżane co 6 godzin po sukcesie,
- offset CET/CEST jest wyliczany według dat zmiany czasu,
- dane Wi-Fi znajdują się w ignorowanym `src/secrets.h`; repozytorium zawiera
  jedynie `src/secrets.example.h`.

### Estymacja COP zbiornika

Zbiornik ma pojemność 300 litrów. `THO` reprezentuje temperaturę góry,
`Ttarget` temperaturę środka, a temperatura dołu nie jest mierzona. Jej wartość
początkowa jest szacowana jako najniższe `THO` z pierwszych 60 sekund cyklu.

Średnia temperatura zbiornika jest przybliżana metodą trapezów:

```text
Tavg = (THO + 2 × Ttarget + Tbottom) / 4
Q = 1,163 × 300 × (Tavg_end - Tavg_start) Wh
```

Po zboczu `HPS: 1 -> 0` wyznaczany jest zakres:

- `cop_min`: dolna warstwa nie ogrzała się podczas cyklu,
- `cop_max`: dolna warstwa osiągnęła końcową temperaturę środka,
- `cop`: środek zakresu zachowany dla zgodności z istniejącym API.

Pola są zamrażane po zakończeniu cyklu i nie zmieniają się podczas postoju.
Jest to estymacja energii zgromadzonej w zbiorniku, a nie pomiar przepływowy
rzeczywistego COP pompy.

## 10. Weryfikacja

Dodano test `test/test_operation_controller/test_main.cpp`, który pokrywa:

1. brak ponownego wysłania identycznej operacji,
2. brak działania dla pustej albo nierozpoznanej operacji,
3. wysłanie tylko zmienionego pola,
4. pierwszeństwo i jednorazowość sekwencji `OFF`,
5. przejścia progu `AUTO_PV`,
6. zachowanie pominiętych pól operacji częściowej,
7. wspólny stan obu lokalnych przekaźników,
8. odrzucenie nieprawidłowego zakresu temperatur,
9. ponawianie komendy odrzuconej przez kolejkę,
10. akceptowanie i odrzucanie typowanych wartości parsera operacji,
11. wyłączenie lokalnych przekaźników po przejściu do `CWU`,
12. obliczenie zakresu COP dopiero po zakończeniu cyklu,
13. ograniczenie estymacji temperatury dołu do pierwszych 60 sekund cyklu.

Pełny testowy firmware dla ESP32 kompiluje się poleceniem:

```text
pio test -e esp32dev -f test_operation_controller --without-uploading --without-testing
```

Polecenie dla ESP32 nie wykonuje asercji — ich uruchomienie wymaga wgrania
testowego firmware na płytkę. Produkcyjny firmware jest weryfikowany
poleceniem `pio run`.

## 11. Świadomie pozostawione kwestie

- Potwierdzenie fizycznego wykonania komendy przez pompę nie jest wymagane.
  Stan uznaje się za zastosowany po przyjęciu komendy do kolejki UART.
- Pełna weryfikacja protokołu wymaga testu na pompie i falowniku PV.
- Serwer zachowa dotychczasowe pole `cop`. Aby archiwizować również zakres,
  jego model danych trzeba rozszerzyć o `cop_min`, `cop_max` oraz
  `cop_bottom_start`.
- HTTP i WebSocket wymagają osobnego wdrożenia z weryfikacją łańcucha TLS;
  obecna wersja biblioteki bez przekazanego CA używa połączenia bez weryfikacji.
