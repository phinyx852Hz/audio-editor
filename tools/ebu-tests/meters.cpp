// Reads the EBU loudness test set (Tech 3341, 3342) with PHI's loudness code, extracted from
// Source/core/Dsp.cpp by run.sh: integrated loudness, LRA, the maximum momentary and short-term
// loudness (every 100 ms as before, and every 10 ms as the Analysis Meters now take them) and true peak.
#include "extracted.h"
using namespace dsp;
struct Wav { int ch=0, rate=0; std::vector<std::vector<float>> x; };
static uint32_t u32(const unsigned char*p){return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}
static Wav readWav(const char* path){
  std::ifstream f(path, std::ios::binary); std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
  Wav w; size_t pos=12; int bits=16, fmt=1;
  while(pos+8<=b.size()){ std::string id((char*)&b[pos],4); uint32_t sz=u32(&b[pos+4]); const unsigned char* d=&b[pos+8];
    if(id=="fmt "){ fmt=d[0]|d[1]<<8; w.ch=d[2]|d[3]<<8; w.rate=u32(d+4); bits=d[14]|d[15]<<8; if(fmt==0xFFFE) fmt=d[24]|d[25]<<8; }
    else if(id=="data"){ int bps=bits/8; size_t n=std::min<size_t>(sz,b.size()-pos-8)/(bps*w.ch); w.x.assign(w.ch,std::vector<float>(n));
      for(size_t i=0;i<n;i++) for(int c=0;c<w.ch;c++){ const unsigned char* s=d+(i*w.ch+c)*bps; double v;
        if(fmt==3&&bits==32){float fv; memcpy(&fv,s,4); v=fv;}
        else if(bits==16) v=(int16_t)(s[0]|s[1]<<8)/32768.0;
        else if(bits==24){ int32_t t=(s[0]<<8|s[1]<<16|s[2]<<24); v=(t>>8)/8388608.0;}
        else { int32_t t=(int32_t)u32(s); v=t/2147483648.0; }
        w.x[c][i]=(float)v; } }
    pos+=8+sz+(sz&1); }
  return w; }
// BS.1770 channel weights for up to 6 channels (L R C LFE Ls Rs, or L R C Ls Rs for 5)
static double weight(int c,int n){ if(n==6){ if(c==3) return 0; if(c>=4) return 1.41;} if(n==5&&c>=3) return 1.41; return 1.0; }
int main(int argc,char**argv){
  for(int a=1;a<argc;a++){ Wav w=readWav(argv[a]); int n=w.ch; size_t len=w.x[0].size();
    std::vector<KWeighting> k(n); for(auto&f:k) f.prepare(w.rate); std::vector<TruePeakDetector> tp(n);
    int bl=(int)std::lround(w.rate*0.1); std::vector<double> blocks, sums(n,0.0); int in=0; float sp=0,tpk=0;
    for(size_t i=0;i<len;i++){ for(int c=0;c<n;c++){ float x=w.x[c][i]; sp=std::max(sp,std::abs(x)); tpk=std::max(tpk,tp[c].process(x)); float y=k[c].process(x); sums[c]+=(double)y*y; }
      if(++in==bl){ double p=0; for(int c=0;c<n;c++) p+=weight(c,n)*sums[c]/bl; blocks.push_back(p); std::fill(sums.begin(),sums.end(),0.0); in=0; } }
    // 10 ms steps, as AnalysisWindow's State now takes the maxima
    int sl=(int)std::lround(w.rate*0.01); std::vector<double> stp; { std::vector<double> ss(n,0.0); int f=0; std::vector<KWeighting> k2(n); for(auto&q:k2) q.prepare(w.rate);
      for(size_t i=0;i<len;i++){ double p=0; for(int c=0;c<n;c++){ float y=k2[c].process(w.x[c][i]); ss[c]+=(double)y*y;} if(++f==sl){ for(int c=0;c<n;c++) p+=weight(c,n)*ss[c]/sl; stp.push_back(p); std::fill(ss.begin(),ss.end(),0.0); f=0;} } }
    auto winMax=[&](size_t from,size_t to,int len){ double m=-200; for(size_t i=std::max<size_t>(from,len-1);i<std::min(to,stp.size());i++){ double s=0; for(size_t j=i+1-len;j<=i;j++) s+=stp[j]; m=std::max(m,lufsFromPower(s/len)); } return m; };
    double maxM10=winMax(0,stp.size(),40), maxS10=winMax(0,stp.size(),300);
    IntegratedLoudness il; for(double p:blocks) il.add(p);
    // live meter: momentary/short-term at each block, maxima
    double maxM=-200,maxS=-200; std::vector<double> sh; 
    for(size_t i=0;i<blocks.size();i++){ auto mean=[&](size_t cnt){cnt=std::min(cnt,i+1); double s=0; for(size_t j=i+1-cnt;j<=i;j++) s+=blocks[j]; return lufsFromPower(s/cnt);};
      if(i>=3) maxM=std::max(maxM,mean(4)); if(i>=29){ double s=mean(30); maxS=std::max(maxS,s); sh.push_back(s);} }
    // measureLoudness LRA (1 s hop)
    std::vector<double> st; for(size_t i=29;i<blocks.size();i+=10){double s=0; for(size_t j=i-29;j<=i;j++) s+=blocks[j]; st.push_back(s/30);}
    auto lra=[&](std::vector<double> lufsv){ std::vector<double> g; double m=0; for(double v:lufsv) if(v>-70){g.push_back(v); m+=power(v);} if(g.size()<2) return 0.0; m=lufsFromPower(m/g.size())-20; std::vector<double> l; for(double v:g) if(v>m) l.push_back(v); if(l.size()<2) return 0.0; std::sort(l.begin(),l.end()); auto at=[&](double q){return l[(size_t)std::lround(q*(l.size()-1))];}; return at(0.95)-at(0.10);};
    std::vector<double> stl; for(double p:st) stl.push_back(lufsFromPower(p));
    std::string name=argv[a]; name=name.substr(name.find_last_of('/')+1);
    printf("%-46s M10=%7.2f S10=%7.2f I=%7.2f  LRA(file)=%5.2f LRA(live)=%5.2f  maxM=%7.2f maxS=%7.2f  TP=%6.2f SP=%6.2f\n", name.c_str(), maxM10, maxS10, il.value(), lra(stl), lra(sh), maxM, maxS, 20*std::log10(std::max(tpk,sp)), 20*std::log10(sp));
  
    if(name.find("3341-14-")!=std::string::npos||name.find("3341-11-")!=std::string::npos){ bool m14=name.find("-14-")!=std::string::npos; size_t seg=m14?80:600; printf("   segments:"); for(int i=0;i<20;i++) printf(" %.2f", winMax(i*seg, (i+1)*seg, m14?40:300)); printf("\n"); }
  } }
