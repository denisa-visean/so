# Fire albe și negre – acces la o resursă comună fără înfometare

O aplicație are mai multe fire, fiecare alb sau negru. Resursa poate fi folosită simultan de oricâte fire de **aceeași** culoare, dar niciodată de un fir alb și unul negru în același timp.

## Soluția: coadă FIFO cu bilete

Un monitor (mutex + variabilă de condiție) ține patru variabile:

| variabilă | rol |
|---|---|
| `urmatorul_bilet` | biletul dat următorului fir care cere resursa |
| `bilet_servit` | biletul care e acum la rând |
| `in_resursa` | câte fire folosesc resursa acum |
| `culoare_curenta` | culoarea firelor din resursă |

**`cere_resursa(culoare)`**
1. Firul ia un bilet.
2. Așteaptă până când e la rând **și** resursa e liberă sau e folosită de fire de culoarea lui.
3. Intră: `in_resursa++`, `bilet_servit++` și îi trezește pe ceilalți. Dacă următorul din coadă e tot de culoarea lui, intră și el imediat, deci mai multe fire de același tip folosesc resursa simultan.

**`elibereaza_resursa()`**
- `in_resursa--`. Când ajunge la 0, îi trezește pe toți, ca să poată intra și cealaltă culoare.

## De ce cerințele sunt respectate

- **Excludere alb–negru:** un fir intră doar dacă resursa e liberă sau e de culoarea lui.
- **Mai multe fire de același tip simultan:** cât timp în fața cozii sunt fire de aceeași culoare, ele intră una după alta, fără să aștepte să iasă cele dinainte.
- **Fără înfometare:** firele intră strict în ordinea biletelor. Când un fir de cealaltă culoare ajunge în fața cozii, firele noi nu mai pot trece înaintea lui. Firele care sunt deja în resursă o folosesc un timp finit, deci resursa se golește și el intră. La fel pentru fiecare fir din coadă, deci toate ajung în final la resursă.
- **Fără refuzuri inutile:** dacă nu există niciun fir de cealaltă culoare, toate firele din coadă au aceeași culoare și intră imediat.
- **Fără preempțiune:** nimeni nu scoate un fir din resursă. Firul pleacă singur, prin `elibereaza_resursa()`.

## Fișiere

- `alb_negru_linux.c` – pthreads: `pthread_mutex_t`, `pthread_cond_t`
- `alb_negru_windows.c` – Windows API: `CRITICAL_SECTION`, `CONDITION_VARIABLE`, `CreateThread`

## Rulare

Linux:
```
gcc alb_negru_linux.c -o alb_negru_linux -pthread
./alb_negru_linux
```

Windows (Visual Studio sau MinGW):
```
gcc alb_negru_windows.c -o alb_negru_windows.exe
alb_negru_windows.exe
```

Programul pornește 10 fire cu culori aleatoare. Fiecare cere resursa de 5 ori. La final afișează numărul de erori de excludere, care trebuie să fie 0.
