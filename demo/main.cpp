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
    HWND hComboBox; // ������
    HWND hDisplayArea;
    HWND hChildWindows[4]; // ���ڴ洢��ʾ�����Ӵ���
    HWND hwnd;

    int CurrentComBoxIndex;
};

#define WHITE_BOARD
// ������
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

    // ��������
    hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"Win32 Layout Example",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 540, // ��ʼ��С
        NULL,
        NULL,
        hInstance,
        this // ����ǰ���󴫵ݸ����ڹ���
    );
}

void MainWindow::Show(int nCmdShow) {
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd); // ȷ���������ݸ���
}

void MainWindow::CreateControls(HWND hwnd) {
    // ����ı������
    hEdit = CreateWindow(L"EDIT", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        10, 10, 150, 30, hwnd, (HMENU)5, NULL, NULL);

    // ���������
    hComboBox = CreateWindow(L"COMBOBOX", NULL,
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWN | WS_VSCROLL,
        10, 50, 150, 100, hwnd, (HMENU)6, NULL, NULL);
    SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)L"ѡ�� 1");
    SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)L"ѡ�� 2");
    SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)L"ѡ�� 3");

    SendMessage(hComboBox, CB_SELECTSTRING, 0, (LPARAM)L"ѡ�� 1");
    CurrentComBoxIndex = 0;
    OnDeviceSelect(0);

    // ��Ӱ�ť
    CreateWindow(L"BUTTON", L"��������ͷ", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        10, 90, 150, 30, hwnd, (HMENU)1, NULL, NULL);
    CreateWindow(L"BUTTON", L"��������Ԥ��", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        10, 130, 150, 30, hwnd, (HMENU)2, NULL, NULL);
    CreateWindow(L"BUTTON", L"��ť 3", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        10, 170, 150, 30, hwnd, (HMENU)3, NULL, NULL);
}

void MainWindow::CreateDisplayArea(HWND hwnd) {
    // ������ʾ������
    hDisplayArea = CreateWindow(L"STATIC", NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, NULL, NULL);

    // ����ʾ���ڲ������ؼ�
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

    // ���������Ϊ��ʾ����ȵ�1/6
    int controlWidth = totalWidth / 6;
    int displayWidth = totalWidth - controlWidth - 10; // ��ȥ���

    // �����ؼ���С��λ��
    SetWindowPos(hDisplayArea, NULL, controlWidth + 10, 10, displayWidth, totalHeight - 20, SWP_NOZORDER);

    // �����������ڿؼ��Ĵ�С��λ��
    SetWindowPos(hEdit, NULL, 10, 10, controlWidth - 20, 30, SWP_NOZORDER);
    SetWindowPos(hComboBox, NULL, 10, 50, controlWidth - 20, 100, SWP_NOZORDER);

    for (int i = 1; i <= 3; ++i) {
        SetWindowPos(GetDlgItem(hwnd, i), NULL, 10, (i + 1) * 40 + 10, controlWidth - 20, 30, SWP_NOZORDER);
    }

    // ������ʾ�����Ӵ���
    ResizeDisplayArea(hwnd);
}

void MainWindow::ResizeDisplayArea(HWND hwnd) {
    RECT rect;
    GetClientRect(hDisplayArea, &rect);

    int displayWidth = rect.right - rect.left;
    int displayHeight = rect.bottom - rect.top;

    // ÿ���Ӵ��ڵĿ�Ⱥ͸߶�
    int childWidth = (displayWidth - 30) / 2; // 2��
    int childHeight = (displayHeight - 30) / 2; // 2��

    // �����Ӵ��ڵĴ�С��λ��
    for (int i = 0; i < 4; ++i) {
        int x = 10 + (i % 2) * (childWidth + 10); // 10 �Ǽ��
        int y = 10 + (i / 2) * (childHeight + 10);
        SetWindowPos(hChildWindows[i], NULL, x, y, childWidth, childHeight, SWP_NOZORDER);
    }
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_NCCREATE) {
        // ��ȡ CREATESTRUCT �ṹ
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        MainWindow* pWindow = (MainWindow*)pCreate->lpCreateParams;

        // ������ָ��洢�ڴ���������
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pWindow);
    }
    else {
        // ��ȡ��������
        MainWindow* pWindow = (MainWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pWindow) {
            switch (uMsg) {
            case WM_CREATE:
                pWindow->CreateControls(hwnd);
                pWindow->CreateDisplayArea(hwnd);
                pWindow->ResizeControls(hwnd); // ��ʼ���ؼ�λ��
                return 0;
            case WM_SIZE:
                pWindow->ResizeControls(hwnd); // �����ڴ�С�仯
                return 0;
            case WM_COMMAND:
                if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == 6) {
                    pWindow->OnComboBoxSelectionChange(); // ����������ѡ��仯
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
            SetWindowText(hButton, L"��������ͷ");
        }
        else {
            SetWindowText(hButton, L"�ر�����ͷ");
        }
        OnCaptureEnable(!m_captureStart);
        break;
    case 2:
        if (m_renderSelfStart) {
            SetWindowText(hButton, L"��������Ԥ��");
        }
        else {
            SetWindowText(hButton, L"�رձ���Ԥ��");
        }
        OnRenderSelfEnable(!m_renderSelfStart? hChildWindows[1]:nullptr);
        break;
    default:
        break;
    }
    EnableWindow(hButton, true);

    //wchar_t msg[256];
    // ��ȡ������е�����
    //wchar_t text[256];
    //GetWindowText(hEdit, text, sizeof(text) / sizeof(wchar_t));

    // ��ʾ��ť��������������
    //swprintf(msg, sizeof(msg) / sizeof(wchar_t), L"��ť %d �����!\n���������: %s", buttonId, text);
    //MessageBox(NULL, msg, L"��ť���", MB_OK);
}

void MainWindow::OnComboBoxSelectionChange() {
    // ��ȡ������ǰѡ���������
    int index = SendMessage(hComboBox, CB_GETCURSEL, 0, 0);
    if (index != CB_ERR) {
        if (index != CurrentComBoxIndex) {
            OnDeviceSelect(index);
            CurrentComBoxIndex = index;
        }
    }
}
