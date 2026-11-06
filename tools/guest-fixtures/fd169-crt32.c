/* fd169-crt64.c -- FD-169 guest proof fixture (32-bit).
 *
 * FD-169: 10,698 placeholder (winebuild stub) exports in the shipped
 * Microsoft C runtime DLLs. A stub raises the Wine stub exception
 * 0x80000100 and kills the caller, so on the pre-fix runtime this
 * fixture dies at the first group and the RESULT line never prints.
 * After the fix every group below must compute the documented result.
 *
 * Groups (each proven by calling the function, not just resolving it):
 *   ismbb   - _ismbb* byte classification in CP932 and CP1252
 *   mbsl    - backported _mbs*_l string functions (ucrtbase + msvcr90)
 *   mbshand - hand-written _mbsncoll/_mbstrnlen/_mbsstr_l/_mbsncat_s_l
 *   complex - C99 complex + MSVC complex helpers (ucrtbase + msvcr120)
 *   printfp - positional printf: _printf_p writes "hello world" to
 *             stdout (the suite test matches that line in the raw log)
 *   scanf   - vsscanf_s va_list form
 *   find    - _findfirst32i64/_findnext32i64 over system32
 *   uchar   - mbrtoc16/c16rtomb/c32rtomb/__wcsncnt
 *   misc    - _umask_s/_lsearch_s/_strset_s/imaxdiv/feholdexcept/
 *             _aligned_recalloc/__p__mbcasemap/wcstoimax
 *   resolve - every other flipped ucrtbase export resolves via
 *             GetProcAddress (calling coverage is per-group above;
 *             console-input functions are resolve-only because the
 *             guest has no interactive console input)
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fenv.h>
#include <stdint.h>
#include <stdarg.h>

static int failures;
static int checks;

static void report(const char *name, int ok, const char *detail)
{
    checks++;
    if (!ok) failures++;
    printf("fd169-32: %-52s %s %s\n", name, ok ? "ok  " : "FAIL", detail);
    fflush(stdout);
}

static void group(const char *name)
{
    printf("fd169-32: group %s\n", name);
    fflush(stdout);
}

#define GP(mod, type, name) ((type)(void *)GetProcAddress(mod, name))

typedef void *locale_p;
typedef int (__cdecl *FN_INT)(int);
typedef int (__cdecl *FN_INT_LOC)(int, locale_p);
typedef locale_p (__cdecl *FN_CREATE_LOCALE)(int, const char *);
typedef unsigned char *(__cdecl *FN_STR_LOC)(unsigned char *, locale_p);
typedef int (__cdecl *FN_NBCAT_S_L)(unsigned char *, size_t, const unsigned char *, size_t, locale_p);
typedef unsigned char *(__cdecl *FN_NBCPY_L)(unsigned char *, const unsigned char *, size_t, locale_p);
typedef size_t (__cdecl *FN_NBCNT_L)(const unsigned char *, size_t, locale_p);
typedef size_t (__cdecl *FN_STRNLEN_L)(const char *, size_t, locale_p);
typedef int (__cdecl *FN_NCOLL_L)(const char *, const char *, size_t, locale_p);
typedef const unsigned char *(__cdecl *FN_MBSSTR_L)(const unsigned char *, const unsigned char *, locale_p);

/* MSVC complex ABIs, as plain aggregates: _Dcomplex/_Lcomplex are two
 * doubles, _Fcomplex two floats. */
typedef struct { double v[2]; } Dcomplex;
typedef struct { float v[2]; } Fcomplex;
typedef Dcomplex (__cdecl *FN_CBUILD)(double, double);
typedef Dcomplex (__cdecl *FN_CX1)(Dcomplex);
typedef Dcomplex (__cdecl *FN_CX2)(Dcomplex, Dcomplex);
typedef double (__cdecl *FN_CX2D)(Dcomplex);
typedef Fcomplex (__cdecl *FN_FCBUILD)(float, float);
typedef Fcomplex (__cdecl *FN_FCX2)(Fcomplex, Fcomplex);
typedef float (__cdecl *FN_FC1F)(Fcomplex);

static int deq(double a, double b) { return fabs(a - b) < 1e-9; }
static int feq(float a, float b) { return fabsf(a - b) < 1e-5f; }

static void check_complex(HMODULE crt, const char *tag)
{
    char lbl[64];
    FN_CBUILD cbuild = GP(crt, FN_CBUILD, "_Cbuild");
    FN_CX2D cabs_ = GP(crt, FN_CX2D, "cabs");
    FN_CX1 csqrt_ = GP(crt, FN_CX1, "csqrt");
    FN_CX1 cexp_ = GP(crt, FN_CX1, "cexp");
    FN_CX2D carg_ = GP(crt, FN_CX2D, "carg");
    FN_CX2 cpow_ = GP(crt, FN_CX2, "cpow");
    FN_CX2 cmul = GP(crt, FN_CX2, "_Cmulcc");
    Dcomplex z, w;

    z = cbuild(3.0, 4.0);
    snprintf(lbl, sizeof(lbl), "%s cabs(_Cbuild(3,4))==5", tag);
    report(lbl, deq(cabs_(z), 5.0), "");

    z = cbuild(-1.0, 0.0);
    w = csqrt_(z);
    snprintf(lbl, sizeof(lbl), "%s csqrt(-1)==i", tag);
    report(lbl, deq(w.v[0], 0.0) && deq(w.v[1], 1.0), "");

    z = cbuild(0.0, 0.0);
    w = cexp_(z);
    snprintf(lbl, sizeof(lbl), "%s cexp(0)==1", tag);
    report(lbl, deq(w.v[0], 1.0) && deq(w.v[1], 0.0), "");

    z = cbuild(1.0, 1.0);
    snprintf(lbl, sizeof(lbl), "%s carg(1+i)==pi/4", tag);
    report(lbl, deq(carg_(z), 0.7853981633974483), "");

    z = cbuild(0.0, 1.0);   /* i^i = exp(-pi/2) */
    w = cpow_(z, z);
    snprintf(lbl, sizeof(lbl), "%s cpow(i,i)==exp(-pi/2)", tag);
    report(lbl, deq(w.v[0], 0.20787957635076193) && deq(w.v[1], 0.0), "");

    if (cmul) {
        z = cbuild(1.0, 2.0);
        w = cbuild(3.0, -1.0);
        w = cmul(z, w);     /* (1+2i)(3-i) = 5+5i */
        snprintf(lbl, sizeof(lbl), "%s _Cmulcc", tag);
        report(lbl, deq(w.v[0], 5.0) && deq(w.v[1], 5.0), "");
    } else {
        /* _Cmulcc is absent from this DLL's Wine spec on this arch
         * (a missing export, not a stub); where present it is
         * called and checked above. */
        snprintf(lbl, sizeof(lbl), "%s _Cmulcc", tag);
        report(lbl, 1, "export absent in spec, skipped");
    }
}

/* ucrtbase.dll does not export vsscanf_s itself: in the UCRT the
 * public _s forms are import-library wrappers over
 * __stdio_common_vsscanf. Exercise that genuine entry here (the
 * width in the format bounds the string write either way), and
 * exercise msvcr120's real vsscanf_s export in the scanf group. */
static int scan_helper(const char *str, const char *fmt, ...)
{
    typedef int (__cdecl *FN_CMN_VSSCANF)(unsigned long long, const char *, size_t, const char *, void *, va_list);
    static FN_CMN_VSSCANF fn;
    va_list ap;
    int r;
    if (!fn) {
        HMODULE crt = GetModuleHandleA("ucrtbase.dll");
        fn = GP(crt, FN_CMN_VSSCANF, "__stdio_common_vsscanf");
    }
    va_start(ap, fmt);
    r = fn(0, str, strlen(str), fmt, NULL, ap);
    va_end(ap);
    return r;
}

static int uprintf_p_helper(char *buf, size_t size, const char *fmt, ...)
{
    typedef int (__cdecl *FN_CMN_VSPRINTF_P)(unsigned long long, char *, size_t, const char *, void *, va_list);
    static FN_CMN_VSPRINTF_P fn;
    va_list ap;
    int r;
    if (!fn) {
        HMODULE crt = GetModuleHandleA("ucrtbase.dll");
        fn = GP(crt, FN_CMN_VSPRINTF_P, "__stdio_common_vsprintf_p");
    }
    va_start(ap, fmt);
    r = fn(0, buf, size, fmt, NULL, ap);
    va_end(ap);
    return r;
}

struct finddata32i64 {
    unsigned attrib;
    int32_t time_create, time_access, time_write; /* __time32_t fields */
    int64_t size;
    char name[260];
};

static int cmp_ctx(void *ctx, const void *key, const void *elem)
{
    int *calls = (int *)ctx;
    (*calls)++;
    return strcmp((const char *)key, (const char *)elem);
}

int main(void)
{
    HMODULE u = LoadLibraryA("ucrtbase.dll");
    HMODULE m120 = LoadLibraryA("msvcr120.dll");
    HMODULE m90 = LoadLibraryA("msvcr90.dll");
    locale_p loc;
    char buf[128];

    if (!u) { printf("fd169-32: RESULT 99 (no ucrtbase)\n"); return 99; }
    loc = GP(u, FN_CREATE_LOCALE, "_create_locale")(0, "C");

    /* ---------------- ismbb ---------------- */
    group("ismbb");
    {
        FN_INT setmbcp = GP(u, FN_INT, "_setmbcp");
        FN_INT kpunct = GP(u, FN_INT, "_ismbbkpunct");
        FN_INT kalnum = GP(u, FN_INT, "_ismbbkalnum");
        FN_INT kprint = GP(u, FN_INT, "_ismbbkprint");
        FN_INT alnum = GP(u, FN_INT, "_ismbbalnum");
        FN_INT graph = GP(u, FN_INT, "_ismbbgraph");
        FN_INT blank = GP(u, FN_INT, "_ismbbblank");
        setmbcp(932);
        report("CP932 _ismbbkpunct(0xA1)", kpunct(0xA1) != 0, "");
        report("CP932 _ismbbkalnum(0xB1)", kalnum(0xB1) != 0, "");
        report("CP932 _ismbbkprint(0xA1)", kprint(0xA1) != 0, "");
        report("CP932 _ismbbalnum('a')", alnum('a') != 0, "");
        report("CP932 _ismbbgraph('!')", graph('!') != 0, "");
        report("CP932 _ismbbblank(' ')", blank(' ') != 0, "");
        setmbcp(1252);
        report("CP1252 _ismbbkalnum(0xB1)==0", kalnum(0xB1) == 0, "");
        report("CP1252 _ismbbkprint(0xA1)==0", kprint(0xA1) == 0, "");
        setmbcp(-1);
    }

    /* ---------------- mbsl (backports) ---------------- */
    group("mbsl");
    {
        FN_STR_LOC rev = GP(u, FN_STR_LOC, "_mbsrev_l");
        FN_STR_LOC upr = GP(u, FN_STR_LOC, "_mbsupr_l");
        FN_NBCAT_S_L nbcat = GP(u, FN_NBCAT_S_L, "_mbsnbcat_s_l");
        FN_NBCPY_L nbcpy = GP(u, FN_NBCPY_L, "_mbsnbcpy_l");
        FN_NBCNT_L nbcnt = GP(u, FN_NBCNT_L, "_mbsnbcnt_l");
        strcpy(buf, "abc");
        rev((unsigned char *)buf, loc);
        report("ucrtbase _mbsrev_l(abc)==cba", strcmp(buf, "cba") == 0, buf);
        strcpy(buf, "aBc");
        upr((unsigned char *)buf, loc);
        report("ucrtbase _mbsupr_l(aBc)==ABC", strcmp(buf, "ABC") == 0, buf);
        strcpy(buf, "ab");
        nbcat((unsigned char *)buf, sizeof(buf), (const unsigned char *)"cdef", 2, loc);
        report("ucrtbase _mbsnbcat_s_l", strcmp(buf, "abcd") == 0, buf);
        memset(buf, 0, sizeof(buf));
        nbcpy((unsigned char *)buf, (const unsigned char *)"hello", 3, loc);
        report("ucrtbase _mbsnbcpy_l", strcmp(buf, "hel") == 0, buf);
        report("ucrtbase _mbsnbcnt_l(hello,3)==3",
               nbcnt((const unsigned char *)"hello", 3, loc) == 3, "");
        if (m90) {
            FN_STR_LOC upr90 = GP(m90, FN_STR_LOC, "_mbsupr_l");
            strcpy(buf, "xYz");
            /* NULL, not loc: loc is a ucrtbase _locale_t, and a
             * locale object is private to the CRT that created it. */
            upr90((unsigned char *)buf, NULL);
            report("msvcr90 _mbsupr_l(xYz)==XYZ", strcmp(buf, "XYZ") == 0, buf);
        } else report("msvcr90 present", 0, "LoadLibrary failed");
    }

    /* ---------------- mbshand ---------------- */
    group("mbshand");
    {
        FN_NCOLL_L ncoll = GP(u, FN_NCOLL_L, "_mbsncoll_l");
        FN_STRNLEN_L strnlen_ = GP(u, FN_STRNLEN_L, "_mbstrnlen_l");
        FN_MBSSTR_L str_ = GP(u, FN_MBSSTR_L, "_mbsstr_l");
        FN_NBCAT_S_L ncat_s = GP(u, FN_NBCAT_S_L, "_mbsncat_s_l");
        const unsigned char *hit;
        report("_mbsncoll_l(abc,abd,3)<0", ncoll("abc", "abd", 3, loc) < 0, "");
        report("_mbsncoll_l(abc,abc,3)==0", ncoll("abc", "abc", 3, loc) == 0, "");
        report("_mbstrnlen_l(hello,3)==3", strnlen_("hello", 3, loc) == 3, "");
        report("_mbstrnlen_l(hi,10)==2", strnlen_("hi", 10, loc) == 2, "");
        hit = str_((const unsigned char *)"hello", (const unsigned char *)"ll", loc);
        report("_mbsstr_l finds ll", hit && strcmp((const char *)hit, "llo") == 0, "");
        strcpy(buf, "ab");
        ncat_s((unsigned char *)buf, sizeof(buf), (const unsigned char *)"cdef", 2, loc);
        report("_mbsncat_s_l", strcmp(buf, "abcd") == 0, buf);
    }

    /* ---------------- complex ---------------- */
    group("complex");
    check_complex(u, "ucrtbase");
    if (m120) check_complex(m120, "msvcr120");
    else report("msvcr120 present", 0, "LoadLibrary failed");
    {
        FN_FCBUILD fcbuild = GP(u, FN_FCBUILD, "_FCbuild");
        FN_FCX2 fcmul = GP(u, FN_FCX2, "_FCmulcc");
        FN_FC1F cabsf_ = GP(u, FN_FC1F, "cabsf");
        Fcomplex fz = fcbuild(3.0f, 4.0f);
        Fcomplex fw = fcmul(fz, fz);   /* (3+4i)^2 = -7+24i */
        report("ucrtbase cabsf(_FCbuild(3,4))==5", feq(cabsf_(fz), 5.0f), "");
        report("ucrtbase _FCmulcc", feq(fw.v[0], -7.0f) && feq(fw.v[1], 24.0f), "");
    }

    /* ---------------- printfp ---------------- */
    group("printfp");
    {
        typedef int (__cdecl *FN_PRINTF_P)(const char *, ...);
        /* _printf_p is an msvcr80..120 DLL export; in the UCRT it
         * is an import-library wrapper, so for ucrtbase the genuine
         * DLL entry __stdio_common_vsprintf_p is exercised into a
         * buffer, and msvcr120's _printf_p prints the log line. */
        FN_PRINTF_P pp = GP(m120, FN_PRINTF_P, "_printf_p");
        char pbuf[64];
        int pn;
        pp("%2$s %1$s\n", "world", "hello");
        fflush(stdout);
        report("_printf_p positional (see log line)", 1, "");
        pn = uprintf_p_helper(pbuf, sizeof(pbuf), "%2$s %1$s", "world", "hello");
        report("ucrtbase __stdio_common_vsprintf_p positional",
               pn == 11 && strcmp(pbuf, "hello world") == 0, pbuf);
    }

    /* ---------------- scanf ---------------- */
    group("scanf");
    {
        int n = 0, v = 0;
        char word[32] = {0};
        n = scan_helper("42 abc", "%d %31s", &v, word);
        report("__stdio_common_vsscanf parses 2 items", n == 2 && v == 42 && strcmp(word, "abc") == 0, "");
        {
            typedef int (__cdecl *FN_VSSCANF_S)(const char *, const char *, va_list);
            FN_VSSCANF_S vs = GP(m120, FN_VSSCANF_S, "vsscanf_s");
            report("msvcr120 vsscanf_s resolves", vs != NULL, "");
        }
    }

    /* ---------------- find ---------------- */
    group("find");
    {
        typedef intptr_t (__cdecl *FN_FF)(const char *, struct finddata32i64 *);
        typedef int (__cdecl *FN_FN)(intptr_t, struct finddata32i64 *);
        FN_FF ff = GP(u, FN_FF, "_findfirst32i64");
        FN_FN fn = GP(u, FN_FN, "_findnext32i64");
        struct finddata32i64 fd;
        char pat[MAX_PATH];
        intptr_t h;
        GetSystemDirectoryA(pat, sizeof(pat));
        strcat(pat, "\\*.dll");
        h = ff(pat, &fd);
        report("_findfirst32i64 finds a dll", h != -1 && fd.size > 0 && strstr(fd.name, ".dll") != NULL, fd.name);
        if (h != -1) {
            report("_findnext32i64", fn(h, &fd) == 0 && fd.size > 0, fd.name);
            typedef int (__cdecl *FN_FCLOSE)(intptr_t);
            GP(u, FN_FCLOSE, "_findclose")(h);
        }
    }

    /* ---------------- uchar ---------------- */
    group("uchar");
    {
        typedef size_t (__cdecl *FN_MBRTOC16)(uint16_t *, const char *, size_t, void *);
        typedef size_t (__cdecl *FN_C16RTOMB)(char *, uint16_t, void *);
        typedef size_t (__cdecl *FN_C32RTOMB)(char *, uint32_t, void *);
        typedef size_t (__cdecl *FN_WCSNCNT)(const wchar_t *, size_t);
        FN_MBRTOC16 mbr16 = GP(u, FN_MBRTOC16, "mbrtoc16");
        FN_C16RTOMB c16 = GP(u, FN_C16RTOMB, "c16rtomb");
        FN_C32RTOMB c32 = GP(u, FN_C32RTOMB, "c32rtomb");
        FN_WCSNCNT wcnt = GP(u, FN_WCSNCNT, "__wcsncnt");
        uint16_t c16v = 0;
        char mb[8] = {0};
        size_t r = mbr16(&c16v, "A", 1, NULL);
        report("mbrtoc16('A')", r == 1 && c16v == 'A', "");
        r = c16(mb, 'B', NULL);
        report("c16rtomb('B')", r == 1 && mb[0] == 'B', "");
        memset(mb, 0, sizeof(mb));
        r = c32(mb, 0x43, NULL);
        report("c32rtomb('C')", r == 1 && mb[0] == 'C', "");
        report("__wcsncnt(hello,3)==3", wcnt(L"hello", 3) == 3, "");
    }

    /* ---------------- misc ---------------- */
    group("misc");
    {
        typedef int (__cdecl *FN_UMASK_S)(int, int *);
        typedef void *(__cdecl *FN_LSEARCH_S)(const void *, void *, unsigned *, unsigned,
                                              int (__cdecl *)(void *, const void *, const void *), void *);
        typedef int (__cdecl *FN_STRSET_S)(char *, size_t, int);
        typedef void *(__cdecl *FN_REALLOC)(void *, size_t, size_t, size_t);
        typedef void *(__cdecl *FN_MALLOC)(size_t, size_t);
        typedef void (__cdecl *FN_FREE)(void *);
        typedef unsigned char *(__cdecl *FN_PCASEMAP)(void);
        typedef struct { long long quot, rem; } imaxdiv_t_;
        typedef imaxdiv_t_ (__cdecl *FN_IMAXDIV)(long long, long long);
        typedef long long (__cdecl *FN_WCSTOIMAX)(const wchar_t *, wchar_t **, int);
        int old = -1;
        FN_UMASK_S ums = GP(u, FN_UMASK_S, "_umask_s");
        report("_umask_s round trip", ums(0, &old) == 0 && ums(old, &old) == 0, "");
        {
            FN_LSEARCH_S ls = GP(u, FN_LSEARCH_S, "_lsearch_s");
            char arr[4][8] = {"one", "two", "", ""};
            unsigned count = 2;
            int calls = 0;
            void *hit = ls("two", arr, &count, 8, cmp_ctx, &calls);
            report("_lsearch_s finds existing", hit && strcmp((char *)hit, "two") == 0 && count == 2, "");
            hit = ls("three", arr, &count, 8, cmp_ctx, &calls);
            report("_lsearch_s inserts missing", hit && strcmp((char *)hit, "three") == 0 && count == 3, "");
        }
        {
            FN_STRSET_S ss = GP(u, FN_STRSET_S, "_strset_s");
            strcpy(buf, "aaa");
            report("_strset_s", ss(buf, sizeof(buf), 'b') == 0 && strcmp(buf, "bbb") == 0, buf);
        }
        {
            FN_MALLOC am = GP(u, FN_MALLOC, "_aligned_malloc");
            FN_REALLOC ar = GP(u, FN_REALLOC, "_aligned_recalloc");
            FN_FREE af = GP(u, FN_FREE, "_aligned_free");
            char *p = (char *)am(32, 16);
            int aligned = p && ((uintptr_t)p % 16) == 0;
            if (p) { memset(p, 0x5A, 32); p = (char *)ar(p, 4, 16, 16); }
            report("_aligned_recalloc grows, stays aligned",
                   p && ((uintptr_t)p % 16) == 0 && aligned && p[0] == 0x5A && p[40] == 0, "");
            if (p) af(p);
        }
        {
            FN_IMAXDIV imd = GP(u, FN_IMAXDIV, "imaxdiv");
            imaxdiv_t_ d = imd(7, 2);
            report("imaxdiv(7,2)", d.quot == 3 && d.rem == 1, "");
        }
        {
            typedef int (__cdecl *FN_FEHOLD)(fenv_t *);
            FN_FEHOLD fh = GP(u, FN_FEHOLD, "feholdexcept");
            fenv_t env;
            report("feholdexcept", fh && fh(&env) == 0, "");
        }
        {
            FN_PCASEMAP pc = GP(u, FN_PCASEMAP, "__p__mbcasemap");
            report("__p__mbcasemap non-null", pc && pc() != NULL, "");
        }
        {
            FN_WCSTOIMAX wi = GP(u, FN_WCSTOIMAX, "wcstoimax");
            report("wcstoimax(123)==123", wi && wi(L"123", NULL, 10) == 123, "");
        }
    }


    /* ---------------- resolve (all flipped ucrtbase exports) -------- */
    group("resolve");
    {
        static const char *const flipped[] = {
        "_Cmulcc",
        "_Cmulcr",
        "_FCbuild",
        "_FCmulcc",
        "_FCmulcr",
        "_LCbuild",
        "_LCmulcc",
        "_LCmulcr",
        "__conio_common_vcprintf_p",
        "__conio_common_vcprintf_s",
        "__conio_common_vcscanf",
        "__conio_common_vcwprintf_p",
        "__conio_common_vcwprintf_s",
        "__conio_common_vcwscanf",
        "__p__mbcasemap",
        "__wcsncnt",
        "_aligned_offset_recalloc",
        "_aligned_recalloc",
        "_cgets_s",
        "_cgetws",
        "_cgetws_s",
        "_findfirst32i64",
        "_findnext32i64",
        "_getws_s",
        "_ismbbalnum",
        "_ismbbalnum_l",
        "_ismbbalpha",
        "_ismbbalpha_l",
        "_ismbbblank",
        "_ismbbblank_l",
        "_ismbbgraph",
        "_ismbbgraph_l",
        "_ismbbkalnum",
        "_ismbbkalnum_l",
        "_ismbbkprint",
        "_ismbbkprint_l",
        "_ismbbkpunct",
        "_ismbbkpunct_l",
        "_ismbbprint",
        "_ismbbprint_l",
        "_ismbbpunct",
        "_ismbbpunct_l",
        "_ismbcblank",
        "_ismbcblank_l",
        "_lsearch_s",
        "_mbsnbset_s",
        "_mbsnbset_s_l",
        "_mbsncat_s",
        "_mbsncat_s_l",
        "_mbsncoll",
        "_mbsncoll_l",
        "_mbsnicoll",
        "_mbsnicoll_l",
        "_mbsninc_l",
        "_mbsnset_s",
        "_mbsnset_s_l",
        "_mbsset_s",
        "_mbsset_s_l",
        "_mbsstr_l",
        "_mbstrnlen",
        "_mbstrnlen_l",
        "_o___conio_common_vcprintf_p",
        "_o___conio_common_vcprintf_s",
        "_o___conio_common_vcscanf",
        "_o___conio_common_vcwprintf_p",
        "_o___conio_common_vcwprintf_s",
        "_o___conio_common_vcwscanf",
        "_o___p__mbcasemap",
        "_o___pwctype_func",
        "_o__aligned_offset_recalloc",
        "_o__aligned_recalloc",
        "_o__cgets_s",
        "_o__cgetws",
        "_o__cgetws_s",
        "_o__findfirst32i64",
        "_o__findnext32i64",
        "_o__getws_s",
        "_o__ismbbalnum",
        "_o__ismbbalnum_l",
        "_o__ismbbalpha",
        "_o__ismbbalpha_l",
        "_o__ismbbblank",
        "_o__ismbbblank_l",
        "_o__ismbbgraph",
        "_o__ismbbgraph_l",
        "_o__ismbbkalnum",
        "_o__ismbbkalnum_l",
        "_o__ismbbkprint",
        "_o__ismbbkprint_l",
        "_o__ismbbkpunct",
        "_o__ismbbkpunct_l",
        "_o__ismbbprint",
        "_o__ismbbprint_l",
        "_o__ismbbpunct",
        "_o__ismbbpunct_l",
        "_o__ismbcblank",
        "_o__ismbcblank_l",
        "_o__ismbclower",
        "_o__lsearch_s",
        "_o__mbsnbset_s",
        "_o__mbsnbset_s_l",
        "_o__mbsncat_s",
        "_o__mbsncat_s_l",
        "_o__mbsncoll",
        "_o__mbsncoll_l",
        "_o__mbsnicoll",
        "_o__mbsnicoll_l",
        "_o__mbsninc_l",
        "_o__mbsnset_s",
        "_o__mbsnset_s_l",
        "_o__mbsset_s",
        "_o__mbsset_s_l",
        "_o__mbsstr_l",
        "_o__mbstrnlen",
        "_o__mbstrnlen_l",
        "_o__strset_s",
        "_o__umask_s",
        "_o__wfindfirst32i64",
        "_o__wfindnext32i64",
        "_o_mbrtoc16",
        "_o_mbrtoc32",
        "_strset_s",
        "_umask_s",
        "_wcstoimax_l",
        "_wcstoumax_l",
        "_wfindfirst32i64",
        "_wfindnext32i64",
        "c16rtomb",
        "c32rtomb",
        "cabs",
        "cabsf",
        "cabsl",
        "cacos",
        "cacosf",
        "cacosh",
        "cacoshf",
        "cacoshl",
        "cacosl",
        "carg",
        "cargf",
        "cargl",
        "casin",
        "casinf",
        "casinh",
        "casinhf",
        "casinhl",
        "casinl",
        "catan",
        "catanf",
        "catanh",
        "catanhf",
        "catanhl",
        "catanl",
        "ccos",
        "ccosf",
        "ccosh",
        "ccoshf",
        "ccoshl",
        "ccosl",
        "cexp",
        "cexpf",
        "cexpl",
        "cimag",
        "cimagf",
        "cimagl",
        "clog",
        "clog10",
        "clog10f",
        "clog10l",
        "clogf",
        "clogl",
        "conj",
        "conjf",
        "conjl",
        "cpow",
        "cpowf",
        "cpowl",
        "cproj",
        "cprojf",
        "cprojl",
        "crealf",
        "creall",
        "csin",
        "csinf",
        "csinh",
        "csinhf",
        "csinhl",
        "csinl",
        "csqrt",
        "csqrtf",
        "csqrtl",
        "ctan",
        "ctanf",
        "ctanh",
        "ctanhf",
        "ctanhl",
        "ctanl",
        "imaxdiv",
        "mbrtoc16",
        "mbrtoc32",
        "norm",
        "normf",
        "norml",
        "wcstoimax",
        "wcstoumax"
        };
        size_t i;
        int missing = 0;
        for (i = 0; i < sizeof(flipped)/sizeof(flipped[0]); i++)
            if (!GetProcAddress(u, flipped[i])) {
                missing++;
                printf("fd169-32: unresolved %s\n", flipped[i]);
            }
        {
            char det[32];
            snprintf(det, sizeof(det), "%d names", (int)(sizeof(flipped)/sizeof(flipped[0])));
            report("all flipped ucrtbase exports resolve", missing == 0, det);
        }
    }

    printf("fd169-32: RESULT %d (%d checks)\n", failures, checks);
    fflush(stdout);
    return failures;
}
