#include <windows.h>

#define DIVISIONS 2 

template <class DERIVED_TYPE> 
class BaseWindow
{
public:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        DERIVED_TYPE *pThis = NULL;

        if (uMsg == WM_NCCREATE)
        {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (DERIVED_TYPE*)pCreate->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

            pThis->m_hwnd = hwnd;
        }
        else
        {
            pThis = (DERIVED_TYPE*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }
        if (pThis)
        {
            return pThis->HandleMessage(uMsg, wParam, lParam);
        }
        else
        {
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }

    BaseWindow() : m_hwnd(NULL) { }

    BOOL Create(
        PCWSTR lpWindowName,
        DWORD dwStyle,
        DWORD dwExStyle = 0,
        int x = CW_USEDEFAULT,
        int y = CW_USEDEFAULT,
        int nWidth = CW_USEDEFAULT,
        int nHeight = CW_USEDEFAULT,
        HWND hWndParent = 0,
        HMENU hMenu = 0
        )
    {
        WNDCLASS wc = {0};

        wc.lpfnWndProc   = DERIVED_TYPE::WindowProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.lpszClassName = ClassName();

        RegisterClass(&wc);

        m_hwnd = CreateWindowEx(
            dwExStyle, ClassName(), lpWindowName, dwStyle, x, y,
            nWidth, nHeight, hWndParent, hMenu, GetModuleHandle(NULL), this
            );

        return (m_hwnd ? TRUE : FALSE);
    }

    HWND Window() const { return m_hwnd; }

protected:

    virtual PCWSTR  ClassName() const = 0;
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) = 0;

    HWND m_hwnd;
};

class ChildWindow : public BaseWindow<ChildWindow>{
public:
    PCWSTR  ClassName() const { return L"Hrtc Child"; }
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam){
        HDC hdc ;
        PAINTSTRUCT ps ;
        RECT rect ; 
        switch (uMsg)
        {
        case WM_CREATE :
            SetWindowLong (m_hwnd, 0, 0) ; // on/off flag
            return 0 ;

        case WM_LBUTTONDOWN :
             //todo add prev view

        case WM_PAINT :
            hdc = BeginPaint (m_hwnd, &ps) ;

            GetClientRect (m_hwnd, &rect) ;
            Rectangle (hdc, 0, 0, rect.right, rect.bottom) ;

            if (GetWindowLong (m_hwnd, 0))
            {
                MoveToEx (hdc, 0, 0, NULL) ;
                LineTo (hdc, rect.right, rect.bottom) ;
                MoveToEx (hdc, 0, rect.bottom, NULL) ;
                LineTo (hdc, rect.right, 0) ;
            }

            EndPaint (m_hwnd, &ps) ;
            return 0 ;
        }
        return DefWindowProc (m_hwnd, uMsg, wParam, lParam) ;
    }
};

class MainWindow : public BaseWindow<MainWindow>
{
public:
    PCWSTR  ClassName() const { return L"Hrtc Demo"; }
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    ChildWindow m_childWin[DIVISIONS][DIVISIONS];
};


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    MainWindow win;

    if (!win.Create(L"Hello Hrtc Demo", WS_OVERLAPPEDWINDOW))
    {
        return 0;
    }

    ShowWindow(win.Window(), nCmdShow);

    // Run the message loop.

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}


LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    int cxBlock, cyBlock, x, y ;

    switch (uMsg)
    {
    case WM_CREATE :
        for (x = 0 ; x < DIVISIONS ; x++)
            for (y = 0 ; y < DIVISIONS ; y++)
                m_childWin[x][y].Create(L"Hrtc_Child",
                    WS_CHILDWINDOW | WS_VISIBLE,0,0,0,0,0,m_hwnd) ;
       
        return 0 ;

    case WM_SIZE :
        cxBlock = LOWORD (lParam) / DIVISIONS ;
        cyBlock = HIWORD (lParam) / DIVISIONS ;
        for (x = 0 ; x < DIVISIONS ; x++)
            for (y = 0 ; y < DIVISIONS ; y++)
                MoveWindow (
                    m_childWin[x][y].Window(),
                    x * cxBlock, y * cyBlock,
                    cxBlock, cyBlock, TRUE) ;
        return 0 ;
    case WM_LBUTTONDOWN :
        MessageBeep (0) ;
        return 0 ;

    case WM_DESTROY :
        PostQuitMessage (0) ;
        return 0 ;
    default:
        return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }
    return TRUE;
}