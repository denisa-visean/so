#!/usr/bin/env python3
"""
Verifica un fisier de log al temei "Podul cu greutate limitata".

Utilizare: python3 verifica_log.py pod.log [W K P]
(implicit W=5, K=2, P=2)

Linie: timestamp_us id_fir N|S greutate NORM|AMB REQ|ENTER|EXIT greutate_pe_pod

Se verifica:
  - formatul fiecarei linii si ordinea timestamp-urilor;
  - fiecare masina face REQ, ENTER, EXIT exact o data, in ordinea asta;
  - ultima coloana = greutatea reala de pe pod dupa eveniment;
  - a) niciodata sensuri opuse pe pod;  b) greutatea <= W;
  - f) cel mult K masini din sens opus, care au cerut dupa ea, intra inaintea ei;
  - g) cel mult P masini din acelasi sens, care au cerut dupa ea, intra inaintea ei;
  - e) nicio masina nu ramane sa astepte cand podul n-are masini din sens opus,
       greutatea ei incape, nu asteapta nicio masina din sens opus si n-o
       opreste regula g) (o masina mai veche din acelasi sens, depasita deja de P ori).
"""
import re
import sys

LINIE = re.compile(r"^(\d+) (\d+) ([NS]) ([123]) (NORM|AMB) (REQ|ENTER|EXIT) (\d+)$")


def verifica(cale, W=5, K=2, P=2):
    erori = []
    masini = {}          # id -> dict
    pe_pod = {}          # id -> (sens, greutate)
    asteapta = []        # id-uri, in ordinea cererii
    ultim_ts = -1
    max_opus = max_acelasi = 0

    def eroare(nr, text):
        erori.append(f"linia {nr}: {text}")

    def verifica_e(nr):
        """regula e), verificata intr-un moment stabil (inaintea unui REQ/EXIT)"""
        greutate = sum(g for _, g in pe_pod.values())
        sensuri_pod = {s for s, _ in pe_pod.values()}
        for poz, y in enumerate(asteapta):
            Y = masini[y]
            if sensuri_pod and Y["sens"] not in sensuri_pod:
                continue
            if greutate + Y["g"] > W:
                continue
            if any(masini[x]["sens"] != Y["sens"] for x in asteapta):
                continue
            if any(masini[x]["sens"] == Y["sens"] and masini[x]["acelasi"] >= P
                   for x in asteapta[:poz]):
                continue
            eroare(nr, f"regula e): masina {y} asteapta fara motiv")

    with open(cale) as f:
        linii = [l.rstrip("\r\n") for l in f if l.strip()]

    for nr, linie in enumerate(linii, 1):
        m = LINIE.match(linie)
        if not m:
            eroare(nr, f"format gresit: {linie!r}")
            continue
        ts, idf, sens, g, _, ev, g_pod = m.groups()
        ts, idf, g, g_pod = int(ts), int(idf), int(g), int(g_pod)
        if ts < ultim_ts:
            eroare(nr, "timestamp mai mic decat cel anterior")
        ultim_ts = ts

        if ev in ("REQ", "EXIT"):
            verifica_e(nr)

        if ev == "REQ":
            if idf in masini:
                eroare(nr, f"masina {idf} cere accesul a doua oara")
                continue
            masini[idf] = {"sens": sens, "g": g, "stare": "REQ", "opus": 0, "acelasi": 0}
            asteapta.append(idf)
        elif ev == "ENTER":
            Y = masini.get(idf)
            if Y is None or Y["stare"] != "REQ":
                eroare(nr, f"masina {idf} intra fara sa fi cerut accesul")
                continue
            if any(s != sens for s, _ in pe_pod.values()):
                eroare(nr, f"regula a): masina {idf} intra peste masini din sens opus")
            pe_pod[idf] = (sens, g)
            if sum(x for _, x in pe_pod.values()) > W:
                eroare(nr, f"regula b): greutatea pe pod depaseste W={W}")
            poz = asteapta.index(idf)
            for x in asteapta[:poz]:          # masinile mai vechi sunt depasite
                X = masini[x]
                if X["sens"] == sens:
                    X["acelasi"] += 1
                    max_acelasi = max(max_acelasi, X["acelasi"])
                    if X["acelasi"] > P:
                        eroare(nr, f"regula g): masina {x} depasita de {X['acelasi']} masini din acelasi sens")
                else:
                    X["opus"] += 1
                    max_opus = max(max_opus, X["opus"])
                    if X["opus"] > K:
                        eroare(nr, f"regula f): masina {x} depasita de {X['opus']} masini din sens opus")
            asteapta.remove(idf)
            Y["stare"] = "ENTER"
        else:  # EXIT
            Y = masini.get(idf)
            if Y is None or Y["stare"] != "ENTER":
                eroare(nr, f"masina {idf} iese fara sa fi intrat")
                continue
            del pe_pod[idf]
            Y["stare"] = "EXIT"

        if sum(x for _, x in pe_pod.values()) != g_pod:
            eroare(nr, f"ultima coloana e {g_pod}, dar pe pod sunt {sum(x for _, x in pe_pod.values())} t")

    for idf, Y in masini.items():
        if Y["stare"] != "EXIT":
            erori.append(f"masina {idf} nu a terminat (ultima stare: {Y['stare']})")

    print(f"{cale}: {len(linii)} linii, {len(masini)} masini (W={W}, K={K}, P={P})")
    print(f"  depasiri maxime suferite de o masina: din sens opus {max_opus}, din acelasi sens {max_acelasi}")
    if erori:
        print(f"  {len(erori)} erori:")
        for e in erori[:30]:
            print("   ", e)
        if len(erori) > 30:
            print(f"    ... si inca {len(erori) - 30}")
    else:
        print("  OK - toate regulile sunt respectate")
    return not erori


if __name__ == "__main__":
    if len(sys.argv) not in (2, 5):
        print(__doc__)
        sys.exit(2)
    args = [int(x) for x in sys.argv[2:]] or [5, 2, 2]
    sys.exit(0 if verifica(sys.argv[1], *args) else 1)
