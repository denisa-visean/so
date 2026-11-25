/*
 * Sisteme de Operare - Problema firelor albe si negre (Windows API)
 *
 * O resursa poate fi folosita simultan de oricate fire de ACELASI tip,
 * dar niciodata de un fir alb si unul negru in acelasi timp.
 *
 * Solutie fara infometare: coada FIFO cu bilete (ca la ghiseu).
 *  - fiecare fir care cere resursa ia un bilet (numar de ordine);
 *  - firele intra strict in ordinea biletelor;
 *  - firul de la rand intra daca resursa e libera SAU e folosita de
 *    fire de aceeasi culoare cu el => mai multe fire de acelasi tip
 *    intra impreuna, unul dupa altul, fara sa astepte;
 *  - daca firul de la rand e de cealalta culoare, el si toti cei din
 *    spatele lui asteapta pana se elibereaza resursa. Firele noi nu pot
 *    "sari" peste el, deci fiecare fir intra in cele din urma.
 *
 * Daca nu exista niciun fir de cealalta culoare, nimeni nu e refuzat:
 * toate firele din coada au aceeasi culoare si intra imediat.
 *
 * Compilare (Visual Studio Developer Prompt): cl alb_negru_windows.c
 * Compilare (MinGW): gcc alb_negru_windows.c -o alb_negru_windows.exe
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NR_FIRE        10
#define NR_ACCESARI    5

enum culoare { ALB = 0, NEGRU = 1 };
static const char *nume_culoare[] = { "ALB", "NEGRU" };

/* ---------------- monitorul care protejeaza resursa ---------------- */
static CRITICAL_SECTION   sectiune;
static CONDITION_VARIABLE cond;

static unsigned long urmatorul_bilet = 0; /* biletul dat urmatorului venit */
static unsigned long bilet_servit    = 0; /* biletul care e acum la rand   */
static int in_resursa      = 0;           /* cate fire folosesc resursa    */
static int culoare_curenta = ALB;         /* culoarea lor (daca in_resursa>0) */

static void cere_resursa(int culoare)
{
    unsigned long bilet;

    EnterCriticalSection(&sectiune);
    bilet = urmatorul_bilet++;

    /* astept sa fiu la rand si resursa sa fie libera sau de culoarea mea */
    while (bilet != bilet_servit ||
           (in_resursa > 0 && culoare_curenta != culoare))
        SleepConditionVariableCS(&cond, &sectiune, INFINITE);

    culoare_curenta = culoare;
    in_resursa++;
    bilet_servit++;                       /* trece la randul urmatorului */

    WakeAllConditionVariable(&cond);      /* urmatorul poate intra daca e tot de culoarea mea */
    LeaveCriticalSection(&sectiune);
}

static void elibereaza_resursa(void)
{
    EnterCriticalSection(&sectiune);
    in_resursa--;
    if (in_resursa == 0)
        WakeAllConditionVariable(&cond);  /* resursa libera: poate intra si cealalta culoare */
    LeaveCriticalSection(&sectiune);
}

/* ------------- verificare independenta (doar pentru test) ------------- */
static volatile LONG albi_in = 0, negri_in = 0, erori = 0;

static void foloseste_resursa(int id, int culoare)
{
    if (culoare == ALB) InterlockedIncrement(&albi_in);
    else                InterlockedIncrement(&negri_in);

    if (albi_in > 0 && negri_in > 0) {
        InterlockedIncrement(&erori);
        printf("EROARE: alb si negru simultan in resursa!\n");
    }

    printf("Firul %2d (%-5s) foloseste resursa  [albi: %ld, negri: %ld]\n",
           id, nume_culoare[culoare], albi_in, negri_in);
    Sleep(10 + rand() % 40);              /* folosire finita a resursei */

    if (culoare == ALB) InterlockedDecrement(&albi_in);
    else                InterlockedDecrement(&negri_in);
}

/* ---------------------------- firele ---------------------------- */
struct info_fir { int id; int culoare; };

static DWORD WINAPI fir(LPVOID arg)
{
    struct info_fir *f = (struct info_fir *)arg;
    int i;

    for (i = 0; i < NR_ACCESARI; i++) {
        Sleep(rand() % 50);               /* face altceva intre accesari */

        printf("Firul %2d (%-5s) cere resursa\n", f->id, nume_culoare[f->culoare]);
        cere_resursa(f->culoare);
        foloseste_resursa(f->id, f->culoare);
        elibereaza_resursa();
        printf("Firul %2d (%-5s) a eliberat resursa\n", f->id, nume_culoare[f->culoare]);
    }
    return 0;
}

int main(void)
{
    HANDLE fire[NR_FIRE];
    struct info_fir info[NR_FIRE];
    int i;

    srand((unsigned)time(NULL));
    InitializeCriticalSection(&sectiune);
    InitializeConditionVariable(&cond);

    for (i = 0; i < NR_FIRE; i++) {
        info[i].id = i;
        info[i].culoare = rand() % 2;     /* fiecare fir e alb sau negru */
        fire[i] = CreateThread(NULL, 0, fir, &info[i], 0, NULL);
    }
    WaitForMultipleObjects(NR_FIRE, fire, TRUE, INFINITE);

    for (i = 0; i < NR_FIRE; i++)
        CloseHandle(fire[i]);
    DeleteCriticalSection(&sectiune);

    printf("\nToate firele au terminat. Erori de excludere: %d\n", (int)erori);
    return 0;
}
