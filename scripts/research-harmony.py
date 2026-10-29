# Writes share/research/modes-9-harmony.md from docs/harmony.md (the Mode menu's harmony, PR #83):
#   python3 scripts/research-harmony.py docs/harmony.md > share/research/modes-9-harmony.md
# Run it again when docs/harmony.md changes, so Research > Modes > Harmony stays one-to-one with it.
import re, sys, urllib.parse
src=open(sys.argv[1]).read().split('\n')
part=sub=None; out=[]; groups={}
order=[]
multi={"Appalachian shape-note singing","Black American choral harmony"}
for line in src:
    if line.startswith('## '):
        h=line[3:].strip()
        part = {'Western art music':'Western art music','jazz':'Jazz','the world':'The world','grown from meeting Western harmony':'Grown from meeting Western harmony'}.get(h)
        sub=None; continue
    if line.startswith('### '):
        sub=line[4:].strip(); continue
    m=re.match(r'- \*\*(.+?)\*\* \((.*)\)\. (.*)$', line)
    if not m or part is None: continue
    name, meta, rest = m.groups()
    # meta: "tradition, dates; texture" -- last ';' splits texture
    tradmeta, texture = meta.rsplit(';',1) if ';' in meta else (meta,'')
    about = rest
    son = srcs = heard = ''
    mm=re.search(r'\*Heard in:\* (.*)$', about)
    if mm: heard=mm.group(1).rstrip('.'); about=about[:mm.start()].strip()
    mm=re.search(r'\*Sources:\* (.*)$', about)
    if mm: srcs=mm.group(1).strip(); about=about[:mm.start()].strip()
    mm=re.search(r'\*Sonorities:\* (.*)$', about)
    if mm: son=mm.group(1).strip(); about=about[:mm.start()].strip()
    path=['Modes','Harmony',part]
    if sub: path.append(sub[0].upper()+sub[1:] if part=='Western art music' else sub)
    trad=tradmeta.split(',')[0].strip()
    if trad in multi: path.append(trad)
    g=' / '.join(path)
    if g not in groups: groups[g]=[]; order.append(g)
    md=f"## {name}\n\n{about}\n\n**From:** {tradmeta.strip().rstrip('.')}. **Texture:** {texture.strip()}.\n\n"
    if son: md+=f"**Sonorities:** {son.rstrip('.')}.\n\n"
    q=urllib.parse.quote(name)
    md+=f"- [Search Wikipedia for “{name}”](https://en.wikipedia.org/w/index.php?search={q})\n"
    if srcs:
        for s in [x.strip() for x in srcs.rstrip('.').split(';') if x.strip()]:
            md+=f"- {s}.\n"
    groups[g].append(md)
print("<!--\nModes > Harmony, one entry for each of Mode > harmony's (Source/mixer/Harmonies.cpp,\ndocs/harmony.md, PR #83), in the menu's order, made from docs/harmony.md by scripts/research-harmony.py. What each is, where it\ncomes from, its sonorities and its sources.\n-->\n")
# The literature's overview, as an entry of its own at the top.
text='\n'.join(src)
lit=text[text.index('## What the literature says')+len('## What the literature says'):text.index('## Sources not yet confirmed')].strip()
print("# Modes / Harmony\n")
print("## Harmony beyond the West\n")
print("Harmony here is any way of sounding more than one pitch at a time on purpose: chords that move by rule, but also drones, parallel and diaphonic singing, heterophony, interlocking and hocket, overtone singing and stratified polyphony.\n")
print(lit+"\n")
print("- [Polyphony](https://en.wikipedia.org/wiki/Polyphony) and [Heterophony](https://en.wikipedia.org/wiki/Heterophony), Wikipedia.")
print("- Joseph Jordania, *Who Asked the First Question? The Origins of Human Choral Singing, Intelligence, Language and Speech* (Logos, 2006).")
print("- Ardian Ahmedaja and Gerlinde Haid (eds.), *European Voices* (Böhlau, 2008–).\n")
n=0
for g in order:
    print(f"# {g}\n")
    for md in groups[g]:
        print(md); n+=1
print(f"<!-- {n} entries -->", file=sys.stderr)
