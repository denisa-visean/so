/*
 * Sisteme de Operare - Tema: Podul cu greutate limitata (Windows)
 * Varianta: W = 5, K = 2, P = 2
 * Mecanisme: procese (CreateProcess), memorie partajata (CreateFileMapping),
 *            obiecte cu nume (mutex si semafoare cu nume).
 *
 * Fiecare masina este un proces separat: programul principal se porneste
 * pe el insusi de N ori, cu argumentul "--masina <id>". Starea podului si
 * lista masinilor care asteapta stau in memoria partajata "Local\Pod_Mem".
 * Accesul la ele e protejat de mutex-ul cu nume "Local\Pod_Mutex".
 * Fiecare masina are un semafor cu nume ("Local\Pod_Masina_<id>") pe care
 * doarme cat asteapta. Cine schimba starea podului (o cerere sau o iesire)
 * apeleaza dispecer(), care hotaraste ce masini pot intra si le trezeste.
 * Nu exista asteptare activa: o masina care asteapta e blocata in
 * WaitForSingleObject.
 *
 * Compilare (Visual Studio): proiectul CMake construieste doua programe:
 *   pod_windows.exe     - versiunea finala
 *   pod_windows_v1.exe  - versiunea 1, fara protectie la infometare
 *                         (compilata cu FARA_PROTECTIE definit)
 * Rulare:
 *   pod_windows.exe [nr_masini] [mod] [fisier_log]
 *   mod: aleator (implicit) | opus | greu
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 5     /* greutatea maxima pe pod (tone)                       */
#define K 2     /* max. masini din sens opus care pot trece inaintea ei  */
#define P 2     /* max. masini din acelasi sens, venite dupa ea, care pot
                   trece inaintea ei                                      */

#define MAX_MASINI   512
#define NUME_SHM     "Local\\Pod_Mem"
#define NUME_MUTEX   "Local\\Pod_Mutex"
#define NUME_SEM     "Local\\Pod_Masina_%d"

enum { NORD = 0, SUD = 1 };   /* N = de la nord la sud, S = de la sud la nord */

struct masina {
    int  sens, greutate;
    int  intarziere_ms, traversare_ms;  /* cand cere accesul, cat traverseaza */
    long ordine;          /* numarul de ordine al cererii (REQ)             */
    int  asteapta;        /* 1 = a cerut accesul si inca n-a intrat         */
    int  depasiri_opus;   /* cate masini din sens opus, venite dupa ea, au intrat inaintea ei */
    int  depasiri_acelasi;/* cate masini din acelasi sens, venite dupa ea, au intrat inaintea ei */
};

struct pod {
    int  nr_masini;
    LONGLONG t0, frecventa;   /* QueryPerformanceCounter la pornire      */
    char fisier_log[MAX_PATH];
    long urmatoarea_ordine;
    int  sens;            /* sensul masinilor de pe pod (daca pe_pod > 0)  */
    int  greutate;        /* greutatea totala de pe pod                    */
    int  pe_pod;          /* cate masini sunt pe pod                       */
    struct masina m[MAX_MASINI];
};

static struct pod *pod;
static HANDLE mutex;
static HANDLE fisier_log;

static LONGLONG acum_us(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (t.QuadPart - pod->t0) * 1000000LL / pod->frecventa;
}

/* scrie un eveniment in log; se apeleaza doar cu mutex-ul luat */
static void scrie_log(int id, const char *eveniment)
{
    char linie[128];
    DWORD scris;
    int n = snprintf(linie, sizeof linie, "%lld %d %c %d NORM %s %d\n",
                     acum_us(), id,
                     pod->m[id].sens == NORD ? 'N' : 'S',
                     pod->m[id].greutate, eveniment, pod->greutate);
    WriteFile(fisier_log, linie, (DWORD)n, &scris, NULL);
}

static void trezeste(int id)
{
    char nume[64];
    HANDLE s;
    snprintf(nume, sizeof nume, NUME_SEM, id);
    s = OpenSemaphoreA(SEMAPHORE_MODIFY_STATE, FALSE, nume);
    ReleaseSemaphore(s, 1, NULL);
    CloseHandle(s);
}

/*
 * Hotaraste ce masini intra pe pod. Se apeleaza doar cu mutex-ul luat,
 * dupa fiecare cerere (REQ) si dupa fiecare iesire (EXIT).
 * Masinile care asteapta sunt parcurse in ordinea cererilor (cea mai
 * veche prima). O masina Y intra daca:
 *   a) podul e gol sau are masini in sensul ei;
 *   b) greutatea ei incape (greutate + g <= W);
 *   f) nicio masina din sens opus care a cerut inaintea ei si inca
 *      asteapta n-a fost deja depasita de K masini din sensul lui Y;
 *   g) nicio masina din acelasi sens care a cerut inaintea ei si inca
 *      asteapta n-a fost deja depasita de P masini venite dupa ea.
 */
static void dispecer(void)
{
    int ordine[MAX_MASINI], cate = 0, i, j, a, b;

    /* lista masinilor care asteapta, sortata dupa ordinea cererii */
    for (i = 0; i < pod->nr_masini; i++) {
        if (!pod->m[i].asteapta)
            continue;
        for (j = cate; j > 0 && pod->m[ordine[j - 1]].ordine > pod->m[i].ordine; j--)
            ordine[j] = ordine[j - 1];
        ordine[j] = i;
        cate++;
    }

    for (a = 0; a < cate; a++) {
        int y = ordine[a];
        struct masina *Y = &pod->m[y];

        if (pod->pe_pod > 0 && pod->sens != Y->sens)   /* regula a) */
            continue;
        if (pod->greutate + Y->greutate > W)           /* regula b) */
            continue;

#ifndef FARA_PROTECTIE
        {
            int poate = 1;
            for (b = 0; b < a && poate; b++) {
                struct masina *X = &pod->m[ordine[b]];
                if (!X->asteapta)
                    continue;
                if (X->sens == Y->sens && X->depasiri_acelasi >= P)  /* regula g) */
                    poate = 0;
                if (X->sens != Y->sens && X->depasiri_opus >= K)     /* regula f) */
                    poate = 0;
            }
            if (!poate)
                continue;
        }
#endif

        /* Y intra: toate masinile mai vechi care inca asteapta sunt depasite */
        for (b = 0; b < a; b++) {
            struct masina *X = &pod->m[ordine[b]];
            if (!X->asteapta)
                continue;
            if (X->sens == Y->sens) X->depasiri_acelasi++;
            else                    X->depasiri_opus++;
        }

        Y->asteapta = 0;
        pod->sens = Y->sens;
        pod->greutate += Y->greutate;
        pod->pe_pod++;
        scrie_log(y, "ENTER");
        trezeste(y);
    }
}

/* ce face fiecare proces-masina */
static int masina(int id)
{
    HANDLE mapare, semaforul_meu;
    struct masina *eu;
    char nume[64];

    mapare = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, NUME_SHM);
    if (mapare == NULL) return 1;
    pod = (struct pod *)MapViewOfFile(mapare, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct pod));
    if (pod == NULL) return 1;
    mutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, NUME_MUTEX);
    snprintf(nume, sizeof nume, NUME_SEM, id);
    semaforul_meu = OpenSemaphoreA(SYNCHRONIZE, FALSE, nume);
    fisier_log = CreateFileA(pod->fisier_log, FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (mutex == NULL || semaforul_meu == NULL || fisier_log == INVALID_HANDLE_VALUE)
        return 1;
    eu = &pod->m[id];

    Sleep(eu->intarziere_ms);

    /* cere accesul */
    WaitForSingleObject(mutex, INFINITE);
    eu->ordine = pod->urmatoarea_ordine++;
    eu->depasiri_opus = eu->depasiri_acelasi = 0;
    eu->asteapta = 1;
    scrie_log(id, "REQ");
    dispecer();
    ReleaseMutex(mutex);

    /* asteapta (blocat, fara busy-waiting) pana il lasa dispecerul sa intre */
    WaitForSingleObject(semaforul_meu, INFINITE);

    /* traverseaza podul - durata finita */
    Sleep(eu->traversare_ms);

    /* iese de pe pod */
    WaitForSingleObject(mutex, INFINITE);
    pod->greutate -= eu->greutate;
    pod->pe_pod--;
    scrie_log(id, "EXIT");
    dispecer();
    ReleaseMutex(mutex);

    CloseHandle(fisier_log);
    CloseHandle(semaforul_meu);
    CloseHandle(mutex);
    UnmapViewOfFile(pod);
    CloseHandle(mapare);
    return 0;
}

/* parametrii masinilor, in functie de mod */
static void genereaza(int n, const char *mod)
{
    int i;
    for (i = 0; i < n; i++) {
        struct masina *m = &pod->m[i];
        if (strcmp(mod, "opus") == 0) {
            /* masina 0 vine din S; restul vin continuu din N */
            m->sens = (i == 0) ? SUD : NORD;
            m->greutate = 1;
            m->intarziere_ms = (i == 0) ? 200 : i * 60;
            m->traversare_ms = 400;
        } else if (strcmp(mod, "greu") == 0) {
            /* masina 0 are 3 t; restul au 1 t si vin continuu, acelasi sens */
            m->sens = NORD;
            m->greutate = (i == 0) ? 3 : 1;
            m->intarziere_ms = (i == 0) ? 200 : i * 50;
            m->traversare_ms = 300;
        } else {
            m->sens = rand() % 2;
            m->greutate = 1 + rand() % 3;
            m->intarziere_ms = rand() % 3000;
            m->traversare_ms = 100 + rand() % 400;
        }
    }
}

int main(int argc, char *argv[])
{
    int n, i;
    const char *mod, *log;
    HANDLE mapare, *procese, *semafoare, f;
    LARGE_INTEGER t;
    char exe[MAX_PATH], linie_comanda[MAX_PATH + 64], nume[64];

    /* procesul copil: o masina */
    if (argc == 3 && strcmp(argv[1], "--masina") == 0)
        return masina(atoi(argv[2]));

    n   = (argc > 1) ? atoi(argv[1]) : 60;
    mod = (argc > 2) ? argv[2] : "aleator";
    log = (argc > 3) ? argv[3] : "pod.log";
    if (n < 1 || n > MAX_MASINI) {
        fprintf(stderr, "nr_masini trebuie sa fie intre 1 si %d\n", MAX_MASINI);
        return 1;
    }
    srand((unsigned)time(NULL) ^ GetCurrentProcessId());

    /* fisierul de log, gol */
    f = CreateFileA(log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { printf("Nu pot crea %s\n", log); return 1; }
    CloseHandle(f);

    /* memoria partajata */
    mapare = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                0, sizeof(struct pod), NUME_SHM);
    if (mapare == NULL) { printf("CreateFileMapping: %lu\n", GetLastError()); return 1; }
    pod = (struct pod *)MapViewOfFile(mapare, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct pod));
    if (pod == NULL) { printf("MapViewOfFile: %lu\n", GetLastError()); return 1; }
    memset(pod, 0, sizeof *pod);
    pod->nr_masini = n;
    GetFullPathNameA(log, MAX_PATH, pod->fisier_log, NULL);
    genereaza(n, mod);

    /* obiectele cu nume: mutex-ul si cate un semafor pentru fiecare masina */
    mutex = CreateMutexA(NULL, FALSE, NUME_MUTEX);
    if (mutex == NULL) { printf("CreateMutex: %lu\n", GetLastError()); return 1; }
    semafoare = (HANDLE *)malloc(n * sizeof(HANDLE));
    procese   = (HANDLE *)malloc(n * sizeof(HANDLE));
    for (i = 0; i < n; i++) {
        snprintf(nume, sizeof nume, NUME_SEM, i);
        semafoare[i] = CreateSemaphoreA(NULL, 0, 1, nume);
        if (semafoare[i] == NULL) { printf("CreateSemaphore: %lu\n", GetLastError()); return 1; }
    }

    printf("Podul: W=%d, K=%d, P=%d, %d masini, mod %s%s, log in %s\n",
           W, K, P, n, mod,
#ifdef FARA_PROTECTIE
           " (versiunea 1, FARA protectie la infometare)",
#else
           "",
#endif
           pod->fisier_log);

    /* cate un proces pentru fiecare masina */
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    QueryPerformanceFrequency(&t);
    pod->frecventa = t.QuadPart;
    QueryPerformanceCounter(&t);
    pod->t0 = t.QuadPart;
    for (i = 0; i < n; i++) {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof si);
        si.cb = sizeof si;
        snprintf(linie_comanda, sizeof linie_comanda, "\"%s\" --masina %d", exe, i);
        if (!CreateProcessA(exe, linie_comanda, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            printf("CreateProcess: %lu\n", GetLastError());
            return 1;
        }
        CloseHandle(pi.hThread);
        procese[i] = pi.hProcess;
    }
    for (i = 0; i < n; i++) {
        WaitForSingleObject(procese[i], INFINITE);
        CloseHandle(procese[i]);
    }

    /* curatenie */
    for (i = 0; i < n; i++)
        CloseHandle(semafoare[i]);
    free(semafoare);
    free(procese);
    CloseHandle(mutex);
    UnmapViewOfFile(pod);
    CloseHandle(mapare);

    printf("Toate masinile au traversat podul.\n");
    return 0;
}
