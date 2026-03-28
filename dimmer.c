/*
 * DIMMER v3.5 - Screen Brightness Dimmer for Windows
 * Fixed: MinGW wide-string formatting, tray icon stability, auto-run
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define _WIN32_IE    0x0600

/*
 * CRITICAL: MinGW uses MSVCRT which treats %s as narrow string even
 * in wide functions (swprintf/vswprintf). Use %ls for wide strings.
 * Alternatively, define __USE_MINGW_ANSI_STDIO to get C99 behavior.
 */
#define __USE_MINGW_ANSI_STDIO 0

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

#define APP_NAME         L"Dimmer"
#define APP_VERSION      L"3.5"
#define MAIN_CLASS       L"DimmerMain35"
#define MSG_CLASS        L"DimmerMsg35"
#define REGKEY_CFG       L"Software\\Dimmer"
#define REGKEY_RUN       L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define REGKEY_GAMMA     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM"

#define TIMER_ID 1
#define TIMER_MS 1000
#define DIM_STEP 5
#define DIM_MIN  0
#define DIM_MAX  95
#define DIM_DEFAULT 40
#define MAIN_W 300
#define MAIN_H 420

#define TAB_SCREENS 0
#define TAB_OPTIONS 1
#define TAB_ABOUT   2
#define TAB_DEBUG   3
#define TAB_COUNT   4

#define IDC_TAB        2000
#define IDC_SLIDER     2001
#define IDC_EDIT_VAL   2002
#define IDC_CHK_SCREEN 2003
#define IDC_OPT_AUTORUN  2100
#define IDC_OPT_STARTMIN 2101
#define IDC_OPT_EXITTRAY 2102
#define IDC_DBG_CLEAR    2200
#define IDC_DBG_REFRESH  2201
#define IDC_DBG_UNLOCK   2202

#define HOTKEY_DIM_UP   1
#define HOTKEY_DIM_DOWN 2
#define HOTKEY_TOGGLE   3
#define WM_TRAYICON     (WM_USER+1)
#define WM_TRAY_SHOWMAIN (WM_USER+2)
#define ID_TRAY_SHOW    3001
#define ID_TRAY_TOGGLE  3002
#define ID_TRAY_EXIT    3003

#define C_BG      RGB(45,45,45)
#define C_BG_DARK RGB(30,30,30)
#define C_FG      RGB(224,224,224)
#define C_ACCENT  RGB(240,192,64)
#define C_ENTRY   RGB(56,56,56)
#define C_DBG_BG  RGB(10,10,10)
#define C_DBG_FG  RGB(0,221,68)

typedef struct{WORD Red[256];WORD Green[256];WORD Blue[256];}GAMMA_RAMP;

/* ================================================================ */
static struct {
    HINSTANCE hInst;
    HWND hMain;
    HWND hMsg;        /* Dedicated hidden window for tray messages */
    HFONT fUI,fBold,fMono,fBig,fTitle,fSmall;
    HBRUSH brBg,brDark,brEntry,brDbg;
    NOTIFYICONDATAW nid;
    BOOL trayOk;
    int dimLevel; BOOL screenOn;
    int maxDim; BOOL gammaUnlocked;
    BOOL optAutoRun,optStartMin,optExitTray;
    int winX,winY; BOOL posValid;
    int activeTab;
    GAMMA_RAMP origGamma; BOOL gammaStored;
    HWND sChk,sSlider,sEdit,sLblPct,sLblDim;
    HWND oChkAutoRun,oChkStartMin,oChkExitTray;
    HWND aLbl[12];int aCount;
    HWND dEdit,dBtnClear,dBtnRefresh,dBtnUnlock;
    HWND hTab;
    WNDPROC origSliderProc;
    UINT wmTaskbarCreated;
} G={0};

/* ================================================================
 *  Debug log - uses %ls for wide strings (MinGW compatible)
 * ================================================================ */
static void Log(const WCHAR *fmt, ...)
{
    WCHAR buf[1024]; SYSTEMTIME st; GetLocalTime(&st);
    int off = _snwprintf(buf, 1024, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    if (off < 0) off = 0;
    va_list a; va_start(a, fmt);
    _vsnwprintf(buf + off, 1024 - off, fmt, a);
    va_end(a);
    buf[1023] = 0; /* Ensure null termination */
    wcscat(buf, L"\r\n");
    if (G.dEdit && IsWindow(G.dEdit)) {
        int len = GetWindowTextLengthW(G.dEdit);
        SendMessageW(G.dEdit, EM_SETSEL, len, len);
        SendMessageW(G.dEdit, EM_REPLACESEL, FALSE, (LPARAM)buf);
    }
}

/* Helper: log a wide string without printf formatting issues */
static void LogStr(const WCHAR *prefix, const WCHAR *str) {
    WCHAR buf[1024];
    _snwprintf(buf, 1024, L"%ls%ls", prefix, str);
    buf[1023] = 0;
    Log(L"%ls", buf);
}

/* ================================================================
 *  Registry config
 * ================================================================ */
static void RegWriteInt(const WCHAR*name,int val){
    HKEY k;if(RegCreateKeyExW(HKEY_CURRENT_USER,REGKEY_CFG,0,NULL,0,KEY_WRITE,NULL,&k,NULL)==ERROR_SUCCESS){
    RegSetValueExW(k,name,0,REG_DWORD,(BYTE*)&val,sizeof(DWORD));RegCloseKey(k);}
}
static int RegReadInt(const WCHAR*name,int def){
    HKEY k;int val=def;if(RegOpenKeyExW(HKEY_CURRENT_USER,REGKEY_CFG,0,KEY_READ,&k)==ERROR_SUCCESS){
    DWORD data=0,sz=sizeof(DWORD),type=0;if(RegQueryValueExW(k,name,NULL,&type,(BYTE*)&data,&sz)==ERROR_SUCCESS&&type==REG_DWORD)val=(int)data;
    RegCloseKey(k);}return val;
}
static void CfgSave(void){
    RegWriteInt(L"DimLevel",G.dimLevel);RegWriteInt(L"ScreenOn",G.screenOn);
    RegWriteInt(L"AutoRun",G.optAutoRun);RegWriteInt(L"StartMin",G.optStartMin);
    RegWriteInt(L"ExitTray",G.optExitTray);RegWriteInt(L"PosValid",G.posValid);
    RegWriteInt(L"WinX",G.winX);RegWriteInt(L"WinY",G.winY);
}
static void CfgLoad(void){
    G.dimLevel=RegReadInt(L"DimLevel",DIM_DEFAULT);G.screenOn=RegReadInt(L"ScreenOn",1);
    G.optAutoRun=RegReadInt(L"AutoRun",0);G.optStartMin=RegReadInt(L"StartMin",0);
    G.optExitTray=RegReadInt(L"ExitTray",0);G.posValid=RegReadInt(L"PosValid",0);
    G.winX=RegReadInt(L"WinX",0);G.winY=RegReadInt(L"WinY",0);
    if(G.dimLevel<DIM_MIN)G.dimLevel=DIM_MIN;if(G.dimLevel>DIM_MAX)G.dimLevel=DIM_MAX;
}
static void SaveWindowPos(void){
    if(G.hMain&&IsWindow(G.hMain)){WINDOWPLACEMENT wp={sizeof(wp)};GetWindowPlacement(G.hMain,&wp);
    G.winX=wp.rcNormalPosition.left;G.winY=wp.rcNormalPosition.top;G.posValid=TRUE;}
}

/* ================================================================
 *  Auto-run - uses %ls for MinGW wide string compat
 * ================================================================ */
static void SetAutoRun(BOOL on){
    HKEY k=NULL;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,REGKEY_RUN,0,NULL,0,KEY_ALL_ACCESS,NULL,&k,NULL)!=ERROR_SUCCESS){
        Log(L"ERROR: Cannot open Run key");return;}
    if(on){
        WCHAR exe[MAX_PATH]; exe[0]=0;
        GetModuleFileNameW(NULL,exe,MAX_PATH);
        /* Build quoted path manually (no swprintf with %s) */
        WCHAR val[MAX_PATH+4]; val[0]=0;
        val[0]=L'"'; val[1]=0;
        wcscat(val, exe);
        wcscat(val, L"\"");
        DWORD cbData = (DWORD)(wcslen(val)+1)*sizeof(WCHAR);
        LONG r=RegSetValueExW(k,APP_NAME,0,REG_SZ,(BYTE*)val,cbData);
        if(r==ERROR_SUCCESS){
            /* Verify by reading back */
            WCHAR chk[MAX_PATH+10]={0};DWORD csz=sizeof(chk);
            if(RegQueryValueExW(k,APP_NAME,NULL,NULL,(BYTE*)chk,&csz)==ERROR_SUCCESS){
                Log(L"Auto-run: ENABLED (verified)");
                LogStr(L"  Value: ", chk);
            }else{
                Log(L"Auto-run: write OK but verify FAILED");
            }
        }else{
            Log(L"ERROR: RegSetValueEx returned %ld", r);
        }
    }else{
        RegDeleteValueW(k,APP_NAME);
        Log(L"Auto-run: DISABLED (key deleted)");
    }
    RegCloseKey(k);
}

static BOOL IsAutoRunValid(void){
    HKEY k;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,REGKEY_RUN,0,KEY_READ,&k)!=ERROR_SUCCESS)return FALSE;
    WCHAR val[MAX_PATH+10]={0};DWORD sz=sizeof(val),type=0;
    BOOL ok=FALSE;
    if(RegQueryValueExW(k,APP_NAME,NULL,&type,(BYTE*)val,&sz)==ERROR_SUCCESS&&type==REG_SZ){
        WCHAR exe[MAX_PATH];exe[0]=0;
        GetModuleFileNameW(NULL,exe,MAX_PATH);
        /* Build expected value manually */
        WCHAR expected[MAX_PATH+4]; expected[0]=0;
        expected[0]=L'"'; expected[1]=0;
        wcscat(expected, exe);
        wcscat(expected, L"\"");
        if(_wcsicmp(val,expected)==0) ok=TRUE;
        else if(_wcsicmp(val,exe)==0) ok=TRUE;
    }
    RegCloseKey(k);return ok;
}

/* ================================================================
 *  Gamma
 * ================================================================ */
static BOOL IsGammaUnlocked(void){HKEY k;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,REGKEY_GAMMA,0,KEY_READ,&k)!=ERROR_SUCCESS)return FALSE;
    DWORD val=0,sz=sizeof(val),type;BOOL ok=(RegQueryValueExW(k,L"GdiICMGammaRange",NULL,&type,(BYTE*)&val,&sz)==ERROR_SUCCESS&&type==REG_DWORD&&val>=256);
    RegCloseKey(k);return ok;}
static BOOL UnlockGammaRange(void){HKEY k;
    if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,REGKEY_GAMMA,0,NULL,0,KEY_SET_VALUE,NULL,&k,NULL)!=ERROR_SUCCESS)return FALSE;
    DWORD val=256;BOOL ok=(RegSetValueExW(k,L"GdiICMGammaRange",0,REG_DWORD,(BYTE*)&val,sizeof(val))==ERROR_SUCCESS);
    RegCloseKey(k);return ok;}
static void RestartAsAdmin(void){WCHAR exe[MAX_PATH];GetModuleFileNameW(NULL,exe,MAX_PATH);
    SHELLEXECUTEINFOW sei={sizeof(sei)};sei.lpVerb=L"runas";sei.lpFile=exe;sei.lpParameters=L"--unlock-gamma";sei.nShow=SW_SHOWNORMAL;
    if(ShellExecuteExW(&sei))PostQuitMessage(0);}
static int DetectMaxGamma(void){HDC hdc=GetDC(NULL);GAMMA_RAMP ramp;int lo=0,hi=DIM_MAX,best=0;
    while(lo<=hi){int mid=(lo+hi)/2;double f=1.0-((double)mid/100.0);if(f<0.02)f=0.02;
    for(int i=0;i<256;i++){WORD v=(WORD)(i*256);ramp.Red[i]=ramp.Green[i]=ramp.Blue[i]=(WORD)(v*f);}
    if(SetDeviceGammaRamp(hdc,&ramp)){best=mid;lo=mid+1;}else{hi=mid-1;}}
    for(int i=0;i<256;i++){WORD v=(WORD)(i*256);ramp.Red[i]=ramp.Green[i]=ramp.Blue[i]=v;}
    SetDeviceGammaRamp(hdc,&ramp);ReleaseDC(NULL,hdc);return best;}
static void GammaStore(void){HDC h=GetDC(NULL);
    if(GetDeviceGammaRamp(h,&G.origGamma)){G.gammaStored=TRUE;}
    else{G.gammaStored=TRUE;for(int i=0;i<256;i++){WORD v=(WORD)(i*256);G.origGamma.Red[i]=v;G.origGamma.Green[i]=v;G.origGamma.Blue[i]=v;}}
    ReleaseDC(NULL,h);}
static void GammaApply(void){if(!G.gammaStored)return;HDC h=GetDC(NULL);
    if(!G.screenOn||G.dimLevel==0){SetDeviceGammaRamp(h,&G.origGamma);ReleaseDC(NULL,h);return;}
    int e=G.dimLevel;if(e>G.maxDim)e=G.maxDim;double f=1.0-((double)e/100.0);if(f<0.05)f=0.05;GAMMA_RAMP r;
    for(int i=0;i<256;i++){r.Red[i]=(WORD)(G.origGamma.Red[i]*f);r.Green[i]=(WORD)(G.origGamma.Green[i]*f);r.Blue[i]=(WORD)(G.origGamma.Blue[i]*f);}
    SetDeviceGammaRamp(h,&r);ReleaseDC(NULL,h);}
static void GammaRestore(void){if(!G.gammaStored)return;HDC h=GetDC(NULL);SetDeviceGammaRamp(h,&G.origGamma);ReleaseDC(NULL,h);}

/* ================================================================
 *  Tray - uses dedicated message window (hMsg) for reliability
 * ================================================================ */
static HICON MakeTrayIcon(void){
    HDC sc=GetDC(NULL);HDC m=CreateCompatibleDC(sc);
    HBITMAP bc=CreateCompatibleBitmap(sc,16,16),bm=CreateBitmap(16,16,1,1,NULL);
    SelectObject(m,bc);RECT r={0,0,16,16};FillRect(m,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));
    HBRUSH by=CreateSolidBrush(RGB(255,200,50));HPEN p0=CreatePen(PS_NULL,0,0);
    SelectObject(m,by);SelectObject(m,p0);Ellipse(m,1,1,15,15);
    HBRUSH bd=CreateSolidBrush(RGB(80,80,80));SelectObject(m,bd);Ellipse(m,4,4,12,12);
    HDC mm=CreateCompatibleDC(sc);SelectObject(mm,bm);FillRect(mm,&r,(HBRUSH)GetStockObject(WHITE_BRUSH));
    HBRUSH bb=CreateSolidBrush(RGB(0,0,0));SelectObject(mm,bb);SelectObject(mm,CreatePen(PS_NULL,0,0));Ellipse(mm,1,1,15,15);
    ICONINFO ii={TRUE,0,0,bm,bc};HICON ico=CreateIconIndirect(&ii);
    DeleteDC(mm);DeleteDC(m);ReleaseDC(NULL,sc);
    DeleteObject(bc);DeleteObject(bm);DeleteObject(by);DeleteObject(bd);DeleteObject(bb);DeleteObject(p0);
    return ico;
}
static void TrayCreate(void){
    if(G.trayOk) Shell_NotifyIconW(NIM_DELETE,&G.nid); /* Remove old if exists */
    ZeroMemory(&G.nid,sizeof(G.nid));
    G.nid.cbSize=sizeof(NOTIFYICONDATAW);
    G.nid.hWnd=G.hMsg;  /* Use dedicated message window */
    G.nid.uID=1;
    G.nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    G.nid.uCallbackMessage=WM_TRAYICON;
    G.nid.hIcon=MakeTrayIcon();
    wcscpy(G.nid.szTip,L"Dimmer");
    if(Shell_NotifyIconW(NIM_ADD,&G.nid)) G.trayOk=TRUE;
    else{ /* Retry once after small delay */
        Sleep(100);
        G.trayOk=Shell_NotifyIconW(NIM_ADD,&G.nid);
    }
}
static void TrayUpdateTip(void){if(!G.trayOk)return;
    _snwprintf(G.nid.szTip,sizeof(G.nid.szTip)/sizeof(WCHAR),L"Dimmer - %d%% %ls",G.dimLevel,G.screenOn?L"(Active)":L"(Off)");
    G.nid.uFlags=NIF_TIP;Shell_NotifyIconW(NIM_MODIFY,&G.nid);}
static void TrayRemove(void){if(G.trayOk){Shell_NotifyIconW(NIM_DELETE,&G.nid);if(G.nid.hIcon)DestroyIcon(G.nid.hIcon);G.trayOk=FALSE;}}
static void TrayMenu(void){
    HMENU m=CreatePopupMenu();WCHAR txt[64];_snwprintf(txt,64,L"Dimmer: %d%%",G.dimLevel);
    AppendMenuW(m,MF_STRING|MF_GRAYED,0,txt);AppendMenuW(m,MF_SEPARATOR,0,NULL);
    AppendMenuW(m,MF_STRING,ID_TRAY_SHOW,L"Open Dimmer");
    AppendMenuW(m,MF_STRING,ID_TRAY_TOGGLE,G.screenOn?L"Turn Off":L"Turn On");
    AppendMenuW(m,MF_SEPARATOR,0,NULL);AppendMenuW(m,MF_STRING,ID_TRAY_EXIT,L"Exit");
    POINT pt;GetCursorPos(&pt);SetForegroundWindow(G.hMsg);
    TrackPopupMenu(m,TPM_RIGHTBUTTON|TPM_BOTTOMALIGN,pt.x,pt.y,0,G.hMsg,NULL);
    DestroyMenu(m);
}
static void MinimizeToTray(void){ShowWindow(G.hMain,SW_HIDE);}
static void RestoreFromTray(void){ShowWindow(G.hMain,SW_SHOW);ShowWindow(G.hMain,SW_RESTORE);SetForegroundWindow(G.hMain);}

/* ================================================================
 *  UI
 * ================================================================ */
static void UpdateUI(void){
    if(G.sSlider)SendMessageW(G.sSlider,TBM_SETPOS,TRUE,(LPARAM)(G.maxDim-G.dimLevel));
    if(G.sEdit){WCHAR b[8];_snwprintf(b,8,L"%d",G.dimLevel);SetWindowTextW(G.sEdit,b);}
    if(G.sLblPct){WCHAR b[8];_snwprintf(b,8,L"%d%%",G.dimLevel);SetWindowTextW(G.sLblPct,b);InvalidateRect(G.sLblPct,NULL,TRUE);}
    TrayUpdateTip();}
static void SetDim(int lv){if(lv<DIM_MIN)lv=DIM_MIN;if(lv>G.maxDim)lv=G.maxDim;G.dimLevel=lv;GammaApply();UpdateUI();CfgSave();}
static void AdjustDim(int d){SetDim(G.dimLevel+d);Log(L"Dim: %d%%",G.dimLevel);}
static void ToggleDim(void){G.screenOn=!G.screenOn;if(G.sChk)SendMessageW(G.sChk,BM_SETCHECK,G.screenOn?BST_CHECKED:BST_UNCHECKED,0);
    GammaApply();CfgSave();TrayUpdateTip();Log(L"Dimmer %ls",G.screenOn?L"ON":L"OFF");}

static void ShowTab(int tab){int s;
    s=(tab==TAB_SCREENS)?SW_SHOW:SW_HIDE;
    if(G.sChk)ShowWindow(G.sChk,s);if(G.sSlider)ShowWindow(G.sSlider,s);if(G.sEdit)ShowWindow(G.sEdit,s);if(G.sLblPct)ShowWindow(G.sLblPct,s);if(G.sLblDim)ShowWindow(G.sLblDim,s);
    s=(tab==TAB_OPTIONS)?SW_SHOW:SW_HIDE;
    if(G.oChkAutoRun)ShowWindow(G.oChkAutoRun,s);if(G.oChkStartMin)ShowWindow(G.oChkStartMin,s);if(G.oChkExitTray)ShowWindow(G.oChkExitTray,s);
    s=(tab==TAB_ABOUT)?SW_SHOW:SW_HIDE;for(int i=0;i<G.aCount;i++)if(G.aLbl[i])ShowWindow(G.aLbl[i],s);
    s=(tab==TAB_DEBUG)?SW_SHOW:SW_HIDE;
    if(G.dEdit)ShowWindow(G.dEdit,s);if(G.dBtnClear)ShowWindow(G.dBtnClear,s);if(G.dBtnRefresh)ShowWindow(G.dBtnRefresh,s);if(G.dBtnUnlock)ShowWindow(G.dBtnUnlock,s);
    G.activeTab=tab;}

static LRESULT CALLBACK SliderProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEWHEEL){short d=GET_WHEEL_DELTA_WPARAM(w);if(d>0)AdjustDim(+DIM_STEP);else if(d<0)AdjustDim(-DIM_STEP);return 0;}
    return CallWindowProcW(G.origSliderProc,h,m,w,l);}

/* ================================================================
 *  Build controls
 * ================================================================ */
static void BuildControls(HWND hw){int L=15,T=42;
    G.sChk=CreateWindowExW(0,L"BUTTON",L"  #1",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,L+10,T+5,100,22,hw,(HMENU)IDC_CHK_SCREEN,G.hInst,NULL);
    SendMessageW(G.sChk,WM_SETFONT,(WPARAM)G.fBold,TRUE);SendMessageW(G.sChk,BM_SETCHECK,G.screenOn?BST_CHECKED:BST_UNCHECKED,0);
    G.sSlider=CreateWindowExW(0,TRACKBAR_CLASSW,NULL,WS_CHILD|WS_VISIBLE|TBS_VERT|TBS_BOTH|TBS_NOTICKS,L+25,T+35,45,210,hw,(HMENU)IDC_SLIDER,G.hInst,NULL);
    SendMessageW(G.sSlider,TBM_SETRANGE,TRUE,MAKELPARAM(0,G.maxDim));SendMessageW(G.sSlider,TBM_SETPOS,TRUE,(LPARAM)(G.maxDim-G.dimLevel));
    SendMessageW(G.sSlider,TBM_SETPAGESIZE,0,5);SendMessageW(G.sSlider,TBM_SETLINESIZE,0,5);
    G.origSliderProc=(WNDPROC)SetWindowLongPtrW(G.sSlider,GWLP_WNDPROC,(LONG_PTR)SliderProc);
    G.sLblPct=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_CENTER,L+90,T+110,130,45,hw,NULL,G.hInst,NULL);SendMessageW(G.sLblPct,WM_SETFONT,(WPARAM)G.fBig,TRUE);
    G.sLblDim=CreateWindowExW(0,L"STATIC",L"Dim level:",WS_CHILD|WS_VISIBLE,L+10,T+260,70,18,hw,NULL,G.hInst,NULL);SendMessageW(G.sLblDim,WM_SETFONT,(WPARAM)G.fSmall,TRUE);
    G.sEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_CENTER|ES_NUMBER,L+80,T+257,50,24,hw,(HMENU)IDC_EDIT_VAL,G.hInst,NULL);
    SendMessageW(G.sEdit,WM_SETFONT,(WPARAM)G.fUI,TRUE);SendMessageW(G.sEdit,EM_SETLIMITTEXT,3,0);
    int oy=T+15;
    G.oChkAutoRun=CreateWindowExW(0,L"BUTTON",L"Auto run (after user login)",WS_CHILD|BS_AUTOCHECKBOX,L+5,oy,260,22,hw,(HMENU)IDC_OPT_AUTORUN,G.hInst,NULL);
    SendMessageW(G.oChkAutoRun,WM_SETFONT,(WPARAM)G.fUI,TRUE);oy+=32;
    G.oChkStartMin=CreateWindowExW(0,L"BUTTON",L"Start minimised (to tray)",WS_CHILD|BS_AUTOCHECKBOX,L+5,oy,260,22,hw,(HMENU)IDC_OPT_STARTMIN,G.hInst,NULL);
    SendMessageW(G.oChkStartMin,WM_SETFONT,(WPARAM)G.fUI,TRUE);SendMessageW(G.oChkStartMin,BM_SETCHECK,G.optStartMin?BST_CHECKED:BST_UNCHECKED,0);oy+=32;
    G.oChkExitTray=CreateWindowExW(0,L"BUTTON",L"Exit button hides to tray",WS_CHILD|BS_AUTOCHECKBOX,L+5,oy,260,22,hw,(HMENU)IDC_OPT_EXITTRAY,G.hInst,NULL);
    SendMessageW(G.oChkExitTray,WM_SETFONT,(WPARAM)G.fUI,TRUE);SendMessageW(G.oChkExitTray,BM_SETCHECK,G.optExitTray?BST_CHECKED:BST_UNCHECKED,0);
    G.aCount=0;
    struct{const WCHAR*t;HFONT*f;int h;DWORD st;}ab[]={
        {L"\x2600",&G.fTitle,35,SS_CENTER},{L"Dimmer",&G.fTitle,28,SS_CENTER},{L"v" APP_VERSION,&G.fUI,22,SS_CENTER},
        {L"Screen Brightness Dimmer for Windows",&G.fUI,25,SS_CENTER},
        {L"\x2022  Hardware gamma-based dimming",&G.fSmall,17,0},{L"\x2022  Invisible to screenshots & recordings",&G.fSmall,17,0},
        {L"\x2022  Works over ALL apps (even fullscreen)",&G.fSmall,17,0},{L"\x2022  Global keyboard shortcuts (5% steps)",&G.fSmall,17,0},
        {L"\x2022  Scroll wheel on slider",&G.fSmall,17,0},{L"\x2022  System tray & auto-start",&G.fSmall,17,0},
        {L"",&G.fSmall,10,0},{L"Standalone Win32 \x2022 No dependencies",&G.fSmall,20,SS_CENTER}};
    int ay=T+8;for(int i=0;i<12;i++){int ax=(ab[i].st&SS_CENTER)?0:L+30;int aw=(ab[i].st&SS_CENTER)?(MAIN_W-20):230;
    G.aLbl[i]=CreateWindowExW(0,L"STATIC",ab[i].t,WS_CHILD|ab[i].st,ax,ay,aw,ab[i].h,hw,NULL,G.hInst,NULL);
    SendMessageW(G.aLbl[i],WM_SETFONT,(WPARAM)*ab[i].f,TRUE);ay+=ab[i].h;G.aCount++;}
    G.dBtnClear=CreateWindowExW(0,L"BUTTON",L"Clear",WS_CHILD|BS_PUSHBUTTON,L,T+5,50,22,hw,(HMENU)IDC_DBG_CLEAR,G.hInst,NULL);SendMessageW(G.dBtnClear,WM_SETFONT,(WPARAM)G.fSmall,TRUE);
    G.dBtnRefresh=CreateWindowExW(0,L"BUTTON",L"Re-apply",WS_CHILD|BS_PUSHBUTTON,L+55,T+5,60,22,hw,(HMENU)IDC_DBG_REFRESH,G.hInst,NULL);SendMessageW(G.dBtnRefresh,WM_SETFONT,(WPARAM)G.fSmall,TRUE);
    G.dBtnUnlock=CreateWindowExW(0,L"BUTTON",L"Unlock Full Range",WS_CHILD|BS_PUSHBUTTON,L+120,T+5,145,22,hw,(HMENU)IDC_DBG_UNLOCK,G.hInst,NULL);SendMessageW(G.dBtnUnlock,WM_SETFONT,(WPARAM)G.fSmall,TRUE);
    G.dEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,L,T+32,MAIN_W-40,270,hw,NULL,G.hInst,NULL);
    SendMessageW(G.dEdit,WM_SETFONT,(WPARAM)G.fMono,TRUE);UpdateUI();ShowTab(TAB_SCREENS);}

static HBRUSH OnColor(HDC hdc,HWND ctl){
    if(ctl==G.dEdit){SetTextColor(hdc,C_DBG_FG);SetBkColor(hdc,C_DBG_BG);return G.brDbg;}
    if(ctl==G.sEdit){SetTextColor(hdc,C_FG);SetBkColor(hdc,C_ENTRY);return G.brEntry;}
    if(ctl==G.sLblPct){SetTextColor(hdc,C_ACCENT);SetBkColor(hdc,C_BG);return G.brBg;}
    SetTextColor(hdc,C_FG);SetBkColor(hdc,C_BG);return G.brBg;}

/* ================================================================
 *  Message window proc (handles tray icon clicks)
 * ================================================================ */
static LRESULT CALLBACK MsgProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_TRAYICON){
        if(lp==WM_LBUTTONUP||lp==WM_LBUTTONDBLCLK){
            if(IsWindowVisible(G.hMain))MinimizeToTray();else RestoreFromTray();
        }else if(lp==WM_RBUTTONUP){
            TrayMenu();
        }
        return 0;
    }
    if(msg==WM_COMMAND){
        /* Forward tray menu commands to main window */
        int id=LOWORD(wp);
        if(id==ID_TRAY_SHOW){RestoreFromTray();return 0;}
        if(id==ID_TRAY_TOGGLE){ToggleDim();return 0;}
        if(id==ID_TRAY_EXIT){if(G.hMain)DestroyWindow(G.hMain);return 0;}
    }
    /* Handle TaskbarCreated (Explorer restart) */
    if(msg==G.wmTaskbarCreated && G.wmTaskbarCreated!=0){
        TrayCreate();TrayUpdateTip();return 0;
    }
    return DefWindowProcW(hw,msg,wp,lp);
}

/* ================================================================
 *  Main window proc
 * ================================================================ */
static LRESULT CALLBACK MainProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        G.fUI=CreateFontW(-13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        G.fBold=CreateFontW(-13,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        G.fMono=CreateFontW(-11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Consolas");
        G.fBig=CreateFontW(-30,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        G.fTitle=CreateFontW(-20,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        G.fSmall=CreateFontW(-11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        G.brBg=CreateSolidBrush(C_BG);G.brDark=CreateSolidBrush(C_BG_DARK);G.brEntry=CreateSolidBrush(C_ENTRY);G.brDbg=CreateSolidBrush(C_DBG_BG);
        G.hTab=CreateWindowExW(0,WC_TABCONTROLW,NULL,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,5,5,MAIN_W-12,30,hw,(HMENU)IDC_TAB,G.hInst,NULL);
        SendMessageW(G.hTab,WM_SETFONT,(WPARAM)G.fBold,TRUE);
        TCITEMW ti={0};ti.mask=TCIF_TEXT;const WCHAR*tabs[]={L"Screens",L"Options",L"About",L"Debug"};
        for(int i=0;i<TAB_COUNT;i++){ti.pszText=(LPWSTR)tabs[i];SendMessageW(G.hTab,TCM_INSERTITEMW,i,(LPARAM)&ti);}
        G.gammaUnlocked=IsGammaUnlocked();GammaStore();G.maxDim=DetectMaxGamma();if(G.maxDim<10)G.maxDim=10;if(G.dimLevel>G.maxDim)G.dimLevel=G.maxDim;
        BuildControls(hw);GammaApply();
        G.optAutoRun=IsAutoRunValid();SendMessageW(G.oChkAutoRun,BM_SETCHECK,G.optAutoRun?BST_CHECKED:BST_UNCHECKED,0);CfgSave();

        Log(L"Dimmer v%ls started",APP_VERSION);
        Log(L"Gamma: %ls (max %d%%)",G.gammaUnlocked?L"UNLOCKED":L"LOCKED",G.maxDim);
        Log(L"Auto-run: %ls",G.optAutoRun?L"ENABLED":L"disabled");
        {WCHAR exe[MAX_PATH];exe[0]=0;GetModuleFileNameW(NULL,exe,MAX_PATH);LogStr(L"Exe: ",exe);}
        {HKEY k;if(RegOpenKeyExW(HKEY_CURRENT_USER,REGKEY_RUN,0,KEY_READ,&k)==ERROR_SUCCESS){
            WCHAR v[MAX_PATH+10]={0};DWORD sz=sizeof(v);
            if(RegQueryValueExW(k,APP_NAME,NULL,NULL,(BYTE*)v,&sz)==ERROR_SUCCESS)LogStr(L"Run key: ",v);
            else Log(L"Run key: (not set)");RegCloseKey(k);}}
        Log(L"Pos: valid=%d x=%d y=%d",G.posValid,G.winX,G.winY);

        TrayCreate();
        G.wmTaskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
        /* Also register on the message window */
        RegisterHotKey(hw,HOTKEY_DIM_UP,MOD_CONTROL|MOD_ALT,VK_UP);
        RegisterHotKey(hw,HOTKEY_DIM_DOWN,MOD_CONTROL|MOD_ALT,VK_DOWN);
        RegisterHotKey(hw,HOTKEY_TOGGLE,MOD_CONTROL|MOD_ALT,VK_F1);
        SetTimer(hw,TIMER_ID,TIMER_MS,NULL);return 0;}
    case WM_TIMER:if(wp==TIMER_ID&&G.screenOn&&G.dimLevel>0)GammaApply();return 0;
    case WM_HOTKEY:if(wp==HOTKEY_DIM_UP)AdjustDim(+DIM_STEP);if(wp==HOTKEY_DIM_DOWN)AdjustDim(-DIM_STEP);if(wp==HOTKEY_TOGGLE)ToggleDim();return 0;
    case WM_MOUSEWHEEL:if(G.activeTab==TAB_SCREENS){short d=GET_WHEEL_DELTA_WPARAM(wp);if(d>0)AdjustDim(+DIM_STEP);else if(d<0)AdjustDim(-DIM_STEP);return 0;}break;
    case WM_NOTIFY:{NMHDR*nm=(NMHDR*)lp;if(nm->hwndFrom==G.hTab&&nm->code==TCN_SELCHANGE){ShowTab((int)SendMessageW(G.hTab,TCM_GETCURSEL,0,0));InvalidateRect(hw,NULL,TRUE);}return 0;}
    case WM_VSCROLL:case WM_HSCROLL:if((HWND)lp==G.sSlider)SetDim(G.maxDim-(int)SendMessageW(G.sSlider,TBM_GETPOS,0,0));return 0;
    case WM_COMMAND:{int id=LOWORD(wp),cmd=HIWORD(wp);
        if(id==IDC_EDIT_VAL&&cmd==EN_CHANGE){WCHAR b[8];GetWindowTextW(G.sEdit,b,8);int v=_wtoi(b);
            if(v>=0&&v<=G.maxDim&&v!=G.dimLevel){G.dimLevel=v;GammaApply();SendMessageW(G.sSlider,TBM_SETPOS,TRUE,G.maxDim-v);
            WCHAR p[8];_snwprintf(p,8,L"%d%%",v);SetWindowTextW(G.sLblPct,p);InvalidateRect(G.sLblPct,NULL,TRUE);CfgSave();TrayUpdateTip();}return 0;}
        if(id==IDC_CHK_SCREEN){G.screenOn=(SendMessageW(G.sChk,BM_GETCHECK,0,0)==BST_CHECKED);GammaApply();CfgSave();TrayUpdateTip();Log(L"Dimmer %ls",G.screenOn?L"ON":L"OFF");return 0;}
        if(id==IDC_OPT_AUTORUN){G.optAutoRun=(SendMessageW(G.oChkAutoRun,BM_GETCHECK,0,0)==BST_CHECKED);SetAutoRun(G.optAutoRun);CfgSave();return 0;}
        if(id==IDC_OPT_STARTMIN){G.optStartMin=(SendMessageW(G.oChkStartMin,BM_GETCHECK,0,0)==BST_CHECKED);CfgSave();Log(L"StartMin: %d",G.optStartMin);return 0;}
        if(id==IDC_OPT_EXITTRAY){G.optExitTray=(SendMessageW(G.oChkExitTray,BM_GETCHECK,0,0)==BST_CHECKED);CfgSave();Log(L"ExitTray: %d",G.optExitTray);return 0;}
        if(id==IDC_DBG_CLEAR){SetWindowTextW(G.dEdit,L"");return 0;}
        if(id==IDC_DBG_REFRESH){GammaApply();Log(L"Gamma re-applied at %d%%",G.dimLevel);return 0;}
        if(id==IDC_DBG_UNLOCK){
            if(G.gammaUnlocked&&G.maxDim>=DIM_MAX){MessageBoxW(hw,L"Already unlocked!",APP_NAME,MB_ICONINFORMATION);return 0;}
            if(G.gammaUnlocked&&G.maxDim<DIM_MAX){MessageBoxW(hw,L"Registry set. Restart PC.",APP_NAME,MB_ICONINFORMATION);return 0;}
            if(UnlockGammaRange()){G.gammaUnlocked=TRUE;MessageBoxW(hw,L"Done! RESTART YOUR PC.",APP_NAME,MB_ICONINFORMATION);}
            else{int r=MessageBoxW(hw,L"Needs Admin. Proceed?",APP_NAME,MB_YESNO|MB_ICONQUESTION);if(r==IDYES){GammaRestore();RestartAsAdmin();}}return 0;}
        return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:return(LRESULT)OnColor((HDC)wp,(HWND)lp);
    case WM_ERASEBKGND:{HDC dc=(HDC)wp;RECT rc;GetClientRect(hw,&rc);FillRect(dc,&rc,G.brBg);
        RECT bar={0,rc.bottom-28,rc.right,rc.bottom};FillRect(dc,&bar,G.brDark);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,C_ACCENT);SelectObject(dc,G.fSmall);
        WCHAR info[128];_snwprintf(info,128,L"Ctrl+Alt+\x2191\x2193  adjust   |   Ctrl+Alt+F1  toggle   |   Max: %d%%",G.maxDim);
        DrawTextW(dc,info,-1,&bar,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return 1;}
    case WM_MOVE:if(IsWindowVisible(hw)&&!IsIconic(hw)){WINDOWPLACEMENT wpl={sizeof(wpl)};GetWindowPlacement(hw,&wpl);
        G.winX=wpl.rcNormalPosition.left;G.winY=wpl.rcNormalPosition.top;G.posValid=TRUE;}return 0;
    case WM_SIZE:if(wp==SIZE_MINIMIZED){MinimizeToTray();return 0;}break;
    case WM_CLOSE:SaveWindowPos();CfgSave();if(G.optExitTray&&G.trayOk){MinimizeToTray();return 0;}DestroyWindow(hw);return 0;
    case WM_DESTROY:KillTimer(hw,TIMER_ID);UnregisterHotKey(hw,HOTKEY_DIM_UP);UnregisterHotKey(hw,HOTKEY_DIM_DOWN);UnregisterHotKey(hw,HOTKEY_TOGGLE);
        TrayRemove();SaveWindowPos();CfgSave();GammaRestore();
        DeleteObject(G.fUI);DeleteObject(G.fBold);DeleteObject(G.fMono);DeleteObject(G.fBig);DeleteObject(G.fTitle);DeleteObject(G.fSmall);
        DeleteObject(G.brBg);DeleteObject(G.brDark);DeleteObject(G.brEntry);DeleteObject(G.brDbg);PostQuitMessage(0);return 0;}
    return DefWindowProcW(hw,msg,wp,lp);}

/* ================================================================ */
int WINAPI wWinMain(HINSTANCE hI,HINSTANCE hP,LPWSTR cmd,int show){
    (void)hP;G.hInst=hI;
    if(cmd&&wcsstr(cmd,L"--unlock-gamma")){
        if(UnlockGammaRange())MessageBoxW(NULL,L"Done! Restart PC.",APP_NAME,MB_ICONINFORMATION);
        else MessageBoxW(NULL,L"Failed. Run as Admin.",APP_NAME,MB_ICONERROR);return 0;}
    HANDLE mtx=CreateMutexW(NULL,TRUE,L"DimmerMtx35");
    if(GetLastError()==ERROR_ALREADY_EXISTS){HWND ex=FindWindowW(MAIN_CLASS,APP_NAME);if(ex){ShowWindow(ex,SW_SHOW);ShowWindow(ex,SW_RESTORE);SetForegroundWindow(ex);}return 0;}
    INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_TAB_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&icc);
    CfgLoad();

    /* Create dedicated message window for tray icon */
    {WNDCLASSEXW wc={sizeof(wc)};wc.lpfnWndProc=MsgProc;wc.hInstance=hI;wc.lpszClassName=MSG_CLASS;RegisterClassExW(&wc);
    G.hMsg=CreateWindowExW(0,MSG_CLASS,L"",WS_POPUP,0,0,0,0,NULL,NULL,hI,NULL);}

    /* Register main window */
    {WNDCLASSEXW wc={sizeof(wc)};wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=MainProc;wc.hInstance=hI;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.lpszClassName=MAIN_CLASS;wc.hIcon=LoadIcon(NULL,IDI_APPLICATION);wc.hIconSm=LoadIcon(NULL,IDI_APPLICATION);
    RegisterClassExW(&wc);}

    int posX,posY;
    if(G.posValid){posX=G.winX;posY=G.winY;
        int sX=GetSystemMetrics(SM_XVIRTUALSCREEN),sY=GetSystemMetrics(SM_YVIRTUALSCREEN);
        int sW=GetSystemMetrics(SM_CXVIRTUALSCREEN),sH=GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if(posX<sX-MAIN_W+50)posX=sX;if(posY<sY-MAIN_H+50)posY=sY;
        if(posX>sX+sW-50)posX=sX+sW-MAIN_W;if(posY>sY+sH-50)posY=sY+sH-MAIN_H;
    }else{posX=(GetSystemMetrics(SM_CXSCREEN)-MAIN_W)/2;posY=(GetSystemMetrics(SM_CYSCREEN)-MAIN_H)/2;}

    /* Use WS_EX_TOOLWINDOW to hide from taskbar, WS_EX_APPWINDOW removed */
    G.hMain=CreateWindowExW(WS_EX_TOOLWINDOW,MAIN_CLASS,APP_NAME,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,posX,posY,MAIN_W,MAIN_H,NULL,NULL,hI,NULL);
    if(!G.hMain){MessageBoxW(NULL,L"Failed",APP_NAME,MB_ICONERROR);return 1;}
    if(G.optStartMin)ShowWindow(G.hMain,SW_HIDE);else ShowWindow(G.hMain,SW_SHOW);
    UpdateWindow(G.hMain);

    MSG msg;while(GetMessageW(&msg,NULL,0,0)){
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&msg.hwnd==G.sEdit){WCHAR b[8];GetWindowTextW(G.sEdit,b,8);SetDim(_wtoi(b));continue;}
        TranslateMessage(&msg);DispatchMessageW(&msg);}
    GammaRestore();if(G.hMsg)DestroyWindow(G.hMsg);if(mtx){ReleaseMutex(mtx);CloseHandle(mtx);}return(int)msg.wParam;}
