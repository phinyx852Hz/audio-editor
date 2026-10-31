import re, json, os
rows={}
segs={}
last=None
for line in open("meter_out.txt"):
    if line.startswith("   segments:"): segs[last]=[float(v) for v in line.split(":")[1].split()]; continue
    name=line.split()[0]; vals=dict(re.findall(r"([A-Za-z0-9()]+)=\s*(-?[\d.]+)",line)); rows[name]={k:float(v) for k,v in vals.items()}; last=name
def r(prefix):
    for k in rows:
        if k.startswith(prefix): return rows[k],k
    raise KeyError(prefix)
tests=[]  # (case, file, quantity, expected, lo, hi, measured)
def add(case,prefix,q,key,exp,lo,hi,val=None):
    v,fn=r(prefix); m=v[key] if val is None else val
    tests.append(dict(case=case,file=fn,quantity=q,expected=exp,tol=f"+{hi}/−{lo}" if lo!=hi else f"±{lo}",measured=round(m,2),passed=exp-lo-1e-9<=m<=exp+hi+1e-9))
for c,p,e in [(1,"seq-3341-1-",-23),(2,"seq-3341-2-",-33),(3,"seq-3341-3-",-23),(4,"seq-3341-4-",-23),(5,"seq-3341-5-",-23),(6,"seq-3341-6-6ch",-23),(7,"seq-3341-7_",-23),(8,"seq-3341-2011-8_",-23)]:
    add(f"3341 #{c}",p,"Integrated (I)","I",e,0.1,0.1)
add("3341 #1","seq-3341-1-","Max momentary (M)","M10",-23,0.1,0.1); add("3341 #1","seq-3341-1-","Max short-term (S)","S10",-23,0.1,0.1)
add("3341 #2","seq-3341-2-","Max momentary (M)","M10",-33,0.1,0.1); add("3341 #2","seq-3341-2-","Max short-term (S)","S10",-33,0.1,0.1)
add("3341 #9","seq-3341-9-","Short-term (S), max","S10",-23,0.1,0.1)
for i in range(1,21): add("3341 #10",f"seq-3341-10-{i}-","Max short-term (S)","S10",-23,0.1,0.1)
for i,v in enumerate(segs[r("seq-3341-11-")[1]]): add("3341 #11","seq-3341-11-",f"Max S, segment {i+1}",None,-38+i,0.1,0.1,v)
add("3341 #12","seq-3341-12-","Momentary (M), max","M10",-23,0.1,0.1)
for i in range(1,21): add("3341 #13",f"seq-3341-13-{i}-","Max momentary (M)","M10",-23,0.1,0.1)
for i,v in enumerate(segs[r("seq-3341-14-")[1]]): add("3341 #14","seq-3341-14-",f"Max M, segment {i+1}",None,-38+i,0.1,0.1,v)
for c,e in [(15,-6),(16,-6),(17,-6),(18,-6),(19,3),(20,0),(21,0),(22,0),(23,0)]: add(f"3341 #{c}",f"seq-3341-{c}-","True peak (dBTP)","TP",e,0.4,0.2)
for c,p,e in [(1,"seq-3342-1-",10),(2,"seq-3342-2-",5),(3,"seq-3342-3-",20),(4,"seq-3342-4-",15),(5,"seq-3341-7_",5),(6,"seq-3341-2011-8_",15)]:
    add(f"3342 #{c}",p,"Loudness range (LRA)","LRA(file)",e,1,1)
add("Tech 3343 §8.2","EBU-reference","Integrated (I)","I",-23,0.1,0.1)
json.dump(tests,open(os.environ.get("OUT","out")+"/conformance.json","w"),indent=1)
print(len(tests),"checks,",sum(t["passed"] for t in tests),"passed"); [print(t) for t in tests if not t["passed"]]
