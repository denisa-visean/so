/*
 * Sisteme de Operare - Problema firelor albe si negre (Linux / pthreads)
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
 * Compilare: gcc alb_negru_linux.c -o alb_negru_linux -pthread
 * Rulare:    ./alb_negru_linux
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define NR_FIRE        10
#define NR_ACCESARI    5

enum culoare { ALB = 0, NEGRU = 1 };
static const char *nume_culoare[] = { "ALB", "NEGRU" };

/* ---------------- monitorul care protejeaza resursa ---------------- */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond  = PTHREAD_COND_INITIALIZER;

static unsigned long urmatorul_bilet = 0; /* biletul dat urmatorului venit */
static unsigned long bilet_servit    = 0; /* biletul care e acum la rand   */
static int in_resursa      = 0;           /* cate fire folosesc resursa    */
static int culoare_curenta = ALB;         /* culoarea lor (daca in_resursa>0) */

static void cere_resursa(int culoare)
{
    unsigned long bilet;

    pthread_mutex_lock(&mutex);
    bilet = urmatorul_bilet++;

    /* astept sa fiu la rand si resursa sa fie libera sau de culoarea mea */
    while (bilet != bilet_servit ||
           (in_resursa > 0 && culoare_curenta != culoare))
        pthread_cond_wait(&cond, &mutex);

    culoare_curenta = culoare;
    in_resursa++;
    bilet_servit++;                       /* trece la randul urmatorului */

    pthread_cond_broadcast(&cond);        /* urmatorul poate intra daca e tot de culoarea mea */
    pthread_mutex_unlock(&mutex);
}

static void elibereaza_resursa(void)
{
    pthread_mutex_lock(&mutex);
    in_resursa--;
    if (in_resursa == 0)
        pthread_cond_broadcast(&cond);    /* resursa libera: poate intra si cealalta culoare */
    pthread_mutex_unlock(&mutex);
}

/* ------------- verificare independenta (doar pentru test) ------------- */
static volatile int albi_in = 0, negri_in = 0, erori = 0;

static void foloseste_resursa(int id, int culoare)
{
    if (culoare == ALB) __sync_fetch_and_add(&albi_in, 1);
    else                __sync_fetch_and_add(&negri_in, 1);

    if (albi_in > 0 && negri_in > 0) {
        __sync_fetch_and_add(&erori, 1);
        printf("EROARE: alb si negru simultan in resursa!\n");
    }

    printf("Firul %2d (%-5s) foloseste resursa  [albi: %d, negri: %d]\n",
           id, nume_culoare[culoare], albi_in, negri_in);
    usleep(1000 * (10 + rand() % 40));    /* folosire finita a resursei */

    if (culoare == ALB) __sync_fetch_and_sub(&albi_in, 1);
    else                __sync_fetch_and_sub(&negri_in, 1);
}

/* ---------------------------- firele ---------------------------- */
struct info_fir { int id; int culoare; };

static void *fir(void *arg)
{
    struct info_fir *f = (struct info_fir *)arg;
    int i;

    for (i = 0; i < NR_ACCESARI; i++) {
        usleep(1000 * (rand() % 50));     /* face altceva intre accesari */

        printf("Firul %2d (%-5s) cere resursa\n", f->id, nume_culoare[f->culoare]);
        cere_resursa(f->culoare);
        foloseste_resursa(f->id, f->culoare);
        elibereaza_resursa();
        printf("Firul %2d (%-5s) a eliberat resursa\n", f->id, nume_culoare[f->culoare]);
    }
    return NULL;
}

int main(void)
{
    pthread_t fire[NR_FIRE];
    struct info_fir info[NR_FIRE];
    int i;

    srand((unsigned)time(NULL));

    for (i = 0; i < NR_FIRE; i++) {
        info[i].id = i;
        info[i].culoare = rand() % 2;     /* fiecare fir e alb sau negru */
        pthread_create(&fire[i], NULL, fir, &info[i]);
    }
    for (i = 0; i < NR_FIRE; i++)
        pthread_join(fire[i], NULL);

    printf("\nToate firele au terminat. Erori de excludere: %d\n", erori);
    return 0;
}
