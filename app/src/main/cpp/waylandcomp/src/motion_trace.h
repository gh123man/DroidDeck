#ifndef DROIDDECK_MOTION_TRACE_H
#define DROIDDECK_MOTION_TRACE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum dd_motion_event {
    DD_MOTION_SENSOR = 1, DD_MOTION_PUBLISH = 2, DD_MOTION_DRIVER = 3,
    DD_MOTION_SENT = 4, DD_MOTION_LAYER = 5, DD_MOTION_PRESENT = 6,
    DD_MOTION_COPY = 7, DD_MOTION_VSYNC = 8, DD_MOTION_THROTTLE = 9,
    DD_MOTION_RECENTER = 10, DD_MOTION_ANDROID = 11, DD_MOTION_COMMIT = 12,
};

struct dd_motion_record {
    int64_t ns;
    uint64_t id, related;
    uint32_t event, flags;
    double v[12];
};

struct dd_motion_header {
    char magic[8];
    uint32_t version, record_size;
    uint64_t run;
    int64_t start_ns, end_ns;
    uint64_t count, dropped;
    uint32_t slot, mode, pid, capacity;
    uint64_t unregistered;
    char process[16];
    uint64_t reserved[4];
};

void dd_motion_start(const char *directory, const char *process);
void dd_motion_stop(void);
int64_t dd_motion_now(void);
void dd_motion_emit(uint32_t event, uint32_t flags, uint64_t id, uint64_t related,
                    const double *values, unsigned count);
unsigned dd_motion_mode(int64_t *epoch);
void dd_motion_yaw(unsigned mode, int64_t epoch, int64_t now, double q[4]);
uint64_t dd_motion_source(uint64_t sensor_ns, int64_t received_ns, uint32_t flags,
                          const double values[10]);
void dd_motion_clear_source(void);
uint64_t dd_motion_source_id(void);
#define DD_MOTION_PIXEL_WIDTH 128
#define DD_MOTION_PIXEL_HEIGHT 72
#define DD_MOTION_PIXEL_BYTES (DD_MOTION_PIXEL_WIDTH * DD_MOTION_PIXEL_HEIGHT * 4)
struct dd_motion_pixels {
    int64_t ns;
    uint64_t frame, texture;
    unsigned char rgba[DD_MOTION_PIXEL_BYTES];
};
int dd_motion_pixels_enabled(void);
int dd_motion_pixels_recording(void);
void dd_motion_pixels_emit(uint64_t frame, uint64_t texture, const void *rgba);

#ifdef __cplusplus
}
static_assert(sizeof(dd_motion_record) == 128, "motion record layout");
static_assert(sizeof(dd_motion_header) == 128, "motion header layout");
#else
_Static_assert(sizeof(struct dd_motion_record) == 128, "motion record layout");
_Static_assert(sizeof(struct dd_motion_header) == 128, "motion header layout");
#endif
#endif
