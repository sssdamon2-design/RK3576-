#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <linux/videodev2.h>
#include <linux/dma-heap.h>

#include <rga/im2d.h>
#include <rga/rga.h>


#define CAMERA_DEVICE   "/dev/video11"
#define OUTPUT_DIR      "calibration_capture"

#define CAM_W           3840
#define CAM_H           2160

#define RESIZE_W        640
#define RESIZE_H        360

#define MODEL_W         640
#define MODEL_H         640

#define PAD_TOP         140
#define PAD_VALUE       114

#define CAMERA_BUF_NUM  4

/*
 * 每次用户按 Enter 准备保存时，
 * 先丢掉若干帧，让 Camera 从“等待期间的旧帧”
 * 更新到当前场景。
 */
#define STARTUP_DISCARD_FRAMES  30
#define SAVE_INTERVAL_FRAMES     30
#define TARGET_IMAGE_COUNT       200


struct CameraBuffer
{
    int dma_fd;
};


/* ============================================================
 * DMA Heap:
 * 申请一个 DMA-BUF
 * ============================================================ */
static int alloc_dma_buffer(
    int heap_fd,
    size_t size)
{
    struct dma_heap_allocation_data data;

    memset(
        &data,
        0,
        sizeof(data)
    );

    data.len =
        size;

    data.fd_flags =
        O_RDWR |
        O_CLOEXEC;


    if (ioctl(
            heap_fd,
            DMA_HEAP_IOCTL_ALLOC,
            &data) < 0)
    {
        perror(
            "DMA_HEAP_IOCTL_ALLOC"
        );

        return -1;
    }


    return data.fd;
}


/* ============================================================
 * POSIX write:
 * 确保把所有数据完整写入文件
 * ============================================================ */
static int write_all(
    int fd,
    const void *buf,
    size_t size)
{
    const unsigned char *p =
        (const unsigned char *)buf;

    size_t done =
        0;


    while (done < size)
    {
        ssize_t n =
            write(
                fd,
                p + done,
                size - done
            );


        if (n < 0)
        {
            if (errno ==
                EINTR)
            {
                continue;
            }

            perror("write");

            return -1;
        }


        if (n == 0)
        {
            printf(
                "write returned 0\n"
            );

            return -1;
        }


        done +=
            (size_t)n;
    }


    return 0;
}


/* ============================================================
 * 保存 PPM(P6) RGB 图像
 *
 * 为什么先用 PPM：
 *
 * 1. 不需要 OpenCV/libjpeg/libpng
 * 2. 直接保存 RGB888
 * 3. 不发生 JPEG 有损压缩
 * 4. 后面可以在 WSL/PC 统一转成 PNG
 *
 * PPM 文件：
 *
 * header:
 *   P6
 *   640 640
 *   255
 *
 * data:
 *   RGB RGB RGB ...
 * ============================================================ */
static int save_ppm(
    const unsigned char *rgb,
    int width,
    int height,
    char *saved_path,
    size_t saved_path_size)
{
    /*
     * 使用 O_EXCL：
     * 如果文件已经存在，就不会覆盖。
     */
    for (int index = 1;
         index <= 9999;
         index++)
    {
        char path[256];

        snprintf(
            path,
            sizeof(path),
            "%s/calib_%04d.ppm",
            OUTPUT_DIR,
            index
        );


        int fd =
            open(
                path,
                O_WRONLY |
                O_CREAT |
                O_EXCL,
                0644
            );


        if (fd < 0)
        {
            if (errno ==
                EEXIST)
            {
                continue;
            }

            perror(
                "open output ppm"
            );

            return -1;
        }


        char header[64];

        int header_len =
            snprintf(
                header,
                sizeof(header),
                "P6\n%d %d\n255\n",
                width,
                height
            );


        if (write_all(
                fd,
                header,
                (size_t)header_len) < 0)
        {
            close(fd);

            return -1;
        }


        size_t image_size =
            (size_t)width *
            (size_t)height *
            3U;


        if (write_all(
                fd,
                rgb,
                image_size) < 0)
        {
            close(fd);

            return -1;
        }


        close(fd);


        snprintf(
            saved_path,
            saved_path_size,
            "%s",
            path
        );


        return 0;
    }


    printf(
        "too many calibration files\n"
    );

    return -1;
}


/* ============================================================
 * DQBUF:
 * 从 V4L2 取出一帧
 * ============================================================ */
static int dequeue_camera_buffer(
    int camera_fd,
    struct v4l2_buffer *buf,
    struct v4l2_plane planes[1])
{
    memset(
        buf,
        0,
        sizeof(*buf)
    );

    memset(
        planes,
        0,
        sizeof(struct v4l2_plane)
    );


    buf->type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    buf->memory =
        V4L2_MEMORY_MMAP;

    buf->m.planes =
        planes;

    buf->length =
        1;


    if (ioctl(
            camera_fd,
            VIDIOC_DQBUF,
            buf) < 0)
    {
        perror(
            "VIDIOC_DQBUF"
        );

        return -1;
    }


    return 0;
}


/* ============================================================
 * QBUF:
 * 把刚才用完的 Camera Buffer 还给驱动
 * ============================================================ */
static int queue_camera_buffer(
    int camera_fd,
    struct v4l2_buffer *buf)
{
    if (ioctl(
            camera_fd,
            VIDIOC_QBUF,
            buf) < 0)
    {
        perror(
            "VIDIOC_QBUF"
        );

        return -1;
    }


    return 0;
}


int main(void)
{
    int ret_code =
        -1;

    int camera_fd =
        -1;

    int heap_fd =
        -1;

    int nv12_fd =
        -1;

    int rgb_fd =
        -1;

    unsigned char *rgb_data =
        NULL;

    unsigned char *model_input =
        NULL;

    CameraBuffer *buffers =
        NULL;

    unsigned int buffer_count =
        0;

    unsigned int frame_counter =
        0;

    unsigned int saved_count =
        0;

    int stream_started =
        0;


    /*
     * C++ 中 goto 不能跳过带初始化的局部变量。
     *
     * 因此凡是 cleanup 之前任何路径都可能跨越的变量，
     * 必须在 main() 最前面统一声明/初始化，
     * 后面只做赋值，不再重新定义。
     */
    size_t nv12_size =
        0;

    size_t rgb_size =
        0;

    size_t model_size =
        0;

    rga_buffer_t dst_nv12 =
        {};

    rga_buffer_t dst_rgb =
        {};

    enum v4l2_buf_type stream_type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;


    /*
     * =====================================================
     * 第 1 步：创建 Calibration 输出目录
     * =====================================================
     */

    if (mkdir(
            OUTPUT_DIR,
            0755) < 0)
    {
        if (errno !=
            EEXIST)
        {
            perror(
                "mkdir calibration_capture"
            );

            goto cleanup;
        }
    }


    /*
     * =====================================================
     * 第 2 步：打开 Camera
     *
     * /dev/video11:
     * 当前已经验证是 rkisp_mainpath
     * =====================================================
     */

    camera_fd =
        open(
            CAMERA_DEVICE,
            O_RDWR
        );


    if (camera_fd < 0)
    {
        perror(
            "open camera"
        );

        goto cleanup;
    }


    /*
     * =====================================================
     * 第 3 步：设置 Camera 输出
     *
     * 3840x2160 NV12
     * =====================================================
     */

    struct v4l2_format fmt;

    memset(
        &fmt,
        0,
        sizeof(fmt)
    );


    fmt.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    fmt.fmt.pix_mp.width =
        CAM_W;

    fmt.fmt.pix_mp.height =
        CAM_H;

    fmt.fmt.pix_mp.pixelformat =
        V4L2_PIX_FMT_NV12;

    fmt.fmt.pix_mp.field =
        V4L2_FIELD_NONE;


    if (ioctl(
            camera_fd,
            VIDIOC_S_FMT,
            &fmt) < 0)
    {
        perror(
            "VIDIOC_S_FMT"
        );

        goto cleanup;
    }


    printf(
        "Camera actual: %u x %u, "
        "stride=%u, num_planes=%u\n",
        fmt.fmt.pix_mp.width,
        fmt.fmt.pix_mp.height,
        fmt.fmt.pix_mp.plane_fmt[0].bytesperline,
        fmt.fmt.pix_mp.num_planes
    );


    /*
     * =====================================================
     * 第 4 步：申请 V4L2 Buffer
     *
     * V4L2_MEMORY_MMAP:
     * Camera 驱动管理这些 Buffer
     *
     * VIDIOC_EXPBUF:
     * 再把同一批 Buffer 导出成 DMA-BUF fd
     * 给 RGA 使用
     * =====================================================
     */

    struct v4l2_requestbuffers req;

    memset(
        &req,
        0,
        sizeof(req)
    );


    req.count =
        CAMERA_BUF_NUM;

    req.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    req.memory =
        V4L2_MEMORY_MMAP;


    if (ioctl(
            camera_fd,
            VIDIOC_REQBUFS,
            &req) < 0)
    {
        perror(
            "VIDIOC_REQBUFS"
        );

        goto cleanup;
    }


    if (req.count == 0)
    {
        printf(
            "camera returned 0 buffers\n"
        );

        goto cleanup;
    }


    buffer_count =
        req.count;


    buffers =
        (CameraBuffer *)calloc(
            buffer_count,
            sizeof(CameraBuffer)
        );


    if (buffers == NULL)
    {
        perror(
            "calloc camera buffers"
        );

        goto cleanup;
    }


    for (unsigned int i = 0;
         i < buffer_count;
         i++)
    {
        buffers[i].dma_fd =
            -1;


        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];

        memset(
            &buf,
            0,
            sizeof(buf)
        );

        memset(
            planes,
            0,
            sizeof(planes)
        );


        buf.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        buf.memory =
            V4L2_MEMORY_MMAP;

        buf.index =
            i;

        buf.m.planes =
            planes;

        buf.length =
            1;


        if (ioctl(
                camera_fd,
                VIDIOC_QUERYBUF,
                &buf) < 0)
        {
            perror(
                "VIDIOC_QUERYBUF"
            );

            goto cleanup;
        }


        struct v4l2_exportbuffer expbuf;

        memset(
            &expbuf,
            0,
            sizeof(expbuf)
        );


        expbuf.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        expbuf.index =
            i;

        expbuf.plane =
            0;

        expbuf.flags =
            O_CLOEXEC;


        if (ioctl(
                camera_fd,
                VIDIOC_EXPBUF,
                &expbuf) < 0)
        {
            perror(
                "VIDIOC_EXPBUF"
            );

            goto cleanup;
        }


        buffers[i].dma_fd =
            expbuf.fd;


        printf(
            "camera buffer[%u] dma_fd=%d\n",
            i,
            buffers[i].dma_fd
        );
    }


    /*
     * =====================================================
     * 第 5 步：给 RGA 准备中间 Buffer
     *
     * Camera:
     * 3840x2160 NV12
     *
     *       ↓ imresize
     *
     * 640x360 NV12
     *
     *       ↓ imcvtcolor
     *
     * 640x360 RGB888
     * =====================================================
     */

    heap_fd =
        open(
            "/dev/dma_heap/system-uncached",
            O_RDWR
        );


    if (heap_fd < 0)
    {
        perror(
            "open /dev/dma_heap/system-uncached"
        );

        goto cleanup;
    }


    nv12_size =
        (size_t)RESIZE_W *
        (size_t)RESIZE_H *
        3U / 2U;


    rgb_size =
        (size_t)RESIZE_W *
        (size_t)RESIZE_H *
        3U;


    nv12_fd =
        alloc_dma_buffer(
            heap_fd,
            nv12_size
        );


    rgb_fd =
        alloc_dma_buffer(
            heap_fd,
            rgb_size
        );


    if (nv12_fd < 0 ||
        rgb_fd < 0)
    {
        goto cleanup;
    }


    /*
     * CPU 只需要映射 RGB Buffer，
     * 因为后面 CPU 要做 Letterbox。
     */
    rgb_data =
        (unsigned char *)mmap(
            NULL,
            rgb_size,
            PROT_READ |
            PROT_WRITE,
            MAP_SHARED,
            rgb_fd,
            0
        );


    if (rgb_data ==
        MAP_FAILED)
    {
        perror(
            "mmap rgb"
        );

        rgb_data =
            NULL;

        goto cleanup;
    }


    dst_nv12 =
        wrapbuffer_fd(
            nv12_fd,
            RESIZE_W,
            RESIZE_H,
            RK_FORMAT_YCbCr_420_SP
        );


    dst_rgb =
        wrapbuffer_fd(
            rgb_fd,
            RESIZE_W,
            RESIZE_H,
            RK_FORMAT_RGB_888
        );


    /*
     * =====================================================
     * 第 6 步：准备最终 640x640 RGB Buffer
     *
     * Calibration 图片保存的是：
     *
     * 640 x 640
     * RGB UINT8
     *
     * 注意：
     * 这里不做 /255。
     *
     * /255 仍然交给后面的 RKNN Toolkit config:
     *
     * mean = [0,0,0]
     * std  = [255,255,255]
     * =====================================================
     */

    model_size =
        (size_t)MODEL_W *
        (size_t)MODEL_H *
        3U;


    model_input =
        (unsigned char *)malloc(
            model_size
        );


    if (model_input == NULL)
    {
        perror(
            "malloc model_input"
        );

        goto cleanup;
    }


    /*
     * =====================================================
     * 第 7 步：所有 Camera Buffer 入队
     * =====================================================
     */

    for (unsigned int i = 0;
         i < buffer_count;
         i++)
    {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];

        memset(
            &buf,
            0,
            sizeof(buf)
        );

        memset(
            planes,
            0,
            sizeof(planes)
        );


        buf.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        buf.memory =
            V4L2_MEMORY_MMAP;

        buf.index =
            i;

        buf.m.planes =
            planes;

        buf.length =
            1;


        if (ioctl(
                camera_fd,
                VIDIOC_QBUF,
                &buf) < 0)
        {
            perror(
                "VIDIOC_QBUF initial"
            );

            goto cleanup;
        }
    }


    if (ioctl(
            camera_fd,
            VIDIOC_STREAMON,
            &stream_type) < 0)
    {
        perror(
            "VIDIOC_STREAMON"
        );

        goto cleanup;
    }


    stream_started =
        1;


    printf(
        "\n"
        "========================================\n"
        "Automatic calibration capture started\n"
        "========================================\n"
        "startup discard frames = %d\n"
        "save interval frames   = %d\n"
        "target image count     = %d\n"
        "\n"
        "Move the camera / change distance / "
        "change lighting while the program runs.\n"
        "The program will save images automatically.\n"
        "\n",
        STARTUP_DISCARD_FRAMES,
        SAVE_INTERVAL_FRAMES,
        TARGET_IMAGE_COUNT
    );


    /*
     * =====================================================
     * 第 8 步：启动后自动采集
     *
     * 设计：
     *
     * 1. Camera 始终 DQBUF -> QBUF
     *    不再停下来等待键盘输入。
     *
     * 2. 启动后先丢掉若干帧，
     *    给自动曝光 / 自动白平衡一些稳定时间。
     *
     * 3. 之后每隔 SAVE_INTERVAL_FRAMES 帧保存一张。
     *
     * 以当前 4K@30fps 为例：
     * SAVE_INTERVAL_FRAMES = 30
     * 大约每 1 秒保存一张。
     *
     * 4. 保存 TARGET_IMAGE_COUNT 张后自动退出。
     * =====================================================
     */

    while (saved_count <
           TARGET_IMAGE_COUNT)
    {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];


        if (dequeue_camera_buffer(
                camera_fd,
                &buf,
                planes) < 0)
        {
            goto cleanup;
        }


        frame_counter++;


        /*
         * 启动阶段先让 Camera 连续跑起来，
         * 不保存这些帧。
         */
        if (frame_counter <=
            STARTUP_DISCARD_FRAMES)
        {
            if (queue_camera_buffer(
                    camera_fd,
                    &buf) < 0)
            {
                goto cleanup;
            }

            continue;
        }


        /*
         * 不是需要保存的帧：
         * 直接还给 Camera。
         */
        unsigned int active_frame =
            frame_counter -
            STARTUP_DISCARD_FRAMES;


        if ((active_frame - 1U) %
            SAVE_INTERVAL_FRAMES != 0U)
        {
            if (queue_camera_buffer(
                    camera_fd,
                    &buf) < 0)
            {
                goto cleanup;
            }

            continue;
        }


        /*
         * Camera DMA-BUF
         *      ↓
         * RGA resize
         *      ↓
         * RGA color convert
         */
        rga_buffer_t src =
            wrapbuffer_fd(
                buffers[buf.index].dma_fd,
                CAM_W,
                CAM_H,
                RK_FORMAT_YCbCr_420_SP
            );


        IM_STATUS rga_ret =
            imresize(
                src,
                dst_nv12
            );


        if (rga_ret !=
            IM_STATUS_SUCCESS)
        {
            printf(
                "imresize failed: %s\n",
                imStrError(rga_ret)
            );

            queue_camera_buffer(
                camera_fd,
                &buf
            );

            goto cleanup;
        }


        rga_ret =
            imcvtcolor(
                dst_nv12,
                dst_rgb,
                RK_FORMAT_YCbCr_420_SP,
                RK_FORMAT_RGB_888
            );


        if (rga_ret !=
            IM_STATUS_SUCCESS)
        {
            printf(
                "imcvtcolor failed: %s\n",
                imStrError(rga_ret)
            );

            queue_camera_buffer(
                camera_fd,
                &buf
            );

            goto cleanup;
        }


        /*
         * RGA 已经读取完 Camera Buffer，
         * 现在就把 Buffer 还给 Camera，
         * 保证采集链路持续运行。
         */
        if (queue_camera_buffer(
                camera_fd,
                &buf) < 0)
        {
            goto cleanup;
        }


        /*
         * =================================================
         * Letterbox:
         *
         * 640x360 RGB
         *      ↓
         * 上下各 pad 140
         *      ↓
         * 640x640 RGB UINT8
         * =================================================
         */

        memset(
            model_input,
            PAD_VALUE,
            model_size
        );


        size_t row_bytes =
            (size_t)RESIZE_W *
            3U;


        for (int y = 0;
             y < RESIZE_H;
             y++)
        {
            memcpy(
                model_input
                +
                (size_t)(y + PAD_TOP) *
                MODEL_W *
                3U,

                rgb_data
                +
                (size_t)y *
                RESIZE_W *
                3U,

                row_bytes
            );
        }


        char saved_path[256];


        if (save_ppm(
                model_input,
                MODEL_W,
                MODEL_H,
                saved_path,
                sizeof(saved_path)) < 0)
        {
            goto cleanup;
        }


        saved_count++;


        printf(
            "[%u/%u] Saved: %s\n",
            saved_count,
            TARGET_IMAGE_COUNT,
            saved_path
        );
    }


    printf(
        "\n"
        "Calibration capture finished.\n"
        "Saved %u images in ./%s/\n",
        saved_count,
        OUTPUT_DIR
    );


    ret_code =
        0;


cleanup:

    /*
     * =====================================================
     * 第 9 步：资源释放
     * =====================================================
     */

    if (stream_started &&
        camera_fd >= 0)
    {
        ioctl(
            camera_fd,
            VIDIOC_STREAMOFF,
            &stream_type
        );
    }


    if (model_input !=
        NULL)
    {
        free(
            model_input
        );
    }


    if (rgb_data !=
        NULL)
    {
        munmap(
            rgb_data,
            rgb_size
        );
    }


    if (nv12_fd >= 0)
    {
        close(
            nv12_fd
        );
    }


    if (rgb_fd >= 0)
    {
        close(
            rgb_fd
        );
    }


    if (heap_fd >= 0)
    {
        close(
            heap_fd
        );
    }


    if (buffers !=
        NULL)
    {
        for (unsigned int i = 0;
             i < buffer_count;
             i++)
        {
            if (buffers[i].dma_fd >=
                0)
            {
                close(
                    buffers[i].dma_fd
                );
            }
        }

        free(
            buffers
        );
    }


    if (camera_fd >= 0)
    {
        close(
            camera_fd
        );
    }


    return ret_code;
}
