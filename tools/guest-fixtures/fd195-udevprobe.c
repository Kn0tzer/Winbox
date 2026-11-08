
/* FD-195 temporary diagnostic: exercise the guest libudev exactly the way
 * winebus does and print every step's result. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

typedef struct udev udev_t; typedef struct udev_enumerate udev_enum_t;
typedef struct udev_list_entry udev_list_t; typedef struct udev_device udev_dev_t;
static udev_t* (*p_new)(void);
static udev_enum_t* (*p_enum_new)(udev_t*);
static int (*p_enum_sub)(udev_enum_t*, const char*);
static int (*p_enum_scan)(udev_enum_t*);
static udev_list_t* (*p_enum_list)(udev_enum_t*);
static udev_list_t* (*p_list_next)(udev_list_t*);
static const char* (*p_list_name)(udev_list_t*);
static udev_dev_t* (*p_dev_bysyspath)(udev_t*, const char*);
static const char* (*p_dev_devnode)(udev_dev_t*);
static const char* (*p_dev_syspath)(udev_dev_t*);
static const char* (*p_dev_subsys)(udev_dev_t*);
static const char* (*p_dev_prop)(udev_dev_t*, const char*);
static udev_dev_t* (*p_dev_parent_sub)(udev_dev_t*, const char*, const char*);
static int (*p_dev_init)(udev_dev_t*);

static void raw_check(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { printf("UDEVPROBE raw open %s -> ERR %d (%s)\n", path, errno, strerror(errno)); }
    else { char buf[256]; ssize_t n = read(fd, buf, sizeof(buf)-1); if (n<0) n=0; buf[n]=0;
           printf("UDEVPROBE raw open %s -> OK content=[%s]\n", path, buf); close(fd); }
    struct stat st; int r = stat(path, &st);
    printf("UDEVPROBE raw stat %s -> %d mode=%o rdev=%llx\n", path, r, r==0?(unsigned)st.st_mode:0, r==0?(unsigned long long)st.st_rdev:0ULL);
    fflush(stdout);
}

int main(void) {
    raw_check("/run/udev/data/c13:69");
    raw_check("/sys/class/input/event5/uevent");
    raw_check("/dev/input/event5");
    void *h = dlopen("libudev.so.1", RTLD_NOW);
    if (!h) { printf("UDEVPROBE dlopen libudev FAILED %s\n", dlerror()); fflush(stdout); return 1; }
    printf("UDEVPROBE dlopen libudev OK\n");
    p_new=(void*)dlsym(h,"udev_new"); p_enum_new=(void*)dlsym(h,"udev_enumerate_new");
    p_enum_sub=(void*)dlsym(h,"udev_enumerate_add_match_subsystem"); p_enum_scan=(void*)dlsym(h,"udev_enumerate_scan_devices");
    p_enum_list=(void*)dlsym(h,"udev_enumerate_get_list_entry");
    p_list_next=(void*)dlsym(h,"udev_list_entry_get_next"); p_list_name=(void*)dlsym(h,"udev_list_entry_get_name");
    p_dev_bysyspath=(void*)dlsym(h,"udev_device_new_from_syspath"); p_dev_devnode=(void*)dlsym(h,"udev_device_get_devnode");
    p_dev_syspath=(void*)dlsym(h,"udev_device_get_syspath"); p_dev_subsys=(void*)dlsym(h,"udev_device_get_subsystem");
    p_dev_prop=(void*)dlsym(h,"udev_device_get_property_value"); p_dev_parent_sub=(void*)dlsym(h,"udev_device_get_parent_with_subsystem_devtype");
    p_dev_init=(void*)dlsym(h,"udev_device_get_is_initialized");
    udev_t *u = p_new(); printf("UDEVPROBE udev_new -> %p\n", (void*)u);
    const char *subs[2] = {"input", "hidraw"};
    for (int i=0;i<2;i++) {
        udev_enum_t *e = p_enum_new(u);
        p_enum_sub(e, subs[i]);
        int r = p_enum_scan(e);
        printf("UDEVPROBE enumerate subsystem=%s scan=%d entries:", subs[i], r);
        int count=0;
        for (udev_list_t *le = p_enum_list(e); le; le = p_list_next(le)) { printf(" [%s]", p_list_name(le)); count++; }
        printf(" (count=%d)\n", count);
        fflush(stdout);
    }
    udev_dev_t *d = p_dev_bysyspath(u, "/sys/class/input/event5");
    printf("UDEVPROBE new_from_syspath(/sys/class/input/event5) -> %p\n", (void*)d);
    if (d) {
        const char *dn = p_dev_devnode(d);
        printf("UDEVPROBE devnode=%s syspath=%s subsystem=%s initialized=%d\n",
               dn?dn:"(null)", p_dev_syspath(d), p_dev_subsys(d)?p_dev_subsys(d):"(null)",
               p_dev_init?p_dev_init(d):-99);
        const char *j = p_dev_prop(d,"ID_INPUT_JOYSTICK"); const char *dv = p_dev_prop(d,"DEVNAME");
        printf("UDEVPROBE prop ID_INPUT_JOYSTICK=%s DEVNAME=%s\n", j?j:"(null)", dv?dv:"(null)");
        udev_dev_t *par = p_dev_parent_sub(d, "input", NULL);
        printf("UDEVPROBE parent(input) -> %p syspath=%s\n", (void*)par, par?p_dev_syspath(par):"(null)");
        if (par) { const char *pj = p_dev_prop(par,"ID_INPUT_JOYSTICK"); printf("UDEVPROBE parent prop ID_INPUT_JOYSTICK=%s\n", pj?pj:"(null)"); }
    }
    printf("UDEVPROBE DONE\n"); fflush(stdout);
    return 0;
}
