"""DIY Play Device - wooden case prototype for Autodesk Fusion.

Run: Fusion > Utilities > Scripts and Add-Ins > "+" > pick this folder > Run.
It creates a new design with:
  Front plate (3 mm, laser)   - screen window, D-pad / A / B / Start / Select holes, speaker grille
  Frame       (14 mm, CNC)    - cavity, screw bosses, USB-C slot (right), power switch slot (left)
  Back plate  (3 mm, laser)   - 4 screw holes
  Wooden button caps          - D-pad (with pivot nub), A, B, Start, Select (flange keeps them in)
  Perfboard spacers           - glued under the front plate, perfboard screws to them
  Placeholders                - T-Display-S3 board, LiPo 503450, perfboard, tact switches, piezo
and writes DXF outlines of the plates and frame to ../dxf for LightBurn / MillMage.

All sizes are in mm in the constants below. Change them and run the script again.
Assembly: front plate + frame glued = shell; back plate screwed on with 4x M2 wood screws.
"""
import adsk.core, adsk.fusion, traceback, math, os

# ---------------- dimensions (mm) ----------------
W, H, R = 70.0, 118.0, 9.0          # outer size and corner radius
WALL = 4.0                           # frame wall
T_BACK, T_FRAME, T_FRONT = 3.0, 14.0, 3.0
Z_FRAME = T_BACK
Z_FRONT = T_BACK + T_FRAME
Z_TOP = Z_FRONT + T_FRONT
CLR = 0.3                            # clearance per side for caps in holes

# LilyGO T-Display-S3: 62 x 26.3 x 9.9 (from LilyGO STL). Lies across the top, USB-C to the right.
BOARD_L, BOARD_W, BOARD_T = 62.0, 26.3, 9.9
BOARD_Y = H - WALL - BOARD_W         # bottom edge of board
BOARD_CY = BOARD_Y + BOARD_W / 2
SCREEN_W, SCREEN_H = 43.5, 23.5      # window (active area 42.7 x 22.7)
SCREEN_DX, SCREEN_DY = -2.0, 0.0     # window offset from board centre - MEASURE YOUR BOARD
USB_W, USB_H, USB_ZC = 13.0, 8.0, Z_FRONT - 6.3   # slot for the plug overmould

# Controls
DPAD = (-17.0, 60.0); DPAD_SPAN, DPAD_ARM = 22.0, 7.5
BTN_A = (24.0, 64.0); BTN_B = (11.0, 56.0); BTN_D = 9.5
START = (7.0, 40.0); SELECT = (-7.0, 40.0); PILL_W, PILL_H = 8.0, 3.5
CAP_PROUD = 1.5                      # how far caps stand above the front
FLANGE_T, FLANGE_EXTRA = 1.5, 1.75   # flange thickness, overhang per side

# Inside
PERF_W, PERF_H, PERF_T, PERF_C = 60.0, 42.0, 1.6, (0.0, 55.0)
SW_BODY, SW_TOTAL = 3.5, 5.0          # 6x6 mm tact switch, 5 mm total height
Z_SW_TOP = Z_FRONT - FLANGE_T         # switch stems touch the cap flanges
Z_PERF_TOP = Z_SW_TOP - SW_TOTAL
SPACERS = [(-27, 37), (27, 37), (-27, 73), (27, 73)]; SPACER = 5.0
BATT = (50.0, 34.0, 5.0); BATT_C = (0.0, 24.0)
SPEAKER = (20.0, 20.0); PIEZO_D, PIEZO_T = 12.0, 3.0
PWR_Y, PWR_W, PWR_H, PWR_ZC = 30.0, 8.0, 4.0, Z_FRAME + 7.0

BOSSES = [(-29.0, 6.5), (29.0, 6.5), (-31.0, 83.5), (31.0, 83.5)]
BOSS_R, PILOT_D, PILOT_DEPTH, SCREW_CLR_D = 3.5, 1.6, 10.0, 2.4


# ---------------- helpers ----------------
def mm(v):
    return v / 10.0  # Fusion API works in cm


def P(x, y, z=0.0):
    return adsk.core.Point3D.create(mm(x), mm(y), mm(z))


def rrect(sk, cx, cy, w, h, r):
    """Closed rounded rectangle (r may equal h/2 or w/2 for a slot)."""
    r = min(r, w / 2, h / 2)
    arcs, lines = sk.sketchCurves.sketchArcs, sk.sketchCurves.sketchLines
    x1, x2, y1, y2 = cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2
    q = math.pi / 2
    br = arcs.addByCenterStartSweep(P(x2 - r, y1 + r), P(x2 - r, y1), q)
    tr = arcs.addByCenterStartSweep(P(x2 - r, y2 - r), P(x2, y2 - r), q)
    tl = arcs.addByCenterStartSweep(P(x1 + r, y2 - r), P(x1 + r, y2), q)
    bl = arcs.addByCenterStartSweep(P(x1 + r, y1 + r), P(x1, y1 + r), q)
    for a, b, length in ((br, tr, h - 2 * r), (tr, tl, w - 2 * r), (tl, bl, h - 2 * r), (bl, br, w - 2 * r)):
        if length > 1e-3:
            lines.addByTwoPoints(a.endSketchPoint, b.startSketchPoint)
        else:
            a.endSketchPoint.merge(b.startSketchPoint)


def circle(sk, cx, cy, d):
    sk.sketchCurves.sketchCircles.addByCenterRadius(P(cx, cy), mm(d / 2))


def cross(sk, cx, cy, span, arm):
    l, t = span / 2, arm / 2
    pts = [(t, -l), (t, -t), (l, -t), (l, t), (t, t), (t, l), (-t, l), (-t, t), (-l, t), (-l, -t), (-t, -t), (-t, -l)]
    lines = sk.sketchCurves.sketchLines
    first = prev = None
    for i in range(len(pts)):
        x0, y0 = pts[i]
        x1, y1 = pts[(i + 1) % len(pts)]
        if prev is None:
            ln = lines.addByTwoPoints(P(cx + x0, cy + y0), P(cx + x1, cy + y1))
            first = ln
        elif i == len(pts) - 1:
            ln = lines.addByTwoPoints(prev.endSketchPoint, first.startSketchPoint)
        else:
            ln = lines.addByTwoPoints(prev.endSketchPoint, P(cx + x1, cy + y1))
        prev = ln


def new_comp(root, name):
    occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    occ.component.name = name
    return occ.component


def sketch(comp, name, draw):
    sk = comp.sketches.add(comp.xYConstructionPlane)
    sk.name = name
    sk.isComputeDeferred = True
    draw(sk)
    sk.isComputeDeferred = False
    return sk


def all_profiles(sk):
    coll = adsk.core.ObjectCollection.create()
    for i in range(sk.profiles.count):
        coll.add(sk.profiles.item(i))
    return coll


def extrude(comp, sk, z0, h, op, target=None):
    ext = comp.features.extrudeFeatures
    inp = ext.createInput(all_profiles(sk), op)
    inp.setDistanceExtent(False, adsk.core.ValueInput.createByReal(mm(h)))
    inp.startExtent = adsk.fusion.OffsetStartDefinition.create(adsk.core.ValueInput.createByReal(mm(z0)))
    if target is not None:
        inp.participantBodies = [target]
    return ext.add(inp)


NEW = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
CUT = adsk.fusion.FeatureOperations.CutFeatureOperation
JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation


def solid(comp, name, draw, z0, h, opacity=None):
    feat = extrude(comp, sketch(comp, name, draw), z0, h, NEW)
    body = feat.bodies.item(0)
    body.name = name
    if opacity is not None:
        try:
            body.opacity = opacity
        except Exception:
            pass
    return body


def cut(comp, body, name, draw, z0, h):
    extrude(comp, sketch(comp, name, draw), z0, h, CUT, body)


def export_top_outline(comp, body, path):
    """Project the top face of a body into a sketch and save it as DXF."""
    top = None
    for i in range(body.faces.count):
        f = body.faces.item(i)
        g = f.geometry
        if g.objectType == adsk.core.Plane.classType() and g.normal.z > 0.99:
            if top is None or f.pointOnFace.z > top.pointOnFace.z:
                top = f
    if top is None:
        return False
    sk = comp.sketches.add(comp.xYConstructionPlane)
    sk.name = "DXF outline"
    sk.project(top)
    sk.saveAsDXF(path)
    sk.isVisible = False
    return True


# ---------------- parts ----------------
def outline(sk):
    rrect(sk, 0, H / 2, W, H, R)


def front_holes(sk):
    rrect(sk, SCREEN_DX, BOARD_CY + SCREEN_DY, SCREEN_W, SCREEN_H, 1.0)
    cross(sk, DPAD[0], DPAD[1], DPAD_SPAN + 2 * CLR, DPAD_ARM + 2 * CLR)
    for c in (BTN_A, BTN_B):
        circle(sk, c[0], c[1], BTN_D + 2 * CLR)
    for c in (START, SELECT):
        rrect(sk, c[0], c[1], PILL_W + 2 * CLR, PILL_H + 2 * CLR, (PILL_H + 2 * CLR) / 2)
    circle(sk, SPEAKER[0], SPEAKER[1], 1.6)
    for k in range(6):
        a = k * math.pi / 3
        circle(sk, SPEAKER[0] + 3.5 * math.cos(a), SPEAKER[1] + 3.5 * math.sin(a), 1.6)


def build(root, dxf_dir):
    report = []

    # Front plate
    c = new_comp(root, "Front plate 3mm")
    front = solid(c, "Front plate", outline, Z_FRONT, T_FRONT)
    cut(c, front, "Front holes", front_holes, Z_FRONT - 1, T_FRONT + 2)
    report.append(("front_plate.dxf", c, front))

    # Frame
    c = new_comp(root, "Frame 14mm")
    frame = solid(c, "Frame", outline, Z_FRAME, T_FRAME)
    cut(c, frame, "Cavity", lambda sk: rrect(sk, 0, H / 2, W - 2 * WALL, H - 2 * WALL, R - WALL), Z_FRAME - 1, T_FRAME + 2)
    extrude(c, sketch(c, "Bosses", lambda sk: [circle(sk, x, y, 2 * BOSS_R) for x, y in BOSSES]),
            Z_FRAME, T_FRAME, JOIN, frame)
    cut(c, frame, "Pilot holes", lambda sk: [circle(sk, x, y, PILOT_D) for x, y in BOSSES], Z_FRAME - 1, PILOT_DEPTH + 1)
    cut(c, frame, "USB-C slot", lambda sk: rrect(sk, W / 2 - WALL / 2, BOARD_CY, WALL + 2, USB_W, 0.5),
        USB_ZC - USB_H / 2, USB_H)
    cut(c, frame, "Power switch slot", lambda sk: rrect(sk, -W / 2 + WALL / 2, PWR_Y, WALL + 2, PWR_W, 0.5),
        PWR_ZC - PWR_H / 2, PWR_H)
    report.append(("frame.dxf", c, frame))

    # Back plate
    c = new_comp(root, "Back plate 3mm")
    back = solid(c, "Back plate", outline, 0, T_BACK)
    cut(c, back, "Screw holes", lambda sk: [circle(sk, x, y, SCREW_CLR_D) for x, y in BOSSES], -1, T_BACK + 2)
    report.append(("back_plate.dxf", c, back))

    # Button caps (wood): top part through the hole + flange under the front plate
    caps = new_comp(root, "Button caps")
    top_h = T_FRONT + CAP_PROUD
    solid(caps, "D-pad", lambda sk: cross(sk, DPAD[0], DPAD[1], DPAD_SPAN, DPAD_ARM), Z_FRONT, top_h)
    solid(caps, "D-pad flange", lambda sk: cross(sk, DPAD[0], DPAD[1], DPAD_SPAN + 2 * FLANGE_EXTRA,
                                                 DPAD_ARM + 2 * FLANGE_EXTRA), Z_FRONT - FLANGE_T, FLANGE_T)
    nub_h = Z_SW_TOP - Z_PERF_TOP - 0.1  # pivot rests on the perfboard so the D-pad rocks
    solid(caps, "D-pad pivot", lambda sk: circle(sk, DPAD[0], DPAD[1], 4.0), Z_SW_TOP - nub_h, nub_h)
    for name, (x, y) in (("A", BTN_A), ("B", BTN_B)):
        solid(caps, name, lambda sk, x=x, y=y: circle(sk, x, y, BTN_D), Z_FRONT, top_h)
        solid(caps, name + " flange", lambda sk, x=x, y=y: circle(sk, x, y, BTN_D + 2 * FLANGE_EXTRA),
              Z_FRONT - FLANGE_T, FLANGE_T)
    for name, (x, y) in (("Start", START), ("Select", SELECT)):
        solid(caps, name, lambda sk, x=x, y=y: rrect(sk, x, y, PILL_W, PILL_H, PILL_H / 2), Z_FRONT, top_h)
        fw, fh = PILL_W + 2 * FLANGE_EXTRA, PILL_H + 2 * FLANGE_EXTRA
        solid(caps, name + " flange", lambda sk, x=x, y=y, fw=fw, fh=fh: rrect(sk, x, y, fw, fh, fh / 2),
              Z_FRONT - FLANGE_T, FLANGE_T)

    # Spacers glued under the front plate; perfboard is screwed to them from below
    sp = new_comp(root, "Perfboard spacers")
    solid(sp, "Spacers", lambda sk: [rrect(sk, x, y, SPACER, SPACER, 0.5) for x, y in SPACERS],
          Z_PERF_TOP, Z_FRONT - Z_PERF_TOP)

    # Placeholders (not to be made) - semi-transparent
    ph = new_comp(root, "Placeholders (bought parts)")
    solid(ph, "T-Display-S3", lambda sk: rrect(sk, 0, BOARD_CY, BOARD_L, BOARD_W, 1.0),
          Z_FRONT - BOARD_T, BOARD_T, 0.5)
    solid(ph, "LiPo 503450", lambda sk: rrect(sk, BATT_C[0], BATT_C[1], BATT[0], BATT[1], 2.0),
          Z_FRAME, BATT[2], 0.5)
    solid(ph, "Perfboard", lambda sk: rrect(sk, PERF_C[0], PERF_C[1], PERF_W, PERF_H, 0.5),
          Z_PERF_TOP - PERF_T, PERF_T, 0.5)
    sw = [(DPAD[0] + 7.5, DPAD[1]), (DPAD[0] - 7.5, DPAD[1]), (DPAD[0], DPAD[1] + 7.5), (DPAD[0], DPAD[1] - 7.5),
          BTN_A, BTN_B, START, SELECT]
    solid(ph, "Tact switches", lambda sk: [rrect(sk, x, y, 6.0, 6.0, 0.3) for x, y in sw],
          Z_PERF_TOP, SW_BODY, 0.5)
    solid(ph, "Switch stems", lambda sk: [circle(sk, x, y, 3.5) for x, y in sw],
          Z_PERF_TOP + SW_BODY, SW_TOTAL - SW_BODY, 0.5)
    solid(ph, "Piezo", lambda sk: circle(sk, SPEAKER[0], SPEAKER[1], PIEZO_D), Z_FRONT - PIEZO_T, PIEZO_T, 0.5)

    # DXF export
    written = []
    os.makedirs(dxf_dir, exist_ok=True)
    for fname, comp, body in report:
        path = os.path.join(dxf_dir, fname)
        if export_top_outline(comp, body, path):
            written.append(fname)
    return written


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        doc.name = "DIY Play Device case"
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        here = os.path.dirname(os.path.realpath(__file__))
        dxf_dir = os.path.normpath(os.path.join(here, "..", "..", "dxf"))
        written = build(design.rootComponent, dxf_dir)
        app.activeViewport.fit()
        ui.messageBox("Case built: %.0f x %.0f x %.0f mm.\nDXF written to:\n%s\n%s"
                      % (W, H, Z_TOP + CAP_PROUD, dxf_dir, "\n".join(written)))
    except Exception:
        if ui:
            ui.messageBox("Failed:\n{}".format(traceback.format_exc()))
