/*
 * Varianta Linux/POSIX a aceluiasi program (shm_open + mmap + sem_open).
 *
 * Compilare: gcc numarare_linux.c -o numarare_linux -pthread
 * Rulare:    ./numarare_linux & ./numarare_linux ; wait
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>

#define NUME_MEMORIE  "/so_numarare_memorie"
#define NUME_SEMAFOR  "/so_numarare_semafor"
#define LIMITA        1000

int main(void)
{
    int fd, creat = 1, *numar;
    sem_t *semafor;
    pid_t pid = getpid();

    srand((unsigned)time(NULL) ^ pid);

    /* obiectul de memorie partajata: primul proces il creeaza */
    fd = shm_open(NUME_MEMORIE, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1 && errno == EEXIST) {
        creat = 0;
        fd = shm_open(NUME_MEMORIE, O_RDWR, 0666);
    }
    if (fd == -1) { perror("shm_open"); return 1; }
    if (creat && ftruncate(fd, sizeof(int)) == -1) { perror("ftruncate"); return 1; }

    numar = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (numar == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);

    /* semafor cu nume, valoare initiala 1 */
    semafor = sem_open(NUME_SEMAFOR, O_CREAT, 0666, 1);
    if (semafor == SEM_FAILED) { perror("sem_open"); return 1; }

    if (creat) {
        sem_wait(semafor);
        *numar = 0;
        sem_post(semafor);
        printf("[%d] Am creat memoria partajata.\n", pid);
    } else {
        printf("[%d] M-am conectat la memoria partajata existenta.\n", pid);
    }

    for (;;) {
        int valoare;

        sem_wait(semafor);                 /* rezerv semaforul */

        valoare = *numar;                  /* citesc memoria */
        if (valoare >= LIMITA) {
            sem_post(semafor);
            break;
        }

        /* dau cu banul: cat timp cade 2 scriu urmatorul numar */
        while ((rand() % 2) + 1 == 2 && valoare < LIMITA) {
            valoare++;
            *numar = valoare;
            printf("[%d] scriu %d\n", pid, valoare);
            fflush(stdout);
        }

        sem_post(semafor);                 /* eliberez semaforul */
        usleep(1000);
    }

    printf("[%d] Am terminat, s-a ajuns la %d.\n", pid, LIMITA);

    munmap(numar, sizeof(int));
    sem_close(semafor);
    if (creat) {                           /* curatenie */
        sleep(1);
        shm_unlink(NUME_MEMORIE);
        sem_unlink(NUME_SEMAFOR);
    }
    return 0;
}
