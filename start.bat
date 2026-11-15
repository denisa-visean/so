@echo off
rem Porneste doua procese separate care numara impreuna in memoria partajata
start "Proces 1" cmd /k numarare_windows.exe
start "Proces 2" cmd /k numarare_windows.exe
