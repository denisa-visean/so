# Comunicare între procese prin obiect mapat în memorie

Două procese separate (același program pornit de două ori) numără împreună de la 1 la 1000 într-o zonă de memorie partajată. Fiecare proces, în buclă:

1. rezervă semaforul
2. citește memoria
3. dă cu banul (`random(2)`); cât timp cade 2, scrie în memorie următorul număr
4. eliberează semaforul

Semaforul (valoare inițială 1, maxim 1) garantează că doar un proces atinge memoria la un moment dat, deci numerele sunt scrise în ordine, fără să se suprapună.

## Fișiere

- `numarare_windows.c` – varianta Windows: `CreateFileMapping` / `MapViewOfFile` + `CreateSemaphore` cu nume
- `numarare_linux.c` – varianta Linux: `shm_open` / `mmap` + `sem_open`
- `start.bat` – pornește două procese pe Windows

## Rulare

Windows:
```
gcc numarare_windows.c -o numarare_windows.exe
start.bat
```

Linux:
```
gcc numarare_linux.c -o numarare_linux -pthread
./numarare_linux & ./numarare_linux ; wait
```
