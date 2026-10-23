# AmoledArcade - 30 new game designs (design only, nothing built)

> Status: design notes written before building. Most of these are now built: see the README for the current list. Still open: Marble Run, Sudoku, Nonogram, Checkers, Othello, Chess, Klondike, Artillery, Sink, Sand the Plank.

Screen 448x368 landscape, true black, glow colours. Buttons: LEFT/RIGHT on top edge (press + hold). Every game is fully playable with buttons + tilt; touch is optional unless noted.
Effort: S = an evening, M = few evenings, L = weeks. "Tune" = tune-player helper, "SD" = SD content. Hardware flags: [ESPNOW] unproven, [GYRO] gyro not yet enabled (accel tilt is proven), [SD] working but new for games.
Not repeated: Snake, Flap, Breakout, Astro, Stacker, Duck, Maze, Runner, Reflex, Pac-Man, Tetris, Mahjong. IDEAS.md's marble labyrinth/Pong/Invaders/Simon/2048/Minesweeper/pinball/sand/duel are used here only where improved (marked "vs IDEAS").

## A. Tilt arcade classics

### 1. Lander - lunar lander - S/M
Pitch: Guide a glowing lander onto a flat pad on a procedural mountain range; fuel is limited, gravity per planet, soft touchdown scores by speed and angle.
Controls: tilt roll rotates the ship; hold LEFT or RIGHT = main thrust (both = full burn); touch tap = thrust.
Fit: tilt-as-stick feels like tilting a real craft; thrust flame particles glow on black.
Look/sound: vector outline ship, neon terrain line, particle exhaust; low rumble beep scaling with thrust, chime on landing.
Curve/hook: Moon, Mars, Io (stronger gravity, wind gusts); pad shrinks, fuel drops; best-fuel-left score per planet on SD/NVS.
Risk: none; needs particle helper (cheap).

### 2. Missile Command - defence - S
Pitch: Cities at the bottom, missile trails rain from the top; detonate counter-blasts that chain-react.
Controls: tilt moves the crosshair (smoothed); LEFT fires from the left silo, RIGHT from the right silo (each has 8 shots); touch tap fires from nearest silo at the point.
Fit: two buttons = two silos, a perfect mapping; bright expanding blast circles.
Look/sound: thin neon trails, additive-looking rings (fade palette), descending whoosh, boom tune on city loss.
Curve/hook: waves add MIRVs, planes, smart bombs; bonus for surviving cities; endless with wave counter.
Risk: none.

### 3. Hop - Frogger variant - S/M
Pitch: Cross traffic lanes and a log river sideways (landscape: travel left-to-right along the long axis, 448 px of road).
Controls: tilt up/down picks lane (snapped); LEFT = hop forward, RIGHT = hop back; hold = ride the log without hopping.
Fit: tilt lane-select is far easier than a D-pad with two buttons; long axis gives room for many lanes.
Look/sound: neon cars as rectangles/rounded blobs, pulsing lily pads; tiny hop blip, splash noise.
Curve/hook: speed and lane count grow, turtles that dive, 5 home bays; a run is 3 lives.
Risk: simple rect sprites only (S), optional frog sprite 12x12.

### 4. Swarm - Galaga-style - M
Pitch: Formations fly in on curves, dive-bomb in loops; a tractor beam can capture your ship and a rescue gives double fire. Differs from Astro (free-space asteroids): this is fixed-formation with choreographed attack paths.
Controls: tilt moves along the bottom; hold LEFT/RIGHT = rapid fire (auto-repeat), tap both = bomb (limited).
Fit: tilt fine-pitch positioning; many bright enemies glow; wave bursts look great on AMOLED.
Look/sound: neon geometric bugs (10 shapes in code, no art), starfield; 3-note jingles, arpeggio on wave clear.
Curve/hook: Challenge stages every 4 waves with perfect-bonus; high-score table on SD.
Risk: enemy path data (bezier tables) take time; sprites optional.

### 5. Ring Breaker - Arkanoid variant - M
Pitch: Bricks form concentric rings around the screen centre; the paddle orbits the ring. Different from Breakout: the arena is circular, tilt angle = paddle angle, balls curve with gravity wells.
Controls: tilt left/right rotates the paddle round the arc; LEFT = shrink paddle for precision (hold) / RIGHT = launch and boost; touch drag orbits it.
Fit: tilt maps to angle intuitively; circular glow bricks, 60 fps rotation.
Look/sound: concentric colour bands, comet trail on ball; pitch rises with combo.
Curve/hook: ring count grows, powerups (multiball, laser), levels as polar tables on SD; replay via endless mode.
Risk: polar collision maths; 2D physics helper helps.

### 6. Pinball - flipper table - M
Pitch: One full table (bumpers, ramps, slingshots, kickers, drop targets), real ball physics. Improves IDEAS #2 with a proper table editor on SD.
Controls: LEFT/RIGHT = flippers (hold keeps raised); tilt = nudge (shake counts toward TILT); touch left/right halves = flippers.
Fit: two top buttons are literally flippers; held landscape is a pinball cabinet view rotated (table runs along the short axis, 368 px wide x scroll or use a compressed 448 x 368 table).
Look/sound: neon table lines, lit lanes, bumpers flash; square-wave bumper dings, ramp arpeggio.
Curve/hook: missions (light all lanes, 3 multiballs), 3 balls; score table.
Risk: ball tunnelling at 60 fps: substep physics 4x.

### 7. Marble Run - physics sandbox - M
Pitch: Place ramps, bumpers and funnels on a grid (touch or cursor), then tilt the board to launch marbles toward goals; unlocked puzzles from SD. Not the Maze: it is a build-and-simulate toy.
Controls: tilt cursor, LEFT = place/rotate piece, RIGHT = run/reset; hold LEFT = cycle piece; touch to place pieces.
Fit: tilt = gravity direction in sim; glowing marbles leave trails.
Look/sound: beads of colour on thin tracks, soft bell notes per goal.
Curve/hook: 30 puzzles with piece budgets; sandbox share via SD files; star ratings.
Risk: stable circle-segment physics.

### 8. Tilt Golf - golf - M
Pitch: Top-down holes with sand, water, slopes; you tilt to aim and read slopes with the real IMU feel (slope zones push the ball).
Controls: tilt sets aim direction; hold LEFT = charge power, release = shoot; RIGHT = mulligan/next hole; touch drag to aim, release to shoot.
Fit: aim by tilt is the real golf feeling; slope zones are rendered as glowing gradients.
Look/sound: green glow tiles, ball trail, thwack beep, hole chime.
Curve/hook: 18 holes in packs (SD course files), par scoring, 9-hole daily course seeded by RTC date.
Risk: course loader from SD (benefits from loader helper).

## B. Toys, zen, simulation

### 9. Hourglass - falling sand - M
Pitch: A real falling-sand sim (sand, water, oil, fire, stone) whose gravity is the tilt direction. Improves IDEAS #7 with element select and a "fill the glass" goal mode.
Controls: tilt = gravity; hold LEFT = pour selected element at cursor, RIGHT = cycle element; touch to paint.
Fit: cheap showpiece of IMU + AMOLED (grains glow); 224x184 half-res grid at 60 fps feasible.
Look/sound: coloured grain per element, fire glow, soft hiss on pour.
Curve/hook: zen mode + 12 goals ("flood the lower half", "burn the wood"); share screenshot to SD.
Risk: CPU at full res; run at half-res with 2x blit.

### 10. Tank - fish aquarium - S/M
Pitch: A glowing aquarium: fish, jellyfish, plankton; tilt makes water slosh and fish react, shake = shake-off scare, hold to feed. Idle app / game hybrid that runs all day on AMOLED.
Controls: tilt = current; LEFT = drop food, RIGHT = change species; touch to pet.
Fit: AMOLED blacks make bioluminescent creatures glow; RTC makes fish sleep at night and feed times matter.
Look/sound: bioluminescent blobs, bubbles; quiet bubble blips, optional.
Curve/hook: grow a collection of species over days, RTC-based breeding/unlocks, state in SD save.
Risk: needs some animated sprites (6 fish bodies as procedural shapes first).

### 11. Murmuration - boids flock toy - M (bold)
Pitch: 300 glowing starlings flock on black. Tilt acts as wind, a finger/hold is a predator, goal mode: herd the flock through hoops. Only makes sense with tilt + AMOLED.
Controls: tilt = wind vector; hold LEFT = attract, hold RIGHT = scatter; touch = predator.
Fit: 300 boids in 60 fps on S3 with spatial grid; huge glow trails look stunning on black.
Look/sound: comet-trail birds, colour by speed; breathy whoosh pitch follows flock density.
Curve/hook: 15 herding levels (hoops, obstacles), sandbox with tweakable cohesion; screensaver mode.
Risk: boid perf tuning only.

### 12. Wobble - tightrope balance - S/M (bold)
Pitch: Walk a glowing tightrope over a canyon: the balance pole angle is fed by real gyro rate, so you must counter-tilt like balancing a stick. Pure inverted-pendulum feel.
Controls: tilt/rate balances the pole; hold LEFT/RIGHT = step forward (and sway), both = pause; touch not used.
Fit: needs the gyro for low-latency rate input [GYRO]; falls back to accel angle with a harder feel.
Look/sound: thin cable, glowing balancer with pole, gusts as particle wind; creaking low tone rises with imbalance.
Curve/hook: gusts, birds, longer spans; distance score, daily challenge seeded by date.
Risk: [GYRO] enabling QMI8658 gyro unproven in the arcade.

## C. Puzzle

### 13. 2048 - slide puzzle - S
Pitch: 4x4 merge puzzle; tilt-flick or swipe. Improves IDEAS #9 with a tilt-flick gesture and an undo.
Controls: tilt flick in four directions (past threshold and return) = move; LEFT = undo, RIGHT = restart; touch swipe = move.
Fit: tilt-flick is like tilting a box of tiles; big glowing tile colours.
Look/sound: neon tile gradient 2..2048, pop animation; rising blip on merge.
Curve/hook: best tile + score; variants 3x3/5x5, time attack.
Risk: gesture tuning to avoid accidental moves.

### 14. Sokoban - box pusher - M (SD levels)
Pitch: Classic push-the-crate puzzles with levels loaded from SD text files (community packs).
Controls: tilt = move one step in the dominant direction (hold LEFT = lock axis), LEFT = undo, RIGHT = restart; touch tap a tile to path-walk to it.
Fit: SD proves level loader; touch pathing is great; tiles glow.
Look/sound: neon crates, goal tiles pulse; scrape tick, level jingle.
Curve/hook: 100+ levels in packs, move/push count stored per level on SD.
Risk: needs level loader helper and 8x8 tile art (code-drawn shapes OK).

### 15. Minefield - Minesweeper - M
Pitch: 16x12 minesweeper sized for landscape. Improves IDEAS #10 with a tilt cursor and hold-to-flag.
Controls: tilt moves the cursor; LEFT = reveal (hold = chord), RIGHT = flag; touch tap = reveal, long press = flag.
Fit: touch is quick, tilt/buttons fully playable; neon numerals.
Look/sound: glowing numbers per colour, explosion on mine; tick tune.
Curve/hook: 3 sizes, safe first move, best times per size on SD.
Risk: none.

### 16. Lights Out - toggle puzzle - S
Pitch: 5x5 grid of lamps; pressing flips a cross; find the minimum solution. Procedurally generated levels with guaranteed solvability.
Controls: tilt cursor (snapped); LEFT = toggle, RIGHT = hint (costs a star); touch tap = toggle.
Fit: glowing lamps are AMOLED-perfect.
Look/sound: warm glow squares with bloom, soft click, chord on win.
Curve/hook: stars for par moves, 99 seeded puzzles, 7x7 expert.
Risk: none; good first puzzle.

### 17. Sudoku - number puzzle - M
Pitch: 9x9 Sudoku with pencil marks, generated or loaded from SD. Quiet, zen.
Controls: tilt = cursor, LEFT = cycle digit up (hold = fast), RIGHT = place digit / hold = pencil mode; touch digit pad optional.
Fit: touch pad is faster, buttons make it playable offline; dark theme saves battery.
Look/sound: thin grid, gold numbers; soft tick, completion tune.
Curve/hook: 4 difficulties, daily puzzle from RTC date, save in progress to SD.
Risk: generator + difficulty rating (use puzzle files from SD first).

### 18. Nonogram - picture logic - M (SD)
Pitch: Fill squares based on row/column clues to reveal a picture (Picross), pictures stored as SD bitmaps.
Controls: tilt cursor; LEFT = fill, RIGHT = mark X; hold = drag line; touch drag to fill.
Fit: AMOLED shows the revealed picture glowing; touch drag is natural.
Look/sound: neon clue numbers, revealed image fades in colour; marching blips.
Curve/hook: 5x5 up to 15x15 puzzles, 60 pictures from SD, completion gallery.
Risk: puzzle pack authoring (a PC script generating clues from PNG).

### 19. Echo - tilt Simon - S
Pitch: Four coloured quadrants light in a growing sequence; repeat it by tilting toward the quadrant. Variation on IDEAS #8: tilt direction picks the pad, not touch.
Controls: tilt toward pad then LEFT/RIGHT = confirm (either button); touch tap alternative.
Fit: tilt = four big zones; strong tone per pad via ES8311.
Look/sound: four glowing sectors, each with its own pitch; fail buzz.
Curve/hook: speed increases, reverse mode, 2-player pass-around mode, best streak.
Risk: none.

## D. Rhythm and music

### 20. Glow Beat - rhythm - M
Pitch: Notes slide down three lanes (left button, right button, tilt-flick for the middle) in time to a square-wave chiptune.
Controls: LEFT, RIGHT, tilt flick up = middle lane; hold notes with hold; touch lanes optional.
Fit: buttons are very low latency; AMOLED neon notes; tilt for the third lane.
Look/sound: neon lanes, judgement text, speaker plays the melody via tune player; hit/miss changes the tune dynamics.
Curve/hook: 6 songs and chart files on SD, calibrate latency screen, grade + combo.
Risk: speaker is tiny; charts need a simple authoring tool.

## E. Board and card vs CPU

### 21. Four Row - Connect Four - S/M
Pitch: Drop discs on a 7x6 board against a negamax CPU (3 levels).
Controls: tilt moves the column cursor, LEFT = drop, RIGHT = undo/hint; touch tap column.
Fit: simple board, quick sessions; glowing discs.
Look/sound: neon red/yellow discs, drop thunk, win line flashes.
Curve/hook: levels from easy random to depth 8; first-player toggle; win record.
Risk: none; first user of the search helper.

### 22. Checkers - draughts - M
Pitch: 8x8 checkers vs CPU with forced captures and kings.
Controls: tilt/cursor; LEFT = pick/drop, RIGHT = cancel; touch tap to pick and tap to move.
Fit: board fits 368 px; touch best, buttons OK.
Look/sound: neon pieces on dark squares, jump animation; click tones.
Curve/hook: 3 levels, alpha-beta depth 4-8, move hints, save game to SD.
Risk: CPU search time; yield-driven search helper.

### 23. Othello - reversi - M
Pitch: 8x8 disc flipping vs CPU with the classic evaluation (corners, mobility).
Controls: tilt cursor; LEFT = place, RIGHT = show legal moves/pass; touch tap.
Fit: glowing discs flip in animation; sessions short.
Look/sound: green/pearl discs, flip sweeps; rising tick as discs flip.
Curve/hook: 4 levels, undo, stats on SD.
Risk: none.

### 24. Chess Pocket - chess vs engine - L
Pitch: Full chess against a small engine (sunfish-style, depth 4-5) with opening book from SD.
Controls: tilt cursor; LEFT = select, RIGHT = confirm/undo; touch tap-tap.
Fit: 8x8 board at 46 px squares; screen fine; PSRAM for transposition table.
Look/sound: neon wireframe pieces (7 glyphs); move click, check ping.
Curve/hook: 5 levels (time per move), puzzle mode from SD ("mate in 2"), PGN save.
Risk: engine strength vs time on 240 MHz; sprites (6 piece glyphs 40x40) needed.

### 25. Klondike - solitaire - M/L
Pitch: Klondike with draw-1/draw-3, undo, auto-complete.
Controls: tilt = cursor jumping between piles, LEFT = pick/place, RIGHT = draw/flip; touch drag-and-drop is the best mode.
Fit: needs fine target selection; touch makes it playable; buttons slower but workable.
Look/sound: dark felt, neon card outlines; shuffle ticks.
Curve/hook: daily deal seeded by RTC date, win streak, move counter; vegas-style scoring.
Risk: card face art (52 glyphs from suit shapes drawn by code - no bitmaps needed).

## F. Two players (hot-seat or ESP-NOW)

### 26. Duo Pong - Pong / air hockey - S (hot-seat) + M (ESP-NOW)
Pitch: Two paddles, a glowing puck; plays on one board (each player on one side). Improves IDEAS #4: two-player first, CPU optional.
Controls: one board - Player 1 tilts, Player 2 uses LEFT/RIGHT to move the paddle up/down (or touch drag); ESP-NOW - each board tilts its own paddle, LEFT = smash.
Fit: simple, frantic; glow trails; ESP-NOW 2-board is the showcase [ESPNOW].
Look/sound: neon paddles, ball trail, pitch rises with rally.
Curve/hook: first to 7, power-ups, CPU levels; scoring on SD.
Risk: [ESPNOW] unproven, needs a pairing handshake.

### 27. Artillery - Scorched-Earth tanks - M
Pitch: Two tanks on destructible terrain, wind changes, three weapon types. Hot-seat (pass the board) or ESP-NOW.
Controls: tilt = barrel angle, hold LEFT = charge power, release fires; RIGHT = switch weapon; touch drag aim.
Fit: tilt angle matches barrel angle; terrain crater destruction glowing; passes board easily.
Look/sound: neon terrain line, explosive particle rings; whistle pitch follows shell.
Curve/hook: 5 rounds, money/weapons, terrain seeded by date for "daily duel"; CPU opponent.
Risk: pass-the-board hot-seat needs a "turn blank" screen; [ESPNOW] optional.

### 28. Sink - Battleship - M
Pitch: Hidden fleets on a 10x10 grid; ESP-NOW between two boards (hidden info works only with two screens), or vs CPU.
Controls: tilt cursor; LEFT = fire, RIGHT = rotate ship in placing phase; touch tap cell.
Fit: radar-style glow sweep; two boards are natural.
Look/sound: green radar grid, sonar ping, splash/explosion noises.
Curve/hook: CPU levels (probability density hunting), salvo variant; stats on SD.
Risk: [ESPNOW] for the real two-player mode; CPU-only mode has no risk.

## G. Workshop-themed (woodworker)

### 29. Cut to Length - timing game - S
Pitch: A saw travels over a board with a ruler; hit the button when the pencil mark (a target mm or fraction like 7-3/8 in) passes under the blade. Teaches reading fractions and the "measure twice" feel.
Controls: hold LEFT or RIGHT = saw glides along the board, press both = cut (tilt not used); touch tap = cut.
Fit: ruler graphics in mm/inch; local bright tick marks glow; zero risk; same units as the unit-converter app.
Look/sound: wood grain board (code-drawn), saw blade, sawdust particles; zip noise and a "snap" on a cut.
Curve/hook: tolerance shrinks (1 mm to 0.1 mm), fractions down to 1/32, speed grows; streaks and error average on SD.
Risk: none.

### 30. Sand the Plank - tilt sanding - S/M
Pitch: Rock the sanding block back and forth with tilt to smooth a rough plank; grit changes (80 to 220) and over-sanding one spot makes a dip. Zen coverage game.
Controls: tilt left/right moves the block; LEFT = press (hold = harder), RIGHT = change grit; touch drag also works.
Fit: tilt rocking feels like sanding; coverage heat-map glows from rough (red) to smooth (gold).
Look/sound: plank texture map, sawdust particles, rhythmic scratch tone via noise bursts.
Curve/hook: timed level + smoothness percentage; ranks; wood species cards unlock.
Risk: coverage buffer only (RAM cheap).

## Ranked top 10 build order (fun x fit / effort, small first)

| # | Game | Why | Done when |
|---|---|---|---|
| 1 | Cut to Length (29) | S, woodworker value, zero risk | Playable with button cuts, fraction targets, tolerance shrinking and best streak saved |
| 2 | Missile Command (2) | S, buttons = silos, tilt crosshair | 10 waves playable at steady 40+ fps with city loss and wave bonuses |
| 3 | Lander (1) | S/M, tilt thrust is excellent, introduces particles | Land on pads on 3 planets with fuel, soft-landing scoring, best saved |
| 4 | 2048 (13) | S, gesture helper, instantly replayable | Tilt-flick moves without accidental double triggers, undo works |
| 5 | Hourglass (9) | M, strongest showpiece for IMU + AMOLED | 4 elements fall by tilt at 40+ fps, fill-the-glass goals work |
| 6 | Pinball (6) | M, flippers = buttons | One table with bumpers, ramps, 3 balls, flipper latency feels immediate |
| 7 | Four Row (21) | S/M, first CPU opponent, builds search helper | CPU beats a random player 20/20 and level 3 beats a casual human |
| 8 | Sokoban (14) | M, validates SD loader | 30 SD levels load, undo/restart, per-level best saved to SD |
| 9 | Murmuration (11) | M, bold AMOLED+tilt showcase | 300 boids run at 40+ fps, tilt wind + attract/scatter work, 5 herding goals |
| 10 | Glow Beat (20) | M, uses the speaker and buttons | 3 songs from SD play in sync, latency calibration screen, score + combo |

After these: Echo (19) and Lights Out (16) as quick wins; Duo Pong hot-seat (26); Tank (10); Wobble (12) once gyro is proven; Chess (24) and ESP-NOW modes last.

## Shared engine helpers (build once, many games cheaper)

| Helper | Used by | Notes |
|---|---|---|
| Tilt input (deadzone, smoothing, flick detector, 4-way snap, cursor mode) | 1-9,11,13-19,21-30 | accel proven; flick/shake = threshold on delta |
| Particle system (fixed pool 256, fade palette) | 1,2,4,6,9,27,29,30 | RGB565 has no alpha: precomputed 8-step fade palette per colour fakes glow |
| Tune player (non-blocking note queue, 2 channels via arpeggio, SFX priority) | all, esp. 20,19 | wraps existing square-wave beeps; tunes as text files on SD |
| SD level/data loader (text/binary packs, safe fallback when no card) | 14,17,18,20,8,7,5 | path `/arcade/<game>/...`, never blocks, atomic writes for saves |
| Tile/grid engine (cell draw, touch-to-cell, animated slide) | 13-19,21-25,28 | one engine for cursor + tap |
| Sprite / glyph blitter (1-bit masks + RGB565, scaled blit) | 3,10,24,25 | glyphs in PROGMEM; draw shapes by code where possible |
| 2D physics (circle/segment, substeps) | 5,6,7,8 | fixed timestep 4 substeps |
| Search helper (negamax alpha-beta with node/time budget, yields to frame loop) | 21-24,28 | board interface: gen moves, apply/undo, eval |
| Cellular/sim grid (half-res buffer + blit 2x) | 9,10 | PSRAM canvas already available |
| Per-game save + best-score table (NVS + SD mirror) | all | extends loadBest/saveBest |
| Hot-seat pass-the-device screen | 19,26,27,28 | blank screen, "pass to P2", tap to continue |
| ESP-NOW link (pair, ping, small packets, lossless counter) [ESPNOW unproven] | 26,27,28 | prove with a "ping" app on two boards first |
| Gyro rate reader [GYRO unproven] | 12 | enable QMI8658 gyro in hw.cpp, expose rate X/Y/Z |

## Hardware dependencies at a glance
- Proven: buttons, tilt (accel), touch, speaker, SD (card now works), NVS, RTC.
- Unproven: ESP-NOW (26/27/28 two-board modes), BLE (none required), gyro (12 only; falls back to accel).
- Everything else works on proven hardware only.
