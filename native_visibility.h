#pragma once
struct ShotRotation {float x,y,z,w;};
ShotRotation shotRotation(Ang a){
    float sy=std::sin(a.yaw/2),cy=std::cos(a.yaw/2),sp=std::sin(a.pitch/2),cp=std::cos(a.pitch/2);
    return {sp*cy,cp*sy,-sp*sy,cp*cy};
}
struct VisibilityRow {uint64_t entity=0,player=0,world=0;uint32_t tick=0,visible=0;Vec eye{},position{},aim{};};
inline float distanceSquared(Vec a,Vec b){return (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z);}
bool freshVisibility(const VisibilityRow& r,DWORD now,Vec eye,Vec position){return r.visible==1&&DWORD(now-r.tick)<=150&&finite(r.aim)&&finite(r.eye)&&finite(r.position)&&distanceSquared(eye,r.eye)<=0.0025f&&distanceSquared(position,r.position)<=0.0025f;}
// Native Windows hardware-breakpoint tracer. No CE, DLL or game-code patch.
struct Visibility {
    struct Thread {HANDLE handle;int slot;DWORD64 savedDr,savedBits;int shotSlot=-1;DWORD64 savedShotDr=0,savedShotBits=0;};
    std::thread worker;std::atomic<bool> stopRequested{false},attached{false};
    std::atomic<unsigned> clearCount{0},blockedCount{0},hitCount{0};
    std::atomic<unsigned> shotHits{0},playerShotHits{0};
    std::atomic<unsigned> redirected{0};bool writesEnabled=false;
    struct ShotRequest {U player=0,entity=0,world=0;Vec eye{},upper{},point{};DWORD tick=0;bool wall=true;};
    ShotRequest shot;
    void setSilent(U p,U e,U w,Vec eye,Vec upper,Vec point,bool wall){std::lock_guard<std::mutex> lock(mutex);shot={p,e,w,eye,upper,point,GetTickCount(),wall};}
    void clearSilent(){std::lock_guard<std::mutex> lock(mutex);shot={};}
    void captureShot(const CONTEXT& c){
        ++shotHits;if(value<U>(c.Rdi)!=base+0x18374B0)return;++playerShotHits;
        if(!writesEnabled||!ui.aim||!ui.silent||!(GetAsyncKeyState(VK_RBUTTON)&0x8000)||!(GetAsyncKeyState(VK_LBUTTON)&0x8000))return;
        DWORD active=0;GetWindowThreadProcessId(GetForegroundWindow(),&active);if(active!=pid)return;
        ShotRequest s;bool clearRay=false;
        {std::lock_guard<std::mutex> lock(mutex);s=shot;if(s.wall)for(const auto& r:cache)if(r.entity==s.entity&&r.player==s.player&&r.world==s.world&&freshVisibility(r,GetTickCount(),s.eye,s.upper)){clearRay=true;break;}}
        if(!s.entity||s.player!=c.Rdi||DWORD(GetTickCount()-s.tick)>50||!finite(s.point)||(s.wall&&!clearRay))return;
        Vec eye{},upper{};struct Placement {ShotRotation rotation;Vec origin;};Placement original{};
        if(value<U>(s.player+0x30)!=s.world||value<U>(s.entity+0x30)!=s.world||value<int>(s.entity+0x574)<=0||!position(s.player,eye)||!position(s.entity,upper))return;
        if(distanceSquared(eye,s.eye)>0.0025f||distanceSquared(upper,s.upper)>0.0025f||!read(c.Rsi,original)||!finite(original.origin))return;
        float norm=original.rotation.x*original.rotation.x+original.rotation.y*original.rotation.y+original.rotation.z*original.rotation.z+original.rotation.w*original.rotation.w;
        if(!std::isfinite(norm)||std::abs(norm-1.f)>0.02f||distanceSquared(original.origin,eye)>9.f)return;
        ShotRotation rotation=shotRotation(angles(original.origin,s.point));
        SIZE_T n=0;if(WriteProcessMemory(process,(void*)c.Rsi,&rotation,sizeof(rotation),&n)&&n==sizeof(rotation))++redirected;
    }
    std::mutex mutex,shutdownMutex;std::vector<VisibilityRow> cache,rows;std::string message="Tracer not started";
    DWORD pid=0;U base=0,address=0;HANDLE process=nullptr;std::map<DWORD,Thread> threads;
    ~Visibility(){stop();}
    std::string status(){std::lock_guard<std::mutex> lock(mutex);return message;}
    void status(std::string s){std::lock_guard<std::mutex> lock(mutex);message=std::move(s);}
    void clear(){std::lock_guard<std::mutex> lock(mutex);cache.clear();}
    template<class T> bool read(U a,T& v){SIZE_T n=0;return a>0x10000&&ReadProcessMemory(process,(void*)a,&v,sizeof(v),&n)&&n==sizeof(v);}
    template<class T> T value(U a){T v{};read(a,v);return v;}
    bool position(U e,Vec& v){U p=value<U>(e+0x220);return p&&read(p+0x4C,v)&&finite(v);}
    U resolve(uint32_t id){
        if(!id)return 0;U table=value<U>(base+0x239C278);if(!table)return 0;
        U slot=table+U(id&0xffffff)*24;uint16_t gen=0;if(!read(slot,gen)||gen!=id>>24)return 0;return value<U>(slot+8);
    }
    void capture(U ray){
        ++hitCount;uint32_t a=0,b=0,visible=0;Vec from{},to{};
        if(!read(ray,a)||!read(ray+4,b)||!read(ray+0x84,visible)||!read(ray+0x10,from)||!read(ray+0x1C,to)||!finite(from)||!finite(to))return;
        U viewer=resolve(a),target=resolve(b);if(!viewer||!target)return;
        VisibilityRow r;Vec endpoint{};
        if(value<U>(viewer)==base+0x18374B0){r.player=viewer;r.entity=target;endpoint=from;r.aim=to;}
        else if(value<U>(target)==base+0x18374B0){r.player=target;r.entity=viewer;endpoint=to;r.aim=from;}else return;
        r.world=value<U>(r.player+0x30);
        if(!r.world||value<U>(r.entity+0x30)!=r.world||!position(r.player,r.eye)||!position(r.entity,r.position))return;
        r.tick=GetTickCount();r.visible=visible==1&&distanceSquared(endpoint,r.eye)<=0.0025f&&distanceSquared(r.position,r.aim)<=4.f;
        if(r.visible)++clearCount;else ++blockedCount;
        std::lock_guard<std::mutex> lock(mutex);
        cache.erase(std::remove_if(cache.begin(),cache.end(),[&](const auto& v){return v.entity==r.entity||DWORD(r.tick-v.tick)>150;}),cache.end());
        if(cache.size()<256)cache.push_back(r);
    }
    static DWORD64& dr(CONTEXT& c,int i){switch(i){case 0:return c.Dr0;case 1:return c.Dr1;case 2:return c.Dr2;default:return c.Dr3;}}
    bool arm(DWORD id,HANDLE h){
        CONTEXT c{};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;if(!GetThreadContext(h,&c))return false;
        for(int i=0;i<4;i++)if(!(c.Dr7&(3ull<<(i*2)))){
            DWORD64 mask=(3ull<<(i*2))|(15ull<<(16+i*4));Thread t{h,i,dr(c,i),c.Dr7&mask};
            dr(c,i)=address;c.Dr7=(c.Dr7&~mask)|(1ull<<(i*2));
            for(int j=0;j<4;j++)if(j!=i&&!(c.Dr7&(3ull<<(j*2)))){
                DWORD64 m=(3ull<<(j*2))|(15ull<<(16+j*4));t.shotSlot=j;t.savedShotDr=dr(c,j);t.savedShotBits=c.Dr7&m;
                dr(c,j)=base+0x2D5FB7;c.Dr7=(c.Dr7&~m)|(1ull<<(j*2));break;
            }
            if(t.shotSlot<0)return false;
            if(!SetThreadContext(h,&c))return false;threads[id]=t;return true;
        }return false;
    }
    void restore(){
        for(auto& [id,t]:threads){
            (void)id;if(SuspendThread(t.handle)!=DWORD(-1)){
                CONTEXT c{};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;
                if(GetThreadContext(t.handle,&c)&&dr(c,t.slot)==address){
                    DWORD64 mask=(3ull<<(t.slot*2))|(15ull<<(16+t.slot*4));
                    dr(c,t.slot)=t.savedDr;c.Dr7=(c.Dr7&~mask)|t.savedBits;c.Dr6&=~(1ull<<t.slot);
                    if(t.shotSlot>=0&&dr(c,t.shotSlot)==base+0x2D5FB7){DWORD64 m=(3ull<<(t.shotSlot*2))|(15ull<<(16+t.shotSlot*4));dr(c,t.shotSlot)=t.savedShotDr;c.Dr7=(c.Dr7&~m)|t.savedShotBits;c.Dr6&=~(1ull<<t.shotSlot);}
                    SetThreadContext(t.handle,&c);
                }ResumeThread(t.handle);
            }CloseHandle(t.handle);
        }threads.clear();
    }
    void run(){
        process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|(writesEnabled?PROCESS_VM_WRITE|PROCESS_VM_OPERATION:0),FALSE,pid);
        if(!process){status("Cannot read game for wall tracer");return;}
        unsigned char bytes[10]{};const unsigned char expected[]={0x0f,0x28,0x74,0x24,0x50,0x4c,0x8d,0x5c,0x24,0x60};
        if(!read(address,bytes)||std::memcmp(bytes,expected,sizeof(bytes))){status("Unsupported raycast signature");CloseHandle(process);process=nullptr;return;}
        unsigned char shotBytes[8]{};const unsigned char shotExpected[]={0x4c,0x8d,0x9c,0x24,0x90,1,0,0};
        if(!read(base+0x2D5FB7,shotBytes)||std::memcmp(shotBytes,shotExpected,8)){status("Unsupported weapon rotation signature");CloseHandle(process);process=nullptr;return;}
        if(!DebugActiveProcess(pid)){status("Wall tracer attach failed ("+std::to_string(GetLastError())+"); close other debuggers");CloseHandle(process);process=nullptr;return;}
        DebugSetProcessKillOnExit(FALSE);attached=true;status("Native wall tracer active");bool initial=true,exited=false,failed=false;
        while(!stopRequested){
            DEBUG_EVENT e{};if(!WaitForDebugEvent(&e,50)){if(GetLastError()==ERROR_SEM_TIMEOUT)continue;status("Wall tracer event error");break;}
            DWORD continuation=DBG_CONTINUE;
            switch(e.dwDebugEventCode){
            case CREATE_PROCESS_DEBUG_EVENT:
                if(e.u.CreateProcessInfo.hFile)CloseHandle(e.u.CreateProcessInfo.hFile);
                if(e.u.CreateProcessInfo.hProcess)CloseHandle(e.u.CreateProcessInfo.hProcess);
                if(!arm(e.dwThreadId,e.u.CreateProcessInfo.hThread)){CloseHandle(e.u.CreateProcessInfo.hThread);failed=true;}break;
            case CREATE_THREAD_DEBUG_EVENT:
                if(!arm(e.dwThreadId,e.u.CreateThread.hThread)){CloseHandle(e.u.CreateThread.hThread);failed=true;}break;
            case EXIT_THREAD_DEBUG_EVENT:{auto it=threads.find(e.dwThreadId);if(it!=threads.end()){CloseHandle(it->second.handle);threads.erase(it);}break;}
            case LOAD_DLL_DEBUG_EVENT:if(e.u.LoadDll.hFile)CloseHandle(e.u.LoadDll.hFile);break;
            case EXCEPTION_DEBUG_EVENT:{
                continuation=DBG_EXCEPTION_NOT_HANDLED;DWORD code=e.u.Exception.ExceptionRecord.ExceptionCode;
                if(code==EXCEPTION_BREAKPOINT&&initial){initial=false;continuation=DBG_CONTINUE;}
                else if(code==EXCEPTION_SINGLE_STEP){auto it=threads.find(e.dwThreadId);if(it!=threads.end()){
                    CONTEXT c{};c.ContextFlags=CONTEXT_FULL|CONTEXT_DEBUG_REGISTERS;
                    if(GetThreadContext(it->second.handle,&c)&&((c.Rip==address&&(c.Dr6&(1ull<<it->second.slot)))||(c.Rip==base+0x2D5FB7&&(c.Dr6&(1ull<<it->second.shotSlot))))){
                        int slot=it->second.slot;
                        if(c.Rip==address)capture(c.Rbx);else{slot=it->second.shotSlot;captureShot(c);}
                        c.Dr6&=~(1ull<<slot);c.EFlags|=0x10000;
                        if(SetThreadContext(it->second.handle,&c))continuation=DBG_CONTINUE;else failed=true;
                    }
                }}break;
            }
            case EXIT_PROCESS_DEBUG_EVENT:exited=true;break;
            }
            ContinueDebugEvent(e.dwProcessId,e.dwThreadId,continuation);if(exited||failed)break;
        }
        restore();if(!exited)DebugActiveProcessStop(pid);attached=false;clear();
        if(failed)status("Could not arm game threads; wall tracer stopped");else if(exited)status("Game exited");
        CloseHandle(process);process=nullptr;
    }
    void start(DWORD p,U b,bool writes=false){stop();pid=p;base=b;writesEnabled=writes;address=b+0x1A47C4;stopRequested=false;worker=std::thread([this]{run();});}
    void stop(){std::lock_guard<std::mutex> lock(shutdownMutex);stopRequested=true;if(worker.joinable())worker.join();clear();}
    void refresh(DWORD p){std::lock_guard<std::mutex> lock(mutex);rows=(p==pid&&attached)?cache:std::vector<VisibilityRow>{};}
    bool allowed(U entity,U player,U world,Vec eye,Vec position,Vec& aim)const{
        for(const auto& r:rows)if(r.entity==entity&&r.player==player&&r.world==world&&freshVisibility(r,GetTickCount(),eye,position)){aim=r.aim;return true;}return false;
    }
} visibility;
