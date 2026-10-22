#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define DD_MOTION_CAPACITY 64
#define DD_MOTION_PIXEL_CAPACITY 2
#include "../../../app/src/main/cpp/waylandcomp/src/motion_trace_impl.h"
#include <assert.h>
#include <dirent.h>

static void pause_ms(long ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000}; nanosleep(&t, NULL);
}
static void arm(const char *dir, unsigned run, unsigned trace, int64_t epoch) {
    char path[768]; snprintf(path, sizeof(path), "%s/motion-control", dir);
    FILE *f = fopen(path, "w"); assert(f);
    fprintf(f, "%u 2 %u %lld 100\n", run, trace, (long long)epoch); assert(fclose(f) == 0);
}
static void *writer(void *arg) {
    int64_t end = *(int64_t *)arg;
    while (dd_motion_now() < end) {
        double v[] = {1, 2, 3};
        dd_motion_emit(DD_MOTION_SENT, 1, 123, 456, v, 3);
        pause_ms(1);
    }
    return NULL;
}
static void *pixel_writer(void *unused) {
    (void)unused;
    while (!dd_motion_pixels_recording()) pause_ms(1);
    unsigned char rgba[DD_MOTION_PIXEL_BYTES]; memset(rgba, 123, sizeof(rgba));
    dd_motion_pixels_emit(45, 100, rgba);
    dd_motion_pixels_emit(46, 100, rgba);
    dd_motion_pixels_emit(47, 100, rgba);
    return NULL;
}
int main(void) {
    char dir[] = "/tmp/dd-motion-test-XXXXXX"; assert(mkdtemp(dir));
    dd_motion_start(dir, "driver"); assert(dd_mt.initialized == 0);
    char enabled[768]; snprintf(enabled, sizeof(enabled), "%s/motion-enabled", dir);
    FILE *enable = fopen(enabled, "w"); assert(enable); fclose(enable);
    snprintf(enabled, sizeof(enabled), "%s/motion-pixels", dir);
    enable = fopen(enabled, "w"); assert(enable); fclose(enable);
    dd_motion_start(dir, "driver");
    int64_t epoch = dd_motion_now() + 600000000;
    arm(dir, 1, 1, epoch);
    pthread_t pixel_thread;
    assert(pthread_create(&pixel_thread, NULL, pixel_writer, NULL) == 0);
    pthread_t writers[3]; int64_t end = epoch + 150000000;
    for (int i = 0; i < 3; i++) assert(pthread_create(&writers[i], NULL, writer, &end) == 0);
    for (int i = 0; i < 3; i++) pthread_join(writers[i], NULL);
    pthread_join(pixel_thread, NULL);
    pause_ms(300);
    uint64_t count = 0, dropped = 0;
    for (int i = 0; i < DD_MOTION_SLOTS; i++) {
        char path[768]; snprintf(path, sizeof(path), "%s/motion-1-driver-%d-%d.bin", dir, getpid(), i);
        FILE *f = fopen(path, "rb"); assert(f);
        struct dd_motion_header h; assert(fread(&h, sizeof(h), 1, f) == 1);
        assert(h.count <= DD_MOTION_CAPACITY && h.start_ns == epoch && h.mode == 2 && h.reserved[1]);
        count += h.count; dropped += h.dropped;
        for (uint64_t j = 0; j < h.count; j++) {
            struct dd_motion_record r; assert(fread(&r, sizeof(r), 1, f) == 1);
            assert(r.ns >= h.start_ns && r.ns < h.end_ns);
            assert(r.id == 123 && r.related == 456 && r.v[2] == 3 && r.v[11] == 0);
        }
        assert(fgetc(f) == EOF); fclose(f);
    }
    assert(count == 3 * DD_MOTION_CAPACITY && dropped > 0);
    char pixel_path[768]; snprintf(pixel_path, sizeof(pixel_path), "%s/motion-1-driver-%d.pixels", dir, getpid());
    FILE *pf = fopen(pixel_path, "rb"); assert(pf);
    struct dd_motion_header ph; assert(fread(&ph, sizeof(ph), 1, pf) == 1);
    assert(!memcmp(ph.magic, "DDPIXELS", 8) && ph.count == 2 && ph.dropped == 1 && ph.reserved[1]);
    struct dd_motion_pixels pixel;
    assert(fread(&pixel, sizeof(pixel), 1, pf) == 1);
    assert(pixel.frame == 45 && pixel.texture == 100 && pixel.rgba[0] == 123 && pixel.rgba[DD_MOTION_PIXEL_BYTES-1] == 123);
    fclose(pf);
    epoch = dd_motion_now() + 600000000;
    arm(dir, 2, 0, epoch); pause_ms(1000);
    char path[768]; snprintf(path, sizeof(path), "%s/motion-2-driver-%d-0.bin", dir, getpid());
    FILE *f = fopen(path, "rb"); assert(f);
    struct dd_motion_header h; assert(fread(&h, sizeof(h), 1, f) == 1); fclose(f);
    assert(h.count == 0 && h.dropped == 0);
    double q[4]; dd_motion_yaw(2, epoch, epoch + 2000000000, q);
    assert(fabs(q[0] - cos(3.141592653589793/12)) < 1e-12);
    dd_motion_yaw(1, epoch, epoch + 2000000000, q); assert(q[0] == 1 && q[2] == 0);
    epoch = dd_motion_now() + 600000000;
    arm(dir, 3, 1, epoch); pause_ms(300);
    dd_motion_stop();
    snprintf(path, sizeof(path), "%s/motion-3-driver-%d-0.bin", dir, getpid());
    f = fopen(path, "rb"); assert(f);
    assert(fread(&h, sizeof(h), 1, f) == 1); fclose(f);
    assert(h.reserved[1] == 0);
    DIR *d = opendir(dir); assert(d); struct dirent *entry;
    while ((entry = readdir(d))) {
        if (entry->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name); unlink(path);
    }
    closedir(d); rmdir(dir);
    puts("motion trace: bounded concurrent capture, overflow, export, rearm, off, synthetic PASS");
}
