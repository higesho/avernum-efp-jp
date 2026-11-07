// Avernum: Escape From the Pit - Japanese text rendering hook (prototype)
//
// Built as a proxy dsound.dll (DirectSoundCreate is passed through to the system dsound.dll).
// On load it replaces the game's "draw text in rect" routine (VA 0x493e80) with
// a UTF-8 aware version. ASCII runs are still drawn with the game's own bitmap
// font; CJK characters are rasterised with GDI into extra textures and drawn
// through the game's own blit routine so clipping/colour/scaling match.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_TEXTURE_RECTANGLE
#define GL_TEXTURE_RECTANGLE 0x84F5
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// ---------------------------------------------------------------- addresses
static BYTE *g_base;
#define VA(x) ((void *)(g_base + ((DWORD)(x) - 0x400000)))

#define A_DRAW_TEXT_RECT 0x493e80 /* replaced */
#define A_DRAW_ASCII 0x496500     /* cdecl (char*, r, g, b), pen at globals */
#define A_BLIT 0x406a90           /* edi = texture obj, cdecl 7 args */
#define A_OFFSET_RECT 0x498060    /* eax = rect*, cdecl (a, b) */
#define A_PEN_X 0x6a9b94
#define A_PEN_Y 0x6a9b98
#define A_CUR_FONT 0x6a9b90
#define A_LINE_ACC 0x6a9980
#define A_RECT_OFF_X 0x6a9984
#define A_RECT_OFF_Y 0x6a9b88
#define A_ORIGIN_A 0x6a90a8
#define A_ORIGIN_B 0x6a90ac
#define A_COLOR_R 0x5fffa4
#define A_COLOR_G 0x5fffa8
#define A_COLOR_B 0x5fffac
#define A_CHARMAP 0x5ed020
#define A_FONT_TEX 0x6a9da0
#define A_BOUND_TEX_CACHE 0x5ec09c
#define A_FONT0 0x5ec0c8
#define FONT_STRIDE 0x310

#define TEXOBJ_SIZE 0x170
#define TEXOBJ_GLID 0x2c
#define TEXOBJ_PIXELS 0x134
#define TEXOBJ_W 0x13c
#define TEXOBJ_H 0x140
#define TEXOBJ_TARGET 0x144

static const BYTE kExpectedPrologue[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x3C, 0x08, 0x00, 0x00};

// ---------------------------------------------------------------- logging
static FILE *g_log;
static void jp_log(const char *fmt, ...)
{
    va_list ap;
    if (!g_log)
        return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

// ---------------------------------------------------------------- config
static char g_fontFace[64] = "Meiryo";
static int g_fontWeight = FW_BOLD;
static float g_alphaGamma = 0.7f; // <1 = darker/thicker strokes
static int g_sizeAdjust = 0;   // px added to CJK em size
static int g_yAdjust = 0;      // px added to CJK vertical placement
static int g_cjkSpacing = 0;   // extra px between CJK glyphs
static int g_eucjp = 1;        // text encoding of game files: 1 = EUC-JP, 0 = UTF-8
static int g_lineGap = 1;      // min px between the CJK em box and the next line

static void load_config(const char *dir)
{
    char ini[MAX_PATH], enc[16];
    _snprintf(ini, sizeof ini, "%s\\avernum_jp.ini", dir);
    GetPrivateProfileStringA("text", "encoding", "eucjp", enc, sizeof enc, ini);
    g_eucjp = _stricmp(enc, "utf8") != 0 && _stricmp(enc, "utf-8") != 0;
    GetPrivateProfileStringA("font", "face", g_fontFace, g_fontFace, sizeof g_fontFace, ini);
    g_fontWeight = GetPrivateProfileIntA("font", "weight", g_fontWeight, ini);
    g_sizeAdjust = GetPrivateProfileIntA("font", "size_adjust", g_sizeAdjust, ini);
    g_yAdjust = GetPrivateProfileIntA("font", "y_adjust", g_yAdjust, ini);
    g_cjkSpacing = GetPrivateProfileIntA("font", "spacing", g_cjkSpacing, ini);
    g_lineGap = GetPrivateProfileIntA("font", "line_gap", g_lineGap, ini);
    {
        char buf[32];
        GetPrivateProfileStringA("font", "alpha_gamma", "0.7", buf, sizeof buf, ini);
        g_alphaGamma = (float)atof(buf);
        if (g_alphaGamma <= 0.05f)
            g_alphaGamma = 1.0f;
    }
}

// ---------------------------------------------------------------- game font helpers
static short fs(const BYTE *font, int off) { return *(const short *)(font + off); }
static int font_index(const BYTE *font)
{
    int i = (int)((DWORD)font - (DWORD)VA(A_FONT0)) / FONT_STRIDE;
    return (i < 0 || i > 4) ? 0 : i;
}
static int glyph_of(unsigned char c) { return ((short *)VA(A_CHARMAP))[c]; }
static int box_h(const BYTE *font) { return fs(font, 8) - fs(font, 4); }

// Width of an ASCII run, mirroring the game's own position routine (0x496790).
static int ascii_width(const BYTE *font, const unsigned char *s, int n)
{
    int pos = 0, i;
    for (i = 0; i < n; i++)
    {
        int g = glyph_of(s[i]);
        if (i > 0)
        {
            int gp = glyph_of(s[i - 1]);
            short kc = fs(font, 0xc + g * 8 + 6), kp = fs(font, 0xc + gp * 8 + 4);
            if (kc > 0 && kp > 0)
                pos -= kp;
            else
                pos += fs(font, 0xa);
        }
        pos += fs(font, 0xc + g * 8 + 2) - fs(font, 0xc + g * 8 + 0);
    }
    return pos;
}

// ---------------------------------------------------------------- CJK glyph cache
typedef struct
{
    DWORD key; // (fontIdx << 24) | codepoint, 0 = empty
    unsigned char page;
    short x, y, w, h, adv;
} CGlyph;

#define CACHE_SIZE 16384
#define MAX_PAGES 32
static CGlyph g_cache[CACHE_SIZE];
static BYTE *g_pages[MAX_PAGES]; // cloned texture objects
static int g_pageCount;
static int g_texW, g_texH;
static GLenum g_target;
static int g_curX, g_curY, g_shelfH = 32;
static HDC g_dc;
static HBITMAP g_dib;
static DWORD *g_dibBits;
#define DIB_W 128
#define DIB_H 64
static HFONT g_hfont[5];
static BYTE g_alphaLut[256];
static int g_emTop[5];

static BYTE *new_page(void)
{
    BYTE *fontObj = *(BYTE **)VA(A_FONT_TEX);
    BYTE *clone;
    GLuint id;
    void *zeros;
    if (!fontObj || g_pageCount >= MAX_PAGES)
        return NULL;
    if (g_pageCount == 0)
    {
        g_texW = (int)*(float *)(fontObj + TEXOBJ_W);
        g_texH = (int)*(float *)(fontObj + TEXOBJ_H);
        g_target = *(GLenum *)(fontObj + TEXOBJ_TARGET);
        jp_log("font texobj=%p glid=%u size=%dx%d target=0x%x\n", fontObj,
             *(DWORD *)(fontObj + TEXOBJ_GLID), g_texW, g_texH, g_target);
        if (g_texW < 64 || g_texH < 64 || g_texW > 4096 || g_texH > 4096)
            return NULL;
    }
    clone = (BYTE *)malloc(TEXOBJ_SIZE);
    memcpy(clone, fontObj, TEXOBJ_SIZE);
    *(DWORD *)(clone + TEXOBJ_PIXELS) = 0;

    glGenTextures(1, &id);
    glBindTexture(g_target, id);
    *(DWORD *)VA(A_BOUND_TEX_CACHE) = id;
    glTexParameteri(g_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(g_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(g_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(g_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    zeros = calloc((size_t)g_texW * g_texH, 4);
    glTexImage2D(g_target, 0, GL_RGBA, g_texW, g_texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, zeros);
    free(zeros);
    *(DWORD *)(clone + TEXOBJ_GLID) = id;
    g_pages[g_pageCount++] = clone;
    g_curX = 1;
    g_curY = 1;
    jp_log("new glyph page %d glid=%u\n", g_pageCount - 1, id);
    return clone;
}

static void ensure_gdi(void)
{
    BITMAPINFO bi;
    int i;
    if (g_dc)
        return;
    for (i = 0; i < 256; i++)
        g_alphaLut[i] = (BYTE)(255.0 * pow(i / 255.0, g_alphaGamma) + 0.5);
    g_dc = CreateCompatibleDC(NULL);
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = DIB_W;
    bi.bmiHeader.biHeight = -DIB_H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    g_dib = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, (void **)&g_dibBits, NULL, 0);
    SelectObject(g_dc, g_dib);
    SetTextColor(g_dc, RGB(255, 255, 255));
    SetBkColor(g_dc, RGB(0, 0, 0));
    SetBkMode(g_dc, OPAQUE);
}

static HFONT get_hfont(int fi, const BYTE *font)
{
    if (!g_hfont[fi])
    {
        // Line advance used by my_draw_text for each font slot (code + 1).
        static const int kLineH[5] = {13, 16, 15, 17, 25};
        TEXTMETRICW tm;
        int bh = box_h(font), em = bh, shift;
        // The ASCII cell (box_h) can be taller than the line advance; a CJK glyph
        // fills its em box, so cap it or consecutive lines overlap.
        if (em > kLineH[fi] - g_lineGap)
            em = kLineH[fi] - g_lineGap;
        em += g_sizeAdjust;
        if (em < 8)
            em = 8;
        shift = bh > em ? (bh - em) / 2 : 0; // centre the smaller em box in the cell
        g_hfont[fi] = CreateFontA(-em, 0, 0, 0, g_fontWeight, 0, 0, 0, SHIFTJIS_CHARSET,
                                  OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                  DEFAULT_PITCH, g_fontFace);
        SelectObject(g_dc, g_hfont[fi]);
        GetTextMetricsW(g_dc, &tm);
        // Place the ideographic em box (approx. 0.88em above baseline) at the top of the cell.
        g_emTop[fi] = tm.tmAscent - (int)(em * 0.88 + 0.5) - shift;
        jp_log("font %d: box=%d em=%d ascent=%d descent=%d emTop=%d\n", fi, bh, em, tm.tmAscent, tm.tmDescent, g_emTop[fi]);
    }
    return g_hfont[fi];
}

static CGlyph *get_glyph(const BYTE *font, DWORD cp)
{
    int fi = font_index(font);
    DWORD key = ((DWORD)fi << 24) | cp;
    DWORD h = (cp * 2654435761u ^ (DWORD)fi * 40503u) & (CACHE_SIZE - 1);
    CGlyph *g;
    WCHAR wc[2];
    int wn, bh, adv, x, y;
    SIZE sz;
    DWORD *rgba;
    GLint oldRow, oldAlign;

    for (;;)
    {
        g = &g_cache[h];
        if (g->key == key)
            return g;
        if (g->key == 0)
            break;
        h = (h + 1) & (CACHE_SIZE - 1);
    }

    ensure_gdi();
    SelectObject(g_dc, get_hfont(fi, font));
    if (cp >= 0x10000)
    {
        wc[0] = (WCHAR)(0xD800 + ((cp - 0x10000) >> 10));
        wc[1] = (WCHAR)(0xDC00 + ((cp - 0x10000) & 0x3FF));
        wn = 2;
    }
    else
    {
        wc[0] = (WCHAR)cp;
        wn = 1;
    }
    GetTextExtentPoint32W(g_dc, wc, wn, &sz);
    adv = sz.cx;
    if (adv > DIB_W - 2)
        adv = DIB_W - 2;
    bh = box_h(font);

    memset(g_dibBits, 0, DIB_W * DIB_H * 4);
    TextOutW(g_dc, 0, -g_emTop[fi] + g_yAdjust, wc, wn);
    GdiFlush();

    if (g_pageCount == 0 || g_curX + adv + 2 > g_texW)
    {
        g_curX = 1;
        g_curY += g_shelfH;
    }
    if (g_pageCount == 0 || g_curY + g_shelfH > g_texH)
    {
        if (!new_page())
            return NULL;
    }

    rgba = (DWORD *)malloc((size_t)adv * bh * 4 + 4);
    for (y = 0; y < bh; y++)
        for (x = 0; x < adv; x++)
        {
            DWORD p = (y < DIB_H) ? g_dibBits[y * DIB_W + x] : 0;
            DWORD a = ((p & 0xff) + ((p >> 8) & 0xff) + ((p >> 16) & 0xff)) / 3;
            a = g_alphaLut[a];
            rgba[y * adv + x] = (a << 24) | 0x00ffffff;
        }
    glBindTexture(g_target, *(DWORD *)(g_pages[g_pageCount - 1] + TEXOBJ_GLID));
    *(DWORD *)VA(A_BOUND_TEX_CACHE) = *(DWORD *)(g_pages[g_pageCount - 1] + TEXOBJ_GLID);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &oldRow);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldAlign);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(g_target, 0, g_curX, g_curY, adv, bh, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, oldRow);
    glPixelStorei(GL_UNPACK_ALIGNMENT, oldAlign);
    free(rgba);

    g->key = key;
    g->page = (unsigned char)(g_pageCount - 1);
    g->x = (short)g_curX;
    g->y = (short)g_curY;
    g->w = (short)adv;
    g->h = (short)bh;
    g->adv = (short)(adv + g_cjkSpacing);
    g_curX += adv + 2;
    return g;
}

// ---------------------------------------------------------------- calls into the game
static void call_blit(void *obj, DWORD src1, DWORD src2, DWORD dst1, DWORD dst2)
{
    void *fn = VA(A_BLIT);
    __asm {
        push edi
        mov edi, obj
        push dst2
        push dst1
        push src2
        push src1
        push 0
        push 0
        push 1
        call fn
        add esp, 0x1c
        pop edi
    }
}

static void call_offset_rect(short *rect, DWORD a, DWORD b)
{
    void *fn = VA(A_OFFSET_RECT);
    __asm {
        mov eax, rect
        push b
        push a
        call fn
        add esp, 8
    }
}

typedef void(__cdecl *DrawAsciiFn)(const char *, int, int, int);

// ---------------------------------------------------------------- text units
typedef struct
{
    DWORD cp;     // codepoint (ASCII for single bytes)
    short x;      // x offset from start of the text (for wrapping)
    short w;      // advance
    BYTE ascii;   // 1 = draw with game font
} Unit;

static unsigned char mac_to_ascii(unsigned char c)
{
    switch (c)
    {
    case 0xc4: case 0xd0: return '-';
    case 0xd2: case 0xd3: return '"';
    case 0xd4: case 0xd5: return '\'';
    }
    return c > 0x7f ? ' ' : c;
}

// EUC-JP pair -> Unicode (0 if not a valid pair). Both bytes are >= 0x8E, so
// they never collide with ASCII control characters the game uses ('_', '|', ...).
static DWORD euc_pair(const unsigned char *s)
{
    WCHAR wc = 0;
    unsigned char c = s[0], d = s[1];
    if (!((c >= 0xA1 && c <= 0xFE && d >= 0xA1 && d <= 0xFE) || (c == 0x8E && d >= 0xA1 && d <= 0xDF)))
        return 0;
    if (MultiByteToWideChar(20932, MB_ERR_INVALID_CHARS, (const char *)s, 2, &wc, 1) != 1)
        return 0;
    return wc;
}

// Decode EUC-JP or UTF-8 (per config), falling back to the game's MacRoman
// cleanup for bytes that are not part of a valid sequence.
static int decode(const unsigned char *s, Unit *u, int max)
{
    int n = 0;
    while (*s && n < max)
    {
        DWORD cp;
        int len = 0;
        unsigned char c = *s;
        if (c < 0x80)
            cp = c, len = 1;
        else if (g_eucjp)
        {
            cp = euc_pair(s);
            if (cp)
                len = 2;
            else
                cp = mac_to_ascii(c), len = 1;
        }
        else if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80)
            cp = ((c & 0x1F) << 6) | (s[1] & 0x3F), len = 2;
        else if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80)
            cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F), len = 3;
        else if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80)
            cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F), len = 4;
        else
            cp = mac_to_ascii(c), len = 1;
        if (cp >= 0x80 && cp < 0x100)
            cp = mac_to_ascii((unsigned char)cp); // Latin-1 range: not in our fonts
        u[n].cp = cp;
        u[n].ascii = cp < 0x80;
        n++;
        s += len;
    }
    return n;
}

static int is_cjk(DWORD cp) { return cp >= 0x2E80; }

// Kinsoku: characters that must not start a line / must not end a line.
static int no_line_start(DWORD cp)
{
    static const WCHAR *s = L"、。，．・：；？！ー―‐～…‥）〕］｝〉》」』】’”ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ々ゝゞヽヾ!?),.:;";
    const WCHAR *p;
    for (p = s; *p; p++)
        if (*p == cp)
            return 1;
    return 0;
}
static int no_line_end(DWORD cp)
{
    static const WCHAR *s = L"（〔［｛〈《「『【‘“(";
    const WCHAR *p;
    for (p = s; *p; p++)
        if (*p == cp)
            return 1;
    return 0;
}

// Can we break the line before unit i?
static int can_break_before(Unit *u, int i)
{
    DWORD a = u[i - 1].cp, b = u[i].cp;
    if (a == ' ')
        return 1;
    if (!(is_cjk(a) || is_cjk(b)))
        return 0;
    if (no_line_start(b) || no_line_end(a))
        return 0;
    return 1;
}

static void measure(const BYTE *font, Unit *u, int n)
{
    int i, x = 0;
    for (i = 0; i < n; i++)
    {
        u[i].x = (short)x;
        if (u[i].ascii)
        {
            unsigned char c = (unsigned char)u[i].cp;
            u[i].w = (short)ascii_width(font, &c, 1) + fs(font, 0xa);
        }
        else
        {
            CGlyph *g = get_glyph(font, u[i].cp);
            u[i].w = g ? g->adv : 0;
        }
        x += u[i].w;
    }
}

static void draw_line(const BYTE *font, Unit *u, int n, int x, int y, int r, int g, int b)
{
    DrawAsciiFn drawAscii = (DrawAsciiFn)VA(A_DRAW_ASCII);
    char buf[1024];
    int i = 0;
    int bh = box_h(font);
    while (i < n)
    {
        if (u[i].ascii)
        {
            int k = 0;
            while (i < n && u[i].ascii && k < (int)sizeof buf - 1)
                buf[k++] = (char)u[i++].cp;
            buf[k] = 0;
            *(short *)VA(A_PEN_X) = (short)x;
            *(short *)VA(A_PEN_Y) = (short)y;
            drawAscii(buf, r, g, b);
            x += ascii_width(font, (unsigned char *)buf, k) + fs(font, 0xa);
        }
        else
        {
            CGlyph *gl = get_glyph(font, u[i].cp);
            if (gl && u[i].cp != 0x3000)
            {
                DWORD src1 = ((DWORD)(WORD)gl->x << 16) | (WORD)gl->y;
                DWORD src2 = ((DWORD)(WORD)(gl->x + gl->w) << 16) | (WORD)(gl->y + bh);
                DWORD dst1 = ((DWORD)(WORD)x << 16) | (WORD)y;
                DWORD dst2 = ((DWORD)(WORD)(x + gl->w) << 16) | (WORD)(y + bh);
                *(float *)VA(A_COLOR_R) = r / 31.0f;
                *(float *)VA(A_COLOR_G) = g / 31.0f;
                *(float *)VA(A_COLOR_B) = b / 31.0f;
                call_blit(g_pages[gl->page], src1, src2, dst1, dst2);
            }
            x += gl ? gl->adv : 0;
            i++;
        }
    }
}

// ---------------------------------------------------------------- replacement
#define MAX_UNITS 4096
static Unit g_units[MAX_UNITS];

static void __cdecl my_draw_text(int mode, const char *str, int code, int r, int g, int b, DWORD ra, DWORD rb)
{
    short rect[4]; // top, left, bottom, right
    const BYTE *font;
    int lh, n, i;
    Unit *u = g_units;

    mode = (short)mode;
    code = (short)code;
    rect[0] = (short)(ra & 0xffff);
    rect[1] = (short)(ra >> 16);
    rect[2] = (short)(rb & 0xffff);
    rect[3] = (short)(rb >> 16);
    rect[1] = (short)(rect[1] + *(DWORD *)VA(A_RECT_OFF_X));
    rect[3] = (short)(rect[3] + *(DWORD *)VA(A_RECT_OFF_X));
    rect[0] = (short)(rect[0] + *(short *)VA(A_RECT_OFF_Y));
    rect[2] = (short)(rect[2] + *(short *)VA(A_RECT_OFF_Y));
    call_offset_rect(rect, *(DWORD *)VA(A_ORIGIN_A), *(DWORD *)VA(A_ORIGIN_B));

    if (!str || !*str)
        return;

    switch (code)
    {
    case 14: font = (BYTE *)VA(0x5ec6e8); break;
    case 15: font = (BYTE *)VA(0x5ec3d8); break;
    case 16: case 18: font = (BYTE *)VA(0x5ec9f8); code = 16; break;
    case 20: case 22: case 24: font = (BYTE *)VA(0x5ecd08); code = 24; break;
    default: font = (BYTE *)VA(0x5ec0c8); code = 12; break;
    }
    lh = code + 1;
    *(BYTE **)VA(A_CUR_FONT) = (BYTE *)font;
    *(short *)VA(A_LINE_ACC) = (short)lh;

    n = decode((const unsigned char *)str, u, MAX_UNITS);
    if (mode == 2)
        for (i = 0; i < n; i++)
            if (u[i].cp == '|')
            {
                mode = 0;
                break;
            }
    measure(font, u, n);

    if (mode == 1)
    {
        int w = n ? u[n - 1].x + u[n - 1].w : 0;
        int x = (rect[1] + rect[3]) / 2 - w / 2 + 2;
        int y = (rect[2] - lh + rect[0]) / 2 + 2;
        draw_line(font, u, n, x, y, r, g, b);
    }
    else if (mode == 2)
    {
        draw_line(font, u, n, rect[1] + 2, rect[0] + 2, r, g, b);
    }
    else if (mode == 0)
    {
        int maxw = rect[3] - rect[1] - 6;
        int start = 0, y = rect[0] + 2, first = 1;
        while (start < n)
        {
            int lastBreak = -1, end = n, next = n;
            while (start < n && u[start].cp == ' ' && !first)
                start++;
            first = 0;
            for (i = start; i < n; i++)
            {
                if (u[i].cp == '|')
                {
                    end = i;
                    next = i + 1;
                    break;
                }
                if (i > start && can_break_before(u, i))
                    lastBreak = i;
                if (u[i].x + u[i].w - u[start].x > maxw && i > start)
                {
                    if (lastBreak > start)
                        end = next = lastBreak;
                    else
                        end = next = i; // force break
                    break;
                }
            }
            // trim trailing spaces
            {
                int e = end;
                while (e > start && u[e - 1].cp == ' ')
                    e--;
                if (e > start)
                {
                    // re-measure x relative to line start
                    draw_line(font, u + start, e - start, rect[1] + 2, y, r, g, b);
                }
            }
            if (next >= n)
                break;
            y += lh;
            *(short *)VA(A_LINE_ACC) = (short)(*(short *)VA(A_LINE_ACC) + lh);
            start = next;
        }
    }
}

__declspec(naked) static void hook_stub(void)
{
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        push dword ptr [ebp + 0x1c]
        push dword ptr [ebp + 0x18]
        push dword ptr [ebp + 0x14]
        push dword ptr [ebp + 0x10]
        push dword ptr [ebp + 0x0c]
        push dword ptr [ebp + 0x08]
        push edx
        push ecx
        call my_draw_text
        add esp, 32
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

static void install(void)
{
    BYTE *target = (BYTE *)VA(A_DRAW_TEXT_RECT);
    DWORD old;
    if (memcmp(target, kExpectedPrologue, sizeof kExpectedPrologue) != 0)
    {
        jp_log("unexpected code at draw_text_rect; game version mismatch, hook NOT installed\n");
        return;
    }
    VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old);
    target[0] = 0xE9;
    *(DWORD *)(target + 1) = (DWORD)hook_stub - ((DWORD)target + 5);
    VirtualProtect(target, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
    jp_log("hook installed at %p\n", target);
}

// ---------------------------------------------------------------- exe string replacement
//
// avernum_jp_exe.tsv (UTF-8, tab separated): va, sink, refs, english, japanese.
// For every row with a translation we check that the exe really has the English
// string at that address, then rewrite every relocated pointer to it (code
// immediates and data tables, found via the exe's base relocation table) so it
// points at the translated string instead. The exe file itself is untouched.

#define A_SPRINTF 0x4ceeb5
static const BYTE kSprintfPrologue[] = {0x8B, 0xFF, 0x55, 0x8B, 0xEC};

typedef struct
{
    DWORD oldPtr;
    DWORD newPtr;
} StrMap;
#define STRMAP_SIZE 4096
static StrMap g_strmap[STRMAP_SIZE];
static int g_strmapCount;

static void strmap_put(DWORD oldPtr, DWORD newPtr)
{
    DWORD h = (oldPtr * 2654435761u) & (STRMAP_SIZE - 1);
    while (g_strmap[h].oldPtr && g_strmap[h].oldPtr != oldPtr)
        h = (h + 1) & (STRMAP_SIZE - 1);
    if (!g_strmap[h].oldPtr)
        g_strmapCount++;
    g_strmap[h].oldPtr = oldPtr;
    g_strmap[h].newPtr = newPtr;
}
static DWORD strmap_get(DWORD oldPtr)
{
    DWORD h = (oldPtr * 2654435761u) & (STRMAP_SIZE - 1);
    while (g_strmap[h].oldPtr)
    {
        if (g_strmap[h].oldPtr == oldPtr)
            return g_strmap[h].newPtr;
        h = (h + 1) & (STRMAP_SIZE - 1);
    }
    return 0;
}

// In-place unescape of \n \r \t \\ (as written by the exporter).
static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++)
    {
        if (*s == '\\' && s[1])
        {
            s++;
            *o++ = *s == 'n' ? '\n' : *s == 'r' ? '\r' : *s == 't' ? '\t' : *s;
        }
        else
            *o++ = *s;
    }
    *o = 0;
}

// UTF-8 -> game encoding (EUC-JP or UTF-8). Returns malloc'd string.
static char *to_game_encoding(const char *utf8, int *unmappable)
{
    int wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0), i, n;
    WCHAR *w;
    char *out;
    BOOL usedDefault = FALSE;
    *unmappable = 0;
    if (!g_eucjp)
        return _strdup(utf8);
    w = (WCHAR *)malloc(wn * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wn);
    for (i = 0; i < wn; i++) // same normalisation as tools/jpconv.py
        switch (w[i])
        {
        case 0xFF5E: w[i] = 0x301C; break;
        case 0xFF0D: w[i] = 0x2212; break;
        case 0x2225: w[i] = 0x2016; break;
        }
    n = WideCharToMultiByte(20932, 0, w, wn, NULL, 0, NULL, NULL);
    out = (char *)malloc(n + 1);
    WideCharToMultiByte(20932, 0, w, wn, out, n, "?", &usedDefault);
    out[n] = 0;
    *unmappable = usedDefault;
    free(w);
    return out;
}

static int g_needPositional;

static int __cdecl my_sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    if (strchr(fmt, '$'))
        r = _vsprintf_p(buf, 65536, fmt, ap); // supports %1$s style reordering
    else
        r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

static void install_sprintf_hook(void)
{
    BYTE *target = (BYTE *)VA(A_SPRINTF);
    DWORD old;
    if (memcmp(target, kSprintfPrologue, sizeof kSprintfPrologue) != 0)
    {
        jp_log("unexpected code at sprintf; positional formats NOT supported\n");
        return;
    }
    VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old);
    target[0] = 0xE9;
    *(DWORD *)(target + 1) = (DWORD)my_sprintf - ((DWORD)target + 5);
    VirtualProtect(target, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
    {
        char t[64];
        my_sprintf(t, "%2$s-%1$d-%3$s", 7, "b", "c");
        jp_log("sprintf hook installed (positional arguments enabled), self-test \"%s\" (expect b-7-c)\n", t);
    }
}

static int is_positional(const char *s)
{
    for (; *s; s++)
        if (s[0] == '%' && s[1] >= '1' && s[1] <= '9' && s[2] == '$')
            return 1;
    return 0;
}

static void load_exe_strings(const char *dir)
{
    char path[MAX_PATH];
    FILE *f;
    long size;
    char *buf, *line, *next;
    int lineno = 0, ok = 0, skipped = 0;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + dos->e_lfanew);
    DWORD imageSize = nt->OptionalHeader.SizeOfImage;

    _snprintf(path, sizeof path, "%s\\avernum_jp_exe.tsv", dir);
    f = fopen(path, "rb");
    if (!f)
    {
        jp_log("no %s (exe strings stay English)\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = 0;
    fclose(f);

    for (line = buf; line && *line; line = next)
    {
        char *col[5] = {0};
        char *p;
        int c = 0, unmappable;
        DWORD va, rva;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        lineno++;
        if ((p = strchr(line, '\r')) != NULL)
            *p = 0;
        if (lineno == 1 && (strncmp(line, "va\t", 3) == 0 || strncmp(line, "\xEF\xBB\xBFva\t", 6) == 0))
            continue;
        col[0] = line;
        for (p = line; *p && c < 4; p++)
            if (*p == '\t')
            {
                *p = 0;
                col[++c] = p + 1;
            }
        if (c < 4 || !col[4][0])
            continue; // untranslated
        unescape(col[3]);
        unescape(col[4]);
        va = strtoul(col[0], NULL, 16);
        rva = va - 0x400000;
        if (va < 0x400000 || rva >= imageSize || strcmp((char *)g_base + rva, col[3]) != 0)
        {
            jp_log("exe_tsv line %d: English text does not match exe at %s, skipped\n", lineno, col[0]);
            skipped++;
            continue;
        }
        {
            char *jp = to_game_encoding(col[4], &unmappable);
            if (unmappable)
                jp_log("exe_tsv line %d: characters not in EUC-JP were replaced with '?'\n", lineno);
            if (strstr(col[1], "float") && strlen(jp) > 39)
                jp_log("exe_tsv line %d: floating text is %u bytes (max 39), it will not be shown\n", lineno, (unsigned)strlen(jp));
            if (is_positional(jp))
                g_needPositional = 1;
            strmap_put((DWORD)(g_base + rva), (DWORD)jp);
            ok++;
        }
    }
    free(buf);

    // Walk the exe's base relocations and redirect every pointer to a translated string.
    {
        IMAGE_DATA_DIRECTORY *rd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        BYTE *rel = g_base + rd->VirtualAddress, *end = rel + rd->Size;
        int patched = 0;
        while (rd->VirtualAddress && rel < end)
        {
            IMAGE_BASE_RELOCATION *blk = (IMAGE_BASE_RELOCATION *)rel;
            WORD *e = (WORD *)(blk + 1);
            int n = (blk->SizeOfBlock - sizeof *blk) / 2, i;
            if (blk->SizeOfBlock < sizeof *blk)
                break;
            for (i = 0; i < n; i++)
            {
                if ((e[i] >> 12) == IMAGE_REL_BASED_HIGHLOW)
                {
                    DWORD *site = (DWORD *)(g_base + blk->VirtualAddress + (e[i] & 0xfff));
                    DWORD np = strmap_get(*site);
                    if (np)
                    {
                        DWORD old;
                        VirtualProtect(site, 4, PAGE_EXECUTE_READWRITE, &old);
                        *site = np;
                        VirtualProtect(site, 4, old, &old);
                        patched++;
                    }
                }
            }
            rel += blk->SizeOfBlock;
        }
        FlushInstructionCache(GetCurrentProcess(), NULL, 0);
        jp_log("exe strings: %d translated, %d skipped, %d references patched\n", ok, skipped, patched);
    }
    if (g_needPositional)
        install_sprintf_hook();
}

// ---------------------------------------------------------------- dsound passthrough
typedef HRESULT(WINAPI *DSCreateFn)(const GUID *, void **, void *);
static HMODULE g_realDsound;
static FARPROC real_dsound(const char *name)
{
    if (!g_realDsound)
    {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat(path, "\\dsound.dll");
        g_realDsound = LoadLibraryA(path);
    }
    return g_realDsound ? GetProcAddress(g_realDsound, name) : NULL;
}
HRESULT WINAPI DirectSoundCreate(const GUID *dev, void **out, void *outer)
{
    DSCreateFn fn = (DSCreateFn)real_dsound("DirectSoundCreate");
    return fn ? fn(dev, out, outer) : E_FAIL;
}

// ---------------------------------------------------------------- script token estimate
// Before tokenizing a script, 0x46ecc0 estimates the token count to size an array of
// (short)(count + 2) * 6 bytes. It counts every byte that is not a digit, letter, '_'
// or blank as its own token -- also inside string literals -- so a Japanese string
// adds 2 per character, and a long dialogue file overflows the short count, the
// allocation fails and the CRT aborts. The real token count does not change (a
// literal is one token), so classify bytes >= 0x80 like letters.
//   0x46ed1a: cmp al,30h / jl 46ed29 / cmp al,39h / jg 46ed29   -> jmp tok_class_stub
#define A_TOKCLASS 0x46ed1a
static const BYTE kTokClassOrig[8] = {0x3C, 0x30, 0x7C, 0x0B, 0x3C, 0x39, 0x7F, 0x07};
static DWORD g_tokDigit, g_tokOther, g_tokLetter; // 0x46ed22, 0x46ed29, 0x46ed4a

__declspec(naked) static void tok_class_stub(void)
{
    __asm {
        test al, al
        js letter
        cmp al, 0x30
        jl other
        cmp al, 0x39
        jg other
        jmp dword ptr [g_tokDigit]
    other:
        jmp dword ptr [g_tokOther]
    letter:
        jmp dword ptr [g_tokLetter]
    }
}

static void install_token_estimate_fix(void)
{
    BYTE *p = (BYTE *)VA(A_TOKCLASS);
    DWORD old;
    if (memcmp(p, kTokClassOrig, sizeof kTokClassOrig) != 0)
    {
        jp_log("token estimate fix skipped: unexpected bytes at %06x\n", A_TOKCLASS);
        return;
    }
    g_tokDigit = (DWORD)VA(0x46ed22);
    g_tokOther = (DWORD)VA(0x46ed29);
    g_tokLetter = (DWORD)VA(0x46ed4a);
    VirtualProtect(p, 8, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    *(DWORD *)(p + 1) = (DWORD)tok_class_stub - (DWORD)(p + 5);
    p[5] = p[6] = p[7] = 0x90;
    VirtualProtect(p, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 8);
    jp_log("token estimate fix installed\n");
}

// ---------------------------------------------------------------- print_named_str
// The script command print_named_str(who, "text") builds the message with
//   0x47bcfc: push 5d2aa0h ("%s %s") / push eax / call sprintf   (buffer: 264 bytes)
// i.e. "<name> <text>". A Japanese text continues the name directly
// ("<name>が応じない。"), so drop the space when the text starts with a non-ASCII byte.
#define A_NAMED_PUSH 0x47bcfc
#define A_NAMED_CALL 0x47bd02
#define A_FMT_S_SPACE_S 0x5d2aa0
// The buffer is [ebp-108h, ebp); its last dword (ebp-4) holds the /GS cookie, so
// stay well inside it.
#define NAMED_BUF 256

static int __cdecl named_sprintf(char *buf, const char *fmt, const char *name, const char *text)
{
    int n;
    (void)fmt;
    if (!name)
        name = "";
    if (!text)
        text = "";
    n = _snprintf(buf, NAMED_BUF - 1, ((unsigned char)text[0] >= 0x80) ? "%s%s" : "%s %s", name, text);
    if (n < 0) // truncated: _snprintf does not terminate
    {
        buf[NAMED_BUF - 1] = 0;
        n = NAMED_BUF - 1;
    }
    return n;
}

static void install_named_str_fix(void)
{
    BYTE *push = (BYTE *)VA(A_NAMED_PUSH);
    BYTE *call = (BYTE *)VA(A_NAMED_CALL);
    DWORD old;
    if (push[0] != 0x68 || *(DWORD *)(push + 1) != (DWORD)VA(A_FMT_S_SPACE_S) || call[0] != 0xE8 ||
        (DWORD)(call + 5) + *(DWORD *)(call + 1) != (DWORD)VA(A_SPRINTF))
    {
        jp_log("print_named_str fix skipped: unexpected code at %06x\n", A_NAMED_PUSH);
        return;
    }
    VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD *)(call + 1) = (DWORD)named_sprintf - (DWORD)(call + 5);
    VirtualProtect(call, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    jp_log("print_named_str fix installed\n");
}

// ---------------------------------------------------------------- crash diagnostics
typedef int(WINAPI *MessageBox_t)(HWND, const void *, const void *, UINT);
static MessageBox_t g_msgBoxBodyA, g_msgBoxBodyW; // entry + 2 (past the hot-patch "mov edi,edi")

static void log_stack(DWORD *sp)
{
    DWORD lo = (DWORD)g_base + 0x1000, hi = (DWORD)g_base + 0x1d0000;
    int i, n = 0;
    jp_log("  exe return addresses on stack:");
    for (i = 0; i < 0x2000 && n < 40; i++)
    {
        DWORD v;
        if (IsBadReadPtr(sp + i, 4))
            break;
        v = sp[i];
        if (v >= lo && v < hi)
        {
            jp_log(" %06x", v - (DWORD)g_base + 0x400000);
            n++;
        }
    }
    jp_log("\n");
}

static int WINAPI my_MessageBoxA(HWND h, const void *text, const void *caption, UINT type)
{
    jp_log("MessageBoxA: %s / %s\n", caption ? (const char *)caption : "", text ? (const char *)text : "");
    log_stack((DWORD *)&h);
    return g_msgBoxBodyA(h, text, caption, type);
}

static int WINAPI my_MessageBoxW(HWND h, const void *text, const void *caption, UINT type)
{
    jp_log("MessageBoxW: %ls / %ls\n", caption ? (const wchar_t *)caption : L"", text ? (const wchar_t *)text : L"");
    log_stack((DWORD *)&h);
    return g_msgBoxBodyW(h, text, caption, type);
}

static void hot_patch(const char *name, void *hook, MessageBox_t *body)
{
    BYTE *f = (BYTE *)GetProcAddress(GetModuleHandleA("user32.dll"), name);
    DWORD old;
    int i;
    if (!f || f[0] != 0x8B || f[1] != 0xFF)
    {
        jp_log("%s hook skipped (not hot-patchable: %02x %02x)\n", name, f ? f[0] : 0, f ? f[1] : 0);
        return;
    }
    for (i = 1; i <= 5; i++)
        if (f[-i] != 0x90 && f[-i] != 0xCC)
        {
            jp_log("%s hook skipped (no padding)\n", name);
            return;
        }
    *body = (MessageBox_t)(f + 2);
    VirtualProtect(f - 5, 7, PAGE_EXECUTE_READWRITE, &old);
    f[-5] = 0xE9;
    *(DWORD *)(f - 4) = (DWORD)hook - (DWORD)f;
    f[0] = 0xEB; // jmp short -7 -> the long jmp above
    f[1] = 0xF9;
    VirtualProtect(f - 5, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), f - 5, 7);
    jp_log("%s hook installed\n", name);
}

// The CRT reports abort() and other runtime errors with a message box. Log the exe
// return addresses found on the stack (as unrelocated 0x4xxxxx addresses) first.
static void install_msgbox_hook(void)
{
    hot_patch("MessageBoxA", (void *)my_MessageBoxA, &g_msgBoxBodyA);
    hot_patch("MessageBoxW", (void *)my_MessageBoxW, &g_msgBoxBodyW);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        char dir[MAX_PATH], path[MAX_PATH], *p;
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, dir, MAX_PATH);
        p = strrchr(dir, '\\');
        if (p)
            *p = 0;
        _snprintf(path, sizeof path, "%s\\avernum_jp.log", dir);
        g_log = fopen(path, "w");
        load_config(dir);
        g_base = (BYTE *)GetModuleHandleA(NULL);
        jp_log("Avernum JP hook loaded. base=%p face=%s\n", g_base, g_fontFace);
        install();
        load_exe_strings(dir);
        install_token_estimate_fix();
        install_named_str_fix();
        install_msgbox_hook();
    }
    return TRUE;
}
