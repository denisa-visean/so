/*
 * Sisteme de Operare - Comunicare intre procese prin obiect mapat in memorie
 *
 * Doua procese (acelasi program pornit de doua ori) numara impreuna
 * de la 1 pana la 1000 intr-o zona de memorie partajata:
 *   - rezerva semaforul
 *   - citesc memoria
 *   - dau cu banul (random(2)); cat timp cade 2 scriu in memorie urmatorul numar
 *   - elibereaza semaforul
 *
 * Compilare (Visual Studio Developer Prompt):  cl numarare_windows.c
 * Compilare (MinGW):                           gcc numarare_windows.c -o numarare_windows.exe
 * Rulare: porniti programul de doua ori (sau start.bat).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUME_MEMORIE  "Local\\SO_Numarare_Memorie"
#define NUME_SEMAFOR  "Local\\SO_Numarare_Semafor"
#define LIMITA        1000

int main(void)
{
    HANDLE hMapare, hSemafor;
    int *numar;
    DWORD pid = GetCurrentProcessId();
    int creat;

    srand((unsigned)time(NULL) ^ pid);

    /* Obiectul mapat in memorie (backed de fisierul de paginare).
       Primul proces il creeaza, al doilea il deschide pe acelasi. */
    hMapare = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                 0, sizeof(int), NUME_MEMORIE);
    if (hMapare == NULL) {
        printf("CreateFileMapping a esuat: %lu\n", GetLastError());
        return 1;
    }
    creat = (GetLastError() != ERROR_ALREADY_EXISTS);

    numar = (int *)MapViewOfFile(hMapare, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int));
    if (numar == NULL) {
        printf("MapViewOfFile a esuat: %lu\n", GetLastError());
        CloseHandle(hMapare);
        return 1;
    }

    /* Semafor cu nume, valoare initiala 1, maxim 1 => un singur proces
       are voie in memoria partajata la un moment dat. */
    hSemafor = CreateSemaphoreA(NULL, 1, 1, NUME_SEMAFOR);
    if (hSemafor == NULL) {
        printf("CreateSemaphore a esuat: %lu\n", GetLastError());
        UnmapViewOfFile(numar);
        CloseHandle(hMapare);
        return 1;
    }

    if (creat) {
        WaitForSingleObject(hSemafor, INFINITE);
        *numar = 0;
        ReleaseSemaphore(hSemafor, 1, NULL);
        printf("[%lu] Am creat memoria partajata.\n", pid);
    } else {
        printf("[%lu] M-am conectat la memoria partajata existenta.\n", pid);
    }

    for (;;) {
        int valoare;

        /* rezerv semaforul */
        WaitForSingleObject(hSemafor, INFINITE);

        /* citesc memoria */
        valoare = *numar;
        if (valoare >= LIMITA) {
            ReleaseSemaphore(hSemafor, 1, NULL);
            break;
        }

        /* dau cu banul: random(2) -> 1 sau 2; cat timp cade 2 scriu urmatorul numar */
        while ((rand() % 2) + 1 == 2 && valoare < LIMITA) {
            valoare++;
            *numar = valoare;
            printf("[%lu] scriu %d\n", pid, valoare);
        }

        /* eliberez semaforul */
        ReleaseSemaphore(hSemafor, 1, NULL);

        /* las si celalalt proces sa prinda semaforul */
        Sleep(1);
    }

    printf("[%lu] Am terminat, s-a ajuns la %d.\n", pid, LIMITA);

    UnmapViewOfFile(numar);
    CloseHandle(hMapare);
    CloseHandle(hSemafor);
    return 0;
}
