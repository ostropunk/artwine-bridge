/* Windows-side test: load libusb.dll by path like Arturia does, find the MiniFreak. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

struct dd { uint8_t bLength, bDescriptorType; uint16_t bcdUSB; uint8_t cls, sub, proto, mps;
            uint16_t idVendor, idProduct, bcdDevice; uint8_t iManufacturer, iProduct, iSerial, nconf; };
typedef int (*init_t)(void **);
typedef intptr_t (*list_t)(void *, void ***);
typedef int (*desc_t)(void *, struct dd *);
typedef int (*open_t)(void *, void **);
typedef int (*str_t)(void *, uint8_t, unsigned char *, int);
typedef const char *(*err_t)(int);
typedef int (*hpreg_t)(void *, int, int, int, int, int, void *, void *, int *);
typedef int (*claim_t)(void *, int);

static int WINAPI on_hp(void *ctx, void *dev, int ev, void *u) { (void)ctx; (void)u; printf("  hotplug event %d dev=%p\n", ev, dev); return 0; }

int main(int argc, char **argv)
{
    HMODULE m = LoadLibraryA(argc > 1 ? argv[1] : "libusb.dll");
    if (!m) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
#define G(t, n) t n = (t)GetProcAddress(m, "libusb_" #n)
    G(init_t, init); G(list_t, get_device_list); G(desc_t, get_device_descriptor); G(open_t, open);
    G(str_t, get_string_descriptor_ascii); G(err_t, error_name); G(hpreg_t, hotplug_register_callback);
    G(claim_t, claim_interface);
    void *ctx = NULL, **devs;
    int r = init(&ctx);
    printf("init -> %d\n", r);
    if (r) return 1;
    intptr_t n = get_device_list(ctx, &devs);
    printf("%d devices\n", (int)n);
    for (intptr_t i = 0; i < n; i++) {
        struct dd d; get_device_descriptor(devs[i], &d);
        if (d.idVendor != 0x1c75) continue;
        printf("Arturia device %04x:%04x\n", d.idVendor, d.idProduct);
        void *h; r = open(devs[i], &h);
        printf("  open -> %s\n", error_name(r));
        if (r) continue;
        unsigned char s[256];
        if (get_string_descriptor_ascii(h, d.iProduct, s, sizeof s) > 0) printf("  product: %s\n", s);
        if (get_string_descriptor_ascii(h, d.iSerial, s, sizeof s) > 0) printf("  serial: %s\n", s);
        printf("  claim interface 0 -> %s\n", error_name(claim_interface(h, 0)));
    }
    int hh; r = hotplug_register_callback(ctx, 3, 1, 0x1c75, -1, -1, on_hp, NULL, &hh);
    printf("hotplug register -> %s\n", error_name(r));
    return 0;
}
