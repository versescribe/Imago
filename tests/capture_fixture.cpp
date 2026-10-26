#include <windows.h>

LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(20,20,20));
        TextOutW(dc, 32, 24, L"Imago capture test / RGB + pixel grid", 36);
        const COLORREF colors[]{RGB(255,0,0),RGB(0,255,0),RGB(0,0,255)};
        for(int i=0;i<3;++i) {
            RECT rect{32+i*192,80,192+i*192,240};
            HBRUSH brush=CreateSolidBrush(colors[i]);FillRect(dc,&rect,brush);DeleteObject(brush);
        }
        for(int y=0;y<6;++y) for(int x=0;x<18;++x) {
            RECT rect{32+x*32,272+y*24,64+x*32,296+y*24};
            FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject((x+y)%2?BLACK_BRUSH:WHITE_BRUSH)));
        }
        EndPaint(window,&paint);return 0;
    }
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{};cls.lpfnWndProc=FixtureProc;cls.hInstance=instance;
    cls.lpszClassName=L"Imago.CaptureFixture";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    RegisterClassW(&cls);
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"Imago capture test",WS_OVERLAPPEDWINDOW,
        120,120,680,500,nullptr,nullptr,instance,nullptr);
    if(!window)return 1;
    ShowWindow(window,SW_SHOW);SetForegroundWindow(window);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    return 0;
}
