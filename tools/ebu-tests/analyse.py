import csv, json, numpy as np, matplotlib
matplotlib.use("Agg"); import matplotlib.pyplot as plt
import os; OUT=os.environ.get("OUT","out")+"/"; fs=48000; res={}
plt.rcParams.update({"figure.dpi":110,"font.size":9,"axes.grid":True,"grid.alpha":0.3})
# EQ: cookbook (independent, from the uploaded text) vs PHI's responseDb vs PHI's impulse response
def cook(kind,f0,Q,g):
    w0=2*np.pi*f0/fs; c,s=np.cos(w0),np.sin(w0); al=s/(2*Q); A=10**(g/40)
    if kind=="bell": b=[1+al*A,-2*c,1-al*A]; a=[1+al/A,-2*c,1-al/A]
    elif kind=="lowshelf": be=2*np.sqrt(A)*al; b=[A*((A+1)-(A-1)*c+be),2*A*((A-1)-(A+1)*c),A*((A+1)-(A-1)*c-be)]; a=[(A+1)+(A-1)*c+be,-2*((A-1)+(A+1)*c),(A+1)+(A-1)*c-be]
    elif kind=="highshelf": be=2*np.sqrt(A)*al; b=[A*((A+1)+(A-1)*c+be),-2*A*((A-1)+(A+1)*c),A*((A+1)+(A-1)*c-be)]; a=[(A+1)-(A-1)*c+be,2*((A-1)-(A+1)*c),(A+1)-(A-1)*c-be]
    elif kind=="notch": b=[1,-2*c,1]; a=[1+al,-2*c,1-al]
    elif kind=="bandpass": b=[al,0,-al]; a=[1+al,-2*c,1-al]
    return np.array(b),np.array(a)
def H(b,a,f):
    z=np.exp(-1j*2*np.pi*f/fs); return 20*np.log10(np.abs((b[0]+b[1]*z+b[2]*z*z)/(a[0]+a[1]*z+a[2]*z*z)))
cases={"bell_1k_+6":("bell",1000,2.0581710272714924,6),"bell_1k_-12":("bell",1000,2.0581710272714924,-12),"lowshelf_80_+6":("lowshelf",80,0.7071067811865476,6),
 "highshelf_15.9k_-6":("highshelf",15900,0.7071067811865476,-6),"notch_1k":("notch",1000,2.0581710272714924,0),"bandpass_1k":("bandpass",1000,2.0581710272714924,0)}
rows=list(csv.DictReader(open("eq.csv"))); eqres={}
fig,ax=plt.subplots(figsize=(7.5,4))
for name,(k,f0,Q,g) in cases.items():
    f=np.array([float(r["hz"]) for r in rows if r["case"]==name]); phi=np.array([float(r["responseDb"]) for r in rows if r["case"]==name])
    b,a=cook(k,f0,Q,g); ref=H(b,a,f)
    imp=np.fromfile(f"eq_{name}.raw",dtype=np.float32); spec=np.fft.rfft(imp); ff=np.fft.rfftfreq(len(imp),1/fs); meas=np.interp(f,ff,20*np.log10(np.abs(spec)+1e-30))
    mask=ref>-40  # ignore the notch's bottom
    eqres[name]={"maxDiffResponseVsCookbook":float(np.max(np.abs(phi-ref)[mask])),"maxDiffProcessedVsCookbook":float(np.max(np.abs(meas-ref)[mask]))}
    ax.semilogx(f,ref,lw=2.5,alpha=0.35); ax.semilogx(f,meas,"--",lw=1,color=ax.lines[-1].get_color(),label=name.replace("_"," "))
ax.set_xlabel("Frequency (Hz)"); ax.set_ylabel("dB"); ax.set_ylim(-30,10); ax.set_title("Φ EQ: processed response (dashed) over the Cookbook formulas (wide)"); ax.legend(fontsize=7,ncol=2); fig.tight_layout(); fig.savefig(OUT+"eq.png"); res["eq"]=eqres
# Multiband sum
imp=np.fromfile("multiband_impulse.raw",dtype=np.float32); spec=20*np.log10(np.abs(np.fft.rfft(imp))+1e-30); ff=np.fft.rfftfreq(len(imp),1/fs); band=(ff>=20)&(ff<=20000)
res["multiband"]={"sumMinDb":float(spec[band].min()),"sumMaxDb":float(spec[band].max())}
fig,ax=plt.subplots(figsize=(7.5,3)); ax.semilogx(ff[band],spec[band]); ax.set_ylim(-1,1); [ax.axvline(x,color="gray",ls=":") for x in (209.4,887.3,3758.4)]
ax.set_xlabel("Frequency (Hz)"); ax.set_ylabel("dB"); ax.set_title("Φ Multiband at unity: the four bands summed (crossovers dotted)"); fig.tight_layout(); fig.savefig(OUT+"multiband.png")
res["multibandUnity"]=list(csv.DictReader(open("multiband_unity.csv")))
# Compressor curve
rows=list(csv.DictReader(open("compressor_curve.csv"))); fig,ax=plt.subplots(figsize=(5,4.5)); cres={}
names={2:"φ (1.62:1)",4:"φ² (2.62:1)",8:"φ⁴ (6.85:1)"}
for st in (2,4,8):
    x=np.array([float(r["inDb"]) for r in rows if int(r["ratioStep"])==st]); y=np.array([float(r["outDb"]) for r in rows if int(r["ratioStep"])==st]); d=np.array([float(r["designDb"]) for r in rows if int(r["ratioStep"])==st])
    ax.plot(x,d,lw=2.5,alpha=0.35); ax.plot(x,y,"o",ms=3,color=ax.lines[-1].get_color(),label=names[st])
    cres[names[st]]={"maxDev":float(np.max(np.abs(y-d))),"atZero_measured":float(y[-1]),"atZero_design":float(d[-1]),"reductionShortfallPct":float(100*np.max(((x-y)-(x-d))/np.maximum(1e-9,(x-d)) * ((x-d)>0.5) * -1))}
ax.plot([-60,0],[-60,0],"k:",lw=0.8); ax.set_xlabel("1 kHz tone in (dBFS peak)"); ax.set_ylabel("out (dBFS peak)"); ax.set_title("Φ Compressor: measured (dots) over the design curve\nthreshold −18 dBFS, knee 6.85 dB"); ax.legend(); fig.tight_layout(); fig.savefig(OUT+"compressor_curve.png"); res["compressorCurve"]=cres
# Compressor timing on EBU signal 5
t,g=np.loadtxt("compressor_timing.csv",delimiter=",",skiprows=1,unpack=True)
def settle(t0,t1):
    m=(t>=t0)&(t<t1); tt=t[m]-t0; gg=g[m]; start,end=gg[0],np.median(gg[-200:]); frac=(gg-start)/(end-start)
    return {"from":float(start),"to":float(end),"t63ms":float(1000*tt[np.argmax(frac>=0.632)]),"t90ms":float(1000*tt[np.argmax(frac>=0.9)])}
res["compressorTiming"]={"attack (−26→−20 dBFS at 20 s)":settle(20.0,22.0),"release (−20→−26 dBFS at 40.1 s)":settle(40.1,43.0)}
fig,axs=plt.subplots(1,2,figsize=(8,3))
for axx,(t0,lab) in zip(axs,((20.0,"attack: step up 6 dB at 20 s"),(40.1,"release: step down 6 dB at 40.1 s"))):
    m=(t>=t0-0.1)&(t<t0+1.0); axx.plot((t[m]-t0)*1000,g[m]); axx.set_xlabel("ms after the step"); axx.set_ylabel("gain (dB)"); axx.set_title(lab)
fig.suptitle("Φ Compressor on EBU test signal 5 (threshold −30 dBFS, φ², hard knee, 13 ms / 233 ms)"); fig.tight_layout(); fig.savefig(OUT+"compressor_timing.png")
# Limiter
rows=list(csv.DictReader(open("limiter.csv"))); res["limiter"]=rows
fig,ax=plt.subplots(figsize=(7.5,3.5)); short={"seq-3341-7_seq-3342-5-24bit.wav":"Programme 1 (NLR)","seq-3341-2011-8_seq-3342-6-24bit-v02.wav":"Programme 2 (WLR)","EBU-reference_listening_signal_pinknoise_500Hz_2kHz_R128.wav":"Reference pink noise"}
for fn,lab in short.items():
    d=[float(r["drive"]) for r in rows if r["file"]==fn]; a=[float(r["outTP4"]) for r in rows if r["file"]==fn]; b=[float(r["outTP32"]) for r in rows if r["file"]==fn]
    ax.plot(d,a,"o-",label=lab+" (PHI meter)"); ax.plot(d,b,"x--",color=ax.lines[-1].get_color(),label=lab+" (32× reference)")
ax.axhline(-1,color="red",lw=1); ax.set_ylim(-10,0); ax.set_xlabel("Drive (dB)"); ax.set_ylabel("True peak out (dBTP)"); ax.set_title("Φ Limiter, ceiling −1 dBTP, on EBU programme material"); ax.legend(fontsize=7); fig.tight_layout(); fig.savefig(OUT+"limiter.png")
json.dump(res,open(OUT+"results.json","w"),indent=1); print(json.dumps({k:res[k] for k in ("eq","multiband","compressorCurve","compressorTiming")},indent=1))
