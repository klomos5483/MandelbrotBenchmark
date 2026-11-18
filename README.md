# MandelbrotBenchmark

Natywna aplikacja Windows C++17 z GUI WinAPI i biblioteką DLL napisaną w MASM x64.
Projekt porównuje dokładnie ten sam skalarny algorytm, a nie dwa sposoby rysowania.

## Wymagane środowisko

Konfiguracja przygotowana dla środowiska wykrytego na komputerze:
- Visual Studio Community 2026, wersja 18.6.2.
- Narzędzia C++ MSVC v145; dostępna instalacja kompilatora 14.51.36231.
- Windows SDK 10.0.26100.0.
- MASM ml64, MSBuild oraz narzędzia linkowania x64.
- Zestaw funkcji Visual Studio: programowanie aplikacji klasycznych w języku C++.

Nie są potrzebne Qt, .NET, CMake, NASM ani zewnętrzne biblioteki GUI.
Wersja Release używa dynamicznego środowiska uruchomieniowego MSVC.
Na komputerze bez Visual Studio może być wymagany Microsoft Visual C++ Redistributable x64.
Wersja Debug jest przeznaczona dla komputera programistycznego.

## Uruchamianie w Visual Studio

1. Otwórz plik `MandelbrotBenchmark.sln`.
2. Wybierz konfigurację `Release` oraz platformę `x64`.
3. Projektem startowym ma być `MandelbrotApp`, umieszczony jako pierwszy w rozwiązaniu.
   Jeśli IDE wybrało inaczej, kliknij projekt prawym przyciskiem i wybierz ustawienie projektu startowego.
4. Zbuduj rozwiązanie i uruchom aplikację klawiszem F5 lub Ctrl+F5.
5. Użyj przycisku „Porównaj”.

Pliki wynikowe znajdują się we wspólnym folderze `bin\\x64\\Release`.
Plik `MandelbrotAsm.dll` musi znajdować się obok `MandelbrotApp.exe`.
Konfiguracja Debug zapisuje wyniki do `bin\\x64\\Debug`.
Pliki pośrednie są rozdzielone według projektu i konfiguracji w folderze `obj`.

## Automatyczne budowanie i testowanie

Uruchom PowerShell w folderze rozwiązania:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\BuildAndTest.ps1
```

Skrypt wykrywa Visual Studio przez vswhere. Buduje Debug i Release, pokazuje eksporty DLL,
uruchamia testy obliczeniowe, testy GUI oraz benchmark Release.
Błąd któregokolwiek etapu przerywa skrypt. Rezultaty są wypisywane na standardowe wyjście.
ExecutionPolicy Bypass dotyczy wyłącznie uruchamianego procesu, nie zmienia ustawienia systemowego.

Program ma również tryby testowe:

```powershell
& .\bin\x64\Release\MandelbrotApp.exe --self-test --benchmark | Out-Host
& .\bin\x64\Release\MandelbrotApp.exe --gui-smoke | Out-Host
```

Potok zapewnia oczekiwanie na zakończenie aplikacji okienkowej w interaktywnej konsoli PowerShell.

## Struktura i odpowiedzialności

- `MandelbrotBenchmark.sln`: rozwiązanie obejmujące dwa projekty.
- `Shared.props`: wspólne ustawienia kompilacji i katalogów wynikowych.
- `Shared/MandelbrotApi.h`: stabilny interfejs DLL i struktura parametrów.
- `MandelbrotApp/Main.cpp`: punkt wejścia i wybór trybu pracy.
- `MandelbrotApp/Gui.cpp`: okno, kontrolki, prezentacja, wątek roboczy i anulowanie.
- `MandelbrotApp/MandelbrotCpp.cpp`: walidacja, referencyjny algorytm C++ i wspólna paleta.
- `MandelbrotApp/Benchmark.cpp`: rozgrzewka, pomiary, mediana i porównanie buforów.
- `MandelbrotApp/SelfTests.cpp`: testy znanych punktów, obrazów, błędów i DLL.
- `MandelbrotApp/Engine.h`: struktury żądania i wyniku oraz deklaracje modułów.
- `MandelbrotAsm/Mandelbrot.asm`: kompletna implementacja obliczeń i walidacji w MASM.
- `MandelbrotAsm/MandelbrotAsm.def`: publiczna nazwa eksportowanej funkcji.
- `MandelbrotAsm/DllMain.cpp`: pusty, bezpieczny punkt wejścia DLL.
- `BuildAndTest.ps1`: powtarzalny proces weryfikacji.

Pliki .vcxproj importują standardowe reguły Visual Studio, a projekt DLL również masm.props i masm.targets.
Plik .asm ma typ elementu MASM. Referencja projektu aplikacji do DLL ustala kolejność budowania
i linkowanie przez bibliotekę importową .lib. Ta .lib nie zawiera zastępczej implementacji algorytmu.
Obliczenia ASM wykonuje kod maszynowy z pliku DLL.

## Algorytm

Dla piksela wyznaczamy punkt zespolony c. Startujemy od z równego zeru.
Powtarzamy podnoszenie z do kwadratu i dodawanie c.
Punkt ucieka, gdy suma kwadratów części rzeczywistej i urojonej przekroczy 4.
Równość 4 nie oznacza ucieczki. Licznik zwiększa się po wyznaczeniu nowej wartości z.

Kolejność działań w obu implementacjach:

```text
zr2 = zr * zr
zi2 = zi * zi
jeśli zr2 + zi2 > 4 albo licznik >= limit: zakończ
product = zr * zi
nextIm = (product + product) + ci
nextRe = (zr2 - zi2) + cr
zr = nextRe
zi = nextIm
licznik = licznik + 1
```

Nowa część urojona korzysta ze starych części z. Nadpisanie zr przed użyciem go
w iloczynie byłoby błędem algorytmu.

Mapowanie obrazu obejmuje oba końce przedziału osi przy wymiarze większym od 1.
Krok to różnica maksimum i minimum podzielona przez wymiar pomniejszony o 1.
Przy wymiarze 1 przyjmujemy dzielnik 1, a jedyny piksel ma współrzędną minimum.
Współrzędna to minimum dodane do iloczynu indeksu i kroku.
Indeksy wierszy rosną w dół ekranu; tak samo rośnie część urojona.
Bufor zawiera wiersze kolejno, od lewej do prawej.

Osiągnięcie limitu iteracji oznacza brak wykrytej ucieczki, nie matematyczny dowód przynależności.
Kolor jest obliczany przez wspólny kod C++ po zakończeniu pomiaru.

## Interfejs DLL i ABI x64

Funkcja eksportowana jako `MandelbrotAsm` ma następującą deklarację:

```cpp
int __cdecl MandelbrotAsm(const MBParams* params, uint32_t* output,
                         uint64_t capacity, const volatile LONG* cancelFlag);
```

Parametry:
- `params`: wskaźnik na strukturę 48 bajtów.
- `output`: bufor liczby iteracji przydzielony przez aplikację.
- `capacity`: liczba dostępnych elementów uint32_t, nie liczba bajtów.
- `cancelFlag`: opcjonalny wskaźnik na Windows LONG; NULL wyłącza sprawdzanie anulowania.

Układ MBParams:
| Offset w bajtach | Pole | Typ |
| --- | --- | --- |
| 0 | width | uint32_t |
| 4 | height | uint32_t |
| 8 | maxIterations | uint32_t |
| 12 | reserved, wymagane zero | uint32_t |
| 16 | minRe | double |
| 24 | maxRe | double |
| 32 | minIm | double |
| 40 | maxIm | double |

Asercje kompilacji kontrolują rozmiar i kluczowe przesunięcia struktury.
Nie wolno zmieniać jej układu bez równoczesnej zmiany kodu MASM.

Dla Windows x64 pierwsze cztery parametry tej funkcji trafiają do RCX, RDX, R8 i R9.
Nie jest to konwencja System V stosowana przez wiele systemów Unix.
Zapis __cdecl nie wybiera osobnego wariantu stosu dla x64.
Plik nagłówkowy używa powiązania C i dllimport; .def zapewnia stabilną nazwę eksportu.

Zwracane kody:
- 0: sukces, cały bufor został obliczony.
- 1: nieprawidłowy argument.
- 2: zbyt mały bufor.
- 3: anulowanie; bufor może zawierać częściowe wyniki i nie jest publikowany w GUI.

Dopuszczalne wymiary to 1-8192, a łączny limit to 16 777 216 pikseli.
Limit iteracji to 1-100 000. Zakres każdej osi musi być uporządkowany i skończony,
a każda granica musi leżeć od -1 000 000 do 1 000 000.
Program sprawdza pojemność przed zapisem. Nie może jednak sprawdzić rzeczywistego rozmiaru
pamięci wskazanej przez obcy kod, który poda nieprawdziwy wskaźnik lub pojemność.

### Rejestry i instrukcje MASM

- RSI przechowuje adres parametrów.
- RDI wskazuje bieżące miejsce bufora.
- R12D, R13D i R14D przechowują szerokość, wysokość i limit.
- R15D jest numerem wiersza, EBX numerem kolumny, EAX licznikiem iteracji i kodem wyniku.
- RBP przechowuje wskaźnik flagi anulowania.
- XMM0 i XMM1 przechowują bieżące części z.
- XMM2 i XMM3 przechowują ich kwadraty.
- XMM4 służy do sumy kwadratów i obliczania nowej części urojonej.
- CVTSI2SD przelicza indeks całkowity na double.
- MULSD, ADDSD, SUBSD i DIVSD wykonują skalarne działania double.
- UCOMISD sprawdza zakresy, w tym nieuporządkowane porównania NaN.
- COMISD i JA rozpoznają przekroczenie promienia ucieczki.
- MOVSD przenosi pojedynczą liczbę double.
- XORPD zeruje stan początkowy.

Kod zachowuje wszystkie używane rejestry nieulotne przez PUSH/POP.
Nie modyfikuje XMM6-XMM15. Ramka stosu zawiera cztery lokalne liczby double
i wyrównanie; PROC FRAME, .pushreg i .allocstack generują dane odwijania stosu.

Nie ma obliczeń wektorowych ani AVX/FMA. Rejestr XMM nie oznacza automatycznie
przetwarzania kilku pikseli jednocześnie.

## Wątki, anulowanie i cykl życia okna

Wątek GUI nie wykonuje algorytmu. Pojedynczy wątek roboczy wykonuje kolejne obliczenia.
Algorytm nie jest zrównoleglony na kilka rdzeni.

Podczas pracy pola i przyciski uruchamiania są zablokowane; działa przycisk anulowania.
GUI zapisuje flagę przez InterlockedExchange. Obie implementacje sprawdzają ją
przed obliczeniem każdego piksela. Projekt jest przeznaczony dla Windows x64 i korzysta
z semantyki MSVC /volatile:ms; nie jest to uniwersalny wzorzec synchronizacji ISO C++.

Wynik przekazywany jest jako własność komunikatu WM_APP. GUI dołącza wątek przed użyciem wyniku.
Zamknięcie aktywnego okna zleca anulowanie i czeka asynchronicznie na komunikat zakończenia,
zamiast blokować pętlę komunikatów. Żaden wątek nie jest odłączany przez detach.

## Metodologia pomiarów

Porównanie:
1. Przydzielenie dwóch buforów poza pomiarem.
2. Jedna pełna rozgrzewka C++ i jedna MASM poza pomiarem.
3. Domyślnie pięć pomiarów każdej implementacji, z naprzemienną kolejnością.
4. Pomiar QueryPerformanceCounter obejmuje jedno wywołanie kernela.
5. Mediana czasu oddzielnie dla C++ i MASM.
6. Dokładne porównanie wszystkich liczników iteracji, bez tolerancji.
7. Wspólna suma kontrolna, kolorowanie i rysowanie poza pomiarem.

W pomiarze pozostaje mały koszt walidacji i wywołania funkcji; dla MASM również
wywołanie przez import DLL. Biblioteka jest załadowana przez system przed wejściem do programu.
Nie mierzymy alokacji, GUI ani ładowania DLL.

Release używa /O2 oraz /fp:strict. Dokumentowana dyrektywa MSVC
`#pragma loop(no_vector)` blokuje wektoryzację pętli referencyjnego kernela.
Optymalizacja C++ pozostaje włączona, ponieważ porównujemy zoptymalizowany algorytm skalarny.
Ścisły model zmiennoprzecinkowy pomaga zachować tę samą kolejność działań.
Nie stosujemy szybkich testów wnętrza zbioru tylko w jednej implementacji.

Współczynnik to czas C++ podzielony przez czas ASM:
- Większy od 1: MASM jest szybszy.
- Mniejszy od 1: C++ jest szybszy.
- Równy 1: zmierzone czasy są równe.

Różnica może wynikać z generacji kodu, dostępu do danych i kosztu instrukcji.
Asembler nie gwarantuje przewagi nad optymalizatorem C++.
Wyniki Debug nie są podstawą wnioskowania o wydajności.
Powtarzaj pomiar przy zbliżonym obciążeniu komputera. Krótkie odcinki i zmiany taktowania
procesora zwiększają niepewność. Pojedynczy współczynnik nie jest uniwersalną oceną języka.

Przyciski pojedynczego generowania pokazują czas jednej próby.
Do wniosków używaj przycisku „Porównaj”, nie tych pojedynczych czasów.

## Zakres testów automatycznych

- Załadowana rzeczywista DLL, architektura AMD64 i eksport znaleziony przez GetProcAddress.
- Siedem punktów o znanych wynikach, w tym punkty graniczne promienia.
- 50 obrazów: różne wymiary, wymiary równe 1, limity, powiększenie i deterministyczne dane losowe.
- Dokładna zgodność każdego piksela.
- Wartości wartownicze przed i za buforem.
- Zerowe i nadmierne wymiary, limit pikseli i iteracji, pole reserved.
- Odwrócone i puste zakresy, NaN, nieskończoności i granice współrzędnych.
- NULL i zbyt mała pojemność.
- Anulowanie przed pierwszym pikselem.
- Ukryte, rzeczywiste okno WinAPI: trzy przyciski obliczeń, odblokowanie kontrolek,
  reset parametrów, walidacja i bezpieczne zamknięcie podczas pracy.

Test GUI nie zastępuje wzrokowej oceny widocznego interfejsu, skalowania DPI ani wyglądu fraktala.
Wartownicy pomagają wykrywać zapis za końcem bufora, ale nie stanowią pełnego audytu bezpieczeństwa pamięci.

## Ręczna kontrola GUI

- Sprawdź czytelność etykiet i brak przyciętych kontrolek na swoim ekranie.
- Wygeneruj domyślny obraz obiema metodami.
- Uruchom Porównaj i sprawdź liczbę rozbieżnych pikseli.
- Zmień rozmiar okna; obraz powinien zachować proporcje.
- Sprawdź wymiary 1 na 1 i niepoprawny wymiar 0.
- Sprawdź separator dziesiętny w postaci kropki i przecinka.
- Uruchom kosztowniejsze obliczenia i użyj Anuluj.
- Zamknij okno podczas obliczeń; proces powinien zakończyć pracę bez błędu.

## Wyniki wykonania

Status: budowanie i wszystkie testy automatyczne zakończone powodzeniem.
Identyfikator wykonania: job_1794844781_2401d4da. Kod zakończenia procesu: 0.
Procesor: 13th Gen Intel Core i5-1335U.

| Weryfikacja | Debug x64 | Release x64 |
| --- | --- | --- |
| Budowanie aplikacji i DLL | OK | OK |
| Rzeczywista DLL AMD64 i eksport MandelbrotAsm | OK | OK |
| 7 znanych punktów i 50 obrazów | OK | OK |
| Zgodność liczników iteracji i granice bufora | OK | OK |
| Walidacja parametrów i anulowanie | OK | OK |
| Ukryte testy kontrolek GUI i zamknięcia wątku | OK | OK |

Benchmark Release: obraz 800 × 600, limit 500 iteracji, pełna rozgrzewka,
pięć prób każdej implementacji i naprzemienna kolejność pomiarów.

| Wielkość | Wynik |
| --- | --- |
| Mediana C++ | 106,996 ms |
| Mediana MASM | 112,102 ms |
| Czas C++ podzielony przez czas MASM | 0,954 |
| Rozbieżne piksele | 0 |
| Suma kontrolna | 15885653025157358691 |

W tej serii pomiarowej C++ uzyskał krótszy czas niż ręcznie napisany MASM.
Nie należy traktować małej różnicy jako uniwersalnej przewagi jednej technologii.
Nie wykonano wzrokowej kontroli widocznego okna ani oceny skalowania na różnych monitorach;
służy do tego procedura ręczna opisana w tym dokumencie.

## Prezentacja na zaliczeniu

1. Pokaż rozwiązanie z osobnymi projektami EXE i DLL.
2. Pokaż MandelbrotApi.h, plik .asm oraz eksport w .def.
3. Wyjaśnij, jak piksel jest mapowany na punkt zespolony i co oznacza liczba iteracji.
4. Wskaż pętlę obliczeniową C++ i odpowiadającą jej pętlę MASM.
5. Uruchom Release x64 i wygeneruj obraz.
6. Uruchom porównanie dla kilku rozdzielczości i limitów.
7. Wyjaśnij rozgrzewkę, medianę, wyłączenie rysowania z pomiaru i kontrolę zgodności.
8. Zinterpretuj rzeczywiste wyniki, nawet gdy C++ okaże się szybszy.

## Ograniczenia i rozszerzenia

Wersja podstawowa nie udostępnia interaktywnego powiększania myszą, eksportu obrazu,
wektorowych implementacji SIMD ani obliczeń wielowątkowych.
Można dodać te funkcje jako oddzielne warianty z własną metodologią porównań.
Duża liczba iteracji i punktów wewnątrz zbioru wydłuża obliczenia; służy do tego anulowanie.
Program nie ma narzuconego limitu czasu pracy kernela.

## Dokumentacja źródłowa

- [Microsoft: MASM for x64](https://learn.microsoft.com/en-us/cpp/assembler/masm/masm-for-x64-ml64-exe)
- [Microsoft: x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
- [Microsoft: eksport DLL przez DEF](https://learn.microsoft.com/en-us/cpp/build/exporting-from-a-dll-using-def-files)
- [Microsoft: .ALLOCSTACK](https://learn.microsoft.com/en-us/cpp/assembler/masm/dot-allocstack)

- [Microsoft: pragma loop i no_vector](https://learn.microsoft.com/en-us/cpp/preprocessor/loop)

Integracja vcpkg jest wyłączona tylko w tym rozwiązaniu, ponieważ nie używa ono pakietów zewnętrznych.

## Leśny motyw interfejsu

Interfejs używa zieleni sosny i mchu, jasnych paneli oraz wspólnej zielono-złotej palety obrazu.
Nagłówek zawiera tylko nazwę Mandelbrot. Usunięto z niego techniczne opisy typów liczb,
wątków i biblioteki. Przyciski są rysowane indywidualnie, reagują na najechanie, wciśnięcie
i fokus klawiatury. Nadal są natywnymi kontrolkami WinAPI.

Wyniki są rozdzielone na trzy pola: czas C++, czas MASM i stosunek czasu C++ do czasu MASM.
Opis wskazuje wariant z krótszym czasem. Wynik zgodności jest pokazany oddzielnie.
Brak pomiaru nie jest prezentowany jako zero. Przy rozbieżnościach aplikacja nie wskazuje
przewagi wydajnościowej. Informacje o rozmiarze obrazu i limicie dotyczą wyświetlonego wyniku,
nie niezastosowanych zmian w polach.

Panel parametrów obsługuje przewijanie, gdy wysokość okna jest mała. Tabulator pozwala
przechodzić między polami i przyciskami. Obraz zachowuje proporcje podczas zmiany rozmiaru okna.
Uruchomienie programu generuje obraz domyślną metodą C++; pomiar MASM i porównanie pozostają
dostępne przez odpowiednie przyciski.

Kod wyglądu znajduje się w Gui.cpp i UiTheme.cpp/UiTheme.h. Korzysta wyłącznie z systemowych
WinAPI, GDI+, Common Controls oraz DWM, bez dodatkowego frameworka.
Zmiana palety nie zmienia obliczeń, zgodności buforów ani zakresu benchmarku.

Tryb kontrolny --ui-preview z pełną ścieżką PNG renderuje własny interfejs do pamięci i pliku,
bez odczytywania ekranu. Zawiera rzeczywiste wyniki obliczeń dla domyślnych parametrów.
