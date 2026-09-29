// Reflected fields: en_bForcingDesiredFront (+C68), en_vForcedDesiredFrontEul (+C6C).
// The engine consumes these in its body-heading update at RVA 27804B.
U modelPlayer=0;Vec modelOriginal{},modelLast{};int modelOriginalFlag=0;DWORD modelTick=0;
ShotRequest modelRequest{};
std::atomic<U> localBodyPlayer{0};
ULONGLONG spinStarted=0;float spinStartHeading=0;
// Visual facing may bridge short query gaps; shot redirection still requires
// its own fresh ray. Revalidate the retained entity on every body update.
static constexpr DWORD modelHoldMs=500;
bool validModelRequest(){
    const auto& s=modelRequest;Vec eye{},upper{};Ang view{};
    if(!s.entity||s.player!=modelPlayer||DWORD(GetTickCount()-modelTick)>modelHoldMs||
       value<U>(s.player+0x30)!=s.world||value<U>(s.entity+0x30)!=s.world||value<int>(s.entity+0x574)<=0||
       !position(s.player,eye)||!position(s.entity,upper)||!read(s.player+0xf58,view))return false;
    Vec point{upper.x+s.point.x-s.upper.x,upper.y+s.point.y-s.upper.y,upper.z+s.point.z-s.upper.z};
    if(distanceSquared(eye,point)>ui.distance*ui.distance||!insideFov(separation(view,angles(eye,point)),ui.fov))return false;
    if(s.wall&&(distanceSquared(eye,s.eye)>0.25f||distanceSquared(upper,s.upper)>0.25f))return false;
    return true;
}
std::atomic<unsigned> modelWrites{0},modelFailures{0},modelObserved{0},modelAligned{0},modelSeparate{0};
void restoreModelFacing(){
    if(!modelPlayer)return;
    Vec current{};int flag=0;
    if(value<U>(modelPlayer)==base+0x18374B0&&read(modelPlayer+0xc6c,current)&&read(modelPlayer+0xc68,flag)&&flag==1&&std::memcmp(&current,&modelLast,sizeof(current))==0){
        SIZE_T n=0;
        WriteProcessMemory(process,(void*)(modelPlayer+0xc68),&modelOriginalFlag,sizeof(modelOriginalFlag),&n);
        WriteProcessMemory(process,(void*)(modelPlayer+0xc6c),&modelOriginal,sizeof(modelOriginal),&n);
    }
    modelPlayer=0;modelRequest={};
}
void updateModelFacing(const ShotRequest& request){
    U player=request.player;Vec target=request.point;
    if(ui.antiAim||!ui.modelFacing||!ui.silent||!writesEnabled)return;
    Vec eye{},current{};int flag=0;
    if(value<U>(player)!=base+0x18374B0||!position(player,eye)||!read(player+0xc6c,current)||!finite(current)||!read(player+0xc68,flag)||(flag!=0&&flag!=1)){++modelFailures;return;}
    if(modelPlayer!=player){restoreModelFacing();modelPlayer=player;modelOriginal=current;modelOriginalFlag=flag;}
    else if(flag!=1||std::memcmp(&current,&modelLast,sizeof(current))!=0){modelOriginal=current;modelOriginalFlag=flag;}
    float body=0;Ang camera{};
    if(modelTick&&DWORD(GetTickCount()-modelTick)<=150&&read(player+0xc48,body)&&read(player+0xf58,camera)&&std::isfinite(body)&&std::isfinite(camera.yaw)){
        ++modelObserved;
        if(std::abs(wrap(body-modelLast.x))<0.15f){++modelAligned;if(std::abs(wrap(body-camera.yaw))>0.25f)++modelSeparate;}
    }
    Ang aim=angles(eye,target);Vec next{aim.yaw,0,0};int enabled=1;SIZE_T n=0;
    if(WriteProcessMemory(process,(void*)(player+0xc6c),&next,sizeof(next),&n)&&n==sizeof(next)){
        modelLast=next;
        if(WriteProcessMemory(process,(void*)(player+0xc68),&enabled,sizeof(enabled),&n)&&n==sizeof(enabled)){modelRequest=request;position(player,modelRequest.eye);modelTick=GetTickCount();++modelWrites;return;}
        WriteProcessMemory(process,(void*)(player+0xc6c),&modelOriginal,sizeof(modelOriginal),&n);
    }
    ++modelFailures;
}
void serviceModelFacing(){
    DWORD active=0;GetWindowThreadProcessId(GetForegroundWindow(),&active);
    if(!ui.antiAim||active!=pid)spinStarted=0;
    if(ui.antiAim){restoreModelFacing();return;}
    if(!ui.modelFacing||!ui.silent||!ui.aim||active!=pid||!(GetAsyncKeyState(VK_RBUTTON)&0x8000)||!validModelRequest())restoreModelFacing();
}

std::atomic<unsigned> bodyBuildHits{0},bodyBuildOverrides{0};
void captureBodyBuild(CONTEXT& c){
    if(value<U>(c.Rsi)!=base+0x18374B0)return;
    ++bodyBuildHits;
    DWORD active=0;GetWindowThreadProcessId(GetForegroundWindow(),&active);
    if(ui.antiAim&&writesEnabled&&!stopRequested&&active==pid&&c.Rsi==localBodyPlayer.load()){
        if(!spinStarted){spinStarted=GetTickCount64();std::memcpy(&spinStartHeading,&c.Xmm0,sizeof(float));}
        // Integrate elapsed time so changing speed preserves the current heading.
        ULONGLONG now=GetTickCount64();
        float elapsed=float(now-spinStarted)/1000.f;spinStarted=now;
        spinStartHeading=wrap(spinStartHeading+elapsed*ui.spinSpeed.load()*pi/180.f);
        float heading=spinStartHeading;
        std::memcpy(&c.Xmm0,&heading,sizeof(heading));
        SIZE_T n=0;WriteProcessMemory(process,(void*)(c.Rsi+0xc48),&heading,sizeof(heading),&n);
        ++bodyBuildOverrides;return;
    }
    if(c.Rsi!=modelPlayer||!writesEnabled||stopRequested||!ui.modelFacing||!ui.silent||!ui.aim||active!=pid||!(GetAsyncKeyState(VK_RBUTTON)&0x8000)||!validModelRequest())return;
    // The following game instruction stores XMM0 as the heading used to build
    // the character rotation. Override that input, not camera-input angles.
    float heading=modelLast.x;
    std::memcpy(&c.Xmm0,&heading,sizeof(heading));
    SIZE_T n=0;WriteProcessMemory(process,(void*)(modelPlayer+0xc48),&heading,sizeof(heading),&n);
    ++bodyBuildOverrides;
}
