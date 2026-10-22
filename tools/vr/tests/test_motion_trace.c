#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define DD_MOTION_CAPACITY 64
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
int main(void) {
    char dir[] = "/tmp/dd-motion-test-XXXXXX"; assert(mkdtemp(dir));
    dd_motion_start(dir, "test");
    int64_t epoch = dd_motion_now() + 600000000;
    arm(dir, 1, 1, epoch);
    pthread_t writers[3]; int64_t end = epoch + 150000000;
    for (int i = 0; i < 3; i++) assert(pthread_create(&writers[i], NULL, writer, &end) == 0);
    for (int i = 0; i < 3; i++) pthread_join(writers[i], NULL);
    pause_ms(300);
    uint64_t count = 0, dropped = 0;
    for (int i = 0; i < DD_MOTION_SLOTS; i++) {
        char path[768]; snprintf(path, sizeof(path), "%s/motion-1-test-%d-%d.bin", dir, getpid(), i);
        FILE *f = fopen(path, "rb"); assert(f);
        struct dd_motion_header h; assert(fread(&h, sizeof(h), 1, f) == 1);
        assert(h.count <= DD_MOTION_CAPACITY && h.start_ns == epoch && h.mode == 2);
        count += h.count; dropped += h.dropped;
        for (uint64_t j = 0; j < h.count; j++) {
            struct dd_motion_record r; assert(fread(&r, sizeof(r), 1, f) == 1);
            assert(r.ns >= h.start_ns && r.ns < h.end_ns);
            assert(r.id == 123 && r.related == 456 && r.v[2] == 3 && r.v[11] == 0);
        }
        assert(fgetc(f) == EOF); fclose(f);
    }
    assert(count == 3 * DD_MOTION_CAPACITY && dropped > 0);
    epoch = dd_motion_now() + 600000000;
    arm(dir, 2, 0, epoch); pause_ms(1000);
    char path[768]; snprintf(path, sizeof(path), "%s/motion-2-test-%d-0.bin", dir, getpid());
    FILE *f = fopen(path, "rb"); assert(f);
    struct dd_motion_header h; assert(fread(&h, sizeof(h), 1, f) == 1); fclose(f);
    assert(h.count == 0 && h.dropped == 0);
    double q[4]; dd_motion_yaw(2, epoch, epoch + 2000000000, q);
    assert(fabs(q[0] - cos(3.141592653589793/12)) < 1e-12);
    dd_motion_yaw(1, epoch, epoch + 2000000000, q); assert(q[0] == 1 && q[2] == 0);
    dd_motion_stop();
    DIR *d = opendir(dir); assert(d); struct dirent *entry;
    while ((entry = readdir(d))) {
        if (entry->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name); unlink(path);
    }
    closedir(d); rmdir(dir);
    puts("motion trace: bounded concurrent capture, overflow, export, rearm, off, synthetic PASS");
}
