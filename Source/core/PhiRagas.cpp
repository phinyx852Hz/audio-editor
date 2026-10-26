// SPDX-License-Identifier: AGPL-3.0-only
// The raga sheets PHI comes with (format in PhiRaga.h). The list is the owner's (2026-10-25). The
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
)sheets";
        return sheets;
    }
}
