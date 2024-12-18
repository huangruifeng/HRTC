#include "board.h"

#define WM_REFERCE_CANVAS (WM_USER + 1)
#define WM_SHOW_TOOL (WM_USER + 2)
const int ERASER_WIDTH = 20; // ��Ƥ����С
const int ERASER_HEIGHT = 30;
LRESULT WhiteBoardWin::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_LBUTTONDOWN:
        lastPoint.x = LOWORD(lParam);
        lastPoint.y = HIWORD(lParam);
        if (!CheckPoint(lastPoint))
            break;
        SetCapture(m_hwnd);

        if (eraserMode) {
            eraserSession++;
            if (eraserSession < 0) {
                eraserSession = 0;
            }

            RECT rc{ lastPoint.x - ERASER_WIDTH / 2, lastPoint.y - ERASER_HEIGHT / 2,
                    lastPoint.x + ERASER_WIDTH / 2, lastPoint.y + ERASER_HEIGHT / 2 };
            OnEraserBegin({ rc.left,rc.top,rc.right - rc.left,rc.bottom - rc.top }, eraserSession);
        }
        else {
            penSession++;
            if (penSession < 0) {
                penSession = 0;
            }
            OnPenBegin({ lastPoint.x ,lastPoint.y }, penSession);
        }
        break;
    case WM_REFERCE_CANVAS:
        {
            if (lParam) {

                ReferceCanvas(((whiteboard::Page*)wParam)->paths);
                hrtc::HEvent* e = (hrtc::HEvent*)lParam;
                e->set();
                ShowToolButton();
            }
            break;
        }
    case WM_MOUSEMOVE:
        if (wParam & MK_LBUTTON) {
            POINT currentPoint = { LOWORD(lParam), HIWORD(lParam) };

            if (!CheckPoint(currentPoint))
                break;

            HDC hdc = GetDC(m_hwnd);
            if (eraserMode) {
                RECT rc{ currentPoint.x - ERASER_WIDTH / 2, currentPoint.y - ERASER_HEIGHT / 2,
                     currentPoint.x + ERASER_WIDTH / 2, currentPoint.y + ERASER_HEIGHT / 2 };
                OnEraserMove({ rc.left,rc.top,rc.right - rc.left,rc.bottom - rc.top }, eraserSession);
                EraseRectangle(hdc, rc);
            }
            else {
                DrawLine(hdc, lastPoint, currentPoint, m_currentColor);
                OnPenMove({ currentPoint.x,currentPoint.y },penSession);
            }
            ReleaseDC(m_hwnd, hdc);
            lastPoint = currentPoint;
        }
        break;

    case WM_LBUTTONUP:
        {
            POINT currentPoint = { LOWORD(lParam), HIWORD(lParam) };
            ReleaseCapture();
            if (CheckPoint(currentPoint))
                lastPoint = currentPoint;
            if (eraserMode) {
                RECT rc{ lastPoint.x - ERASER_WIDTH / 2, lastPoint.y - ERASER_HEIGHT / 2,
                         lastPoint.x + ERASER_WIDTH / 2, lastPoint.y + ERASER_HEIGHT / 2 };
                OnEraserEnd({ rc.left,rc.top,rc.right - rc.left,rc.bottom - rc.top },eraserSession);
            }
            else {
                OnPenEnd({ lastPoint.x ,lastPoint.y },penSession);
            }
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_COLOR) {
            CHOOSECOLOR cc = {};
            static COLORREF acrCustClr[16];
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = m_hwnd;
            cc.rgbResult = m_currentColor;
            cc.lpCustColors = acrCustClr;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;

            if (ChooseColor(&cc)) {
                m_currentColor = cc.rgbResult;
                eraserMode = FALSE;
            }
        }
        else if (LOWORD(wParam) == ID_EDIT) {
            eraserMode = !eraserMode;
            if (eraserMode) {
                SetWindowText(toolButton[ID_EDIT], L"��");
            }
            else {
                SetWindowText(toolButton[ID_EDIT], L"��Ƥ��");
            }
        }
        else if (LOWORD(wParam) == ID_CLEAR) {
            HDC hdc = GetDC(m_hwnd);
            SetBackgroundColor(hdc);
            ReleaseDC(m_hwnd, hdc);
            OnDataClear();
            ShowToolButton();
        }
        break;

    case WM_CREATE:
        CreateMainMenu();
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        int i = 0;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        
        whiteboard::Page page;
        GetCurrentPage(page);
        ReferceCanvas(page.paths);
        EndPaint(m_hwnd, &ps);
        //SendMessage(m_hwnd, WM_SHOW_TOOL, 0,0);
        break;
    }
    case WM_SHOW_TOOL:
    case WM_SIZE: {
        ShowToolButton();
        break;
    }
        
    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void WhiteBoardWin::ShowToolButton()
{
    RECT rect;
    GetClientRect(m_hwnd,&rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int buttonWidth = 100;
    int buttonHeight = 30;
    int spacing = 10;

    // ���㰴ť��λ��
    int totalWidth = buttonWidth * 3 + spacing * 2; // ������ť�Ŀ��ȼ��ϼ��
    int startX = (width - totalWidth) / 2; // ����

                                           // ���ð�ť��λ��

    SetWindowPos(GetDlgItem(m_hwnd, ID_COLOR), NULL, startX, height - buttonHeight - 10, 0, 0, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(m_hwnd, ID_EDIT), NULL, startX + buttonWidth + spacing, height - buttonHeight - 10, 0, 0, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(m_hwnd, ID_CLEAR), NULL, startX + (buttonWidth + spacing) * 2, height - buttonHeight - 10, 0, 0, SWP_NOZORDER);

    SetWindowPos(GetDlgItem(m_hwnd, ID_COLOR), NULL, startX, height - buttonHeight - 10, buttonWidth, buttonHeight, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(m_hwnd, ID_EDIT), NULL, startX + buttonWidth + spacing, height - buttonHeight - 10, buttonWidth, buttonHeight, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(m_hwnd, ID_CLEAR), NULL, startX + (buttonWidth + spacing) * 2, height - buttonHeight - 10, buttonWidth, buttonHeight, SWP_NOZORDER);
}


void WhiteBoardWin::DrawLine(HDC hdc, POINT start, POINT end, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, m_penWidth, color);
    SelectObject(hdc, hPen);
    MoveToEx(hdc, start.x, start.y, NULL);
    LineTo(hdc, end.x, end.y);
    DeleteObject(hPen);
}

void WhiteBoardWin::ShowColorDialog(COLORREF* color) {
    CHOOSECOLOR cc = {};
    static COLORREF customColors[16];
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = NULL;
    cc.lpCustColors = customColors;
    cc.rgbResult = *color;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColor(&cc)) {
        *color = cc.rgbResult;
    }
}

void WhiteBoardWin::CreateMainMenu()
{
    toolButton[ID_COLOR] = CreateWindow(
        L"BUTTON", L"ѡ����ɫ",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        0, 0, 100, 30,
        m_hwnd, (HMENU)ID_COLOR, NULL, NULL);

    toolButton[ID_EDIT] = CreateWindow(
        L"BUTTON", L"��Ƥ��",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        0, 0, 100, 30,
        m_hwnd, (HMENU)ID_EDIT, NULL, NULL);

    toolButton[ID_CLEAR] = CreateWindow(
        L"BUTTON", L"����",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        0, 0, 100, 30,
        m_hwnd, (HMENU)ID_CLEAR, NULL, NULL);
}

void WhiteBoardWin::EraseRectangle(HDC hdc,RECT rc)
{
    HBRUSH hBrush = CreateSolidBrush(RGB(50, 50, 50)); // ��ɫ����
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255)); // ��ɫ�߿�
    SelectObject(hdc, hBrush);
    SelectObject(hdc, hPen);
    Rectangle(hdc,rc.left,rc.top, rc.right, rc.bottom);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

//void WhiteBoardWin::DrawEraser(HDC hdc, POINT point)
//{
//    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255)); // ��ɫ�߿�
//    SelectObject(hdc, hPen);
//
//    Rectangle(hdc, point.x - ERASER_WIDTH / 2, point.y - ERASER_HEIGHT / 2,
//        point.x + ERASER_WIDTH / 2, point.y + ERASER_HEIGHT / 2);
//
//    DeleteObject(hPen);
//}

void WhiteBoardWin::SetBackgroundColor(HDC hdc)
{
    RECT rect;
    GetClientRect(m_hwnd, &rect);
    HBRUSH hBrush = CreateSolidBrush(RGB(50, 50, 50)); // ��ɫ�ڰ�ɫ
    FillRect(hdc, &rect, hBrush);
    DeleteObject(hBrush);
}

void WhiteBoardWin::OnEraserEndComplete()
{
    hrtc::HEvent waitUiThreadEvent;
    SendMessage(m_hwnd, WM_REFERCE_CANVAS, (WPARAM)&m_page,(LPARAM)&waitUiThreadEvent);
    waitUiThreadEvent.Wait();
}

void WhiteBoardWin::ReferceCanvas(const std::list<whiteboard::Path>& data)
{
    HDC hdc = GetDC(m_hwnd);
    SetBackgroundColor(hdc);
    HDC memDC = CreateCompatibleDC(hdc);
    RECT rect;
    GetClientRect(m_hwnd, &rect);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, rect.right-rect.left, rect.bottom-rect.top);
    SelectObject(memDC, hBitmap);
    SetBackgroundColor(memDC);
    for (auto& path : data) {
        HPEN hPen = CreatePen(PS_SOLID, path.width, path.color);
        SelectObject(memDC, hPen);

        for (int i = 0; i < path.points.size() - 1; i++) {
            MoveToEx(memDC, path.points[i].x, path.points[i].y, NULL);
            LineTo(memDC, path.points[i+1].x, path.points[i+1].y);
        }
        DeleteObject(hPen);
    }

    BitBlt(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, memDC, 0, 0, SRCCOPY);

    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(m_hwnd, hdc);

}

bool WhiteBoardWin::CheckPoint(POINT x)
{
    RECT rect;
    GetClientRect(m_hwnd, &rect);
    if (x.x > rect.right || x.x < rect.left)
        return false;
    if (x.y > rect.bottom || x.y < rect.top)
        return false;
    return true;
}

void BoardData::OnPenBegin(const whiteboard::Point& p, int sessionId)
{
    m_dataThread->BeginInvoke([this,p]() {
        m_currentPath.Reset();
        m_currentPath.color = m_currentColor;
        m_currentPath.width = m_penWidth;
        m_currentPath.Append(p);
    });
}

void BoardData::OnPenMove(const whiteboard::Point& p, int sessionId)
{
    m_dataThread->BeginInvoke([this,p]() {
        m_currentPath.Append(p);
    });
}

void BoardData::OnPenEnd(const whiteboard::Point& p, int sessionId)
{
    m_dataThread->BeginInvoke([this,p]() {
        m_currentPath.Append(p);
        m_page.Append(m_currentPath);
    });
}

void BoardData::OnEraserBegin(const whiteboard::Rect& rc, int sessionId)
{
    m_dataThread->BeginInvoke([this,rc, sessionId]() {
        m_page.Eraser(rc,sessionId);
    });
}

void BoardData::OnEraserMove(const whiteboard::Rect& rc, int sessionId)
{
    m_dataThread->BeginInvoke([this, rc, sessionId]() {
        m_page.Eraser(rc, sessionId);
    });
}

void BoardData::OnEraserEnd(const whiteboard::Rect& rc, int sessionId)
{
    m_dataThread->BeginInvoke([this, rc,sessionId]() {
        m_page.Eraser(rc,sessionId);
        OnEraserEndComplete();
    });
}

void BoardData::OnDataClear()
{
    m_dataThread->BeginInvoke([this]() {
        m_page.Clear();
    });
}

void BoardData::GetCurrentPage(whiteboard::Page& page)
{
    m_dataThread->Invoke([&]() {
        page = m_page;
    });
}
