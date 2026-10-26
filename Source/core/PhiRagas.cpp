// SPDX-License-Identifier: AGPL-3.0-only
// The raga sheets PHI comes with (format in PhiRaga.h). The list is the owner's (2026-10-25), with
// their teacher's additions (2026-10-26). Madh kauns is #16 of Ghulām Ḥaidar's Sau Achhūp Rāg (1983):
// Mālkauns with shuddh ga, which it takes mostly going up. The
// tuning of each is as agreed; the āroha, avaroha, vādī, samvādī, pakaḍ and āndolan are a first
// draft for the owner and their teacher to correct.
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

raga: madh kauns
plain: madh kauns
thaat: none
svaras: S g G m d n
aroha: S G m d n S'
avaroha: S' n d m G m g S
vadi: m
samvadi: S
pakad: G m d m G g S
)sheets";
        return sheets;
    }
}
