// Fresh target raycasts run at the player's weapon-return breakpoint.
// The original game thread context is restored after query cleanup.
U rayCode=0,rayData=0,rayInitTrap=0,rayDoneTrap=0,rayCleanup=0;
bool rayPending=false;DWORD rayThread=0;CONTEXT raySaved{};
struct RayCandidate {ShotRequest request;Rank rank;};
std::vector<RayCandidate> rayWanted,rayBatch;size_t rayIndex=0,rayCursor=0;DWORD batchStarted=0;
ShotRequest rayCurrent{};bool raySetupOk=false;
std::atomic<U> activeTarget{0};std::atomic<unsigned> cameraWrites{0};float yawOffset=0;
void setCandidates(std::vector<RayCandidate> candidates){std::lock_guard<std::mutex> lock(mutex);rayWanted=std::move(candidates);}

std::atomic<unsigned> activeClear{0},activeBlocked{0},activeErrors{0};
void requestRay(U p,U e,U w,Vec eye,Vec upper,Vec point){std::lock_guard<std::mutex> lock(mutex);rayWanted={{{p,e,w,eye,upper,point,GetTickCount(),true},{0,0,0,e}}};}
template<class T> bool put(U a,const T& v){SIZE_T n=0;return WriteProcessMemory(process,(void*)a,&v,sizeof(v),&n)&&n==sizeof(v);}
bool initActive(){
    rayData=(U)VirtualAllocEx(process,nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    rayCode=(U)VirtualAllocEx(process,nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!rayData||!rayCode)return false;
    std::vector<unsigned char> code;
    auto emit=[&](std::initializer_list<unsigned char> bytes){code.insert(code.end(),bytes);};
    auto qword=[&](U q){for(int i=0;i<8;i++)code.push_back((unsigned char)(q>>(i*8)));};
    auto rcx=[&](U q){emit({0x48,0xb9});qword(q);};
    auto r11=[&](U q){emit({0x49,0xbb});qword(q);};
    auto call=[&](U q){emit({0x48,0xb8});qword(q);emit({0xff,0xd0});};
    // Entry RSP is 8 mod 16; each call has 32 bytes of shadow space.
    emit({0x48,0x83,0xec,0x28});rcx(rayData);call(base+0x1A3F80);
    r11(rayData+0x200);emit({0x49,0x8b,0x0b});call(base+0x1113230);r11(rayData);emit({0x41,0x89,0x03});
    r11(rayData+0x208);emit({0x49,0x8b,0x0b});call(base+0x1113230);r11(rayData+4);emit({0x41,0x89,0x03});
    rayInitTrap=rayCode+code.size();emit({0xcc});
    rcx(rayData);r11(rayData+0x210);emit({0x49,0x8b,0x13});
    r11(rayData+0x218);emit({0xf3,0x41,0x0f,0x10,0x13});call(base+0x1A4590);
    rayCleanup=rayCode+code.size();rcx(rayData+0xa8);call(base+0xC4AB20);
    r11(rayData+0x50);emit({0x49,0x8b,0x0b,0x48,0x85,0xc9,0x74,0x16});
    emit({0x48,0xba});qword(rayData+0x50);call(base+0x10C6B00);
    rayDoneTrap=rayCode+code.size();emit({0xcc,0x0f,0x0b});
    SIZE_T n=0;DWORD old=0;
    return WriteProcessMemory(process,(void*)rayCode,code.data(),code.size(),&n)&&n==code.size()&&VirtualProtectEx(process,(void*)rayCode,4096,PAGE_EXECUTE_READ,&old)&&FlushInstructionCache(process,(void*)rayCode,code.size());
}
bool activeInput(){
    DWORD active=0;GetWindowThreadProcessId(GetForegroundWindow(),&active);
    return ui.wall&&ui.aim&&active==pid&&(GetAsyncKeyState(VK_RBUTTON)&0x8000)&&(!ui.silent||ui.modelFacing||(GetAsyncKeyState(VK_LBUTTON)&0x8000));
}
bool nextRay(CONTEXT& c){
    while(rayIndex<rayBatch.size()){
        auto s=rayBatch[rayIndex++].request;
        if(!s.entity||s.player!=raySaved.Rdi||DWORD(GetTickCount()-s.tick)>500||value<U>(s.player+0x30)!=s.world||value<U>(s.entity+0x30)!=s.world||value<int>(s.entity+0x574)<=0)continue;
        Vec upper{},eye{},playerEye{};Ang view{};
        if(!position(s.entity,upper)||!position(s.player,playerEye)||!read(raySaved.Rsi+16,eye)||!finite(eye)||!read(s.player+0xf58,view))continue;
        Vec offset{s.point.x-s.upper.x,s.point.y-s.upper.y,s.point.z-s.upper.z};
        s.eye=eye;s.upper=upper;s.point={upper.x+offset.x,upper.y+offset.y,upper.z+offset.z};
        float distance=std::sqrt(distanceSquared(playerEye,s.point));
        if(distance<0.1f||distance>ui.distance||!insideFov(separation(view,angles(playerEye,s.point,yawOffset)),ui.fov))continue;
        MEMORY_BASIC_INFORMATION mbi{};U stack=((raySaved.Rsp-0x100)&~U(15))-8;
        if(!VirtualQueryEx(process,(void*)(stack-0x4000),&mbi,sizeof(mbi))||mbi.State!=MEM_COMMIT||(mbi.Protect&PAGE_GUARD)||(U)mbi.BaseAddress+mbi.RegionSize<stack){++activeErrors;return false;}
        if(!put(rayData+0x200,s.player)||!put(rayData+0x208,s.entity)||!put(rayData+0x210,s.world)){++activeErrors;return false;}
        rayCurrent=s;raySetupOk=false;rayPending=true;c=raySaved;c.Rip=rayCode;c.Rsp=stack;return true;
    }
    return false;
}
bool beginRay(CONTEXT& c,DWORD thread){
    if(rayPending||!rayCode||stopRequested||value<U>(c.Rdi)!=base+0x18374B0||(writesEnabled&&!activeInput()))return false;
    {std::lock_guard<std::mutex> lock(mutex);rayBatch=rayWanted;}
    if(rayBatch.empty())return false;
    ShotRotation original{};
    if(!read(c.Rsi,original))return false;
    float norm=original.x*original.x+original.y*original.y+original.z*original.z+original.w*original.w;
    if(!std::isfinite(norm)||std::abs(norm-1.f)>0.02f){++activeErrors;return false;}
    Priority priority=ui.priority.load();
    std::sort(rayBatch.begin(),rayBatch.end(),[&](const auto& a,const auto& b){return prefer(priority,a.rank,b.rank);});
    rayIndex=rayCursor<rayBatch.size()?rayCursor:0;raySaved=c;rayThread=thread;batchStarted=GetTickCount();activeTarget=0;
    return nextRay(c);
}
bool applyActive(){
    if(!writesEnabled||stopRequested||!activeInput())return false;
    const auto& s=rayCurrent;Vec upper{},eye{};
    if(value<U>(s.player+0x30)!=s.world||value<U>(s.entity+0x30)!=s.world||value<int>(s.entity+0x574)<=0||!position(s.entity,upper)||!position(s.player,eye)||distanceSquared(upper,s.upper)>0.0025f){++poseReject;return false;}
    if(ui.silent){
        updateModelFacing(s);
        if(!(GetAsyncKeyState(VK_LBUTTON)&0x8000))return true;
        ShotRotation rotation=shotRotation(angles(s.eye,s.point));
        if(put(raySaved.Rsi,rotation)){++redirected;return true;}++writeReject;return false;
    }
    Ang aim=angles(eye,s.point,yawOffset);aim.pitch=std::clamp(aim.pitch,-1.55f,1.55f);
    if(put(s.player+0xf58,aim)){++cameraWrites;return true;}++writeReject;return false;
}
bool activeTrap(DEBUG_EVENT& event){
    if(!rayPending||event.dwThreadId!=rayThread)return false;
    U trap=(U)event.u.Exception.ExceptionRecord.ExceptionAddress;
    if(trap!=rayInitTrap&&trap!=rayDoneTrap)return false;
    auto it=threads.find(rayThread);if(it==threads.end())return false;
    if(trap==rayInitTrap){
        const auto& s=rayCurrent;Vec d{s.point.x-s.eye.x,s.point.y-s.eye.y,s.point.z-s.eye.z};float len=std::sqrt(distanceSquared(s.eye,s.point));
        bool ok=resolve(value<uint32_t>(rayData))==s.player&&resolve(value<uint32_t>(rayData+4))==s.entity;
        U filter=value<U>(base+0x2282198),empty=value<U>(base+0x1FC5278);int mode=0;
        ok=ok&&put(rayData+0x10,s.eye)&&put(rayData+0x1c,s.point)&&put(rayData+0x78,d)&&put(rayData+0x218,len)&&put(rayData+0xc,mode)&&put(rayData+0x74,mode)&&put(rayData+0x88,filter)&&put(rayData+0x90,empty);
        raySetupOk=ok;
        if(!ok){++activeErrors;status("Active query setup failed");}
        // ID resolution and all writes are expected to succeed in the stopped game.
        // On setup failure skip the cast, execute destructor/release through a cleanup entry.
        CONTEXT c{};c.ContextFlags=CONTEXT_FULL|CONTEXT_DEBUG_REGISTERS;
        if(GetThreadContext(it->second.handle,&c)){c.Rip=ok?rayInitTrap+1:rayCleanup;SetThreadContext(it->second.handle,&c);}
    }else{
        unsigned v=raySetupOk?value<unsigned>(rayData+0x84):0;
        if(v==1){
            ++activeClear;activeTarget=rayCurrent.entity;
            // Publish the fresh active-ray result into the regular visibility cache.
            // Non-silent camera aim consumes this result on the main thread instead
            // of depending on a camera write from inside the debugger callback.
            Vec playerPos{},entityPos{};
            if(position(rayCurrent.player,playerPos)&&position(rayCurrent.entity,entityPos)){
                VisibilityRow r{};r.entity=rayCurrent.entity;r.player=rayCurrent.player;r.world=rayCurrent.world;
                r.tick=GetTickCount();r.visible=1;r.eye=playerPos;r.position=entityPos;r.aim=rayCurrent.point;
                std::lock_guard<std::mutex> lock(mutex);
                cache.erase(std::remove_if(cache.begin(),cache.end(),[&](const auto& old){return old.entity==r.entity||DWORD(r.tick-old.tick)>150;}),cache.end());
                if(cache.size()<256)cache.push_back(r);
            }
            if(ui.silent)applyActive();
        }else ++activeBlocked;
        if(!writesEnabled){std::ofstream log("active-ray-results.txt",std::ios::app);log<<"target "<<std::hex<<rayCurrent.entity<<std::dec<<" visible "<<v<<" clear "<<activeClear<<" blocked "<<activeBlocked<<" errors "<<activeErrors<<'\n';}
        rayPending=false;
        // Try the next priority candidate if blocked; cap each batch's elapsed time.
        CONTEXT next{};
        if(v!=1&&!stopRequested&&DWORD(GetTickCount()-batchStarted)<20&&nextRay(next)){
            next.EFlags|=0x10000;next.Dr6&=~(1ull<<it->second.shotSlot);
            if(!SetThreadContext(it->second.handle,&next)){++activeErrors;stopRequested=true;}return true;
        }
        rayCursor=v==1||rayIndex>=rayBatch.size()?0:rayIndex;
        raySaved.EFlags|=0x10000;raySaved.Dr6&=~(1ull<<it->second.shotSlot);
        if(!SetThreadContext(it->second.handle,&raySaved)){++activeErrors;stopRequested=true;}

    }
    return true;
}
void freeActive(){if(!rayPending){if(rayCode)VirtualFreeEx(process,(void*)rayCode,0,MEM_RELEASE);if(rayData)VirtualFreeEx(process,(void*)rayData,0,MEM_RELEASE);rayCode=rayData=0;}}
