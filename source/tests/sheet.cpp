// Renders every pattern to a contact sheet (2x2 tiles each, to expose seams) + full-screen kinds.
#include "../src/engine.h"
#include <cstdio>
#include <vector>
static void savebmp(const char* fn, const std::vector<uint32_t>& px, int W, int H){
  FILE* f=fopen(fn,"wb"); int rb=(W*3+3)&~3; BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{}; ih.biSize=sizeof ih; ih.biWidth=W; ih.biHeight=H; ih.biPlanes=1; ih.biBitCount=24;
  fh.bfType=0x4D42; fh.bfOffBits=sizeof fh+sizeof ih; fh.bfSize=fh.bfOffBits+rb*H; fwrite(&fh,sizeof fh,1,f); fwrite(&ih,sizeof ih,1,f);
  std::vector<uint8_t> row(rb); for(int y=H-1;y>=0;y--){ for(int x=0;x<W;x++){uint32_t c=px[y*W+x]; row[x*3]=c&255; row[x*3+1]=(c>>8)&255; row[x*3+2]=(c>>16)&255;} fwrite(row.data(),rb,1,f);} fclose(f);}
int main(int argc,char**argv){
  const auto& P=Patterns(); int cell=300, cols=8; int n=P.size(); int rows=(n+cols-1)/cols;
  std::vector<uint32_t> sheet(cell*cols*cell*rows,0x202020);
  for(int i=0;i<n;i++){ if(P[i].kind==K_HEADER) continue; BgSettings s; s.pattern=i; for(int k=0;k<4;k++) s.colors[k]=P[i].def[k]; s.seed=1; s.fit=DefaultFit(P[i].kind); s.zoom=1;
    int cx=(i%cols)*cell, cy=(i/cols)*cell; uint32_t* d=&sheet[cy*cell*cols+cx];
    if(IsTiled(P[i].kind)) RenderTiled(s,d,cell*cols,-37,-53,cell-4,cell-4); else RenderDisplay(s,d,cell*cols,cell-4,(cell-4)*9/16);
    printf("%2d %ls\n",i,P[i].name.c_str()); }
  savebmp("sheet.bmp",sheet,cell*cols,cell*rows);
  // full-size check of a full-screen composition on a 21:9 display
  BgSettings s; s.pattern=FindPattern(L"Citrus Citrus"); for(int k=0;k<4;k++) s.colors[k]=P[s.pattern].def[k]; s.fit=FIT_SCALE; s.colors[0]=0x803000;
  std::vector<uint32_t> big(1720*720); RenderDisplay(s,big.data(),1720,1720,720); savebmp("full.bmp",big,1720,720);
  return 0; }
