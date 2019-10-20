/*
 * MS Windows driver for QEmacs
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "qe.h"

#include <windows.h>
#include <windowsx.h> 

/* Probably need this if you don't have windowsx.h */
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp)  ((int)(short)(LOWORD(lp)))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)(HIWORD(lp)))
#endif

extern int main1(int argc, char **argv);
LRESULT CALLBACK qe_wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern QEDisplay win32_dpy;

static HINSTANCE _hPrev, _hInstance;
static int font_xsize;

/* state of a single window */
typedef struct WinWindow {
    HWND w;
    HDC hdc;
    HFONT font;
} WinWindow;

typedef struct QEEventQ {
    QEEvent ev;
    struct QEEventQ *next;
} QEEventQ;
 
QEEventQ *first_event, *last_event;
WinWindow win_ctx;

#define PROG_NAME "qemacs"

/* the main is there. We simulate a unix command line by parsing the
   windows command line */
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInst, 
                   LPSTR lpszCmdLine, int nCmdShow)
{
   char **argv;
   int argc, count;
   char *command_line, *p;

   command_line = malloc(strlen(lpszCmdLine) + sizeof(PROG_NAME) + 1);
   if (!command_line)
       return 0;
   strcpy(command_line, PROG_NAME " ");
   strcat(command_line, lpszCmdLine);
   _hPrev = hPrevInst;
   _hInstance = hInstance;

   p = command_line;
   count = 0;
   for(;;) {
       skip_spaces((const char **)&p);
       if (*p == '\0')
           break;
       while (*p != '\0' && !css_is_space(*p))
           p++;
      count++;
   }

   argv = (char **)malloc( (count + 1) * sizeof(char *) );
   if (!argv)
       return 0;
   
   argc = 0;
   p = command_line;
   for(;;) {
       skip_spaces((const char **)&p);
       if (*p == '\0')
           break;
       argv[argc++] = p;
       while (*p != '\0' && !css_is_space(*p))
           p++;
       if (*p == '\0')
           break;
       *p = '\0';
       p++;
   }

   argv[argc] = NULL;

#if 0
   {
       int i;
       for(i=0;i<argc;i++) {
           printf("%d: '%s'\n", i, argv[i]);
       }
   }
#endif

   return main1(argc, argv);
}

static int win_probe(void)
{
    return 1;
}

static void init_application(void)
{
    WNDCLASS wc;
    
    wc.style = 0;
    wc.lpfnWndProc = qe_wnd_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = _hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "qemacs";
    
    RegisterClass(&wc);
}

static int win_init(QEditScreen *s, int w, int h)
{
    int xsize, ysize, font_ysize;
    TEXTMETRIC tm;
    HDC hdc;
    HWND desktop_hwnd;
    
    if (!_hPrev) 
        init_application();

    memcpy(&s->dpy, &win32_dpy, sizeof(QEDisplay));

    s->private = NULL;
    s->media = CSS_MEDIA_SCREEN;
    
    win_ctx.font = CreateFont(-12, 0, 0, 0, 0 , 0, 0, 0, 0, 0, 0, 0, 
                              FIXED_PITCH, "fixed");

    /* get font metric for window size */
    desktop_hwnd = GetDesktopWindow();
    hdc = GetDC(desktop_hwnd);
    SelectObject(hdc, win_ctx.font);
    GetTextMetrics(hdc, &tm);
    ReleaseDC(desktop_hwnd, hdc);

    font_xsize = tm.tmAveCharWidth;
    font_ysize = tm.tmHeight;

    xsize = w * font_xsize; /* xsize = 80 * font_xsize; */
    ysize = h * font_ysize; /* ysize = 25 * font_ysize; */

    s->width = xsize;
    s->height = ysize;
    s->charset = &charset_utf8;

    s->clip_x1 = 0;
    s->clip_y1 = 0;
    s->clip_x2 = s->width;
    s->clip_y2 = s->height;

    win_ctx.w = CreateWindow("qemacs", "qemacs", WS_OVERLAPPEDWINDOW, 
      CW_USEDEFAULT, CW_USEDEFAULT, xsize, ysize, NULL, NULL, _hInstance, NULL);

    win_ctx.hdc = GetDC(win_ctx.w);
    SelectObject(win_ctx.hdc, win_ctx.font);

    //    SetWindowPos (win_ctx.w, NULL, 0, 0, xsize, ysize, SWP_NOMOVE);

    ShowWindow(win_ctx.w, SW_SHOW);
    UpdateWindow(win_ctx.w);
    
    return 0;
}

static void win_close(QEditScreen *s)
{
    ReleaseDC(win_ctx.w, win_ctx.hdc);
    DestroyWindow(win_ctx.w);
    DeleteObject(win_ctx.font);
}

static void win_flush(QEditScreen *s)
{
}

static int win_is_user_input_pending(QEditScreen *s)
{
    /* The tty.c code does a zero timeout select on the tty. */
    /* So this should do a Windows PeekMessage. */
    MSG msg;

    /* Consider using WM_KEYFIRST, WM_KEYLAST instead of 0,0 for keys only. */
    if (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
        return 1;
   
    return 0;
}

static void push_event(QEEvent *ev)
{
    QEEventQ *e;
    
    e = malloc(sizeof(QEEventQ));
    if (!e)
        return;
    e->ev = *ev;
    e->next = NULL;
    if (!last_event)
        first_event = e;
    else
        last_event->next = e;
    last_event = e;
}

static void push_key(int key)
{
    QEEvent ev;
    ev.type = QE_KEY_EVENT;
    ev.key_event.key = key;
    push_event(&ev);
}

static int ignore_wchar_msg = 0;

LRESULT CALLBACK qe_wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    //    printf("msg=%d\n", msg);
    switch (msg) {
    case WM_CLOSE:
        /* if (MessageBox(hWnd, L"Really Quit?", L"qemacs", MB_OKCANCEL) == IDOK) */
        {
        DestroyWindow(hWnd);
        url_exit();
        return 0;
        }
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        {
	QEEvent ev;
        ev.button_event.button = QE_BUTTON_LEFT;
        if (msg == WM_LBUTTONDOWN)
            ev.button_event.type = QE_BUTTON_PRESS_EVENT;
        else
            ev.button_event.type = QE_BUTTON_RELEASE_EVENT;
        ev.button_event.x = GET_X_LPARAM(lParam);
        ev.button_event.y = GET_Y_LPARAM(lParam);
        push_event(&ev); /* qe_handle_event(ev); */
        }
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        {
	QEEvent ev;
        ev.button_event.button = QE_BUTTON_MIDDLE;
        if (msg == WM_MBUTTONDOWN)
            ev.button_event.type = QE_BUTTON_PRESS_EVENT;
        else
            ev.button_event.type = QE_BUTTON_RELEASE_EVENT;
        ev.button_event.x = GET_X_LPARAM(lParam);
        ev.button_event.y = GET_Y_LPARAM(lParam);
        push_event(&ev); /* qe_handle_event(ev); */
        }
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        {
	QEEvent ev;
        ev.button_event.button = QE_BUTTON_RIGHT;
        if (msg == WM_RBUTTONDOWN)
            ev.button_event.type = QE_BUTTON_PRESS_EVENT;
        else
            ev.button_event.type = QE_BUTTON_RELEASE_EVENT;
        ev.button_event.x = GET_X_LPARAM(lParam);
        ev.button_event.y = GET_Y_LPARAM(lParam);
        push_event(&ev); /* qe_handle_event(ev); */
        }
        break;
    case WM_MOUSEWHEEL:
        {
	QEEvent ev;
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (zDelta < 0) 
	    ev.button_event.button = QE_WHEEL_DOWN;
        else /* if (zDelta > 0) */
	    ev.button_event.button = QE_WHEEL_UP;
        ev.button_event.type = QE_BUTTON_PRESS_EVENT;
        ev.button_event.x = GET_X_LPARAM(lParam);
        ev.button_event.y = GET_Y_LPARAM(lParam);
        push_event(&ev); /* qe_handle_event(ev); */
        }
        break;
    case WM_MOUSEMOVE: 
        {
	QEEvent ev;
        ev.button_event.type = QE_MOTION_EVENT;
        ev.button_event.x = GET_X_LPARAM(lParam);
        ev.button_event.y = GET_Y_LPARAM(lParam);
        push_event(&ev); /* qe_handle_event(ev); */
        }
        break;
#if 0
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_HELP_ABOUT:
          {
            return 0;
            DialogBox(hInstance, MAKEINTRESOURCE(IDD_ABOUTDIALOG), hWnd, &AboutDialogProc);
          }
        case ID_FILE_EXIT:
          {
            DestroyWindow(hWnd);
            url_exit();
            return 0;
          }
        }
        break;
#endif
    case WM_CREATE:
        /* NOTE: must store them here to avoid problems in main */
        win_ctx.w = hWnd;
        return 0;
        
        /* key handling */
    case WM_CHAR:
        if (!ignore_wchar_msg) {
            push_key(wParam);
        } else {
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }
        break;
    case WM_SYSCHAR:
        if (!ignore_wchar_msg) {
            int key;
            key = wParam;
            if (key >= ' ' && key <= '~') {
                key = KEY_META(' ') + key - ' ';
                push_key(key);
                break;
            }
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
        {
            unsigned int scan;
            int ctrl, shift, alt, key;
            
            ctrl = (GetKeyState(VK_CONTROL) & 0x8000);
            shift = (GetKeyState(VK_SHIFT) & 0x8000);
            alt = (GetKeyState(VK_MENU) & 0x8000);
            
            ignore_wchar_msg = 0;
            
            scan = (unsigned int) ((lParam >> 16) & 0x1FF);
            switch(scan) {
            case 0x00E:
                ignore_wchar_msg = 1;
                push_key(KEY_BACKSPACE);
                break;
            case 0x039: /* space */
                ignore_wchar_msg = 1;
                if (!ctrl) 
                    push_key(KEY_SPC);
                else
                    push_key(KEY_CTRL('@'));
                break;
            case 0x147: 		       /* home */
                push_key(KEY_HOME);
                break;
            case 0x148:		       /* UP */
                push_key(KEY_UP);
                break;
            case 0x149:		       /* PGUP */
                push_key(KEY_PAGEUP);
                break;
            case 0x14B:		       /* LEFT */
                push_key(KEY_LEFT);
                break;
            case 0x14D:		       /* RIGHT */
                push_key(KEY_RIGHT);
                break;
            case 0x14F:		       /* END */
                push_key(KEY_END);
                break;
            case 0x150:		       /* DOWN */
                push_key(KEY_DOWN);
                break;
            case 0x151:		       /* PGDN */
                push_key(KEY_PAGEDOWN);
                break;
            case 0x153:		       /* DEL */
                push_key(KEY_DELETE);
                break;
            case 0x152:		       /* INSERT */
                push_key(KEY_INSERT);
                break;
            case 0x3b:		       /* F1 */
            case 0x3c:
            case 0x3d:
            case 0x3e:
            case 0x3f:
            case 0x40:
            case 0x41:
            case 0x42:
            case 0x43:
            case 0x44:
            case 0x57:
            case 0x58:		       /* F12 */
                key = scan - 0x3b;
                if (key > 9)
                    key -= 0x12;
                key += KEY_F1;
                /* we leave Alt-F4 to close the window */
                if (alt && key == KEY_F4)
                    return DefWindowProc(hWnd, msg, wParam, lParam);
                push_key(key);
                break;
                  
            default: 
                return DefWindowProc(hWnd, msg, wParam, lParam);
            }
        }
        break;

    case WM_KEYUP:
        ignore_wchar_msg = 0;
        break;
          
    case WM_SYSKEYUP:
        ignore_wchar_msg = 0;
        return DefWindowProc(hWnd, msg, wParam, lParam);

    case WM_SIZE:
	if (wParam != SIZE_MINIMIZED) {
            QEmacsState *qs = &qe_state;
            QEEvent ev;

            qs->screen->width = LOWORD(lParam);
            qs->screen->height = HIWORD(lParam);
            ev.expose_event.type = QE_EXPOSE_EVENT;
            push_event(&ev);
        }
	break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC saved_hdc;
            QEEvent ev;

            BeginPaint(win_ctx.w, &ps);
            saved_hdc = win_ctx.hdc;
            win_ctx.hdc = ps.hdc;
            SelectObject(win_ctx.hdc, win_ctx.font);
            do_refresh(NULL);
            
            EndPaint(win_ctx.w, &ps);
            win_ctx.hdc = saved_hdc;

	    ev.expose_event.type = QE_EXPOSE_EVENT;
	    push_event(&ev);
        }
	break;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        break;

    default:
	return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

#if 0
static int get_unicode_key(QEditScreen *s, QEPollData *pd, QEEvent *ev)
{
    MSG msg;
    QEEventQ *e;

    for(;;) {
        /* check if events queued */
        if (first_event != NULL) {
            e = first_event;
            *ev = e->ev;
            first_event = e->next;
            if (!first_event)
                last_event = NULL;
            free(e);
            break;
        }

        /* check if message queued */
        if (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return 1;
}
#endif

static void win_fill_rectangle(QEditScreen *s,
                               int x1, int y1, int w, int h, QEColor color)
{
   RECT rc;
   HBRUSH hbr;
   COLORREF col;
   QEColor clr = color;

   /* XXX: suppress XOR mode */
   if (color == QECOLOR_XOR)
       color = QERGB(0xff, 0xff, 0xff);

   SetRect(&rc, x1, y1, x1 + w, y1 + h);
   col = RGB((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
   hbr = CreateSolidBrush(col);
   if (clr == QECOLOR_XOR)
     FrameRect(win_ctx.hdc, &rc, hbr); /* Cheap cursor fix (unfilled rect) */
   else
   FillRect(win_ctx.hdc, &rc, hbr);
   DeleteObject(hbr);
}

static QEFont *win_open_font(QEditScreen *s, int style, int size)
{
    QEFont *font;
    TEXTMETRIC tm;

    font = malloc(sizeof(QEFont));
    if (!font)
        return NULL;
    GetTextMetrics(win_ctx.hdc, &tm);
    font->ascent = tm.tmAscent;
    font->descent = tm.tmDescent;
    font->private = NULL;
    return font;
}

static void win_close_font(QEditScreen *s, QEFont *font)
{
    free(font);
}

static void win_text_metrics(QEditScreen *s, QEFont *font, 
                             QECharMetrics *metrics,
                             const unsigned int *str, int len)
{
    int i, x;
    metrics->font_ascent = font->ascent;
    metrics->font_descent = font->descent;
    x = 0;
    for(i=0;i<len;i++)
        x += font_xsize;
    metrics->width = x;
}

static void win_draw_text(QEditScreen *s, QEFont *font,
                          int x1, int y, const unsigned int *str, int len,
                          QEColor color)
{
    int i;
    WORD buf[len];
    COLORREF col;
    RECT rc;

    for(i=0;i<len;i++)
        buf[i] = str[i];
    col = RGB((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
    SetTextColor(win_ctx.hdc, col);
    SetBkMode(win_ctx.hdc, TRANSPARENT);
    /* Clip text to window or it bleeds into mode line and around boxes. */
    SetRect(&rc, s->clip_x1, s->clip_y1, s->clip_x2,s->clip_y2);
    ExtTextOutW(win_ctx.hdc, x1, y - font->ascent, ETO_CLIPPED, &rc, buf, len, 0);
}

static void win_set_clip(QEditScreen *s,
                         int x, int y, int w, int h)
{
    /* nothing to do */
}

/* Stubs for code from unix.c */
static int url_exit_request;

void register_bottom_half(void (*cb)(void *opaque), void *opaque)
{
}

void url_main_loop(void (*init)(void *opaque), void *opaque)
{
    HACCEL hAccelerators;
    MSG msg;
    QEEventQ *e;
    QEEvent ev1, *ev = &ev1;

    init(opaque);

    /* Load Accelerators */
    /* hAccelerators = LoadAccelerators(_hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR1)); */

    /* Main message loop */
    for(;;) {
        /* Handle any events queued before we dispatch more stuff. */
        while (first_event != NULL) {
            e = first_event;
            *ev = e->ev;
            first_event = e->next;
            if (!first_event)
                last_event = NULL;
            free(e);
	    
	    /* Try to compress expose events on pop from queue */
	    e = NULL;
	    if (ev->type == QE_EXPOSE_EVENT) {
	        for (e = first_event; e != NULL; e - e->next)
	            if (e->ev.type == QE_EXPOSE_EVENT) 
		        break;
	    }
	    if (e == NULL) /* Process this ev if it's not an EXPOSE dup. */
	        qe_handle_event(ev);
        }

        if (url_exit_request)
            break;

        /* check if message queued */
        if (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

    }
}

/* exit from url loop */
void url_exit(void)
{
    url_exit_request = 1;
}

/* Stubs for code from dired.c */
void do_dired(EditState *s)
{
    /* Dired code needs a bit of work for Windows */
}

/* Stubs for code from shell.c */
EditBuffer *new_shell_buffer(const char *name, const char *path, char **argv, int is_shell)
{
    return NULL;
}

/* End of Stubs. */

static void win_clipboard_activate(QEditScreen *s)
{
    /* What about UTF-8?  Also, maybe add CR to LF? */
    if (OpenClipboard(NULL)) {
        HGLOBAL hglb;
        QEmacsState *qs = &qe_state;
        unsigned char *buf;
        EditBuffer *b;

        EmptyClipboard();
        if (b = qs->yank_buffers[qs->yank_current]) {
            hglb = GlobalAlloc(GMEM_DDESHARE, b->total_size +1);
            buf = (unsigned char*)GlobalLock(hglb);
            eb_read(b, 0, buf, b->total_size);
            GlobalUnlock(hglb);
            SetClipboardData(CF_TEXT, hglb);
        }
        CloseClipboard();
    }
}

/* request Windows clipboard text and put it in a new yank buffer if needed */
static void win_clipboard_request(QEditScreen *s)
{
    HGLOBAL hglb;
    LPTSTR   lptstr;
    EditBuffer *b;

    /* What about UTF-8?  Also, strip CR from CRLF pairs? */
    if (!IsClipboardFormatAvailable(CF_TEXT))
        return;
    if (!OpenClipboard(NULL))
        return;
    hglb = GetClipboardData(CF_TEXT);
    if (hglb != NULL) {
        lptstr = GlobalLock(hglb);
        if (lptstr != NULL) {
            /* copy GUI selection to a new yank buffer */
            b = new_yank_buffer();
            eb_write(b, 0, lptstr, strlen(lptstr));
            GlobalUnlock(hglb);
        }
    }
    CloseClipboard();
}

static QEDisplay win32_dpy = {
    "win32",
    win_probe,
    win_init,
    win_close,
    NULL,
    win_flush,
    win_is_user_input_pending,
    win_fill_rectangle,
    win_open_font,
    win_close_font,
    win_text_metrics,
    win_draw_text,
    win_set_clip,
    win_clipboard_activate,
    win_clipboard_request,
};

int win32_init(void)
{
    return qe_register_display(&win32_dpy);
}

qe_module_init(win32_init);
