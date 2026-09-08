#include <SDL.h>
#include <SDL_ttf.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MAX_DIMENSIONS 4096
#include <stb_image.h>
#include <zip.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>
#ifdef __PSP__
#include <pspkernel.h>
#include <pspctrl.h>
#endif

static volatile bool running = true;
#ifdef __PSP__
static int onExit(int, int, void*) { running = false; return 0; }
static int callbacks(SceSize, void*) {
    int id = sceKernelCreateCallback("MangaDock exit", onExit, nullptr);
    sceKernelRegisterExitCallback(id); sceKernelSleepThreadCB(); return 0;
}
#endif
static SDL_Renderer* renderer = nullptr;
static TTF_Font* font = nullptr;
static std::string notice;
static std::string lower(std::string s) {
    for (char& c : s) c = std::tolower(static_cast<unsigned char>(c)); return s;
}
static std::string ext(const std::string& s) {
    size_t i=s.find_last_of('.'); return i==std::string::npos ? "" : lower(s.substr(i));
}
static bool imageName(const std::string& s) {
    auto e=ext(s); return e==".jpg" || e==".jpeg" || e==".png" || e==".bmp" || e==".gif" || e==".tga";
}
static bool archiveName(const std::string& s) {
    auto e=ext(s); return e==".zip" || e==".cbz" || e==".cdz";
}
static bool naturalLess(const std::string& a, const std::string& b) {
    size_t i=0,j=0;
    while(i<a.size() && j<b.size()) {
        if(std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j])) {
            size_t ae=i,be=j;
            while(ae<a.size() && std::isdigit((unsigned char)a[ae])) ++ae;
            while(be<b.size() && std::isdigit((unsigned char)b[be])) ++be;
            size_t ai=i,bj=j;
            while(ai<ae && a[ai]=='0') ++ai;
            while(bj<be && b[bj]=='0') ++bj;
            if(ae-ai != be-bj) return ae-ai < be-bj;
            int cmp=a.compare(ai,ae-ai,b,bj,be-bj);
            if(cmp) return cmp<0;
            i=ae;j=be;
        } else {
            int ac=std::tolower((unsigned char)a[i]),bc=std::tolower((unsigned char)b[j]);
            if(ac!=bc) return ac<bc;
            ++i;++j;
        }
    }
    if(i!=a.size() || j!=b.size()) return i==a.size();
    return a<b;
}
static int favoriteFor(const std::string& key);
struct Entry { std::string name,path; bool dir; int favorite; };
static std::vector<Entry> scan(const std::string& path) {
    std::vector<Entry> out;
    DIR* d=opendir(path.c_str());
    if(!d) { notice="Nao foi possivel abrir a pasta.";return out; }
    while(auto e=readdir(d)) {
        std::string name=e->d_name;
        if(name.empty() || name[0]=='.') continue;
        struct stat st{}; auto full=path+"/"+name;
        if(stat(full.c_str(),&st)) continue;
        bool dir=S_ISDIR(st.st_mode);
        if(dir || (S_ISREG(st.st_mode) && (imageName(name)||archiveName(name)))) out.push_back({name,full,dir,favoriteFor(imageName(name)?path:full)});
        if(out.size()>=10000) {notice="Limite: 10.000 itens por pasta.";break;}
    }
    closedir(d);
    std::sort(out.begin(),out.end(),[](const Entry&a,const Entry&b) {return a.dir!=b.dir ? a.dir>b.dir : naturalLess(a.name,b.name);});
    return out;
}
struct State { int page=0,favorite=0,rtl=1,rotation=0; };
static std::string statePath(const std::string& key) {
    uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:key) {hash^=c;hash*=1099511628211ULL;}
    char name[80];std::snprintf(name,sizeof(name),"state/%016llx.txt",(unsigned long long)hash);return name;
}
static State readState(const std::string& key) {
    State s;std::ifstream in(statePath(key));in>>s.page>>s.favorite>>s.rtl>>s.rotation;
    s.page=std::max(0,s.page);s.favorite=!!s.favorite;s.rtl=!!s.rtl;s.rotation=((s.rotation%4)+4)%4;return s;
}
static int favoriteFor(const std::string& key) { return readState(key).favorite; }
static void saveState(const std::string& key,const State& s) {
    auto path=statePath(key),temp=path+".tmp";
    {std::ofstream out(temp);out<<s.page<<' '<<s.favorite<<' '<<s.rtl<<' '<<s.rotation<<'\n';out.flush();
     if(!out) {notice="Erro ao salvar progresso. Verifique o cartao.";return;}}
    if(std::rename(temp.c_str(),path.c_str())) notice="Erro ao atualizar progresso.";
}
struct Page { std::string name; zip_uint64_t index; };
class Book {
public:
    std::string path;std::vector<Page> pages;zip_t* archive=nullptr;State state;
    ~Book(){close();}
    void close(){if(archive)zip_close(archive);archive=nullptr;pages.clear();path.clear();}
    bool open(const std::string& key,bool zipped) {
        close();path=key;state=readState(path);
        if(zipped) {
            int error=0;archive=zip_open(key.c_str(),ZIP_RDONLY,&error);
            if(!archive){notice="ZIP/CBZ invalido ou nao suportado.";return false;}
            auto count=zip_get_num_entries(archive,0);
            if(count<0 || count>20000){notice="Arquivo possui itens demais. Divida em volumes.";close();return false;}
            for(zip_uint64_t i=0;i<(zip_uint64_t)count;++i) {
                const char* n=zip_get_name(archive,i,0);
                if(n && imageName(n) && std::string(n).find("__MACOSX/")==std::string::npos) pages.push_back({n,i});
            }
        } else for(auto& e:scan(key)) if(!e.dir && imageName(e.name)) pages.push_back({e.path,0});
        std::sort(pages.begin(),pages.end(),[](const Page&a,const Page&b){return naturalLess(a.name,b.name);});
        if(pages.empty()){notice="Nenhuma imagem compativel neste volume.";close();return false;}
        state.page=std::min(state.page,(int)pages.size()-1);return true;
    }
    SDL_Texture* load(int& w,int& h) {
        const size_t limit=12*1024*1024;
        std::vector<unsigned char> bytes;
        auto& page=pages[state.page];
        if(archive) {
            zip_stat_t st;zip_stat_init(&st);
            if(zip_stat_index(archive,page.index,0,&st) || st.size>limit || st.encryption_method!=ZIP_EM_NONE) {notice="Pagina grande ou protegida. Use o importador.";return nullptr;}
            bytes.resize((size_t)st.size);auto f=zip_fopen_index(archive,page.index,0);
            if(!f){notice="Nao foi possivel descompactar a pagina.";return nullptr;}
            size_t done=0;
            while(done<bytes.size()) {auto n=zip_fread(f,bytes.data()+done,bytes.size()-done);if(n<=0)break;done+=(size_t)n;}
            int status=zip_fclose(f);
            if(done!=bytes.size()||status){notice="Pagina compactada corrompida.";return nullptr;}
        } else {
            std::ifstream in(page.name,std::ios::binary|std::ios::ate);auto size=in.tellg();
            if(size<=0 || size>(std::streamoff)limit){notice="Pagina grande ou ilegivel. Use o importador.";return nullptr;}
            bytes.resize((size_t)size);in.seekg(0);in.read((char*)bytes.data(),bytes.size());
            if(!in){notice="Falha ao ler a pagina.";return nullptr;}
        }
        int channels=0;
        if(!stbi_info_from_memory(bytes.data(),(int)bytes.size(),&w,&h,&channels) || w<=0 || h<=0 || w>4096 || h>4096 || (uint64_t)w*h>3000000) {
            notice="Imagem invalida ou acima de 3 MP. Use o importador.";return nullptr;
        }
        auto pixels=stbi_load_from_memory(bytes.data(),(int)bytes.size(),&w,&h,&channels,4);
        if(!pixels){notice="Falha ao decodificar a imagem.";return nullptr;}
        auto surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels,w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);
        auto texture=surface?SDL_CreateTextureFromSurface(renderer,surface):nullptr;
        if(surface)SDL_FreeSurface(surface);stbi_image_free(pixels);
        if(!texture)notice="Memoria insuficiente. Reduza a pagina no PC.";
        return texture;
    }
};
static void rect(int x,int y,int w,int h,SDL_Color c) {
    SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);SDL_Rect r{x,y,w,h};SDL_RenderFillRect(renderer,&r);
}
static void text(int x,int y,std::string s,SDL_Color c={228,233,244,255}) {
    auto surface=TTF_RenderUTF8_Blended(font,s.c_str(),c);if(!surface)return;
    auto texture=SDL_CreateTextureFromSurface(renderer,surface);
    SDL_Rect dst{x,y,surface->w,surface->h};SDL_FreeSurface(surface);
    if(texture){SDL_RenderCopy(renderer,texture,nullptr,&dst);SDL_DestroyTexture(texture);}
}
enum Button {UP=1,DOWN=2,LEFT=4,RIGHT=8,CROSS=16,CIRCLE=32,TRIANGLE=64,SQUARE=128,L=256,R=512,START=1024,SELECT=2048};
static unsigned input() {
    unsigned out=0;SDL_Event e;while(SDL_PollEvent(&e))if(e.type==SDL_QUIT)running=false;
#ifdef __PSP__
    SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);
    const unsigned native[]={PSP_CTRL_UP,PSP_CTRL_DOWN,PSP_CTRL_LEFT,PSP_CTRL_RIGHT,PSP_CTRL_CROSS,PSP_CTRL_CIRCLE,PSP_CTRL_TRIANGLE,PSP_CTRL_SQUARE,PSP_CTRL_LTRIGGER,PSP_CTRL_RTRIGGER,PSP_CTRL_START,PSP_CTRL_SELECT};
    for(int i=0;i<12;++i)if(pad.Buttons&native[i])out|=1u<<i;
    if(pad.Lx<75)out|=LEFT;if(pad.Lx>180)out|=RIGHT;if(pad.Ly<75)out|=UP;if(pad.Ly>180)out|=DOWN;
#else
    auto k=SDL_GetKeyboardState(nullptr);
    const SDL_Scancode keys[]={SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_RETURN,SDL_SCANCODE_ESCAPE,SDL_SCANCODE_T,SDL_SCANCODE_SPACE,SDL_SCANCODE_Q,SDL_SCANCODE_E,SDL_SCANCODE_TAB,SDL_SCANCODE_BACKSPACE};
    for(int i=0;i<12;++i)if(k[keys[i]])out|=1u<<i;
#endif
    return out;
}
int main(int argc,char** argv) {
#ifdef __PSP__
    int thread=sceKernelCreateThread("callbacks",callbacks,0x11,0x1000,0,nullptr);
    if(thread>=0)sceKernelStartThread(thread,0,nullptr);
    sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    std::string root="ms0:/MANGA";
#else
    std::string root=argc>1?argv[1]:"MANGA";
#endif
    mkdir("state",0777);mkdir(root.c_str(),0777);
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)<0)return 1;
    auto window=SDL_CreateWindow("MangaDock",0,0,480,272,0);
    // Software renderer accepts pages larger than the PSP GU 512-pixel texture limit.
    renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE):nullptr;
    if(!renderer || TTF_Init()<0){SDL_Quit();return 2;}
    font=TTF_OpenFont("font.ttf",12);
    if(!font){std::fprintf(stderr,"font.ttf ausente\n");SDL_Quit();return 3;}
    std::string folder=root;auto entries=scan(folder);int selected=0;
    Book book;SDL_Texture* page=nullptr;bool reading=false,hud=true,fitWidth=false;
    int iw=1,ih=1;float zoom=1,panX=0,panY=0;unsigned previous=0;Uint32 repeat=0;
    auto reload=[&](){if(page)SDL_DestroyTexture(page);page=nullptr;notice.clear();page=book.load(iw,ih);panX=panY=0;if(page)saveState(book.path,book.state);};
    while(running) {
        unsigned held=input(),pressed=held&~previous;previous=held;auto now=SDL_GetTicks();
        if(pressed&(UP|DOWN))repeat=now+350;
        if(!reading && now>repeat && (held&(UP|DOWN))){pressed|=held&(UP|DOWN);repeat=now+110;}
        if(reading) {
            if(pressed&CIRCLE){reading=false;if(page)SDL_DestroyTexture(page);page=nullptr;book.close();}
            else {
                int delta=(pressed&R)?1:((pressed&L)?-1:0);
                if(delta){int next=std::max(0,std::min((int)book.pages.size()-1,book.state.page+delta));if(next!=book.state.page){book.state.page=next;reload();}}
                if(pressed&CROSS){zoom=zoom>=3?1:zoom+0.5f;}
                if(pressed&SQUARE){fitWidth=!fitWidth;zoom=1;panX=panY=0;}
                if(pressed&TRIANGLE){book.state.rotation=(book.state.rotation+1)%4;panX=panY=0;saveState(book.path,book.state);}
                if(pressed&SELECT){book.state.rtl=!book.state.rtl;saveState(book.path,book.state);panX=0;}
                if(pressed&START)hud=!hud;
                if(held&LEFT)panX+=8;if(held&RIGHT)panX-=8;if(held&UP)panY+=8;if(held&DOWN)panY-=8;
            }
        } else {
            if(pressed&UP)selected=std::max(0,selected-1);
            if(pressed&DOWN)selected=std::min(std::max(0,(int)entries.size()-1),selected+1);
            if((pressed&CIRCLE)&&folder!=root){folder=folder.substr(0,folder.find_last_of('/'));entries=scan(folder);selected=0;notice.clear();}
            if((pressed&TRIANGLE)&&!entries.empty()) {
                const auto& e=entries[selected];auto key=imageName(e.name)?folder:e.path;auto s=readState(key);s.favorite=!s.favorite;saveState(key,s);entries=scan(folder);
            }
            if((pressed&CROSS)&&!entries.empty()) {
                const auto e=entries[selected];notice.clear();
                if(e.dir){folder=e.path;entries=scan(folder);selected=0;}
                else {
                    bool zipped=archiveName(e.name);
                    if(book.open(zipped?e.path:folder,zipped)) {
                        // Opening an image resumes the containing volume; L/R navigate pages.
                        reading=true;zoom=1;reload();
                    }
                }
            }
        }
        SDL_SetRenderDrawColor(renderer,15,20,32,255);SDL_RenderClear(renderer);
        if(reading) {
            if(page) {
                bool rotated=book.state.rotation%2;float bw=rotated?ih:iw,bh=rotated?iw:ih;
                float scale=(fitWidth?480.f/bw:std::min(480.f/bw,272.f/bh))*zoom;
                float rw=bw*scale,rh=bh*scale;
                float baseX=rw<=480?(480-rw)/2:(book.state.rtl?480-rw:0);
                float baseY=rh<=272?(272-rh)/2:0;
                float x=rw<=480?baseX:std::max(480-rw,std::min(0.f,baseX+panX));
                float y=rh<=272?baseY:std::max(272-rh,std::min(0.f,baseY+panY));
                panX=x-baseX;panY=y-baseY;
                SDL_Rect dst{(int)(x+rw/2-iw*scale/2),(int)(y+rh/2-ih*scale/2),(int)(iw*scale),(int)(ih*scale)};
                SDL_RenderCopyEx(renderer,page,nullptr,&dst,book.state.rotation*90,nullptr,SDL_FLIP_NONE);
            }
            if(hud){rect(0,0,480,23,{15,20,32,255});text(8,3,std::to_string(book.state.page+1)+" / "+std::to_string(book.pages.size())+"   "+(book.state.rtl?"Manga RTL":"Ocidental LTR"));rect(0,249,480,23,{15,20,32,255});text(7,253,"L/R paginas  X zoom  [] ajustar  /\\ girar  O voltar");}
        } else {
            rect(0,0,480,47,{25,34,53,255});text(12,6,"MANGADOCK",{98,220,192,255});
            text(12,27,folder.substr(folder.size()>63?folder.size()-63:0),{162,176,202,255});
            int start=(selected/7)*7;
            for(int i=start;i<(int)entries.size()&&i<start+7;++i){const auto&e=entries[i];int y=53+(i-start)*25;
                if(i==selected)rect(7,y,466,24,{41,68,84,255});
                std::string label=(e.favorite?"* ":"  ")+std::string(e.dir?"[+] ":"")+e.name;
                text(11,y+4,label);
            }
            if(entries.empty()){text(14,95,"Coloque CBZ/ZIP ou pastas de imagens em /MANGA.");text(14,117,"PDF, CBR, WebP e TIFF: use o importador no PC.");}
            rect(0,244,480,28,{25,34,53,255});text(9,251,"X abrir   O pasta anterior   /\\ favorito   HOME sair");
        }
        if(!notice.empty()){rect(0,218,480,26,{103,42,43,255});text(6,224,notice);}
        SDL_RenderPresent(renderer);SDL_Delay(16);
    }
    if(reading && page)saveState(book.path,book.state);
    if(page)SDL_DestroyTexture(page);book.close();TTF_CloseFont(font);TTF_Quit();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
#ifdef __PSP__
    sceKernelExitGame();
#endif
    return 0;
}
