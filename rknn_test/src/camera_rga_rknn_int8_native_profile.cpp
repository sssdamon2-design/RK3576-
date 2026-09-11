#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <linux/videodev2.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>

#include <rga/im2d.h>
#include <rga/rga.h>

#include "rknn_api.h"


#define MODEL_W 640U
#define MODEL_H 640U

#define CAMERA_W 3840U
#define CAMERA_H 2160U

#define WARMUP_FRAMES 10U
#define MEASURE_FRAMES 100U
#define PRINT_INTERVAL 20U
#define LETTERBOX_VALUE 114


typedef struct
{
    size_t length;
    int dma_fd;

} CameraBuffer;


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

    data.heap_flags =
        0;


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


static int dma_buf_read_start(
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
            "DMA_BUF_SYNC_START"
        );

        return -1;
    }


    return 0;
}


static int dma_buf_read_end(
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
            "DMA_BUF_SYNC_END"
        );

        return -1;
    }


    return 0;
}


static void make_letterbox_rgb(
    unsigned char *model_input,
    const unsigned char *resize_rgb,

    uint32_t resized_w,
    uint32_t resized_h,

    uint32_t pad_left,
    uint32_t pad_top)
{
    size_t model_size =
        (size_t)MODEL_W *
        (size_t)MODEL_H *
        3U;


    memset(
        model_input,
        LETTERBOX_VALUE,
        model_size
    );


    size_t row_bytes =
        (size_t)resized_w *
        3U;


    for (uint32_t y = 0;
         y < resized_h;
         y++)
    {
        unsigned char *dst =
            model_input
            +
            (
                (size_t)(pad_top + y) *
                MODEL_W
                +
                pad_left
            )
            * 3U;


        const unsigned char *src =
            resize_rgb
            +
            (size_t)y *
            row_bytes;


        memcpy(
            dst,
            src,
            row_bytes
        );
    }
}


static unsigned char *load_file(
    const char *path,
    uint32_t *file_size)
{
    int fd = open(path, O_RDONLY);

    if (fd < 0)
    {
        perror("open");
        return NULL;
    }

    struct stat st;

    if (fstat(fd, &st) < 0)
    {
        perror("fstat");
        close(fd);
        return NULL;
    }

    unsigned char *data =
        (unsigned char *)malloc(st.st_size);

    if (data == NULL)
    {
        perror("malloc");
        close(fd);
        return NULL;
    }

    ssize_t ret =
        read(fd, data, st.st_size);

    close(fd);

    if (ret != st.st_size)
    {
        printf("read file failed\n");
        free(data);
        return NULL;
    }

    *file_size =
        (uint32_t)st.st_size;

    return data;
}


/* =========================================================
 * COCO 80 类
 * ========================================================= */
static const char *coco_labels[80] =
{
    "person",
    "bicycle",
    "car",
    "motorcycle",
    "airplane",
    "bus",
    "train",
    "truck",
    "boat",
    "traffic light",
    "fire hydrant",
    "stop sign",
    "parking meter",
    "bench",
    "bird",
    "cat",
    "dog",
    "horse",
    "sheep",
    "cow",
    "elephant",
    "bear",
    "zebra",
    "giraffe",
    "backpack",
    "umbrella",
    "handbag",
    "tie",
    "suitcase",
    "frisbee",
    "skis",
    "snowboard",
    "sports ball",
    "kite",
    "baseball bat",
    "baseball glove",
    "skateboard",
    "surfboard",
    "tennis racket",
    "bottle",
    "wine glass",
    "cup",
    "fork",
    "knife",
    "spoon",
    "bowl",
    "banana",
    "apple",
    "sandwich",
    "orange",
    "broccoli",
    "carrot",
    "hot dog",
    "pizza",
    "donut",
    "cake",
    "chair",
    "couch",
    "potted plant",
    "bed",
    "dining table",
    "toilet",
    "tv",
    "laptop",
    "mouse",
    "remote",
    "keyboard",
    "cell phone",
    "microwave",
    "oven",
    "toaster",
    "sink",
    "refrigerator",
    "book",
    "clock",
    "vase",
    "scissors",
    "teddy bear",
    "hair drier",
    "toothbrush"
};


/* =========================================================
 * 一个候选目标
 *
 * 当前还没有经过 NMS
 * ========================================================= */
typedef struct
{
    float x1;
    float y1;
    float x2;
    float y2;

    float score;

    uint32_t class_id;

    uint32_t grid_x;
    uint32_t grid_y;

    uint32_t feature_size;

} DetectionCandidate;


/* =========================================================
 * Letterbox 参数
 *
 * 记录：
 * 原图尺寸
 * resize 后尺寸
 * 四周 padding
 * 实际 x/y 缩放比例
 *
 * 这部分必须和生成 R_640_rgb.raw 时的预处理保持一致：
 *
 * scale = min(640 / original_w, 640 / original_h)
 * resized_w = (uint32_t)(original_w * scale)
 * resized_h = (uint32_t)(original_h * scale)
 * 居中 padding
 * ========================================================= */
typedef struct
{
    uint32_t original_w;
    uint32_t original_h;

    uint32_t input_w;
    uint32_t input_h;

    uint32_t resized_w;
    uint32_t resized_h;

    uint32_t pad_left;
    uint32_t pad_top;
    uint32_t pad_right;
    uint32_t pad_bottom;

    float scale;
    float scale_x;
    float scale_y;

} LetterboxInfo;


/* =========================================================
 * 根据原图尺寸计算 Letterbox 参数
 * ========================================================= */
static void calculate_letterbox_info(
    uint32_t original_w,
    uint32_t original_h,
    uint32_t input_w,
    uint32_t input_h,
    LetterboxInfo *info)
{
    float scale_w =
        (float)input_w /
        (float)original_w;

    float scale_h =
        (float)input_h /
        (float)original_h;


    float scale =
        scale_w < scale_h ?
        scale_w : scale_h;


    uint32_t resized_w =
        (uint32_t)(
            (float)original_w *
            scale
        );

    uint32_t resized_h =
        (uint32_t)(
            (float)original_h *
            scale
        );


    /*
     * 防止浮点误差导致尺寸比目标大 1
     */
    if (resized_w > input_w)
        resized_w = input_w;

    if (resized_h > input_h)
        resized_h = input_h;


    uint32_t pad_left =
        (input_w - resized_w) / 2;

    uint32_t pad_top =
        (input_h - resized_h) / 2;

    uint32_t pad_right =
        input_w -
        resized_w -
        pad_left;

    uint32_t pad_bottom =
        input_h -
        resized_h -
        pad_top;


    info->original_w =
        original_w;

    info->original_h =
        original_h;

    info->input_w =
        input_w;

    info->input_h =
        input_h;

    info->resized_w =
        resized_w;

    info->resized_h =
        resized_h;

    info->pad_left =
        pad_left;

    info->pad_top =
        pad_top;

    info->pad_right =
        pad_right;

    info->pad_bottom =
        pad_bottom;

    info->scale =
        scale;


    /*
     * resize 后尺寸经过了整数取整，
     * 所以真正的 x/y 缩放比例分别计算。
     *
     * 这样比只使用 scale 更精确。
     */
    info->scale_x =
        (float)resized_w /
        (float)original_w;

    info->scale_y =
        (float)resized_h /
        (float)original_h;
}


/* =========================================================
 * 把 640x640 Letterbox 坐标恢复到原始图片坐标
 *
 * 正向：
 *
 * original
 *   ↓ resize
 * resized
 *   ↓ + padding
 * 640x640
 *
 * 反向：
 *
 * 640x640 bbox
 *   ↓ - padding
 * resized bbox
 *   ↓ / scale_x, / scale_y
 * original bbox
 * ========================================================= */
static void restore_bbox_to_original(
    const DetectionCandidate *det,
    const LetterboxInfo *info,
    float *x1,
    float *y1,
    float *x2,
    float *y2)
{
    *x1 =
        (det->x1 -
         (float)info->pad_left) /
        info->scale_x;

    *y1 =
        (det->y1 -
         (float)info->pad_top) /
        info->scale_y;

    *x2 =
        (det->x2 -
         (float)info->pad_left) /
        info->scale_x;

    *y2 =
        (det->y2 -
         (float)info->pad_top) /
        info->scale_y;


    /*
     * 限制到原图范围
     */
    if (*x1 < 0.0f)
        *x1 = 0.0f;

    if (*y1 < 0.0f)
        *y1 = 0.0f;

    if (*x2 > (float)info->original_w)
        *x2 = (float)info->original_w;

    if (*y2 > (float)info->original_h)
        *y2 = (float)info->original_h;
}


/* =========================================================
 * DFL Decode
 *
 * 输入：
 * 16 个 logits
 *
 * 输出：
 * 一个距离
 * ========================================================= */
static float dfl_decode(
    const float *logits,
    uint32_t count)
{
    float max_value =
        logits[0];

    /*
     * 先找最大值
     * 防止 exp() 数值过大
     */
    for (uint32_t i = 1;
         i < count;
         i++)
    {
        if (logits[i] > max_value)
        {
            max_value =
                logits[i];
        }
    }

    float sum_exp = 0.0f;
    float weighted_sum = 0.0f;

    /*
     * Softmax + 加权求期望
     */
    for (uint32_t i = 0;
         i < count;
         i++)
    {
        float value =
            expf(
                logits[i] -
                max_value
            );

        sum_exp +=
            value;

        weighted_sum +=
            value * (float)i;
    }

    return
        weighted_sum /
        sum_exp;
}


/* =========================================================
 * INT8 affine 反量化
 *
 * real = (q - zero_point) * scale
 * ========================================================= */
static inline float dequantize_int8(
    int8_t q,
    int32_t zero_point,
    float scale)
{
    return
        ((float)((int32_t)q - zero_point))
        * scale;
}


/* =========================================================
 * 把 float threshold 映射到 INT8 量化域。
 *
 * q_threshold = ceil(real / scale + zero_point)
 * ========================================================= */
static inline int32_t quantize_threshold_int8(
    float threshold,
    int32_t zero_point,
    float scale)
{
    int32_t q =
        (int32_t)ceilf(
            threshold / scale
            + (float)zero_point
        );

    if (q < -128)
        q = -128;

    if (q > 127)
        q = 127;

    return q;
}


/* =========================================================
 * 解码某个网格位置的 bbox
 *
 * bbox_data：
 * 64 x H x W
 *
 * 输出：
 * x1 y1 x2 y2
 *
 * 坐标位于 640x640 模型输入图上
 * ========================================================= */
static void decode_bbox_at_int8(
    const int8_t *bbox_data,
    int32_t bbox_zero_point,
    float bbox_scale,

    uint32_t grid_x,
    uint32_t grid_y,

    uint32_t height,
    uint32_t width,

    uint32_t input_h,
    uint32_t input_w,

    float *x1,
    float *y1,
    float *x2,
    float *y2)
{
    float dfl[64];

    /*
     * bbox output 是 INT8 NCHW。
     * 只对真正通过置信度阈值的网格，读取并反量化 64 个 DFL logits。
     */
    for (uint32_t c = 0;
         c < 64;
         c++)
    {
        uint32_t index =
            c * height * width
            + grid_y * width
            + grid_x;

        dfl[c] =
            dequantize_int8(
                bbox_data[index],
                bbox_zero_point,
                bbox_scale
            );
    }

    float left =
        dfl_decode(
            &dfl[0],
            16
        );

    float top =
        dfl_decode(
            &dfl[16],
            16
        );

    float right =
        dfl_decode(
            &dfl[32],
            16
        );

    float bottom =
        dfl_decode(
            &dfl[48],
            16
        );

    float stride_x =
        (float)input_w /
        (float)width;

    float stride_y =
        (float)input_h /
        (float)height;

    float center_x =
        ((float)grid_x + 0.5f)
        * stride_x;

    float center_y =
        ((float)grid_y + 0.5f)
        * stride_y;

    *x1 =
        center_x -
        left * stride_x;

    *y1 =
        center_y -
        top * stride_y;

    *x2 =
        center_x +
        right * stride_x;

    *y2 =
        center_y +
        bottom * stride_y;

    if (*x1 < 0.0f)
        *x1 = 0.0f;

    if (*y1 < 0.0f)
        *y1 = 0.0f;

    if (*x2 > (float)input_w)
        *x2 = (float)input_w;

    if (*y2 > (float)input_h)
        *y2 = (float)input_h;
}


/* =========================================================
 * 遍历一个检测尺度
 *
 * 例如：
 *
 * bbox  = output[6]
 * class = output[7]
 * H=W=20
 *
 * 对每个网格：
 *
 * 1. 查看 80 个类别
 * 2. 找最高类别
 * 3. 判断置信度是否 >= threshold
 * 4. DFL 解码 bbox
 * 5. 保存 Candidate
 * ========================================================= */
static void collect_candidates_int8(
    const int8_t *bbox_data,
    int32_t bbox_zero_point,
    float bbox_scale,

    const int8_t *class_data,
    int32_t class_zero_point,
    float class_scale,

    uint32_t height,
    uint32_t width,

    float conf_threshold,

    DetectionCandidate *candidates,
    uint32_t max_candidates,
    uint32_t *candidate_count)
{
    const uint32_t num_classes =
        80;

    /*
     * class tensor 使用 per-tensor affine INT8。
     * scale > 0 时，量化值大小关系和真实分数大小关系一致。
     * 因此先在 INT8 域完成 80 类 argmax + threshold，
     * 只有真正的 candidate 才做 float 反量化和 DFL。
     */
    int32_t q_threshold =
        quantize_threshold_int8(
            conf_threshold,
            class_zero_point,
            class_scale
        );

    for (uint32_t y = 0;
         y < height;
         y++)
    {
        for (uint32_t x = 0;
             x < width;
             x++)
        {
            uint32_t first_index =
                y * width + x;

            int8_t best_q =
                class_data[
                    first_index
                ];

            uint32_t best_class =
                0;

            for (uint32_t c = 1;
                 c < num_classes;
                 c++)
            {
                uint32_t index =
                    c * height * width
                    + y * width
                    + x;

                int8_t q =
                    class_data[index];

                if (q > best_q)
                {
                    best_q = q;
                    best_class = c;
                }
            }

            if ((int32_t)best_q <
                q_threshold)
            {
                continue;
            }

            if (*candidate_count >=
                max_candidates)
            {
                return;
            }

            DetectionCandidate *det =
                &candidates[
                    *candidate_count
                ];

            det->class_id =
                best_class;

            det->score =
                dequantize_int8(
                    best_q,
                    class_zero_point,
                    class_scale
                );

            det->grid_x =
                x;

            det->grid_y =
                y;

            det->feature_size =
                width;

            decode_bbox_at_int8(
                bbox_data,
                bbox_zero_point,
                bbox_scale,
                x,
                y,
                height,
                width,
                MODEL_H,
                MODEL_W,
                &det->x1,
                &det->y1,
                &det->x2,
                &det->y2
            );

            (*candidate_count)++;
        }
    }
}


/* =========================================================
 * qsort 比较函数
 *
 * 按 confidence：
 * 从高到低排列
 * ========================================================= */
static int compare_candidate_score(
    const void *a,
    const void *b)
{
    const DetectionCandidate *det_a =
        (const DetectionCandidate *)a;

    const DetectionCandidate *det_b =
        (const DetectionCandidate *)b;


    if (det_a->score <
        det_b->score)
    {
        return 1;
    }

    if (det_a->score >
        det_b->score)
    {
        return -1;
    }

    return 0;
}


/* =========================================================
 * 计算两个候选框的 IoU
 *
 * IoU =
 * 交集面积 / 并集面积
 *
 * 0 -> 完全不重叠
 * 1 -> 两个框完全相同
 * ========================================================= */
static float compute_iou(
    const DetectionCandidate *a,
    const DetectionCandidate *b)
{
    float inter_x1 =
        fmaxf(a->x1, b->x1);

    float inter_y1 =
        fmaxf(a->y1, b->y1);

    float inter_x2 =
        fminf(a->x2, b->x2);

    float inter_y2 =
        fminf(a->y2, b->y2);


    float inter_w =
        inter_x2 - inter_x1;

    float inter_h =
        inter_y2 - inter_y1;


    /*
     * 没有重叠
     */
    if (inter_w <= 0.0f ||
        inter_h <= 0.0f)
    {
        return 0.0f;
    }


    float inter_area =
        inter_w * inter_h;


    float area_a =
        (a->x2 - a->x1) *
        (a->y2 - a->y1);

    float area_b =
        (b->x2 - b->x1) *
        (b->y2 - b->y1);


    float union_area =
        area_a +
        area_b -
        inter_area;


    if (union_area <= 0.0f)
    {
        return 0.0f;
    }


    return
        inter_area /
        union_area;
}


/* =========================================================
 * NMS
 *
 * 输入的 candidates 已经按 score 从高到低排序
 *
 * 处理逻辑：
 *
 * 1. 保留当前最高分框
 * 2. 与后面的同类别框计算 IoU
 * 3. 如果 IoU > threshold
 *    说明两个框高度重叠
 *    抑制低分框
 * 4. 继续处理下一个未被抑制的框
 * ========================================================= */
static uint32_t nms(
    const DetectionCandidate *candidates,
    uint32_t candidate_count,

    float iou_threshold,

    DetectionCandidate *results,
    uint32_t max_results)
{
    if (candidate_count == 0)
    {
        return 0;
    }


    uint8_t *suppressed =
        (uint8_t *)calloc(
            candidate_count,
            sizeof(uint8_t)
        );


    if (suppressed == NULL)
    {
        perror("calloc");
        return 0;
    }


    uint32_t result_count =
        0;


    for (uint32_t i = 0;
         i < candidate_count;
         i++)
    {
        /*
         * 当前框已经被前面的高分框抑制
         */
        if (suppressed[i])
        {
            continue;
        }


        /*
         * 当前框保留下来
         */
        if (result_count <
            max_results)
        {
            results[result_count] =
                candidates[i];

            result_count++;
        }
        else
        {
            break;
        }


        /*
         * 与后面所有候选框比较
         */
        for (uint32_t j = i + 1;
             j < candidate_count;
             j++)
        {
            if (suppressed[j])
            {
                continue;
            }


            /*
             * 不同类别不互相抑制
             *
             * 例如：
             * person 和 bicycle 即使重叠
             * 也不能直接认为是重复框
             */
            if (candidates[i].class_id !=
                candidates[j].class_id)
            {
                continue;
            }


            float iou =
                compute_iou(
                    &candidates[i],
                    &candidates[j]
                );


            /*
             * 高度重叠：
             * 保留高分框 candidates[i]
             * 抑制低分框 candidates[j]
             */
            if (iou >
                iou_threshold)
            {
                suppressed[j] =
                    1;
            }
        }
    }


    free(suppressed);


    return
        result_count;
}


/* =========================================================
 * main
 * ========================================================= */


/*
 * ============================================================
 * YOLO 后处理
 *
 * 输入：
 *   RKNN 的 9 个输出
 *
 * 输出：
 *   DFL + Candidate + NMS
 *   并把 bbox 从 640x640 映射回 Camera 3840x2160
 * ============================================================
 */

typedef struct
{
    double dqbuf_ms;
    double rga_resize_ms;
    double rga_csc_ms;
    double letterbox_ms;

    double rknn_input_set_ms;
    double rknn_run_ms;
    double rknn_output_get_ms;

    double postprocess_ms;
    double qbuf_ms;

    double processing_ms;
    double e2e_ms;

} ProfileStats;


typedef struct
{
    uint32_t candidate_count;
    uint32_t final_count;

    int has_best;

    DetectionCandidate best;

    float camera_x1;
    float camera_y1;
    float camera_x2;
    float camera_y2;

} FrameResult;


static double now_ms(void)
{
    struct timespec ts;

    clock_gettime(
        CLOCK_MONOTONIC,
        &ts
    );

    return
        (double)ts.tv_sec * 1000.0
        +
        (double)ts.tv_nsec / 1000000.0;
}


static void add_profile(
    ProfileStats *sum,
    const ProfileStats *frame)
{
    sum->dqbuf_ms += frame->dqbuf_ms;
    sum->rga_resize_ms += frame->rga_resize_ms;
    sum->rga_csc_ms += frame->rga_csc_ms;
    sum->letterbox_ms += frame->letterbox_ms;

    sum->rknn_input_set_ms += frame->rknn_input_set_ms;
    sum->rknn_run_ms += frame->rknn_run_ms;
    sum->rknn_output_get_ms += frame->rknn_output_get_ms;

    sum->postprocess_ms += frame->postprocess_ms;
    sum->qbuf_ms += frame->qbuf_ms;

    sum->processing_ms += frame->processing_ms;
    sum->e2e_ms += frame->e2e_ms;
}


static void print_profile(
    const ProfileStats *sum,
    uint32_t frame_count)
{
    double n =
        (double)frame_count;

    double avg_e2e =
        sum->e2e_ms / n;

    double fps =
        avg_e2e > 0.0 ?
        1000.0 / avg_e2e :
        0.0;


    printf(
        "\n"
        "========== PERFORMANCE ==========\n"
    );

    printf(
        "frames               = %u\n",
        frame_count
    );

    printf(
        "avg DQBUF wait       = %.3f ms\n",
        sum->dqbuf_ms / n
    );

    printf(
        "avg RGA resize       = %.3f ms\n",
        sum->rga_resize_ms / n
    );

    printf(
        "avg RGA CSC          = %.3f ms\n",
        sum->rga_csc_ms / n
    );

    printf(
        "avg Letterbox        = %.3f ms\n",
        sum->letterbox_ms / n
    );

    printf(
        "avg rknn_inputs_set   = %.3f ms\n",
        sum->rknn_input_set_ms / n
    );

    printf(
        "avg rknn_run          = %.3f ms\n",
        sum->rknn_run_ms / n
    );

    printf(
        "avg outputs_get       = %.3f ms\n",
        sum->rknn_output_get_ms / n
    );

    printf(
        "avg Postprocess       = %.3f ms\n",
        sum->postprocess_ms / n
    );

    printf(
        "avg QBUF              = %.3f ms\n",
        sum->qbuf_ms / n
    );

    printf(
        "avg processing        = %.3f ms\n",
        sum->processing_ms / n
    );

    printf(
        "avg E2E loop          = %.3f ms\n",
        avg_e2e
    );

    printf(
        "pipeline FPS          = %.2f\n",
        fps
    );

    printf(
        "\n"
        "processing = after DQBUF -> after QBUF\n"
        "E2E loop   = before DQBUF -> after QBUF\n"
        "pipeline FPS = 1000 / avg E2E loop\n"
    );
}


static int postprocess_frame_int8(
    rknn_output *outputs,
    const rknn_tensor_attr *output_attrs,
    const LetterboxInfo *letterbox,
    FrameResult *result)
{
    const float conf_threshold =
        0.25f;

    const float nms_threshold =
        0.45f;


    memset(
        result,
        0,
        sizeof(*result)
    );


    const uint32_t max_candidates =
        80U * 80U +
        40U * 40U +
        20U * 20U;


    DetectionCandidate *candidates =
        (DetectionCandidate *)calloc(
            max_candidates,
            sizeof(DetectionCandidate)
        );


    if (candidates == NULL)
    {
        perror("calloc candidates");

        return -1;
    }


    uint32_t candidate_count =
        0;


    collect_candidates_int8(
        (const int8_t *)outputs[0].buf,
        output_attrs[0].zp,
        output_attrs[0].scale,
        (const int8_t *)outputs[1].buf,
        output_attrs[1].zp,
        output_attrs[1].scale,
        80,
        80,
        conf_threshold,
        candidates,
        max_candidates,
        &candidate_count
    );


    collect_candidates_int8(
        (const int8_t *)outputs[3].buf,
        output_attrs[3].zp,
        output_attrs[3].scale,
        (const int8_t *)outputs[4].buf,
        output_attrs[4].zp,
        output_attrs[4].scale,
        40,
        40,
        conf_threshold,
        candidates,
        max_candidates,
        &candidate_count
    );


    collect_candidates_int8(
        (const int8_t *)outputs[6].buf,
        output_attrs[6].zp,
        output_attrs[6].scale,
        (const int8_t *)outputs[7].buf,
        output_attrs[7].zp,
        output_attrs[7].scale,
        20,
        20,
        conf_threshold,
        candidates,
        max_candidates,
        &candidate_count
    );


    if (candidate_count > 1)
    {
        qsort(
            candidates,
            candidate_count,
            sizeof(DetectionCandidate),
            compare_candidate_score
        );
    }


    DetectionCandidate *final_results =
        (DetectionCandidate *)calloc(
            candidate_count > 0 ?
            candidate_count : 1,
            sizeof(DetectionCandidate)
        );


    if (final_results == NULL)
    {
        perror("calloc final_results");

        free(candidates);

        return -1;
    }


    uint32_t final_count =
        nms(
            candidates,
            candidate_count,
            nms_threshold,
            final_results,
            candidate_count
        );


    result->candidate_count =
        candidate_count;

    result->final_count =
        final_count;


    if (final_count > 0)
    {
        result->has_best =
            1;

        result->best =
            final_results[0];


        restore_bbox_to_original(
            &result->best,
            letterbox,
            &result->camera_x1,
            &result->camera_y1,
            &result->camera_x2,
            &result->camera_y2
        );
    }


    free(final_results);
    free(candidates);


    return 0;
}


static void print_frame_result(
    uint32_t frame_index,
    const FrameResult *result)
{
    printf(
        "\n"
        "Frame %u/%u\n",
        frame_index,
        MEASURE_FRAMES
    );

    printf(
        "candidates = %u, "
        "after NMS = %u\n",
        result->candidate_count,
        result->final_count
    );


    if (!result->has_best)
    {
        printf(
            "top detection = none\n"
        );

        return;
    }


    printf(
        "top detection = %s (%u), "
        "confidence = %.6f\n",
        coco_labels[
            result->best.class_id
        ],
        result->best.class_id,
        result->best.score
    );

    printf(
        "camera bbox   = "
        "(%.1f, %.1f) "
        "(%.1f, %.1f)\n",
        result->camera_x1,
        result->camera_y1,
        result->camera_x2,
        result->camera_y2
    );
}


int main(
    int argc,
    char *argv[])
{
    /*
     * ========================================================
     * 本阶段目标：
     *
     * 不再生成 camera_640_rgb.raw
     *
     * Camera
     *   ↓
     * RGA
     *   ↓
     * Letterbox
     *   ↓
     * model_input 内存
     *   ↓
     * rknn_inputs_set()
     *
     * 当前版本：
     * 先 warm-up 10 帧，随后连续处理 100 帧。
     *
     * 同时统计：
     * DQBUF / RGA / Letterbox / RKNN / Postprocess / E2E。
     * ========================================================
     */

    if (argc != 2)
    {
        printf(
            "Usage: %s model.rknn\n",
            argv[0]
        );

        printf(
            "Example: %s "
            "../model/yolov8n_fp_230.rknn\n",
            argv[0]
        );

        return -1;
    }


    /*
     * ========================================================
     * 第 1 步：初始化 RKNN
     *
     * 模型只加载一次。
     * 以后实时循环时不能每帧重新 rknn_init。
     * ========================================================
     */

    uint32_t model_size =
        0;


    unsigned char *model_data =
        load_file(
            argv[1],
            &model_size
        );


    if (model_data == NULL)
    {
        return -1;
    }


    rknn_context ctx =
        0;


    int ret =
        rknn_init(
            &ctx,
            model_data,
            model_size,
            0,
            NULL
        );


    if (ret != RKNN_SUCC)
    {
        printf(
            "rknn_init failed: %d\n",
            ret
        );

        free(model_data);

        return -1;
    }


    printf(
        "rknn_init success\n"
    );


    rknn_input_output_num io_num;

    memset(
        &io_num,
        0,
        sizeof(io_num)
    );


    ret =
        rknn_query(
            ctx,
            RKNN_QUERY_IN_OUT_NUM,
            &io_num,
            sizeof(io_num)
        );


    if (ret != RKNN_SUCC)
    {
        printf(
            "RKNN_QUERY_IN_OUT_NUM failed: %d\n",
            ret
        );

        rknn_destroy(ctx);
        free(model_data);

        return -1;
    }


    printf(
        "RKNN input=%u output=%u\n",
        io_num.n_input,
        io_num.n_output
    );


    if (io_num.n_output < 9)
    {
        printf(
            "Unexpected RKNN output count: %u\n",
            io_num.n_output
        );

        rknn_destroy(ctx);
        free(model_data);

        return -1;
    }


    /*
     * INT8 Native Output 版本：
     * 查询前 9 个逻辑输出的 type / zp / scale。
     * 后处理不再要求 Runtime 把 INT8 输出整体转成 float。
     */
    rknn_tensor_attr output_attrs[9];

    memset(
        output_attrs,
        0,
        sizeof(output_attrs)
    );

    for (uint32_t i = 0;
         i < 9;
         i++)
    {
        output_attrs[i].index =
            i;

        ret =
            rknn_query(
                ctx,
                RKNN_QUERY_OUTPUT_ATTR,
                &output_attrs[i],
                sizeof(rknn_tensor_attr)
            );

        if (ret != RKNN_SUCC)
        {
            printf(
                "RKNN_QUERY_OUTPUT_ATTR[%u] failed: %d\n",
                i,
                ret
            );

            rknn_destroy(ctx);
            free(model_data);

            return -1;
        }

        if (output_attrs[i].type !=
            RKNN_TENSOR_INT8)
        {
            printf(
                "output[%u] is not INT8, type=%d\n",
                i,
                (int)output_attrs[i].type
            );

            printf(
                "This binary is for the INT8 model only.\n"
            );

            rknn_destroy(ctx);
            free(model_data);

            return -1;
        }

        printf(
            "output[%u]: zp=%d scale=%.9f\n",
            i,
            output_attrs[i].zp,
            output_attrs[i].scale
        );
    }


    rknn_output *outputs =
        (rknn_output *)calloc(
            io_num.n_output,
            sizeof(rknn_output)
        );


    if (outputs == NULL)
    {
        perror("calloc outputs");

        rknn_destroy(ctx);
        free(model_data);

        return -1;
    }


    /*
     * ========================================================
     * 第 2 步：打开并配置 Camera
     * ========================================================
     */

    int camera_fd =
        open(
            "/dev/video11",
            O_RDWR
        );


    if (camera_fd < 0)
    {
        perror("open camera");

        free(outputs);
        rknn_destroy(ctx);
        free(model_data);

        return -1;
    }


    struct v4l2_format fmt;

    memset(
        &fmt,
        0,
        sizeof(fmt)
    );


    fmt.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    fmt.fmt.pix_mp.width =
        CAMERA_W;

    fmt.fmt.pix_mp.height =
        CAMERA_H;

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

        close(camera_fd);
        free(outputs);
        rknn_destroy(ctx);
        free(model_data);

        return -1;
    }


    uint32_t camera_width =
        fmt.fmt.pix_mp.width;

    uint32_t camera_height =
        fmt.fmt.pix_mp.height;

    uint32_t camera_stride =
        fmt.fmt.pix_mp
        .plane_fmt[0]
        .bytesperline;


    printf(
        "Camera = %u x %u, stride=%u\n",
        camera_width,
        camera_height,
        camera_stride
    );


    if (camera_stride !=
        camera_width)
    {
        printf(
            "Current code requires "
            "camera stride == width\n"
        );

        close(camera_fd);
        free(outputs);
        rknn_destroy(ctx);
        free(model_data);

        return -1;
    }


    /*
     * Letterbox 信息后面既用于：
     *
     * 1. RGA resize 尺寸
     * 2. CPU Letterbox
     * 3. bbox 反变换
     */
    LetterboxInfo letterbox;


    calculate_letterbox_info(
        camera_width,
        camera_height,
        MODEL_W,
        MODEL_H,
        &letterbox
    );


    printf(
        "Letterbox: resize=%ux%u "
        "padding=(%u,%u,%u,%u)\n",
        letterbox.resized_w,
        letterbox.resized_h,
        letterbox.pad_left,
        letterbox.pad_top,
        letterbox.pad_right,
        letterbox.pad_bottom
    );


    /*
     * ========================================================
     * 第 3 步：申请 Camera Buffer，并导出 DMA-BUF
     * ========================================================
     */

    struct v4l2_requestbuffers req;

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

        close(camera_fd);
        free(outputs);
        rknn_destroy(ctx);
        free(model_data);

        return -1;
    }


    CameraBuffer *buffers =
        (CameraBuffer *)calloc(
            req.count,
            sizeof(CameraBuffer)
        );


    if (buffers == NULL)
    {
        perror("calloc CameraBuffer");

        close(camera_fd);
        free(outputs);
        rknn_destroy(ctx);
        free(model_data);

        return -1;
    }


    for (uint32_t i = 0;
         i < req.count;
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
            perror("VIDIOC_QUERYBUF");

            return -1;
        }


        buffers[i].length =
            planes[0].length;


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

            return -1;
        }


        buffers[i].dma_fd =
            expbuf.fd;


        printf(
            "Camera buffer[%u] "
            "dma_fd=%d\n",
            i,
            buffers[i].dma_fd
        );
    }


    /*
     * ========================================================
     * 第 4 步：准备 RGA 输出 DMA-BUF
     * ========================================================
     */

    int heap_fd =
        open(
            "/dev/dma_heap/system",
            O_RDWR
        );


    if (heap_fd < 0)
    {
        perror("open dma_heap");
        return -1;
    }


    size_t resize_nv12_size =
        (size_t)letterbox.resized_w *
        (size_t)letterbox.resized_h *
        3U /
        2U;


    size_t resize_rgb_size =
        (size_t)letterbox.resized_w *
        (size_t)letterbox.resized_h *
        3U;


    int resize_nv12_fd =
        alloc_dma_buffer(
            heap_fd,
            resize_nv12_size
        );


    int resize_rgb_fd =
        alloc_dma_buffer(
            heap_fd,
            resize_rgb_size
        );


    if (resize_nv12_fd < 0 ||
        resize_rgb_fd < 0)
    {
        return -1;
    }


    rga_buffer_t dst_nv12 =
        wrapbuffer_fd(
            resize_nv12_fd,
            letterbox.resized_w,
            letterbox.resized_h,
            RK_FORMAT_YCbCr_420_SP
        );


    rga_buffer_t dst_rgb =
        wrapbuffer_fd(
            resize_rgb_fd,
            letterbox.resized_w,
            letterbox.resized_h,
            RK_FORMAT_RGB_888
        );


    unsigned char *resize_rgb_data =
        (unsigned char *)mmap(
            NULL,
            resize_rgb_size,
            PROT_READ |
            PROT_WRITE,
            MAP_SHARED,
            resize_rgb_fd,
            0
        );


    if (resize_rgb_data ==
        MAP_FAILED)
    {
        perror("mmap resize_rgb");

        return -1;
    }


    size_t model_input_size =
        (size_t)MODEL_W *
        (size_t)MODEL_H *
        3U;


    unsigned char *model_input =
        (unsigned char *)malloc(
            model_input_size
        );


    if (model_input == NULL)
    {
        perror("malloc model_input");

        return -1;
    }


    /*
     * ========================================================
     * 第 5 步：QBUF + STREAMON
     * ========================================================
     */

    for (uint32_t i = 0;
         i < req.count;
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

            return -1;
        }
    }


    enum v4l2_buf_type type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;


    if (ioctl(
            camera_fd,
            VIDIOC_STREAMON,
            &type) < 0)
    {
        perror("VIDIOC_STREAMON");

        return -1;
    }


    printf(
        "Streaming started\n"
    );


    /*
     * ========================================================
     * 第 6 步：Camera warm-up
     *
     * 这 10 帧不做推理，只 DQBUF → QBUF。
     * 目的是让曝光等 Camera 状态先稳定。
     * ========================================================
     */

    for (uint32_t frame = 0;
         frame < WARMUP_FRAMES;
         frame++)
    {
        struct v4l2_buffer frame_buf;
        struct v4l2_plane frame_planes[1];

        memset(
            &frame_buf,
            0,
            sizeof(frame_buf)
        );

        memset(
            frame_planes,
            0,
            sizeof(frame_planes)
        );


        frame_buf.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        frame_buf.memory =
            V4L2_MEMORY_MMAP;

        frame_buf.m.planes =
            frame_planes;

        frame_buf.length =
            1;


        if (ioctl(
                camera_fd,
                VIDIOC_DQBUF,
                &frame_buf) < 0)
        {
            perror(
                "VIDIOC_DQBUF warmup"
            );

            return -1;
        }


        if (ioctl(
                camera_fd,
                VIDIOC_QBUF,
                &frame_buf) < 0)
        {
            perror(
                "VIDIOC_QBUF warmup"
            );

            return -1;
        }
    }


    printf(
        "Warm-up finished: %u frames\n",
        WARMUP_FRAMES
    );


    /*
     * ========================================================
     * 第 7 步：准备 RKNN 输入描述
     *
     * 结构体本身在 100 帧期间复用。
     * 每帧变化的是 model_input 中的数据。
     * ========================================================
     */

    rknn_input input;

    memset(
        &input,
        0,
        sizeof(input)
    );


    input.index =
        0;

    input.buf =
        model_input;

    input.size =
        model_input_size;

    input.type =
        RKNN_TENSOR_UINT8;

    input.fmt =
        RKNN_TENSOR_NHWC;

    input.pass_through =
        0;


    ProfileStats profile_sum;

    memset(
        &profile_sum,
        0,
        sizeof(profile_sum)
    );


    printf(
        "\n"
        "Start %u-frame synchronous pipeline\n",
        MEASURE_FRAMES
    );


    /*
     * ========================================================
     * 第 8 步：100 帧同步循环
     *
     * DQBUF
     *   ↓
     * RGA resize
     *   ↓
     * RGA CSC
     *   ↓
     * CPU Letterbox
     *   ↓
     * RKNN
     *   ↓
     * Postprocess
     *   ↓
     * QBUF
     * ========================================================
     */

    for (uint32_t frame = 0;
         frame < MEASURE_FRAMES;
         frame++)
    {
        ProfileStats p;

        memset(
            &p,
            0,
            sizeof(p)
        );


        FrameResult frame_result;

        memset(
            &frame_result,
            0,
            sizeof(frame_result)
        );


        struct v4l2_buffer frame_buf;
        struct v4l2_plane frame_planes[1];

        memset(
            &frame_buf,
            0,
            sizeof(frame_buf)
        );

        memset(
            frame_planes,
            0,
            sizeof(frame_planes)
        );


        frame_buf.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        frame_buf.memory =
            V4L2_MEMORY_MMAP;

        frame_buf.m.planes =
            frame_planes;

        frame_buf.length =
            1;


        /*
         * ----------------------------------------------------
         * A. DQBUF
         * ----------------------------------------------------
         */
        double e2e_start =
            now_ms();

        double t0 =
            now_ms();


        if (ioctl(
                camera_fd,
                VIDIOC_DQBUF,
                &frame_buf) < 0)
        {
            perror(
                "VIDIOC_DQBUF"
            );

            return -1;
        }


        double t1 =
            now_ms();


        p.dqbuf_ms =
            t1 - t0;


        /*
         * processing latency 从 DQBUF 已经返回后开始。
         */
        double processing_start =
            t1;


        /*
         * ----------------------------------------------------
         * B. 当前 Camera DMA-BUF → RGA src
         * ----------------------------------------------------
         */

        rga_buffer_t src =
            wrapbuffer_fd(
                buffers[
                    frame_buf.index
                ].dma_fd,
                camera_width,
                camera_height,
                RK_FORMAT_YCbCr_420_SP
            );


        /*
         * ----------------------------------------------------
         * C. RGA Resize
         * ----------------------------------------------------
         */

        t0 =
            now_ms();


        IM_STATUS rga_ret =
            imresize(
                src,
                dst_nv12
            );


        t1 =
            now_ms();


        p.rga_resize_ms =
            t1 - t0;


        if (rga_ret !=
            IM_STATUS_SUCCESS)
        {
            printf(
                "imresize failed: %s\n",
                imStrError(rga_ret)
            );

            return -1;
        }


        /*
         * ----------------------------------------------------
         * D. RGA CSC
         * ----------------------------------------------------
         */

        t0 =
            now_ms();


        rga_ret =
            imcvtcolor(
                dst_nv12,
                dst_rgb,
                RK_FORMAT_YCbCr_420_SP,
                RK_FORMAT_RGB_888
            );


        t1 =
            now_ms();


        p.rga_csc_ms =
            t1 - t0;


        if (rga_ret !=
            IM_STATUS_SUCCESS)
        {
            printf(
                "imcvtcolor failed: %s\n",
                imStrError(rga_ret)
            );

            return -1;
        }


        /*
         * ----------------------------------------------------
         * E. DMA-BUF sync + CPU Letterbox
         *
         * 当前 Letterbox 时间包含：
         *   DMA_BUF_SYNC_START
         *   memset
         *   memcpy
         *   DMA_BUF_SYNC_END
         * ----------------------------------------------------
         */

        t0 =
            now_ms();


        if (dma_buf_read_start(
                resize_rgb_fd) < 0)
        {
            return -1;
        }


        make_letterbox_rgb(
            model_input,
            resize_rgb_data,
            letterbox.resized_w,
            letterbox.resized_h,
            letterbox.pad_left,
            letterbox.pad_top
        );


        if (dma_buf_read_end(
                resize_rgb_fd) < 0)
        {
            return -1;
        }


        t1 =
            now_ms();


        p.letterbox_ms =
            t1 - t0;


        /*
         * ----------------------------------------------------
         * F. rknn_inputs_set
         * ----------------------------------------------------
         */

        t0 =
            now_ms();


        ret =
            rknn_inputs_set(
                ctx,
                1,
                &input
            );


        t1 =
            now_ms();


        p.rknn_input_set_ms =
            t1 - t0;


        if (ret != RKNN_SUCC)
        {
            printf(
                "rknn_inputs_set failed: %d\n",
                ret
            );

            return -1;
        }


        /*
         * ----------------------------------------------------
         * G. rknn_run
         * ----------------------------------------------------
         */

        t0 =
            now_ms();


        ret =
            rknn_run(
                ctx,
                NULL
            );


        t1 =
            now_ms();


        p.rknn_run_ms =
            t1 - t0;


        if (ret != RKNN_SUCC)
        {
            printf(
                "rknn_run failed: %d\n",
                ret
            );

            return -1;
        }


        /*
         * ----------------------------------------------------
         * H. rknn_outputs_get
         * ----------------------------------------------------
         */

        memset(
            outputs,
            0,
            io_num.n_output *
            sizeof(rknn_output)
        );


        for (uint32_t i = 0;
             i < io_num.n_output;
             i++)
        {
            outputs[i].index =
                i;

            outputs[i].want_float =
                0;

            outputs[i].is_prealloc =
                0;
        }


        t0 =
            now_ms();


        ret =
            rknn_outputs_get(
                ctx,
                io_num.n_output,
                outputs,
                NULL
            );


        t1 =
            now_ms();


        p.rknn_output_get_ms =
            t1 - t0;


        if (ret != RKNN_SUCC)
        {
            printf(
                "rknn_outputs_get failed: %d\n",
                ret
            );

            return -1;
        }


        /*
         * ----------------------------------------------------
         * I. YOLO Postprocess
         * ----------------------------------------------------
         */

        t0 =
            now_ms();


        ret =
            postprocess_frame_int8(
                outputs,
                output_attrs,
                &letterbox,
                &frame_result
            );


        t1 =
            now_ms();


        p.postprocess_ms =
            t1 - t0;


        if (ret < 0)
        {
            return -1;
        }


        /*
         * 当前帧 Runtime 输出使用结束。
         */
        rknn_outputs_release(
            ctx,
            io_num.n_output,
            outputs
        );


        /*
         * ----------------------------------------------------
         * J. QBUF
         * ----------------------------------------------------
         */

        t0 =
            now_ms();


        if (ioctl(
                camera_fd,
                VIDIOC_QBUF,
                &frame_buf) < 0)
        {
            perror(
                "VIDIOC_QBUF"
            );

            return -1;
        }


        t1 =
            now_ms();


        p.qbuf_ms =
            t1 - t0;


        double e2e_end =
            t1;


        p.processing_ms =
            e2e_end -
            processing_start;

        p.e2e_ms =
            e2e_end -
            e2e_start;


        add_profile(
            &profile_sum,
            &p
        );


        /*
         * 打印放在计时区间之后，
         * 不计入本帧 stage latency。
         */
        uint32_t frame_number =
            frame + 1;


        if (frame_number == 1 ||
            frame_number ==
                MEASURE_FRAMES ||
            frame_number %
                PRINT_INTERVAL == 0)
        {
            print_frame_result(
                frame_number,
                &frame_result
            );

            printf(
                "E2E = %.3f ms, "
                "RKNN run = %.3f ms\n",
                p.e2e_ms,
                p.rknn_run_ms
            );
        }
    }


    /*
     * ========================================================
     * 第 9 步：100 帧性能汇总
     * ========================================================
     */

    print_profile(
        &profile_sum,
        MEASURE_FRAMES
    );


    /*
     * ========================================================
     * 第 10 步：停止并释放
     * ========================================================
     */

    if (ioctl(
            camera_fd,
            VIDIOC_STREAMOFF,
            &type) < 0)
    {
        perror("VIDIOC_STREAMOFF");
    }


    printf(
        "Streaming stopped\n"
    );


    free(model_input);


    munmap(
        resize_rgb_data,
        resize_rgb_size
    );


    close(resize_rgb_fd);
    close(resize_nv12_fd);
    close(heap_fd);


    for (uint32_t i = 0;
         i < req.count;
         i++)
    {
        if (buffers[i].dma_fd >= 0)
        {
            close(
                buffers[i].dma_fd
            );
        }
    }


    free(buffers);

    close(camera_fd);

    free(outputs);

    rknn_destroy(ctx);

    free(model_data);


    return 0;
}
