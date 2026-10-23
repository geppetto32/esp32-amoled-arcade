"""hh_grip.py - one-hand wooden grip for the bare Waveshare ESP32-S3-Touch-AMOLED-1.8 + an Xbox thumbstick.

Layout B (stick right of the screen, palm handle under the stick, index finger on the top-edge buttons).
Laminated stack: top plate / upper frame / lower frame (glued = body) + screwed back plate. See grip_design.md.

Run inside Fusion through frun.mjs (never saves, touches only the active design):
  run("hh_grip")                                   dry: prints the plan, clearances, warnings. Changes nothing.
  run("hh_grip", "main", {dry: false}, true)       builds container "hh_grip" in the active design.
  run("hh_grip", "main", {dry: false, replace: true}, true)   deletes an earlier "hh_grip" first (only that).
  run("hh_grip", "check")                          read-only: interference + clearances, board vs stick/battery/wood.
Outside Fusion, `uv run --no-project python hh_grip.py` prints the plan from the STEP numbers.

Frame used everywhere below: u = right as the player sees it, v = up (the board's button edge),
w = out of the glass; origin = centre of the display glass, w = 0 on the glass face. All sizes mm / degrees.
"""
import math
import traceback

try:
    import adsk.core
    import adsk.fusion
except ImportError:          # plan-only mode outside Fusion
    adsk = None

# ============================== PARAMETERS (mm, deg) ==============================
HAND = "right"                     # "right": stick on the player's right (right thumb); "left": mirrored
EXPECT_DOC = "DIY Play handheld"   # active document name must start with this (None = no check)
BOARD_OCC = "ESP32-S3-TOUCH-AMOLED-1_8_ASM:1"
CONTAINER = "hh_grip"

# layers (w = 0 is the glass face)
T_TOP = 3.0            # top plate: screen window + stick hole (laser or CNC)
T_FRAME1 = 9.0         # upper frame: display pocket, pushers, USB / SD slots, stick well (CNC)
T_BACK = 3.0           # back plate, removable (M2 screws)
# the lower frame thickness follows from the stick height (printed as t_frame2)

BOARD_CLR = 0.3        # per side, display module in its pocket
DOGBONE_R = 1.05       # inside-corner relief for a 2 mm end mill (0 = none)

# screen window - MEASURE the active area offset on the real glass
ACTIVE_LONG, ACTIVE_SHORT = 35.33, 29.02   # 1.8" 368 x 448 px at 0.0789 mm pitch
WINDOW_MARGIN = 0.5    # per side beyond the active area
WINDOW_R = 2.0
WINDOW_DU, WINDOW_DV = 0.0, 0.0            # window centre offset from the glass centre (MEASURE)
EDGE_CHAMFER = 1.0     # 45 deg chamfer on the window and stick-hole top edges

# outline
WALL_LEFT, WALL_TOP, WALL_BOTTOM = 6.0, 5.0, 6.0
WALL_MID = 4.4         # board pocket -> stick well
WALL_STICK = 7.0       # stick well -> right edge
WALL_HANDLE = 6.0      # battery pocket -> handle sides
HANDLE_END_WALL = 13.0 # battery pocket -> handle bottom (screw + lanyard live here)
R_TL, R_TR, R_BR, R_BLH, R_CONC, R_BL = 4.0, 10.0, 14.0, 12.0, 6.0, 4.0

# thumbstick - PLACEHOLDERS from ref/xbox_stick.md, measure before cutting
STICK_U = None         # None = next to the board pocket (WALL_MID + hole radius)
STICK_V = -2.0
CAP_D, CAP_H = 21.0, 5.0
CAP_PROUD = 4.0        # cap top above the top plate
SEAT_TO_CAPTOP = 20.0  # module seating plane (carrier PCB top) -> cap top
PIVOT_ABOVE_SEAT = 6.0
TILT_DEG = 23.0
STICK_CLR = 0.7        # cap rim to hole at full tilt
STICK_BODY = (18.0, 18.0, 9.0)
SHAFT_D = 5.5
CARRIER = (24.0, 24.0, 1.6)   # perfboard the module is soldered to; rests on the back plate
CARRIER_CLR = 0.3
CARRIER_SCREW_INSET = 2.5

# battery - PLACEHOLDER pocket
BATT = (30.0, 40.0, 6.0)      # across (u), along (v), thickness
BATT_CLR = 0.5
BATT_GAP = 2.0                # to the stick well / carrier pocket
HANDLE_DU = 4.0               # battery centre right of the stick centre

# side-button pushers (wood, captive in the upper frame)
PUSH_W = 5.0                  # stem width (u)
PUSH_FLANGE, PUSH_FLANGE_V = 2.0, 1.5   # retaining flange, outward side only (away from the USB)
PUSH_CLR = 0.15
PUSH_GAP = 0.1                # nub to switch plunger at rest
PUSH_REST = 0.2               # stem inner face beyond the pocket wall at rest
PUSH_PROUD = 1.0              # stem beyond the top edge
NUB = 2.0

# edge cut-outs
USB_SLOT_W = 11.0             # through the top wall, full upper-frame height (plug overmould)
SD_SLOT_W, SD_FLARE_W, SD_DU = 13.0, 17.0, 0.0   # flared slot; SD_DU = card centre vs socket centre (MEASURE)
CONN_RELIEF, CONN_CLR = 4.0, 1.0                  # battery plug relief beyond the PCB edge
PAD_EDGE = "stick"            # edge with the 1.27 mm pad row: stick|far|top|bottom (MEASURE)
PAD_ROW_LEN, PAD_POS, PAD_RELIEF = 22.0, 0.0, 1.5
WIRE_W, STICK_WIRE_W = 3.0, 4.0

# fasteners, lanyard, sound
SCREW_PILOT_D, SCREW_CLR_D, SCREW_DEPTH_F1 = 1.6, 2.4, 5.0   # M2 screws into hardwood
LANYARD_D = 3.5
SPEAKER_HOLES, SPK_HOLE_D, SPK_PITCH = True, 1.5, 2.5         # under STEP part PRT0001 (assumed speaker)

TOP_EDGE_R, BACK_EDGE_R = 2.5, 2.5
WOOD_DENSITY = 0.65           # g/cm3 (walnut ~0.65, maple ~0.70)

# board geometry from the official STEP (mm, STEP/world frame: display face z=0, long side Y, buttons +X)
STEP_BOX = {
    "display": (-16.80, -20.60, -6.67, 16.80, 20.60, 0.00),
    "all":     (-16.80, -20.60, -10.90, 17.40, 20.60, 0.00),
    "pcb":     (-16.61, -20.34, -7.40, 16.40, 20.29, -6.20),
    "usb":     (9.93, -5.77, -7.78, 17.40, 5.77, -4.53),
    "sw1":     (13.05, -12.78, -6.60, 16.70, -8.22, -4.40),
    "sw2":     (13.05, 8.22, -6.60, 16.70, 12.78, -4.40),
    "sd":      (-15.60, -15.70, -6.80, -0.60, 0.40, -4.35),
    "conn":    (-16.09, -5.47, -10.90, -8.44, -0.27, -7.50),
    "spk":     (-7.30, -12.00, -9.40, -1.80, -5.00, -7.40),
}
PART_PREFIX = {"display": "SPEC-DO0180FMST08", "usb": "TYPE-C-SINK", "sw": "SWITCH-TS24CA",
               "sd": "MICRO_SD_SOCKET", "conn": "HP1_25MM", "spk": "PRT0001", "pcb": "BOARD"}
# ==================================================================================


class PlanError(Exception):
    pass


def cm(v):
    return v / 10.0


# ------------------------------------------------------------------ frame
class Frame:
    """u, v, w grip frame <-> world mm. V is the up axis (unit, in XY); U = right as seen from the glass side."""

    def __init__(self, origin, up, hand):
        self.O, self.V = origin, up
        s = 1.0 if hand == "right" else -1.0
        self.U = (s * up[1], -s * up[0])          # right = up x out-of-glass (mirrored for the left hand)

    def xy(self, u, v):
        return (self.O[0] + u * self.U[0] + v * self.V[0], self.O[1] + u * self.U[1] + v * self.V[1])

    def z(self, w):
        return self.O[2] + w

    def uvw(self, x, y, z):
        dx, dy = x - self.O[0], y - self.O[1]
        return (dx * self.U[0] + dy * self.U[1], dx * self.V[0] + dy * self.V[1], z - self.O[2])

    def box(self, bb):
        """world (x0,y0,z0,x1,y1,z1) -> (u0,u1,v0,v1,w0,w1)"""
        pts = [self.uvw(x, y, z) for x in (bb[0], bb[3]) for y in (bb[1], bb[4]) for z in (bb[2], bb[5])]
        us, vs, ws = zip(*pts)
        return (min(us), max(us), min(vs), max(vs), min(ws), max(ws))


# ------------------------------------------------------------------ plan (pure python)
def _corner(cu, cv, r, su, sv, first_along_v):
    """three points of a round corner at (cu,cv); (su,sv) points from the corner into the material."""
    c = (cu + su * r, cv + sv * r)
    k = r / math.sqrt(2.0)
    mid = (c[0] - su * k, c[1] - sv * k)
    a, b = (cu, cv + sv * r), (cu + su * r, cv)
    return [a, mid, b] if first_along_v else [b, mid, a]


def _densify(pts, step=22.0):
    out = []
    n = len(pts)
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        out.append(a)
        d = math.hypot(b[0] - a[0], b[1] - a[1])
        k = int(d // step)
        for j in range(1, k + 1):
            t = j / (k + 1.0)
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return out


def plan(bx, hand=HAND):
    """bx: board part boxes in the grip frame {name: (u0,u1,v0,v1,w0,w1)}. Returns a dict with everything."""
    g = {"warn": [], "hand": hand}
    W = g["warn"]
    d = bx["display"]
    if (d[1] - d[0]) < (d[3] - d[2]):
        W.append("display long side is not horizontal in the grip frame - check the board orientation")
    pu0, pu1, pv0, pv1 = d[0] - BOARD_CLR, d[1] + BOARD_CLR, d[2] - BOARD_CLR, d[3] + BOARD_CLR
    g["pocket"] = (pu0, pu1, pv0, pv1)

    # ---- heights
    w_captop = T_TOP + CAP_PROUD
    w_seat = w_captop - SEAT_TO_CAPTOP
    w_car0 = w_seat - CARRIER[2]
    w_back0 = w_car0 - T_BACK
    t_f2 = -T_FRAME1 - w_car0
    if t_f2 < 2.0:
        raise PlanError("lower frame would be %.1f mm: raise SEAT_TO_CAPTOP or thin T_FRAME1" % t_f2)
    foam = bx["all"][4] - w_car0
    if foam < 1.0:
        W.append("board bottom only %.1f mm above the back plate" % foam)
    w_pivot = w_seat + PIVOT_ABOVE_SEAT
    w_cap0 = w_captop - CAP_H
    t = math.radians(TILT_DEG)
    h = w_cap0 - w_pivot
    hole_r = CAP_D / 2 * math.cos(t) + h * math.sin(t) + STICK_CLR
    rim_low = w_pivot + h * math.cos(t) - CAP_D / 2 * math.sin(t)
    if rim_low < -T_FRAME1:
        W.append("cap rim dips to w=%.1f, below the upper frame" % rim_low)
    g.update(w_captop=w_captop, w_seat=w_seat, w_car0=w_car0, w_back0=w_back0, t_f2=t_f2, foam=foam,
             w_pivot=w_pivot, w_cap0=w_cap0, hole_r=hole_r, rim_low=rim_low)

    # ---- plan positions
    SU = STICK_U if STICK_U is not None else pu1 + WALL_MID + hole_r
    SV = STICK_V
    chu, chv = CARRIER[0] / 2 + CARRIER_CLR, CARRIER[1] / 2 + CARRIER_CLR
    HU = SU + HANDLE_DU
    bw = BATT[0] / 2 + BATT_CLR
    b_top = min(SV - chv, SV - hole_r) - BATT_GAP
    b_bot = b_top - BATT[1] - 2 * BATT_CLR
    HB = b_bot - HANDLE_END_WALL
    L, T, B = pu0 - WALL_LEFT, pv1 + WALL_TOP, pv0 - WALL_BOTTOM
    UR = max(SU + hole_r + WALL_STICK, HU + bw + WALL_HANDLE)
    ULh = HU - bw - WALL_HANDLE
    if ULh - R_CONC < L + R_BL + 5:
        raise PlanError("handle too far left: concave corner meets the bottom-left corner")
    g.update(SU=SU, SV=SV, HU=HU, HB=HB, L=L, T=T, B=B, UR=UR, ULh=ULh, b_top=b_top, b_bot=b_bot)

    # ---- outline (closed fitted spline through round-corner points)
    r2 = R_CONC / math.sqrt(2.0)
    pts = []
    pts += _corner(L, T, R_TL, 1, -1, True)               # top-left: from the left edge to the top edge
    pts += _corner(UR, T, R_TR, -1, -1, False)            # top-right
    pts += _corner(UR, HB, R_BR, -1, 1, True)             # handle bottom-right
    pts += _corner(ULh, HB, R_BLH, 1, 1, False)           # handle bottom-left
    pts += [(ULh, B - R_CONC), (ULh - R_CONC + r2, B - R_CONC + r2), (ULh - R_CONC, B)]   # concave
    pts += _corner(L, B, R_BL, 1, 1, False)               # bottom-left
    g["outline"] = _densify(pts)

    # ---- features
    R = lambda u0, u1, v0, v1, r=0.0, dog=False: ("rect", u0, u1, v0, v1, r, dog)
    C = lambda u, v, dia: ("circ", u, v, dia)
    pocket = R(pu0, pu1, pv0, pv1, 0.0, True)
    wcu, wcv = (d[0] + d[1]) / 2 + WINDOW_DU, (d[2] + d[3]) / 2 + WINDOW_DV
    wl, ws = ACTIVE_LONG / 2 + WINDOW_MARGIN, ACTIVE_SHORT / 2 + WINDOW_MARGIN
    window = R(wcu - wl, wcu + wl, wcv - ws, wcv + ws, WINDOW_R)
    well = C(SU, SV, 2 * hole_r)
    carrier = R(SU - chu, SU + chu, SV - chv, SV + chv, 0.0, True)
    battery = R(HU - bw, HU + bw, b_bot, b_top, 0.0, True)

    usb = bx["usb"]
    uc_usb = (usb[0] + usb[1]) / 2
    usb_slot = R(uc_usb - USB_SLOT_W / 2, uc_usb + USB_SLOT_W / 2, pv1 - 0.01, T + 2)
    if usb[4] < -T_FRAME1 + 0.3:
        W.append("USB-C reaches w=%.2f, below the upper frame - slot will not clear it" % usb[4])

    pushers, push_cuts = [], []
    for i, sb in enumerate(bx["switches"]):
        ub, vp, wc = (sb[0] + sb[1]) / 2, sb[3], (sb[4] + sb[5]) / 2
        s = 1.0 if ub >= uc_usb else -1.0
        vi = pv1 + PUSH_REST
        push_cuts.append(R(ub - PUSH_W / 2 - PUSH_CLR, ub + PUSH_W / 2 + PUSH_CLR, pv1 - 0.01, T + 2))
        fa, fb = ub + s * (PUSH_W / 2 - 0.5), ub + s * (PUSH_W / 2 + PUSH_FLANGE + PUSH_CLR)
        push_cuts.append(R(min(fa, fb), max(fa, fb), pv1 - 0.01, vi + PUSH_FLANGE_V + PUSH_CLR))
        e = lambda du: ub + s * du
        poly = [(e(-PUSH_W / 2), vi), (e(PUSH_W / 2 + PUSH_FLANGE), vi), (e(PUSH_W / 2 + PUSH_FLANGE), vi + PUSH_FLANGE_V),
                (e(PUSH_W / 2), vi + PUSH_FLANGE_V), (e(PUSH_W / 2), T + PUSH_PROUD), (e(-PUSH_W / 2), T + PUSH_PROUD)]
        nub = ("box", ub - NUB / 2, ub + NUB / 2, vp + PUSH_GAP, vi + 0.01, wc - NUB / 2, wc + NUB / 2)
        if vp + PUSH_GAP >= vi:
            W.append("switch %d plunger face is outside the pocket wall: nub not needed / check" % (i + 1))
        pushers.append({"name": "pusher_%d" % (i + 1), "poly": poly, "w0": -T_FRAME1 + 0.2, "w1": -0.2,
                        "nub": nub, "u": ub, "plunger_w": wc})

    sd = bx["sd"]
    ucs = (sd[0] + sd[1]) / 2 + SD_DU
    if abs(sd[2] - pv0) > abs(sd[3] - pv1):
        W.append("SD socket is not at the bottom edge - SD slot placed at the bottom anyway, check")
    sd_slot = ("poly", [(ucs - SD_SLOT_W / 2, pv0 + 0.01), (ucs + SD_SLOT_W / 2, pv0 + 0.01),
                        (ucs + SD_FLARE_W / 2, B - 3), (ucs - SD_FLARE_W / 2, B - 3)])
    cn = bx["conn"]
    conn = R(cn[0] - CONN_CLR, cn[1] + CONN_CLR, pv0 - CONN_RELIEF, pv0 + 0.01)
    bu0 = HU - bw
    batt_ch = [R(min(cn[0] - CONN_CLR, bu0), max(cn[1] + CONN_CLR, bu0 + WIRE_W), pv0 - WIRE_W, pv0 + 0.01),
               R(bu0, bu0 + WIRE_W, b_top - 0.5, pv0 + 0.01)]
    stick_ch = R(pu1 - 0.5, SU - chu + 0.5, SV - STICK_WIRE_W / 2, SV + STICK_WIRE_W / 2)
    pc_v, pc_u = (pv0 + pv1) / 2 + PAD_POS, (pu0 + pu1) / 2 + PAD_POS
    pad = {"stick": R(pu1 - 0.01, pu1 + PAD_RELIEF, pc_v - PAD_ROW_LEN / 2, pc_v + PAD_ROW_LEN / 2),
           "far": R(pu0 - PAD_RELIEF, pu0 + 0.01, pc_v - PAD_ROW_LEN / 2, pc_v + PAD_ROW_LEN / 2),
           "top": R(pc_u - PAD_ROW_LEN / 2, pc_u + PAD_ROW_LEN / 2, pv1 - 0.01, pv1 + PAD_RELIEF),
           "bottom": R(pc_u - PAD_ROW_LEN / 2, pc_u + PAD_ROW_LEN / 2, pv0 - PAD_RELIEF, pv0 + 0.01)}[PAD_EDGE]

    screws = {"s_top_left": (L + 3.2, T - 3.2), "s_bottom_left": (L + 3.2, B + 3.2), "s_top_right": (SU - 9.0, T - 3.2),
              "s_right": (UR - 4.0, SV), "s_handle": (ULh + 11.5, HB + 8.5)}
    lanyard = (HU, HB + 5.5)
    cs = [(SU - CARRIER[0] / 2 + CARRIER_SCREW_INSET, SV - CARRIER[1] / 2 + CARRIER_SCREW_INSET),
          (SU + CARRIER[0] / 2 - CARRIER_SCREW_INSET, SV + CARRIER[1] / 2 - CARRIER_SCREW_INSET)]
    spk = []
    if SPEAKER_HOLES and bx.get("spk"):
        sp = bx["spk"]
        cu, cv = (sp[0] + sp[1]) / 2, (sp[2] + sp[3]) / 2
        spk = [C(cu + i * SPK_PITCH, cv + j * SPK_PITCH, SPK_HOLE_D) for i in (-1, 0, 1) for j in (-1, 0, 1)]

    pil = [C(u, v, SCREW_PILOT_D) for u, v in screws.values()]
    lan = C(lanyard[0], lanyard[1], LANYARD_D)
    batt_depth_f1 = max(0.0, BATT[2] + 0.3 - t_f2)
    g["layers"] = [
        {"name": "hh_top_plate", "w0": 0.0, "w1": T_TOP, "thru": [window, well, lan],
         "blind": [], "fillet": ("top", TOP_EDGE_R), "chamfer": EDGE_CHAMFER},
        {"name": "hh_frame_upper", "w0": -T_FRAME1, "w1": 0.0,
         "thru": [pocket, pad, usb_slot, sd_slot, conn, well, lan] + push_cuts,
         "blind": [(SCREW_DEPTH_F1, pil)] + ([(batt_depth_f1, [battery])] if batt_depth_f1 > 0 else [])},
        {"name": "hh_frame_lower", "w0": w_car0, "w1": -T_FRAME1,
         "thru": [pocket, pad, conn, stick_ch, carrier, battery, lan] + batt_ch + pil, "blind": []},
        {"name": "hh_back_plate", "w0": w_back0, "w1": w_car0,
         "thru": [C(u, v, SCREW_CLR_D) for u, v in screws.values()] + [C(u, v, SCREW_CLR_D) for u, v in cs] + spk + [lan],
         "blind": [], "fillet": ("bottom", BACK_EDGE_R)},
    ]
    g["pushers"] = pushers
    sb_ = STICK_BODY
    g["placeholders"] = [
        ("ph_stick_body", ("box", SU - sb_[0] / 2, SU + sb_[0] / 2, SV - sb_[1] / 2, SV + sb_[1] / 2, w_seat, w_seat + sb_[2])),
        ("ph_stick_shaft", ("cyl", SU, SV, SHAFT_D, w_seat + sb_[2], w_cap0)),
        ("ph_stick_cap", ("cyl", SU, SV, CAP_D, w_cap0, w_captop)),
        ("ph_stick_carrier", ("box", SU - CARRIER[0] / 2, SU + CARRIER[0] / 2, SV - CARRIER[1] / 2, SV + CARRIER[1] / 2, w_car0, w_seat)),
        ("ph_battery", ("box", HU - BATT[0] / 2, HU + BATT[0] / 2, b_bot + BATT_CLR, b_top - BATT_CLR, w_car0, w_car0 + BATT[2])),
    ]
    g["screws"], g["lanyard"] = screws, lanyard
    g["wall_feats"] = [("board pocket", pocket), ("stick well", well), ("carrier pocket", carrier),
                       ("battery pocket", battery), ("stick wire channel", stick_ch), ("pad relief", pad),
                       ("lanyard", lan)] + [("battery wire channel", c) for c in batt_ch] + \
                      [(k, C(u, v, SCREW_PILOT_D)) for k, (u, v) in screws.items()]
    return g


# ------------------------------------------------------------------ geometry checks (pure python)
def _crom(pts, n=12):
    """closed centripetal Catmull-Rom - a close stand-in for Fusion's fitted spline (dry run only)."""
    out = []
    N = len(pts)
    for i in range(N):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[(i + 1) % N], pts[(i + 2) % N]
        tj = lambda ti, a, b: ti + max(math.hypot(b[0] - a[0], b[1] - a[1]), 1e-9) ** 0.5
        t0 = 0.0
        t1 = tj(t0, p0, p1)
        t2 = tj(t1, p1, p2)
        t3 = tj(t2, p2, p3)
        L = lambda a, b, ta, tb, tt: tuple(a[k] * (tb - tt) / (tb - ta) + b[k] * (tt - ta) / (tb - ta) for k in (0, 1))
        for k in range(n):
            tt = t1 + (t2 - t1) * k / n
            A1, A2, A3 = L(p0, p1, t0, t1, tt), L(p1, p2, t1, t2, tt), L(p2, p3, t2, t3, tt)
            B1, B2 = L(A1, A2, t0, t2, tt), L(A2, A3, t1, t3, tt)
            out.append(L(B1, B2, t1, t2, tt))
    return out


def _inside(poly, p):
    c = False
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[i - 1]
        if (a[1] > p[1]) != (b[1] > p[1]) and p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0]:
            c = not c
    return c


def _seg_dist(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy or 1e-12)))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy)


def wall(poly, shape):
    """minimum wood between a closed internal feature and the outline (negative = breaks out)."""
    if shape[0] == "circ":
        _, u, v, dia = shape
        dmin = min(_seg_dist((u, v), poly[i - 1], poly[i]) for i in range(len(poly)))
        return (dmin if _inside(poly, (u, v)) else -dmin) - dia / 2
    _, u0, u1, v0, v1 = shape[:5]
    corners = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
    if not all(_inside(poly, c) for c in corners) or any(u0 < q[0] < u1 and v0 < q[1] < v1 for q in poly):
        return -1.0
    best = 1e9
    for q in poly:
        best = min(best, math.hypot(max(u0 - q[0], 0, q[0] - u1), max(v0 - q[1], 0, q[1] - v1)))
    for c in corners:
        best = min(best, min(_seg_dist(c, poly[i - 1], poly[i]) for i in range(len(poly))))
    return best


def _aabb(sh):
    if sh[0] == "box":
        return sh[1:7]
    _, u, v, dia, w0, w1 = sh
    return (u - dia / 2, u + dia / 2, v - dia / 2, v + dia / 2, w0, w1)


def _overlap(a, b):
    o = [min(a[2 * k + 1], b[2 * k + 1]) - max(a[2 * k], b[2 * k]) for k in range(3)]
    return o if all(x > 1e-6 for x in o) else None


def print_plan(g, bx, polyline=None, approx=True):
    f = lambda v: "%.1f" % v
    print("== hh_grip plan (%s hand) ==" % g["hand"])
    for w in g["warn"]:
        print("WARNING:", w)
    pts = polyline or _crom(g["outline"])
    us, vs = zip(*pts)
    print("outline: u %s..%s  v %s..%s  -> %s x %s mm (plan)%s" % (f(min(us)), f(max(us)), f(min(vs)), f(max(vs)),
          f(max(us) - min(us)), f(max(vs) - min(vs)), "  [approx spline]" if approx else ""))
    w_hi = max(g["w_captop"], T_TOP)
    print("thickness: wood %s mm (w %s..%s), %s with the stick cap; pushers stand %s mm proud of the top edge"
          % (f(T_TOP - g["w_back0"]), f(g["w_back0"]), f(T_TOP), f(w_hi - g["w_back0"]), f(PUSH_PROUD)))
    print("layers (w from the glass face):")
    for L in g["layers"]:
        print("  %-15s w %6.2f .. %6.2f  t=%5.2f  through-cuts %d, blind %d"
              % (L["name"], L["w0"], L["w1"], L["w1"] - L["w0"], len(L["thru"]), sum(len(b[1]) for b in L["blind"])))
    print("stick: centre u %s v %s; seat w %s, pivot w %s, cap w %s..%s; hole d %s (cap rim dips to w %s at %s deg)"
          % (f(g["SU"]), f(g["SV"]), f(g["w_seat"]), f(g["w_pivot"]), f(g["w_cap0"]), f(g["w_captop"]),
             f(2 * g["hole_r"]), f(g["rim_low"]), f(TILT_DEG)))
    print("battery pocket: u %s..%s v %s..%s (handle bottom v %s); foam gap under board %s mm"
          % (f(g["HU"] - BATT[0] / 2 - BATT_CLR), f(g["HU"] + BATT[0] / 2 + BATT_CLR), f(g["b_bot"]), f(g["b_top"]),
             f(g["HB"]), f(g["foam"])))
    print("board pocket u %s..%s v %s..%s; lower frame t=%s" % tuple(f(x) for x in g["pocket"] + (g["t_f2"],)))
    for p in g["pushers"]:
        print("  %s at u %s, plunger w %s" % (p["name"], f(p["u"]), f(p["plunger_w"])))
    print("screws:", ", ".join("%s (%s,%s)" % (k, f(u), f(v)) for k, (u, v) in g["screws"].items()),
          "| lanyard (%s,%s)" % (f(g["lanyard"][0]), f(g["lanyard"][1])))
    print("board parts (u0,u1,v0,v1,w0,w1):")
    for k in ("display", "usb", "sd", "conn", "spk"):
        if bx.get(k):
            print("  %-8s" % k, ", ".join(f(x) for x in bx[k]))
    for i, s in enumerate(bx["switches"]):
        print("  switch%d " % (i + 1), ", ".join(f(x) for x in s))
    print("walls to the outline%s:" % (" (approx)" if approx else ""))
    for name, sh in g["wall_feats"]:
        wv = wall(pts, sh)
        print("  %-22s %6.2f mm%s" % (name, wv, "  <-- THIN" if wv < 1.8 else ""))
    print("bounding-box interference, placeholders vs board / each other:")
    hits = 0
    phs = [(n, _aabb(s)) for n, s in g["placeholders"]]
    allb = bx["all"]
    for n, b in phs:
        o = _overlap(b, allb)
        if o:
            hits += 1
            print("  %s overlaps the board box by %s mm" % (n, ", ".join(f(x) for x in o)))
    for i in range(len(phs)):
        for j in range(i + 1, len(phs)):
            o = _overlap(phs[i][1], phs[j][1])
            if o and not {phs[i][0], phs[j][0]} <= {"ph_stick_body", "ph_stick_shaft", "ph_stick_cap", "ph_stick_carrier"}:
                hits += 1
                print("  %s overlaps %s" % (phs[i][0], phs[j][0]))
    print("  none" if not hits else "  %d overlap(s)" % hits)


# ------------------------------------------------------------------ board in Fusion
def _bb_mm(b):
    return (b.minPoint.x * 10, b.minPoint.y * 10, b.minPoint.z * 10, b.maxPoint.x * 10, b.maxPoint.y * 10, b.maxPoint.z * 10)


def _union(bbs):
    return tuple(min(b[k] for b in bbs) for k in range(3)) + tuple(max(b[k] for b in bbs) for k in range(3, 6))


def _guard():
    app = adsk.core.Application.get()
    des = adsk.fusion.Design.cast(app.activeProduct)
    if des is None:
        raise RuntimeError("the active product is not a Fusion design")
    name = app.activeDocument.name if app.activeDocument else ""
    if EXPECT_DOC and not name.startswith(EXPECT_DOC):
        raise RuntimeError("active document is '%s', expected '%s...' - open it or set EXPECT_DOC" % (name, EXPECT_DOC))
    return app, des, des.rootComponent


def _find_board(root):
    occ = root.occurrences.itemByName(BOARD_OCC)
    if occ is None:
        base = BOARD_OCC.split(":")[0]
        for i in range(root.allOccurrences.count):
            o = root.allOccurrences.item(i)
            if o.name.split(":")[0] == base:
                return o
    return occ


def _board_parts(root, board):
    """world mm boxes of the board's key sub-parts, found by STEP product names."""
    found = {}
    pref = board.fullPathName + "+"
    for i in range(root.allOccurrences.count):
        o = root.allOccurrences.item(i)
        if not o.fullPathName.startswith(pref):
            continue
        base = o.name.split(":")[0]
        for key, p in PART_PREFIX.items():
            if base.startswith(p):
                bb = _bb_mm(o.boundingBox)
                if max(bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2]) > 0.01:
                    found.setdefault(key, []).append((bb, o))
    return found


def read_board(root, hand):
    """-> (Frame, bx in grip frame, board occurrence, parts dict). Falls back to STEP numbers per missing part."""
    board = _find_board(root)
    if board is None:
        raise RuntimeError("board occurrence '%s' not found in the active design" % BOARD_OCC)
    parts = _board_parts(root, board)
    world = {}
    for k in ("display", "usb", "sd", "conn", "spk", "pcb"):
        if parts.get(k):
            world[k] = _union([b for b, _ in parts[k]])
        else:
            print("note: board part '%s' not found by name - using the STEP numbers (board assumed at origin)" % k)
            world[k] = STEP_BOX[k]
    sws = [b for b, _ in parts.get("sw", [])]
    if len(sws) != 2:
        print("note: found %d SWITCH-TS24CA parts - using the STEP numbers" % len(sws))
        sws = [STEP_BOX["sw1"], STEP_BOX["sw2"]]
    world["all"] = _bb_mm(board.boundingBox)
    d, a = world["display"], world["all"]
    if abs(d[5] - a[5]) > 0.5:
        raise RuntimeError("the display face is not the top (+Z) of the board: display zmax %.2f, board zmax %.2f"
                           % (d[5], a[5]))
    if (a[5] - a[2]) > min(a[3] - a[0], a[4] - a[1]):
        raise RuntimeError("board looks rotated (thickest along Z is expected to be ~11 mm)")
    O = ((d[0] + d[3]) / 2, (d[1] + d[4]) / 2, d[5])
    sc = [((b[0] + b[3]) / 2, (b[1] + b[4]) / 2) for b in sws]
    dx, dy = sum(p[0] for p in sc) / 2 - O[0], sum(p[1] for p in sc) / 2 - O[1]
    up = (1.0 if dx > 0 else -1.0, 0.0) if abs(dx) >= abs(dy) else (0.0, 1.0 if dy > 0 else -1.0)
    fr = Frame(O, up, hand)
    bx = {k: fr.box(v) for k, v in world.items()}
    bx["switches"] = sorted((fr.box(b) for b in sws), key=lambda s: s[0])
    print("board '%s': display centre (%.2f, %.2f), face z %.2f; buttons edge = world %s%s; right = world %s"
          % (board.name, O[0], O[1], O[2], "+" if up[0] + up[1] > 0 else "-", "X" if up[0] else "Y",
             "%+.0fX %+.0fY" % fr.U))
    return fr, bx, board, parts


def step_board(hand):
    fr = Frame((0.0, 0.0, 0.0), (1.0, 0.0), hand)
    bx = {k: fr.box(v) for k, v in STEP_BOX.items() if not k.startswith("sw")}
    bx["switches"] = sorted((fr.box(STEP_BOX[k]) for k in ("sw1", "sw2")), key=lambda s: s[0])
    return fr, bx


# ------------------------------------------------------------------ Fusion building
class Builder:
    def __init__(self, fr):
        self.fr = fr
        self.planes = {}

    def plane(self, comp, w):
        z = self.fr.z(w)
        key = (comp.name, round(z, 4))
        if key not in self.planes:
            if abs(z) < 1e-6:
                self.planes[key] = comp.xYConstructionPlane
            else:
                pi = comp.constructionPlanes.createInput()
                pi.setByOffset(comp.xYConstructionPlane, adsk.core.ValueInput.createByReal(cm(z)))
                pl = comp.constructionPlanes.add(pi)
                pl.name = "z %.2f" % z
                pl.isLightBulbOn = False
                self.planes[key] = pl
        return self.planes[key]

    def sketch(self, comp, w, name, shapes):
        sk = comp.sketches.add(self.plane(comp, w))
        sk.name = name
        sk.isComputeDeferred = True
        z = self.fr.z(w)
        P = lambda x, y: sk.modelToSketchSpace(adsk.core.Point3D.create(cm(x), cm(y), cm(z)))
        curves = sk.sketchCurves
        spline = None
        for sh in shapes:
            kind = sh[0]
            if kind == "circ":
                x, y = self.fr.xy(sh[1], sh[2])
                curves.sketchCircles.addByCenterRadius(P(x, y), cm(sh[3] / 2))
            elif kind == "rect":
                _, u0, u1, v0, v1, r, dog = sh
                xa, ya = self.fr.xy(u0, v0)
                xb, yb = self.fr.xy(u1, v1)
                x0, x1, y0, y1 = min(xa, xb), max(xa, xb), min(ya, yb), max(ya, yb)
                self._rrect(curves, P, x0, x1, y0, y1, r)
                if dog and DOGBONE_R > 0:
                    k = DOGBONE_R / math.sqrt(2.0)
                    for cx, cy, sx, sy in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x1, y1, -1, -1), (x0, y1, 1, -1)):
                        curves.sketchCircles.addByCenterRadius(P(cx + sx * k, cy + sy * k), cm(DOGBONE_R))
            elif kind == "poly":
                self._poly(curves, [P(*self.fr.xy(u, v)) for u, v in sh[1]])
            elif kind == "spline":
                coll = adsk.core.ObjectCollection.create()
                for u, v in sh[1]:
                    coll.add(P(*self.fr.xy(u, v)))
                spline = curves.sketchFittedSplines.add(coll)
                spline.isClosed = True
        sk.isComputeDeferred = False
        return sk, spline

    @staticmethod
    def _poly(curves, pts):
        lines = curves.sketchLines
        first = prev = None
        for i in range(len(pts)):
            if prev is None:
                prev = first = lines.addByTwoPoints(pts[0], pts[1])
            elif i == len(pts) - 1:
                lines.addByTwoPoints(prev.endSketchPoint, first.startSketchPoint)
            else:
                prev = lines.addByTwoPoints(prev.endSketchPoint, pts[i + 1])

    def _rrect(self, curves, P, x0, x1, y0, y1, r):
        r = min(r, (x1 - x0) / 2 - 1e-3, (y1 - y0) / 2 - 1e-3)
        if r <= 0.01:
            curves.sketchLines.addTwoPointRectangle(P(x0, y0), P(x1, y1))
            return
        k = r * (1 - 1 / math.sqrt(2.0))
        arcs = []
        for (sx, sy, ex, ey, mx, my) in ((x1 - r, y0, x1, y0 + r, x1 - k, y0 + k), (x1, y1 - r, x1 - r, y1, x1 - k, y1 - k),
                                         (x0 + r, y1, x0, y1 - r, x0 + k, y1 - k), (x0, y0 + r, x0 + r, y0, x0 + k, y0 + k)):
            a = curves.sketchArcs.addByThreePoints(P(sx, sy), P(mx, my), P(ex, ey))
            arcs.append((a, (sx, sy), (ex, ey)))

        def near(a, xy):
            p = P(*xy)
            s, e = a.startSketchPoint, a.endSketchPoint
            return s if s.geometry.distanceTo(p) < e.geometry.distanceTo(p) else e
        for i in range(4):
            a, _, end = arcs[i]
            b, start, _ = arcs[(i + 1) % 4]
            curves.sketchLines.addByTwoPoints(near(a, end), near(b, start))

    def extrude(self, comp, sk, dist, op, target=None):
        prof = adsk.core.ObjectCollection.create()
        for i in range(sk.profiles.count):
            prof.add(sk.profiles.item(i))
        if prof.count == 0:
            raise RuntimeError("sketch '%s' has no closed profile" % sk.name)
        ext = comp.features.extrudeFeatures
        inp = ext.createInput(prof, op)
        inp.setDistanceExtent(False, adsk.core.ValueInput.createByReal(cm(dist)))
        if target is not None:
            inp.participantBodies = [target]
        return ext.add(inp)

    def solid(self, comp, name, shape, w0, w1):
        if shape[0] == "box":
            sk_shape, w0, w1 = ("rect",) + tuple(shape[1:5]) + (0.0, False), shape[5], shape[6]
        elif shape[0] == "cyl":
            sk_shape, w0, w1 = ("circ", shape[1], shape[2], shape[3]), shape[4], shape[5]
        else:
            sk_shape = shape
        sk, _ = self.sketch(comp, w0, name, [sk_shape])
        body = self.extrude(comp, sk, w1 - w0, adsk.fusion.FeatureOperations.NewBodyFeatureOperation).bodies.item(0)
        body.name = name
        self.expect_w(body, w0, w1)
        return body

    def expect_w(self, body, w0, w1):
        bb = _bb_mm(body.boundingBox)
        z0, z1 = self.fr.z(w0), self.fr.z(w1)
        if abs(bb[2] - z0) > 0.05 or abs(bb[5] - z1) > 0.05:
            raise RuntimeError("body '%s' came out at z %.2f..%.2f, expected %.2f..%.2f (extrude direction?)"
                               % (body.name, bb[2], bb[5], z0, z1))


def _planar_face(body, top):
    best = None
    for i in range(body.faces.count):
        fc = body.faces.item(i)
        if fc.geometry.objectType != adsk.core.Plane.classType():
            continue
        n = fc.geometry.normal
        if abs(abs(n.z) - 1.0) > 1e-6:
            continue
        z = fc.pointOnFace.z
        if best is None or (z > best[0] if top else z < best[0]):
            best = (z, fc)
    return best[1] if best else None


def _loop_edges(face, outer):
    coll = adsk.core.ObjectCollection.create()
    for i in range(face.loops.count):
        lp = face.loops.item(i)
        if lp.isOuter == outer:
            for j in range(lp.edges.count):
                coll.add(lp.edges.item(j))
    return coll


def _fillet(comp, edges, r):
    fi = comp.features.filletFeatures
    inp = fi.createInput()
    val = adsk.core.ValueInput.createByReal(cm(r))
    try:
        inp.edgeSetInputs.addConstantRadiusEdgeSet(edges, val, True)
    except AttributeError:
        inp.addConstantRadiusEdgeSet(edges, val, True)
    fi.add(inp)


def _chamfer(comp, edges, dist):
    ch = comp.features.chamferFeatures
    val = adsk.core.ValueInput.createByReal(cm(dist))
    try:
        inp = ch.createInput2()
        inp.chamferEdgeSets.addEqualDistanceChamferEdgeSet(edges, val, True)
    except AttributeError:
        inp = ch.createInput(edges, True)
        inp.setToEqualDistance(val)
    ch.add(inp)


def _find_container(root):
    for i in range(root.occurrences.count):
        o = root.occurrences.item(i)
        if o.component.name == CONTAINER or o.component.name.startswith(CONTAINER + " ("):
            return o
    return None


def build(des, root, fr, g):
    NEW = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
    CUT = adsk.fusion.FeatureOperations.CutFeatureOperation
    JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation
    b = Builder(fr)
    cont_occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    cont = cont_occ.component
    cont.name = CONTAINER
    made = []
    outline = ("spline", g["outline"])
    spline_pts = None
    for L in g["layers"]:
        occ = cont.occurrences.addNewComponent(adsk.core.Matrix3D.create())
        comp = occ.component
        comp.name = L["name"]
        sk, spl = b.sketch(comp, L["w0"], "outline", [outline])
        body = b.extrude(comp, sk, L["w1"] - L["w0"], NEW).bodies.item(0)
        body.name = L["name"].replace("hh_", "")
        b.expect_w(body, L["w0"], L["w1"])
        if spline_pts is None:
            spline_pts = _spline_uv(fr, spl)
        if L["thru"]:
            sk2, _ = b.sketch(comp, L["w0"] - 0.5, "through cuts", L["thru"])
            b.extrude(comp, sk2, L["w1"] - L["w0"] + 1.0, CUT, body)
        for depth, shapes in L["blind"]:
            sk3, _ = b.sketch(comp, L["w0"], "blind %.1f from bottom" % depth, shapes)
            b.extrude(comp, sk3, depth, CUT, body)
        if L.get("fillet"):
            side, r = L["fillet"]
            try:
                _fillet(comp, _loop_edges(_planar_face(body, side == "top"), True), r)
            except Exception as e:
                print("warning: %s edge fillet R%.1f failed (%s) - round it over by hand" % (L["name"], r, e))
        if L.get("chamfer"):
            try:
                _chamfer(comp, _loop_edges(_planar_face(body, True), False), L["chamfer"])
            except Exception as e:
                print("warning: %s window/hole chamfer failed (%s)" % (L["name"], e))
        made.append(body)
    occ = cont.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    comp = occ.component
    comp.name = "hh_button_pushers"
    for p in g["pushers"]:
        body = b.solid(comp, p["name"], ("poly", p["poly"]), p["w0"], p["w1"])
        n = p["nub"]
        sk, _ = b.sketch(comp, n[5], p["name"] + " nub", [("rect", n[1], n[2], n[3], n[4], 0.0, False)])
        b.extrude(comp, sk, n[6] - n[5], JOIN, body)
        made.append(body)
    occ = cont.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    comp = occ.component
    comp.name = "hh_placeholders"
    for name, sh in g["placeholders"]:
        body = b.solid(comp, name, sh, 0, 0)
        try:
            body.opacity = 0.5
        except Exception:
            pass
        made.append(body)
    return cont_occ, made, spline_pts


def _spline_uv(fr, spl):
    try:
        ev = spl.worldGeometry.evaluator
        ok, p0, p1 = ev.getParameterExtents()
        ok, pts = ev.getStrokes(p0, p1, 0.002)
        return [fr.uvw(p.x * 10, p.y * 10, p.z * 10)[:2] for p in pts]
    except Exception as e:
        print("note: could not sample the outline spline (%s)" % e)
        return None


# ------------------------------------------------------------------ entry points
def main(dry=True, hand=None, replace=False):
    hand = hand or HAND
    if hand not in ("right", "left"):
        raise ValueError("hand must be 'right' or 'left'")
    if adsk is None:
        fr, bx = step_board(hand)
        g = plan(bx, hand)
        print_plan(g, bx)
        print("(outside Fusion: STEP numbers, nothing built)")
        return g
    app, des, root = _guard()
    fr, bx, board, _ = read_board(root, hand)
    g = plan(bx, hand)
    old = _find_container(root)
    if dry:
        print_plan(g, bx)
        print("DRY RUN - would build container '%s' with %s, hh_button_pushers (%d), hh_placeholders (%d)%s"
              % (CONTAINER, ", ".join(L["name"] for L in g["layers"]), len(g["pushers"]), len(g["placeholders"]),
                 "; an old '%s' exists (pass replace=True)" % old.component.name if old else ""))
        return
    if old is not None:
        if not replace:
            raise RuntimeError("'%s' already exists in this design - run with replace=True to rebuild it" % old.component.name)
        print("replacing the old '%s'" % old.component.name)
        old.deleteMe()
    cont_occ, made, spline_pts = build(des, root, fr, g)
    print_plan(g, bx, polyline=spline_pts, approx=spline_pts is None)
    summary(fr, made)
    print("built '%s' (not saved)" % cont_occ.component.name)
    try:
        check(brep=True, _ctx=(app, des, root, fr))
    except Exception:
        print("check() failed (the build is kept):\n" + traceback.format_exc())


def summary(fr, bodies):
    print("== parts ==")
    tot_m, boxes = 0.0, []
    for bd in bodies:
        bb = fr.box(_bb_mm(bd.boundingBox))
        vol = bd.volume
        wood = not bd.name.startswith("ph_")
        mass = vol * WOOD_DENSITY if wood else 0.0
        tot_m += mass
        if wood:
            boxes.append(bb)
        print("  %-18s %5.1f x %5.1f x %5.2f mm  %7.2f cm3%s" % (bd.name, bb[1] - bb[0], bb[3] - bb[2], bb[5] - bb[4], vol,
                                                              "  %.0f g" % mass if wood else "  (placeholder)"))
    if boxes:
        u = _union([(b[0], b[2], b[4], b[1], b[3], b[5]) for b in boxes])
        print("overall wood (incl. pushers): %.1f (u) x %.1f (v) x %.1f (w) mm, ~%.0f g wood"
              % (u[3] - u[0], u[4] - u[1], u[5] - u[2], tot_m))


def _bodies_of(occ):
    out = [occ.bRepBodies.item(i) for i in range(occ.bRepBodies.count)]
    for i in range(occ.childOccurrences.count):
        out += _bodies_of(occ.childOccurrences.item(i))
    return out


def check(brep=True, hand=None, _ctx=None):
    """Interference and clearance between the board, the stick and battery placeholders and the wood parts."""
    if _ctx:
        app, des, root, fr = _ctx
        board = _find_board(root)
        parts = _board_parts(root, board)
    else:
        app, des, root = _guard()
        fr, _, board, parts = read_board(root, hand or HAND)
    cont = _find_container(root)
    if cont is None:
        raise RuntimeError("no '%s' container in the active design - build it first" % CONTAINER)
    ours = _bodies_of(cont)
    wood = [bd for bd in ours if not bd.name.startswith("ph_")]
    phs = [bd for bd in ours if bd.name.startswith("ph_")]
    print("== check: %d wood bodies, %d placeholders ==" % (len(wood), len(phs)))
    # 1) bounding boxes vs the board box
    bb_board = _bb_mm(board.boundingBox)
    for bd in phs:
        o = _overlap(fr.box(_bb_mm(bd.boundingBox)), fr.box(bb_board))
        print("  bbox %-16s vs board: %s" % (bd.name, "overlap %s mm" % ", ".join("%.2f" % x for x in o) if o else "clear"))
    # 2) exact interference
    if brep:
        try:
            coll = adsk.core.ObjectCollection.create()
            coll.add(board)
            for bd in ours:
                coll.add(bd)
            inp = des.createInterferenceInput(coll)
            inp.areCoincidentFacesIncluded = False
            res = des.analyzeInterference(inp)
            bpref = board.fullPathName
            n = 0
            for i in range(res.count):
                r = res.item(i)
                l1, l2 = _label(r.entityOne), _label(r.entityTwo)
                if l1.startswith(bpref) and l2.startswith(bpref):
                    continue                       # the STEP model's own internal overlaps
                vol = r.interferenceBody.volume * 1000.0
                if vol < 1e-3:
                    continue
                n += 1
                print("  INTERFERENCE %.3f mm3: %s  <->  %s" % (vol, l1, l2))
            print("  brep interference: %s" % ("none" if n == 0 else "%d pair(s)" % n))
        except Exception as e:
            print("  brep interference failed (%s) - bounding boxes only" % e)
    # 3) clearances
    mm_ = app.measureManager
    key = [("display", parts.get("display")), ("usb", parts.get("usb")), ("switches", parts.get("sw")),
           ("sd", parts.get("sd")), ("batt plug", parts.get("conn"))]
    print("  min distance (mm), board parts / placeholders -> nearest wood part:")
    for name, lst in key:
        if not lst:
            continue
        bodies = [bd for _, o in lst for bd in _bodies_of(o)]
        print("    %-10s %s" % (name, _nearest(mm_, bodies, wood)))
    for bd in phs:
        print("    %-10s %s" % (bd.name.replace("ph_", ""), _nearest(mm_, [bd], wood)))


def _label(e):
    try:
        ctx = e.assemblyContext
        return (ctx.fullPathName + "/" if ctx else "") + e.name
    except Exception:
        return getattr(e, "name", str(e))


def _nearest(mm_, bodies, wood):
    best = None
    for a in bodies:
        for w in wood:
            try:
                d = mm_.measureMinimumDistance(a, w).value * 10.0
            except Exception:
                continue
            if best is None or d < best[0]:
                best = (d, w.name)
    return "n/a" if best is None else "%.2f to %s%s" % (best[0], best[1], "  (contact)" if best[0] < 0.005 else "")


def run(_context):
    """Fusion 'Scripts and Add-Ins' entry: dry run only (use frun.mjs for a build)."""
    try:
        main(dry=True)
    except Exception:
        print(traceback.format_exc())


if __name__ == "__main__":
    main(dry=True)
