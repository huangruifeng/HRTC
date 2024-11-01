#include <windows.h>
#include <string>
#include "model.h"
#include "Whiteboard/board.h"
class MainWindow : public RtcModel {
public:
    MainWindow(HINSTANCE hInstance);
    void Show(int nCmdShow);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    void CreateControls(HWND hwnd);
    void CreateDisplayArea(HWND hwnd);
    void ResizeControls(HWND hwnd);
    void ResizeDisplayArea(HWND hwnd);
    void OnButtonClick(int buttonId);
    void OnComboBoxSelectionChange();

    HWND hEdit;
    HWND hComboBox; // 下拉框
    HWND hDisplayArea;
    HWND hChildWindows[4]; // 用于存储显示区的子窗口
    HWND hwnd;

    int CurrentComBoxIndex;
};

#define WHITE_BOARD
// 主函数
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
#ifdef WHITE_BOARD
    WhiteBoardWin mainWindow;
    mainWindow.Create(L"board", WS_OVERLAPPEDWINDOW,0, CW_USEDEFAULT, CW_USEDEFAULT, 1080, 720);
    mainWindow.Show(true);
#else
    MainWindow mainWindow(hInstance);
    mainWindow.Show(nCmdShow);
#endif;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

MainWindow::MainWindow(HINSTANCE hInstance):CurrentComBoxIndex(-1) {
    const wchar_t CLASS_NAME[] = L"SampleWindowClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc = MainWindow::WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    // 创建窗口
    hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"Win32 Layout Example",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 540, // 初始大小
        NULL,
        NULL,
        hInstance,
        this // 将当前对象传递给窗口过程
    );
}

void MainWindow::Show(int nCmdShow) {
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd); // 确保窗口内容更新
}

void MainWindow::CreateControls(HWND hwnd) {
    // 添加文本输入框
    hEdit = CreateWindow(L"EDIT", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        10, 10, 150, 30, hwnd, (HMENU)5, NULL, NULL);

    // 添加下拉框
    hComboBox = CreateWindow(L"COMBOBOX", NULL,
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWN | WS_VSCROLL,
        10, 50, 150, 100, hwnd, (HMENU)6, NULL, NULL);
    SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)L"选项 1");
    SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)L"选项 2");
    SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)L"选项 3");

    SendMessage(hComboBox, CB_SELECTSTRING, 0, (LPARAM)L"选项 1");
    CurrentComBoxIndex = 0;
    OnDeviceSelect(0);

    // 添加按钮
    CreateWindow(L"BUTTON", L"开启摄像头", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        10, 90, 150, 30, hwnd, (HMENU)1, NULL, NULL);
    CreateWindow(L"BUTTON", L"开启本地预览", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        10, 130, 150, 30, hwnd, (HMENU)2, NULL, NULL);
    CreateWindow(L"BUTTON", L"按钮 3", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        10, 170, 150, 30, hwnd, (HMENU)3, NULL, NULL);
}

void MainWindow::CreateDisplayArea(HWND hwnd) {
    // 创建显示区容器
    hDisplayArea = CreateWindow(L"STATIC", NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, NULL, NULL);

    // 在显示区内部创建控件
    for (int i = 0; i < 4; ++i) {
        hChildWindows[i] = CreateWindow(L"STATIC", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER,
            0, 0, 0, 0, hDisplayArea, NULL, NULL, NULL);
    }
}

void MainWindow::ResizeControls(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);

    int totalWidth = rect.right - rect.left;
    int totalHeight = rect.bottom - rect.top;

    // 控制区宽度为显示区宽度的1/6
    int controlWidth = totalWidth / 6;
    int displayWidth = totalWidth - controlWidth - 10; // 减去间距

    // 调整控件大小和位置
    SetWindowPos(hDisplayArea, NULL, controlWidth + 10, 10, displayWidth, totalHeight - 20, SWP_NOZORDER);

    // 调整控制区内控件的大小和位置
    SetWindowPos(hEdit, NULL, 10, 10, controlWidth - 20, 30, SWP_NOZORDER);
    SetWindowPos(hComboBox, NULL, 10, 50, controlWidth - 20, 100, SWP_NOZORDER);

    for (int i = 1; i <= 3; ++i) {
        SetWindowPos(GetDlgItem(hwnd, i), NULL, 10, (i + 1) * 40 + 10, controlWidth - 20, 30, SWP_NOZORDER);
    }

    // 调整显示区内子窗口
    ResizeDisplayArea(hwnd);
}

void MainWindow::ResizeDisplayArea(HWND hwnd) {
    RECT rect;
    GetClientRect(hDisplayArea, &rect);

    int displayWidth = rect.right - rect.left;
    int displayHeight = rect.bottom - rect.top;

    // 每个子窗口的宽度和高度
    int childWidth = (displayWidth - 30) / 2; // 2列
    int childHeight = (displayHeight - 30) / 2; // 2行

    // 调整子窗口的大小和位置
    for (int i = 0; i < 4; ++i) {
        int x = 10 + (i % 2) * (childWidth + 10); // 10 是间隔
        int y = 10 + (i / 2) * (childHeight + 10);
        SetWindowPos(hChildWindows[i], NULL, x, y, childWidth, childHeight, SWP_NOZORDER);
    }
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_NCCREATE) {
        // 获取 CREATESTRUCT 结构
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        MainWindow* pWindow = (MainWindow*)pCreate->lpCreateParams;

        // 将对象指针存储在窗口数据中
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pWindow);
    }
    else {
        // 获取窗口数据
        MainWindow* pWindow = (MainWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pWindow) {
            switch (uMsg) {
            case WM_CREATE:
                pWindow->CreateControls(hwnd);
                pWindow->CreateDisplayArea(hwnd);
                pWindow->ResizeControls(hwnd); // 初始化控件位置
                return 0;
            case WM_SIZE:
                pWindow->ResizeControls(hwnd); // 处理窗口大小变化
                return 0;
            case WM_COMMAND:
                if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == 6) {
                    pWindow->OnComboBoxSelectionChange(); // 处理下拉框选择变化
                    return 0;
                }
                else if (LOWORD(wParam) >= 1 && LOWORD(wParam) <= 3) {
                    pWindow->OnButtonClick(LOWORD(wParam));
                    return 0;
                }
                break;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void MainWindow::OnButtonClick(int buttonId) {
    HWND hButton = GetDlgItem(hwnd, buttonId);
    EnableWindow(hButton, false);

    switch (buttonId)
    {
    case 1:
        if (m_captureStart) {
            SetWindowText(hButton, L"开启摄像头");
        }
        else {
            SetWindowText(hButton, L"关闭摄像头");
        }
        OnCaptureEnable(!m_captureStart);
        break;
    case 2:
        if (m_renderSelfStart) {
            SetWindowText(hButton, L"开启本地预览");
        }
        else {
            SetWindowText(hButton, L"关闭本地预览");
        }
        OnRenderSelfEnable(!m_renderSelfStart? hChildWindows[1]:nullptr);
        break;
    default:
        break;
    }
    EnableWindow(hButton, true);

    //wchar_t msg[256];
    // 获取输入框中的内容
    //wchar_t text[256];
    //GetWindowText(hEdit, text, sizeof(text) / sizeof(wchar_t));

    // 显示按钮点击和输入框内容
    //swprintf(msg, sizeof(msg) / sizeof(wchar_t), L"按钮 %d 被点击!\n输入框内容: %s", buttonId, text);
    //MessageBox(NULL, msg, L"按钮点击", MB_OK);
}

void MainWindow::OnComboBoxSelectionChange() {
    // 获取下拉框当前选中项的索引
    int index = SendMessage(hComboBox, CB_GETCURSEL, 0, 0);
    if (index != CB_ERR) {
        if (index != CurrentComBoxIndex) {
            OnDeviceSelect(index);
            CurrentComBoxIndex = index;
        }
    }
}
