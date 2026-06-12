#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void print_format(const char *label, const struct v4l2_format *fmt)
{
    const struct v4l2_pix_format *pix = &fmt->fmt.pix;

    printf("%s: %ux%u %c%c%c%c, bytesperline=%u, sizeimage=%u\n",
           label, pix->width, pix->height, pix->pixelformat & 0xff,
           (pix->pixelformat >> 8) & 0xff,
           (pix->pixelformat >> 16) & 0xff,
           (pix->pixelformat >> 24) & 0xff, pix->bytesperline,
           pix->sizeimage);
}

int main(int argc, char **argv)
{
    const char *device = argc > 1 ? argv[1] : "/dev/video0";
    struct v4l2_requestbuffers req = {
        .count = 2,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    struct v4l2_buffer buf = {
        .index = 0,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    int saved_errno;
    int fd;
    int ret = 1;

    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT YUYV");
        goto out;
    }
    print_format("Initial format", &fmt);

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        goto out;
    }
    printf("Allocated buffers: %u\n", req.count);

    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
        perror("VIDIOC_QUERYBUF");
        goto release_buffers;
    }
    printf("Buffer 0 length: %u\n", buf.length);

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    errno = 0;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        saved_errno = errno;
        printf("S_FMT after REQBUFS: failed: %s\n", strerror(saved_errno));
    } else {
        printf("S_FMT after REQBUFS: succeeded\n");
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        perror("VIDIOC_G_FMT");
        goto release_buffers;
    }
    print_format("Current format", &fmt);

    errno = 0;
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        saved_errno = errno;
        printf("QBUF after S_FMT: failed: %s\n", strerror(saved_errno));
    } else {
        printf("QBUF after S_FMT: succeeded\n");
    }
    ret = 0;

release_buffers:
    req.count = 0;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0)
        perror("VIDIOC_REQBUFS release");
out:
    close(fd);
    return ret;
}
