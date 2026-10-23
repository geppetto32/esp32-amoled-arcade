# One-hand wooden grip ("handheld") - design v1

Built by `hh_grip.py` (Fusion). Frame used below: **u** = right as the player sees it, **v** = up, **w** = out of
the glass. Origin = centre of the display glass, w = 0 on the glass face. All numbers mm, default right hand.

## Board facts read from the official STEP (board frame: X up, Y long side, glass at z = 0)
| part (STEP product) | box X / Y / Z | in use |
|---|---|---|
| display module SPEC-DO0180FMST08 | ±16.80 / ±20.60 / -6.67..0 | 41.2 wide x 33.6 tall, landscape |
| PCB (BOARD) | -16.61..16.40 / -20.34..20.29 / -7.40..-6.20 | hangs under the glass |
| BOOT/PWR switches SWITCH-TS24CA x2 | 13.05..16.70 / ±(8.22..12.78) / -6.6..-4.4 | top edge, u = ±10.5, plunger 4.4-6.6 below the glass |
| USB-C TYPE-C-SINK... | 9.93..17.40 / ±5.77 / -7.78..-4.53 | top edge, centre, sticks out 0.6 past the glass |
| micro-SD MICRO_SD_SOCKET_PU | -15.6..-0.6 / -15.7..0.4 / -6.8..-4.35 | bottom edge, card centre ~u 7.7 (opens downward - verify) |
| battery plug HP1.25 2P | -16.1..-8.4 / -5.5..-0.3 / -10.9..-7.5 | bottom edge, back side, u 0.3..5.5 |
| PRT0001 (assumed speaker) | -7.3..-1.8 / -12..-5 / -9.4..-7.4 | back side; sound holes in the back plate |
| whole board | x -16.8..17.4, y ±20.6, z -10.9..0 | |

The 1.27 mm pad row is not in the STEP (pads are copper only): its edge is the parameter `PAD_EDGE` (measure).
The script reads all these boxes from the imported occurrence by name, so it follows the board if it is moved.

## Layout choice
- **A (stick below the screen, phone-style landscape):** the hand must cradle the bottom; the index finger ends
  up *behind* the device, ~50 mm from the top edge, so the top buttons are out of reach one-handed. The thumb
  arcs up across the lower screen. Rejected.
- **B (stick right of the screen, controller half) - chosen.** The palm wraps a handle that hangs under the stick,
  the thumb rests on the stick from the lower right, the index finger lies on the top edge like a shoulder
  button over both pushers (21 mm apart; the near one at 29 mm left of the stick, the far one 50 mm - one finger
  rocks between them; BOOT/PWR are LEFT/RIGHT in the firmware, so both are game inputs). The screen is left of
  the thumb and never covered. Battery in the handle = weight in the palm, the grip balances on the hand.
  Thumb CMC sits ~40 mm from the stick centre, inside the 50-60 mm comfortable arc.

## Dimensions (defaults; printed by a dry run)
- Plan **92 x 95 mm**, wood **20.6 mm** thick (24.6 at the stick cap); pushers 1 mm proud of the top edge.
- Board pocket u ±20.9, v ±17.1 (display + 0.3/side). Walls: left 6, top 5, bottom 6.
- Stick centre (u 39.2, v -2): 24 mm below the top edge, 18.3 mm right of the board pocket edge.
- Battery pocket 31 x 41 in the handle, u 27.7..58.7, v -17.9..-58.9; handle bottom v -71.9.

| layer | w | t | how | cut-outs |
|---|---|---|---|---|
| top plate | 0..3 | 3 | laser or CNC, hardwood/veneered ply | window 36.3 x 30.0 R2 (chamfer 1), stick hole Ø27.8, lanyard; outer top edge R2.5 |
| upper frame | -9..0 | 9 | CNC, hardwood | display pocket + dogbones, pusher slots + flange recesses, USB slot 11 wide, flared SD slot 13->17, plug relief, pad relief, stick well Ø27.8; from the back: battery pocket 0.7 deep, 5 pilot holes Ø1.6 x 5 |
| lower frame | -14.6..-9 | 5.6 | CNC, hardwood | board cavity, stick carrier pocket 24.6 sq., battery pocket, wire channels (battery plug -> battery 3 wide; carrier -> board 4 wide), pilots through |
| back plate | -17.6..-14.6 | 3 | laser, ply/hardwood | 5 x Ø2.4 screw holes (countersink by hand), 2 carrier screws, 9 sound holes, lanyard; outer bottom edge R2.5 |
| 2 pushers | -8.8..-0.2 | 8.6 | CNC/laser offcut | stem 5 wide, flange 2 (outward), nub 2 x 2 on the plunger |

Stick (placeholders from `ref/xbox_stick.md`): soldered to a 24 x 24 perfboard that rests on the back plate
(seat w -13), pivot w -7, cap Ø21 x 5 at w 2..7 (4 mm proud). Hole = cap rim swept at 23 deg + 0.7: Ø27.8; the rim
dips to w -2.8 at full tilt, so the well goes through the upper frame too. Under the board 3.7 mm stays free for
a 5 mm foam pad that presses the glass up against the top plate (nothing else holds the board).

## Why a laminated stack (not a solid pocketed block)
Every layer is a 2D part: through-cuts on the CNC (dogbones in pocket corners for a 2 mm bit) or the laser for
the plates; no two-sided registration and no 15 mm deep blind pocket in one block. The pushers and all edge
slots (USB, SD, buttons) are simply through-cut in the 9 mm frame and closed above/below by the next layer - a
solid block would need side machining for them. Grain can cross between layers for strength. After glue-up the
outline is one surface: route a roundover (R3-R5) on the glued body and sand the handle by hand; the CAD only
carries R2.5 on the plate edges. Woods: walnut/cherry/maple frames, 3 mm birch ply or hardwood for the back.

## Assembly
1. Cut all layers; dry-fit the display in the upper frame and the pushers in their slots (they must slide).
2. Drop the pushers in, glue top plate + upper frame + lower frame (registration: 2 dowel pins through the screw
   pilots or the outline itself in a jig). Route the outer roundover, sand, oil.
3. Thread M2 brass inserts or drive M2 wood screws into the 5 pilots (drill them through the back plate as a template).
4. Solder the stick to its perfboard, wire X/Y/SW/3V3/GND (GPIO17/18/38) to the pad row; plug the LiPo.
5. Board in from the back (glass to the window), foam pad, stick carrier (2 screws through the back), battery
   with foam; close with 5 x M2 x 8 countersunk screws. Fit the stick cap from the front.

## Measure before cutting (the script marks these as parameters)
1. Stick: body footprint and height, seat-to-cap-top (`SEAT_TO_CAPTOP`), pivot height, cap Ø and tilt -> hole Ø.
2. Battery cell size (`BATT`) - the handle length follows it (a thinner/shorter cell shortens the grip).
3. Screen active area offset on the glass (`WINDOW_DU/DV`): the FPC side bezel is usually wider.
4. Glass + panel thickness at the button edge: the pusher nub reaches under the glass overhang at w -5.5.
5. Which switch is BOOT; the plunger height; SD card exit side and card centre (`SD_DU`).
6. Pad row edge and position (`PAD_EDGE`, `PAD_POS`); whether PRT0001 is really the speaker.
7. USB cable overmould width (slot 11) and the battery plug's length out of the board edge (`CONN_RELIEF`).
8. Kerf/fit: test-cut the display pocket and a pusher slot first; adjust `BOARD_CLR` / `PUSH_CLR`.

## Run (Fusion, never saves)
From `case/fusion` via `frun.mjs`: `run("hh_grip")` = dry plan; `run("hh_grip","main",{dry:false},true)` = build;
add `replace:true` to rebuild; `run("hh_grip","check")` = interference + clearances (read-only);
`{hand:"left"}` mirrors the grip (the board stays). Outside Fusion: `uv run --no-project python hh_grip.py`.
