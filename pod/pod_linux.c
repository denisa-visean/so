/*
 * Sisteme de Operare - Tema: Podul cu greutate limitata (Linux)
 * Varianta: W = 5, K = 2, P = 2
 * Mecanisme: procese (fork), memorie partajata (shm_open + mmap),
 *            semafoare cu nume (sem_open).
 *
 * Fiecare masina este un proces separat. Starea podului si lista
 * masinilor care asteapta stau in memoria partajata. Accesul la ele e
 * protejat de semaforul cu nume "/pod_mutex". Fiecare masina are si un
 * semafor propriu, cu nume ("/pod_m_<id>"), pe care doarme cat asteapta.
 * Cine schimba starea podului (o cerere sau o iesire) apeleaza
 * dispecer(), care hotaraste ce masini pot intra si le trezeste.
 * Nu exista asteptare activa: o masina care asteapta e blocata in sem_wait.
 *
 * Compilare:
 *   versiunea finala:        gcc pod_linux.c -o pod -pthread
 *   versiunea 1 (fara
 *   protectie la infometare): gcc pod_linux.c -o pod_v1 -pthread -DFARA_PROTECTIE
 * Rulare:
 *   ./pod [nr_masini] [mod] [fisier_log]
 *   mod: aleator (implicit) | opus | greu
 *     opus  - o masina din S cere accesul cat timp vin continuu masini din N
 *     greu  - o masina grea (3 t) cere accesul cat timp vin continuu masini usoare
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define W 5     /* greutatea maxima pe pod (tone)                       */
#define K 2     /* max. masini din sens opus care pot trece inaintea ei  */
#define P 2     /* max. masini din acelasi sens, venite dupa ea, care pot
                   trece inaintea ei                                      */

#define MAX_MASINI   512
#define NUME_SHM     "/pod_shm"
#define NUME_MUTEX   "/pod_mutex"
#define NUME_SEM     "/pod_m_%d"

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
    long long t0;         /* momentul pornirii, in microsecunde            */
    long urmatoarea_ordine;
    int  sens;            /* sensul masinilor de pe pod (daca pe_pod > 0)  */
    int  greutate;        /* greutatea totala de pe pod                    */
    int  pe_pod;          /* cate masini sunt pe pod                       */
    struct masina m[MAX_MASINI];
};

static struct pod *pod;
static sem_t *mutex;
static int fd_log;

static long long acum_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}

/* scrie un eveniment in log; se apeleaza doar cu mutex-ul luat */
static void scrie_log(int id, const char *eveniment)
{
    char linie[128];
    int n = snprintf(linie, sizeof linie, "%lld %d %c %d NORM %s %d\n",
                     acum_us() - pod->t0, id,
                     pod->m[id].sens == NORD ? 'N' : 'S',
                     pod->m[id].greutate, eveniment, pod->greutate);
    if (write(fd_log, linie, n) != n)
        perror("write log");
}

static void asteapta_sem(sem_t *s)
{
    while (sem_wait(s) == -1 && errno == EINTR)
        ;
}

static void trezeste(int id)
{
    char nume[32];
    sem_t *s;
    snprintf(nume, sizeof nume, NUME_SEM, id);
    s = sem_open(nume, 0);
    sem_post(s);
    sem_close(s);
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
static void masina(int id)
{
    struct masina *eu = &pod->m[id];
    char nume[32];
    sem_t *semaforul_meu;

    snprintf(nume, sizeof nume, NUME_SEM, id);
    semaforul_meu = sem_open(nume, 0);
    fd_log = open(getenv("POD_LOG"), O_WRONLY | O_APPEND);

    usleep(eu->intarziere_ms * 1000);

    /* cere accesul */
    asteapta_sem(mutex);
    eu->ordine = pod->urmatoarea_ordine++;
    eu->depasiri_opus = eu->depasiri_acelasi = 0;
    eu->asteapta = 1;
    scrie_log(id, "REQ");
    dispecer();
    sem_post(mutex);

    /* asteapta (blocat, fara busy-waiting) pana il lasa dispecerul sa intre */
    asteapta_sem(semaforul_meu);

    /* traverseaza podul - durata finita */
    usleep(eu->traversare_ms * 1000);

    /* iese de pe pod */
    asteapta_sem(mutex);
    pod->greutate -= eu->greutate;
    pod->pe_pod--;
    scrie_log(id, "EXIT");
    dispecer();
    sem_post(mutex);

    sem_close(semaforul_meu);
    close(fd_log);
    exit(0);
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
    int n = (argc > 1) ? atoi(argv[1]) : 60;
    const char *mod = (argc > 2) ? argv[2] : "aleator";
    const char *log = (argc > 3) ? argv[3] : "pod.log";
    int fd, i;
    char nume[32];

    if (n < 1 || n > MAX_MASINI) {
        fprintf(stderr, "nr_masini trebuie sa fie intre 1 si %d\n", MAX_MASINI);
        return 1;
    }
    srand((unsigned)time(NULL) ^ getpid());

    /* fisierul de log, gol */
    fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) { perror("log"); return 1; }
    close(fd);
    setenv("POD_LOG", log, 1);

    /* memoria partajata */
    shm_unlink(NUME_SHM);
    fd = shm_open(NUME_SHM, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) { perror("shm_open"); return 1; }
    if (ftruncate(fd, sizeof(struct pod)) == -1) { perror("ftruncate"); return 1; }
    pod = mmap(NULL, sizeof(struct pod), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pod == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);
    memset(pod, 0, sizeof *pod);
    pod->nr_masini = n;
    genereaza(n, mod);

    /* semafoarele cu nume */
    sem_unlink(NUME_MUTEX);
    mutex = sem_open(NUME_MUTEX, O_CREAT | O_EXCL, 0600, 1);
    if (mutex == SEM_FAILED) { perror("sem_open mutex"); return 1; }
    for (i = 0; i < n; i++) {
        sem_t *s;
        snprintf(nume, sizeof nume, NUME_SEM, i);
        sem_unlink(nume);
        s = sem_open(nume, O_CREAT | O_EXCL, 0600, 0);
        if (s == SEM_FAILED) { perror("sem_open masina"); return 1; }
        sem_close(s);
    }

    printf("Podul: W=%d, K=%d, P=%d, %d masini, mod %s%s, log in %s\n",
           W, K, P, n, mod,
#ifdef FARA_PROTECTIE
           " (versiunea 1, FARA protectie la infometare)",
#else
           "",
#endif
           log);

    /* cate un proces pentru fiecare masina */
    fflush(stdout);
    pod->t0 = acum_us();
    for (i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid == -1) { perror("fork"); return 1; }
        if (pid == 0)
            masina(i);
    }
    for (i = 0; i < n; i++)
        wait(NULL);

    /* curatenie */
    for (i = 0; i < n; i++) {
        snprintf(nume, sizeof nume, NUME_SEM, i);
        sem_unlink(nume);
    }
    sem_close(mutex);
    sem_unlink(NUME_MUTEX);
    munmap(pod, sizeof *pod);
    shm_unlink(NUME_SHM);

    printf("Toate masinile au traversat podul.\n");
    return 0;
}
