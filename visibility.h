#pragma once
#pragma pack(push,1)
struct VisibilityHeader {uint32_t magic,pid,count;};
struct VisibilityRow {uint64_t entity,player,world;uint32_t tick,visible;Vec eye,position,aim;};
#pragma pack(pop)
static_assert(sizeof(VisibilityRow)==68);
bool freshVisibility(const VisibilityRow& r,DWORD now,Vec eye,Vec position){
    auto dist=[](Vec a,Vec b){return (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z);};
    return r.visible==1&&DWORD(now-r.tick)<=150&&finite(r.aim)&&finite(r.eye)&&finite(r.position)&&dist(eye,r.eye)<=0.0025f&&dist(position,r.position)<=0.0025f;
}
struct Visibility {
    std::vector<VisibilityRow> rows;
    void refresh(DWORD pid){
        rows.clear();wchar_t tmp[MAX_PATH]{};DWORD len=GetTempPathW(MAX_PATH,tmp);if(!len||len>=MAX_PATH)return;
        std::wstring path=std::wstring(tmp)+L"Sam4Visibility-"+std::to_wstring(pid)+L".bin";
        HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE)return;
        struct Close {HANDLE h;~Close(){CloseHandle(h);}} close{f};
        VisibilityHeader h{};DWORD got=0;
        if(!ReadFile(f,&h,sizeof(h),&got,nullptr)||got!=sizeof(h)||h.magic!=0x31565353||h.pid!=pid||h.count>256)return;
        LARGE_INTEGER size{};if(!GetFileSizeEx(f,&size)||size.QuadPart!=static_cast<LONGLONG>(sizeof(h)+h.count*sizeof(VisibilityRow)))return;
        std::vector<VisibilityRow> data(h.count);
        DWORD bytes=h.count*sizeof(VisibilityRow);
        if(bytes&&(!ReadFile(f,data.data(),bytes,&got,nullptr)||got!=bytes))return;
        rows=std::move(data);
    }
    bool allowed(U entity,U player,U world,Vec eye,Vec position,Vec& aim)const{
        for(const auto& r:rows)if(r.entity==entity&&r.player==player&&r.world==world&&freshVisibility(r,GetTickCount(),eye,position)){aim=r.aim;return true;}
        return false;
    }
} visibility;
