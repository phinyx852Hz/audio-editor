# The Φ mark

PHI's logo is its own capital phi (Unicode U+03A6, Φ).

**Origin.** It is an original drawing, made from golden-ratio geometry. It was not taken or traced from any font or existing artwork, so no attribution is owed to anyone. It was created on 24 October 2026 for PHI by its author (phinyx852Hz), who holds the copyright. The drawing was made with Claude (Anthropic) at the author's request.

**How it is drawn.** In units where the mark is 1000 tall, with φ = (1 + √5) / 2 ≈ 1.618:

| Part | Size | Why |
|---|---|---|
| Height | 1000 | |
| Ring width | 618 = 1000 / φ | the ring and the stem are in golden proportion |
| Ring height | 382 = 618 / φ | the ring itself is a golden ellipse; the stem is φ² times its height |
| Stem width | 90.2 = 1000 / φ⁵ | |
| Ring stroke | one stem at the sides, 1/φ of a stem at top and bottom | the thick and thin of a pen stroke |
| Serifs | φ² stems wide, 1/φ² of a stem tall | |

**Colours.** Gold `#c8a24a` on black `#0b0b0c`. A Φ plugin's window is black throughout, with the gold mark. The program draws the mark itself from these numbers (`Source/ui/PhiMark.cpp`), so it is sharp at every size.

**The compact mark.** For the Φ button used across the DAW. It is as tall as the ring is wide (618 = 382 × φ), so it is square and fits a button without making it bigger. Same ring and stem, no serifs, always white (`PhiButton` in `Source/ui/PhiMark.h`).

**Files.**
- `phi.svg`: the app icon, gold on a black rounded tile.
- `phi-mark-compact.svg`: the compact mark, white.
- `phi-mark-gold.svg`, `phi-mark-white.svg`, `phi-mark-black.svg`: the mark alone on a transparent background.
