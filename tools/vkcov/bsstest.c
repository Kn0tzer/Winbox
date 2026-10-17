/* Minimal BSS host-write repro.
 * Build: x86_64-w64-mingw32-gcc -o bsstest.exe bsstest.c -I../../dxvk/src/include/vulkan/include
 * The bridge's vkCreateInstance writes the instance ID via wr64() to our
 * out-pointer. We pass a BSS global, then read it back.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

static VkInstance bss_inst;   /* BSS: must be zero at startup */
static uint64_t bss_sentinel; /* BSS */

int main(void) {
    printf("BSSTEST: bss_inst addr=%p init=0x%llx\n",
           (void*)&bss_inst, (unsigned long long)bss_inst);
    fflush(stdout);

    HMODULE vkdll = LoadLibraryA("vulkan-1.dll");
    if (!vkdll) { printf("BSSTEST: no vulkan-1.dll\n"); return 2; }
    PFN_vkCreateInstance pCreate = (PFN_vkCreateInstance)(void*)GetProcAddress(vkdll, "vkCreateInstance");
    if (!pCreate) { printf("BSSTEST: no vkCreateInstance\n"); return 3; }

    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "bsstest", 1, "bsstest", 1, VK_API_VERSION_1_0 };
    VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &ai, 0, 0, 0, 0 };

    /* Guest-side sanity: write BSS ourselves, read back. */
    bss_sentinel = 0x123456789ABCDEF0ULL;
    printf("BSSTEST: guest-write bss_sentinel=0x%llx\n", (unsigned long long)bss_sentinel);
    fflush(stdout);

    /* Bridge write to BSS out-pointer. */
    VkResult r = pCreate(&ci, 0, &bss_inst);
    printf("BSSTEST: vkCreateInstance r=%d bss_inst=0x%llx (expect nonzero 0x564b....)\n",
           (int)r, (unsigned long long)bss_inst);
    fflush(stdout);

    /* Control: bridge write to stack out-pointer. */
    VkInstance stk_inst = 0;
    r = pCreate(&ci, 0, &stk_inst);
    printf("BSSTEST: stack control r=%d stk_inst=0x%llx addr=%p\n",
           (int)r, (unsigned long long)stk_inst, (void*)&stk_inst);
    fflush(stdout);

    int ok = (bss_inst != 0);
    printf("BSSTEST: %s\n", ok ? "PASS" : "FAIL");
    fflush(stdout);
    return ok ? 0 : 1;
}
