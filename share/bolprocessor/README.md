# Bol Processor examples

Bernard Bel and Jim Kippen's tablā and kathak examples for the Bol Processor, from Bel's BP3 test
set (github.com/bolprocessor/bp3-ctests, commit a34bd4a). The grammars are Kippen and Bel's work,
modelling the improvisation of tablā players; PHI ships them with that credit (owner, 2026-10-31:
"Tabla and kathak grammars only"). Each grammar and data file names in its header the alphabet and
settings it uses, all in this folder.

## Grammars (they produce items)

- `-gr.dhin--` (with `-al.dhin--`, `-se.dhin--`): the Ajrada gharānā qāʿida "dhin--dhagena". This
  is the grammar Kippen and Bel built in BP1, with rule weights inferred from real performances.
  PHI's owner learned the qāʿida from Ustad Akram Khan, a direct descendant of that lineage, and
  included it in PHI on that authority (2026-10-31). The alphabet's `*` homomorphism is the khālī:
  dha becomes ta, ge becomes ke and dhin becomes tin.
- `-gr.dhati2` (with `-al.dhati`, `-se.dhati2`): the qāʿida "dhatidhagena" commented in Kippen and
  Bel, "Modelling music with grammars" (1992), in Marsden and Pople (eds), *Computer
  Representations and Models in Music*. `-so.dhati` holds Bel's MIDI sound-objects for its bols,
  made on a Roland D-50.
- `-gr.dhadhatite` (with `-al.dhadhatite`, `-se.dhadhatite`): the qāʿida "dhadhatite".
- `-gr.12345678` (with `-al.EkDoTin`, `-se.EkDoTin`, `-so.EkDoTin`): a well-known kathak tihāī on the
  counting syllables (ek, do, tin...), showing BP's performance controls. It makes notes from its
  sound-objects, so it plays as BP's own MIDI file.

## Data (items already, played as written)

- `-da.dhin--`, `-da.dhin2--`: the dhin--dhagena qāʿida written out.
- `-da.dhati2`, `-da.dhadhatite`: the other two qāʿidas written out, for the grammars to analyse.
- `-da.kathak`: kathak material composed by Pandit Birju Maharaj and documented by Andréine Bel,
  in tīntāl, with its changes of speed, a chakkardār and others.

Open them with Compose > South and West Asia > Bol Processor... (docs/bol-processor.md).
