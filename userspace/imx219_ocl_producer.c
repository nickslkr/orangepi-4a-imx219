#define _GNU_SOURCE
#define CL_TARGET_OPENCL_VERSION 300

#include <CL/cl.h>
#include <linux/videodev2.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define W 1920
#define H 1080
#define NBUF 4

#define IN_SIZE  ((size_t)W * H * 2)
#define OUT_SIZE ((size_t)W * H * 3 / 2)

#define INPUT_DEV  "/dev/video0"
#define OUTPUT_DEV "/dev/video10"
#define KERNEL_CL  "/usr/local/share/opi4a-imx219/imx219_debayer_yuv420.cl"

struct buffer {
    void   *addr;
    size_t  len;
};

static volatile sig_atomic_t stop_flag;

static void on_signal(int sig)
{
    (void)sig;
    stop_flag = 1;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;

    do {
        r = ioctl(fd, req, arg);
    } while (r < 0 && errno == EINTR);

    return r;
}

static void die(const char *s)
{
    perror(s);
    exit(EXIT_FAILURE);
}

static void cl_die(cl_int err, const char *where)
{
    if (err != CL_SUCCESS) {
        fprintf(stderr, "%s: OpenCL error %d\n", where, err);
        exit(EXIT_FAILURE);
    }
}

static char *load_text(const char *path, size_t *len_out)
{
    FILE *f;
    long n;
    char *p;

    f = fopen(path, "rb");
    if (!f)
        die(path);

    if (fseek(f, 0, SEEK_END) != 0)
        die("fseek");

    n = ftell(f);
    if (n <= 0) {
        fprintf(stderr, "%s is empty\n", path);
        exit(EXIT_FAILURE);
    }

    rewind(f);

    p = malloc((size_t)n + 1);
    if (!p)
        die("malloc");

    if (fread(p, 1, (size_t)n, f) != (size_t)n)
        die("fread");

    fclose(f);

    p[n] = '\0';

    if (len_out)
        *len_out = (size_t)n;

    return p;
}

static void fourcc_string(uint32_t f, char s[5])
{
    s[0] = f & 0xff;
    s[1] = (f >> 8) & 0xff;
    s[2] = (f >> 16) & 0xff;
    s[3] = (f >> 24) & 0xff;
    s[4] = '\0';
}

static void write_full(int fd, const void *ptr, size_t len)
{
    const uint8_t *p = ptr;

    while (len && !stop_flag) {
        ssize_t n = write(fd, p, len);

        if (n > 0) {
            p += n;
            len -= (size_t)n;
            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd = {
                .fd = fd,
                .events = POLLOUT
            };

            if (poll(&pfd, 1, 1000) < 0 && errno != EINTR)
                die("poll output");

            continue;
        }

        die("write /dev/video10");
    }
}

int main(void)
{
    int cap_fd = -1;
    int out_fd = -1;

    struct buffer bufs[NBUF];
    unsigned nbufs = 0;

    cl_platform_id platform = NULL;
    cl_device_id device = NULL;
    cl_context ctx = NULL;
    cl_command_queue queue = NULL;
    cl_program program = NULL;
    cl_kernel kernel = NULL;
    cl_mem cl_in = NULL;
    cl_mem cl_out = NULL;

    uint8_t *host_out = NULL;
    char *cl_source = NULL;
    size_t cl_source_len = 0;

    cl_int err;

    memset(bufs, 0, sizeof(bufs));

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "IMX219 OpenCL producer\n");
    fprintf(stderr, "  input : %s RG10 %dx%d\n",
            INPUT_DEV, W, H);
    fprintf(stderr, "  output: %s YU12 %dx%d\n",
            OUTPUT_DEV, W, H);
    fprintf(stderr, "  kernel: %s\n", KERNEL_CL);

    /*
     * Find Rusticl platform.
     */
    {
        cl_uint np = 0;

        err = clGetPlatformIDs(0, NULL, &np);
        cl_die(err, "clGetPlatformIDs(count)");

        if (!np) {
            fprintf(stderr, "No OpenCL platforms found\n");
            return 1;
        }

        cl_platform_id *plats = calloc(np, sizeof(*plats));
        if (!plats)
            die("calloc platforms");

        err = clGetPlatformIDs(np, plats, NULL);
        cl_die(err, "clGetPlatformIDs");

        for (cl_uint i = 0; i < np; i++) {
            char name[256] = {0};

            clGetPlatformInfo(plats[i],
                              CL_PLATFORM_NAME,
                              sizeof(name),
                              name,
                              NULL);

            if (strcasestr(name, "rusticl")) {
                platform = plats[i];
                break;
            }
        }

        free(plats);
    }

    if (!platform) {
        fprintf(stderr,
                "Rusticl OpenCL platform not found.\n"
                "Run with RUSTICL_ENABLE=panfrost\n");
        return 1;
    }

    {
        char pname[256] = {0};

        clGetPlatformInfo(platform,
                          CL_PLATFORM_NAME,
                          sizeof(pname),
                          pname,
                          NULL);

        fprintf(stderr, "OpenCL platform: %s\n", pname);
    }

    err = clGetDeviceIDs(platform,
                         CL_DEVICE_TYPE_GPU,
                         1,
                         &device,
                         NULL);
    cl_die(err, "clGetDeviceIDs(GPU)");

    {
        char dname[256] = {0};

        clGetDeviceInfo(device,
                        CL_DEVICE_NAME,
                        sizeof(dname),
                        dname,
                        NULL);

        fprintf(stderr, "OpenCL device  : %s\n", dname);
    }

    ctx = clCreateContext(NULL,
                          1,
                          &device,
                          NULL,
                          NULL,
                          &err);
    cl_die(err, "clCreateContext");

    {
        const cl_queue_properties props[] = { 0 };

        queue = clCreateCommandQueueWithProperties(ctx,
                                                   device,
                                                   props,
                                                   &err);
        cl_die(err, "clCreateCommandQueueWithProperties");
    }

    cl_source = load_text(KERNEL_CL, &cl_source_len);

    {
        const char *src = cl_source;

        program = clCreateProgramWithSource(ctx,
                                            1,
                                            &src,
                                            &cl_source_len,
                                            &err);
        cl_die(err, "clCreateProgramWithSource");
    }

    err = clBuildProgram(program,
                         1,
                         &device,
                         NULL,
                         NULL,
                         NULL);

    if (err != CL_SUCCESS) {
        size_t log_len = 0;

        clGetProgramBuildInfo(program,
                              device,
                              CL_PROGRAM_BUILD_LOG,
                              0,
                              NULL,
                              &log_len);

        char *log = calloc(1, log_len + 1);

        if (log) {
            clGetProgramBuildInfo(program,
                                  device,
                                  CL_PROGRAM_BUILD_LOG,
                                  log_len,
                                  log,
                                  NULL);

            fprintf(stderr,
                    "OpenCL build log:\n%s\n",
                    log);

            free(log);
        }

        cl_die(err, "clBuildProgram");
    }

    kernel = clCreateKernel(program,
                            "debayer_yuv420",
                            &err);
    cl_die(err, "clCreateKernel(debayer_yuv420)");

    cl_in = clCreateBuffer(ctx,
                           CL_MEM_READ_ONLY,
                           IN_SIZE,
                           NULL,
                           &err);
    cl_die(err, "clCreateBuffer(input)");

    cl_out = clCreateBuffer(ctx,
                            CL_MEM_WRITE_ONLY,
                            OUT_SIZE,
                            NULL,
                            &err);
    cl_die(err, "clCreateBuffer(output)");

    {
        int iw = W;
        int ih = H;

        err  = clSetKernelArg(kernel,
                              0,
                              sizeof(cl_in),
                              &cl_in);

        err |= clSetKernelArg(kernel,
                              1,
                              sizeof(cl_out),
                              &cl_out);

        err |= clSetKernelArg(kernel,
                              2,
                              sizeof(iw),
                              &iw);

        err |= clSetKernelArg(kernel,
                              3,
                              sizeof(ih),
                              &ih);

        cl_die(err, "clSetKernelArg");
    }

    host_out = malloc(OUT_SIZE);

    if (!host_out)
        die("malloc host_out");

    /*
     * Open IMX219 capture.
     */
    cap_fd = open(INPUT_DEV,
                  O_RDWR | O_NONBLOCK);

    if (cap_fd < 0)
        die("open /dev/video0");

    {
        struct v4l2_format fmt;

        memset(&fmt, 0, sizeof(fmt));

        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        fmt.fmt.pix.width = W;
        fmt.fmt.pix.height = H;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SRGGB10;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;

        if (xioctl(cap_fd,
                   VIDIOC_S_FMT,
                   &fmt) < 0)
            die("VIDIOC_S_FMT video0");

        char fourcc[5];

        fourcc_string(fmt.fmt.pix.pixelformat,
                      fourcc);

        fprintf(stderr,
                "Capture format : %ux%u %s bpl=%u size=%u\n",
                fmt.fmt.pix.width,
                fmt.fmt.pix.height,
                fourcc,
                fmt.fmt.pix.bytesperline,
                fmt.fmt.pix.sizeimage);

        if (fmt.fmt.pix.width != W ||
            fmt.fmt.pix.height != H ||
            fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_SRGGB10) {
            fprintf(stderr,
                    "Unexpected capture format\n");
            return 1;
        }

        if (fmt.fmt.pix.sizeimage < IN_SIZE) {
            fprintf(stderr,
                    "Capture sizeimage too small: %u < %zu\n",
                    fmt.fmt.pix.sizeimage,
                    IN_SIZE);
            return 1;
        }
    }

    /*
     * Allocate MMAP capture buffers.
     */
    {
        struct v4l2_requestbuffers req;

        memset(&req, 0, sizeof(req));

        req.count = NBUF;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (xioctl(cap_fd,
                   VIDIOC_REQBUFS,
                   &req) < 0)
            die("VIDIOC_REQBUFS");

        if (req.count < 2) {
            fprintf(stderr,
                    "Not enough capture buffers: %u\n",
                    req.count);
            return 1;
        }

        nbufs = req.count;

        if (nbufs > NBUF)
            nbufs = NBUF;

        for (unsigned i = 0; i < nbufs; i++) {
            struct v4l2_buffer b;

            memset(&b, 0, sizeof(b));

            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            b.index = i;

            if (xioctl(cap_fd,
                       VIDIOC_QUERYBUF,
                       &b) < 0)
                die("VIDIOC_QUERYBUF");

            bufs[i].len = b.length;

            bufs[i].addr = mmap(NULL,
                                b.length,
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED,
                                cap_fd,
                                b.m.offset);

            if (bufs[i].addr == MAP_FAILED)
                die("mmap capture");

            if (xioctl(cap_fd,
                       VIDIOC_QBUF,
                       &b) < 0)
                die("VIDIOC_QBUF initial");
        }
    }

    /*
     * Open v4l2loopback output.
     */
    out_fd = open(OUTPUT_DEV,
                  O_RDWR);

    if (out_fd < 0)
        die("open /dev/video10");

    {
        struct v4l2_format fmt;

        memset(&fmt, 0, sizeof(fmt));

        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

        fmt.fmt.pix.width = W;
        fmt.fmt.pix.height = H;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        fmt.fmt.pix.bytesperline = W;
        fmt.fmt.pix.sizeimage = OUT_SIZE;
        fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
        fmt.fmt.pix.quantization = V4L2_QUANTIZATION_FULL_RANGE;

        if (xioctl(out_fd,
                   VIDIOC_S_FMT,
                   &fmt) < 0)
            die("VIDIOC_S_FMT video10");

        char fourcc[5];

        fourcc_string(fmt.fmt.pix.pixelformat,
                      fourcc);

        fprintf(stderr,
                "Output format  : %ux%u %s bpl=%u size=%u\n",
                fmt.fmt.pix.width,
                fmt.fmt.pix.height,
                fourcc,
                fmt.fmt.pix.bytesperline,
                fmt.fmt.pix.sizeimage);

        if (fmt.fmt.pix.width != W ||
            fmt.fmt.pix.height != H ||
            fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUV420) {
            fprintf(stderr,
                    "Unexpected video10 output format\n");
            return 1;
        }
    }

    /*
     * Advertise 10 fps on video10.
     */
    {
        struct v4l2_streamparm parm;

        memset(&parm, 0, sizeof(parm));

        parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        parm.parm.output.timeperframe.numerator = 1;
        parm.parm.output.timeperframe.denominator = 10;

        if (xioctl(out_fd,
                   VIDIOC_S_PARM,
                   &parm) < 0) {
            fprintf(stderr,
                    "VIDIOC_S_PARM video10: %s (continuing)\n",
                    strerror(errno));
        }
    }

    /*
     * Start camera.
     */
    {
        enum v4l2_buf_type type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (xioctl(cap_fd,
                   VIDIOC_STREAMON,
                   &type) < 0)
            die("VIDIOC_STREAMON");
    }

    fprintf(stderr,
            "\nStreaming started. Ctrl+C to stop.\n\n");

    double stat_start = now_s();
    double next_process = stat_start;

    unsigned long captured = 0;
    unsigned long processed = 0;
    unsigned long dropped = 0;

    double process_ms_sum = 0.0;

    while (!stop_flag) {
        struct pollfd pfd = {
            .fd = cap_fd,
            .events = POLLIN
        };

        int pr = poll(&pfd, 1, 1000);

        if (pr < 0) {
            if (errno == EINTR)
                continue;

            die("poll video0");
        }

        if (pr == 0)
            continue;

        for (;;) {
            struct v4l2_buffer b;

            memset(&b, 0, sizeof(b));

            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;

            if (xioctl(cap_fd,
                       VIDIOC_DQBUF,
                       &b) < 0) {
                if (errno == EAGAIN)
                    break;

                die("VIDIOC_DQBUF");
            }

            captured++;

            if (b.index >= nbufs) {
                fprintf(stderr,
                        "Invalid buffer index %u\n",
                        b.index);
                return 1;
            }

            double tnow = now_s();

            /*
             * Drain camera continuously but only send roughly
             * 10 frames/s through OpenCL.
             */
            if (tnow < next_process) {
                dropped++;

                if (xioctl(cap_fd,
                           VIDIOC_QBUF,
                           &b) < 0)
                    die("VIDIOC_QBUF drop");

                continue;
            }

            next_process += 0.100;
            if (next_process < tnow)
                next_process = tnow;

            if (b.bytesused < IN_SIZE) {
                fprintf(stderr,
                        "Short capture frame: %u bytes\n",
                        b.bytesused);

                if (xioctl(cap_fd,
                           VIDIOC_QBUF,
                           &b) < 0)
                    die("VIDIOC_QBUF short");

                continue;
            }

            double t0 = now_s();

            /*
             * CPU/MMAP -> Mali.
             *
             * Blocking upload is intentional: after it completes,
             * the V4L2 capture buffer can safely be returned to
             * the CSI driver immediately.
             */
            err = clEnqueueWriteBuffer(queue,
                                       cl_in,
                                       CL_TRUE,
                                       0,
                                       IN_SIZE,
                                       bufs[b.index].addr,
                                       0,
                                       NULL,
                                       NULL);
            cl_die(err, "clEnqueueWriteBuffer");

            if (xioctl(cap_fd,
                       VIDIOC_QBUF,
                       &b) < 0)
                die("VIDIOC_QBUF processed");

            {
                const size_t global[2] = {
                    W / 2,
                    H / 2
                };

                err = clEnqueueNDRangeKernel(queue,
                                             kernel,
                                             2,
                                             NULL,
                                             global,
                                             NULL,
                                             0,
                                             NULL,
                                             NULL);
                cl_die(err, "clEnqueueNDRangeKernel");
            }

            /*
             * Mali -> RAM, YU12:
             *
             * Y plane: W*H
             * U plane: W*H/4
             * V plane: W*H/4
             */
            err = clEnqueueReadBuffer(queue,
                                      cl_out,
                                      CL_TRUE,
                                      0,
                                      OUT_SIZE,
                                      host_out,
                                      0,
                                      NULL,
                                      NULL);
            cl_die(err, "clEnqueueReadBuffer");

            double t1 = now_s();

            write_full(out_fd,
                       host_out,
                       OUT_SIZE);

            processed++;
            process_ms_sum += (t1 - t0) * 1000.0;

            double ts = now_s();

            if (ts - stat_start >= 5.0) {
                double dt = ts - stat_start;

                fprintf(stderr,
                        "capture=%lu  processed=%lu  dropped=%lu  "
                        "out=%.2f fps  OpenCL+copies=%.2f ms/frame\n",
                        captured,
                        processed,
                        dropped,
                        processed / dt,
                        processed
                            ? process_ms_sum / processed
                            : 0.0);

                captured = 0;
                processed = 0;
                dropped = 0;
                process_ms_sum = 0.0;
                stat_start = ts;
            }
        }
    }

    fprintf(stderr, "\nStopping...\n");

    if (cap_fd >= 0) {
        enum v4l2_buf_type type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE;

        xioctl(cap_fd,
               VIDIOC_STREAMOFF,
               &type);
    }

    for (unsigned i = 0; i < nbufs; i++) {
        if (bufs[i].addr &&
            bufs[i].addr != MAP_FAILED) {
            munmap(bufs[i].addr,
                   bufs[i].len);
        }
    }

    if (cap_fd >= 0)
        close(cap_fd);

    if (out_fd >= 0)
        close(out_fd);

    if (cl_out)
        clReleaseMemObject(cl_out);

    if (cl_in)
        clReleaseMemObject(cl_in);

    if (kernel)
        clReleaseKernel(kernel);

    if (program)
        clReleaseProgram(program);

    if (queue)
        clReleaseCommandQueue(queue);

    if (ctx)
        clReleaseContext(ctx);

    free(cl_source);
    free(host_out);

    fprintf(stderr, "Stopped cleanly.\n");

    return 0;
}
