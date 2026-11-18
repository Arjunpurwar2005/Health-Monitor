> STATUS (2026-11-06): Sirf plan hai, abhi kuch bana nahi. ~1 hafte baad (13 Nov 2026 ke aas paas) discuss karke shuru karna hai.

# Plan: Check-up button + Google Sheet + beta ka dashboard

## Context (kyun)
Buzurg maa-baap gaon me hain, beta shehar me. Maa-baap kabhi "main theek hoon" bol dete hain. Isliye ek **bada "Check-up" button** chahiye: dabao, ungli rakho, aur us samay ki pakki reading (Temp, BPM, SpO2, time) **Google Sheet** me save ho jaaye. Beta ek **secret link** pe sundar dashboard dekhe: aakhri check-up, trend charts aur poori history. Saalon ka data free me rahega.

User ke faisle:
- Naya external button (GPIO 27)
- Data sirf button dabane pe save hoga
- Google Sheet + Apps Script dashboard (free, alag hosting nahi)
- Dashboard secret link se khulega, login nahi
- Existing Blynk live readings aur alerts waise hi chalte rahenge

## Poora system kaise kaam karega
```
[Button] -> ESP32 check-up (5 readings ka average)
         -> NVS queue me save (internet na ho to bhi data na khoye)
         -> upload task: HTTPS POST + DEVICE_KEY  ->  Apps Script doPost() -> Sheet me nayi row
Beta: secret link (?key=VIEW_KEY)  ->  Apps Script doGet() -> Dashboard page (Sheet ka data + charts)
Beta ke phone pe turant: Blynk event "checkup_done" (notification)
```
Do alag keys hongi. `DEVICE_KEY` ESP32 me rahegi, `VIEW_KEY` beta ke link me. Dashboard ka link leak ho jaaye to bhi koi nakli data nahi daal payega.

## Hardware
- Push button: ek pair **D27**, doosra pair **GND**. Code me `INPUT_PULLUP`.
- GPIO 27 abhi kahin use nahi ho raha (used: 0, 2, 4, 21, 22, 25) aur strapping pin nahi hai.

## 1. Firmware: `v2/v2.ino`

### Check-up state machine (non-blocking, 1 sec timer pe)
1. **IDLE → button dabaya:** state `WAITING`, chhoti beep. OLED pe "CHECK-UP / Ungli sensor pe rakho".
2. **Readings ikatthi karna:**
   - Naya `volatile uint32_t calcCount`: `calculateHeartRateAndSpO2()` me tab badhega jab BPM aur SpO2 dono valid hon. Check-up sirf nayi calculation pe sample lega.
   - Pehli 2 calculations chhod do, phir 5 ka average lo. OLED pe "Ungli rakhe raho 3/5".
   - Ungli hati to samples reset aur OLED pe "Ungli mat hatao".
3. **60 sec me 5 samples na mile:** "Check-up nahi hua, dobara dabao", lambi beep, wapas IDLE.
4. **DONE:**
   - Temp `bodyTemp` se lo (`tempOk` false ho to temp khaali bhejo).
   - Status nikaalo existing limits se (`HIGH_TEMP_LIMIT`, `LOW_SPO2_LIMIT`, `HIGH_BPM_LIMIT`, `LOW_BPM_LIMIT`).
   - Record NVS queue me daalo.
   - OLED pe 10 sec result + "Bhej rahe hain...". Phir "Beta ko bhej diya ✓" ya "Internet aate hi bhejenge". 2 beep.
   - Blynk connected ho to `Blynk.logEvent("checkup_done", "...")`.

### Kaun se existing variables/functions reuse honge
- Readings: `heartRate`, `spo2`, `fingerOn`, `bodyTemp`, `tempOk`.
- OLED: `updateOLED()` (check-up screen sabse pehle), `showMessage()`, `oledShow()`.
- Button ka pattern `checkButton()` jaisa hoga, plus debounce.
- `BlynkTimer timer` pe naya 1 sec interval.
- **Buzzer fix:** `checkAlerts()` har 2 sec buzzer HIGH/LOW set karta hai, jo beep ko kaat dega. Isliye `beepUntil` aur `updateBuzzer()` helper banega jo `loop()` me chalega. Buzzer ON rahega agar alert hai YA beep chal rahi hai.

### Time
- WiFi judte hi `configTzTime("IST-5:30", "pool.ntp.org")`.
- Record me check-up ka epoch time jaayega. Time abhi pata nahi (NTP sync nahi hua) to record me `millis()` aur ek boot counter save hoga. Upload ke waqt epoch is tarah nikalega: `ab ka time - (ab ka millis - record ka millis)`.
- Beech me reboot ho gaya to server pe aane ka time use hoga, aur record pe "time andaaze se" ka flag lagega.

### Queue + upload (internet na ho tab bhi)
- `Preferences` (NVS) me max 20 records ki queue. Bijli jaaye to bhi bachi rahegi. Ek mutex se access hoga.
- Naya FreeRTOS **`uploadTask`** (stack ~8 KB, priority 1). HTTPS request 2-5 sec leti hai, isliye loop/OLED nahi rukenge.
  - Task me: WiFi connected aur queue me kuch ho to `WiFiClientSecure` (`setInsecure()`) + `HTTPClient` se JSON POST bhejo `SHEET_URL` pe.
  - `setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS)`, kyunki Apps Script POST ke baad 302 redirect deta hai.
  - Body me `"OK"` aaye to record queue se hatao. Fail hua to 60 sec baad dobara try.
- Naye config defines sabse upar, Blynk lines ke saath:
  - `#define SHEET_URL "https://script.google.com/macros/s/.../exec"`
  - `#define DEVICE_KEY "..."`

## 2. Google Apps Script: naya folder `dashboard/`
Beta apne Gmail me Sheet banayega, phir Extensions > Apps Script me ye 2 files paste karega.

- **`dashboard/Code.gs`**
  - `doPost(e)`: JSON parse karo aur `DEVICE_KEY` check karo (Script Properties se). Sheet `Checkups` me row jodo: `Time | Temp | BPM | SpO2 | Status | Late upload | Time andaaze se`. `"OK"` return karo. `LockService` se do request ek saath aayein to row na bigde.
  - `doGet(e)`: `key` galat hai to "Link galat hai" page. Sahi hai to `HtmlService` template me Sheet ka data JSON bhar ke `Dashboard.html` do. `setTitle`, aur mobile ke liye viewport meta.
  - `setup()` helper: ek baar chalao. Sheet aur header banata hai, aur do random keys bana ke Script Properties me save karta hai aur log me dikhata hai.
- **`dashboard/Dashboard.html`**: mobile-first sundar page, light/dark theme. Chart.js (pinned version, cdnjs se).
  - Upar: "Maa-Papa ki Health", aakhri check-up kab hua ("2 ghante pehle"), aur Temp / BPM / SpO2 ke bade tiles. Har tile pe rang: normal, dhyan do, khatra. Limits firmware wale hi hain.
  - **24 ghante se check-up nahi hua:** peela banner "Kal se check-up nahi hua - phone karke poocho".
  - Trend charts: Temp, BPM, SpO2. Normal range halki patti me dikhegi. Range buttons: 7 din / 30 din / 3 mahine / sab.
  - History table: naya sabse upar, status chips, aur "der se aaya" ka nishaan.
  - Google page ke upar "created by a Google Apps Script user" ki patti dikhata hai. Ye normal hai, setup.txt me likhenge.

## 3. Naya test: `tests/test5_button/test5_button.ino`
- Check-up button dabao: Serial pe "Check-up button daba!", beep, aur board LED toggle.
- BOOT dabao: Serial pe "BOOT daba".

## 4. Docs: `setup.txt`
- Button wiring (D27 aur GND).
- Naya section "GOOGLE SHEET DASHBOARD SETUP" (beta karega):
  1. Sheet banao.
  2. Apps Script me 2 files paste karo.
  3. `setup()` chalao aur dono keys note karo.
  4. Deploy > Web app: "Execute as: Me", "Who has access: Anyone".
  5. `/exec` URL aur `DEVICE_KEY` `v2.ino` me daalo.
  6. Dashboard link = URL + `?key=VIEW_KEY`, phone ki home screen pe save kar lo.
- Blynk me event `checkup_done` (notification ON).
- Maa-baap ke liye 4 line: "Probe bagal me 1-2 min, phir button dabao, ungli rakho, 2 beep ka wait karo".
- TEST CASES me test5 aur check-up ka expected output.

## 5. Diagram: `diagrams/step5_final_v2.png`
ESP32 box me D27 pin aur button (GND symbol ke saath) PIL script se jodna hai, jaise DS18B20 resistor joda tha. Connections list me `• Check-up button -> D27 aur GND`.

## Verification
1. **Compile:** scratch env me `arduino-cli compile` se `v2` aur `tests/test5_button` (libraries `scratchpad/user` me hain).
2. **Dashboard local preview:** `Dashboard.html` ko ~3 mahine ke nakli data ke saath scratchpad me ek normal HTML banake browser me kholo. Check karo:
   - normal, dhyan do aur khatra wale tiles
   - 24 ghante wala banner
   - charts ki saari ranges
   - phone width aur dark mode
3. **Apps Script (user deploy karega):** PowerShell se ek nakli POST bhejo (`Invoke-RestMethod` + `DEVICE_KEY`). Sheet me row aaye. Galat key pe row na aaye. Dashboard link khule, aur galat key pe "Link galat hai" aaye.
4. **Board pe:**
   - test5.
   - v2 check-up: ungli ke saath ~15-20 sec me result, 2 beep, Sheet me row, dashboard pe dikhe, Blynk notification aaye.
   - Bina ungli: 60 sec baad "Check-up nahi hua".
   - Router band karke check-up, phir chalu: row sahi purane time ke saath aaye.
   - Pending record ke saath board reset: reboot ke baad bhi upload ho.
   - Upload ke waqt OLED aur live readings atke nahi.
