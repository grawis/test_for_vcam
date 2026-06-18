#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUFFER_COUNT 3
#define FRAME_COUNT 10

struct mapped_buffer {
    void *start;
    size_t length;
};

static int xioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret < 0 && errno == EINTR);

    return ret;
}

static unsigned long long timeval_us(const struct timeval *tv)
{
    return (unsigned long long) tv->tv_sec * 1000000ULL + tv->tv_usec;
}

int main(int argc, char **argv)
{
    const char *device = argc > 1 ? argv[1] : "/dev/video0";
    int frame_count = argc > 2 ? atoi(argv[2]) : FRAME_COUNT;
    struct mapped_buffer buffers[BUFFER_COUNT] = {0};
    struct v4l2_requestbuffers req = {0};
    struct v4l2_format fmt = {0};
    enum v4l2_buf_type type;
    unsigned int expected_sequence = 0;
    unsigned long long last_ts = 0;
    int sequence_ok = 1;
    int timestamp_ok = 1;
    int bytesused_ok = 1;
    int fd;
    int i;

    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        perror("VIDIOC_G_FMT");
        close(fd);
        return 1;
    }

    printf("Format: %ux%u %c%c%c%c, sizeimage=%u\n", fmt.fmt.pix.width,
           fmt.fmt.pix.height, fmt.fmt.pix.pixelformat & 0xff,
           (fmt.fmt.pix.pixelformat >> 8) & 0xff,
           (fmt.fmt.pix.pixelformat >> 16) & 0xff,
           (fmt.fmt.pix.pixelformat >> 24) & 0xff,
           fmt.fmt.pix.sizeimage);

    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return 1;
    }

    if (req.count < BUFFER_COUNT) {
        fprintf(stderr, "Only %u buffers allocated\n", req.count);
        close(fd);
        return 1;
    }

    for (i = 0; i < BUFFER_COUNT; i++) {
        struct v4l2_buffer buf = {0};

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            close(fd);
            return 1;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            close(fd);
            return 1;
        }

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            close(fd);
            return 1;
        }
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        close(fd);
        return 1;
    }

    printf("Frame  Index  Sequence  Bytesused  Timestamp(us)\n");
    for (i = 0; i < frame_count; i++) {
        struct v4l2_buffer buf = {0};
        unsigned long long ts;

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
            break;
        }

        ts = timeval_us(&buf.timestamp);
        printf("%5d  %5u  %8u  %9u  %13llu\n", i, buf.index,
               buf.sequence, buf.bytesused, ts);

        if (buf.sequence != expected_sequence)
            sequence_ok = 0;
        if (i > 0 && ts <= last_ts)
            timestamp_ok = 0;
        if (buf.bytesused != fmt.fmt.pix.sizeimage)
            bytesused_ok = 0;

        expected_sequence++;
        last_ts = ts;

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF requeue");
            break;
        }
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0)
        perror("VIDIOC_STREAMOFF");

    for (i = 0; i < BUFFER_COUNT; i++) {
        if (buffers[i].start && buffers[i].start != MAP_FAILED)
            munmap(buffers[i].start, buffers[i].length);
    }

    req.count = 0;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0)
        perror("VIDIOC_REQBUFS release");

    close(fd);

    printf("Sequence: %s\n", sequence_ok ? "PASS" : "FAIL");
    printf("Timestamp: %s\n", timestamp_ok ? "PASS" : "FAIL");
    printf("Bytesused: %s\n", bytesused_ok ? "PASS" : "FAIL");

    return sequence_ok && timestamp_ok && bytesused_ok ? 0 : 1;
}
