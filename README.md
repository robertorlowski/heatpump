# co — sterownik pompy ciepła (ESP32)

Firmware sterownika, który łączy pompę ciepła CHPC z chmurą `chpc-web`.
Sterownik odczytuje pompę i instalację fotowoltaiczną po RS-485, wysyła
telemetrię do chmury i wykonuje ustawienia, które z niej otrzymuje.

```text
pompa CHPC (0x41) ───┐
                     ├── RS-485 ── co (ESP32) ── Wi-Fi / HTTPS ── chpc-web ── przeglądarka
DTU Hoymiles (0x69) ─┘                 │
                                       ├── przekaźniki CO / CWU
                                       ├── wyświetlacz ST7735, RTC DS3231
                                       └── przycisk trybu
```

Powiązane repozytoria:

| Repozytorium | Rola |
|---|---|
| [robertorlowski/heatpomp](https://github.com/robertorlowski/heatpomp) (to repo) | firmware `co` na ESP32 |
| [robertorlowski/chpc](https://github.com/robertorlowski/chpc) | firmware pompy CHPC (Arduino Pro Mini) oraz testy E2E całego łańcucha |
| [robertorlowski/chpc-web](https://github.com/robertorlowski/chpc-web) | serwer i klient WWW, harmonogramy, historia telemetrii |

Testy E2E z `chpc` szukają tego repozytorium w katalogu `../heatpump` obok
`chpc` (inną ścieżkę podaje się w zmiennej `CO_DIR`), więc klonuj je poleceniem
`git clone https://github.com/robertorlowski/heatpomp.git heatpump`.

## Co robi sterownik

- **Odczyty.** Co 10 s, gdy sprężarka pracuje, i co 30 s w spoczynku, odpytuje
  pompę (JSON), a co dziesiąty cykl DTU Hoymiles (Modbus, dwa zapytania po pięć
  portów). Szacuje COP zbiornika w każdym cyklu grzania.
- **Chmura.** Wysyła telemetrię przez `POST /api/hp/add` i w odpowiedzi dostaje
  obiekt `operation`. Komunikat WebSocket `operation` przyspiesza tę wymianę.
  Zmienione ustawienia trafiają do pompy jako komendy RS-485. Komenda, której
  efektywna wartość się nie zmieniła, nie jest wysyłana ponownie.
- **Rejestracja.** Sterownik bez Root ID po połączeniu z internetem rejestruje
  się przez `POST /api/devices/register` swoim SN, czyli fabrycznym MAC układu,
  i zapisuje otrzymany Root ID w NVS. Znany SN dostaje z powrotem swój
  dotychczasowy Root ID. Do czasu rejestracji telemetria nie jest wysyłana.
- **Tryby.** Przycisk na GPIO5 przełącza tryb sterownika
  `OFF → CLOUD → MANUAL_CO → MANUAL_CWU → OFF`. Pierwsze naciśnięcie tylko
  pokazuje bieżący tryb, każde kolejne przechodzi do następnego. Tryb jest
  stosowany 5 s po ostatnim naciśnięciu i przetrwa restart. W trybie `CLOUD` obowiązuje
  `work_mode` z chmury (`M`, `A`, `PV`, `CWU`, `OFF`). Pełna semantyka trybów:
  [docs/server-driven-refactor-2026-09-20.md](docs/server-driven-refactor-2026-09-20.md#6-semantyka-trybów).
- **Strony WWW na porcie 80**, w sieci lokalnej i na własnym, otwartym punkcie
  dostępowym `HP-CO-setup`:

  | Adres | Dostęp | Zawartość |
  |---|---|---|
  | `/` | otwarty | podgląd telemetrii, odświeżany co 5 s |
  | `/telemetry.json` | otwarty | dokument telemetrii |
  | `/install` | hasło (`admin`) | Wi-Fi, SN i Root ID (tylko do odczytu), status rejestracji |
  | `/save` | hasło | zapis Wi-Fi i restart |

## Sprzęt

| Element | Połączenie |
|---|---|
| ESP32-WROOM (`esp32dev`) | — |
| RS-485 (pompa CHPC `0x41`, DTU `0x69`) | `Serial`, 9600 b/s |
| Wyświetlacz ST7735 | DC 12, CS 13, MOSI 14, CLK 27, RST 0 |
| Przekaźniki CO / CWU | GPIO26 / GPIO25 |
| Zasilanie modułów | GPIO18 |
| Przycisk trybu | GPIO5, z zewnętrznym rezystorem |
| RTC DS3231 | I²C, synchronizacja z NTP co 6 h |

Piny i adresy są w [src/hardware_config.hpp](src/hardware_config.hpp) oraz
[src/modbus_frame.cpp](src/modbus_frame.cpp).

## Budowanie i wgrywanie

Wymagane jest PlatformIO (CLI albo rozszerzenie w VS Code; `pio` jest wtedy
w `~/.platformio/penv/Scripts/`).

1. Skopiuj `src/secrets.example.h` do `src/secrets.h` i uzupełnij Wi-Fi.
   `CLOUD_ROOT_ID` jest opcjonalny: bez niego sterownik zarejestruje się sam.
   Wartości z `secrets.h` są tylko domyślne, bo dane zapisane na stronie
   `/install` mają pierwszeństwo.
2. Zbuduj i wgraj:

   ```sh
   pio run                  # build
   pio run -t upload        # wgranie
   pio device monitor       # port szeregowy, 9600 b/s
   ```

Przed wgraniem firmware z nową funkcją chmury najpierw wdróż odpowiednią wersję
`chpc-web`, na przykład endpoint rejestracji.

## Testy

**Jednostkowe** (to repo): działają na komputerze, bez płytki. Potrzebny jest
g++ (MinGW) w `PATH`.

```sh
pio test -e native
```

| Zestaw | Co sprawdza |
|---|---|
| [test_operation_controller](test/test_operation_controller/test_main.cpp) | scalanie operacji z chmury, tryby, reguły `OFF` i `AUTO_PV`, brak powtórnych komend |
| [test_pv_data_processor](test/test_pv_data_processor/test_main.cpp) | parser odpowiedzi Modbus z DTU Hoymiles |
| [test_modbus_frame](test/test_modbus_frame/test_main.cpp) | kodowanie ramek RS-485 i CRC |

**E2E całego łańcucha** (chpc ⇄ RS-485 ⇄ co ⇄ chpc-web ⇄ przeglądarka) są
w repozytorium `chpc`, w katalogu `test/e2e/`, razem z instrukcją i raportem.
Most testowy kompiluje z tego repozytorium pliki `src/operation_parser.cpp`,
`src/operation_controller.cpp`, `src/modbus_frame.cpp` i
`src/cop_estimator.cpp`, a ArduinoJson bierze z `.pio/libdeps/native`, więc
przed pierwszym uruchomieniem E2E wykonaj tu `pio test -e native`. Zmiana nazw
lub interfejsów tych plików wymaga poprawki w `chpc/test/e2e/build-bridge.sh`.

## Struktura kodu

| Plik | Odpowiedzialność |
|---|---|
| `main.cpp` | pętla główna: odczyty, przycisk, wymiana z chmurą |
| `cloud_client` | HTTPS i WebSocket do `chpc-web`, rejestracja urządzenia |
| `operation_parser` | JSON operacji z chmury → struktury domenowe |
| `operation_controller` | stan z chmury, tryby, efektywne komendy i przekaźniki |
| `serial_bus`, `modbus_frame` | kolejka RS-485 z priorytetami, kodowanie ramek, CRC |
| `heat_pump_data_processor`, `cop_estimator` | odczyt pompy i estymacja COP |
| `pv_data_processor` | odczyt DTU Hoymiles, dane dla każdego panelu |
| `telemetry`, `json_converters` | dokument telemetrii wysyłany do chmury i na stronę `/` |
| `config_portal` | strony WWW na porcie 80 |
| `device_config` | Wi-Fi, Root ID i SN; zapis w NVS |
| `device_io` | wyświetlacz, RTC, NTP, start Wi-Fi |

## Dokumentacja

- [chpc-web/CLAUDE.md](https://github.com/robertorlowski/chpc-web/blob/main/CLAUDE.md):
  opis całego systemu i kontraktów między repozytoriami; chpc-web jest projektem
  wiodącym.
- [docs/server-driven-refactor-2026-09-20.md](docs/server-driven-refactor-2026-09-20.md):
  obecna architektura, przepływ operacji, tryby, UART, chmura.

## Licencja

MIT, szczegóły w [LICENSE](LICENSE).
