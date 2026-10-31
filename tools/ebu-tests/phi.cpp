// Measures the Φ Limiter, Compressor, Multiband and EQ on EBU signals and tones, with PHI's
// loudness code (checked against the EBU set by meters.cpp) and an independent 32x true-peak reading.
#include "extracted.h"
#include "../../Source/core/PhiDynamics.h"
#include "../../Source/core/Phi.h"
#include <map>
#include <functional>
#include <cstdlib>
using namespace dsp;
struct Wav { int ch=0, rate=0; std::vector<std::vector<float>> x; };
static uint32_t u32(const unsigned char*p){return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}
static Wav readWav(const std::string& path){
  std::ifstream f(path, std::ios::binary); std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
  Wav w; size_t pos=12; int bits=16, fmt=1;
  while(pos+8<=b.size()){ std::string id((char*)&b[pos],4); uint32_t sz=u32(&b[pos+4]); const unsigned char* d=&b[pos+8];
    if(id=="fmt "){ fmt=d[0]|d[1]<<8; w.ch=d[2]|d[3]<<8; w.rate=u32(d+4); bits=d[14]|d[15]<<8; if(fmt==0xFFFE) fmt=d[24]|d[25]<<8; }
    else if(id=="data"){ int bps=bits/8; size_t n=std::min<size_t>(sz,b.size()-pos-8)/(bps*w.ch); w.x.assign(w.ch,std::vector<float>(n));
      for(size_t i=0;i<n;i++) for(int c=0;c<w.ch;c++){ const unsigned char* s=d+(i*w.ch+c)*bps; double v;
        if(bits==16) v=(int16_t)(s[0]|s[1]<<8)/32768.0; else { int32_t t=(s[0]<<8|s[1]<<16|s[2]<<24); v=(t>>8)/8388608.0;}
        w.x[c][i]=(float)v; } }
    pos+=8+sz+(sz&1); }
  return w; }
struct Meas { double I, lra, tp, sp; };
static Meas measure(const Wav& w){
  int n=w.ch; size_t len=w.x[0].size(); std::vector<KWeighting> k(n); for(auto&f:k) f.prepare(w.rate); std::vector<TruePeakDetector> tp(n);
  int bl=(int)std::lround(w.rate*0.1); std::vector<double> blocks, sums(n,0.0); int in=0; float sp=0,tpk=0;
  for(size_t i=0;i<len;i++){ for(int c=0;c<n;c++){ float x=w.x[c][i]; sp=std::max(sp,std::abs(x)); tpk=std::max(tpk,tp[c].process(x)); float y=k[c].process(x); sums[c]+=(double)y*y; }
    if(++in==bl){ double p=0; for(int c=0;c<n;c++) p+=sums[c]/bl; blocks.push_back(p); std::fill(sums.begin(),sums.end(),0.0); in=0; } }
  IntegratedLoudness il; for(double p:blocks) il.add(p);
  std::vector<double> st; for(size_t i=29;i<blocks.size();i+=10){double s=0; for(size_t j=i-29;j<=i;j++) s+=blocks[j]; st.push_back(lufsFromPower(s/30));}
  std::vector<double> g; double m=0; for(double v:st) if(v>-70){g.push_back(v); m+=power(v);} double lra=0;
  if(g.size()>=2){ m=lufsFromPower(m/g.size())-20; std::vector<double> l; for(double v:g) if(v>m) l.push_back(v); if(l.size()>=2){ std::sort(l.begin(),l.end()); auto at=[&](double q){return l[(size_t)std::lround(q*(l.size()-1))];}; lra=at(0.95)-at(0.10);} }
  return { il.value(), lra, 20*std::log10(std::max(tpk,sp)), 20*std::log10(sp) }; }
// An independent true-peak reading: 32x oversampling with a 64-tap-per-phase Kaiser-windowed sinc.
static double truePeak32(const Wav& w){
  const int L=32, T=64; std::vector<double> h(L*T); auto bessel0=[](double x){double s=1,t=1; for(int k=1;k<40;k++){t*=(x/2)/k; s+=t*t;} return s;}; double beta=10;
  for(int i=0;i<L*T;i++){ double x=(i-(L*T-1)/2.0)/L; double sinc=std::abs(x)<1e-12?1:std::sin(pi*x)/(pi*x); double r=2.0*i/(L*T-1)-1; h[i]=sinc*bessel0(beta*std::sqrt(std::max(0.0,1-r*r)))/bessel0(beta); }
  double peak=0; for(auto& ch:w.x){ size_t n=ch.size(); for(size_t i=0;i<n;i++){ peak=std::max(peak,(double)std::abs(ch[i])); }
    for(size_t i=T;i+T<n;i++){ // only look between samples where something is loud
      if(std::abs(ch[i])<peak*0.5 && std::abs(ch[i+1])<peak*0.5) continue;
      for(int p=1;p<L;p++){ double y=0; for(int t=0;t<T;t++){ long idx=(long)i+T/2-t; y+=h[t*L+p]*ch[idx]; } peak=std::max(peak,std::abs(y)); } } }
  return 20*std::log10(peak); }
template<class P> static Wav run(const Wav& in, P& proc, int latency=0){
  Wav o=in; int n=in.ch; size_t len=in.x[0].size(); std::vector<std::vector<float>> buf(n,std::vector<float>(512)); std::vector<float*> ptr(n);
  for(int c=0;c<n;c++) { ptr[c]=buf[c].data(); o.x[c].assign(len+latency,0.f); }
  for(size_t pos=0;pos<len+latency;pos+=512){ int m=(int)std::min<size_t>(512,len+latency-pos); for(int c=0;c<n;c++) for(int i=0;i<m;i++) buf[c][i]=pos+i<len?in.x[c][pos+i]:0.f;
    proc.process(ptr.data(),n,m); for(int c=0;c<n;c++) for(int i=0;i<m;i++) o.x[c][pos+i]=buf[c][i]; }
  for(int c=0;c<n;c++) o.x[c].erase(o.x[c].begin(), o.x[c].begin()+latency); return o; }
static Wav sine(double hz,double ampDb,double secs,int rate=48000,int ch=2){ Wav w; w.ch=ch; w.rate=rate; size_t n=(size_t)(secs*rate); w.x.assign(ch,std::vector<float>(n)); double a=std::pow(10,ampDb/20);
  for(size_t i=0;i<n;i++){ float v=(float)(a*std::sin(2*pi*hz*i/rate)); for(int c=0;c<ch;c++) w.x[c][i]=v; } return w; }
static double rmsDb(const std::vector<float>& x,size_t a,size_t b){ double s=0; for(size_t i=a;i<b;i++) s+=(double)x[i]*x[i]; return 10*std::log10(s/(b-a)); }
int main(){
  const std::string D=std::string(getenv("EBU_SET")?getenv("EBU_SET"):".")+"/";
  FILE* f;
  // 1. Φ Limiter
  f=fopen("limiter.csv","w"); fprintf(f,"file,drive,inI,inTP,outI,outLRA,outTP4,outTP32,maxGR\n");
  for(std::string name: {"seq-3341-7_seq-3342-5-24bit.wav","seq-3341-2011-8_seq-3342-6-24bit-v02.wav","EBU-reference_listening_signal_pinknoise_500Hz_2kHz_R128.wav",
      "seq-3341-19-24bit.wav.wav","seq-3341-20-24bit.wav.wav","seq-3341-21-24bit.wav.wav","seq-3341-22-24bit.wav.wav","seq-3341-23-24bit.wav.wav"}){
    Wav w=readWav(D+name); Meas a=measure(w);
    for(double drive: {0.0,6.0,12.0,18.0}){ phi::Limiter lim; lim.prepare(w.rate,w.ch); lim.set({drive,-1.0,89.0}); lim.reset();
      Wav o=run(w,lim,lim.latency()); Meas b=measure(o); double t32=truePeak32(o);
      fprintf(f,"%s,%g,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.2f\n",name.c_str(),drive,a.I,a.tp,b.I,b.lra,b.tp,t32,0.0); fflush(f); } }
  fclose(f);
  // 2. Φ Compressor static curve, 1 kHz tones (as EBU test signal 1) from -60 to 0 dBFS
  f=fopen("compressor_curve.csv","w"); fprintf(f,"ratioStep,inDb,outDb,designDb\n");
  for(int step: {2,4,8}){ phi::Compressor::Settings s; s.ratioStep=step;
    for(int lvl=-60; lvl<=0; lvl+=2){ phi::Compressor c; c.prepare(48000,2); c.set(s); c.reset(); Wav w=sine(1000,lvl,3); Wav o=run(w,c);
      double out=rmsDb(o.x[0],96000,144000)+3.0103; fprintf(f,"%d,%d,%.3f,%.3f\n",step,lvl,out,phi::Compressor::curveDb(s,lvl)); } }
  fclose(f);
  // 3. Φ Compressor timing on EBU signal 5 (-26, -20, -26 dBFS): gain over time
  { Wav w=readWav(D+"seq-3341-5-16bit-v02.wav"); phi::Compressor::Settings s; s.thresholdDb=-30; s.ratioStep=4; s.kneeDb=0; phi::Compressor c; c.prepare(w.rate,2); c.set(s); c.reset();
    Wav o=run(w,c); f=fopen("compressor_timing.csv","w"); fprintf(f,"t,gainDb\n"); int hop=w.rate/1000;
    for(size_t i=0;i+hop<=w.x[0].size();i+=hop){ double a=rmsDb(w.x[0],i,i+hop), b=rmsDb(o.x[0],i,i+hop); if(std::isfinite(a)&&a>-80) fprintf(f,"%.3f,%.3f\n",(double)i/w.rate,b-a); }
    fclose(f);
    Meas a=measure(w), b=measure(o); printf("timing file: in I %.2f out I %.2f\n",a.I,b.I); }
  // 4. Φ Multiband at unity (thresholds above any level): loudness change, and the sum's response
  f=fopen("multiband_unity.csv","w"); fprintf(f,"file,inI,outI,dI,inLRA,outLRA\n");
  for(std::string name: {"seq-3341-7_seq-3342-5-24bit.wav","seq-3341-2011-8_seq-3342-6-24bit-v02.wav","EBU-reference_listening_signal_pinknoise_500Hz_2kHz_R128.wav","seq-3342-4-16bit.wav","1kHz Sine -20 LUFS-16bit.wav"}){
    Wav w=readWav(D+name); phi::Multiband::Settings s; for(auto& b:s.bands) b.thresholdDb=24.0; phi::Multiband mb; mb.prepare(w.rate,w.ch); mb.set(s); mb.reset();
    Wav o=run(w,mb); Meas a=measure(w), b=measure(o); fprintf(f,"%s,%.3f,%.3f,%.3f,%.2f,%.2f\n",name.c_str(),a.I,b.I,b.I-a.I,a.lra,b.lra); }
  fclose(f);
  { Wav imp; imp.ch=1; imp.rate=48000; imp.x.assign(1,std::vector<float>(65536,0.f)); imp.x[0][0]=1.f; phi::Multiband::Settings s; for(auto& b:s.bands) b.thresholdDb=24.0; phi::Multiband mb; mb.prepare(48000,1); mb.set(s); mb.reset();
    Wav o=run(imp,mb); f=fopen("multiband_impulse.raw","wb"); fwrite(o.x[0].data(),4,o.x[0].size(),f); fclose(f); }
  // 5. Φ EQ: responseDb against the impulse response it actually produces
  { f=fopen("eq.csv","w"); fprintf(f,"case,hz,responseDb\n"); fclose(f);
    struct Case { const char* name; phi::Eq::Type type; double hz, q, gain; };
    std::vector<Case> cases { {"bell_1k_+6",phi::Eq::Type::bell,1000,phi::Eq::bellQ,6}, {"bell_1k_-12",phi::Eq::Type::bell,1000,phi::Eq::bellQ,-12},
      {"lowshelf_80_+6",phi::Eq::Type::lowShelf,80,phi::Eq::shelfQ,6}, {"highshelf_15.9k_-6",phi::Eq::Type::highShelf,15900,phi::Eq::shelfQ,-6},
      {"notch_1k",phi::Eq::Type::notch,1000,phi::Eq::bellQ,0}, {"bandpass_1k",phi::Eq::Type::bandPass,1000,phi::Eq::bellQ,0} };
    for(auto& cs: cases){ phi::Eq::Settings s; for(auto& b:s.bands) b.type=phi::Eq::Type::off; s.bands[5]={cs.type,cs.hz,cs.q,cs.gain};
      phi::Eq eq; eq.prepare(48000,1); eq.set(s); eq.reset();
      Wav imp; imp.ch=1; imp.rate=48000; imp.x.assign(1,std::vector<float>(65536,0.f)); imp.x[0][0]=1.f; Wav o=run(imp,eq);
      std::string fn=std::string("eq_")+cs.name+".raw"; FILE* g=fopen(fn.c_str(),"wb"); fwrite(o.x[0].data(),4,o.x[0].size(),g); fclose(g);
      FILE* h=fopen("eq.csv","a"); for(int i=0;i<=300;i++){ double hz=20*std::pow(1000.0,i/300.0); fprintf(h,"%s,%.4f,%.6f\n",cs.name,hz,eq.responseDb(hz)); } fclose(h); } }
  return 0; }
