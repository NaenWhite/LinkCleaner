#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <wctype.h>

#define WM_TRAY (WM_APP + 1)
#define ID_TOGGLE 1
#define ID_AUTOSTART 2
#define ID_EXIT 3
#define TIMER_CLIPBOARD 1
#define MAX_URL 8192
#define MAX_RETRIES 10

static const wchar_t *const exactKeys[] = {
    L"fbclid", L"gclid", L"gclsrc", L"dclid", L"gbraid", L"wbraid", L"msclkid",
    L"yclid", L"twclid", L"ttclid", L"igshid", L"igsh", L"mc_cid", L"mc_eid",
    L"_hsenc", L"_hsmi", L"hsctatracking", L"mkt_tok", L"ref_src", L"ref_url",
    L"si", L"spm", L"scid", L"vero_id", L"oly_enc_id", L"oly_anon_id", L"s_cid",
    L"cmpid", L"trk", L"trkcampaign", L"sc_channel", L"_ga", L"_gl", L"wickedid",
    L"rb_clickid", L"li_fat_id", L"epik", L"ocid", L"ncid", L"soc_src",
    L"soc_trk", L"share_id", L"sharetype", L"sender_device", L"xmt"
};

static const wchar_t *const prefixKeys[] = {
    L"utm_", L"pk_", L"mtm_", L"hsa_", L"ga_"
};

static BOOL IsTracking(const wchar_t *key, size_t length)
{
    for (size_t i = 0; i < ARRAYSIZE(exactKeys); i++) {
        if (wcslen(exactKeys[i]) == length && _wcsnicmp(key, exactKeys[i], length) == 0)
            return TRUE;
    }
    for (size_t i = 0; i < ARRAYSIZE(prefixKeys); i++) {
        size_t prefixLength = wcslen(prefixKeys[i]);
        if (length >= prefixLength && _wcsnicmp(key, prefixKeys[i], prefixLength) == 0)
            return TRUE;
    }
    return FALSE;
}

static void BuildCleanUrl(const wchar_t *url, wchar_t *out)
{
    const wchar_t *fragment = wcschr(url, L'#');
    const wchar_t *end = fragment ? fragment : url + wcslen(url);
    const wchar_t *query = url;
    while (query < end && *query != L'?')
        query++;
    if (query == end) {
        wcscpy(out, url);
        return;
    }
    size_t length = (size_t)(query - url);
    wmemcpy(out, url, length);
    BOOL first = TRUE;
    const wchar_t *part = query + 1;
    while (part <= end) {
        const wchar_t *next = part;
        while (next < end && *next != L'&')
            next++;
        if (next > part) {
            const wchar_t *equals = part;
            while (equals < next && *equals != L'=')
                equals++;
            if (!IsTracking(part, (size_t)(equals - part))) {
                out[length++] = first ? L'?' : L'&';
                first = FALSE;
                wmemcpy(out + length, part, (size_t)(next - part));
                length += (size_t)(next - part);
            }
        }
        part = next + 1;
    }
    out[length] = 0;
    if (fragment)
        wcscpy(out + length, fragment);
}

static BOOL IsUrl(const wchar_t *text)
{
    if (_wcsnicmp(text, L"http://", 7) != 0 && _wcsnicmp(text, L"https://", 8) != 0)
        return FALSE;
    for (const wchar_t *p = text; *p; p++) {
        if (iswspace(*p))
            return FALSE;
    }
    return TRUE;
}

static wchar_t *Trim(wchar_t *text)
{
    while (*text && iswspace(*text))
        text++;
    size_t length = wcslen(text);
    while (length > 0 && iswspace(text[length - 1]))
        text[--length] = 0;
    return text;
}

static const wchar_t RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t APP_NAME[] = L"LinkCleaner";

static HWND mainWindow;
static HICON iconOn;
static HICON iconOff;
static BOOL enabled = TRUE;
static UINT taskbarCreated;
static int retries;
static wchar_t source[MAX_URL + 1];
static wchar_t cleaned[MAX_URL + 1];

static BOOL ProcessClipboard(void)
{
    if (!OpenClipboard(mainWindow))
        return FALSE;
    BOOL haveText = FALSE;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const wchar_t *text = (const wchar_t *)GlobalLock(data);
        if (text) {
            if (wcslen(text) <= MAX_URL) {
                wcscpy(source, text);
                haveText = TRUE;
            }
            GlobalUnlock(data);
        }
    }
    if (haveText) {
        wchar_t *url = Trim(source);
        if (IsUrl(url)) {
            BuildCleanUrl(url, cleaned);
            if (wcscmp(url, cleaned) != 0) {
                size_t bytes = (wcslen(cleaned) + 1) * sizeof(wchar_t);
                HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (memory) {
                    void *target = GlobalLock(memory);
                    if (target) {
                        memcpy(target, cleaned, bytes);
                        GlobalUnlock(memory);
                        EmptyClipboard();
                        if (!SetClipboardData(CF_UNICODETEXT, memory))
                            GlobalFree(memory);
                    } else {
                        GlobalFree(memory);
                    }
                }
            }
        }
    }
    CloseClipboard();
    return TRUE;
}

static HICON MakeIcon(DWORD rgb)
{
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size < 16)
        size = 16;
    if (size > 64)
        size = 64;
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    DWORD *pixels = NULL;
    HBITMAP color = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    BYTE maskBits[512] = {0};
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits);
    HICON icon = NULL;
    if (color && mask && pixels) {
        int center = size * 4;
        int radius = center - 4;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                int covered = 0;
                for (int sy = 0; sy < 4; sy++) {
                    for (int sx = 0; sx < 4; sx++) {
                        int dx = x * 8 + sx * 2 + 1 - center;
                        int dy = y * 8 + sy * 2 + 1 - center;
                        if (dx * dx + dy * dy <= radius * radius)
                            covered++;
                    }
                }
                DWORD alpha = (DWORD)(covered * 255 / 16);
                pixels[y * size + x] = (alpha << 24) | rgb;
            }
        }
        ICONINFO iconInfo = {TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&iconInfo);
    }
    if (color)
        DeleteObject(color);
    if (mask)
        DeleteObject(mask);
    return icon ? icon : LoadIconW(NULL, IDI_APPLICATION);
}

static void UpdateTray(DWORD message)
{
    NOTIFYICONDATAW data = {0};
    data.cbSize = sizeof(data);
    data.hWnd = mainWindow;
    data.uID = 1;
    data.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    data.uCallbackMessage = WM_TRAY;
    data.hIcon = enabled ? iconOn : iconOff;
    lstrcpyW(data.szTip, enabled ? L"Link Cleaner: ON" : L"Link Cleaner: OFF");
    Shell_NotifyIconW(message, &data);
}

static void Toggle(void)
{
    enabled = !enabled;
    UpdateTray(NIM_MODIFY);
}

static BOOL AutostartEnabled(void)
{
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME, RRF_RT_REG_SZ, NULL, NULL, NULL) == ERROR_SUCCESS;
}

static void SetAutostart(BOOL on)
{
    if (!on) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME);
        return;
    }
    wchar_t path[MAX_PATH + 3];
    DWORD length = GetModuleFileNameW(NULL, path + 1, MAX_PATH);
    if (length == 0)
        return;
    path[0] = L'"';
    path[length + 1] = L'"';
    path[length + 2] = 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME, REG_SZ, path, (length + 3) * sizeof(wchar_t));
}

static void ShowMenu(void)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (enabled ? MF_CHECKED : 0), ID_TOGGLE, L"Enabled");
    AppendMenuW(menu, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), ID_AUTOSTART, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(mainWindow);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0, mainWindow, NULL);
    PostMessageW(mainWindow, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CLIPBOARDUPDATE:
        if (enabled && GetClipboardOwner() != window) {
            retries = 0;
            SetTimer(window, TIMER_CLIPBOARD, 30, NULL);
        }
        return 0;
    case WM_TIMER:
        if (wParam == TIMER_CLIPBOARD) {
            if (ProcessClipboard() || ++retries >= MAX_RETRIES)
                KillTimer(window, TIMER_CLIPBOARD);
        }
        return 0;
    case WM_TRAY:
        if (lParam == WM_LBUTTONUP)
            Toggle();
        else if (lParam == WM_RBUTTONUP)
            ShowMenu();
        return 0;
    case WM_HOTKEY:
        Toggle();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TOGGLE:
            Toggle();
            break;
        case ID_AUTOSTART:
            SetAutostart(!AutostartEnabled());
            break;
        case ID_EXIT:
            DestroyWindow(window);
            break;
        }
        return 0;
    case WM_DESTROY:
        RemoveClipboardFormatListener(window);
        UnregisterHotKey(window, 1);
        UpdateTray(NIM_DELETE);
        PostQuitMessage(0);
        return 0;
    }
    if (message == taskbarCreated) {
        UpdateTray(NIM_ADD);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    (void)previous;
    (void)command;
    (void)show;
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"LinkCleanerSingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW windowClass = {0};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = APP_NAME;
    RegisterClassW(&windowClass);
    mainWindow = CreateWindowExW(0, APP_NAME, APP_NAME, WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (!mainWindow)
        return 1;
    iconOn = MakeIcon(0x2ECC71);
    iconOff = MakeIcon(0x808080);
    UpdateTray(NIM_ADD);
    AddClipboardFormatListener(mainWindow);
    RegisterHotKey(mainWindow, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'L');
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
