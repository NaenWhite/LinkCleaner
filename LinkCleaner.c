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
#define GLYPH_PERCENT 70

static const unsigned char glyphAlpha[GLYPH_SIZE * GLYPH_SIZE] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    3,0,0,0,31,70,96,100,78,42,4,0,0,3,1,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,
    0,12,105,193,240,255,255,255,255,248,211,139,40,0,0,2,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,3,0,
    83,221,255,255,255,254,253,253,254,255,255,255,248,141,9,0,2,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,3,0,135,
    255,255,251,252,253,251,251,251,251,252,253,251,255,255,203,27,0,2,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,3,0,145,255,
    251,253,255,252,255,255,255,255,255,255,253,254,255,250,255,212,17,2,1,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,3,0,150,255,250,
    255,255,253,255,236,168,120,113,147,210,255,255,253,255,250,255,172,0,3,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,2,0,154,255,251,255,
    254,253,255,138,23,0,0,0,0,3,89,233,255,253,255,251,255,67,0,3,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,162,255,250,255,254,
    253,255,103,0,0,3,4,4,4,1,0,42,232,253,253,251,254,180,0,3,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,166,255,250,255,253,254,
    255,95,0,5,1,0,0,0,0,0,6,0,84,255,252,254,255,240,17,0,1,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,169,255,249,254,253,254,253,
    87,0,5,0,0,0,0,0,0,0,1,2,3,222,255,253,254,255,55,0,3,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,1,2,16,184,255,255,255,255,255,251,81,
    0,5,0,0,0,0,0,0,0,0,0,3,1,182,255,252,253,254,70,0,3,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,119,228,200,207,221,242,232,75,0,
    5,0,0,0,0,0,0,0,0,0,0,2,0,195,255,252,253,255,62,0,3,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,1,4,2,3,0,0,0,0,24,21,0,3,
    0,0,0,0,0,0,0,0,0,0,2,2,22,241,255,253,255,249,31,0,2,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,14,41,57,50,25,0,0,0,3,
    1,0,0,0,0,0,0,0,0,0,6,0,143,255,252,252,254,207,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,1,2,0,15,101,185,233,255,255,255,245,211,142,47,0,
    0,2,0,0,0,0,0,0,0,5,0,93,254,252,255,251,255,115,0,4,2,4,3,0,
    0,0,0,0,0,0,0,0,2,0,0,98,224,255,255,255,255,254,254,255,255,255,252,151,
    16,0,2,0,0,0,0,0,4,0,99,255,253,254,253,254,230,15,1,2,0,0,0,1,
    0,0,0,0,0,0,0,2,0,8,169,255,255,251,252,252,251,251,251,252,253,251,254,255,
    212,34,0,2,0,0,0,4,0,103,255,253,254,254,251,255,79,0,4,0,36,105,65,0,
    0,0,0,0,0,0,2,0,10,187,255,250,253,254,252,255,255,255,255,255,253,253,255,250,
    255,217,25,1,1,0,4,0,109,255,252,254,254,251,255,125,0,6,2,44,241,255,255,118,
    0,0,0,0,0,2,0,12,190,255,250,255,254,255,255,235,182,156,167,214,255,255,253,255,
    247,255,133,0,5,4,0,115,255,252,254,254,251,255,125,0,4,3,0,167,255,245,253,215,
    0,0,0,0,2,0,15,195,255,251,255,253,255,247,118,20,0,0,0,4,73,211,255,248,
    255,227,46,2,6,0,120,255,251,254,254,252,255,119,0,4,3,1,39,249,253,252,255,125,
    0,0,0,3,0,16,199,255,251,255,252,255,242,63,0,0,3,6,0,1,0,15,196,255,
    222,40,0,6,0,126,255,251,254,254,252,255,111,0,4,0,3,0,157,255,249,254,226,13,
    0,0,3,0,18,202,255,251,255,252,255,239,58,0,5,1,3,0,12,0,4,0,14,94,
    26,0,7,0,133,255,251,255,254,253,255,104,0,4,0,2,1,31,247,254,251,255,115,0,
    0,2,0,20,204,255,251,255,252,255,235,53,0,4,0,2,0,90,230,170,2,0,0,0,
    0,0,0,140,255,251,255,254,253,255,99,0,5,0,0,3,0,147,255,250,254,233,15,1,
    2,2,17,207,255,251,255,252,255,233,48,0,4,0,2,1,97,255,255,255,184,44,0,0,
    0,37,164,255,251,255,254,253,255,92,0,5,0,0,2,1,27,244,254,250,255,122,0,4,
    3,0,182,255,250,255,252,255,230,44,0,4,0,1,0,16,252,253,252,252,255,251,200,177,
    196,247,255,252,255,253,254,253,87,0,4,0,0,0,4,0,139,255,250,254,234,17,1,1,
    0,94,255,250,255,253,255,227,41,0,4,0,0,0,3,2,153,255,249,255,252,255,255,255,
    255,255,252,255,251,255,251,81,0,5,2,4,4,5,4,23,240,254,250,255,126,0,4,0,
    6,214,254,253,253,254,227,38,0,4,0,0,0,0,1,1,6,186,255,252,251,253,252,251,
    252,253,252,252,255,244,75,0,6,3,0,0,0,0,0,131,255,250,254,237,19,1,1,0,
    70,255,254,255,253,251,57,0,4,0,0,0,0,0,0,1,0,8,153,255,255,255,255,255,
    255,255,255,255,196,49,0,6,0,0,35,98,141,155,145,238,254,250,255,127,0,3,0,0,
    137,255,252,252,255,177,0,4,0,0,0,0,0,0,0,0,1,0,0,64,165,224,250,255,
    251,227,174,83,0,0,4,0,22,160,247,255,255,255,255,255,254,255,242,18,0,3,0,0,
    173,255,252,251,255,106,1,4,0,0,0,0,0,0,0,0,0,1,0,0,0,6,34,43,
    35,11,0,0,0,4,1,47,236,255,252,248,250,251,251,254,255,254,251,188,29,0,2,0,
    177,255,252,251,255,93,1,4,0,0,0,0,0,0,0,1,0,0,77,64,25,3,0,0,
    0,0,3,3,1,3,4,69,193,241,255,255,255,252,252,254,255,255,254,255,217,20,2,2,
    151,254,252,251,255,138,0,4,0,0,0,0,0,0,1,2,0,156,255,255,241,222,213,217,
    36,1,2,0,0,1,0,0,0,32,108,188,246,255,255,255,252,252,254,249,255,165,0,3,
    95,255,253,253,254,228,12,1,4,0,0,0,0,3,1,1,164,255,250,254,255,255,255,107,
    2,2,0,1,3,0,130,135,52,0,0,0,33,110,190,246,255,255,255,251,250,251,33,0,
    22,238,254,254,252,255,168,0,0,1,3,3,1,0,3,169,255,250,255,253,252,253,91,0,
    2,0,0,5,0,124,255,255,255,208,134,54,0,0,0,33,109,187,245,255,255,255,90,0,
    0,142,255,251,255,252,255,185,52,0,0,0,0,56,192,255,251,255,253,254,253,86,0,4,
    0,0,5,0,101,255,250,251,255,255,255,255,207,132,53,0,0,0,30,106,181,245,104,0,
    2,20,230,255,252,255,252,255,255,202,164,167,204,255,255,252,255,253,254,251,81,0,5,0,
    0,5,0,93,255,252,254,255,255,253,251,255,255,255,255,209,136,57,0,0,0,29,21,0,
    3,0,63,251,255,251,255,252,255,255,255,255,255,255,253,255,251,255,249,76,0,5,0,1,
    4,0,97,255,253,254,255,255,255,255,255,255,253,251,255,255,255,255,212,139,60,0,0,0,
    0,3,0,75,241,255,252,252,253,252,251,251,252,253,251,253,255,233,67,0,4,0,3,0,
    0,116,255,253,254,254,255,255,255,255,255,255,255,255,255,253,251,254,255,255,255,89,1,4,
    0,0,3,0,42,184,255,255,255,255,254,255,255,255,255,255,173,31,0,4,0,4,0,6,
    152,255,252,254,253,255,247,252,255,255,255,255,255,255,254,255,255,255,253,249,255,71,0,3,
    0,0,0,3,0,0,70,162,222,249,255,255,247,216,152,61,0,0,3,0,0,0,50,201,
    255,251,255,252,255,221,161,253,253,255,255,255,255,254,255,255,255,255,254,255,250,32,0,2,
    0,0,0,0,2,1,0,0,7,33,50,48,31,4,0,0,2,2,1,1,16,132,245,255,
    251,255,252,255,229,58,218,255,253,255,255,255,255,255,245,255,255,255,253,255,223,6,0,1,
    0,0,0,0,0,0,3,3,0,0,0,0,0,0,3,3,0,0,3,0,55,255,255,254,
    251,250,255,229,28,127,255,251,255,255,255,253,255,200,190,255,253,255,252,255,187,0,2,0,
    0,0,0,0,0,0,0,0,1,2,2,2,2,1,0,0,0,0,0,1,0,39,154,245,
    255,255,217,29,39,255,252,254,255,255,255,252,253,72,220,255,254,255,251,255,143,0,4,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,34,
    132,158,21,10,212,252,249,253,255,255,251,255,135,41,255,254,255,255,251,255,96,0,4,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,3,0,
    0,0,0,56,244,255,255,255,255,249,254,226,3,124,255,251,255,255,254,255,54,0,2,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,
    4,3,2,0,22,89,163,222,255,255,255,64,2,207,254,250,252,251,252,240,18,0,1,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,7,46,101,82,0,27,243,255,255,255,255,255,202,0,1,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,4,3,0,0,0,0,2,2,44,76,100,113,119,115,49,0,2,0,0
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
    iconOn = MakeIcon(0x2ECC71);
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
