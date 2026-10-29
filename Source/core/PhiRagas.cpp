// SPDX-License-Identifier: AGPL-3.0-only
// The raga sheets PHI comes with (format in PhiRaga.h). The list is the owner's (2026-10-25), with
// their teacher's additions (2026-10-26) and the hundred rare rāgs of Ghulām Ḥaidar's Sau Achhūp Rāg
// (Lahore, 1983), each checked against the scans of the teacher's copy. A book rāg whose name an
// earlier sheet or tuning already has carries "(ghulām ḥaidar)". The book gives no tunings, chalans or
// āndolan; its svaras get just intonation. The tuning of each earlier sheet is as agreed; the āroha,
// avaroha, vādī, samvādī, pakaḍ and āndolan are a first draft for the owner and their teacher to correct.
#include "PhiRaga.h"

namespace phi
{
    const std::string& builtInRagaSheets()
    {
        static const std::string sheets = R"sheets(
raga: bilāval
plain: bilaval
thaat: bilāval
svaras: S R G m P D N
aroha: S R G m P D N S'
avaroha: S' N D P m G R S
vadi: D
samvadi: G
pakad: G R G P | D N S'
time: morning

raga: yaman (kalyāṇ)
plain: yaman (kalyan)
thaat: kalyāṇ
svaras: S R G M P D N
aroha: N, R G M D N S'
avaroha: S' N D P M G R S
vadi: G
samvadi: N
pakad: N, R G R S | P M G R S
time: evening

raga: khamāj
plain: khamaj
thaat: khamāj
svaras: S R G m P D n N
aroha: S G m P D N S'
avaroha: S' n D P m G R S
vadi: G
samvadi: n
pakad: n D m P D | m G
murki: G, n
time: late evening

raga: kāfī
plain: kafi
thaat: kāfī
svaras: S R g m P D n
aroha: S R g m P D n S'
avaroha: S' n D P m g R S
vadi: P
samvadi: S
pakad: S S R R g g m m P
murki: g
time: night

raga: āsāvarī
plain: asavari
thaat: āsāvarī
svaras: S R g m P d n
aroha: S R m P d S'
avaroha: S' n d P m g R S
vadi: d
samvadi: g
time: late morning

raga: bhairavī
plain: bhairavi
thaat: bhairavī
svaras: S r g m P d n
aroha: S r g m P d n S'
avaroha: S' n d P m g r S
vadi: m
samvadi: S
murki: g
time: morning

raga: bhairav
plain: bhairav
thaat: bhairav
svaras: S r=256/243 G m P d=128/81 N
aroha: S r G m P d N S'
avaroha: S' N d P m G r S
vadi: d
samvadi: r
pakad: G m d d P | G m r r S
andolan: r 15 0.5, d 15 0.5
time: dawn

raga: mārvā
plain: marva
thaat: mārvā
svaras: S r G M D N
aroha: N, r G M D N r' S'
avaroha: r' N D M G r S
vadi: r
samvadi: D
time: sunset

raga: pūrvī
plain: purvi
thaat: pūrvī
svaras: S r G m M P d N
aroha: N, r G M P d N S'
avaroha: S' N d P M G m G r S
vadi: G
samvadi: N
time: sunset

raga: toḍī
plain: todi
thaat: toḍī
svaras: S r=256/243 g=32/27 M=729/512 P d=128/81 N
aroha: S r g M d N S'
avaroha: S' N d P M g r S
vadi: d
samvadi: g
time: morning

raga: darbārī kānaḍā
plain: darbari kanada
thaat: āsāvarī
svaras: S R g=32/27 m P d=128/81 n
aroha: n, S R g m P d n S'
avaroha: S' d n P m P g m R S
vadi: R
samvadi: P
pakad: g m R S | d n P
andolan: g 20 0.5, d 20 0.5
kan: m>g
time: midnight
rasa: grave

raga: jaunpurī
plain: jaunpuri
thaat: āsāvarī
svaras: S R g m P d n
aroha: S R m P d n S'
avaroha: S' n d P m g R S
vadi: d
samvadi: g
time: late morning

raga: bāgeśrī
plain: bageshri
thaat: kāfī
svaras: S R g m P D n
aroha: n, S g m D n S'
avaroha: S' n D m P D g m g R S
vadi: m
samvadi: S
time: night

raga: bhīmpalāsī
plain: bhimpalasi
thaat: kāfī
svaras: S R g m P D n
aroha: n, S g m P n S'
avaroha: S' n D P m g R S
vadi: m
samvadi: S
time: afternoon

raga: deś
plain: desh
thaat: khamāj
svaras: S R G m P D n N
aroha: S R m P N S'
avaroha: S' n D P m G R G N, S
vadi: R
samvadi: P
murki: R
time: night

raga: bihāg
plain: bihag
thaat: bilāval
svaras: S R G m M P D N
aroha: N, S G m P N S'
avaroha: S' N D P M P G m G R S
vadi: G
samvadi: N
khatka: G
time: night

raga: kedār
plain: kedar
thaat: kalyāṇ
svaras: S R G m M P D N
aroha: S m P D P N D S'
avaroha: S' N D P M P D P m G m R S
vadi: m
samvadi: S
time: night

raga: miyāṅ kī malhār
plain: miyan ki malhar
thaat: kāfī
svaras: S R g m P D n N
aroha: n, S R P m P n D N S'
avaroha: S' n P m P g m R S
vadi: m
samvadi: S
time: monsoon

raga: megh
plain: megh
thaat: kāfī
svaras: S R m P n
aroha: S R m P n S'
avaroha: S' n P m R S
vadi: S
samvadi: P
time: monsoon

raga: bhūpālī
plain: bhupali
thaat: kalyāṇ
svaras: S R G P D
aroha: S R G P D S'
avaroha: S' D P G R S
vadi: G
samvadi: D
time: evening

raga: durgā
plain: durga
thaat: bilāval
svaras: S R m P D
aroha: S R m P D S'
avaroha: S' D P m R S
vadi: m
samvadi: S
time: night

raga: haṃsadhvani
plain: hamsadhvani
thaat: bilāval
svaras: S R G P N
aroha: S R G P N S'
avaroha: S' N P G R S
vadi: S
samvadi: P
time: evening

raga: mālkauns
plain: malkauns
thaat: bhairavī
svaras: S g m d n
aroha: S g m d n S'
avaroha: S' n d m g S
vadi: m
samvadi: S
gamak: d 5
time: late night

raga: jog
plain: jog
thaat: khamāj
svaras: S g G m P n
aroha: S G m P n S'
avaroha: S' n P m G m g S
vadi: m
samvadi: S
time: night

raga: ahīr bhairav
plain: ahir bhairav
thaat: bhairav
svaras: S r G m P D n
aroha: S r G m P D n S'
avaroha: S' n D P m G r S
vadi: m
samvadi: S
time: morning

raga: lalit
plain: lalit
thaat: mārvā
svaras: S r G m M d N
aroha: N, r G m M m G M d S'
avaroha: r' N d M d m G r S
vadi: m
samvadi: S
time: dawn

raga: pūriyā dhanāśrī
plain: puriya dhanashri
thaat: pūrvī
svaras: S r G M P d N
aroha: N, r G M P d N S'
avaroha: r' N d P M G M r G r S
vadi: P
samvadi: r
time: sunset

raga: śrī
plain: shri
thaat: pūrvī
svaras: S r G M P d N
aroha: S r M P N S'
avaroha: S' N d P M G r S
vadi: r
samvadi: P
time: sunset

raga: kaunsī kānaḍā
plain: kaunsi kanada
thaat: bhairavī
svaras: S R g=32/27 m P d n
aroha: n, S g m d n S'
avaroha: S' n d n P m g m R S
vadi: m
samvadi: S
andolan: g 20 0.5
kan: m>g
time: late night

raga: nāyakī kānaḍā
plain: nayaki kanada
thaat: kāfī
svaras: S R g=32/27 m P n
aroha: S R m P n P S'
avaroha: S' n P m P g m R S
vadi: P
samvadi: S
andolan: g 20 0.5
kan: m>g
time: night

raga: kāfī kānaḍā
plain: kafi kanada
thaat: kāfī
svaras: S R g=32/27 m P D n
aroha: S R g m P D n S'
avaroha: S' n D P m g m R S
vadi: P
samvadi: S
andolan: g 20 0.5
kan: m>g
time: night

raga: śahānā kānaḍā
plain: shahana kanada
thaat: kāfī
svaras: S R g=32/27 m P D n
aroha: S R m P D n S'
avaroha: S' n P m P g m R S
vadi: P
samvadi: S
andolan: g 20 0.5
kan: m>g
time: night

raga: cārukeśī
plain: charukeshi
thaat: none (from Karnatak music)
svaras: S R G m P d n
aroha: S R G m P d n S'
avaroha: S' n d P m G R S
vadi: P
samvadi: S

raga: jogkauns
plain: jogkauns
thaat: khamāj
svaras: S g G m d n
aroha: S G m d n S'
avaroha: S' n d m G m g S
vadi: m
samvadi: S
time: late night

raga: sampūrṇ mālkauns
plain: sampurn malkauns
thaat: āsāvarī
svaras: S R g m P d n
aroha: S R g m P d n S'
avaroha: S' n d P m g R S
vadi: m
samvadi: S
time: late night

raga: jhiñjhoṭī
plain: jhinjhoti
thaat: khamāj
svaras: S R G m P D n
aroha: D, S R m P D S'
avaroha: S' n D P m G R S
vadi: G
samvadi: n
murki: G
time: night

raga: janasammohinī
plain: janasammohini
thaat: khamāj
svaras: S R G P D n
aroha: S R G P D n S'
avaroha: S' n D P G R S
vadi: G
samvadi: D
time: night

raga: bilāskhānī toḍī
plain: bilaskhani todi
thaat: bhairavī
svaras: S r=256/243 g=32/27 m P d=128/81 n
aroha: S r g P d S'
avaroha: S' n d m g r S
vadi: d
samvadi: g
time: morning

raga: miyāṅ kī toḍī
plain: miyan ki todi
thaat: toḍī
svaras: S r=256/243 g=32/27 M=729/512 P d=128/81 N
aroha: S r g M d N S'
avaroha: S' N d P M g r S
vadi: d
samvadi: g
time: morning

raga: naṭ bhairav
plain: nat bhairav
thaat: bhairav
svaras: S R G m P d N
aroha: S R G m P d N S'
avaroha: S' N d P m G R S
vadi: m
samvadi: S
time: morning

raga: śuddh sāraṅg
plain: shuddh sarang
thaat: kalyāṇ
svaras: S R m M P D N
aroha: N, S R M P N S'
avaroha: S' N D P M P m R S
vadi: R
samvadi: P
time: afternoon

raga: śyām kalyāṇ
plain: shyam kalyan
thaat: kalyāṇ
svaras: S R G m M P D N
aroha: N, S R M P N S'
avaroha: S' N D P M P G m R S
vadi: R
samvadi: P
time: evening

raga: śyām kauns
plain: shyam kauns
thaat: bhairavī
svaras: S g m P d n
aroha: S g m P d n S'
avaroha: S' n d P m g S
vadi: m
samvadi: S
time: late night

raga: bairāgī bhairav
plain: bairagi bhairav
thaat: bhairav
svaras: S r=256/243 m P n
aroha: S r m P n S'
avaroha: S' n P m r S
vadi: m
samvadi: S
time: morning

raga: vṛndāvanī sāraṅg
plain: vrindavani sarang
thaat: kāfī
svaras: S R m P n N
aroha: N, S R m P N S'
avaroha: S' n P m R S
vadi: R
samvadi: P
time: midday

raga: madhmād sāraṅg
plain: madhmad sarang
thaat: kāfī
svaras: S R m P n
aroha: n, S R m P n S'
avaroha: S' n P m R S
vadi: R
samvadi: P
time: midday

raga: miyāṅ kī sāraṅg
plain: miyan ki sarang
thaat: kāfī
svaras: S R m P D n N
aroha: N, S R m P N D N S'
avaroha: S' n P m R S
vadi: R
samvadi: P
time: midday

raga: sāmant sāraṅg
plain: samant sarang
thaat: kāfī
svaras: S R m P D n N
aroha: N, S R m P D N S'
avaroha: S' n D P m R S
vadi: R
samvadi: P
time: midday

raga: candrakauns
plain: chandrakauns
thaat: none
svaras: S g m d N
aroha: S g m d N S'
avaroha: S' N d m g S
vadi: m
samvadi: S
time: late night

# bahādur kauns: drafted from a recording the owner shared (2026-10-29), by pitch tracking, for
# the owner's teacher to correct. Candrakauns with R, which comes mostly in the descent (m g R S,
# g m R g) and is held long; G only passing as m is pulled down to g. Tuning as measured on the
# recording: g 300, d 799, N 1102 cents; held ma shaken about a semitone below at some 5 Hz.
raga: bahādur kauns
plain: bahadur kauns
thaat: none
svaras: S R g=300c m d=800c N=1100c
aroha: N, S g m d N S'
avaroha: S' N d m g R S
vadi: m
samvadi: S
pakad: m g R S | g m R g | d N S'
andolan: m 80 5
kan: m>g
time: late night

raga: harikauns
plain: harikauns
thaat: none
svaras: S g M d n
aroha: S g M d n S'
avaroha: S' n d M g S
vadi: M
samvadi: S
time: late night

raga: madhukauns
plain: madhukauns
thaat: none
svaras: S g M P n
aroha: S g M P n S'
avaroha: S' n P M g S
vadi: P
samvadi: S
time: late night

raga: sūhā kānaḍā
plain: suha kanada
thaat: kāfī
svaras: S R g=32/27 m P n
aroha: n, S m P n S'
avaroha: S' n P m P g m R S
vadi: m
samvadi: S
pakad: g m n P | m P S'
andolan: g 20 0.5
kan: m>g
time: midday

raga: sugharāī kānaḍā
plain: sughrai kanada
thaat: kāfī
svaras: S R g=32/27 m P n N
aroha: N, S R m P N S'
avaroha: S' n P m P g m R S
vadi: P
samvadi: S
pakad: n n P m R S R g
andolan: g 20 0.5
kan: m>g
time: late morning

raga: ābhogī kānaḍā
plain: abhogi kanada
thaat: kāfī
svaras: S R g m D
aroha: S R g m D S'
avaroha: S' D m g R S
vadi: S
samvadi: m
time: night

raga: bahār
plain: bahar
thaat: kāfī
svaras: S R g=32/27 m P D n N
aroha: S m P g m D N S'
avaroha: S' n P m P g m R S
vadi: m
samvadi: S
time: spring, night

raga: rāmdāsī malhār
plain: ramdasi malhar
thaat: kāfī
svaras: S R g G m P D n N
aroha: S R G m P D N S'
avaroha: S' n P m P g m R S
vadi: m
samvadi: S
time: monsoon

raga: gauḍ malhār
plain: gaud malhar
thaat: khamāj
svaras: S R G m P D n N
aroha: S R G m P D N S'
avaroha: S' D n P m G m R S
vadi: m
samvadi: S
time: monsoon

raga: jayant malhār
plain: jayant malhar
thaat: khamāj
svaras: S R g G m P D n N
aroha: S R G m P N S'
avaroha: S' n D P m g R S
vadi: R
samvadi: P
time: monsoon

raga: ānand bhairav
plain: anand bhairav
thaat: bhairav
svaras: S r G m P D N
aroha: S r G m P D N S'
avaroha: S' N D P m G r S
vadi: m
samvadi: S
time: morning

raga: guṇakalī
plain: gunkali
thaat: bhairav
svaras: S r m P d
aroha: S r m P d S'
avaroha: S' d P m r S
vadi: d
samvadi: r
time: morning

raga: jogiyā
plain: jogiya
thaat: bhairav
svaras: S r m P d N
aroha: S r m P d S'
avaroha: S' N d P d m r S
vadi: m
samvadi: S
time: dawn

raga: kāliṅgaḍā
plain: kalingda
thaat: bhairav
svaras: S r G m P d N
aroha: S r G m P d N S'
avaroha: S' N d P m G r S
vadi: P
samvadi: S
time: dawn

raga: gaurī
plain: gauri
thaat: bhairav
svaras: S r G m P d N
aroha: S r m P N S'
avaroha: S' N d P m G r S
vadi: r
samvadi: P
time: sunset

raga: basant mukhārī
plain: basant mukhari
thaat: bhairav
svaras: S r G m P d n
aroha: S r G m P d n S'
avaroha: S' n d P m G r S
vadi: m
samvadi: S
time: morning

raga: devsākh kānaḍā
plain: devsakh kanada
thaat: kāfī
svaras: S R g=32/27 m P n
aroha: S R m P n S'
avaroha: S' n P g m R S
vadi: P
samvadi: S
pakad: g P | n P m R S
andolan: g 20 0.5
kan: m>g
time: night

raga: bhavsākh
plain: bhavsakh
thaat: kāfī
svaras: S R g=32/27 m P d D n
aroha: S m P D n S'
avaroha: S' n d n P m P g m R S
vadi: P
samvadi: S
pakad: S m m P g g P m | n P D n R' S' | d n P
andolan: g 20 0.5
kan: m>g
time: night

raga: rāmsākh
plain: ramsakh
thaat: bilāval
svaras: S R g=32/27 G m P D n N
aroha: S R G m P D N S'
avaroha: S' n P m P g m R S
vadi: P
samvadi: S
kan: m>g
time: night

raga: lacchāsākh
plain: lachhasakh
thaat: bilāval
svaras: S R G m P D N
aroha: S G P D N S'
avaroha: S' N D P G m R S
vadi: P
samvadi: S
pakad: G P | m R S
time: morning

raga: guñjī kānaḍā
plain: gunji kanada
thaat: kāfī
svaras: S R g=32/27 G m P d n
aroha: S R G m P d n S'
avaroha: S' n d P m g m R S
vadi: P
samvadi: S
andolan: g 20 0.5
kan: m>g
time: night

raga: mudrikī kānaḍā
plain: mudriki kanada
thaat: kāfī
svaras: S R g=32/27 m P D n
aroha: S R m R P D n S'
avaroha: S' n P m P g m R S
vadi: P
samvadi: S
pakad: R m R P
andolan: g 20 0.5
kan: m>g
time: night

raga: husainī kānaḍā
plain: hussaini kanada
thaat: kāfī
svaras: S R g=32/27 m P D n
aroha: S n, R g m P D n P S'
avaroha: S' D n P m P g m R S
vadi: P
samvadi: S
pakad: S n, R S | g m R S | P m P | D n P
andolan: g 20 0.5
kan: m>g
time: night

raga: śivmat bhairav
plain: shivmat bhairav
thaat: bhairav
svaras: S r=256/243 g G m P d=128/81 n N
aroha: S r G m P G m P d N S'
avaroha: S' N d P d n d P m G m G r S r g r S
vadi: m
samvadi: S
pakad: N S N d P | P d n d P | G m G r S r g r S
time: morning

raga: prabhāt bhairav
plain: prabhat bhairav
thaat: bhairav
svaras: S r G m M P d N
aroha: S r G m G m d P d N S'
avaroha: S' N d P m G r G m M G r S
vadi: m
samvadi: S
time: dawn

raga: maṅgal bhairav
plain: mangal bhairav
thaat: bhairav
svaras: S r G m P D N
aroha: S r G m D N S'
avaroha: S' N D P m G r S
vadi: m
samvadi: S
pakad: S D, P, D, S | P, D, r r S
time: morning

# The hundred of Ghulām Ḥaidar's Sau Achhūp Rāg (Lahore, 1983), in the book's order, from the copy
# of the owner's teacher, Anwar Khurshid. Each comment gives the book's number and page.

# sau achhūp rāg #1, p. 17
raga: dīpak
plain: dipak
thaat: kalyāṇ
svaras: S R G M P D
aroha: S R G M P D S'
avaroha: S' D P M G R S
vadi: P
samvadi: R
pakad: G M P R G M P D S'
time: night

# sau achhūp rāg #2, p. 17
raga: rāj kalyāṇ
plain: raj kalyan
thaat: kalyāṇ
svaras: S R G M D N
aroha: S R G M D N S'
avaroha: S' N D M G R S
vadi: G
samvadi: N
pakad: N, R G M D M G R S
time: night

# sau achhūp rāg #3, p. 18: the book gives its Madrasi name as janasammohinī
raga: madh kalyāṇ
plain: madh kalyan
thaat: kalyāṇ
svaras: S R G P D n
aroha: S G P D n S'
avaroha: S' n D P G R S
vadi: P
samvadi: R
pakad: n D P G R | G R S G P R R
time: night

# sau achhūp rāg #4, p. 19
raga: haṃs kalyāṇ
plain: hans kalyan
thaat: kalyāṇ
svaras: S R G M P D N
aroha: S R G P N S'
avaroha: S' N D P M G R S
vadi: P
samvadi: R
pakad: G P N S' N D P G R
time: night

# sau achhūp rāg #5, p. 19
raga: sāvant kalyāṇ
plain: savant kalyan
thaat: kalyāṇ
svaras: S R G M P D N
aroha: S R G P N D M P D S'
avaroha: S' N D P M D P G R S
vadi: P
samvadi: S
pakad: D M P D S' D P G R S
time: night

# sau achhūp rāg #6, p. 20: avaroha as printed (M G. S R S); only tīvra Ma is given
raga: mārū bihāg
plain: maru bihag
thaat: kalyāṇ
svaras: S R G M P D N
aroha: S G M P N S'
avaroha: S' N D P M G S R S
vadi: P
samvadi: S
pakad: G M P G S R S
time: night

# sau achhūp rāg #7, p. 20: Ustād Fateḥ ʿAlī Khān Qawwāl's
raga: śrī yaman
plain: shri yaman
thaat: kalyāṇ
svaras: S R G M P D N
aroha: S R M P N S'
avaroha: S' N D P M G R S
vadi: R
samvadi: P
pakad: R M P D M G R R
time: night

# sau achhūp rāg #8, p. 21
raga: vijayantī
plain: vijayanti
thaat: kalyāṇ
svaras: S R M P N
aroha: S R M P N S'
avaroha: S' N P M R S
vadi: P
samvadi: S
pakad: S' N P M P M R N, R S
time: second watch of the night

# sau achhūp rāg #9, p. 22
raga: mālārānī
plain: malarani
thaat: kalyāṇ
svaras: S R G M P D N
aroha: S R M P N S'
avaroha: S' D P G R S
vadi: P
samvadi: S
pakad: P N S' D P G R
time: night

# sau achhūp rāg #10, p. 22
raga: joginī kalyāṇ
plain: jogini kalyan
thaat: kalyāṇ
svaras: S G M P N
aroha: S G M P N S'
avaroha: S' N P M G S
vadi: G
samvadi: N
pakad: P, N, S G M G G S
time: night

# sau achhūp rāg #11, p. 23: Ustād Ṣādiq ʿAlī Khān Māṇḍū's
raga: hiṇḍolī kalyāṇ
plain: hindoli kalyan
thaat: kalyāṇ
svaras: S R G M P D N
aroha: S G M D N S'
avaroha: S' N P M G R S
vadi: P
samvadi: R
pakad: M D S' R' N P M G M G S R S
time: night

# sau achhūp rāg #12, p. 23: Ustād Ṣādiq ʿAlī Khān Māṇḍū's; the second word of the name is unclear
raga: kauśik sarāolī
plain: kaushik saraoli
thaat: kalyāṇ
svaras: S G M P D
aroha: S G M P D S'
avaroha: S' D P M G S
vadi: P
samvadi: S
pakad: S G M P P D P M G S
time: night

# sau achhūp rāg #13, p. 25
raga: mārgī toḍī
plain: margi todi
thaat: bhairavī
svaras: S r g m d n
aroha: S r g m d n S'
avaroha: S' n d m g r S
vadi: m
samvadi: S
pakad: g m n d m g r r S
time: morning

# sau achhūp rāg #14, p. 25: both Ni: shuddh going up, komal coming down
raga: rasūlī toḍī
plain: rasuli todi
thaat: bhairavī
svaras: S r g P d n N
aroha: S r g P d N S'
avaroha: S' n d P g r S
vadi: P
samvadi: S
pakad: g P d n d N S'
time: morning

# sau achhūp rāg #15, p. 26
raga: gopī basant
plain: gopi basant
thaat: bhairavī
svaras: S g m P d n
aroha: S g m P d n S'
avaroha: S' n d P m g S
vadi: P
samvadi: S
pakad: g m d n d P m g S
time: morning

# sau achhūp rāg #16, p. 26: Mālkauns with shuddh ga, which it takes mostly going up; the printed āroha has g, the chalan S G m; no time given
raga: madh kauns
plain: madh kauns
thaat: bhairavī
svaras: S g G m d n
aroha: S G m d n S'
avaroha: S' n d m G m g S
vadi: m
samvadi: S
pakad: G m d m G g S

# sau achhūp rāg #17, p. 27
raga: ahīrī toḍī
plain: ahiri todi
thaat: bhairavī
svaras: S r g m P D n
aroha: S r g m D n S'
avaroha: S' n D P m g r S
vadi: m
samvadi: S
pakad: g m D n D P m g r S
time: morning

# sau achhūp rāg #18, p. 27
raga: komal rāmkalī
plain: komal ramkali
thaat: bhairavī
svaras: S r g M P d n
aroha: S r g P M P d S'
avaroha: S' n d P M P d P g r S
vadi: P
samvadi: S
pakad: M P d n d P g P g r S
time: morning

# sau achhūp rāg #19, p. 28
raga: rañjan
plain: ranjan
thaat: bhairavī
svaras: S m P d n
aroha: S m P d n S'
avaroha: S' n d P m S
vadi: P
samvadi: S
pakad: S m m P d n d P m S
time: morning

# sau achhūp rāg #20, p. 28: the octaves of the last pakaḍ group are unclear
raga: bairāgī
plain: bairagi
thaat: bhairavī
svaras: S r m P n
aroha: S r m P n S'
avaroha: S' n P m r S
vadi: P
samvadi: r
pakad: r m P n S' r' S' | n, P, n, S
time: morning

# sau achhūp rāg #21, p. 29: the name's reading is unsure; the book says it is also called kaunsiyā toḍī
raga: mehdī
plain: mehdi
thaat: bhairavī
svaras: S r m d n
aroha: S r m d n S'
avaroha: S' n d m r S
vadi: m
samvadi: S
pakad: r m d m d m r S
time: morning, or the last watch of the night

# sau achhūp rāg #22, p. 29: the name's reading is unsure
raga: sanharū
plain: sanharu
thaat: bhairavī
svaras: S r m P d n
aroha: S r m P d S'
avaroha: S' n d P m r S
vadi: m
samvadi: S
pakad: r m P d P | n d P m r S
time: third watch of the day

# sau achhūp rāg #23, p. 30
raga: śuddh sāvant
plain: shuddh savant
thaat: bhairavī
svaras: S r g m P d
aroha: S r m P d S'
avaroha: S' d P m g r S
vadi: m
samvadi: S
pakad: d, S r m P d P m g r S
time: third watch of the day

# sau achhūp rāg #24, p. 30: the name could be mālū or mālav; no pakaḍ given
raga: mālū
plain: malu
thaat: bhairavī
svaras: S g m P d n
aroha: S g m d n S'
avaroha: S' n d P m g S
vadi: m
samvadi: S
time: third watch of the night

# sau achhūp rāg #25, p. 31: Ustād Ghulām Ḥaidar Khān's
raga: dīp kauns
plain: dip kauns
thaat: bhairavī
svaras: S R G m d n
aroha: S G m d n S'
avaroha: S' n d m G m R S
vadi: m
samvadi: S
pakad: G m R S G m d n S'
time: night

# sau achhūp rāg #26, p. 31: Ustād Salīm Ḥusain's; the name's reading is unsure
raga: saṅgham
plain: sangham
thaat: bhairavī
svaras: S r m P d n
aroha: S m P d n S'
avaroha: S' n d P r S
vadi: P
samvadi: S
pakad: S m P P d n d P r S
time: first watch of the day

# sau achhūp rāg #27, p. 33: Ustād Salāmat ʿAlī Khān's; known in books as sūraj kalī
raga: kanval bhairav
plain: kanval bhairav
thaat: bhairav
svaras: S r G P n
aroha: S r G P n S'
avaroha: S' n P G r S
vadi: r
samvadi: P
pakad: r G P n P G r S
time: morning

# sau achhūp rāg #28, p. 33: no time on the scan
raga: dhenukā
plain: dhenuka
thaat: bhairav
svaras: S r m d N
aroha: S r m d N S'
avaroha: S' N d m r S
vadi: m
samvadi: S
pakad: m r m d N d m

# sau achhūp rāg #29, p. 34: the pakaḍ prints ga, not in the rāg; read as ma
raga: devrañjanī
plain: devranjani
thaat: bhairav
svaras: S m P d N
aroha: S m P d N d S'
avaroha: S' N d P m S
vadi: S
samvadi: m
pakad: N d P m P m m S
time: morning

# sau achhūp rāg #30, p. 34
raga: madh rañjanī
plain: madh ranjani
thaat: bhairav
svaras: S r G m N
aroha: S r G m N S'
avaroha: S' N m G r S
vadi: m
samvadi: S
pakad: N, r G m m | N m G r S
time: third watch of the night

# sau achhūp rāg #31, p. 35: ʿĀdil Shāh Sūrī's; the second name is unclear
raga: sūrya kauns
plain: surya kauns
thaat: bhairav
svaras: S G m d N
aroha: S G m d N S'
avaroha: S' N d m G S
vadi: m
samvadi: S
pakad: S G m m | d, N, G S
time: morning

# sau achhūp rāg #32, p. 36
raga: kaunsī bhairav
plain: kaunsi bhairav
thaat: bhairav
svaras: S r G m P d N
aroha: S m G m P m N d N S'
avaroha: S' N d P m G m r S
vadi: m
samvadi: S
pakad: S m m | P | G m d N S'
time: morning

# sau achhūp rāg #33, p. 36
raga: naṭ bhairav (ghulām ḥaidar)
plain: nat bhairav (ghulam haidar)
thaat: bhairav
svaras: S R G m P d N
aroha: S R G m d N S'
avaroha: S' N d P m G R S
vadi: d
samvadi: R
pakad: G m d d P m P G m R S
time: morning

# sau achhūp rāg #34, p. 37: the name's reading is unsure
raga: holeśrī
plain: holeshri
thaat: bhairav
svaras: S r G m P d N
aroha: S r G m P d S'
avaroha: S' N P m P G m r S
vadi: P
samvadi: S
pakad: m P d S' N P m P
time: morning

# sau achhūp rāg #35, p. 37
raga: komal manoharī
plain: komal manohari
thaat: bhairav
svaras: S G m P d N
aroha: S G m P d N S'
avaroha: S' N d P m G S
vadi: G
samvadi: N
pakad: G m P d N d P m G S
time: night

# sau achhūp rāg #36, p. 38: Ustād Fateḥ ʿAlī Khān Qawwāl's
raga: ahīr kalī
plain: ahir kali
thaat: bhairav
svaras: S r m D n
aroha: S r m D n S'
avaroha: S' n D m r S
vadi: m
samvadi: S
pakad: m D n D m r S
time: morning

# sau achhūp rāg #37, p. 40
raga: līlāvatī
plain: lilavati
thaat: āsāvarī
svaras: S R g P d
aroha: S R g P d S'
avaroha: S' d P g R S
vadi: P
samvadi: S
pakad: S R S | d, d, P, g R S
time: morning

# sau achhūp rāg #38, p. 40
raga: kokil pañcam
plain: kokil pancham
thaat: āsāvarī
svaras: S g m P d
aroha: S g m P d S'
avaroha: S' d P m g S
vadi: P
samvadi: S
pakad: S g S d, P, m g m S g S
time: morning

# sau achhūp rāg #39, p. 41: the first word's reading is unsure
raga: śanū bhāvrī
plain: shanu bhavri
thaat: āsāvarī
svaras: S R m P d
aroha: S R m P d S'
avaroha: S' d P m R S
vadi: P
samvadi: S
pakad: R m d P m R S
time: morning

# sau achhūp rāg #40, p. 41: Ḥaẓrat Amīr Khusrau's; named apart from the dastgāh navā
raga: navā (ghulām ḥaidar)
plain: nava (ghulam haidar)
thaat: āsāvarī
svaras: S R m P d n
aroha: S R m P d n S'
avaroha: S' n d P m R S
vadi: P
samvadi: S
pakad: m P d n d P m R S
time: morning

# sau achhūp rāg #41, p. 42
raga: candra āsāvarī
plain: chandra asavari
thaat: āsāvarī
svaras: S R m P d n
aroha: S R m P d n S'
avaroha: S' n d P m R S
vadi: S
samvadi: m
pakad: n, S R m P d P m R S
time: second watch of the night

# sau achhūp rāg #42, p. 42: the pakaḍ is cut at the page edge
raga: tikhārī
plain: tikhari
thaat: āsāvarī
svaras: S R g m d
aroha: S R g m d S'
avaroha: S' d m g R S
vadi: d
samvadi: R
pakad: S R g m d d | m d d m g R
time: day

# sau achhūp rāg #43, p. 43: the name's reading is unsure
raga: jogvānī
plain: jogvani
thaat: āsāvarī
svaras: S R g m P d n
aroha: S g R m P d S'
avaroha: S' n d P d m P g R S
vadi: P
samvadi: S
pakad: g R m P d m P d S'
time: second watch of the night

# sau achhūp rāg #44, p. 43
raga: ānand bhairavī
plain: anand bhairavi
thaat: āsāvarī
svaras: S R g m P d n
aroha: S R g m P d S'
avaroha: S' n d P m g R S
vadi: P
samvadi: S
pakad: g m P d P | g R S n, S
time: third watch of the night

# sau achhūp rāg #45, p. 44
raga: ābhīrī
plain: abhiri
thaat: āsāvarī
svaras: S R g m P d n
aroha: S g m P n S'
avaroha: S' n d P m g R S
vadi: P
samvadi: S
pakad: g m P n d P m g R S
time: morning

# sau achhūp rāg #46, p. 44
raga: kauśik
plain: kaushik
thaat: āsāvarī
svaras: S R g m M P d n
aroha: S g m P M d n S'
avaroha: S' n d P m g R S
vadi: P
samvadi: S
pakad: S g m P M d n d P m g R S
time: morning

# sau achhūp rāg #47, p. 47
raga: kalāvatī
plain: kalavati
thaat: khamāj
svaras: S G P D n
aroha: S G P D n S'
avaroha: S' n D P G S
vadi: P
samvadi: S
pakad: n D P G P G S
time: night

# sau achhūp rāg #48, p. 47: the book says it is sung without Pa nowadays
raga: gorakh kalyāṇ
plain: gorakh kalyan
thaat: khamāj
svaras: S R m P D n
aroha: S R m D n S'
avaroha: S' n D P m R S
vadi: m
samvadi: S
pakad: m D n D S' n D P m R S
time: night

# sau achhūp rāg #49, p. 48: the name's reading is unsure
raga: pacampak
plain: pachampak
thaat: khamāj
svaras: S R G m P D n
aroha: S R m P D S'
avaroha: S' n D P m G R S
vadi: m
samvadi: S
pakad: m P D S' n D P m G R S
time: night

# sau achhūp rāg #50, p. 48
raga: campākalī
plain: champakali
thaat: khamāj
svaras: S R G m M P D n
aroha: S R M P n S'
avaroha: S' n D P M G m R S
vadi: P
samvadi: S
pakad: n D P M G m G S R R S
time: midnight

# sau achhūp rāg #51, p. 49
raga: jog (ghulām ḥaidar)
plain: jog (ghulam haidar)
thaat: khamāj
svaras: S g G m P n
aroha: S G m P n S'
avaroha: S' n P m G S g S
vadi: P
samvadi: S
pakad: n P m G m S g S
time: third watch of the day

# sau achhūp rāg #52, p. 49: Ustād Ghulām Ḥaidar Khān's
raga: lājorī
plain: lajori
thaat: khamāj
svaras: S G m D n
aroha: S G m D n S'
avaroha: S' n D m G S
vadi: m
samvadi: S
pakad: G m m G S | n, D, n, S
time: night

# sau achhūp rāg #53, p. 50: the samvādī is misprinted (the label is repeated); read as Sa
raga: sājan
plain: sajan
thaat: khamāj
svaras: S R G m P D n
aroha: n, S G m D n S'
avaroha: S' D n P m G m R S
vadi: m
samvadi: S
pakad: S' D n P m G m R S
time: night

# sau achhūp rāg #54, p. 50: Ustād Fateḥ ʿAlī Khān Qawwāl's
raga: gāvatī
plain: gavati
thaat: khamāj
svaras: S R G m P D n
aroha: S G m P n S'
avaroha: S' D m P G m R S
vadi: P
samvadi: S
pakad: m D m P G m R n, S
time: night

# sau achhūp rāg #55, p. 51
raga: śyām kedār
plain: shyam kedar
thaat: khamāj
svaras: S R m M P D n N
aroha: S R S m R M P D N S'
avaroha: S' N D P D n D P m R S
vadi: m
samvadi: S
pakad: m R M P D n D P m R S
time: night

# sau achhūp rāg #56, p. 51
raga: sarasvatī
plain: sarasvati
thaat: khamāj
svaras: S R M P D n
aroha: S R M P n D S'
avaroha: S' n D P M R S
vadi: P
samvadi: R
pakad: R M P n D P M R S
time: night

# sau achhūp rāg #57, p. 53: shuddh Ni as printed (the header names Re, Dha and Ni tīvra)
raga: jogeśrī
plain: jogeshri
thaat: kāfī
svaras: S R g m D N
aroha: S g m D N S'
avaroha: S' N D m g R S
vadi: m
samvadi: N
pakad: m g R S N, S R D, N, D,
time: second watch of the night

# sau achhūp rāg #58, p. 53
raga: revatī kānaḍā
plain: revati kanada
thaat: kāfī
svaras: S R g m P D n
aroha: S R m P D m P S'
avaroha: S' D n P m P g m R S
vadi: P
samvadi: S
pakad: D m P S' D n P g m R S
time: second watch of the day

# sau achhūp rāg #59, p. 54: the book also calls it śivrañjanī
raga: mālatī
plain: malati
thaat: kāfī
svaras: S R g P D
aroha: S R g P D S'
avaroha: S' D P g R S
vadi: P
samvadi: S
pakad: S R g R g P D P
time: midnight

# sau achhūp rāg #60, p. 54
raga: haṃs mañjarī
plain: hans manjari
thaat: kāfī
svaras: S R m P D n
aroha: S R m P D n S'
avaroha: S' D P m P D P m R S
vadi: P
samvadi: S
pakad: m P D n S' D P m R S
time: evening

# sau achhūp rāg #61, p. 55
raga: ābhogī kānaḍā (ghulām ḥaidar)
plain: abhogi kanada (ghulam haidar)
thaat: kāfī
svaras: S R g m D
aroha: S R g m D S'
avaroha: S' D m g R S
vadi: m
samvadi: S
pakad: m D m S' | D m g m R S
time: second and third watches of the night

# sau achhūp rāg #62, p. 55: also called śubh kauns
raga: candrakauns (ghulām ḥaidar)
plain: chandrakauns (ghulam haidar)
thaat: kāfī
svaras: S g m D n
aroha: S g m D n S'
avaroha: S' n D m g S
vadi: m
samvadi: S
pakad: S g m m D g m m
time: midnight

# sau achhūp rāg #63, p. 56
raga: deśākhī
plain: deshakhi
thaat: kāfī
svaras: S R g m P n
aroha: S m R P m n P S'
avaroha: S' n P m P g m R S
vadi: P
samvadi: S
pakad: R P m n P S' | n P g m R S
time: second watch of the night

# sau achhūp rāg #64, p. 56: shuddh Ni: the header names Dha and Ni tīvra
raga: rājeśvarī
plain: rajeshvari
thaat: kāfī
svaras: S g m D N
aroha: S g m D N S'
avaroha: S' N D m g S
vadi: m
samvadi: N
pakad: m D N m N D m m
time: second watch of the night

# sau achhūp rāg #65, p. 57
raga: sāvan
plain: savan
thaat: kāfī
svaras: S R g m P D n
aroha: S g P g m P n S'
avaroha: S' n D P m g R S
vadi: S
samvadi: P
pakad: g m P g R n D P m P g R S R g S
time: any time in the rains

# sau achhūp rāg #66, p. 57: the header names both Ma, Ni tīvra and Ga komal; Dha is not stated
raga: jai kauns
plain: jai kauns
thaat: kāfī
svaras: S g m M d N
aroha: S g m M m d N S'
avaroha: S' N d m M m g S
vadi: m
samvadi: S
pakad: S g m M m | N d | m m g S
time: second watch of the night

# sau achhūp rāg #67, p. 58
raga: daulatī kānaḍā
plain: daulati kanada
thaat: kāfī
svaras: S R g m P n N
aroha: S R P g n P N S'
avaroha: S' n P g m R S
vadi: P
samvadi: R
pakad: R P g n P N S'
time: midnight

# sau achhūp rāg #68, p. 58: the name's reading is unsure
raga: jaint
plain: jaint
thaat: kāfī
svaras: S g m P D n
aroha: S g m P D S'
avaroha: S' n D P m g S
vadi: P
samvadi: S
pakad: m g m P P D n D P D S'
time: midnight

# sau achhūp rāg #69, p. 59: Ustād Nuṣrat Fateḥ ʿAlī Khān's
raga: haṃs rañjanī
plain: hans ranjani
thaat: kāfī
svaras: S R g P N
aroha: S R g P N S'
avaroha: S' N P g R S
vadi: P
samvadi: R
pakad: R g P N S' | g' R' S'
time: night

# sau achhūp rāg #70, p. 59
raga: sarasvatī rañjanī
plain: sarasvati ranjani
thaat: kāfī
svaras: S R g M P D n N
aroha: S g M P N S'
avaroha: S' n D P M g R S
vadi: P
samvadi: R
pakad: g M P N S' | n D P P
time: second watch of the night

# sau achhūp rāg #71, p. 60: the svaras are not stated; read in kāfī
raga: rāg rañjanī
plain: rag ranjani
thaat: kāfī
svaras: S R g m P D n
aroha: S g m D n D S'
avaroha: S' n D P m g m R S
vadi: m
samvadi: S
pakad: g m D n D S' | R' n D P m g m R S
time: night

# sau achhūp rāg #72, p. 60
raga: madhur rañjanī
plain: madhur ranjani
thaat: kāfī
svaras: S g m P N
aroha: S g m P N S'
avaroha: S' N P m P g m S
vadi: P
samvadi: S
pakad: N S' N P | g m P g m S
time: midnight

# sau achhūp rāg #73, p. 61
raga: maheśvarī
plain: maheshvari
thaat: kāfī
svaras: S R g m P D
aroha: S R g m P m D S'
avaroha: S' D m P D g m R S
vadi: m
samvadi: S
pakad: R g m P D m D S'
time: night

# sau achhūp rāg #74, p. 61
raga: kalhār sāraṅg
plain: kalhar sarang
thaat: kāfī
svaras: S R M P n
aroha: S R M P n S'
avaroha: S' n P M R S
vadi: P
samvadi: S
pakad: R M P n P M R S
time: evening

# sau achhūp rāg #75, p. 62: Ma tīvra as the header prints it; a handwritten mark there may correct it
raga: vijay
plain: vijay
thaat: kāfī
svaras: S R g M P D
aroha: S R g M P D S'
avaroha: S' D P M g R S
vadi: S
samvadi: P
pakad: g M P D S' D P M g R
time: evening

# sau achhūp rāg #76, p. 62: Ustād Ghulām Ḥaidar Khān's
raga: dīpā
plain: dipa
thaat: kāfī
svaras: S g m P D N
aroha: S g m P N S'
avaroha: S' D P m g S
vadi: P
samvadi: S
pakad: g m P D P m g S | m P N S' D P m P
time: evening

# sau achhūp rāg #77, p. 63
raga: rakt haṃs
plain: rakt hans
thaat: kāfī
svaras: S R m P D n
aroha: S R m P D S'
avaroha: S' n P m R S
vadi: R
samvadi: P
pakad: m P D S' n P m R
time: second watch of the day

# sau achhūp rāg #78, p. 63: the name's reading is unsure
raga: kohal
plain: kohal
thaat: kāfī
svaras: S R g P n N
aroha: S R g P N S'
avaroha: S' n P g R S
vadi: R
samvadi: P
pakad: P, N, S R g R | R P g R S
time: second watch of the night

# sau achhūp rāg #79, p. 64: Nawāb Wājid ʿAlī Shāh's; the book also calls it madhukauns
raga: sulṭānī
plain: sultani
thaat: kāfī
svaras: S g M P n
aroha: S g M P n S'
avaroha: S' n P M g S
vadi: P
samvadi: S
pakad: g M P n P g S | S g n, S
time: second watch of the night

# sau achhūp rāg #80, p. 64: the name's reading is unsure
raga: kalhās
plain: kalhas
thaat: kāfī
svaras: S R g P D n
aroha: S R g P D n S'
avaroha: S' n D P g R S
vadi: P
samvadi: S
pakad: g S R g P n D P g R S
time: third watch of the day

# sau achhūp rāg #81, p. 66
raga: kamal rañjanī
plain: kamal ranjani
thaat: bilāval
svaras: S G m P D N
aroha: S G P D N S'
avaroha: S' N D P m G S
vadi: D
samvadi: G
pakad: G P D N D P m G S
time: second watch of the day

# sau achhūp rāg #82, p. 66: the vādī is printed "Kharaj Pañcham"
raga: candrikā
plain: chandrika
thaat: bilāval
svaras: S R m P D N
aroha: S R m P N S'
avaroha: S' D P m R S
vadi: P
samvadi: S
pakad: m P N S' D P m P D P m R S
time: midnight

# sau achhūp rāg #83, p. 67
raga: cakradhar
plain: chakradhar
thaat: bilāval
svaras: S R G m D N
aroha: S R G m D N S'
avaroha: S' N D m G R S
vadi: m
samvadi: S
pakad: N D m G R S R G m m
time: midnight

# sau achhūp rāg #84, p. 67
raga: ras rañjanī
plain: ras ranjani
thaat: bilāval
svaras: S R m D N
aroha: S R m D N S'
avaroha: S' N D m R S
vadi: m
samvadi: S
pakad: m D N S' N D m m R S
time: midnight

# sau achhūp rāg #85, p. 68: called bhinna ṣaḍj in the old saṅgīts
raga: kauśik dhunī
plain: kaushik dhuni
thaat: bilāval
svaras: S G m D N
aroha: S G m D N S'
avaroha: S' N D m G S
vadi: S
samvadi: m
pakad: S' N D m G m m G S
time: midnight

# sau achhūp rāg #86, p. 69: the name's reading is unsure
raga: cint mohinī
plain: chint mohini
thaat: bilāval
svaras: S R G m P D N
aroha: S R G m P D N S'
avaroha: S' N P m R S
vadi: R
samvadi: P
pakad: R G m P m R | m P D N S' N P m R
time: second watch of the day

# sau achhūp rāg #87, p. 69
raga: jan rañjanī
plain: jan ranjani
thaat: bilāval
svaras: S R G m P D N
aroha: S R G m P N S'
avaroha: S' D P m R G R S
vadi: S
samvadi: m
pakad: m P N S' D P m | S R G m P m R S
time: second watch of the night

# sau achhūp rāg #88, p. 70: headed "raʿsa (raʾīs kānaḍā)"; Sulṭān Ḥusain Sharqī of Jaunpur's
raga: raīs kānaḍā
plain: rais kanada
thaat: bilāval
svaras: S R g G m P D
aroha: S R G m D S'
avaroha: S' D P m g m R S
vadi: m
samvadi: S
pakad: S G m m | D P m P m | P m g m R S D, S
time: night

# sau achhūp rāg #89, p. 70
raga: ārabhī
plain: arabhi
thaat: bilāval
svaras: S R G m P D N
aroha: S R m P D S'
avaroha: S' N D P m G R S
vadi: R
samvadi: D
pakad: R m P D N P D m P m G R S
time: second watch of the night

# sau achhūp rāg #90, p. 71
raga: kusum bilāval
plain: kusum bilaval
thaat: bilāval
svaras: S R G m P D
aroha: S R G m P D S'
avaroha: S' D P m G R S
vadi: G
samvadi: D
pakad: G R G m G P D S' D P m G R G m G
time: second watch of the morning

# sau achhūp rāg #91, p. 72
raga: mārg hindol
plain: marg hindol
thaat: mārvā
svaras: S r G M D N
aroha: S r G M D N D S'
avaroha: S' N D M G r S
vadi: D
samvadi: r
pakad: G M D N | D S' N D M G r
time: end of the night

# sau achhūp rāg #92, p. 72: the name's reading is unsure
raga: raldīp
plain: raldip
thaat: mārvā
svaras: S r G M P D N
aroha: S G M P N S'
avaroha: S' N D P M P M G r S
vadi: P
samvadi: S
pakad: S G M P M D P M G r S
time: third watch of the night

# sau achhūp rāg #93, p. 73
raga: mālin
plain: malin
thaat: mārvā
svaras: S r G P D N
aroha: S G P N D S'
avaroha: S' N D P G r S
vadi: P
samvadi: S
pakad: G P N D S' N D P G P r G r S
time: evening

# sau achhūp rāg #94, p. 73: the name's reading is unsure
raga: dhanya dhaivat
plain: dhanya dhaivat
thaat: mārvā
svaras: S r G P D
aroha: S r G P D S'
avaroha: S' D P G r S
vadi: D
samvadi: r
pakad: r G P D D P | D P G r S
time: evening

# sau achhūp rāg #95, p. 74: Malika-e-Mausīqī Roshan Ārā Begum's
raga: zartābī
plain: zartabi
thaat: mārvā
svaras: S r G M P D n N
aroha: S r G M P D N S'
avaroha: S' n D P M G r S
vadi: P
samvadi: S
pakad: S r G M P D N D P M G r r S N, S
time: evening and night

# sau achhūp rāg #96, p. 74: tīvra Ni only as a turn into Sa
raga: mārū
plain: maru
thaat: mārvā
svaras: S r G M D n N
aroha: S r G M D n S'
avaroha: S' n D M G r S
vadi: D
samvadi: r
pakad: r G M D n D S' N r' r' S'
time: evening

# sau achhūp rāg #97, p. 76: Ustād Salīm Ḥusain's; shuddh Dha as printed
raga: mārvī toḍī
plain: marvi todi
thaat: toḍī
svaras: S r g M D N
aroha: S r g M D N S'
avaroha: S' N D M g r S
vadi: D
samvadi: r
pakad: r g M D D N D M g r r
time: first watch of the morning

# sau achhūp rāg #98, p. 76: Ustād Ghulām Ḥaidar Khān's
raga: suhānī toḍī
plain: suhani todi
thaat: toḍī
svaras: S g M P d
aroha: S g M P d S'
avaroha: S' d P M g S
vadi: g
samvadi: d
pakad: S g M P d d M g S
time: morning

# sau achhūp rāg #99, p. 77
raga: chāyā toḍī
plain: chaya todi
thaat: toḍī
svaras: S r g M d
aroha: S r g M d S'
avaroha: S' d M g r S
vadi: g
samvadi: d
pakad: r g M d d M g r g r S
time: morning

# sau achhūp rāg #100, p. 77
raga: sālag varālī toḍī
plain: salag varali todi
thaat: toḍī
svaras: S r g P D n
aroha: S r g P D n S'
avaroha: S' n D P g r S
vadi: P
samvadi: r
pakad: r g P D n D P g r S
time: morning

# rāg darpaṇ: made for PHI by the owner and Claude (2026-10-26), after Faqīrullāh's Rāg Darpaṇ
# (1666), which mirrored Man Singh Tomar's Mānakutūhala into Persian. A mirror: its svaras
# reflect about Sa and Ma (r and N, G and d), the lower half climbing ½ 1½ 1 and the upper
# 1 1½ ½, each with the ḥijāz step; Ma sits at the octave's middle (600 cents), the mirror's
# plane, and any phrase may be answered by its reflection. Its rasa follows Faqīrullāh: love
# that stirs tender longing in the heart, the vāsakasajjā nāyikā at her mirror at dusk.
raga: darpaṇ
plain: darpan
thaat: pūrvī
svaras: S r=16/15 G=5/4 M=600c d=8/5 N=15/8
aroha: S r G M G M d N S'
avaroha: S' N d M d M G r S
vadi: M
samvadi: S
pakad: G M d M G | r G M d N d M G r
andolan: d 15 0.4
kan: S>N
gamak: M 3
time: dusk
rasa: śṛṅgāra in separation (vipralambha)
)sheets";
        return sheets;
    }
}
