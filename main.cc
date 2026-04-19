#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct buffer {
  void *start;
  size_t length;
  size_t bytesused;
  __u32 type;
  __u32 index;
  __u32 field;
  __u32 sequence;
  __u32 timestamp_type;
  __u64 timestamp;
  void *priv;
  __u32 memory;
};

struct CameraDevice {
  int fd;
  struct v4l2_capability capability;
  struct v4l2_cropcap cropcap;
  struct v4l2_input input;
  struct v4l2_format format;
  struct v4l2_streamparm streamparm;
  struct v4l2_buffer buffer;
  struct v4l2_requestbuffers requestbuffers;
  struct v4l2_crop crop;
};

static void errno_exit(const char *s) {
  fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
  exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg) {
  int r;

  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);

  return r;
}

/* 16bit/pixel interleaved YUV */
static void process_image(const void *_p, struct v4l2_pix_format *fmt) {
  const uint8_t *p = (const uint8_t *)_p;
  int8_t u;
  uint8_t y1;
  int8_t v;
  uint8_t y2;
  int r, g, b;
  int red = 0, x, y;
  int size = fmt->sizeimage;

  printf("Processing Frame: %dx%d %c%c%c%c\n", fmt->width, fmt->height,
         (fmt->pixelformat >> 0) & 0xff, (fmt->pixelformat >> 8) & 0xff,
         (fmt->pixelformat >> 16) & 0xff, (fmt->pixelformat >> 24) & 0xff);

  for (int i = 0; i < size; i += 4) {
    u = p[i + 0];
    y1 = p[i + 1];
    v = p[i + 2];
    y2 = p[i + 3];

    u -= 128;
    v -= 128;

    // YUV to RGB approximation
    r = y1 + v + (v >> 2) + (v >> 3) + (v >> 5);
    g = y1 - ((u >> 2) + (u >> 4) + (u >> 5)) -
        ((v >> 1) + (v >> 3) + (v >> 4) + (v >> 5));
    b = y1 + u + (u >> 1) + (u >> 2) + (u >> 6);
    if (r > 100 && g < 60 && b < 60)
      red++;

    r = y2 + v + (v >> 2) + (v >> 3) + (v >> 5);
    g = y2 - ((u >> 2) + (u >> 4) + (u >> 5)) -
        ((v >> 1) + (v >> 3) + (v >> 4) + (v >> 5));
    b = y2 + u + (u >> 1) + (u >> 2) + (u >> 6);
    if (r > 100 && g < 60 && b < 60)
      red++;

    /* describe pixels on first line every 250 pixels (colorbars) */
    x = (i >> 1) % fmt->width;
    y = (i >> 1) / fmt->width;
    if (y == 0 && !(x % 250)) {
      printf("[%4d,%4d] YUYV:0x%02x%02x%02x%02x ", x, y, y1, (uint8_t)u, y2,
             (uint8_t)v);
      printf("RGB:0x%02x%02x%02x\n", r, g, b);
    }
  }
  printf("red pixel count=%d\n", red);
}

static void save_frame(const char *path, const void *p, int size) {
  int fd, rz;

  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (fd < 0)
    perror("open");
  else {
    rz = write(fd, p, size);
    printf("Wrote %d of %d bytes to %s\n", rz, size, path);
    close(fd);
  }
}

int main(int argc, char **argv) {
  char *dev_name;
  int width, height;
  struct CameraDevice cam;
  struct buffer *buffers;
  unsigned int n_buffers;
  enum v4l2_buf_type type;
  v4l2_std_id std_id = V4L2_STD_UNKNOWN;
  unsigned int count;
  unsigned int i;
  char filename[32];
  const char *extension = "raw";

  /* parse args */
  if (argc < 5) {
    fprintf(stderr, "usage: %s <device> <width> <height> <count>\n", argv[0]);
    exit(1);
  }
  dev_name = argv[1];
  width = atoi(argv[2]);
  height = atoi(argv[3]);
  count = atoi(argv[4]);

  /* open device */
  cam.fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
  if (-1 == cam.fd) {
    fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno,
            strerror(errno));
    exit(EXIT_FAILURE);
  }

  /* ensure device has video capture capability */
  if (-1 == xioctl(cam.fd, VIDIOC_QUERYCAP, &cam.capability)) {
    if (errno == EINVAL) {
      fprintf(stderr, "%s is no V4L2 device\n", dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_QUERYCAP");
    }
  }
  if (!(cam.capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf(stderr, "%s is no video capture device\n", dev_name);
    exit(EXIT_FAILURE);
  }
  if (!(cam.capability.capabilities & V4L2_CAP_STREAMING)) {
    fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
    exit(EXIT_FAILURE);
  }

  /* get standard (only if it has a tuner/analog support) */
  if (cam.capability.capabilities & (V4L2_CAP_TUNER | V4L2_CAP_VBI_CAPTURE)) {
    if (-1 == xioctl(cam.fd, VIDIOC_G_STD, &std_id))
      perror("VIDIOC_G_STD");
    for (i = 0; std_id == V4L2_STD_ALL && i < 10; i++) {
      usleep(100000);
      xioctl(cam.fd, VIDIOC_G_STD, &std_id);
    }

    /* set the standard to the detected standard */
    if (std_id != V4L2_STD_UNKNOWN) {
      if (-1 == xioctl(cam.fd, VIDIOC_S_STD, &std_id))
        perror("VIDIOC_S_STD");
      if (std_id & V4L2_STD_NTSC)
        printf("found NTSC TV decoder\n");
      if (std_id & V4L2_STD_SECAM)
        printf("found SECAM TV decoder\n");
      if (std_id & V4L2_STD_PAL)
        printf("found PAL TV decoder\n");
    }
  }

  /* set video input */
  CLEAR(cam.input);
  cam.input.index = 0; 
  if (-1 == xioctl(cam.fd, VIDIOC_S_INPUT, &cam.input)) {
    if (errno != EINVAL)
      perror("VIDIOC_S_INPUT");
  }

  /* set framerate */
  CLEAR(cam.streamparm);
  cam.streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(cam.fd, VIDIOC_S_PARM, &cam.streamparm))
    perror("VIDIOC_S_PARM");

  /* get framerate */
  CLEAR(cam.streamparm);
  cam.streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(cam.fd, VIDIOC_G_PARM, &cam.streamparm))
    perror("VIDIOC_G_PARM");

  /* set format */
  CLEAR(cam.format);
  cam.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  cam.format.fmt.pix.width = width;
  cam.format.fmt.pix.height = height;
  cam.format.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
  cam.format.fmt.pix.field = V4L2_FIELD_ANY;
  if (-1 == xioctl(cam.fd, VIDIOC_S_FMT, &cam.format))
    errno_exit("VIDIOC_S_FMT");

  /* get and display format */
  CLEAR(cam.format);
  cam.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(cam.fd, VIDIOC_G_FMT, &cam.format))
    errno_exit("VIDIOC_G_FMT");

  if (cam.format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
    extension = "jpg";
  } else {
    extension = "raw";
  }

  printf("%s: %dx%d %c%c%c%c %2.2ffps\n", dev_name, cam.format.fmt.pix.width,
         cam.format.fmt.pix.height, (cam.format.fmt.pix.pixelformat >> 0) & 0xff,
         (cam.format.fmt.pix.pixelformat >> 8) & 0xff,
         (cam.format.fmt.pix.pixelformat >> 16) & 0xff,
         (cam.format.fmt.pix.pixelformat >> 24) & 0xff,
         cam.streamparm.parm.capture.timeperframe.numerator
             ? (float)cam.streamparm.parm.capture.timeperframe.denominator /
                   (float)cam.streamparm.parm.capture.timeperframe.numerator
             : 0);

  /* request buffers */
  CLEAR(cam.requestbuffers);
  cam.requestbuffers.count = 4;
  cam.requestbuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  cam.requestbuffers.memory = V4L2_MEMORY_MMAP;
  if (-1 == xioctl(cam.fd, VIDIOC_REQBUFS, &cam.requestbuffers)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s does not support memory mapping\n", dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_REQBUFS");
    }
  }
  if (cam.requestbuffers.count < 2) {
    fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
    exit(EXIT_FAILURE);
  }

  /* allocate buffers */
  buffers = (struct buffer *)calloc(cam.requestbuffers.count, sizeof(*buffers));
  if (!buffers) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }

  /* mmap buffers */
  for (n_buffers = 0; n_buffers < cam.requestbuffers.count; ++n_buffers) {
    struct v4l2_buffer buffer_info;

    CLEAR(buffer_info);

    buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer_info.memory = V4L2_MEMORY_MMAP;
    buffer_info.index = n_buffers;

    if (-1 == xioctl(cam.fd, VIDIOC_QUERYBUF, &buffer_info))
      errno_exit("VIDIOC_QUERYBUF");

    buffers[n_buffers].length = buffer_info.length;
    buffers[n_buffers].start =
        mmap(NULL /* start anywhere */, buffer_info.length,
             PROT_READ | PROT_WRITE /* required */,
             MAP_SHARED /* recommended */, cam.fd, buffer_info.m.offset);

    if (MAP_FAILED == buffers[n_buffers].start)
      errno_exit("mmap");
  }

  /* queue buffers */
  for (i = 0; i < n_buffers; ++i) {
    struct v4l2_buffer buffer_info;

    CLEAR(buffer_info);
    buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer_info.memory = V4L2_MEMORY_MMAP;
    buffer_info.index = i;

    if (-1 == xioctl(cam.fd, VIDIOC_QBUF, &buffer_info))
      errno_exit("VIDIOC_QBUF");
  }

  /* start capture */
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(cam.fd, VIDIOC_STREAMON, &type))
    errno_exit("VIDIOC_STREAMON");

  /* capture frame(s) (we throw away first incomplete frame ) */
  for (i = 0; i < count + 1; i++) {
    for (;;) {
      fd_set fds;
      struct timeval tv;
      int r;

      FD_ZERO(&fds);
      FD_SET(cam.fd, &fds);

      /* Timeout. */
      tv.tv_sec = 2;
      tv.tv_usec = 0;

      r = select(cam.fd + 1, &fds, NULL, NULL, &tv);
      if (-1 == r) {
        if (EINTR == errno)
          continue;
        errno_exit("select");
      }
      if (0 == r) {
        fprintf(stderr, "select timeout\n");
        exit(EXIT_FAILURE);
      }

      /* dequeue captured buffer */
      CLEAR(cam.buffer);
      cam.buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      cam.buffer.memory = V4L2_MEMORY_MMAP;
      if (-1 == xioctl(cam.fd, VIDIOC_DQBUF, &cam.buffer)) {
        if (errno == EAGAIN)
          continue;
        errno_exit("VIDIOC_DQBUF");
      }
      assert(cam.buffer.index < n_buffers);

      /* skip first image as it may not be sync'd */
      if (i > 0) {
        if (cam.format.fmt.pix.pixelformat == V4L2_PIX_FMT_UYVY ||
            cam.format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
          process_image(buffers[cam.buffer.index].start, &cam.format.fmt.pix);
        }
        sprintf(filename, "frame%d.%s", i, extension);
        save_frame(filename, buffers[cam.buffer.index].start,
                   cam.buffer.bytesused);
      }

      /* queue buffer */
      if (-1 == xioctl(cam.fd, VIDIOC_QBUF, &cam.buffer))
        errno_exit("VIDIOC_QBUF");

      break;
    }
  }

  /* stop capture */
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(cam.fd, VIDIOC_STREAMOFF, &type))
    errno_exit("VIDIOC_STREAMOFF");

  /* unmap and free buffers */
  for (i = 0; i < n_buffers; ++i)
    if (-1 == munmap(buffers[i].start, buffers[i].length))
      errno_exit("munmap");
  free(buffers);

  /* close device */
  if (-1 == close(cam.fd))
    errno_exit("close");

  fprintf(stderr, "\n");
  return 0;
}