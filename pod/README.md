# Podul cu greutate limitată

**Varianta: W = 5, K = 2, P = 2.** Fiecare mașină e un proces separat. Starea podului și lista mașinilor care așteaptă stau în memoria partajată, protejată de un mutex cu nume. Fiecare mașină doarme pe semaforul ei cu nume până primește voie să intre, deci nu există așteptare activă.

## Cum funcționează

Fiecare cerere primește un **număr de ordine**. După fiecare `REQ` și fiecare `EXIT`, funcția `dispecer()` parcurge mașinile care așteaptă, de la cea mai veche la cea mai nouă, și lasă să intre orice mașină Y pentru care:

1. podul e gol sau are mașini în sensul lui Y (regula a);
2. greutatea lui Y încape: `greutate + g ≤ W` (regula b);
3. nicio mașină X mai veche decât Y, care încă așteaptă, nu și-a terminat „bugetul” de depășiri:
   - dacă X e din sens opus, trebuie ca `X.depasiri_opus < K` (regula f);
   - dacă X e din același sens, trebuie ca `X.depasiri_acelasi < P` (regula g).

Când Y intră, toate mașinile mai vechi care încă așteaptă au fost depășite, deci contorul potrivit al fiecăreia crește cu 1. Dispecerul actualizează podul, scrie `ENTER` în log și trezește mașina.

## Cum sunt respectate regulile f) și g)

O mașină X poate fi depășită doar de mașini care au cerut accesul după ea. Fiecare astfel de depășire crește un contor al lui X. Când contorul din sens opus ajunge la K, nicio mașină din sens opus mai nouă nu mai trece înaintea lui X. Când contorul din același sens ajunge la P, nicio mașină din același sens mai nouă nu mai trece. Așadar X e depășită de cel mult K mașini din sens opus și de cel mult P din același sens.

Toate mașinile ajung să traverseze. Mașina cea mai veche care așteaptă nu poate fi oprită de f) sau g), pentru că nu are în fața ei nicio mașină mai veche. Pe podul gol, ea poate intra mereu, pentru că g ≤ 3 < W. Iar dacă podul e ocupat, el se golește în timp finit, fiindcă traversarea durează un timp finit și mașinile noi pot trece înaintea ei de cel mult K + P ori. Astfel, fiecare mașină devine la un moment dat cea mai veche și intră.

## De ce f) și g) nu contrazic regula e)

Regula e) interzice ca o mașină să fie ținută pe loc **fără motiv**.

- **Regula f)** oprește o mașină Y doar dacă **există o mașină din sens opus care așteaptă** (cea care și-a terminat bugetul de K depășiri). În cazul acesta, condiția din e), „nu așteaptă nicio mașină din sens opus”, nu e îndeplinită, deci e) nu cere ca Y să intre.
- **Regula g)** oprește o mașină Y doar dacă în fața ei așteaptă o mașină X din același sens, depășită deja de P ori, de obicei pentru că X e prea grea și nu încape încă. Motivul e concret, iar oprirea e temporară: nu mai intră mașini mai noi decât X, podul se golește, X intră și imediat după ea poate intra Y.

Prin urmare, o mașină care așteaptă este oprită doar de a), de b), de o mașină din sens opus care așteaptă sau de o mașină mai veche din același sens care nu mai poate fi depășită. În toate celelalte cazuri, dispecerul o lasă să intre imediat.

## Fișiere

| Fișier | Ce conține |
|---|---|
| `pod_linux.c` | `fork`, `shm_open` + `mmap`, `sem_open`; compilat cu `-DFARA_PROTECTIE` dă versiunea 1 |
| `pod_windows.c` | `CreateProcess`, `CreateFileMapping`, mutex și semafoare cu nume |
| `CMakeLists.txt`, `launch.vs.json` | proiectul pentru Visual Studio |
| `verifica_log.py` | verifică un log după toate regulile |
| `loguri/` | log-urile versiunii 1 și ale versiunii finale |
