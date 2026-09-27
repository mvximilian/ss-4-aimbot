#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <map>

using U = uintptr_t;
constexpr float pi=3.14159265358979323846f;
struct Vec {float x,y,z;};
struct Ang {float yaw,pitch;};
float wrap(float a) {return std::remainder(a,2*pi);}
bool finite(Vec v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::abs(v.x)<1e6&&std::abs(v.y)<1e6&&std::abs(v.z)<1e6;}
Ang angles(Vec a,Vec b,float offset=0) {
    float x=b.x-a.x,y=b.y-a.y,z=b.z-a.z;
    // Serious Engine convention: zero heading faces -Z, positive heading turns left.
    return {wrap(std::atan2(-x,-z)+offset),std::atan2(y,std::hypot(x,z))};
}
float separation(Ang a,Ang b) {
    float dot=std::sin(a.pitch)*std::sin(b.pitch)+std::cos(a.pitch)*std::cos(b.pitch)*std::cos(a.yaw-b.yaw);
    return std::acos(std::clamp(dot,-1.f,1.f));
}
#include "priority.h"
#include "ui.h"
#include "native_visibility.h"
BOOL WINAPI consoleClose(DWORD type){
    if(type==CTRL_C_EVENT||type==CTRL_BREAK_EVENT||type==CTRL_CLOSE_EVENT||type==CTRL_LOGOFF_EVENT||type==CTRL_SHUTDOWN_EVENT){ui.quit=true;visibility.stop();return TRUE;}return FALSE;
}
struct Game {
    HANDLE h=nullptr; DWORD pid=0; U base=0,player=0;
    ~Game(){close();}
    void close(){if(h)CloseHandle(h); h=nullptr;pid=0;base=player=0;}
    bool bytes(U a,void* p,size_t n)const {SIZE_T got=0;return h&&a>0x10000&&ReadProcessMemory(h,(void*)a,p,n,&got)&&got==n;}
    template<class T> T read(U a)const {T v{};bytes(a,&v,sizeof(v));return v;}
    bool valid(U p)const {
        if(read<U>(p)!=base+0x18374B0)return false;
        U w=read<U>(p+0x30); int n=read<int>(w+0x78); Ang a{};
        return read<U>(w)==base+0x1E00DA8&&n>0&&n<100000&&bytes(p+0xF58,&a,sizeof(a))&&std::isfinite(a.yaw)&&std::isfinite(a.pitch);
    }
    bool attach(bool probe) {
        close(); HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0); if(s==INVALID_HANDLE_VALUE)return false;
        PROCESSENTRY32W e{};e.dwSize=sizeof(e);
        if(Process32FirstW(s,&e))do{if(!_wcsicmp(e.szExeFile,L"Sam4.exe")){pid=e.th32ProcessID;break;}}while(Process32NextW(s,&e));
        CloseHandle(s);if(!pid)return false;
        h=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|(probe?0:PROCESS_VM_WRITE|PROCESS_VM_OPERATION),FALSE,pid);
        if(!h){close();return false;}
        s=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
        MODULEENTRY32W m{};m.dwSize=sizeof(m);
        if(s!=INVALID_HANDLE_VALUE){if(Module32FirstW(s,&m))do{if(!_wcsicmp(m.szModule,L"Sam4.exe")){base=(U)m.modBaseAddr;break;}}while(Module32NextW(s,&m));CloseHandle(s);}
        if(!base){close();return false;}return true;
    }
    bool running()const {DWORD c=0;return h&&GetExitCodeProcess(h,&c)&&c==STILL_ACTIVE;}
    bool resolve() {
        if(valid(player))return true;player=0;
        U hint=read<U>(base+0x22D7790);if(valid(hint)){player=hint;return true;}
        U w=read<U>(base+0x22F5AE0),arr=read<U>(w+0x70); int n=read<int>(w+0x78);
        std::vector<U> found;
        if(read<U>(w)==base+0x1E00DA8&&n>0&&n<100000)for(int i=0;i<n;i++){U p=read<U>(arr+i*8);if(valid(p))found.push_back(p);}
        if(found.size()==1){player=found[0];return true;}if(found.size()>1)return false;
        // Slow fallback: aligned player-vtable matches in committed writable memory.
        MEMORY_BASIC_INFORMATION mbi{};std::vector<unsigned char> buf(1024*1024);
        for(U a=0;VirtualQueryEx(h,(void*)a,&mbi,sizeof(mbi))==sizeof(mbi);){
            U start=(U)mbi.BaseAddress,end=start+mbi.RegionSize;if(end<=a)break;
            DWORD prot=mbi.Protect&0xff;
            bool writable=prot==PAGE_READWRITE||prot==PAGE_WRITECOPY||prot==PAGE_EXECUTE_READWRITE||prot==PAGE_EXECUTE_WRITECOPY;
            if(mbi.State==MEM_COMMIT&&writable&&!(mbi.Protect&PAGE_GUARD))for(U q=start;q<end;q+=buf.size()){
                if(ui.quit||(GetAsyncKeyState(VK_END)&0x8000))return false;
                size_t len=std::min<size_t>(buf.size(),end-q);
                if(!bytes(q,buf.data(),len))continue;
                for(size_t k=0;k+8<=len;k+=8){U v;std::memcpy(&v,buf.data()+k,8);if(v==base+0x18374B0&&valid(q+k))found.push_back(q+k);}
            }
            a=end;
        }
        if(found.size()==1){player=found[0];return true;}return false;
    }
    std::string label(U a)const {
        char s[96]{};if(!bytes(a,s,95))return {};s[95]=0;return s;
    }
    bool position(U t,Vec& v)const {return bytes(t+0x4C,&v,sizeof(v))&&finite(v);}
};
bool enemy(const std::string& s){
    const char* prefixes[]={"Beheaded_","Gnaar_","Processed_","Trooper_","Skeleton_","Werebull","Scrapjack","Walker_","Raider","Kamikaze","Khnum","Harpy","Arachnoid","Reptiloid","Kleer","Kalopsy","Pyro","Spider_","Technopolyp","Vampire","WitchBride"};
    for(auto p:prefixes)if(s.rfind(p,0)==0)return true;return false;
}
void catalog(Game& g){
    struct Counts {int total=0,runtime=0,health=0,upper=0;};std::map<std::string,Counts> groups;
    U w=g.read<U>(g.player+0x30),arr=g.read<U>(w+0x70);int n=g.read<int>(w+0x78);
    if(n<1||n>100000)return;
    Vec origin{};bool originOk=g.position(g.read<U>(g.player+0x220),origin);
    std::cout<<"Eye valid="<<originOk<<" XYZ="<<origin.x<<','<<origin.y<<','<<origin.z<<'\n';
    for(int i=0;i<n;i++){
        U e=g.read<U>(arr+i*8);if(g.read<U>(e+0x30)!=w)continue;
        auto s=g.label(g.read<U>(e+0xE8));
        if(s.empty()||s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)continue;
        auto& c=groups[s];c.total++;U r=g.read<U>(e+0x1D0);
        if(!r||g.read<U>(r+0x58)!=e)continue;c.runtime++;
        int hp=g.read<int>(e+0x574);if(hp<=0||hp>1000000||g.read<int>(e+0x584)!=hp)continue;c.health++;
        Vec v{};if(g.position(g.read<U>(e+0x220),v)){c.upper++;if(enemy(s))std::cout<<"Active "<<s<<" HP="<<hp<<" XYZ="<<v.x<<','<<v.y<<','<<v.z<<'\n';}
    }
    for(const auto& [s,c]:groups)std::cout<<s<<" allowed="<<enemy(s)<<" total="<<c.total<<" runtime="<<c.runtime<<" health="<<c.health<<" upper="<<c.upper<<'\n';
}
struct Target {U p=0,world=0,array=0;int count=0,hp=0;Ang aim{};float error=0;std::string cls;float distance=0;Vec point{},eye{},upper{};};
Vec adjustedAimPoint(const std::string& cls,Vec point){if(cls.rfind("Beheaded_",0)==0)point.y-=0.4f;return point;}
Target select(Game& g,float fov,float maxDistance,float offset){
    if(ui.wall)visibility.refresh(g.pid);
    Target best{};best.error=pi+1;Priority priority=ui.priority.load();
    auto failed=[](){ui.publish({});return Target{};};
    U p=g.player,w=g.read<U>(p+0x30),arr=g.read<U>(w+0x70);int n=g.read<int>(w+0x78);
    if(!g.valid(p)||n<1||n>100000)return failed();
    Vec origin{};Ang current{};
    if(!g.position(g.read<U>(p+0x220),origin)||!g.bytes(p+0xF58,&current,sizeof(current)))return failed();
    Frame frame;frame.origin=origin;frame.view=current;frame.view.yaw=wrap(current.yaw-offset);frame.valid=true;
    std::vector<U> entries(n);if(!g.bytes(arr,entries.data(),n*sizeof(U)))return failed();
    for(U e:entries){
        if(e==p||g.read<U>(e+0x30)!=w)continue;
        std::string cls=g.label(g.read<U>(e+0xE8));if(!enemy(cls))continue;
        U runtime=g.read<U>(e+0x1D0);if(!runtime||g.read<U>(runtime+0x58)!=e)continue;
        int hp=g.read<int>(e+0x574);if(hp<=0||hp>1000000||g.read<int>(e+0x584)!=hp)continue;
        Vec pos{};if(!g.position(g.read<U>(e+0x220),pos))continue;
        Vec upper=pos;
        float distance=std::sqrt((pos.x-origin.x)*(pos.x-origin.x)+(pos.y-origin.y)*(pos.y-origin.y)+(pos.z-origin.z)*(pos.z-origin.z));
        if(distance<0.1f||distance>maxDistance)continue;
        Vec feet{};
        if(g.position(g.read<U>(e+0x228),feet)||g.position(runtime,feet))frame.boxes.push_back({pos,feet,hp,distance,e});
        if(ui.wall&&!visibility.allowed(e,p,w,origin,pos,pos))continue;
        pos=adjustedAimPoint(cls,pos);
        Ang aim=angles(origin,pos,offset);float error=separation(current,aim);
        if(insideFov(error,fov)&&prefer(priority,{error,distance,hp,e},{best.error,best.distance,best.hp,best.p}))best={e,w,arr,n,hp,aim,error,cls,distance,pos,origin,upper};
    }
    if(g.read<U>(p+0x30)!=w||g.read<U>(w+0x70)!=arr||g.read<int>(w+0x78)!=n)return failed();
    frame.target=best.p;ui.publish(std::move(frame));return best;
}
bool foreground(DWORD pid){DWORD active=0;GetWindowThreadProcessId(GetForegroundWindow(),&active);return active==pid;}
int main(int argc,char** argv){
    SetProcessDPIAware();
    bool probe=false,selftest=false,uiTest=false,list=false,monitor=false;int monitorSeconds=45;float fov=40,distance=100,smooth=0,offset=0;
    try{for(int i=1;i<argc;i++){
        std::string a=argv[i];if(a=="--shot-monitor"){probe=true;monitor=true;monitorSeconds=180;}else if(a=="--wall-monitor"){probe=true;monitor=true;}else if(a=="--probe")probe=true;else if(a=="--catalog"){probe=true;list=true;}else if(a=="--self-test")selftest=true;else if(a=="--ui-test")uiTest=true;
        else if(a=="--fov"||a=="--distance"||a=="--smooth"||a=="--yaw-offset"){
            if(++i>=argc)throw std::runtime_error("Missing option value");size_t used=0;std::string value=argv[i];float v=std::stof(value,&used);
            if(used!=value.size()||!std::isfinite(v))throw std::runtime_error("Invalid option value");
            if(a=="--fov")fov=v;else if(a=="--distance")distance=v;else if(a=="--smooth")smooth=v;else offset=v;
        }else throw std::runtime_error("Options: --probe --catalog --self-test --fov degrees --distance units --smooth rate --yaw-offset degrees");
    }
    if(fov<1||fov>360||distance<1||distance>2000||smooth<0)throw std::runtime_error("FOV must be [1,360]; distance [1,2000]; smooth must be nonnegative (0 = instant)");
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
    if(selftest){
        auto approx=[](float a,float b){return std::abs(a-b)<0.0001f;};
        bool ok=approx(angles({0,0,0},{0,0,-1}).yaw,0)&&approx(angles({0,0,0},{-1,0,0}).yaw,pi/2)&&approx(angles({0,0,0},{0,1,-1}).pitch,pi/4)&&approx(wrap(-pi+0.1f-(pi-0.1f)),0.2f)&&approx(separation({0,0},{pi/2,0}),pi/2)&&enemy("Processed_Brawler")&&!enemy("PlayerBot");
        ShotRotation q=shotRotation({1.421009f,-0.098447f});
        ok=ok&&approx(q.x,-0.0372981f)&&approx(q.y,0.651426f)&&approx(q.z,0.0320915f)&&approx(q.w,0.757115f);
        auto forward=[](ShotRotation r){return Vec{-2*(r.x*r.z+r.w*r.y),2*(r.w*r.x-r.y*r.z),-(1-2*(r.x*r.x+r.y*r.y))};};
        for(Ang a: {Ang{0,0},Ang{pi,0},Ang{-pi/2,0.4f},Ang{pi/2,-0.6f}}){Vec v=forward(shotRotation(a));ok=ok&&approx(v.x,-std::sin(a.yaw)*std::cos(a.pitch))&&approx(v.y,std::sin(a.pitch))&&approx(v.z,-std::cos(a.yaw)*std::cos(a.pitch));}
        Frame f;POINT screen{};f.valid=true;
        ok=ok&&approx(adjustedAimPoint("Beheaded_Rocketeer",{1,2,3}).y,1.6f)&&approx(adjustedAimPoint("Beheaded_Kamikaze",{1,2,3}).y,1.6f)&&approx(adjustedAimPoint("Kleer",{1,2,3}).y,2.f);
        Rank nearTarget{0.4f,5,80,1},centerTarget{0.1f,20,50,2},farTarget{0.2f,50,30,3},weakTarget{0.3f,15,5,4};
        Rank ranks[]={nearTarget,centerTarget,farTarget,weakTarget};
        for(int mode=0;mode<4;mode++){Rank winner{};for(auto r:ranks)if(prefer((Priority)mode,r,winner))winner=r;ok=ok&&winner.address==U(mode+1);}
        ok=ok&&prefer(Priority::LowestHealth,{0.1f,10,5,2},{0.2f,5,5,1})&&!prefer(Priority::Closest,{0.1f,10,5,2},{0.1f,10,5,1});
        VisibilityRow vr{};vr.visible=1;vr.tick=1000;vr.aim={0,0,-10};
        ok=ok&&freshVisibility(vr,1150,{0,0,0},{0,0,0})&&!freshVisibility(vr,1151,{0,0,0},{0,0,0})&&!freshVisibility(vr,1100,{1,0,0},{0,0,0});
        vr.visible=0;ok=ok&&!freshVisibility(vr,1100,{0,0,0},{0,0,0});
        for(const char* cls:{"Kleer","Kalopsy","Pyro","Spider_Big","Spider_Small","Technopolyp","Vampire","WitchBride"})ok=ok&&enemy(cls);
        for(const char* cls:{"PlayerPuppet","PlayerBot","PathMarker022","Composite001","GenericShooter"})ok=ok&&!enemy(cls);
        ok=ok&&insideFov(pi,360)&&!insideFov(pi,359)&&insideFov(pi/2,180)&&!insideFov(pi/2,90);
        ok=ok&&project({0,0,-10},f,1920,1080,120,screen)&&screen.x==960&&screen.y==540;
        ok=ok&&!project({0,0,10},f,1920,1080,120,screen);
        ok=ok&&project({1,1,-10},f,1920,1080,120,screen)&&screen.x>960&&screen.y<540;
        f.view.yaw=pi;ok=ok&&project({0,0,10},f,1920,1080,120,screen)&&std::abs(screen.x-960)<=1;
        std::cout<<(ok?"Priority modes, tie-breaks, visibility freshness, FOV, projection and class-filter tests passed\n":"Tests failed\n");return ok?0:1;
    }
    ui.fov=fov;ui.distance=distance;UiThread menu(!probe);
    if(uiTest){Sleep(1500);bool ok=ui.ready.load()&&Menu::window&&Menu::overlay&&IsWindow(Menu::window);
        if(ok){HWND combo=GetDlgItem(Menu::window,104);ok=combo&&SendMessage(combo,CB_GETCOUNT,0,0)==4;
            for(int i=0;i<4&&ok;i++){SendMessage(combo,CB_SETCURSEL,i,0);SendMessage(Menu::window,WM_COMMAND,MAKEWPARAM(104,CBN_SELCHANGE),(LPARAM)combo);ok=ui.priority.load()==(Priority)i;}}
        std::cout<<(ok?"Menu, overlay and all four priority selections passed\n":"Menu test failed\n");return ok?0:1;}
    SetConsoleCtrlHandler(consoleClose,TRUE);
    std::cout<<"Sam4 aim: hold RIGHT MOUSE to aim; END exits. Game must be foreground.\nBuilt-in wall tracer: close Cheat Engine's debugger and previous aim apps.\n";
    Game g;if(!g.attach(probe)){std::cerr<<"Cannot open Sam4.exe. Start game; match its elevation if needed.\n";ui.message("Cannot open Sam4.exe. Start game, then restart this app.");if(!probe)while(!ui.quit)Sleep(50);return 1;}
    ui.pid=g.pid;
    if(!probe||monitor)visibility.start(g.pid,g.base,!probe);
    auto nextResolve=std::chrono::steady_clock::now();U lastTarget=0;
    while(!ui.quit&&!(GetAsyncKeyState(VK_END)&0x8000)){
        if(!g.running()){std::cout<<"Game exited. Restart this program after restarting the game.\n";break;}
        auto now=std::chrono::steady_clock::now();
        if(!g.valid(g.player)){
            if(now<nextResolve){Sleep(50);continue;}
            ui.publish({});ui.message("Resolving player - load gameplay...");
            std::cout<<"Resolving player...\n";bool ok=g.resolve();nextResolve=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            if(!ok){if(probe){std::cerr<<"No unique player found\n";return 1;}continue;}
            std::cout<<"Player 0x"<<std::hex<<g.player<<std::dec<<"; PID "<<g.pid<<'\n';
        }
        if(!probe&&!foreground(g.pid)){visibility.clearSilent();ui.publish({});ui.message("Ready - return to game. Hold right mouse to aim.");Sleep(20);continue;}
        if(list){catalog(g);return 0;}
        if(monitor){
            auto until=std::chrono::steady_clock::now()+std::chrono::seconds(monitorSeconds);auto report=std::chrono::steady_clock::now();int accepted=0,rejected=0;U last=~U(0);
            while(std::chrono::steady_clock::now()<until&&g.running()&&!ui.quit){
                Target sample=select(g,360,2000,offset*pi/180);
                if(sample.p)accepted++;else rejected++;
                if(sample.p!=last){std::cout<<(sample.p?"CLEAR: "+sample.cls:"BLOCKED / UNKNOWN")<<std::endl;last=sample.p;}
                if(monitorSeconds>45&&std::chrono::steady_clock::now()>=report){std::cout<<"Trace counts: rays="<<visibility.hitCount<<" shot-points="<<visibility.shotHits<<" player="<<visibility.playerShotHits<<std::endl;report=std::chrono::steady_clock::now()+std::chrono::seconds(10);}
                Sleep(20);
            }
            std::cout<<"Visibility samples: accepted="<<accepted<<" rejected="<<rejected<<" ray hits="<<visibility.hitCount<<" clear="<<visibility.clearCount<<" blocked="<<visibility.blockedCount<<" shot-point hits="<<visibility.shotHits<<" player shot-point hits="<<visibility.playerShotHits<<" status="<<visibility.status()<<'\n';return 0;
        }
        Target t=select(g,ui.fov,ui.distance,offset*pi/180);
        if(!probe&&ui.aim&&ui.silent&&t.p)visibility.setSilent(g.player,t.p,t.world,t.eye,t.upper,t.point,ui.wall);
        else visibility.clearSilent();
        {auto tracerStatus=visibility.status();std::lock_guard<std::mutex> lock(ui.mutex);ui.status="ESP: "+std::to_string(ui.frame.boxes.size())+(t.p?" | Target: "+t.cls:(ui.wall?" | No fresh clear ray":" | No target inside aim FOV"))+"\r\n"+tracerStatus+" | Redirects: "+std::to_string(visibility.redirected.load());}
        if(probe){
            std::cout<<"Player HP "<<g.read<int>(g.player+0x574)<<"; entity count "<<g.read<int>(g.read<U>(g.player+0x30)+0x78)<<'\n';
            if(t.p)std::cout<<"Target 0x"<<std::hex<<t.p<<std::dec<<' '<<t.cls<<" HP "<<t.hp<<" error "<<t.error*180/pi<<" deg; desired yaw "<<t.aim.yaw<<" pitch "<<t.aim.pitch<<'\n';
            else std::cout<<"No eligible target inside FOV/range\n";
            return 0;
        }
        if(ui.silent){Sleep(10);continue;}
        if(ui.aim&&t.p&&foreground(g.pid)&&(GetAsyncKeyState(VK_RBUTTON)&0x8000)&&g.valid(g.player)&&g.read<U>(g.player+0x30)==t.world&&g.read<U>(t.world+0x70)==t.array&&g.read<int>(t.world+0x78)==t.count&&g.read<U>(t.p+0x30)==t.world&&g.read<int>(t.p+0x574)>0){
            if(ui.wall){Vec eye{},pos{},aim{};if(!g.position(g.read<U>(g.player+0x220),eye)||!g.position(g.read<U>(t.p+0x220),pos)||!visibility.allowed(t.p,g.player,t.world,eye,pos,aim)){Sleep(10);continue;}}
            Ang a{};if(g.bytes(g.player+0xF58,&a,sizeof(a))){
                if(smooth==0){
                    a.yaw=t.aim.yaw;
                    a.pitch=std::clamp(t.aim.pitch,-1.55f,1.55f);
                }else{
                    float alpha=1-std::exp(-smooth*0.01f);
                    a.yaw=wrap(a.yaw+wrap(t.aim.yaw-a.yaw)*alpha);
                    a.pitch=std::clamp(a.pitch+(t.aim.pitch-a.pitch)*alpha,-1.55f,1.55f);
                }
                SIZE_T written=0;
                if(!WriteProcessMemory(g.h,(void*)(g.player+0xF58),&a,sizeof(a),&written)||written!=sizeof(a)){std::cerr<<"Camera write failed; stopping\n";return 1;}
                if(t.p!=lastTarget){std::cout<<"Target: "<<t.cls<<" HP "<<t.hp<<'\n';lastTarget=t.p;}
            }
        }
        Sleep(10);
    }
    return 0;
}


