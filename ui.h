#pragma once
#include <commctrl.h>
#include <atomic>
#include <mutex>
#include <sstream>
#include <iomanip>

struct BoxEntity {Vec top{},feet{};int hp=0;float distance=0;U address=0;};
struct Frame {Vec origin{};Ang view{};std::vector<BoxEntity> boxes;U target=0;bool valid=false;};
struct Interface {
    std::atomic<float> fov{40},distance{100},cameraFov{120};
    std::atomic<bool> aim{true},esp{true},wall{true},silent{false},quit{false},ready{false};
    std::atomic<DWORD> pid{0};
    std::atomic<Priority> priority{Priority::Fov};
    std::mutex mutex;Frame frame;std::string status="Waiting for game";
    void publish(Frame f){std::lock_guard<std::mutex> lock(mutex);frame=std::move(f);}
    void message(std::string s){std::lock_guard<std::mutex> lock(mutex);status=std::move(s);}
} ui;

// Full cone width: 360 accepts every direction, including exactly behind.
bool insideFov(float error,float degrees){return degrees>=360.f||error<=degrees*pi/360.f;}
bool project(Vec p,const Frame& f,int width,int height,float horizontalFov,POINT& out){
    if(width<=0||height<=0||horizontalFov<30||horizontalFov>150)return false;
    float sy=std::sin(f.view.yaw),cy=std::cos(f.view.yaw),sp=std::sin(f.view.pitch),cp=std::cos(f.view.pitch);
    Vec d{p.x-f.origin.x,p.y-f.origin.y,p.z-f.origin.z};
    float right=d.x*cy-d.z*sy;
    float up=d.x*sy*sp+d.y*cp+d.z*cy*sp;
    float depth=-d.x*sy*cp+d.y*sp-d.z*cy*cp;
    if(depth<=0.05f)return false;
    float focal=width/(2.f*std::tan(horizontalFov*pi/360.f));
    float x=width/2.f+right*focal/depth,y=height/2.f-up*focal/depth;
    if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>100000||std::abs(y)>100000)return false;
    out={static_cast<LONG>(x),static_cast<LONG>(y)};return true;
}
namespace Menu {
HWND window=nullptr,overlay=nullptr,fovLabel=nullptr,distanceLabel=nullptr,cameraLabel=nullptr,statusLabel=nullptr;
HWND fovSlider=nullptr,distanceSlider=nullptr,cameraSlider=nullptr;
HFONT font=nullptr;
void labels(){
    std::string a="Aim FOV: "+std::to_string((int)ui.fov.load())+" degrees (360 = all directions)";
    SetWindowTextA(fovLabel,a.c_str());
    a="Maximum distance: "+std::to_string((int)ui.distance.load())+" world units";SetWindowTextA(distanceLabel,a.c_str());
    a="ESP camera horizontal FOV: "+std::to_string((int)ui.cameraFov.load())+" degrees";SetWindowTextA(cameraLabel,a.c_str());
}
HWND child(const char* cls,const char* text,DWORD style,int x,int y,int w,int h,int id=0){
    HWND c=CreateWindowExA(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,(HMENU)(INT_PTR)id,GetModuleHandle(nullptr),nullptr);
    SendMessage(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
}
HWND slider(int y,int lo,int hi,int value,int id){
    HWND c=child(TRACKBAR_CLASSA,"",TBS_AUTOTICKS|WS_TABSTOP,16,y,454,32,id);
    SendMessage(c,TBM_SETRANGE,TRUE,MAKELPARAM(lo,hi));SendMessage(c,TBM_SETPOS,TRUE,value);return c;
}
BOOL CALLBACK findWindow(HWND w,LPARAM result){
    DWORD pid=0;GetWindowThreadProcessId(w,&pid);
    if(pid==ui.pid.load()&&IsWindowVisible(w)&&!GetWindow(w,GW_OWNER)){*(HWND*)result=w;return FALSE;}return TRUE;
}
LRESULT CALLBACK paint(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_NCHITTEST)return HTTRANSPARENT;
    if(msg==WM_ERASEBKGND)return 1;
    if(msg!=WM_PAINT)return DefWindowProc(w,msg,wp,lp);
    PAINTSTRUCT ps{};HDC dc=BeginPaint(w,&ps);RECT rc{};GetClientRect(w,&rc);
    HDC mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,std::max(1L,rc.right),std::max(1L,rc.bottom));
    HGDIOBJ oldBitmap=SelectObject(mem,bitmap);FillRect(mem,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
    Frame f;{std::lock_guard<std::mutex> lock(ui.mutex);f=ui.frame;}
    if(f.valid&&ui.esp){
        HGDIOBJ oldBrush=SelectObject(mem,GetStockObject(HOLLOW_BRUSH));HGDIOBJ oldFont=SelectObject(mem,font);
        SetBkMode(mem,TRANSPARENT);
        for(const auto& e:f.boxes){
            POINT top{},feet{};
            if(!project(e.top,f,rc.right,rc.bottom,ui.cameraFov,top)||!project(e.feet,f,rc.right,rc.bottom,ui.cameraFov,feet))continue;
            int h=std::abs(feet.y-top.y);if(h<3)continue;
            int center=(top.x+feet.x)/2,half=std::max(3,(int)(h*0.24f));
            int y=std::min(top.y,feet.y),bottom=std::max(top.y,feet.y);
            if(center+half<0||center-half>rc.right||bottom<0||y>rc.bottom)continue;
            COLORREF color=e.address==f.target?RGB(255,210,50):RGB(70,255,140);
            HPEN pen=CreatePen(PS_SOLID,2,color);HGDIOBJ oldPen=SelectObject(mem,pen);
            Rectangle(mem,center-half,y,center+half,bottom);
            std::string label=std::to_string(e.hp)+" HP | "+std::to_string((int)e.distance)+" u";
            SetTextColor(mem,color);TextOutA(mem,center-half,std::max(0,y-18),label.c_str(),(int)label.size());
            SelectObject(mem,oldPen);DeleteObject(pen);
        }
        SelectObject(mem,oldFont);SelectObject(mem,oldBrush);
    }
    BitBlt(dc,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY);SelectObject(mem,oldBitmap);DeleteObject(bitmap);DeleteDC(mem);EndPaint(w,&ps);return 0;
}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_HSCROLL){
        ui.fov=(float)SendMessage(fovSlider,TBM_GETPOS,0,0);ui.distance=(float)SendMessage(distanceSlider,TBM_GETPOS,0,0);ui.cameraFov=(float)SendMessage(cameraSlider,TBM_GETPOS,0,0);labels();return 0;
    }
    if(msg==WM_COMMAND){
        if(LOWORD(wp)==101)ui.aim=SendMessage((HWND)lp,BM_GETCHECK,0,0)==BST_CHECKED;
        if(LOWORD(wp)==102)ui.esp=SendMessage((HWND)lp,BM_GETCHECK,0,0)==BST_CHECKED;
        if(LOWORD(wp)==103)ui.wall=SendMessage((HWND)lp,BM_GETCHECK,0,0)==BST_CHECKED;
        if(LOWORD(wp)==105)ui.silent=SendMessage((HWND)lp,BM_GETCHECK,0,0)==BST_CHECKED;
        if(LOWORD(wp)==104&&HIWORD(wp)==CBN_SELCHANGE){LRESULT index=SendMessage((HWND)lp,CB_GETCURSEL,0,0);if(index>=0&&index<4)ui.priority=(Priority)index;}
        return 0;
    }
    if(msg==WM_TIMER){
        if(ui.quit||(GetAsyncKeyState(VK_END)&0x8000)){DestroyWindow(w);return 0;}
        static bool insertDown=false;bool down=(GetAsyncKeyState(VK_INSERT)&0x8000)!=0;
        if(down&&!insertDown)ShowWindow(w,IsWindowVisible(w)?SW_HIDE:SW_SHOWNOACTIVATE);insertDown=down;
        std::string status;{std::lock_guard<std::mutex> lock(ui.mutex);status=ui.status;}SetWindowTextA(statusLabel,status.c_str());
        DWORD active=0;GetWindowThreadProcessId(GetForegroundWindow(),&active);HWND game=nullptr;
        if(ui.pid&&active==ui.pid&&ui.esp)EnumWindows(findWindow,(LPARAM)&game);
        if(game&&!IsIconic(game)){
            RECT r{};GetClientRect(game,&r);POINT origin{0,0};ClientToScreen(game,&origin);
            SetWindowPos(overlay,HWND_TOPMOST,origin.x,origin.y,r.right,r.bottom,SWP_NOACTIVATE|SWP_SHOWWINDOW);InvalidateRect(overlay,nullptr,FALSE);
        }else ShowWindow(overlay,SW_HIDE);
        return 0;
    }
    if(msg==WM_DESTROY){ui.quit=true;KillTimer(w,1);if(overlay)DestroyWindow(overlay);PostQuitMessage(0);return 0;}
    return DefWindowProc(w,msg,wp,lp);
}
void run(){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    HINSTANCE instance=GetModuleHandle(nullptr);font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    WNDCLASSA wc{};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName="Sam4AimMenu";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassA(&wc);
    wc.lpfnWndProc=paint;wc.lpszClassName="Sam4AimOverlay";wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);RegisterClassA(&wc);
    window=CreateWindowExA(0,"Sam4AimMenu","Sam4 - Aim and ESP",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,510,550,nullptr,nullptr,instance,nullptr);
    overlay=CreateWindowExA(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,"Sam4AimOverlay","",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,nullptr);
    if(!window||!overlay){ui.message("Could not create menu/overlay");ui.quit=true;return;}
    SetLayeredWindowAttributes(overlay,RGB(0,0,0),255,LWA_COLORKEY);
    HWND aim=child("BUTTON","Enable aim (hold right mouse)",BS_AUTOCHECKBOX|WS_TABSTOP,18,14,290,24,101);SendMessage(aim,BM_SETCHECK,BST_CHECKED,0);
    HWND esp=child("BUTTON","Box ESP",BS_AUTOCHECKBOX|WS_TABSTOP,330,14,130,24,102);SendMessage(esp,BM_SETCHECK,BST_CHECKED,0);
    fovLabel=child("STATIC","",0,18,54,465,22);fovSlider=slider(77,1,360,(int)ui.fov.load(),201);
    distanceLabel=child("STATIC","",0,18,120,465,22);distanceSlider=slider(143,1,2000,(int)ui.distance.load(),202);
    cameraLabel=child("STATIC","",0,18,186,465,22);cameraSlider=slider(209,30,150,(int)ui.cameraFov.load(),203);
    child("STATIC","Instant aim. Insert: show/hide menu. End: exit.\r\nESP needs borderless/windowed mode. Boxes are approximate.\r\nMatch camera FOV to the game; aim FOV is separate.\r\nWall check filters aim; ESP boxes still show through walls.",0,18,258,465,72);
    HWND wall=child("BUTTON","Wall check (built-in tracer)",BS_AUTOCHECKBOX|WS_TABSTOP,18,333,465,24,103);SendMessage(wall,BM_SETCHECK,BST_CHECKED,0);
    child("STATIC","Target priority",0,18,370,130,22);
    HWND priority=child("COMBOBOX","",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,155,366,315,160,104);
    for(auto name:{"Closest","FOV (closest to crosshair)","Farthest","Lowest health"})SendMessageA(priority,CB_ADDSTRING,0,(LPARAM)name);
    SendMessage(priority,CB_SETCURSEL,(WPARAM)ui.priority.load(),0);
    child("BUTTON","Silent aim (experimental; hold right mouse + fire)",BS_AUTOCHECKBOX|WS_TABSTOP,18,403,465,25,105);
    statusLabel=child("STATIC","Starting...",0,18,443,465,55);
    labels();SetTimer(window,1,33,nullptr);ShowWindow(window,SW_SHOW);ui.ready=true;
    MSG msg{};while(GetMessage(&msg,nullptr,0,0)>0){if(!IsDialogMessage(window,&msg)){TranslateMessage(&msg);DispatchMessage(&msg);}}
}
}
struct UiThread {
    std::thread thread;
    explicit UiThread(bool enabled){if(enabled)thread=std::thread(Menu::run);}
    ~UiThread(){ui.quit=true;if(thread.joinable())thread.join();}
};
