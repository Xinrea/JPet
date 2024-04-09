/*==============================================================================
FMOD Example Framework
Copyright (c), Firelight Technologies Pty, Ltd 2012-2024.
==============================================================================*/
#define WIN32_LEAN_AND_MEAN

#include "common.h"
#include <stdio.h>
#include <conio.h>
#include <Windows.h>
#include <Objbase.h>
#include <vector>

static HWND gWindow = nullptr;
static int gScreenWidth = 0;
static int gScreenHeight = 0;
static unsigned int gPressedButtons = 0;
static unsigned int gDownButtons = 0;
static unsigned int gLastDownButtons = 0;
static char gWriteBuffer[(NUM_COLUMNS+1) * NUM_ROWS] = {0};
static char gDisplayBuffer[(NUM_COLUMNS+1) * NUM_ROWS] = {0};
static unsigned int gYPos = 0;
static bool gQuit = false;
static std::vector<char *> gPathList;

bool Common_Private_Test;
int Common_Private_Argc;
char** Common_Private_Argv;
void (*Common_Private_Update)(unsigned int*);
void (*Common_Private_Print)(const char*);
void (*Common_Private_Close)();

void Common_Init(void** /*extraDriverData*/)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
}

void Common_Close()
{
    CoUninitialize();

    for (std::vector<char *>::iterator item = gPathList.begin(); item != gPathList.end(); ++item)
    {
        free(*item);
    }
    if (Common_Private_Close)
    {
        Common_Private_Close();
    }
}

static unsigned int translateButton(unsigned int button)
{
    switch (button)
    {
        case '1':       return (1 << BTN_ACTION1);
        case '2':       return (1 << BTN_ACTION2);
        case '3':       return (1 << BTN_ACTION3);
        case '4':       return (1 << BTN_ACTION4);
        case VK_LEFT:   return (1 << BTN_LEFT);
        case VK_RIGHT:  return (1 << BTN_RIGHT);
        case VK_UP:     return (1 << BTN_UP);
        case VK_DOWN:   return (1 << BTN_DOWN);
        case VK_SPACE:  return (1 << BTN_MORE);
        case VK_ESCAPE: return (1 << BTN_QUIT);
        default:        return 0;
    }
}

void Common_Update()
{
    MSG msg = { };
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    gPressedButtons = (gLastDownButtons ^ gDownButtons) & gDownButtons;
    gPressedButtons |= (gQuit ? (1 << BTN_QUIT) : 0);
    gLastDownButtons = gDownButtons;

    memcpy(gDisplayBuffer, gWriteBuffer, sizeof(gWriteBuffer));
    InvalidateRect(gWindow, nullptr, FALSE);

    gYPos = 0;
    memset(gWriteBuffer, ' ', sizeof(gWriteBuffer));
    for (int i = 0; i < NUM_ROWS; i++)
    {
        gWriteBuffer[(i * (NUM_COLUMNS + 1)) + NUM_COLUMNS] = '\n';
    }

    if (Common_Private_Update)
    {
        Common_Private_Update(&gPressedButtons);
    }
}

void Common_Exit(int returnCode)
{
    exit(returnCode);
}

void Common_DrawText(const char *text)
{
    if (gYPos < NUM_ROWS)
    {
        char tempBuffer[NUM_COLUMNS + 1];
        Common_Format(tempBuffer, sizeof(tempBuffer), "%s", text);
        memcpy(&gWriteBuffer[gYPos * (NUM_COLUMNS + 1)], tempBuffer, strlen(tempBuffer));
        gYPos++;
    }
}

bool Common_BtnPress(Common_Button btn)
{
    return ((gPressedButtons & (1 << btn)) != 0);
}

bool Common_BtnDown(Common_Button btn)
{
    return ((gDownButtons & (1 << btn)) != 0);
}

const char *Common_BtnStr(Common_Button btn)
{
    switch (btn)
    {
        case BTN_ACTION1:   return "1";
        case BTN_ACTION2:   return "2";
        case BTN_ACTION3:   return "3";
        case BTN_ACTION4:   return "4";
        case BTN_LEFT:      return "Left";
        case BTN_RIGHT:     return "Right";
        case BTN_UP:        return "Up";
        case BTN_DOWN:      return "Down";
        case BTN_MORE:      return "Space";
        case BTN_QUIT:      return "Escape";
        default:            return "Unknown";
    }
}

const char *Common_MediaPath(const char *fileName)
{
    char *filePath = (char *)calloc(256, sizeof(char));

    static const char* pathPrefix = nullptr;
    if (!pathPrefix)
    {
        const char *emptyPrefix = "";
        const char *mediaPrefix = "../media/";
        FILE *file = fopen(fileName, "r");
        if (file)
        {
            fclose(file);
            pathPrefix = emptyPrefix;
        }
        else
        {
            pathPrefix = mediaPrefix;
        }
    }

    strcat(filePath, pathPrefix);
    strcat(filePath, fileName);

    gPathList.push_back(filePath);

    return filePath;
}

const char *Common_WritePath(const char *fileName)
{
	return Common_MediaPath(fileName);
}

void Common_TTY(const char *format, ...)
{
    char string[1024] = {0};

    va_list args;
    va_start(args, format);
    Common_vsnprintf(string, 1023, format, args);
    va_end(args);

    if (Common_Private_Print)
    {
        (*Common_Private_Print)(string);
    }
    else
    {
        OutputDebugStringA(string);
    }
}

HFONT CreateDisplayFont()
{
    return CreateFontA(22, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH, 0);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        HDC hdcBack = CreateCompatibleDC(hdc);
        
        HBITMAP hbmBack = CreateCompatibleBitmap(hdc, gScreenWidth, gScreenHeight);
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcBack, hbmBack);

        HFONT hfnt = CreateDisplayFont();
        HFONT hfntOld = (HFONT)SelectObject(hdcBack, hfnt);

        SetBkColor(hdcBack, RGB(0x0, 0x0, 0x0));
        SetTextColor(hdcBack, RGB(0xFF, 0xFF, 0xFF));
        DrawTextA(hdcBack, gDisplayBuffer, -1, &ps.rcPaint, 0);
        BitBlt(hdc, 0, 0, gScreenWidth, gScreenHeight, hdcBack, 0, 0, SRCCOPY);

        SelectObject(hdcBack, hfntOld);
        DeleteObject(hfnt);

        SelectObject(hdcBack, hbmOld);
        DeleteObject(hbmBack);

        DeleteDC(hdcBack);
        EndPaint(hWnd, &ps);
    }
    else if (message == WM_DESTROY)
    {
        gQuit = true;
    }
    else if (message == WM_GETMINMAXINFO)
    {
        if (gScreenWidth == 0)
        {
            HDC hdc = GetDC(hWnd);

            HFONT hfnt = CreateDisplayFont();
            HFONT hfntOld = (HFONT)SelectObject(hdc, hfnt);

            TEXTMETRICA metrics = { };
            GetTextMetricsA(hdc, &metrics);

            SelectObject(hdc, hfntOld);
            DeleteObject(hfnt);

            ReleaseDC(hWnd, hdc);

            RECT rec = { };
            rec.right = metrics.tmAveCharWidth * NUM_COLUMNS;
            rec.bottom = metrics.tmHeight * NUM_ROWS;

            BOOL success = AdjustWindowRect(&rec, WS_CAPTION | WS_SYSMENU, FALSE);
            assert(success);

            gScreenWidth = rec.right - rec.left;
            gScreenHeight = rec.bottom - rec.top;
        }

        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
        lpMMI->ptMinTrackSize.x = gScreenWidth;
        lpMMI->ptMinTrackSize.y = gScreenHeight;
        lpMMI->ptMaxTrackSize = lpMMI->ptMinTrackSize;
    }
    else if (message == WM_KEYDOWN)
    {
        gDownButtons |= translateButton((unsigned int)wParam);
    }
    else if (message == WM_KEYUP)
    {
        gDownButtons &= ~translateButton((unsigned int)wParam);
    }
    else
    {
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PSTR /*pCmdLine*/, int nCmdShow)
{
    const char CLASS_NAME[] = "FMOD Example Window Class";

    Common_Private_Argc = __argc;
    Common_Private_Argv = __argv;

    WNDCLASSA wc = { };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;

    ATOM atom = RegisterClassA(&wc);
    assert(atom);

    gWindow = CreateWindowA(CLASS_NAME, "FMOD Example", WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hInstance, nullptr);
    assert(gWindow);

    ShowWindow(gWindow, nCmdShow);

    return FMOD_Main();
}
