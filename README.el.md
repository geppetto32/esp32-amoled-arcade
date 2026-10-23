[English](README.md) · **Ελληνικά**

<div align="center">

# esp32-amoled-arcade

**Ένα arcade τσέπης: 30+ παιχνίδια με χειρισμό μέσω κλίσης και αφής, μαζί με εργαλεία εργαστηρίου, σε μια πλακέτα ESP32-S3 με AMOLED 1.8".**

![ESP32-S3](https://img.shields.io/badge/ESP32--S3-Waveshare%20AMOLED%201.8-red)
![Arduino](https://img.shields.io/badge/Arduino-esp32%20core%203.3.10-00979D)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

<img src="docs/screenshots/launcher_2x.png" width="360" alt="Μενού εκκίνησης">

</div>

Μία μικρή πλακέτα, χωρίς να χρειάζεται κολλητήρι: οθόνη αφής AMOLED 368x448, αισθητήρας κλίσης, ηχείο, φορτιστής
μπαταρίας, RTC και υποδοχή micro-SD. Την κρατάς σαν φορητή κονσόλα, τη γέρνεις για να κατευθύνεις, χρησιμοποιείς τα δύο κουμπιά στο πάνω μέρος
και την οθόνη αφής. Παιχνίδια, εργαλεία ξυλουργικής και μια σελίδα ρυθμίσεων συνυπάρχουν σε ένα
firmware.

## Περιεχόμενα
[Δυνατότητες](#features) | [Υλικό](#hardware--what-to-buy) | [Παιχνίδια](#games-gallery) | [Εφαρμογές](#apps) | [Χειρισμός](#controls) | [Μεταγλώττιση και φλασάρισμα](#build-and-flash) | [Δομή](#project-layout) | [Περίβλημα](#wooden-case) | [Επόμενα βήματα](#roadmap) | [Συντελεστές](#credits)

<a id="features"></a>
## Δυνατότητες
- **32 παιχνίδια**: κλασικά (Snake, Tetris, Pacman, Breakout, Pinball), παιχνίδια με κλίση (Lander, Hourglass, Tank, Murmur), γρίφοι (2048, Sokoban, Minefield, Mahjong), επιτραπέζια και παιχνίδια ρυθμού (Four Row, Glow Beat), καθώς και ένα παιχνίδι εργαστηρίου (Cut Length).
- **8 εφαρμογές**: Level, Clock (χρονόμετρο και αντίστροφη μέτρηση), Metronome, Wood Calc, Dice, Counter, Torch, System.
- **Χειρισμός με κλίση** μέσω της IMU QMI8658, καθώς και με αφή, με προαιρετικό αναλογικό στικ ή χειριστήριο I2C.
- Λίστες με **κύλιση αφής** (σύρσιμο, γρήγορη σάρωση, ελαστική επαναφορά)· πατάς μια κάρτα για να την ανοίξεις.
- **Ρεκόρ στη μνήμη flash**, ενώ κάθε παιχνίδι αποθηκεύει τις δικές του επιλογές (θέμα, χειρισμός, δυσκολία).
- **Ρυθμίσεις**: ενεργοποίηση/απενεργοποίηση ήχου, ένταση, φωτεινότητα, μείωση φωτεινότητας σε αδράνεια (30 s / 60 s / 3 min / απενεργοποίηση), κατάσταση κάρτας SD, δοκιμή στικ, ρύθμιση κουμπιών, έλεγχος κλίσης.
- Λειτουργεί **χωρίς κάρτα SD**. Η κάρτα είναι προαιρετική.

<p align="center"><img src="docs/screenshots/settings_2x.png" width="280" alt="Ρυθμίσεις"></p>

<a id="hardware--what-to-buy"></a>
## Υλικό / τι να αγοράσεις
| Εξάρτημα | Σημειώσεις |
|---|---|
| **Waveshare ESP32-S3-Touch-AMOLED-1.8** (V1) | Διατίθεται στο [Grobotronics (Ελλάδα)](https://grobotronics.com/waveshare-esp32-s3-touch-amoled-display-1-8-368448.html). Τεκμηρίωση: [docs.waveshare.com](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8) |
| Μπαταρία LiPo (προαιρετική) | Χρησιμοποίησε την υποδοχή μπαταρίας της πλακέτας. Πριν τη συνδέσεις, έλεγξε την πολικότητα του βύσματος με βάση τις ενδείξεις πάνω στην πλακέτα: πολλές μπαταρίες έχουν τα καλώδια συνδεδεμένα ανάποδα |
| Κάρτα micro-SD (προαιρετική) | FAT32. **Αγόρασε γνήσια κάρτα από αξιόπιστο κατάστημα.** Κυκλοφορούν πλαστές κάρτες με παραπλανητικά μεγάλη χωρητικότητα: έλεγξε κάθε καινούργια με το [H2testw](https://www.heise.de/download/product/h2testw-50539) (Windows) ή το [F3](https://github.com/AltraMayor/f3) (Linux/macOS) |
| Αναλογικό στικ (προαιρετικό) | Ανταλλακτική μονάδα στικ Xbox One/Series (δύο ποτενσιόμετρα + διακόπτης πίεσης), συνδεδεμένη στη σειρά επαφών στην άκρη της πλακέτας (πίνακας παρακάτω) |
| Χειριστήριο I2C (προαιρετικό) | [Adafruit Mini I2C Gamepad (5743)](https://www.adafruit.com/product/5743) στις επαφές I2C (3V3, GND, SDA, SCL). Δεν έχει δοκιμαστεί σε πραγματικό υλικό |

Η πλακέτα κυκλοφορεί σε V1 και V2. Αυτό το firmware προορίζεται για τη **V1** (οθόνη SH8601 + αφή FT3168)· η Waveshare αναφέρει ότι η V1
έχει καταργηθεί και η V2 χρησιμοποιεί διαφορετικά ολοκληρωμένα (CO5300 + CST820), οπότε θα χρειαζόταν αλλαγές στους οδηγούς.

**Συνδεσμολογία αναλογικού στικ** (ανιχνεύεται κατά την εκκίνηση και τότε βαθμονομείται το κέντρο του, οπότε μην το αγγίζεις όσο ξεκινά):

| Ακροδέκτης μονάδας στικ | Επαφή πλακέτας |
|---|---|
| 3V3 (εξωτερικός ακροδέκτης και των δύο ποτενσιόμετρων) | 3V3 |
| GND (ο άλλος εξωτερικός ακροδέκτης και ο διακόπτης) | GND |
| Δρομέας X | IO17 |
| Δρομέας Y | IO18 |
| Διακόπτης πίεσης (η άλλη πλευρά στο GND) | IO38 |

Αν κάποια κατεύθυνση είναι ανάποδη, άλλαξε το `STK_INVERT_X`, το `STK_INVERT_Y` ή το `STK_SWAP_XY` στην αρχή της ενότητας του στικ στο
`firmware/AmoledArcade/hw.cpp`. Το SETTINGS -> JOYSTICK εμφανίζει δοκιμή σε πραγματικό χρόνο. Το στικ και το χειριστήριο δεν έχουν δοκιμαστεί σε πραγματικό υλικό.
Περισσότερες επιλογές (χειριστήριο BLE, USB host, ESP-NOW) εξετάζονται στο [CONTROLLER_OPTIONS.md](CONTROLLER_OPTIONS.md). Ακροδέκτες και διευθύνσεις I2C: [HARDWARE.md](HARDWARE.md).

<a id="games-gallery"></a>
## Συλλογή παιχνιδιών
Τα στιγμιότυπα είναι λήψεις της οθόνης σε κλίμακα 2x (οριζόντιος προσανατολισμός). Κάτω από κάθε παιχνίδι συνοψίζεται ο χειρισμός του· κάθε παιχνίδι έχει επίσης σελίδα επιλογών.

| | | |
|:--:|:--:|:--:|
| <img src="docs/screenshots/snake_2x.png" width="280" alt="Snake"><br>**Snake**<br>Το κλασικό φιδάκι με κλασικό, ρεαλιστικό και νέον θέμα.<br><sub>Κλίση ή κουμπιά</sub> | <img src="docs/screenshots/flap_2x.png" width="280" alt="Flap"><br>**Flap**<br>Πάτα για να περάσεις ανάμεσα στους σωλήνες.<br><sub>Πάτημα στην οθόνη / οποιοδήποτε κουμπί</sub> | <img src="docs/screenshots/breakout_2x.png" width="280" alt="Breakout"><br>**Breakout**<br>Κράτα την μπάλα στο παιχνίδι και σπάσε όλα τα τούβλα.<br><sub>Κλίση ή κουμπιά</sub> |
| <img src="docs/screenshots/astro_2x.png" width="280" alt="Astro"><br>**Astro**<br>Απόφυγε και ανατίναξε αστεροειδείς.<br><sub>Κλίση για κίνηση, κουμπιά για πυρά</sub> | <img src="docs/screenshots/stacker_2x.png" width="280" alt="Stacker"><br>**Stacker**<br>Ρίξε τουβλάκια και χτίσε έναν πύργο.<br><sub>Οποιοδήποτε κουμπί / πάτημα στην οθόνη</sub> | <img src="docs/screenshots/duck_2x.png" width="280" alt="Duck"><br>**Duck**<br>Κυνήγι πάπιας: σημάδεψε και πυροβόλησε τις πάπιες.<br><sub>Κλίση ή κουμπιά για στόχευση, πάτημα στην οθόνη για βολή</sub> |
| <img src="docs/screenshots/maze_2x.png" width="280" alt="Maze"><br>**Maze**<br>Κύλησε την μπάλα μέχρι τον στόχο.<br><sub>Κλίση</sub> | <img src="docs/screenshots/runner_2x.png" width="280" alt="Runner"><br>**Runner**<br>Πήδα πάνω από τους κάκτους και τρέξε όσο αντέχεις.<br><sub>Πάτημα στην οθόνη / κουμπί</sub> | <img src="docs/screenshots/reflex_2x.png" width="280" alt="Reflex"><br>**Reflex**<br>Πάτα την πλευρά που ανάβει.<br><sub>LEFT / RIGHT</sub> |
| <img src="docs/screenshots/pacman_2x.png" width="280" alt="Pacman"><br>**Pacman**<br>Φάε τις τελείες και απόφυγε τα φαντάσματα.<br><sub>Κλίση ή κουμπιά</sub> | <img src="docs/screenshots/tetris_2x.png" width="280" alt="Tetris"><br>**Tetris**<br>Αποθήκευση κομματιού, προεπισκόπηση θέσης, άμεση πτώση, επίπεδα.<br><sub>Κλίση ή κουμπιά: κίνηση, περιστροφή, πτώση</sub> | <img src="docs/screenshots/mahjong_2x.png" width="280" alt="Mahjong"><br>**Mahjong**<br>Πασιέντζα με 82 πλακίδια, πάντα με λύση.<br><sub>Κλίση ή LEFT/RIGHT· υπόδειξη, αναίρεση, ανακάτεμα</sub> |
| <img src="docs/screenshots/cut_length_2x.png" width="280" alt="Cut Length"><br>**Cut Length**<br>Σωστός συγχρονισμός στο εργαστήριο: κόψε τη σανίδα στο ζητούμενο μήκος.<br><sub>Κράτημα LEFT/RIGHT για μετακίνηση πριονιού, και τα δύο = κοπή</sub> | <img src="docs/screenshots/missile_2x.png" width="280" alt="Missile"><br>**Missile**<br>Missile Command: υπερασπίσου τις πόλεις.<br><sub>Κλίση για στόχευση, LEFT/RIGHT για εκτόξευση από τα σιλό</sub> | <img src="docs/screenshots/lander_2x.png" width="280" alt="Lander"><br>**Lander**<br>Προσεδαφίσου ομαλά σε μια βάση: στη Σελήνη, στον Άρη ή στην Ιώ.<br><sub>Κλίση για περιστροφή, κράτημα LEFT/RIGHT = ώθηση</sub> |
| <img src="docs/screenshots/2048_2x.png" width="280" alt="2048"><br>**2048**<br>Σύρε και ένωσε πλακίδια (3x3 έως 5x5).<br><sub>Γρήγορη σάρωση ή κλίση· LEFT αναίρεση, RIGHT επανεκκίνηση</sub> | <img src="docs/screenshots/hourglass_2x.png" width="280" alt="Hourglass"><br>**Hourglass**<br>Παιχνίδι προσομοίωσης υλικών: άμμος, νερό, λάδι, φωτιά, πέτρα, ξύλο.<br><sub>Κλίση = βαρύτητα, κράτημα LEFT για ροή υλικού, RIGHT επόμενο υλικό</sub> | <img src="docs/screenshots/duo_pong_2x.png" width="280" alt="Duo Pong"><br>**Duo Pong**<br>Δύο παίκτες, μία πλακέτα, νίκη στους 7 (ή εναντίον CPU).<br><sub>Κράτα το κουμπί σου: η ρακέτα ανεβαίνει</sub> |
| <img src="docs/screenshots/echo_2x.png" width="280" alt="Echo"><br>**Echo**<br>Simon με κλίση: επανάλαβε τη φωτεινή ακολουθία που μεγαλώνει.<br><sub>Γείρε προς ένα πεδίο και πάτα ένα κουμπί</sub> | <img src="docs/screenshots/four_row_2x.png" width="280" alt="Four Row"><br>**Four Row**<br>Connect Four εναντίον CPU με τρία επίπεδα δυσκολίας.<br><sub>LEFT/RIGHT για κίνηση, και τα δύο για ρίψη</sub> | <img src="docs/screenshots/glow_beat_2x.png" width="280" alt="Glow Beat"><br>**Glow Beat**<br>Παιχνίδι ρυθμού με τρεις λωρίδες: εσύ παίζεις τη μελωδία.<br><sub>LEFT / RIGHT / και τα δύο = μεσαία λωρίδα</sub> |
| <img src="docs/screenshots/tilt_golf_2x.png" width="280" alt="Tilt Golf"><br>**Tilt Golf**<br>Εννέα τρύπες μίνι γκολφ με άμμο, νερό και πλαγιές.<br><sub>Κλίση για στόχευση, κράτημα LEFT = δύναμη, απελευθέρωση = βολή</sub> | <img src="docs/screenshots/hop_2x.png" width="280" alt="Hop"><br>**Hop**<br>Frogger σε δρόμο και ποτάμι με νέον χρώματα.<br><sub>Τίναξε ελαφρά την πλακέτα για να πηδήξεις</sub> | <img src="docs/screenshots/lights_out_2x.png" width="280" alt="Lights Out"><br>**Lights Out**<br>Άλλαζε την κατάσταση των φώτων μέχρι να σβήσουν όλα, από 4x4 έως 6x6.<br><sub>Κλίση για κίνηση δείκτη, RIGHT για εναλλαγή, LEFT για υπόδειξη</sub> |
| <img src="docs/screenshots/minefield_2x.png" width="280" alt="Minefield"><br>**Minefield**<br>Ναρκαλιευτής με δείκτη που κινείται με κλίση.<br><sub>Κλίση, RIGHT αποκάλυψη, LEFT σημαιάκι</sub> | <img src="docs/screenshots/murmur_2x.png" width="280" alt="Murmur"><br>**Murmur**<br>Ένα χαλαρωτικό σμήνος από 300 ψαρόνια πάνω από το ηλιοβασίλεμα.<br><sub>Κλίση = φως, κράτημα LEFT για συγκέντρωση, RIGHT για διασπορά</sub> | <img src="docs/screenshots/pinball_2x.png" width="280" alt="Pinball"><br>**Pinball**<br>Ένα πλήρες τραπέζι φλίπερ: προσκρουστήρες, ράμπα, πολλές μπάλες ταυτόχρονα.<br><sub>LEFT/RIGHT για πτερύγια, κούνημα = σπρώξιμο</sub> |
| <img src="docs/screenshots/ring_breaker_2x.png" width="280" alt="Ring Breaker"><br>**Ring Breaker**<br>Κυκλικό Arkanoid με βαρυτικό πηγάδι.<br><sub>Κλίση για περιστροφή ρακέτας, και τα δύο = εκτόξευση</sub> | <img src="docs/screenshots/sokoban_2x.png" width="280" alt="Sokoban"><br>**Sokoban**<br>Σπρώξε τα κιβώτια, 24 επίπεδα.<br><sub>Κλίση για βήματα, LEFT αναίρεση, RIGHT επιλογή επιπέδου</sub> | <img src="docs/screenshots/swarm_2x.png" width="280" alt="Swarm"><br>**Swarm**<br>Σχηματισμοί και επιθέσεις εφόρμησης σε στιλ Galaga.<br><sub>Κλίση για κίνηση, κράτημα κουμπιού = πυρά</sub> |
| <img src="docs/screenshots/tank_2x.png" width="280" alt="Tank"><br>**Tank**<br>Ενυδρείο: τάισε, χάιδεψε, γείρε για να αλλάξεις το ρεύμα.<br><sub>Κλίση, πάτημα στην οθόνη για τάισμα, LEFT επιλογή τροφής</sub> | <img src="docs/screenshots/wobble_2x.png" width="280" alt="Wobble"><br>**Wobble**<br>Βοήθησε έναν σχοινοβάτη να ισορροπήσει πάνω από ένα φαράγγι.<br><sub>Κλίση για ισορροπία, LEFT/RIGHT για βήματα</sub> |  |

<a id="apps"></a>
## Εφαρμογές
Ανοίγουν από την κάρτα APPS στην κύρια λίστα. Η φωτεινότητα της οθόνης δεν μειώνεται λόγω αδράνειας όσο είναι ανοιχτό το Level ή λειτουργεί χρονόμετρο, αντίστροφη μέτρηση ή μετρονόμος.

| | | |
|:--:|:--:|:--:|
| <img src="docs/screenshots/level_2x.png" width="280" alt="Level"><br>**Level**<br>Αλφάδι φυσαλίδας και γωνιόμετρο (0.1 μοίρες).<br><sub>LEFT πάγωμα ένδειξης, RIGHT μηδενισμός</sub> | <img src="docs/screenshots/clock_2x.png" width="280" alt="Clock"><br>**Clock**<br>Ώρα, χρονόμετρο και αντίστροφη μέτρηση.<br><sub>LEFT/RIGHT, πάτημα στις καρτέλες</sub> | <img src="docs/screenshots/metronome_2x.png" width="280" alt="Metronome"><br>**Metronome**<br>30-240 BPM με τονισμένο χτύπο.<br><sub>LEFT/RIGHT -/+, πάτημα στην οθόνη = έναρξη/διακοπή</sub> |
| <img src="docs/screenshots/wood_calc_2x.png" width="280" alt="Wood Calc"><br>**Wood Calc**<br>Κλάσματα ιντσών/mm, όγκος σανίδας, λίστα κοπών.<br><sub>LEFT πεδίο, RIGHT +, κλίση +/-</sub> | <img src="docs/screenshots/dice_2x.png" width="280" alt="Dice"><br>**Dice**<br>1-6 ζάρια από d4 έως d20, κούνημα για ρίψη.<br><sub>Κούνημα, RIGHT ή πάτημα στην οθόνη</sub> | <img src="docs/screenshots/counter_2x.png" width="280" alt="Counter"><br>**Counter**<br>Τέσσερις μετρητές με αποθήκευση τιμών.<br><sub>RIGHT +1, LEFT -1</sub> |
| <img src="docs/screenshots/torch_2x.png" width="280" alt="Torch"><br>**Torch**<br>Λευκός, κόκκινος ή θερμός φωτισμός σε όλη την οθόνη.<br><sub>LEFT πιο χαμηλή φωτεινότητα, RIGHT πιο υψηλή</sub> | <img src="docs/screenshots/system_2x.png" width="280" alt="System"><br>**System**<br>Μπαταρία, μνήμη, SD, RTC, ζωντανές ενδείξεις IMU, χειριστήριο.<br><sub>RIGHT επανέλεγχος SD</sub> |  |

<a id="controls"></a>
## Χειρισμός
Κράτα την πλακέτα **οριζόντια, με τα δύο κουμπιά στο πάνω μέρος**.

| Ενέργεια | Τρόπος |
|---|---|
| LEFT / RIGHT | τα δύο πάνω κουμπιά (BOOT και PWR). Στην πρώτη εκκίνηση σου ζητά να πατήσεις το LEFT· μπορείς να επαναλάβεις τη ρύθμιση από το SETTINGS |
| Μενού εκκίνησης | LEFT = επόμενο, RIGHT = άνοιγμα / επιλογή, ή πάτημα σε κάρτα· σύρσιμο για κύλιση |
| **Αρχική** (επιστροφή στο μενού εκκίνησης) | **τριπλό πάτημα BOOT** μέσα σε 0.8 s, **ή κράτημα της πάνω αριστερής γωνίας** της οθόνης για 1.5 s, ή START+SELECT στο χειριστήριο I2C |
| Κλίση | κατευθύνει τα περισσότερα παιχνίδια· το στικ λειτουργεί ως ισοδύναμη εναλλακτική (πλήρης εκτροπή = 0.7 g) |

Το παρατεταμένο πάτημα κουμπιού δεν σε βγάζει ποτέ από το παιχνίδι, οπότε μπορεί να χρησιμοποιείται ελεύθερα στον χειρισμό. Στα παιχνίδια όπου τα γρήγορα επαναλαμβανόμενα πατήματα είναι συνηθισμένα
(Flap, Snake, Breakout, Astro, Pacman, Tetris, Duck, Missile, Counter, χρονόμετρο σε λειτουργία), το τριπλό πάτημα αγνοείται:
χρησιμοποίησε το μενού παύσης / την οθόνη τέλους παιχνιδιού (LEFT), τη γωνία της οθόνης αφής ή το χειριστήριο.

<a id="build-and-flash"></a>
## Μεταγλώττιση και φλασάρισμα
**Windows, Arduino IDE 2.x.**

1. Εγκατάστησε το **esp32 core 3.3.10** (Boards Manager, πρόσθεσε το `https://espressif.github.io/arduino-esp32/package_esp32_index.json`).
2. Εγκατάστησε τη βιβλιοθήκη **GFX Library for Arduino 1.6.4** (Arduino_GFX από moononournation).
3. **Διόρθωση για το core 3.3.10:** το core άλλαξε την υπογραφή της `spiFrequencyToClockDiv` (πλέον δέχεται τον δίαυλο SPI ως πρώτο όρισμα). Στα αρχεία `src/databus/Arduino_ESP32SPI.cpp` και `src/databus/Arduino_ESP32SPIDMA.cpp` της βιβλιοθήκης, βάλε τις κλήσεις μέσα στον εξής έλεγχο έκδοσης:
   ```cpp
   #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
     _div = spiFrequencyToClockDiv(&_spi_bus_array[_spi_num], _speed);   // new signature
   #else
     _div = spiFrequencyToClockDiv(_speed);
   #endif
   ```
   (το ίδιο ισχύει για την άλλη κλήση της `spiFrequencyToClockDiv` στο `Arduino_ESP32SPI.cpp`, που περνά το `_spi`). Στο `Arduino_ESP32SPIDMA.cpp`, η διαδρομή ESP-IDF ρυθμίζει μόνη της το ρολόι, οπότε εκεί αρκεί το `_div = 1` στο core 3.3+.
4. Άνοιξε το `firmware\AmoledArcade\AmoledArcade.ino` και όρισε: πλακέτα **Waveshare ESP32-S3-Touch-AMOLED-1.8**, **PSRAM: Enabled**, **USB CDC On Boot: Disabled**, USB Mode: Hardware CDC and JTAG.

**Γραμμή εντολών:** το `firmware\AmoledArcade\flash.cmd [COMx]` (προεπιλογή COM6) κάνει μεταγλώττιση με το arduino-cli και εγγραφή firmware σε δύο βήματα:
πρώτα βάζει το ολοκληρωμένο σε λειτουργία bootloader (`chip-id`) και μετά γράφει τον bootloader, τον πίνακα κατατμήσεων και την εφαρμογή. Μια απλή μεταφόρτωση μερικές φορές
αποτυγχάνει στη μέση ενώ τρέχει κάποιο παιχνίδι· τα δύο βήματα το αποφεύγουν.

```
arduino-cli compile -b esp32:esp32:waveshare_esp32_s3_touch_amoled_18:PSRAM=enabled,CDCOnBoot=default,USBMode=hwcdc firmware\AmoledArcade
```

**Δεν συνδέεται;** Αποσύνδεσε το USB, **κράτα πατημένο το BOOT ενώ το συνδέεις ξανά**, άφησέ το και επανάλαβε το φλασάρισμα. Μετά από εγγραφή σε χειροκίνητη λειτουργία λήψης,
αποσύνδεσε και σύνδεσε ξανά κανονικά (χωρίς BOOT) για να τρέξει το sketch. Το άδειο παράθυρο σειριακής παρακολούθησης δεν σημαίνει αποτυχία: η κατάσταση εμφανίζεται στην οθόνη.

<a id="project-layout"></a>
## Δομή έργου
| Διαδρομή | Περιεχόμενο |
|---|---|
| `firmware/AmoledArcade/` | Το firmware του arcade: μενού εκκίνησης, παιχνίδια (`*_game.cpp`), εφαρμογές (`app_*.cpp`), βοηθητικές λειτουργίες `engine.h`, επίπεδο πρόσβασης στο υλικό `hw.cpp`, `flash.cmd` |
| `firmware/AmoledSnake/` | Πρώιμη έκδοση με ένα παιχνίδι (μόνο Snake) για την ίδια πλακέτα |
| `firmware/PlayDevice/` | Παλαιότερη έκδοση για LilyGO T-Display-S3 με φυσικά κουμπιά (Snake, Tetris, Breakout, Pong, Invaders) |
| `firmware/tests/` | Δοκιμές λογικής στον υπολογιστή (χωρίς πλακέτα) |
| `games/html_prototypes/` | Πρωτότυπο σε πρόγραμμα περιήγησης για δοκιμή του παιχνιδιού σε PC |
| `case/` | Script για ξύλινο περίβλημα στο Fusion (δες παρακάτω) |
| `HARDWARE.md`, `CONTROLLER_OPTIONS.md` | Ακροδέκτες και εξαρτήματα· έρευνα για εξωτερικά χειριστήρια |
| `IDEAS.md`, `GAME_IDEAS_30.md` | Σημειώσεις σχεδιασμού και ιδέες |
| `tools/capture.py` | Καταγράφει τα στιγμιότυπα μέσω σειριακής σύνδεσης USB: `uv run --with pyserial --with pillow tools/capture.py --port COM6 --out docs/screenshots` |
| `docs/screenshots/` | Στιγμιότυπα που χρησιμοποιούνται σε αυτό το README |

<a id="wooden-case"></a>
## Ξύλινο περίβλημα
Το `case/fusion/PlayDeviceCase/` είναι ένα script για το Fusion που δημιουργεί μια ξύλινη φορητή κονσόλα (μπροστινή πλάκα, πλαίσιο, πίσω πλάκα, καλύμματα κουμπιών, αποστάτες)
και εξάγει περιγράμματα DXF για λέιζερ ή CNC στο `case/dxf/` (παράγονται αυτόματα και δεν αποθηκεύονται στο repo). **Σημείωση:** αυτή η πρώτη έκδοση προορίζεται για το
παλαιότερο σχέδιο με LilyGO (κατακόρυφος προσανατολισμός, οκτώ κουμπιά), όχι για αυτή την πλακέτα AMOLED, και δεν έχει εκτελεστεί ακόμη στο Fusion.

### Λαβή για ένα χέρι (v1, υπό σχεδιασμό)
> **Δεν έχει ολοκληρωθεί:** είναι ένα πρώτο σχέδιο. Θα αλλάξει ακόμα και δεν έχει κατασκευαστεί ούτε δοκιμαστεί λειτουργική λαβή.

Μια ξύλινη λαβή για τη γυμνή πλακέτα AMOLED και ένα αναλογικό στικ τύπου Xbox, που δημιουργείται στο Fusion από το `case/fusion/hh_grip.py`
(σημειώσεις σχεδιασμού: `case/fusion/grip_design.md`). Δεξί χέρι: ο αντίχειρας στο στικ, ο δείκτης στα δύο πάνω κουμπιά, η μπαταρία μέσα στη
λαβή της παλάμης. Τέσσερις επάλληλες στρώσεις (λέιζερ + CNC), περίπου 92 x 95 x 21 mm. Το στικ και η μπαταρία είναι ακόμη ενδεικτικά μοντέλα: μέτρησε τα δικά σου
πριν κόψεις.

<p><img src="docs/grip/grip_iso.png" width="420" alt="λαβή για ένα χέρι, ισομετρική όψη"> <img src="docs/grip/grip_top.png" width="360" alt="λαβή για ένα χέρι, κάτοψη"></p>

Το αρχείο STEP και το ηλεκτρονικό σχηματικό της Waveshare δεν αναδημοσιεύονται εδώ:
θα τα βρεις στην [επίσημη τεκμηρίωση](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8).

<a id="roadmap"></a>
## Επόμενα βήματα
- Περισσότερα παιχνίδια από το [GAME_IDEAS_30.md](GAME_IDEAS_30.md): Marble Run, Sudoku, Nonogram, Checkers, Othello, Chess, Klondike, Artillery, Sink, Sand the Plank. Παιχνίδι δύο παικτών μέσω ESP-NOW.
- Περιεχόμενο SD: επιπλέον επίπεδα Sokoban, αντίγραφα των σκορ στην κάρτα, προβολή φωτογραφιών και αναπαραγωγή μουσικής.
- Wi-Fi: συγχρονισμός ώρας NTP, οθόνες καιρού και κατάστασης· ενημερώσεις OTA (χρειάζεται νέος πίνακας κατατμήσεων).
- Υλικό: η λαβή για ένα χέρι με αναλογικό στικ, RTC με εφεδρική τροφοδοσία από μπαταρία, πειραματική υλοποίηση εξομοιωτή Game Boy.
- Περισσότερα στο [IDEAS.md](IDEAS.md) (πρώιμες σημειώσεις σχεδιασμού: ορισμένα έχουν ήδη υλοποιηθεί).

<a id="credits"></a>
## Συντελεστές
- Πλακέτα και παραδείγματα: [Waveshare](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8) (τεχνική αναφορά υλικού και επίσημα παραδείγματα).
- `es8311.c`, `es8311.h`, `es8311_reg.h`: οδηγός κωδικοποιητή/αποκωδικοποιητή ήχου ES8311, Copyright Espressif Systems (Shanghai) CO LTD, Apache-2.0. Δες το [THIRD_PARTY.md](THIRD_PARTY.md).
- [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) από moononournation: βιβλιοθήκη οδήγησης οθόνης (δεν περιλαμβάνεται).
- Δημιουργήθηκε με βοήθεια από το [Claude](https://claude.com/claude-code).

## Άδεια χρήσης
MIT για τον κώδικα αυτού του αποθετηρίου, δες το [LICENSE](LICENSE). Τα ενσωματωμένα αρχεία τρίτων διατηρούν τις δικές τους άδειες: [THIRD_PARTY.md](THIRD_PARTY.md).
