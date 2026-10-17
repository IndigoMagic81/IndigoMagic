// For each tile: compare pixel change across the wrap seam with the average change between neighbours.
#include "../src/engine.h"
#include <cstdio>
#include <cmath>
int main(){ const auto& P=Patterns(); int bad=0;
 for(size_t i=0;i<P.size();i++){ if(P[i].kind!=K_GEN && P[i].kind!=K_TILE4 && P[i].kind!=K_TILE2) continue;
  const IndexImage* im=GetIndexImage((int)i,1); int w=im->w,h=im->h; uint32_t pal[4]={0,85,170,255};
  auto L=[&](int x,int y){ uint8_t v=im->px[y*w+x]; return P[i].kind==K_TILE2? v*255.0 : (double)(v==3?0:v==1?85:v==0?170:255); };
  double in=0,seamX=0,seamY=0; int n=0;
  for(int y=0;y<h;y++) for(int x=0;x+1<w;x++){ in+=fabs(L(x,y)-L(x+1,y)); n++; } in/=n;
  for(int y=0;y<h;y++) seamX+=fabs(L(w-1,y)-L(0,y)); seamX/=h;
  for(int x=0;x<w;x++) seamY+=fabs(L(x,h-1)-L(x,0)); seamY/=w;
  double r=std::max(seamX,seamY)/(in+1e-9); if(r>1.6) bad++;
  printf("%-14ls %3dx%-3d interior %6.1f  seamX %6.1f seamY %6.1f  ratio %.2f%s\n",P[i].name.c_str(),w,h,in,seamX,seamY,r, r>1.6?"  <-- check":""); }
 printf("flagged: %d\n",bad); }
