/* fd169r-smf64.c -- FD-169 remainder guest proof fixture (64-bit).
 *
 * FD-169 remainder: msvcp140_2.dll shipped with every C++17 special
 * math export (__std_smf_*) as a winebuild stub, and
 * msvcp140_atomic_wait.dll shipped the execution wait/wake pair,
 * the shared-mutex pair, the CRT allocation pair and the tzdb
 * deleters as stubs. A stub raises the Wine stub exception
 * 0x80000100 and kills the caller, so on the pre-fix runtime this
 * fixture dies inside the first group and the RESULT line never
 * prints. After the fix every group below must compute the
 * documented result.
 *
 * The expected values are 50-digit mpmath references (the same
 * grid the implementations were validated against on the host).
 * The tzdb deleters are exercised on hand-built structs laid out
 * exactly as the DLL's own tzdb structs: the matching getters
 * stay stubs (the browser build has no time-zone database), so
 * no getter-allocated struct can exist yet; the deleters are the
 * real upstream memory-management code over those layouts.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

static int failures;
static int checks;

static void report(const char *name, int ok, const char *detail)
{
    checks++;
    if (!ok) failures++;
    printf("fd169r-64: %-52s %s %s\n", name, ok ? "ok  " : "FAIL", detail);
    fflush(stdout);
}

static void group(const char *name)
{
    printf("fd169r-64: group %s\n", name);
    fflush(stdout);
}

#define GP(mod, type, name) ((type)(void *)GetProcAddress(mod, name))

typedef double (__stdcall *FN_D_D)(double);
typedef double (__stdcall *FN_D_DD)(double, double);
typedef double (__stdcall *FN_D_DDD)(double, double, double);
typedef double (__stdcall *FN_D_UD)(unsigned, double);
typedef double (__stdcall *FN_D_UUD)(unsigned, unsigned, double);
typedef float (__stdcall *FN_F_F)(float);
typedef float (__stdcall *FN_F_FF)(float, float);
typedef float (__stdcall *FN_F_FFF)(float, float, float);

static int close_rel(double got, double want, double tol)
{
    double scale = fabs(want) > 1.0 ? fabs(want) : 1.0;
    return fabs(got - want) <= tol * scale;
}

struct valcase { const char *fn; int nargs; double a1, a2, a3, want; };

static const struct valcase VALS[] = {

    { "__std_smf_beta", 2, 2.5, 3.5, 0.0, 0.03681553890925539 },
    { "__std_smf_riemann_zeta", 1, 3.5, 0.0, 0.0, 1.1267338673170566 },
    { "__std_smf_riemann_zeta", 1, -1.0, 0.0, 0.0, -0.08333333333333333 },
    { "__std_smf_expint", 1, 1.0, 0.0, 0.0, 1.8951178163559368 },
    { "__std_smf_expint", 1, -2.0, 0.0, 0.0, -0.04890051070806112 },
    { "__std_smf_comp_ellint_1", 1, 0.5, 0.0, 0.0, 1.685750354812596 },
    { "__std_smf_comp_ellint_2", 1, 0.5, 0.0, 0.0, 1.4674622093394272 },
    { "__std_smf_comp_ellint_3", 2, 0.5, 1.5, 0.0, -0.16695441924700546 },
    { "__std_smf_ellint_1", 2, 0.5, 0.7, 0.0, 0.7136205028677777 },
    { "__std_smf_ellint_2", 2, 0.5, 0.7, 0.0, 0.6868291803522703 },
    { "__std_smf_ellint_3", 3, 0.5, 0.9, 0.7, 0.8412103271656132 },
    { "__std_smf_cyl_bessel_j", 2, 2.5, 2.0, 0.0, 0.22392453146891578 },
    { "__std_smf_cyl_neumann", 2, 2.5, 2.0, 0.0, -0.8282206324443038 },
    { "__std_smf_cyl_bessel_i", 2, 2.5, 2.0, 0.0, 0.39702708013939053 },
    { "__std_smf_cyl_bessel_k", 2, 2.5, 2.0, 0.0, 0.3897977588961997 },
};

static const char *NAMES_D[] = {

    "__std_smf_assoc_laguerre",
    "__std_smf_assoc_legendre",
    "__std_smf_beta",
    "__std_smf_comp_ellint_1",
    "__std_smf_comp_ellint_2",
    "__std_smf_comp_ellint_3",
    "__std_smf_cyl_bessel_i",
    "__std_smf_cyl_bessel_j",
    "__std_smf_cyl_bessel_k",
    "__std_smf_cyl_neumann",
    "__std_smf_ellint_1",
    "__std_smf_ellint_2",
    "__std_smf_ellint_3",
    "__std_smf_expint",
    "__std_smf_hermite",
    "__std_smf_hypot3",
    "__std_smf_laguerre",
    "__std_smf_legendre",
    "__std_smf_riemann_zeta",
    "__std_smf_sph_bessel",
    "__std_smf_sph_legendre",
    "__std_smf_sph_neumann",
};

static const char *NAMES_F[] = {

    "__std_smf_assoc_laguerref",
    "__std_smf_assoc_legendref",
    "__std_smf_betaf",
    "__std_smf_comp_ellint_1f",
    "__std_smf_comp_ellint_2f",
    "__std_smf_comp_ellint_3f",
    "__std_smf_cyl_bessel_if",
    "__std_smf_cyl_bessel_jf",
    "__std_smf_cyl_bessel_kf",
    "__std_smf_cyl_neumannf",
    "__std_smf_ellint_1f",
    "__std_smf_ellint_2f",
    "__std_smf_ellint_3f",
    "__std_smf_expintf",
    "__std_smf_hermitef",
    "__std_smf_hypot3f",
    "__std_smf_laguerref",
    "__std_smf_legendref",
    "__std_smf_riemann_zetaf",
    "__std_smf_sph_besself",
    "__std_smf_sph_legendref",
    "__std_smf_sph_neumannf",
};

static void run_vals(HMODULE m2)
{
    char lbl[96], det[96];
    unsigned i;
    for (i = 0; i < sizeof(VALS) / sizeof(VALS[0]); i++)
    {
        const struct valcase *c = &VALS[i];
        double got = 0.0;
        if (c->nargs == 1)
        {
            FN_D_D f = GP(m2, FN_D_D, c->fn);
            if (!f) { report(c->fn, 0, "missing export"); continue; }
            got = f(c->a1);
        }
        else if (c->nargs == 2)
        {
            FN_D_DD f = GP(m2, FN_D_DD, c->fn);
            if (!f) { report(c->fn, 0, "missing export"); continue; }
            got = f(c->a1, c->a2);
        }
        else if (c->nargs == 3)
        {
            FN_D_DDD f = GP(m2, FN_D_DDD, c->fn);
            if (!f) { report(c->fn, 0, "missing export"); continue; }
            got = f(c->a1, c->a2, c->a3);
        }
        sprintf(det, "got %.17g want %.17g", got, c->want);
        sprintf(lbl, "value %s", c->fn);
        report(lbl, close_rel(got, c->want, 1e-9), det);
    }
}

/* order-indexed cases: unsigned first argument(s) */
static void run_order_cases(HMODULE m2)
{
    char det[96];
    FN_D_UD hermite = GP(m2, FN_D_UD, "__std_smf_hermite");
    FN_D_UD laguerre = GP(m2, FN_D_UD, "__std_smf_laguerre");
    FN_D_UD legendre = GP(m2, FN_D_UD, "__std_smf_legendre");
    FN_D_UD sph_bessel = GP(m2, FN_D_UD, "__std_smf_sph_bessel");
    FN_D_UD sph_neumann = GP(m2, FN_D_UD, "__std_smf_sph_neumann");
    FN_D_UUD assoc_laguerre = GP(m2, FN_D_UUD, "__std_smf_assoc_laguerre");
    FN_D_UUD assoc_legendre = GP(m2, FN_D_UUD, "__std_smf_assoc_legendre");
    FN_D_UUD sph_legendre = GP(m2, FN_D_UUD, "__std_smf_sph_legendre");

    report("resolve order-indexed exports",
           hermite && laguerre && legendre && sph_bessel && sph_neumann &&
           assoc_laguerre && assoc_legendre && sph_legendre, "");
    if (!hermite || !laguerre || !legendre || !sph_bessel || !sph_neumann ||
        !assoc_laguerre || !assoc_legendre || !sph_legendre)
        return;
    sprintf(det, "got %.17g", hermite(5, 1.0));
    report("value hermite(5,1)", close_rel(hermite(5, 1.0), -8.0, 1e-9), det);
    sprintf(det, "got %.17g", laguerre(3, 1.0));
    report("value laguerre(3,1)", close_rel(laguerre(3, 1.0), -0.6666666666666666, 1e-9), det);
    sprintf(det, "got %.17g", legendre(10, 0.3));
    report("value legendre(10,0.3)", close_rel(legendre(10, 0.3), 0.2514763495160156, 1e-9), det);
    sprintf(det, "got %.17g", assoc_legendre(2, 1, 0.5));
    report("value assoc_legendre(2,1,0.5) no-CS-phase",
           close_rel(assoc_legendre(2, 1, 0.5), 1.299038105676658, 1e-12), det);
    sprintf(det, "got %.17g", assoc_laguerre(2, 1, 1.0));
    report("value assoc_laguerre(2,1,1)", close_rel(assoc_laguerre(2, 1, 1.0), 0.5, 1e-9), det);
    sprintf(det, "got %.17g", sph_legendre(5, 2, 1.0));
    report("value sph_legendre(5,2,1.0)", close_rel(sph_legendre(5, 2, 1.0), -0.08054129322032023, 1e-9), det);
    sprintf(det, "got %.17g", sph_bessel(5, 1.0));
    report("value sph_bessel(5,1)", close_rel(sph_bessel(5, 1.0), 9.256115861125816e-05, 1e-9), det);
    sprintf(det, "got %.17g", sph_neumann(5, 1.0));
    report("value sph_neumann(5,1)", close_rel(sph_neumann(5, 1.0), -999.4403433922364, 1e-9), det);
    /* hermite three-term recurrence as an identity */
    {
        double h4 = hermite(4, 0.7), h5 = hermite(5, 0.7), h6 = hermite(6, 0.7);
        report("identity hermite recurrence", close_rel(h6, 2 * 0.7 * h5 - 2 * 5 * h4, 1e-11), "");
    }
    report("identity sph_bessel(0,x)=sin(x)/x",
           close_rel(sph_bessel(0, 1.3), sin(1.3) / 1.3, 1e-12), "");
}

static void run_float_cases(HMODULE m2)
{
    char det[96];
    FN_F_FF betaf = GP(m2, FN_F_FF, "__std_smf_betaf");
    FN_F_FF cyl_bessel_jf = GP(m2, FN_F_FF, "__std_smf_cyl_bessel_jf");
    FN_F_FF ellint_1f = GP(m2, FN_F_FF, "__std_smf_ellint_1f");
    FN_F_F zetaf = GP(m2, FN_F_F, "__std_smf_riemann_zetaf");
    FN_F_F expintf = GP(m2, FN_F_F, "__std_smf_expintf");
    FN_F_FFF hypot3f = GP(m2, FN_F_FFF, "__std_smf_hypot3f");
    report("resolve float exports",
           betaf && cyl_bessel_jf && ellint_1f && zetaf && expintf && hypot3f, "");
    if (!betaf || !cyl_bessel_jf || !ellint_1f || !zetaf || !expintf || !hypot3f)
        return;
    sprintf(det, "got %.9g", betaf(2.5f, 3.5f));
    report("value betaf(2.5,3.5)", close_rel(betaf(2.5f, 3.5f), 0.03681553890925539, 2e-6), det);
    sprintf(det, "got %.9g", cyl_bessel_jf(2.5f, 2.0f));
    report("value cyl_bessel_jf(2.5,2)", close_rel(cyl_bessel_jf(2.5f, 2.0f), 0.22392453146891578, 2e-6), det);
    sprintf(det, "got %.9g", ellint_1f(0.5f, 0.7f));
    report("value ellint_1f(0.5,0.7)", close_rel(ellint_1f(0.5f, 0.7f), 0.7136205028677777, 2e-6), det);
    sprintf(det, "got %.9g", zetaf(3.5f));
    report("value riemann_zetaf(3.5)", close_rel(zetaf(3.5f), 1.1267338673170566, 2e-6), det);
    sprintf(det, "got %.9g", expintf(1.0f));
    report("value expintf(1)", close_rel(expintf(1.0f), 1.8951178163559368, 2e-6), det);
    report("value hypot3f(3,4,12)=13", close_rel(hypot3f(3.0f, 4.0f, 12.0f), 13.0, 1e-6), "");
}

static void run_identities(HMODULE m2)
{
    FN_D_DD beta = GP(m2, FN_D_DD, "__std_smf_beta");
    FN_D_D zeta = GP(m2, FN_D_D, "__std_smf_riemann_zeta");
    FN_D_D comp_ellint_1 = GP(m2, FN_D_D, "__std_smf_comp_ellint_1");
    FN_D_DDD hypot3 = GP(m2, FN_D_DDD, "__std_smf_hypot3");
    FN_D_DD cyl_j = GP(m2, FN_D_DD, "__std_smf_cyl_bessel_j");
    FN_D_DD cyl_y = GP(m2, FN_D_DD, "__std_smf_cyl_neumann");
    report("resolve identity exports", beta && zeta && comp_ellint_1 && hypot3 && cyl_j && cyl_y, "");
    if (!beta || !zeta || !comp_ellint_1 || !hypot3 || !cyl_j || !cyl_y)
        return;
    report("identity beta symmetry",
           close_rel(beta(1.3, 2.9), beta(2.9, 1.3), 1e-12), "");
    report("identity zeta(2)=pi^2/6",
           close_rel(zeta(2.0), 1.6449340668482264, 1e-12), "");
    report("identity comp_ellint_1(0)=pi/2",
           close_rel(comp_ellint_1(0.0), 1.5707963267948966, 1e-13), "");
    report("value hypot3(3,4,12)=13", close_rel(hypot3(3.0, 4.0, 12.0), 13.0, 1e-13), "");
    {
        double nu = 1.7, x = 4.2;
        double w = cyl_j(nu + 1, x) * cyl_y(nu, x) - cyl_j(nu, x) * cyl_y(nu + 1, x);
        report("identity Bessel Wronskian 2/(pi x)",
               close_rel(w, 2.0 / (3.14159265358979323846 * x), 1e-9), "");
    }
}

/* ------- msvcp140_atomic_wait ------- */

typedef void (__stdcall *FN_EXEC_WAIT)(void *, unsigned char);
typedef void (__stdcall *FN_EXEC_WAKE)(void *);
typedef void *(__stdcall *FN_ACQUIRE)(void *);
typedef void (__stdcall *FN_RELEASE)(void *);
typedef void *(__stdcall *FN_CALLOC)(size_t, size_t);
typedef void (__stdcall *FN_FREE)(void *);
typedef void (__stdcall *FN_DEL_ZONES)(void *);
typedef void (__stdcall *FN_DEL_ZONE)(void *);
typedef void (__stdcall *FN_DEL_LEAP)(void *);

static volatile unsigned char exec_byte;
static HANDLE exec_started;

static DWORD WINAPI exec_waiter(LPVOID unused)
{
    HMODULE aw = GetModuleHandleA("msvcp140_atomic_wait.dll");
    FN_EXEC_WAIT w = GP(aw, FN_EXEC_WAIT, "__std_execution_wait_on_uchar");
    (void)unused;
    SetEvent(exec_started);
    while (exec_byte == 0)
        w((void *)&exec_byte, 0);
    return 0;
}

static void run_atomic(void)
{
    HMODULE aw = LoadLibraryA("msvcp140_atomic_wait.dll");
    FN_EXEC_WAIT exec_wait = GP(aw, FN_EXEC_WAIT, "__std_execution_wait_on_uchar");
    FN_EXEC_WAKE exec_wake = GP(aw, FN_EXEC_WAKE, "__std_execution_wake_by_address_all");
    FN_ACQUIRE acquire = GP(aw, FN_ACQUIRE, "__std_acquire_shared_mutex_for_instance");
    FN_RELEASE release = GP(aw, FN_RELEASE, "__std_release_shared_mutex_for_instance");
    FN_CALLOC calloc_crt = GP(aw, FN_CALLOC, "__std_calloc_crt");
    FN_FREE free_crt = GP(aw, FN_FREE, "__std_free_crt");
    FN_DEL_ZONES del_zones = GP(aw, FN_DEL_ZONES, "__std_tzdb_delete_time_zones");
    FN_DEL_ZONE del_zone = GP(aw, FN_DEL_ZONE, "__std_tzdb_delete_current_zone");
    FN_DEL_LEAP del_leap = GP(aw, FN_DEL_LEAP, "__std_tzdb_delete_leap_seconds");
    char det[96];

    report("resolve atomic-wait exports",
           exec_wait && exec_wake && acquire && release && calloc_crt &&
           free_crt && del_zones && del_zone && del_leap, "");
    if (!exec_wait || !exec_wake || !acquire || !release || !calloc_crt ||
        !free_crt || !del_zones || !del_zone || !del_leap)
        return;

    /* execution wait/wake across a second thread */
    {
        HANDLE th;
        exec_byte = 0;
        exec_started = CreateEventA(NULL, TRUE, FALSE, NULL);
        th = CreateThread(NULL, 0, exec_waiter, NULL, 0, NULL);
        if (!th || WaitForSingleObject(exec_started, 10000) != WAIT_OBJECT_0)
        {
            report("execution wait/wake", 0, "thread did not start");
        }
        else
        {
            Sleep(200);
            exec_byte = 1;
            exec_wake((void *)&exec_byte);
            sprintf(det, "wait result %lu", (unsigned long)WaitForSingleObject(th, 10000));
            report("execution wait/wake", WaitForSingleObject(th, 0) == WAIT_OBJECT_0, det);
        }
        if (th) CloseHandle(th);
        CloseHandle(exec_started);
    }

    /* shared mutex instance identity + refcounting */
    {
        static int instance_key;
        void *m1 = acquire(&instance_key);
        void *m2 = acquire(&instance_key);
        void *m3 = acquire((void *)&exec_byte);
        report("shared mutex same instance same pointer", m1 && m1 == m2, "");
        report("shared mutex distinct instance distinct pointer", m3 && m3 != m1, "");
        release(&instance_key);
        release(&instance_key);
        release((void *)&exec_byte);
        report("shared mutex re-acquire after drain",
               acquire(&instance_key) != NULL, "");
        release(&instance_key);
    }

    /* CRT allocation pair */
    {
        unsigned char *p = (unsigned char *)calloc_crt(16, 8);
        int zeroed = 1;
        int i;
        if (p)
            for (i = 0; i < 128; i++)
                if (p[i]) zeroed = 0;
        report("calloc_crt zeroed block", p && zeroed, "");
        if (p) free_crt(p);
        free_crt(NULL);
        report("free_crt(NULL) safe", 1, "");
    }

    /* tzdb deleters over hand-built structs (layouts as in the DLL) */
    {
        struct tzdb_time_zones
        {
            int error; char *ver; unsigned int count; char **names; char **links;
        } *z = (struct tzdb_time_zones *)malloc(sizeof(struct tzdb_time_zones));
        struct tzdb_current_zone { int error; char *name; } *c =
            (struct tzdb_current_zone *)malloc(sizeof(struct tzdb_current_zone));
        z->error = 0; z->ver = NULL; z->count = 2;
        z->names = (char **)calloc(2, sizeof(char *));
        z->links = (char **)calloc(2, sizeof(char *));
        z->names[0] = (char *)malloc(8); strcpy(z->names[0], "ZoneA");
        z->names[1] = (char *)malloc(8); strcpy(z->names[1], "ZoneB");
        del_zones(z);
        c->error = 0;
        c->name = (char *)malloc(8); strcpy(c->name, "ZoneA");
        del_zone(c);
        del_leap(malloc(12));
        del_leap(NULL);
        report("tzdb deleters free hand-built structs", 1, "");
    }
}

int main(void)
{
    HMODULE m2 = LoadLibraryA("msvcp140_2.dll");
    unsigned i;
    int missing = 0;

    group("resolve-all");
    if (!m2)
    {
        report("LoadLibrary msvcp140_2.dll", 0, "load failed");
    }
    else
    {
        for (i = 0; i < sizeof(NAMES_D) / sizeof(NAMES_D[0]); i++)
            if (!GetProcAddress(m2, NAMES_D[i])) missing++;
        for (i = 0; i < sizeof(NAMES_F) / sizeof(NAMES_F[0]); i++)
            if (!GetProcAddress(m2, NAMES_F[i])) missing++;
        report("all 44 __std_smf_* exports resolve", missing == 0, "");
    }

    if (m2)
    {
        group("values");
        run_vals(m2);
        group("order");
        run_order_cases(m2);
        group("float");
        run_float_cases(m2);
        group("identities");
        run_identities(m2);
    }

    group("atomic");
    run_atomic();

    printf("fd169r-64: RESULT %d\n", failures);
    fflush(stdout);
    return failures ? 1 : 0;
}
