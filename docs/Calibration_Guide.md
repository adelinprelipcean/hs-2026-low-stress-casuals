# Ghid de Calibrare - Hard&Soft 2026

Acest document descrie pașii exacți pentru calibrarea senzorilor sistemului hardware bazat pe ESP32-C3.

## 1. Calibrarea CPU Load (FreeRTOS Idle Task)
**Cum funcționează:** CPU load-ul este dedus numărând de câte ori se apelează funcția `idle_task_hook` pe parcursul unei secunde. Cu cât e apelată mai rar, cu atât procesorul este mai aglomerat cu alte task-uri (Senzori/Display).
**Cum se calibrează:**
- În funcția `setup()` din `main.cpp`, vei vedea că imediat după afișarea Boot Logo-ului "LOW STRESS CASUALS", sistemul execută un `delay(1000)`. 
- În timpul acestei secunde, **numai** task-ul Idle rulează la capacitate maximă, iar variabila `idleCounter` este salvată ca `maxIdleCount`.
- **Acțiune necesară:** Calibrarea se face 100% automat la fiecare pornire! Doar asigură-te că nu miști firele/resetezi de mai multe ori strict în primele 2 secunde de la pornire ca valoarea max să fie calculată corect.

## 2. Calibrarea Senzorului de Putere (INA219)
**Senzorul măsoară Curentul și Tensiunea, dar adesea are un mic *offset* (valoare falsă când curentul e de fapt 0mA).**
**Cum se calibrează:**
1. Lasă sistemul să funcționeze **FĂRĂ** să ai o sarcină pe ieșirea INA219.
2. Citește valoarea indicată pe OLED pe ecranul *POWER SYSTEMS*. Să presupunem că arată `0.8 mA`.
3. Deschide `main.cpp` la linia ~105 (în funcția `vTaskSensors`) și scade valoarea offset-ului:
   ```cpp
   float current = ina219.getCurrent_mA() - 0.8; // Calibrare offset
   if(current < 0) current = 0; // Previne valori negative
   ```

## 3. Calibrarea Convertorului AD/DA HW-011 (PCF8591)
Acest modul returnează valori brute între `0` și `255` pe canalele Analogice.
**A. Senzorul de Lumină (Photoresistor - AIN0):**
1. Du modulul în întuneric total și notează valoarea curentă din variabila `rawLight` (ex: 20).
2. Du modulul în lumină puternică (lanterna telefonului) și notează valoarea maximă (ex: 240).
3. Modifică maparea din `main.cpp` (linia ~117):
   ```cpp
   // mapping(raw_val, limit_jos, limit_sus, procent_jos, procent_sus)
   sysData.light_val = map(rawLight, 20, 240, 0, 100); 
   ```

**B. Senzorul de Temperatură (Thermistor NTC - AIN1):**
Termistorul schimbă rezistența odată cu temperatura într-o curbă logaritmică (ecuația Steinhart-Hart). Aici e mai greu de calibrat fără termometru.
**Metoda Liniară Ușoară:**
1. Citește T în cameră cu un termometru real (ex: `24.5 °C`).
2. Introdu cod temporar care să afișeze `rawTemp` pe Serial. Notează valoarea (ex: `140`).
3. Pune degetul pe rezistor și vezi la ce valoare urcă `rawTemp` (ex: `160`).
4. Estimează o valoare empirică de multiplicare în `main.cpp` (linia ~118). Momentan este pusă formula `20.0 + ((rawTemp - 128) * 0.1)` care assumează că valoarea `128` înseamnă `20 °C`. Joacă-te cu factorul `0.1` până se mișcă stabil.
