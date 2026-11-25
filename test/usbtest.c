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

struct xfer { void *h; UINT8 flags; unsigned char ep, type; unsigned int timeout; int status, length, actual;
              void (WINAPI *cb)(struct xfer *); void *user; unsigned char *buf; int niso; };
typedef struct xfer *(*alloc_t)(int);
typedef int (*xsub_t)(struct xfer *);
typedef void (*xfree_t)(struct xfer *);
typedef int (*hev_t)(void *, long *);
static volatile int done;
static void WINAPI on_xfer(struct xfer *x) { printf("  callback: status=%d actual=%d\n", x->status, x->actual); done = 1; }
static void pump(void *ctx, hev_t hev) { long tv[2] = { 0, 100000 }; for (int i = 0; i < 50 && !done; i++) hev(ctx, tv); }

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
        int claimed = claim_interface(h, 0);
        printf("  claim interface 0 -> %s\n", error_name(claimed));
        G(alloc_t, alloc_transfer); G(xsub_t, submit_transfer); G(xsub_t, cancel_transfer);
        G(xfree_t, free_transfer); G(hev_t, handle_events_timeout);
        /* async control: GET_DESCRIPTOR(device) */
        static unsigned char cbuf[8 + 18] = { 0x80, 6, 0, 1, 0, 0, 18, 0 };
        struct xfer *x = alloc_transfer(0);
        x->h = h; x->ep = 0; x->type = 0; x->timeout = 1000; x->buf = cbuf; x->length = sizeof cbuf; x->cb = on_xfer;
        done = 0;
        printf("async control submit -> %s\n", error_name(submit_transfer(x)));
        pump(ctx, handle_events_timeout);
        printf("  descriptor vid:pid %02x%02x:%02x%02x\n", cbuf[8 + 9], cbuf[8 + 8], cbuf[8 + 11], cbuf[8 + 10]);
        if (!claimed) {
            /* async bulk read that never completes, then cancel it */
            static unsigned char bbuf[1024];
            x->ep = 0x83; x->type = 2; x->timeout = 0; x->buf = bbuf; x->length = sizeof bbuf;
            done = 0;
            printf("async bulk submit -> %s\n", error_name(submit_transfer(x)));
            printf("cancel -> %s\n", error_name(cancel_transfer(x)));
            pump(ctx, handle_events_timeout);
        }
        free_transfer(x);
    }
    int hh; r = hotplug_register_callback(ctx, 3, 1, 0x1c75, -1, -1, on_hp, NULL, &hh);
    printf("hotplug register -> %s\n", error_name(r));
    return 0;
}
