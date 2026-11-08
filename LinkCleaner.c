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

#define GLYPH_SIZE 48
#define GLYPH_PERCENT 90

static const unsigned char glyphAlpha[GLYPH_SIZE * GLYPH_SIZE] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,2,4,4,4,4,4,3,1,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,3,1,0,0,0,0,0,0,0,0,3,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,1,2,0,0,37,89,128,147,138,109,61,11,0,0,3,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,2,0,0,78,188,249,255,255,255,255,255,255,224,136,27,0,3,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    3,0,21,172,255,255,255,252,250,250,250,251,253,255,255,234,96,0,3,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,
    0,33,217,255,252,252,253,251,255,255,255,253,251,253,251,255,255,130,0,3,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,
    36,223,255,250,255,252,255,255,255,240,246,255,255,255,254,254,251,255,106,0,3,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,39,
    226,255,252,255,252,255,217,112,45,21,29,70,163,255,255,254,254,252,249,36,1,2,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,44,229,
    255,252,255,251,255,164,11,0,0,0,0,0,0,74,242,254,254,251,255,156,0,3,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,48,233,255,
    252,255,251,255,155,0,0,5,2,1,2,3,5,0,76,254,253,253,254,237,15,0,1,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,52,235,255,252,
    255,251,255,147,0,2,2,0,0,0,0,0,0,5,0,185,255,252,254,255,60,0,3,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,55,239,255,252,252,
    251,255,139,0,3,1,0,0,0,0,0,0,0,4,1,115,255,251,251,255,88,0,4,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,48,246,255,251,255,255,
    255,134,0,3,1,0,0,0,0,0,0,0,0,4,1,108,255,251,251,255,87,0,4,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,4,27,53,31,34,50,85,
    76,0,2,1,0,0,0,0,0,0,0,0,0,4,0,161,255,251,254,255,61,0,3,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,3,0,0,0,0,8,6,0,0,
    0,0,3,0,0,0,0,0,0,0,0,0,3,0,30,242,254,253,255,239,17,0,1,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,33,116,182,222,238,235,214,173,
    100,17,0,1,1,0,0,0,0,0,0,2,0,8,192,255,253,251,255,166,0,3,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,3,0,20,147,243,255,255,255,255,255,255,255,
    255,227,106,0,0,1,0,0,0,0,2,0,11,188,255,252,254,251,254,49,0,3,0,0,
    0,0,0,0,0,0,0,0,0,0,0,4,0,59,223,255,255,251,251,250,250,250,251,251,
    251,255,255,170,7,0,1,0,0,3,0,13,192,255,251,255,249,255,136,0,3,0,0,0,
    0,0,0,0,0,0,0,0,0,0,4,0,68,245,255,251,254,252,255,255,255,255,255,254,
    253,254,250,255,172,6,2,0,3,0,16,197,255,251,255,250,255,179,3,3,1,0,0,0,
    0,0,0,0,0,0,0,0,0,5,0,71,247,255,252,255,253,255,246,191,156,162,206,255,
    255,252,255,248,255,72,0,6,0,19,202,255,251,255,250,255,177,6,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,5,0,77,249,255,253,254,252,255,154,34,0,0,0,1,61,
    201,255,249,255,190,19,5,0,22,206,255,251,255,250,255,171,3,1,1,0,0,0,0,0,
    0,0,0,0,0,0,0,5,0,80,251,254,253,254,252,255,118,0,0,2,6,0,0,0,
    8,185,255,182,10,1,0,25,210,255,251,255,250,255,163,1,1,2,0,0,0,0,0,0,
    0,0,0,0,0,0,4,0,83,252,254,253,254,252,255,113,0,5,2,3,0,33,16,0,
    2,9,65,4,3,0,30,216,255,251,255,251,255,157,0,2,1,0,0,0,0,0,0,0,
    0,0,0,0,0,4,0,87,253,254,254,254,252,255,106,0,4,0,2,0,93,248,216,31,
    0,0,0,0,0,36,220,255,251,255,251,255,152,0,2,1,0,0,0,0,0,0,0,0,
    0,0,0,0,3,0,78,254,253,254,254,253,255,101,0,4,0,2,0,98,255,254,255,227,
    96,28,15,31,105,232,255,252,255,251,255,145,0,3,1,0,0,0,0,0,0,0,0,0,
    0,0,0,2,1,32,242,254,253,254,253,255,96,0,5,0,0,3,0,197,255,250,253,255,
    255,243,229,246,255,255,253,254,251,255,138,0,3,1,0,0,0,0,0,0,0,0,0,0,
    0,0,0,3,0,170,255,251,254,253,254,92,0,5,0,0,0,2,1,50,247,255,251,253,
    252,255,255,255,252,253,252,253,255,133,0,3,1,0,0,0,0,0,0,0,0,0,0,0,
    0,0,2,0,37,251,253,254,253,255,93,0,5,0,0,0,0,0,2,0,74,244,255,254,
    251,250,250,250,251,254,255,255,119,0,4,1,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,4,0,114,255,251,251,255,167,0,5,0,0,0,0,0,0,0,3,0,46,184,255,
    255,255,255,255,255,255,189,61,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,4,0,163,255,251,253,255,63,2,3,0,0,0,0,0,0,0,0,4,0,0,58,
    122,164,176,158,115,51,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,3,0,182,255,251,255,250,31,0,2,0,0,0,0,0,0,0,2,0,0,0,0,
    0,0,0,0,0,0,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,3,0,171,255,251,254,253,42,2,2,0,0,0,0,0,0,4,0,37,195,196,151,
    122,107,117,16,4,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,4,0,132,254,251,251,255,118,0,6,0,0,0,0,0,5,0,42,227,255,255,255,
    255,255,179,9,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,3,0,61,255,253,254,254,236,33,0,4,4,4,4,3,0,47,231,255,251,252,246,
    255,154,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,2,3,208,254,252,253,255,220,59,0,0,0,0,0,75,234,255,252,255,251,255,
    151,0,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,3,0,71,255,250,255,253,255,255,179,116,97,122,189,255,255,253,255,250,255,145,
    0,3,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,3,0,139,255,250,254,253,255,255,255,255,255,255,254,254,253,252,255,140,0,
    3,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,3,0,152,255,255,251,252,251,251,251,251,251,252,251,255,255,130,0,3,
    1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,1,2,0,106,234,255,255,255,254,253,255,255,255,255,221,82,0,3,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,2,0,25,123,203,244,255,255,255,239,192,107,14,0,3,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,3,0,0,0,28,53,60,48,21,0,0,0,3,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,1,4,0,0,0,0,0,0,1,4,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,2,3,3,2,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static void RenderIcon(DWORD *pixels, int size, DWORD rgb)
{
    int red = (int)((rgb >> 16) & 0xFF);
    int green = (int)((rgb >> 8) & 0xFF);
    int blue = (int)(rgb & 0xFF);
    int center = size * 8;
    int radius = center - 8;
    int glyphSpan = size * 16 * GLYPH_PERCENT / 100;
    int glyphStart = (size * 16 - glyphSpan) / 2;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int circle = 0;
            int glyph = 0;
            for (int sy = 0; sy < 8; sy++) {
                int py = y * 16 + sy * 2 + 1;
                int dy = py - center;
                int gy = py - glyphStart;
                for (int sx = 0; sx < 8; sx++) {
                    int px = x * 16 + sx * 2 + 1;
                    int dx = px - center;
                    int gx = px - glyphStart;
                    if (dx * dx + dy * dy <= radius * radius)
                        circle++;
                    if (gx >= 0 && gx < glyphSpan && gy >= 0 && gy < glyphSpan)
                        glyph += glyphAlpha[(gy * GLYPH_SIZE / glyphSpan) * GLYPH_SIZE + gx * GLYPH_SIZE / glyphSpan];
                }
            }
            int alpha = circle * 255 / 64;
            int white = glyph / 64;
            int r = red + (255 - red) * white / 255;
            int g = green + (255 - green) * white / 255;
            int b = blue + (255 - blue) * white / 255;
            pixels[y * size + x] = ((DWORD)alpha << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
        }
    }
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
        RenderIcon(pixels, size, rgb);
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

static void TrimMemory(void)
{
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
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
    TrimMemory();
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
            if (ProcessClipboard() || ++retries >= MAX_RETRIES) {
                KillTimer(window, TIMER_CLIPBOARD);
                TrimMemory();
            }
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
    iconOn = MakeIcon(0x0A0A0A);
    iconOff = MakeIcon(0x808080);
    UpdateTray(NIM_ADD);
    AddClipboardFormatListener(mainWindow);
    RegisterHotKey(mainWindow, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'L');
    TrimMemory();
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
