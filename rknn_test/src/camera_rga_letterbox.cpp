#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/videodev2.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>

#include <rga/im2d.h>
#include <rga/rga.h>


/*
 * ============================================================
 * 当前阶段目标
 * ============================================================
 *
 * IMX415 /dev/video11
 *      ↓
 * V4L2 3840x2160 NV12
 *      ↓
 * VIDIOC_EXPBUF 导出 Camera DMA-BUF fd
 *      ↓
 * RGA resize
 *      ↓
 * 640x360 NV12
 *      ↓
 * RGA CSC
 *      ↓
 * 640x360 RGB888
 *      ↓
 * CPU 构造 640x640 Letterbox
 * 上下 padding = 114
 *      ↓
 * camera_640_rgb.raw
 * camera_640_preview.ppm
 *
 * 注意：
 * 这一版先验证 Camera → RGA → AI 输入是否完全正确。
 * Letterbox 的最后一次小图 copy 暂时由 CPU 完成。
 * 下一阶段再把它继续优化成 DMA-BUF / RKNN Memory 低拷贝链路。
 */


#define MODEL_WIDTH   640U
#define MODEL_HEIGHT  640U

#define CAMERA_REQ_WIDTH   3840U
#define CAMERA_REQ_HEIGHT  2160U

#define WARMUP_FRAMES 10U

#define LETTERBOX_VALUE 114


typedef struct
{
    void *start;
    size_t length;
    int dma_fd;

} CameraBuffer;


/*
 * ============================================================
 * 从 dma-heap 申请一个 DMA-BUF
 *
 * heap_fd：
 *   /dev/dma_heap/system 的 fd
 *
 * size：
 *   需要的字节数
 *
 * 返回：
 *   成功 -> DMA-BUF fd
 *   失败 -> -1
 * ============================================================
 */
static int alloc_dma_buffer(
    int heap_fd,
    size_t size)
{
    struct dma_heap_allocation_data alloc_data;

    memset(
        &alloc_data,
        0,
        sizeof(alloc_data)
    );

    alloc_data.len =
        size;

    alloc_data.fd_flags =
        O_RDWR |
        O_CLOEXEC;

    alloc_data.heap_flags =
        0;


    if (ioctl(
            heap_fd,
            DMA_HEAP_IOCTL_ALLOC,
            &alloc_data) < 0)
    {
        perror("DMA_HEAP_IOCTL_ALLOC");
        return -1;
    }


    return
        alloc_data.fd;
}


/*
 * ============================================================
 * DMA-BUF CPU 同步
 *
 * RGA 刚写完 RGB DMA-BUF，
 * 接下来 CPU 要 mmap 后读取，
 * 对 cached system heap 做 READ sync。
 * ============================================================
 */
static int dma_buf_sync_read_start(
    int dma_fd)
{
    struct dma_buf_sync sync;

    memset(
        &sync,
        0,
        sizeof(sync)
    );

    sync.flags =
        DMA_BUF_SYNC_START |
        DMA_BUF_SYNC_READ;


    if (ioctl(
            dma_fd,
            DMA_BUF_IOCTL_SYNC,
            &sync) < 0)
    {
        perror(
            "DMA_BUF_IOCTL_SYNC START READ"
        );

        return -1;
    }


    return 0;
}


static int dma_buf_sync_read_end(
    int dma_fd)
{
    struct dma_buf_sync sync;

    memset(
        &sync,
        0,
        sizeof(sync)
    );

    sync.flags =
        DMA_BUF_SYNC_END |
        DMA_BUF_SYNC_READ;


    if (ioctl(
            dma_fd,
            DMA_BUF_IOCTL_SYNC,
            &sync) < 0)
    {
        perror(
            "DMA_BUF_IOCTL_SYNC END READ"
        );

        return -1;
    }


    return 0;
}


/*
 * ============================================================
 * 保存纯 RGB RAW
 *
 * 这个文件可以直接作为我们现在 rknn_run 的输入：
 *
 * 640 x 640 x 3
 * RGB
 * UINT8
 *
 * 大小必须是：
 * 1228800 Byte
 * ============================================================
 */
static int save_rgb_raw(
    const char *path,
    const unsigned char *data,
    size_t size)
{
    FILE *fp =
        fopen(
            path,
            "wb"
        );


    if (fp == NULL)
    {
        perror("fopen raw");
        return -1;
    }


    size_t written =
        fwrite(
            data,
            1,
            size,
            fp
        );


    if (written != size)
    {
        printf(
            "fwrite raw failed: "
            "%zu / %zu\n",
            written,
            size
        );

        fclose(fp);

        return -1;
    }


    fclose(fp);

    return 0;
}


/*
 * ============================================================
 * 保存 PPM 预览图
 *
 * PPM(P6) 本身直接存 RGB888，
 * 不需要 OpenCV。
 *
 * VSCode 或大多数图像工具可以直接查看。
 * ============================================================
 */
static int save_rgb_ppm(
    const char *path,
    const unsigned char *rgb,
    uint32_t width,
    uint32_t height)
{
    FILE *fp =
        fopen(
            path,
            "wb"
        );


    if (fp == NULL)
    {
        perror("fopen ppm");
        return -1;
    }


    fprintf(
        fp,
        "P6\n%u %u\n255\n",
        width,
        height
    );


    size_t image_size =
        (size_t)width *
        (size_t)height *
        3U;


    size_t written =
        fwrite(
            rgb,
            1,
            image_size,
            fp
        );


    if (written != image_size)
    {
        printf(
            "fwrite ppm failed: "
            "%zu / %zu\n",
            written,
            image_size
        );

        fclose(fp);

        return -1;
    }


    fclose(fp);

    return 0;
}


/*
 * ============================================================
 * 根据 Camera 尺寸计算 640x640 Letterbox 参数
 *
 * 对当前 3840x2160：
 *
 * scale = 640 / 3840 = 1/6
 *
 * resize:
 * 3840x2160
 *      ↓
 * 640x360
 *
 * padding:
 * left   = 0
 * right  = 0
 * top    = 140
 * bottom = 140
 *
 * NV12 的宽高需要偶数，
 * 因此 resize 后做偶数对齐。
 * ============================================================
 */
static void calculate_letterbox(
    uint32_t src_width,
    uint32_t src_height,
    uint32_t *resized_width,
    uint32_t *resized_height,
    uint32_t *pad_left,
    uint32_t *pad_top,
    uint32_t *pad_right,
    uint32_t *pad_bottom)
{
    float scale_x =
        (float)MODEL_WIDTH /
        (float)src_width;

    float scale_y =
        (float)MODEL_HEIGHT /
        (float)src_height;


    float scale =
        scale_x < scale_y ?
        scale_x :
        scale_y;


    uint32_t rw =
        (uint32_t)(
            (float)src_width *
            scale
        );

    uint32_t rh =
        (uint32_t)(
            (float)src_height *
            scale
        );


    /*
     * NV12 要求偶数尺寸。
     */
    rw &= ~1U;
    rh &= ~1U;


    if (rw == 0)
        rw = 2;

    if (rh == 0)
        rh = 2;

    if (rw > MODEL_WIDTH)
        rw = MODEL_WIDTH;

    if (rh > MODEL_HEIGHT)
        rh = MODEL_HEIGHT;


    uint32_t left =
        (MODEL_WIDTH - rw) / 2U;

    uint32_t top =
        (MODEL_HEIGHT - rh) / 2U;

    uint32_t right =
        MODEL_WIDTH -
        rw -
        left;

    uint32_t bottom =
        MODEL_HEIGHT -
        rh -
        top;


    *resized_width =
        rw;

    *resized_height =
        rh;

    *pad_left =
        left;

    *pad_top =
        top;

    *pad_right =
        right;

    *pad_bottom =
        bottom;
}


/*
 * ============================================================
 * 把 resize 后的 RGB 图放入 640x640 Canvas
 *
 * Canvas：
 * 先全部填 114
 *
 * 然后把 640x360 RGB
 * 拷贝到 y=140 开始的位置。
 *
 * 当前这是 CPU copy。
 * 它只处理 640x360 小图，
 * 不会把 4K Camera 帧 CPU copy 一遍。
 * ============================================================
 */
static void make_letterbox_rgb(
    unsigned char *canvas,
    const unsigned char *resized_rgb,

    uint32_t resized_width,
    uint32_t resized_height,

    uint32_t pad_left,
    uint32_t pad_top)
{
    size_t canvas_size =
        (size_t)MODEL_WIDTH *
        (size_t)MODEL_HEIGHT *
        3U;


    /*
     * RGB = (114,114,114)
     *
     * 因为三个通道数值一样，
     * 直接 memset 即可。
     */
    memset(
        canvas,
        LETTERBOX_VALUE,
        canvas_size
    );


    size_t src_row_bytes =
        (size_t)resized_width *
        3U;


    for (uint32_t y = 0;
         y < resized_height;
         y++)
    {
        unsigned char *dst_row =
            canvas
            +
            (
                (size_t)(pad_top + y) *
                MODEL_WIDTH
                +
                pad_left
            )
            * 3U;


        const unsigned char *src_row =
            resized_rgb
            +
            (size_t)y *
            src_row_bytes;


        memcpy(
            dst_row,
            src_row,
            src_row_bytes
        );
    }
}


int main()
{
    const char *device =
        "/dev/video11";


    int camera_fd =
        -1;

    int heap_fd =
        -1;

    int resize_nv12_fd =
        -1;

    int resize_rgb_fd =
        -1;


    CameraBuffer *buffers =
        NULL;

    uint32_t buffer_count =
        0;


    unsigned char *resize_rgb_map =
        NULL;

    unsigned char *letterbox_rgb =
        NULL;


    size_t resize_rgb_size =
        0;

    size_t letterbox_size =
        (size_t)MODEL_WIDTH *
        (size_t)MODEL_HEIGHT *
        3U;


    int streaming =
        0;

    int result =
        1;


    /*
     * ========================================================
     * C++ + goto 注意事项
     * ========================================================
     *
     * cleanup 标签位于 main() 最后。
     *
     * C++ 不允许 goto 向前跳过同一作用域内变量的初始化，
     * 因此所有可能被 goto cleanup 跨过的 main() 局部变量，
     * 都统一提前在这里声明/初始化。
     *
     * 后面只做赋值，不再重新声明这些变量。
     */

    struct v4l2_capability cap;

    struct v4l2_format fmt;

    struct v4l2_requestbuffers req;


    uint32_t camera_width =
        0;

    uint32_t camera_height =
        0;

    uint32_t camera_stride =
        0;


    uint32_t resized_width =
        0;

    uint32_t resized_height =
        0;

    uint32_t pad_left =
        0;

    uint32_t pad_top =
        0;

    uint32_t pad_right =
        0;

    uint32_t pad_bottom =
        0;


    size_t resize_nv12_size =
        0;


    rga_buffer_t resize_nv12;

    rga_buffer_t resize_rgb;


    memset(
        &resize_nv12,
        0,
        sizeof(resize_nv12)
    );

    memset(
        &resize_rgb,
        0,
        sizeof(resize_rgb)
    );


    enum v4l2_buf_type stream_type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;


    /*
     * ========================================================
     * 1. 打开 Camera
     * ========================================================
     */

    camera_fd =
        open(
            device,
            O_RDWR
        );


    if (camera_fd < 0)
    {
        perror("open camera");
        goto cleanup;
    }


    printf(
        "open %s successfully\n",
        device
    );


    /*
     * ========================================================
     * 2. 查询 Camera 能力
     * ========================================================
     */

    memset(
        &cap,
        0,
        sizeof(cap)
    );


    if (ioctl(
            camera_fd,
            VIDIOC_QUERYCAP,
            &cap) < 0)
    {
        perror("VIDIOC_QUERYCAP");
        goto cleanup;
    }


    printf(
        "driver = %s\n",
        cap.driver
    );

    printf(
        "card   = %s\n",
        cap.card
    );


    if (!(cap.device_caps &
          V4L2_CAP_VIDEO_CAPTURE_MPLANE))
    {
        printf(
            "Camera does not support "
            "VIDEO_CAPTURE_MPLANE\n"
        );

        goto cleanup;
    }


    if (!(cap.device_caps &
          V4L2_CAP_STREAMING))
    {
        printf(
            "Camera does not support "
            "STREAMING\n"
        );

        goto cleanup;
    }


    /*
     * ========================================================
     * 3. 设置 Camera = 3840x2160 NV12
     * ========================================================
     */

    memset(
        &fmt,
        0,
        sizeof(fmt)
    );


    fmt.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    fmt.fmt.pix_mp.width =
        CAMERA_REQ_WIDTH;

    fmt.fmt.pix_mp.height =
        CAMERA_REQ_HEIGHT;

    fmt.fmt.pix_mp.pixelformat =
        V4L2_PIX_FMT_NV12;

    fmt.fmt.pix_mp.field =
        V4L2_FIELD_NONE;


    if (ioctl(
            camera_fd,
            VIDIOC_S_FMT,
            &fmt) < 0)
    {
        perror("VIDIOC_S_FMT");
        goto cleanup;
    }


    camera_width =
        fmt.fmt.pix_mp.width;

    camera_height =
        fmt.fmt.pix_mp.height;

    camera_stride =
        fmt.fmt.pix_mp
        .plane_fmt[0]
        .bytesperline;


    printf(
        "\n========== CAMERA ==========\n"
    );

    printf(
        "size       = %u x %u\n",
        camera_width,
        camera_height
    );

    printf(
        "stride     = %u\n",
        camera_stride
    );

    printf(
        "num_planes = %u\n",
        fmt.fmt.pix_mp.num_planes
    );


    if (fmt.fmt.pix_mp.pixelformat !=
        V4L2_PIX_FMT_NV12)
    {
        printf(
            "Camera did not accept NV12\n"
        );

        goto cleanup;
    }


    /*
     * 当前已知 /dev/video11：
     * width=3840, stride=3840。
     *
     * wrapbuffer_fd() 这里没有单独传 stride，
     * 所以本阶段要求 stride == width。
     */
    if (camera_stride !=
        camera_width)
    {
        printf(
            "Unsupported camera stride: "
            "width=%u stride=%u\n",
            camera_width,
            camera_stride
        );

        printf(
            "Current code assumes "
            "stride == width.\n"
        );

        goto cleanup;
    }


    /*
     * ========================================================
     * 4. 计算 Letterbox
     * ========================================================
     */

    calculate_letterbox(
        camera_width,
        camera_height,

        &resized_width,
        &resized_height,

        &pad_left,
        &pad_top,
        &pad_right,
        &pad_bottom
    );


    printf(
        "\n========== LETTERBOX ==========\n"
    );

    printf(
        "camera      = %u x %u\n",
        camera_width,
        camera_height
    );

    printf(
        "RGA resize  = %u x %u\n",
        resized_width,
        resized_height
    );

    printf(
        "model input = %u x %u\n",
        MODEL_WIDTH,
        MODEL_HEIGHT
    );

    printf(
        "padding     = "
        "left:%u top:%u right:%u bottom:%u\n",
        pad_left,
        pad_top,
        pad_right,
        pad_bottom
    );


    /*
     * ========================================================
     * 5. 申请 V4L2 MMAP Buffer
     * ========================================================
     */

    memset(
        &req,
        0,
        sizeof(req)
    );


    req.count =
        4;

    req.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    req.memory =
        V4L2_MEMORY_MMAP;


    if (ioctl(
            camera_fd,
            VIDIOC_REQBUFS,
            &req) < 0)
    {
        perror("VIDIOC_REQBUFS");
        goto cleanup;
    }


    if (req.count == 0)
    {
        printf(
            "No V4L2 buffers allocated\n"
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
        perror("calloc CameraBuffer");
        goto cleanup;
    }


    /*
     * fd 默认设为 -1，
     * 方便 cleanup 判断。
     */
    for (uint32_t i = 0;
         i < buffer_count;
         i++)
    {
        buffers[i].dma_fd =
            -1;
    }


    /*
     * ========================================================
     * 6. QUERYBUF + mmap + EXPBUF
     * ========================================================
     */

    for (uint32_t i = 0;
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
                VIDIOC_QUERYBUF,
                &buf) < 0)
        {
            perror("VIDIOC_QUERYBUF");
            goto cleanup;
        }


        buffers[i].length =
            planes[0].length;


        buffers[i].start =
            mmap(
                NULL,
                planes[0].length,
                PROT_READ |
                PROT_WRITE,
                MAP_SHARED,
                camera_fd,
                planes[0].m.mem_offset
            );


        if (buffers[i].start ==
            MAP_FAILED)
        {
            buffers[i].start =
                NULL;

            perror("mmap camera buffer");

            goto cleanup;
        }


        /*
         * 把同一个 V4L2 buffer
         * 导出成 DMA-BUF fd。
         *
         * 后面 RGA 直接用这个 fd，
         * 不复制 4K 帧。
         */
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
            perror("VIDIOC_EXPBUF");
            goto cleanup;
        }


        buffers[i].dma_fd =
            expbuf.fd;


        printf(
            "camera buffer[%u]: "
            "length=%zu dma_fd=%d\n",
            i,
            buffers[i].length,
            buffers[i].dma_fd
        );
    }


    /*
     * ========================================================
     * 7. 打开 DMA Heap
     *
     * 使用 system：
     * RGB 输出后 CPU 要读取，
     * 因此前后使用 DMA_BUF_IOCTL_SYNC。
     * ========================================================
     */

    heap_fd =
        open(
            "/dev/dma_heap/system",
            O_RDWR
        );


    if (heap_fd < 0)
    {
        perror(
            "open /dev/dma_heap/system"
        );

        goto cleanup;
    }


    /*
     * ========================================================
     * 8. 申请 RGA resize NV12 DMA-BUF
     * ========================================================
     */

    resize_nv12_size =
        (size_t)resized_width *
        (size_t)resized_height *
        3U /
        2U;


    resize_nv12_fd =
        alloc_dma_buffer(
            heap_fd,
            resize_nv12_size
        );


    if (resize_nv12_fd < 0)
    {
        goto cleanup;
    }


    /*
     * ========================================================
     * 9. 申请 RGA RGB888 DMA-BUF
     * ========================================================
     */

    resize_rgb_size =
        (size_t)resized_width *
        (size_t)resized_height *
        3U;


    resize_rgb_fd =
        alloc_dma_buffer(
            heap_fd,
            resize_rgb_size
        );


    if (resize_rgb_fd < 0)
    {
        goto cleanup;
    }


    printf(
        "\n========== RGA BUFFERS ==========\n"
    );

    printf(
        "resize NV12: "
        "%zu bytes, fd=%d\n",
        resize_nv12_size,
        resize_nv12_fd
    );

    printf(
        "resize RGB : "
        "%zu bytes, fd=%d\n",
        resize_rgb_size,
        resize_rgb_fd
    );


    /*
     * ========================================================
     * 10. 创建 RGA Buffer 描述
     * ========================================================
     */

    resize_nv12 =
        wrapbuffer_fd(
            resize_nv12_fd,
            resized_width,
            resized_height,
            RK_FORMAT_YCbCr_420_SP
        );


    resize_rgb =
        wrapbuffer_fd(
            resize_rgb_fd,
            resized_width,
            resized_height,
            RK_FORMAT_RGB_888
        );


    /*
     * ========================================================
     * 11. mmap RGB DMA-BUF
     *
     * 后面 CPU 只读取 640x360 RGB，
     * 用于构造 640x640 Letterbox。
     * ========================================================
     */

    resize_rgb_map =
        (unsigned char *)mmap(
            NULL,
            resize_rgb_size,
            PROT_READ |
            PROT_WRITE,
            MAP_SHARED,
            resize_rgb_fd,
            0
        );


    if (resize_rgb_map ==
        MAP_FAILED)
    {
        resize_rgb_map =
            NULL;

        perror("mmap resize RGB");

        goto cleanup;
    }


    /*
     * 640x640 RGB Canvas
     */
    letterbox_rgb =
        (unsigned char *)malloc(
            letterbox_size
        );


    if (letterbox_rgb == NULL)
    {
        perror("malloc letterbox_rgb");
        goto cleanup;
    }


    /*
     * ========================================================
     * 12. 所有 Camera Buffer 入队
     * ========================================================
     */

    for (uint32_t i = 0;
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
            perror("VIDIOC_QBUF");
            goto cleanup;
        }
    }


    /*
     * ========================================================
     * 13. STREAMON
     * ========================================================
     */

    if (ioctl(
            camera_fd,
            VIDIOC_STREAMON,
            &stream_type) < 0)
    {
        perror("VIDIOC_STREAMON");
        goto cleanup;
    }


    streaming =
        1;


    printf(
        "\nStreaming started\n"
    );


    /*
     * ========================================================
     * 14. 丢掉前面几帧，然后处理一帧
     *
     * 这样 Camera 刚启动时更稳定。
     * ========================================================
     */

    for (uint32_t frame = 0;
         frame < WARMUP_FRAMES;
         frame++)
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

        buf.m.planes =
            planes;

        buf.length =
            1;


        if (ioctl(
                camera_fd,
                VIDIOC_DQBUF,
                &buf) < 0)
        {
            perror("VIDIOC_DQBUF");
            goto cleanup;
        }


        printf(
            "DQBUF frame %u/%u: "
            "index=%u bytesused=%u\n",
            frame + 1,
            WARMUP_FRAMES,
            buf.index,
            planes[0].bytesused
        );


        /*
         * 前 WARMUP_FRAMES - 1 帧：
         * 直接还给驱动。
         */
        if (frame + 1 <
            WARMUP_FRAMES)
        {
            if (ioctl(
                    camera_fd,
                    VIDIOC_QBUF,
                    &buf) < 0)
            {
                perror("VIDIOC_QBUF warmup");
                goto cleanup;
            }

            continue;
        }


        /*
         * ====================================================
         * 15. Camera DMA-BUF → RGA
         * ====================================================
         */

        if (buf.index >=
            buffer_count)
        {
            printf(
                "Invalid DQBUF index: %u\n",
                buf.index
            );

            goto cleanup;
        }


        rga_buffer_t src =
            wrapbuffer_fd(
                buffers[buf.index].dma_fd,
                camera_width,
                camera_height,
                RK_FORMAT_YCbCr_420_SP
            );


        /*
         * 3840x2160 NV12
         *      ↓ RGA
         * 640x360 NV12
         */
        IM_STATUS resize_status =
            imresize(
                src,
                resize_nv12
            );


        if (resize_status !=
            IM_STATUS_SUCCESS)
        {
            printf(
                "imresize failed: %s\n",
                imStrError(
                    resize_status
                )
            );

            /*
             * 当前 DQBUF 出来的 buffer
             * 仍然要还给 Camera。
             */
            ioctl(
                camera_fd,
                VIDIOC_QBUF,
                &buf
            );

            goto cleanup;
        }


        /*
         * 640x360 NV12
         *      ↓ RGA CSC
         * 640x360 RGB888
         */
        IM_STATUS color_status =
            imcvtcolor(
                resize_nv12,
                resize_rgb,
                RK_FORMAT_YCbCr_420_SP,
                RK_FORMAT_RGB_888
            );


        if (color_status !=
            IM_STATUS_SUCCESS)
        {
            printf(
                "imcvtcolor failed: %s\n",
                imStrError(
                    color_status
                )
            );

            ioctl(
                camera_fd,
                VIDIOC_QBUF,
                &buf
            );

            goto cleanup;
        }


        printf(
            "\nRGA resize + RGB CSC success\n"
        );


        /*
         * ====================================================
         * 16. CPU 读取 RGB DMA-BUF
         * ====================================================
         */

        if (dma_buf_sync_read_start(
                resize_rgb_fd) < 0)
        {
            ioctl(
                camera_fd,
                VIDIOC_QBUF,
                &buf
            );

            goto cleanup;
        }


        /*
         * 640x360 RGB
         *      ↓
         * 640x640 RGB Letterbox
         */
        make_letterbox_rgb(
            letterbox_rgb,
            resize_rgb_map,

            resized_width,
            resized_height,

            pad_left,
            pad_top
        );


        if (dma_buf_sync_read_end(
                resize_rgb_fd) < 0)
        {
            ioctl(
                camera_fd,
                VIDIOC_QBUF,
                &buf
            );

            goto cleanup;
        }


        /*
         * ====================================================
         * 17. 保存给 RKNN 的 RAW
         * ====================================================
         */

        const char *raw_path =
            "camera_640_rgb.raw";


        if (save_rgb_raw(
                raw_path,
                letterbox_rgb,
                letterbox_size) < 0)
        {
            ioctl(
                camera_fd,
                VIDIOC_QBUF,
                &buf
            );

            goto cleanup;
        }


        /*
         * ====================================================
         * 18. 保存肉眼检查的 PPM
         * ====================================================
         */

        const char *ppm_path =
            "camera_640_preview.ppm";


        if (save_rgb_ppm(
                ppm_path,
                letterbox_rgb,
                MODEL_WIDTH,
                MODEL_HEIGHT) < 0)
        {
            ioctl(
                camera_fd,
                VIDIOC_QBUF,
                &buf
            );

            goto cleanup;
        }


        /*
         * 当前 Camera Buffer 归还驱动。
         */
        if (ioctl(
                camera_fd,
                VIDIOC_QBUF,
                &buf) < 0)
        {
            perror("VIDIOC_QBUF final");
            goto cleanup;
        }


        printf(
            "\n========== SUCCESS ==========\n"
        );

        printf(
            "saved raw     : %s\n",
            raw_path
        );

        printf(
            "raw size      : %zu bytes\n",
            letterbox_size
        );

        printf(
            "saved preview : %s\n",
            ppm_path
        );

        printf(
            "\nExpected RKNN input:\n"
        );

        printf(
            "640 x 640 x 3 RGB UINT8\n"
        );

        printf(
            "padding value = %d\n",
            LETTERBOX_VALUE
        );


        result =
            0;

        break;
    }


cleanup:

    /*
     * ========================================================
     * 19. STREAMOFF
     * ========================================================
     */

    if (streaming)
    {
        if (ioctl(
                camera_fd,
                VIDIOC_STREAMOFF,
                &stream_type) < 0)
        {
            perror("VIDIOC_STREAMOFF");
        }
        else
        {
            printf(
                "Streaming stopped\n"
            );
        }
    }


    /*
     * ========================================================
     * 20. 释放 CPU / mmap
     * ========================================================
     */

    if (letterbox_rgb != NULL)
    {
        free(
            letterbox_rgb
        );

        letterbox_rgb =
            NULL;
    }


    if (resize_rgb_map != NULL)
    {
        munmap(
            resize_rgb_map,
            resize_rgb_size
        );

        resize_rgb_map =
            NULL;
    }


    /*
     * ========================================================
     * 21. 释放 DMA-BUF
     * ========================================================
     */

    if (resize_rgb_fd >= 0)
    {
        close(
            resize_rgb_fd
        );

        resize_rgb_fd =
            -1;
    }


    if (resize_nv12_fd >= 0)
    {
        close(
            resize_nv12_fd
        );

        resize_nv12_fd =
            -1;
    }


    if (heap_fd >= 0)
    {
        close(
            heap_fd
        );

        heap_fd =
            -1;
    }


    /*
     * ========================================================
     * 22. 释放 Camera Buffer
     * ========================================================
     */

    if (buffers != NULL)
    {
        for (uint32_t i = 0;
             i < buffer_count;
             i++)
        {
            if (buffers[i].start !=
                NULL)
            {
                munmap(
                    buffers[i].start,
                    buffers[i].length
                );

                buffers[i].start =
                    NULL;
            }


            if (buffers[i].dma_fd >= 0)
            {
                close(
                    buffers[i].dma_fd
                );

                buffers[i].dma_fd =
                    -1;
            }
        }


        free(
            buffers
        );

        buffers =
            NULL;
    }


    if (camera_fd >= 0)
    {
        close(
            camera_fd
        );

        camera_fd =
            -1;
    }


    return
        result;
}