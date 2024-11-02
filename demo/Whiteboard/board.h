#pragma once
#include "../BaseWindow.h"
#include <windows.h>
#include <commdlg.h>
#include "board_define.h"
#include "Headers/HrtcEngine.h"
#include "Base/Event.h"
class BoardData {
public:
    BoardData() :m_dataThread(hrtc::CreateThread()) {
        m_page.EnableEraserInsert(true);
    }
    virtual ~BoardData() {}
    void OnPenBegin(const whiteboard::Point& p, int sessionId);
    void OnPenMove(const whiteboard::Point& p, int sessionId);
    void OnPenEnd(const whiteboard::Point& p, int sessionId);

    void OnEraserBegin(const whiteboard::Rect& rc, int sessionId);
    void OnEraserMove(const whiteboard::Rect& rc, int sessionId);
    void OnEraserEnd(const whiteboard::Rect& rc, int sessionId);

    void OnDataClear();

    void GetCurrentPage(whiteboard::Page& page);

    virtual void OnEraserEndComplete() = 0;
protected:
    whiteboard::Page m_page;
    whiteboard::Path m_currentPath;
    COLORREF m_currentColor = RGB(255, 255, 255);
    int m_penWidth = 2;
    std::shared_ptr<hrtc::IThread> m_dataThread;
};

class WhiteBoardWin : public BaseWindow<WhiteBoardWin>,public BoardData {
public:
    virtual PCWSTR  ClassName() const override {
        return L"WhiteboardClass";
    }
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

    void ShowToolButton();

    void DrawLine(HDC hdc, POINT pt1, POINT pt2, COLORREF color);
    void ShowColorDialog(COLORREF* color);

    void CreateMainMenu();

    void EraseRectangle(HDC hdc, RECT rc);

    void SetBackgroundColor(HDC hdc);
    virtual void OnEraserEndComplete() override;

    void ReferceCanvas(const std::list<whiteboard::Path>& data);

    bool CheckPoint(POINT x);

    enum ToolId {
        ID_BEGIN = 1,
        ID_COLOR = ID_BEGIN,
        ID_EDIT,
        ID_CLEAR,

        ID_COUNT
    };
private:
    POINT lastPoint = { -1, -1 };
    BOOL eraserMode = FALSE;
    int penSession = 0;
    int eraserSession = 0;
    HWND toolButton[ID_COUNT] ;
};


