#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define FRAME_COUNT 61
#define INTERVAL_TOLERANCE 0.05

static double elapsed_ms(const struct timespec *start,
                         const struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * 1000.0 +
           (end->tv_nsec - start->tv_nsec) / 1000000.0;
}

int main(int argc, char **argv)
{
    const char *device = argc > 1 ? argv[1] : "/dev/video0";
    struct v4l2_streamparm parm = {0};
    struct v4l2_format fmt = {0};
    struct timespec first, last;
    unsigned int expected_bpl;
    unsigned int expected_size;
    double reported_ms;
    double measured_ms;
    double error;
    uint8_t *frame;
    int format_ok;
    int timing_ok;
    int fd;
    int i;

    fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        perror("VIDIOC_G_FMT");
        close(fd);
        return 1;
    }

    if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24)
        expected_bpl = fmt.fmt.pix.width * 3;
    else if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
        expected_bpl = fmt.fmt.pix.width * 2;
    else {
        fprintf(stderr, "Test supports RGB24 and YUYV only\n");
        close(fd);
        return 1;
    }

    expected_size = expected_bpl * fmt.fmt.pix.height;
    format_ok = fmt.fmt.pix.bytesperline == expected_bpl &&
                fmt.fmt.pix.sizeimage == expected_size;

    printf("Format: %ux%u %c%c%c%c\n", fmt.fmt.pix.width,
           fmt.fmt.pix.height, fmt.fmt.pix.pixelformat & 0xff,
           (fmt.fmt.pix.pixelformat >> 8) & 0xff,
           (fmt.fmt.pix.pixelformat >> 16) & 0xff,
           (fmt.fmt.pix.pixelformat >> 24) & 0xff);
    printf("Stride/bytesperline: reported=%u expected=%u [%s]\n",
           fmt.fmt.pix.bytesperline, expected_bpl,
           fmt.fmt.pix.bytesperline == expected_bpl ? "PASS" : "FAIL");
    printf("Sizeimage: reported=%u expected=%u [%s]\n",
           fmt.fmt.pix.sizeimage, expected_size,
           fmt.fmt.pix.sizeimage == expected_size ? "PASS" : "FAIL");

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_PARM, &parm) < 0 ||
        !parm.parm.capture.timeperframe.denominator) {
        perror("VIDIOC_G_PARM");
        close(fd);
        return 1;
    }

    reported_ms =
        1000.0 * parm.parm.capture.timeperframe.numerator /
        parm.parm.capture.timeperframe.denominator;
    frame = malloc(fmt.fmt.pix.sizeimage);
    if (!frame) {
        close(fd);
        return 1;
    }

    for (i = 0; i < FRAME_COUNT; i++) {
        if (read(fd, frame, fmt.fmt.pix.sizeimage) < 0) {
            perror("read");
            free(frame);
            close(fd);
            return 1;
        }
        if (i == 0)
            clock_gettime(CLOCK_MONOTONIC, &first);
        else if (i == FRAME_COUNT - 1)
            clock_gettime(CLOCK_MONOTONIC, &last);
    }

    measured_ms = elapsed_ms(&first, &last) / (FRAME_COUNT - 1);
    error = measured_ms > reported_ms
                ? (measured_ms - reported_ms) / reported_ms
                : (reported_ms - measured_ms) / reported_ms;
    timing_ok = error <= INTERVAL_TOLERANCE;

    printf("Frame interval: reported=%.3f ms measured=%.3f ms "
           "error=%.1f%% [%s]\n",
           reported_ms, measured_ms, error * 100.0,
           timing_ok ? "PASS" : "FAIL");

    free(frame);
    close(fd);
    return format_ok && timing_ok ? 0 : 1;
}
