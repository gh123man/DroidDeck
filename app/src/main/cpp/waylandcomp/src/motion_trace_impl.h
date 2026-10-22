#include "motion_trace.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef DD_MOTION_CAPACITY
#define DD_MOTION_CAPACITY 65536
#endif
#define DD_MOTION_SLOTS 8
#ifndef DD_MOTION_PIXEL_CAPACITY
#define DD_MOTION_PIXEL_CAPACITY 2048
#endif

struct dd_motion_ring {
    struct dd_motion_record *records;
    uint64_t count, dropped;
    unsigned busy;
};
static struct {
    pthread_t thread;
    unsigned running, initialized, active, mode, slots;
    uint64_t unregistered, source_serial;
    int64_t epoch, end;
    char directory[512], process[16];
    struct dd_motion_ring rings[DD_MOTION_SLOTS];
    struct dd_motion_pixels *pixels;
    unsigned pixels_enabled, pixels_busy;
    uint64_t pixels_count, pixels_dropped;
} dd_mt;
static __thread int dd_mt_slot = -1;
static __thread uint64_t dd_mt_source;

int64_t dd_motion_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void dd_motion_emit(uint32_t event, uint32_t flags, uint64_t id, uint64_t related,
                    const double *values, unsigned count) {
    unsigned generation = __atomic_load_n(&dd_mt.active, __ATOMIC_ACQUIRE);
    if (!generation) return;
    if (dd_mt_slot == -1) {
        unsigned slot = __atomic_fetch_add(&dd_mt.slots, 1, __ATOMIC_RELAXED);
        dd_mt_slot = slot < DD_MOTION_SLOTS ? (int)slot : -2;
    }
    if (dd_mt_slot < 0) {
        __atomic_fetch_add(&dd_mt.unregistered, 1, __ATOMIC_RELAXED);
        return;
    }
    struct dd_motion_ring *ring = &dd_mt.rings[dd_mt_slot];
    __atomic_store_n(&ring->busy, 1, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&dd_mt.active, __ATOMIC_SEQ_CST) == generation) {
        int64_t now = dd_motion_now();
        if (now >= dd_mt.epoch && now < dd_mt.end) {
            if (ring->count < DD_MOTION_CAPACITY) {
                struct dd_motion_record *r = &ring->records[ring->count++];
                r->ns = now; r->id = id; r->related = related; r->event = event; r->flags = flags;
                memset(r->v, 0, sizeof(r->v));
                if (count > 12) count = 12;
                if (count) memcpy(r->v, values, count * sizeof(double));
            } else ring->dropped++;
        }
    }
    __atomic_store_n(&ring->busy, 0, __ATOMIC_SEQ_CST);
}

unsigned dd_motion_mode(int64_t *epoch) {
    unsigned mode = __atomic_load_n(&dd_mt.mode, __ATOMIC_ACQUIRE);
    *epoch = __atomic_load_n(&dd_mt.epoch, __ATOMIC_RELAXED);
    return mode;
}

void dd_motion_yaw(unsigned mode, int64_t epoch, int64_t now, double q[4]) {
    double t = now > epoch ? (double)(now - epoch) * 1e-9 : 0.0;
    double angle = mode == 2 ? (3.141592653589793 / 6) * sin(t * (3.141592653589793 / 4)) : 0.0;
    q[0] = cos(angle / 2); q[1] = 0; q[2] = sin(angle / 2); q[3] = 0;
}

uint64_t dd_motion_source(uint64_t sensor_ns, int64_t received_ns, uint32_t flags,
                          const double values[10]) {
    if (!__atomic_load_n(&dd_mt.initialized, __ATOMIC_ACQUIRE)) return 0;
    dd_mt_source = __atomic_add_fetch(&dd_mt.source_serial, 1, __ATOMIC_RELAXED);
    double v[12];
    memcpy(v, values, 10 * sizeof(double));
    v[10] = (double)received_ns; v[11] = 0;
    dd_motion_emit(DD_MOTION_SENSOR, flags, dd_mt_source, sensor_ns, v, 12);
    return dd_mt_source;
}
void dd_motion_clear_source(void) { dd_mt_source = 0; }
uint64_t dd_motion_source_id(void) { return dd_mt_source; }

int dd_motion_pixels_enabled(void) {
    return __atomic_load_n(&dd_mt.active, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&dd_mt.pixels_enabled, __ATOMIC_ACQUIRE);
}
int dd_motion_pixels_recording(void) {
    if (!dd_motion_pixels_enabled()) return 0;
    int64_t now = dd_motion_now();
    return now >= __atomic_load_n(&dd_mt.epoch, __ATOMIC_RELAXED) &&
        now < __atomic_load_n(&dd_mt.end, __ATOMIC_RELAXED);
}
void dd_motion_pixels_emit(uint64_t frame, uint64_t texture, const void *rgba) {
    unsigned generation = __atomic_load_n(&dd_mt.active, __ATOMIC_ACQUIRE);
    if (!generation || !dd_motion_pixels_enabled()) return;
    __atomic_store_n(&dd_mt.pixels_busy, 1, __ATOMIC_SEQ_CST);
    int64_t now = dd_motion_now();
    if (__atomic_load_n(&dd_mt.active, __ATOMIC_SEQ_CST) == generation &&
            now >= __atomic_load_n(&dd_mt.epoch, __ATOMIC_RELAXED) &&
            now < __atomic_load_n(&dd_mt.end, __ATOMIC_RELAXED)) {
        if (rgba && dd_mt.pixels_count < DD_MOTION_PIXEL_CAPACITY) {
            struct dd_motion_pixels *p = &dd_mt.pixels[dd_mt.pixels_count++];
            p->ns = now; p->frame = frame; p->texture = texture;
            memcpy(p->rgba, rgba, sizeof(p->rgba));
        } else dd_mt.pixels_dropped++;
    }
    __atomic_store_n(&dd_mt.pixels_busy, 0, __ATOMIC_SEQ_CST);
}

static void dd_mt_quiesce(void) {
    __atomic_store_n(&dd_mt.active, 0, __ATOMIC_SEQ_CST);
    for (unsigned i = 0; i < DD_MOTION_SLOTS; i++)
        while (__atomic_load_n(&dd_mt.rings[i].busy, __ATOMIC_SEQ_CST)) {
            struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
        }
    while (__atomic_load_n(&dd_mt.pixels_busy, __ATOMIC_SEQ_CST)) {
        struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
    }
}

static int dd_mt_export(uint64_t run, unsigned mode) {
    dd_mt_quiesce();
    int ok = 1;
    for (unsigned i = 0; i < DD_MOTION_SLOTS; i++) {
        struct dd_motion_ring *ring = &dd_mt.rings[i];
        if (!ring->records) continue;
        char path[768], tmp[784];
        snprintf(path, sizeof(path), "%s/motion-%llu-%s-%d-%u.bin", dd_mt.directory,
                 (unsigned long long)run, dd_mt.process, getpid(), i);
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        struct dd_motion_header h;
        memset(&h, 0, sizeof(h));
        memcpy(h.magic, "DDMOTION", 8); h.version = 1; h.record_size = sizeof(struct dd_motion_record);
        h.run = run; h.start_ns = dd_mt.epoch; h.end_ns = dd_mt.end;
        h.count = ring->count; h.dropped = ring->dropped;
        h.slot = i; h.mode = mode; h.pid = getpid(); h.capacity = DD_MOTION_CAPACITY;
        h.unregistered = __atomic_load_n(&dd_mt.unregistered, __ATOMIC_RELAXED);
        h.reserved[0] = (uint64_t)dd_motion_now();
        h.reserved[1] = (int64_t)h.reserved[0] >= h.end_ns;
        snprintf(h.process, sizeof(h.process), "%s", dd_mt.process);
        FILE *f = fopen(tmp, "wb");
        if (!f) { ok = 0; continue; }
        int written = fwrite(&h, sizeof(h), 1, f) == 1 &&
            fwrite(ring->records, sizeof(struct dd_motion_record), ring->count, f) == ring->count;
        if (fclose(f) != 0) written = 0;
        if (!written || rename(tmp, path) != 0) ok = 0;
    }
    if (dd_mt.pixels_enabled) {
        char path[768], tmp[784];
        snprintf(path, sizeof(path), "%s/motion-%llu-%s-%d.pixels", dd_mt.directory,
                 (unsigned long long)run, dd_mt.process, getpid());
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        struct dd_motion_header h;
        memset(&h, 0, sizeof(h)); memcpy(h.magic, "DDPIXELS", 8);
        h.version = 1; h.record_size = sizeof(struct dd_motion_pixels); h.run = run;
        h.start_ns = dd_mt.epoch; h.end_ns = dd_mt.end;
        h.count = dd_mt.pixels_count; h.dropped = dd_mt.pixels_dropped;
        h.mode = mode; h.pid = getpid(); h.capacity = DD_MOTION_PIXEL_CAPACITY;
        snprintf(h.process, sizeof(h.process), "%s", dd_mt.process);
        h.reserved[0] = (uint64_t)dd_motion_now(); h.reserved[1] = (int64_t)h.reserved[0] >= h.end_ns;
        h.reserved[2] = DD_MOTION_PIXEL_WIDTH; h.reserved[3] = DD_MOTION_PIXEL_HEIGHT;
        FILE *f = fopen(tmp, "wb");
        if (!f) ok = 0;
        else {
            int written = fwrite(&h, sizeof(h), 1, f) == 1 &&
                fwrite(dd_mt.pixels, sizeof(struct dd_motion_pixels), h.count, f) == h.count;
            if (fclose(f) != 0) written = 0;
            if (!written || rename(tmp, path) != 0) ok = 0;
        }
    }
    return ok;
}

static void *dd_mt_control(void *unused) {
    (void)unused;
    uint64_t last_run = 0, run = 0;
    unsigned mode = 0, generation = 0;
    int capturing = 0, exported = 0;
    char config[768], status[768], temporary[784];
    snprintf(config, sizeof(config), "%s/motion-control", dd_mt.directory);
    snprintf(status, sizeof(status), "%s/motion-%s-%d.status", dd_mt.directory, dd_mt.process, getpid());
    snprintf(temporary, sizeof(temporary), "%s.tmp", status);
    while (__atomic_load_n(&dd_mt.running, __ATOMIC_ACQUIRE)) {
        int64_t now = dd_motion_now();
        if (capturing && now >= dd_mt.end) {
            __atomic_store_n(&dd_mt.mode, 0, __ATOMIC_RELEASE);
            exported = dd_mt_export(run, mode) ? 1 : -1;
            capturing = 0;
        }
        FILE *f = fopen(config, "r");
        unsigned long long requested = 0;
        long long epoch = 0;
        unsigned requested_mode = 0, trace = 0, duration_ms = 0;
        int valid = f && fscanf(f, "%llu %u %u %lld %u", &requested, &requested_mode, &trace,
                                &epoch, &duration_ms) == 5;
        if (f) fclose(f);
        if (valid && requested > last_run && requested_mode <= 2 && trace <= 1 &&
            duration_ms >= 100 && duration_ms <= 30000 && epoch > now && !capturing) {
            int ready = 1;
            dd_mt_quiesce();
            for (unsigned i = 0; i < DD_MOTION_SLOTS; i++) {
                struct dd_motion_ring *ring = &dd_mt.rings[i];
                if (!ring->records) {
                    ring->records = (struct dd_motion_record *)calloc(DD_MOTION_CAPACITY, sizeof(*ring->records));
                    if (!ring->records) { ready = 0; break; }
                    volatile char *p = (volatile char *)ring->records;
                    for (size_t j = 0; j < DD_MOTION_CAPACITY * sizeof(*ring->records); j += 4096) p[j] = 0;
                }
                ring->count = ring->dropped = 0;
            }
            char pixel_flag[768];
            snprintf(pixel_flag, sizeof(pixel_flag), "%s/motion-pixels", dd_mt.directory);
            unsigned pixels = !strcmp(dd_mt.process, "driver") && access(pixel_flag, F_OK) == 0;
            if (pixels && !dd_mt.pixels) {
                dd_mt.pixels = (struct dd_motion_pixels *)calloc(DD_MOTION_PIXEL_CAPACITY, sizeof(*dd_mt.pixels));
                if (!dd_mt.pixels) ready = 0;
                else {
                    volatile char *p = (volatile char *)dd_mt.pixels;
                    for (size_t j = 0; j < DD_MOTION_PIXEL_CAPACITY * sizeof(*dd_mt.pixels); j += 4096) p[j] = 0;
                }
            }
            dd_mt.pixels_count = dd_mt.pixels_dropped = 0;
            __atomic_store_n(&dd_mt.pixels_enabled, pixels, __ATOMIC_RELEASE);
            if (ready && epoch > dd_motion_now()) {
                run = last_run = requested; mode = requested_mode; exported = 0;
                __atomic_store_n(&dd_mt.epoch, (int64_t)epoch, __ATOMIC_RELAXED);
                __atomic_store_n(&dd_mt.end, epoch + (int64_t)duration_ms * 1000000, __ATOMIC_RELAXED);
                __atomic_store_n(&dd_mt.mode, mode, __ATOMIC_RELEASE);
                __atomic_store_n(&dd_mt.unregistered, 0, __ATOMIC_RELAXED);
                if (trace) __atomic_store_n(&dd_mt.active, ++generation, __ATOMIC_RELEASE);
                capturing = 1;
            }
        }
        f = fopen(temporary, "w");
        if (f) {
            fprintf(f, "run=%llu mode=%u active=%d exported=%d now_ns=%lld start_ns=%lld end_ns=%lld\n",
                    (unsigned long long)run, mode, capturing, exported, (long long)dd_motion_now(),
                    (long long)dd_mt.epoch, (long long)dd_mt.end);
            fclose(f); rename(temporary, status);
        }
        struct timespec pause = {0, 200000000}; nanosleep(&pause, NULL);
    }
    if (capturing) dd_mt_export(run, mode);
    dd_mt_quiesce();
    return NULL;
}

void dd_motion_start(const char *directory, const char *process) {
    char enabled[768];
    snprintf(enabled, sizeof(enabled), "%s/motion-enabled", directory);
    if (access(enabled, F_OK) != 0) return;
    if (__atomic_exchange_n(&dd_mt.initialized, 1, __ATOMIC_ACQ_REL)) return;
    snprintf(dd_mt.directory, sizeof(dd_mt.directory), "%s", directory);
    snprintf(dd_mt.process, sizeof(dd_mt.process), "%s", process);
    __atomic_store_n(&dd_mt.running, 1, __ATOMIC_RELEASE);
    if (pthread_create(&dd_mt.thread, NULL, dd_mt_control, NULL)) {
        __atomic_store_n(&dd_mt.running, 0, __ATOMIC_RELEASE);
    }
}

void dd_motion_stop(void) {
    if (__atomic_exchange_n(&dd_mt.running, 0, __ATOMIC_ACQ_REL)) pthread_join(dd_mt.thread, NULL);
    __atomic_store_n(&dd_mt.mode, 0, __ATOMIC_RELEASE);
}
