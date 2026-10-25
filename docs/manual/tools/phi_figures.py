#!/usr/bin/env python3
"""Draws the diagrams of the manual's Φ chapter as SVG files in images/phi/.

    python3 phi_figures.py

Every number is computed here from φ itself, the same way PHI computes it.
"""
import math
from pathlib import Path

PHI = (1 + 5 ** 0.5) / 2
GOLD = "#c8a24a"
GOLD_SOFT = "#e9dcb6"
INK = "#1a1a1c"
DIM = "#8a8578"
BLACK = "#0b0b0c"
FONT = "font-family='Inter, Helvetica, Arial, sans-serif'"
SERIF = "font-family='EB Garamond, Garamond, Georgia, serif'"
OUT = Path(__file__).resolve().parent.parent / "images" / "phi"


def svg(name, w, h, body, bg=None):
    OUT.mkdir(parents=True, exist_ok=True)
    back = f"<rect width='{w}' height='{h}' fill='{bg}'/>" if bg else ""
    (OUT / f"{name}.svg").write_text(
        f"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 {w} {h}' width='{w}' height='{h}'>{back}{body}</svg>\n")


def golden_rectangle():
    """The golden rectangle cut into squares, with the spiral of quarter circles through them."""
    W, H = 640, 640 / PHI
    pad = 20
    parts = []
    x, y, w, h = pad, pad, W, H
    arcs = []
    d = 0   # direction: 0 square on the left, 1 top, 2 right, 3 bottom
    for i in range(10):
        s = min(w, h)
        if d == 0:
            sq = (x, y, s, s); x += s; w -= s
            arcs.append((sq[0] + s, sq[1] + s, s, 180, 270))
        elif d == 1:
            sq = (x, y, s, s); y += s; h -= s
            arcs.append((sq[0], sq[1] + s, s, 270, 360))
        elif d == 2:
            sq = (x + w - s, y, s, s); w -= s
            arcs.append((sq[0], sq[1], s, 0, 90))
        else:
            sq = (x, y + h - s, s, s); h -= s
            arcs.append((sq[0] + s, sq[1], s, 90, 180))
        shade = 0.10 + 0.07 * (i % 2)
        parts.append(f"<rect x='{sq[0]:.2f}' y='{sq[1]:.2f}' width='{sq[2]:.2f}' height='{sq[3]:.2f}' "
                     f"fill='{GOLD}' fill-opacity='{shade:.2f}' stroke='{GOLD}' stroke-width='1'/>")
        d = (d + 1) % 4
    path = []
    for cx, cy, r, a0, a1 in arcs:
        x0 = cx + r * math.cos(math.radians(a0)); y0 = cy + r * math.sin(math.radians(a0))
        x1 = cx + r * math.cos(math.radians(a1)); y1 = cy + r * math.sin(math.radians(a1))
        if not path:
            path.append(f"M{x0:.2f},{y0:.2f}")
        path.append(f"A{r:.2f},{r:.2f} 0 0 1 {x1:.2f},{y1:.2f}")
    parts.append(f"<path d='{' '.join(path)}' fill='none' stroke='{INK}' stroke-width='2.2'/>")
    parts.append(f"<text x='{pad + W / 2:.1f}' y='{pad + H + 26:.1f}' text-anchor='middle' {FONT} font-size='13' fill='{DIM}'>"
                 f"1 : φ = 1 : 1.618…  —  cut off a square and a smaller golden rectangle is left, for ever</text>")
    svg("golden-rectangle", W + 2 * pad, H + 2 * pad + 22, "".join(parts))


def sunflower():
    """Points placed by turning the golden angle each time: the pattern of a sunflower head."""
    n = 610   # a Fibonacci number
    size = 520
    c = size / 2
    golden_angle = math.pi * (3 - 5 ** 0.5)
    parts = []
    for i in range(1, n + 1):
        r = 245 * math.sqrt(i / n)
        a = i * golden_angle
        x, y = c + r * math.cos(a), c + r * math.sin(a)
        rad = 2.0 + 4.2 * math.sqrt(i / n)
        op = 0.35 + 0.65 * (i / n)
        parts.append(f"<circle cx='{x:.2f}' cy='{y:.2f}' r='{rad:.2f}' fill='{GOLD}' fill-opacity='{op:.2f}'/>")
    svg("sunflower", size, size, "".join(parts), BLACK)


def golden_angle():
    """The circle divided in the golden ratio: 222.5° and 137.5°."""
    W, H = 560, 300
    cx, cy, r = 150, 150, 115
    ga = 360 / PHI ** 2
    big = 360 - ga
    def pt(deg, rr=r):
        a = math.radians(-90 + deg)
        return cx + rr * math.cos(a), cy + rr * math.sin(a)
    x1, y1 = pt(0); x2, y2 = pt(ga)
    parts = [
        f"<circle cx='{cx}' cy='{cy}' r='{r}' fill='{GOLD}' fill-opacity='0.12' stroke='{GOLD}' stroke-width='1.5'/>",
        f"<path d='M{cx},{cy} L{x1:.2f},{y1:.2f} A{r},{r} 0 0 1 {x2:.2f},{y2:.2f} Z' fill='{GOLD}' fill-opacity='0.55' stroke='{GOLD}'/>",
    ]
    lx, ly = pt(ga / 2, r * 0.58)
    parts.append(f"<text x='{lx:.1f}' y='{ly:.1f}' text-anchor='middle' {SERIF} font-size='19' fill='{INK}'>137.5°</text>")
    lx, ly = pt(ga + big / 2, r * 0.55)
    parts.append(f"<text x='{lx:.1f}' y='{ly:.1f}' text-anchor='middle' {SERIF} font-size='19' fill='{INK}'>222.5°</text>")
    tx = 310
    lines = [("The golden angle", 600, 20),
             ("360° ÷ φ² = 137.507 764…°", 400, 17),
             ("= π (3 − √5) = 2.399 963… rad", 400, 17),
             ("222.5° : 137.5° = φ : 1", 400, 17),
             ("Turn by it again and again and", 400, 14),
             ("no two turns ever line up.", 400, 14)]
    y = 70
    for text, weight, size in lines:
        fam = SERIF if size >= 17 else FONT
        parts.append(f"<text x='{tx}' y='{y}' {fam} font-size='{size}' font-weight='{weight}' fill='{INK if size > 14 else DIM}'>{text}</text>")
        y += 34 if size >= 17 else 22
    svg("golden-angle", W, H, "".join(parts))


def log_x(hz, x0, x1, lo=20.0, hi=20000.0):
    return x0 + (x1 - x0) * (math.log10(hz) - math.log10(lo)) / (math.log10(hi) - math.log10(lo))


def phi_eq():
    """The Φ EQ's twelve bands on a log frequency axis."""
    W, H = 680, 210
    x0, x1, y0 = 40, 650, 150
    parts = [f"<line x1='{x0}' y1='{y0}' x2='{x1}' y2='{y0}' stroke='{DIM}'/>"]
    for hz, label in [(20, "20"), (50, "50"), (100, "100"), (200, "200"), (500, "500"), (1000, "1k"), (2000, "2k"),
                      (5000, "5k"), (10000, "10k"), (20000, "20k")]:
        x = log_x(hz, x0, x1)
        parts.append(f"<line x1='{x:.1f}' y1='{y0}' x2='{x:.1f}' y2='{y0 + 5}' stroke='{DIM}'/>"
                     f"<text x='{x:.1f}' y='{y0 + 20}' text-anchor='middle' {FONT} font-size='11' fill='{DIM}'>{label}</text>")
    for n in range(12):
        hz = 80 * PHI ** n
        x = log_x(hz, x0, x1)
        if n in (0, 11):
            col, h = INK, 70
        else:
            col, h = GOLD, 92
        parts.append(f"<line x1='{x:.1f}' y1='{y0}' x2='{x:.1f}' y2='{y0 - h}' stroke='{col}' stroke-width='3' stroke-linecap='round'/>")
        lab = f"{hz:.0f}" if hz < 1000 else f"{hz / 1000:.1f}k"
        parts.append(f"<text x='{x:.1f}' y='{y0 - h - 8}' text-anchor='middle' {FONT} font-size='10.5' fill='{INK}'>{lab}</text>")
        if 0 < n < 11:
            q = PHI ** 1.5
            half = hz / (2 * q)
            xa, xb = log_x(hz - half, x0, x1), log_x(hz + half, x0, x1)
            parts.append(f"<path d='M{xa:.1f},{y0} Q{x:.1f},{y0 - 70} {xb:.1f},{y0}' fill='{GOLD}' fill-opacity='0.16' stroke='none'/>")
    parts.append(f"<text x='{x0}' y='{H - 8}' {FONT} font-size='11.5' fill='{DIM}'>Super Low shelf 80 Hz · ten bells at 80 Hz × φⁿ · Super High shelf 80 Hz × φ¹¹ = 15.9 kHz · each step ≈ 0.69 octave</text>")
    svg("phi-eq", W, H, "".join(parts))


def echo_taps():
    """Φ Echo: the taps in time (top) and their places in the stereo field (bottom)."""
    W, H = 680, 300
    x0, x1 = 40, 650
    t1 = 250.0
    taps = [t1 * PHI ** k for k in range(5)]
    span = taps[-1] * 1.08
    parts = []
    y = 110
    parts.append(f"<line x1='{x0}' y1='{y}' x2='{x1}' y2='{y}' stroke='{DIM}'/>")
    parts.append(f"<line x1='{x0}' y1='{y}' x2='{x0}' y2='{y - 95}' stroke='{INK}' stroke-width='3'/>")
    parts.append(f"<text x='{x0}' y='{y + 18}' text-anchor='middle' {FONT} font-size='11' fill='{DIM}'>0</text>")
    level = 1.0
    for k, t in enumerate(taps):
        x = x0 + (x1 - x0) * t / span
        level *= 1 / PHI
        h = 95 * level
        parts.append(f"<line x1='{x:.1f}' y1='{y}' x2='{x:.1f}' y2='{y - h:.1f}' stroke='{GOLD}' stroke-width='3' stroke-linecap='round'/>")
        parts.append(f"<text x='{x:.1f}' y='{y + 18}' text-anchor='middle' {FONT} font-size='11' fill='{INK}'>{t:.0f} ms</text>")
        parts.append(f"<text x='{x:.1f}' y='{y - h - 7:.1f}' text-anchor='middle' {SERIF} font-size='13' fill='{DIM}'>φ{['⁰','¹','²','³','⁴'][k]}</text>")
    parts.append(f"<text x='{x0}' y='22' {FONT} font-size='12' font-weight='600' fill='{INK}'>Time: each tap φ later than the last, and 1/φ quieter</text>")
    # Stereo field from above
    cy, cx, r = 225, W / 2, 52
    parts.append(f"<text x='{x0}' y='165' {FONT} font-size='12' font-weight='600' fill='{INK}'>Space: each tap turned by the golden angle (137.5°), folded onto the stereo field</text>")
    parts.append(f"<line x1='{cx - 200}' y1='{cy}' x2='{cx + 200}' y2='{cy}' stroke='{DIM}'/>")
    parts.append(f"<text x='{cx - 210}' y='{cy + 4}' text-anchor='end' {FONT} font-size='12' fill='{DIM}'>L</text>")
    parts.append(f"<text x='{cx + 210}' y='{cy + 4}' {FONT} font-size='12' fill='{DIM}'>R</text>")
    ga = 2 * math.pi / PHI ** 2
    for k in range(5):
        pan = math.sin(k * ga)       # -1..1
        x = cx + 200 * pan
        parts.append(f"<circle cx='{x:.1f}' cy='{cy}' r='{9 - k}' fill='{GOLD}' fill-opacity='{1 - k * 0.15:.2f}'/>")
        parts.append(f"<text x='{x:.1f}' y='{cy + 26}' text-anchor='middle' {FONT} font-size='10.5' fill='{INK}'>{k + 1}</text>")
    parts.append(f"<text x='{x0}' y='{H - 10}' {FONT} font-size='11' fill='{DIM}'>The defaults: 250 ms, five taps, decay 1/φ. (The pan positions shown are illustrative of the golden-angle spread.)</text>")
    svg("echo-taps", W, H, "".join(parts))


def fibonacci_word(n):
    # Sturmian word of slope 1/φ: 1 where floor((k+2)/φ) - floor((k+1)/φ) == 1
    out = []
    for k in range(n):
        out.append(1 if math.floor((k + 2) / PHI) - math.floor((k + 1) / PHI) == 1 else 0)
    return out


def rhythm():
    """Φ Rhythm: four bars of sixteenths at each density."""
    W = 680
    steps = 32
    cell = 18
    x0 = 120
    dens = [(1 / PHI, "61.8 %  1/φ"), (1 / PHI ** 2, "38.2 %  1/φ²"), (1 / PHI ** 3, "23.6 %  1/φ³"), (1 / PHI ** 4, "14.6 %  1/φ⁴")]
    parts = []
    y = 30
    for d, label in dens:
        parts.append(f"<text x='{x0 - 10}' y='{y + 13}' text-anchor='end' {FONT} font-size='11.5' fill='{INK}'>{label}</text>")
        for s in range(steps):
            hit = math.ceil((s + 1) * d) != math.ceil(s * d)
            x = x0 + s * cell
            beat = s % 4 == 0
            parts.append(f"<rect x='{x + 1}' y='{y}' width='{cell - 3}' height='{cell - 3}' rx='3' "
                         f"fill='{GOLD if hit else 'none'}' stroke='{GOLD if hit else (DIM if beat else '#cfc9ba')}' stroke-width='1.2'/>")
        y += 32
    parts.append(f"<text x='{x0}' y='{y + 10}' {FONT} font-size='11' fill='{DIM}'>Two bars of sixteenths. Hits never clump and never fall into a loop, yet each row keeps its exact share of the steps.</text>")
    svg("rhythm", W, y + 24, "".join(parts))


def phi_in_octave():
    """Where φ and its powers fall on the piano, folded into one octave."""
    W, H = 680, 150
    x0, x1, y = 40, 640, 70
    parts = []
    names = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B", "C"]
    for i, nm in enumerate(names):
        x = x0 + (x1 - x0) * i / 12
        parts.append(f"<line x1='{x:.1f}' y1='{y - 22}' x2='{x:.1f}' y2='{y + 22}' stroke='{DIM if nm[-1] != '♯' else '#cfc9ba'}'/>")
        parts.append(f"<text x='{x:.1f}' y='{y + 38}' text-anchor='middle' {FONT} font-size='11' fill='{DIM}'>{nm}</text>")
    parts.append(f"<line x1='{x0}' y1='{y}' x2='{x1}' y2='{y}' stroke='{DIM}'/>")
    for n in range(8):
        cents = (n * 1200 * math.log2(PHI)) % 1200
        x = x0 + (x1 - x0) * cents / 1200
        parts.append(f"<circle cx='{x:.1f}' cy='{y}' r='6' fill='{GOLD}'/>")
        sup = "⁰¹²³⁴⁵⁶⁷"[n]
        parts.append(f"<text x='{x:.1f}' y='{y - 14}' text-anchor='middle' {SERIF} font-size='13' fill='{INK}'>φ{sup}</text>")
    parts.append(f"<text x='{x0}' y='{H - 8}' {FONT} font-size='11' fill='{DIM}'>Each power of φ is 833.1 cents (8.33 semitones) above the last: always between the keys, never on one.</text>")
    svg("phi-octave", W, H, "".join(parts))


def fib_sphere():
    """Points spread over a sphere by the golden angle, seen from the front."""
    size = 360
    c = size / 2
    n = 377
    ga = math.pi * (3 - 5 ** 0.5)
    tilt = math.radians(20)
    pts = []
    for i in range(n):
        z = 1 - (i + 0.5) * 2 / n
        r = math.sqrt(max(0.0, 1 - z * z))
        x, y = r * math.cos(ga * i), r * math.sin(ga * i)
        # tilt towards the viewer
        y2 = y * math.cos(tilt) - z * math.sin(tilt)
        z2 = y * math.sin(tilt) + z * math.cos(tilt)
        pts.append((x, y2, z2))
    pts.sort(key=lambda p: p[1])
    parts = [f"<circle cx='{c}' cy='{c}' r='{c - 14}' fill='none' stroke='{GOLD}' stroke-opacity='0.35'/>"]
    for x, y, z in pts:
        front = (y + 1) / 2
        parts.append(f"<circle cx='{c + x * (c - 14):.1f}' cy='{c - z * (c - 14):.1f}' r='{1.8 + 2.6 * front:.2f}' "
                     f"fill='{GOLD}' fill-opacity='{0.25 + 0.75 * front:.2f}'/>")
    svg("fibonacci-sphere", size, size, "".join(parts), BLACK)


def pd_phase():
    """Phase distortion: a straight phase gives a cosine; a bent phase gives a brighter wave."""
    W, H = 680, 230
    parts = []
    def plot(x0, y0, w, h, f, col, width=2):
        pts = []
        for i in range(201):
            t = i / 200
            v = f(t)
            pts.append(f"{x0 + w * t:.1f},{y0 + h / 2 - v * h / 2:.1f}")
        return f"<polyline points='{' '.join(pts)}' fill='none' stroke='{col}' stroke-width='{width}'/>"
    def bent(t, amount):
        knee = 0.5 - 0.49 * amount
        return t * 0.5 / knee if t < knee else 0.5 + (t - knee) * 0.5 / (1 - knee)
    boxes = [(30, "phase φ, straight", lambda t: 2 * t - 1, lambda t: -math.cos(2 * math.pi * t)),
             (360, "phase φ, bent (DCW up)", lambda t: 2 * bent(t, 0.85) - 1, lambda t: -math.cos(2 * math.pi * bent(t, 0.85)))]
    for x0, title, ph, wave in boxes:
        parts.append(f"<text x='{x0}' y='20' {FONT} font-size='12' font-weight='600' fill='{INK}'>{title}</text>")
        parts.append(f"<rect x='{x0}' y='32' width='290' height='70' fill='{GOLD}' fill-opacity='0.07'/>")
        parts.append(plot(x0, 32, 290, 70, ph, DIM, 1.6))
        parts.append(f"<rect x='{x0}' y='122' width='290' height='80' fill='{GOLD}' fill-opacity='0.07'/>")
        parts.append(plot(x0, 122, 290, 80, wave, GOLD, 2.4))
    parts.append(f"<text x='30' y='{H - 6}' {FONT} font-size='11' fill='{DIM}'>Top: the phase angle through one cycle. Bottom: the cosine read through it. Bending the phase is all phase distortion does.</text>")
    svg("phase-distortion", W, H, "".join(parts))


def main():
    golden_rectangle()
    sunflower()
    golden_angle()
    phi_eq()
    echo_taps()
    rhythm()
    phi_in_octave()
    fib_sphere()
    pd_phase()
    print("written to", OUT)


if __name__ == "__main__":
    main()
