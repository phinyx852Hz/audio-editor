# SPDX-License-Identifier: AGPL-3.0-only
#
# PHINYX artwork for Cardinal. Draws a PHINYX version of every file in VCV
# Rack's component library (knobs, jacks, switches, buttons, lights, sliders,
# screws, cable plugs, rails), whose own artwork is CC-BY-NC-4.0, over the
# originals before Cardinal is built. Also restyles VCV Fundamental's knobs,
# jacks and buttons to match (its panels stay) and gives the cables PHINYX's colours.
# Run in Cardinal's source folder:
#   python3 phinyx_skin.py
#
# Everything is plain SVG that Rack's renderer (nanosvg) draws: paths, solid
# fills and strokes, linear and radial gradients and opacity; no filters,
# images or <text>. Knobs follow PHINYX's Potentiometer (Source/ui/Potentiometer.h):
# a track around the travel and a line to the current position.

import math
import os
import re
import sys

DARK = dict(
    window="#1e2128", base="#15171c", alt="#23262e", button="#2a2e37", mid="#3a3f4a",
    text="#dddddd", label="#a3a9b4", highlight="#4fc3f7", accent="#ffb74d",
    green="#66bb6a", red="#ef5350", hole="#0b0c0f", top="#262a33", bottom="#15171c",
)
LIGHT = dict(
    window="#eceff4", base="#ffffff", alt="#f3f5f8", button="#e2e6ed", mid="#c5cad3",
    text="#1f2329", label="#4b515b", highlight="#1e88e5", accent="#e65100",
    green="#2e7d32", red="#c62828", hole="#1f2329", top="#f6f8fb", bottom="#dfe3ea",
)


def svg(w, h, body, defs="", units="px"):
    """A document w x h user units big; units "mm" keeps a light's size in millimetres."""
    d = f"<defs>{defs}</defs>" if defs else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:g}{units}" height="{h:g}{units}" '
            f'viewBox="0 0 {w:g} {h:g}">{d}{body}</svg>\n')


def polar(cx, cy, r, deg):
    """deg clockwise from 12 o'clock, the way Rack turns its knobs."""
    a = math.radians(deg)
    return cx + r * math.sin(a), cy - r * math.cos(a)


def arc(cx, cy, r, a0, a1):
    x0, y0 = polar(cx, cy, r, a0)
    x1, y1 = polar(cx, cy, r, a1)
    large = 1 if abs(a1 - a0) > 180 else 0
    sweep = 1 if a1 > a0 else 0
    return f"M{x0:.2f} {y0:.2f}A{r:.2f} {r:.2f} 0 {large} {sweep} {x1:.2f} {y1:.2f}"


def circle(cx, cy, r):
    """A full circle as a path."""
    return f"M{cx - r:.3f} {cy:.3f}a{r:.3f} {r:.3f} 0 1 0 {2 * r:.3f} 0a{r:.3f} {r:.3f} 0 1 0 {-2 * r:.3f} 0z"


def rounded(x, y, w, h, r):
    r = min(r, w / 2, h / 2)
    return (f"M{x + r:.3f} {y:.3f}h{w - 2 * r:.3f}a{r:.3f} {r:.3f} 0 0 1 {r:.3f} {r:.3f}v{h - 2 * r:.3f}"
            f"a{r:.3f} {r:.3f} 0 0 1 {-r:.3f} {r:.3f}h{-(w - 2 * r):.3f}a{r:.3f} {r:.3f} 0 0 1 {-r:.3f} {-r:.3f}"
            f"v{-(h - 2 * r):.3f}a{r:.3f} {r:.3f} 0 0 1 {r:.3f} {-r:.3f}z")


# --------------------------------------------------------------------------
# Controls

def knob_geometry(size):
    c = size / 2
    width = max(1.6, size * 0.075)
    return c, c - width / 2 - 0.3, width


def knob_bg(w, h, p, sweep):
    """The part that stays still: body and track."""
    size = min(w, h)
    c, r, tw = knob_geometry(size)
    cx, cy = w / 2, h / 2
    gid = f"body{int(size * 100)}"
    defs = (f'<radialGradient id="{gid}" cx="{cx - c * 0.2:.2f}" cy="{cy - c * 0.3:.2f}" r="{r:.2f}" '
            f'gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="{p["button"]}"/>'
            f'<stop offset="1" stop-color="{p["alt"]}"/></radialGradient>')
    body = f'<path d="{circle(cx, cy, r - tw * 0.9)}" fill="url(#{gid})"/>'
    body += (f'<path d="{arc(cx, cy, r, -sweep, sweep)}" fill="none" stroke="{p["mid"]}" '
             f'stroke-width="{tw:.2f}"/>')
    return svg(w, h, body, defs)


def knob_pointer(w, h, p, colour=None):
    """The part that turns: a line from a quarter of the radius out to the track."""
    size = min(w, h)
    c, r, tw = knob_geometry(size)
    cx, cy = w / 2, h / 2
    lw = max(1.4, size * 0.06)
    body = (f'<path d="M{cx:.2f} {cy - r * 0.25:.2f}L{cx:.2f} {cy - r:.2f}" stroke="{colour or p["text"]}" '
            f'stroke-width="{lw:.2f}" stroke-linecap="round" fill="none"/>')
    return svg(w, h, body)


def empty(w, h, units="px"):
    return svg(w, h, "", units=units)


def jack(w, h, p, ring=None):
    c, cy = w / 2, h / 2
    r = min(w, h) / 2
    body = f'<path d="{circle(c, cy, r - 0.6)}" fill="{p["button"]}" stroke="{ring or p["label"]}" stroke-width="1.2"/>'
    body += f'<path d="{circle(c, cy, r * 0.56)}" fill="{p["mid"]}"/>'
    body += f'<path d="{circle(c, cy, r * 0.38)}" fill="{p["hole"]}"/>'
    return svg(w, h, body)


def button(w, h, p, on, dot=True):
    """dot: draw the lit centre (off where Rack lights the button itself)."""
    edge = p["highlight"] if on else p["mid"]
    inset = 0.6
    r = min(w, h) * 0.16
    body = f'<path d="{rounded(inset, inset, w - 2 * inset, h - 2 * inset, r)}" fill="{p["button"]}" stroke="{edge}" stroke-width="1.2"/>'
    if on and dot:
        body += f'<path d="{circle(w / 2, h / 2, min(w, h) * 0.14)}" fill="{p["highlight"]}"/>'
    return svg(w, h, body)


def bezel(w, h, p):
    """Ring around a lit button; the light inside is drawn by Rack."""
    r = min(w, h) / 2
    body = f'<path d="{circle(w / 2, h / 2, r - 0.7)}" fill="{p["button"]}" stroke="{p["mid"]}" stroke-width="1.4"/>'
    return svg(w, h, body)


def toggle(w, h, p, pos, positions, horizontal=False):
    """A switch: a slot with the lever at position pos of positions (0 is the bottom or left)."""
    inset = 0.6
    long_side, short_side = (w, h) if horizontal else (h, w)
    slot_w = short_side * 0.62
    body = ""
    if horizontal:
        body += f'<path d="{rounded(inset, (h - slot_w) / 2, w - 2 * inset, slot_w, slot_w / 2)}" fill="{p["base"]}" stroke="{p["mid"]}" stroke-width="1"/>'
    else:
        body += f'<path d="{rounded((w - slot_w) / 2, inset, slot_w, h - 2 * inset, slot_w / 2)}" fill="{p["base"]}" stroke="{p["mid"]}" stroke-width="1"/>'
    travel = long_side - 2 * inset - slot_w
    t = pos / (positions - 1) if positions > 1 else 0.5
    knob_r = slot_w / 2 - 1.2
    if horizontal:
        x, y = inset + slot_w / 2 + travel * t, h / 2
    else:
        x, y = w / 2, h - inset - slot_w / 2 - travel * t
    colour = p["highlight"] if pos > 0 else p["text"]
    body += f'<path d="{circle(x, y, knob_r)}" fill="{colour}"/>'
    return svg(w, h, body)


def slider_track(w, h, p):
    groove = min(w * 0.22, 4.5)
    body = f'<path d="{rounded((w - groove) / 2, 1, groove, h - 2, groove / 2)}" fill="{p["base"]}" stroke="{p["mid"]}" stroke-width="1"/>'
    for i in range(11):
        y = 6 + (h - 12) * i / 10
        tick = w * 0.18 if i % 5 else w * 0.26
        body += (f'<path d="M{(w - groove) / 2 - 2 - tick:.2f} {y:.2f}h{tick:.2f}M{(w + groove) / 2 + 2:.2f} {y:.2f}h{tick:.2f}" '
                 f'stroke="{p["mid"]}" stroke-width="0.8" fill="none"/>')
    return svg(w, h, body)


def slider_handle(w, h, p):
    """Like the handle of PHINYX's opacity slider: a rounded block with a centre line."""
    body = f'<path d="{rounded(0.4, 0.4, w - 0.8, h - 0.8, min(w, h) * 0.3)}" fill="{p["text"]}" stroke="{p["mid"]}" stroke-width="0.8"/>'
    body += f'<path d="M{w * 0.2:.2f} {h / 2:.2f}H{w * 0.8:.2f}" stroke="{p["highlight"]}" stroke-width="{max(0.8, h * 0.08):.2f}" fill="none"/>'
    return svg(w, h, body)


def lamp(w, h, p, units="px"):
    """The cap of a light; Rack draws the lit colour over it."""
    r = min(w, h) / 2
    body = f'<path d="{circle(w / 2, h / 2, r * 0.96)}" fill="{p["mid"]}"/>'
    return svg(w, h, body, units=units)


def screw(w, h, p):
    """A panel screw drawn as a Phi: a ring with a slot through it."""
    c = min(w, h) / 2
    cx, cy = w / 2, h / 2
    body = f'<path d="{circle(cx, cy, c * 0.8)}" fill="{p["button"]}" stroke="{p["mid"]}" stroke-width="0.8"/>'
    body += f'<path d="{circle(cx, cy, c * 0.42)}" fill="none" stroke="{p["label"]}" stroke-width="1"/>'
    body += f'<path d="M{cx:.3f} {cy - c * 0.68:.3f}V{cy + c * 0.68:.3f}" stroke="{p["label"]}" stroke-width="1" fill="none"/>'
    return svg(w, h, body)


def plug(w, h):
    """Cable plug: a round head over a tapering sleeve, in greys that Rack tints with the cable colour."""
    cx = w / 2
    r = w * 0.36
    cy = r + 1
    sleeve = (f"M{cx - r * 0.95:.2f} {cy + r * 0.3:.2f}L{cx - r * 0.42:.2f} {h - 1.5:.2f}"
              f"H{cx + r * 0.42:.2f}L{cx + r * 0.95:.2f} {cy + r * 0.3:.2f}z")
    body = f'<path d="{sleeve}" fill="#d6d6d6" stroke="#8a8a8a" stroke-width="0.8"/>'
    body += f'<path d="{circle(cx, cy, r)}" fill="#f2f2f2" stroke="#8a8a8a" stroke-width="0.8"/>'
    body += f'<path d="{circle(cx, cy, r * 0.55)}" fill="#c8c8c8"/>'
    return svg(w, h, body)


def plug_port(w, h):
    """The jack hole showing through the plug's head."""
    r = min(w, h) / 2
    body = f'<path d="{circle(w / 2, h / 2, r * 0.62)}" fill="#0b0c0f" fill-opacity="0.85"/>'
    return svg(w, h, body)


def rail(w, h, p, hc=False):
    """Rack background: the panel colour between two rails with mounting holes."""
    back = p["base"] if hc else p["window"]
    rail_h = 14
    body = f'<path d="M0 0H{w}V{h}H0z" fill="{back}"/>'
    for y in (0, h - rail_h):
        body += f'<path d="M0 {y}H{w}V{y + rail_h}H0z" fill="{p["button"]}"/>'
        body += f'<path d="M0 {y + (rail_h - 1 if y == 0 else 0.5)}H{w}" stroke="{p["mid"]}" stroke-width="1" fill="none"/>'
        for i in range(int(w // 15)):
            x = 7.5 + i * 15
            body += f'<path d="{rounded(x - 3, y + rail_h / 2 - 1.5, 6, 3, 1.5)}" fill="{p["hole"]}"/>'
    return svg(w, h, body)


def midi_din(w, h, p):
    cx, cy = w / 2, h / 2
    r = min(w, h) / 2
    body = f'<path d="{circle(cx, cy, r - 0.8)}" fill="{p["button"]}" stroke="{p["label"]}" stroke-width="1.4"/>'
    body += f'<path d="{circle(cx, cy, r * 0.72)}" fill="{p["hole"]}"/>'
    for a in (-90, -45, 0, 45, 90):
        x, y = polar(cx, cy, r * 0.48, a)
        body += f'<path d="{circle(x, y, r * 0.075)}" fill="{p["label"]}"/>'
    body += f'<path d="{rounded(cx - r * 0.12, cy + r * 0.5, r * 0.24, r * 0.24, 0.6)}" fill="{p["button"]}"/>'
    return svg(w, h, body)


def usb_b(w, h, p):
    inset = 1
    iw, ih = w - 2 * inset, h - 2 * inset
    body = f'<path d="{rounded(inset, inset, iw, ih, 2.5)}" fill="{p["button"]}" stroke="{p["label"]}" stroke-width="1.2"/>'
    x0, y0, sw, sh = w * 0.24, h * 0.24, w * 0.52, h * 0.5
    cut = sw * 0.22
    body += (f'<path d="M{x0 + cut:.2f} {y0:.2f}H{x0 + sw - cut:.2f}L{x0 + sw:.2f} {y0 + cut:.2f}V{y0 + sh:.2f}'
             f'H{x0:.2f}V{y0 + cut:.2f}z" fill="{p["hole"]}"/>')
    body += f'<path d="{rounded(w * 0.38, h * 0.36, w * 0.24, h * 0.3, 1)}" fill="{p["mid"]}"/>'
    return svg(w, h, body)


def adat(w, h, p):
    inset = 1
    body = f'<path d="{rounded(inset, inset, w - 2 * inset, h - 2 * inset, 2.5)}" fill="{p["button"]}" stroke="{p["label"]}" stroke-width="1.2"/>'
    body += f'<path d="{rounded(w * 0.28, h * 0.28, w * 0.44, h * 0.44, 2)}" fill="{p["hole"]}"/>'
    body += f'<path d="{circle(w / 2, h / 2, min(w, h) * 0.08)}" fill="{p["red"]}"/>'
    return svg(w, h, body)


# --------------------------------------------------------------------------
# VCV Rack's component library, file by file

def size_of(path):
    """Width and height of an SVG and whether they are in millimetres."""
    with open(path, errors="ignore") as f:
        head = re.search(r"<svg\b(.*?)>", f.read(), re.S).group(1)
    w = re.search(r'\swidth="([\d.]+)\s*(mm|px)?"', head)
    h = re.search(r'\sheight="([\d.]+)\s*(mm|px)?"', head)
    return float(w.group(1)), float(h.group(1)), (w.group(2) or "px")


KNOB_SWEEP = 0.83 * 180  # Rack's knobs turn from -0.83 pi to 0.83 pi


def pointer_colour(name, p):
    for word, key in (("Red", "red"), ("Blue", "highlight"), ("Green", "green"), ("White", "text")):
        if word in name:
            return p[key]
    return p["text"]


def component(name, w, h, units):
    """The PHINYX drawing for a component library file, or None when it is not one we know."""
    p = DARK
    knobs = ("Knob", "Rogan", "Davies", "Trimpot", "SynthTechAlco", "BefacoTinyPoint")
    if name in ("ScrewSilver",):
        return screw(w, h, LIGHT)
    if name in ("ScrewBlack",):
        return screw(w, h, DARK)
    if name in ("PJ301M", "PJ301M-dark", "PJ3410", "CL1362"):
        return jack(w, h, DARK)
    if name == "Plug":
        return plug(w, h)
    if name == "PlugPort":
        return plug_port(w, h)
    if name == "Rail":
        return rail(w, h, DARK)
    if name == "Rail-hcdark":
        return rail(w, h, DARK, hc=True)
    if name == "Rail-light":
        return rail(w, h, LIGHT)
    if name == "MIDI_DIN":
        return midi_din(w, h, p)
    if name == "USB_B":
        return usb_b(w, h, p)
    if name == "ADAT":
        return adat(w, h, p)
    if name.endswith("Light") and "Slider" not in name:
        return lamp(w, h, p, units)
    if name == "VCVBezel":
        return bezel(w, h, p)
    if name == "VCVSliderLight":
        return lamp(w, h, p, units)
    if name in ("VCVSlider", "BefacoSlidePot"):
        return slider_track(w, h, p)
    if name in ("VCVSliderHandle", "BefacoSlidePotHandle"):
        return slider_handle(w, h, p)
    m = re.match(r"(CKSSThreeHorizontal|CKSSThree|CKSS|NKK|BefacoSwitch)(?:_(\d))?$", name)
    if m:
        positions = 2 if m.group(1) == "CKSS" else 3
        pos = int(m.group(2)) if m.group(2) else 1
        return toggle(w, h, p, pos, positions, horizontal=m.group(1) == "CKSSThreeHorizontal")
    m = re.match(r"(VCVButton|TL1105|CKD6|BefacoPush|PB61303)(?:_(\d))?$", name)
    if m:
        return button(w, h, p, on=m.group(2) == "1")
    if any(k in name for k in knobs):
        if name.endswith("_bg"):
            return knob_bg(w, h, p, KNOB_SWEEP)
        if name.endswith("_fg"):
            return empty(w, h)
        return knob_pointer(w, h, p, pointer_colour(name, p))
    return None


def component_library(folder):
    names = sorted(f[:-4] for f in os.listdir(folder) if f.endswith(".svg"))
    unknown = []
    for name in names:
        path = os.path.join(folder, name + ".svg")
        w, h, units = size_of(path)
        art = component(name, w, h, units)
        if art is None:
            unknown.append(name)
            continue
        with open(path, "w") as f:
            f.write(art)
    if unknown:
        # A file we do not know would ship VCV's non-commercial artwork: stop the build.
        sys.exit("phinyx_skin.py: no PHINYX drawing for component library file(s): " + ", ".join(unknown))
    return len(names)


# --------------------------------------------------------------------------
# VCV Fundamental's own controls (GPL-3.0-or-later), restyled to match. The
# panels stay as they are.

FUNDAMENTAL_SWEEP = 135.0  # PHINYX's 270 degree travel, set on the knobs below


def edit(path, old, new, marker):
    """Replace old with new in a source file once; marker shows it is done."""
    with open(path) as f:
        source = f.read()
    if marker in source:
        return
    if old not in source:
        sys.exit(f"phinyx_skin.py: {path} has changed, its PHINYX edit needs updating")
    with open(path, "w") as f:
        f.write(source.replace(old, new, 1))


KNOB_VALUE = """
	// PHINYX: the value along the track in the highlight colour, from zero on a
	// knob that goes both ways, and a dot at each step of a stepped knob (phinyx_skin.py).
	void drawPhinyxValue(const DrawArgs& args) {
		ParamQuantity* const pq = getParamQuantity();
		if (pq == nullptr || ! (pq->getMaxValue() > pq->getMinValue()))
			return;
		const float lo = pq->getMinValue(), hi = pq->getMaxValue();
		// The track as phinyx_skin.py draws it in the 18 or 68 unit SVG, scaled to the knob.
		const float design = size <= 22 ? 18.f : 68.f;
		const float k = size / design;
		const float track = std::max(1.6f, design * 0.075f);
		const float width = track * k;
		const float radius = (design * 0.5f - track * 0.5f - 0.3f) * k;
		const float c = size * 0.5f;
		auto angle = [&](float t) { return minAngle + (maxAngle - minAngle) * t - float(M_PI) * 0.5f; };
		const float origin = lo < 0.f && hi > 0.f ? -lo / (hi - lo) : 0.f;
		const float v = pq->getScaledValue();
		if (std::fabs(v - origin) > 0.002f) {
			nvgBeginPath(args.vg);
			nvgArc(args.vg, c, c, radius, angle(std::min(v, origin)), angle(std::max(v, origin)), NVG_CW);
			nvgLineCap(args.vg, NVG_BUTT);
			nvgStrokeWidth(args.vg, width);
			nvgStrokeColor(args.vg, nvgRGB(0x4f, 0xc3, 0xf7));
			nvgStroke(args.vg);
		}
		const int steps = pq->snapEnabled ? (int) std::round(hi - lo) : 0;
		if (steps >= 1 && steps <= 24) {
			nvgBeginPath(args.vg);
			for (int i = 0; i <= steps; ++i) {
				const float a = angle((float) i / steps);
				nvgCircle(args.vg, c + radius * std::cos(a), c + radius * std::sin(a), width * 0.32f);
			}
			nvgFillColor(args.vg, nvgRGB(0xa3, 0xa9, 0xb4));
			nvgFill(args.vg);
		}
	}
"""


def fundamental(folder):
    res = os.path.join(folder, "res/components")
    p = DARK
    files = {
        "knob.svg": knob_bg(68, 68, p, FUNDAMENTAL_SWEEP),
        "knob-marker.svg": knob_pointer(68, 68, p),
        "knob-small.svg": knob_bg(18, 18, p, FUNDAMENTAL_SWEEP),
        "knob-marker-small.svg": knob_pointer(18, 18, p),
        "port.svg": jack(22, 22, p, p["highlight"]),
        "port-out.svg": jack(22, 22, p, p["accent"]),
        "button-off.svg": button(21, 21, p, False),
        "button-on.svg": button(21, 21, p, True, dot=False),
    }
    for name, art in files.items():
        with open(os.path.join(res, name), "w") as f:
            f.write(art)

    hpp = os.path.join(folder, "src/components.hpp")
    edit(hpp, """		scale = size / sw->box.size.x;
""", """		scale = size / sw->box.size.x;
		// PHINYX: 270 degrees of travel, as on PHINYX's own knobs
		minAngle = -0.75f * M_PI;
		maxAngle = 0.75f * M_PI;
""", "PHINYX: 270 degrees")
    edit(hpp, """		RoundKnob::draw(args);
		nvgRestore(args.vg);
	}
""", """		RoundKnob::draw(args);
		nvgRestore(args.vg);
		drawPhinyxValue(args);
	}
""" + KNOB_VALUE, "PHINYX: the value along the track")
    edit(hpp, """struct FundamentalPort : app::SvgPort {
	FundamentalPort() {
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/port.svg")));
	}
};""", """// PHINYX: inputs ring in the highlight colour, outputs in the accent colour (phinyx_skin.py).
// The port's type is set after it is made, so outputs change drawing on their first step.
struct FundamentalPort : app::SvgPort {
	bool outputSvg = false;

	FundamentalPort() {
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/port.svg")));
	}

	void step() override {
		if (type == engine::Port::OUTPUT && ! outputSvg) {
			setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/port-out.svg")));
			outputSvg = true;
		}
		app::SvgPort::step();
	}
};""", "PHINYX: inputs ring")
    edit(hpp, """			bgColor = nvgRGBA(190, 152, 152, 53);
			borderColor = nvgRGBA(241, 33, 33, 53);
			addBaseColor(nvgRGB(210, 11, 11));""", """			// PHINYX: lit in the highlight colour (phinyx_skin.py)
			bgColor = nvgRGBA(0x4f, 0xc3, 0xf7, 40);
			borderColor = nvgRGBA(0x4f, 0xc3, 0xf7, 60);
			addBaseColor(nvgRGB(0x4f, 0xc3, 0xf7));""", "PHINYX: lit in the highlight")


def cardinal_widgets(path):
    """Cardinal's own drawn knob (Host Audio's level) in the highlight colour instead of Cardinal red."""
    edit(path, """        const NVGcolor cardinalColor = nvgRGBf(0.76f, 0.11f, 0.22f);""",
         """        const NVGcolor cardinalColor = nvgRGB(0x4f, 0xc3, 0xf7); // PHINYX: the highlight colour (phinyx_skin.py)""",
         "PHINYX: the highlight colour")


ARTWORK_NOTE = """# PHINYX artwork in Cardinal

The files in `res/ComponentLibrary` (installed as `resources/ComponentLibrary`)
are not VCV Rack's component library artwork: PHINYX replaces every one of them
with its own drawings (cmake/instruments/cardinal-skin/phinyx_skin.py in the
PHINYX sources) before Cardinal is built. They are part of PHINYX and licensed
AGPL-3.0-only. The CC-BY-NC-4.0 entry for ComponentLibrary in LICENSES.md does
not apply to this build.
"""


# The Selector's eight colours (FindPanel::colour), in its order: silver,
# violet, indigo, azure, emerald, gold, saffron and ruby. New cables take them
# in turn.
CABLE_COLOURS = ["#c0c0c0", "#a040ff", "#6a3dd8", "#007fff", "#50c878", "#ffd700", "#ff9933", "#9b111e"]


def cable_colours(path):
    with open(path) as f:
        source = f.read()
    block = re.compile(r"(\n[ \t]*)(?://[^\n]*\n[ \t]*)?settings::cableColors = \{.*?\};", re.S)
    if not block.search(source):
        sys.exit(f"phinyx_skin.py: no cable colour list in {path}")
    def ours(m):
        pad = m.group(1)
        colours = "".join(f'{pad}    color::fromHexString("{c}"),' for c in CABLE_COLOURS)
        return f"{pad}// PHINYX: the Selector's colours (phinyx_skin.py){pad}settings::cableColors = {{{colours}{pad}}};"
    with open(path, "w") as f:
        f.write(block.sub(ours, source, count=1))


def main():
    count = component_library("src/Rack/res/ComponentLibrary")
    cable_colours("src/CardinalCommon.cpp")
    fundamental("plugins/Fundamental")
    cardinal_widgets("plugins/Cardinal/src/Widgets.hpp")
    with open("PHINYX-ARTWORK.md", "w") as f:
        f.write(ARTWORK_NOTE)
    print(f"phinyx_skin.py: drew {count} component library files")


if __name__ == "__main__":
    main()
