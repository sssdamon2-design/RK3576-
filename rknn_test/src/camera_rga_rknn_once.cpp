#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

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
 * 打印 Output Tensor 基本统计信息
 * ========================================================= */
static void print_output(
    uint32_t index,
    const float *data,
    uint32_t n_elems)
{
    if (data == NULL || n_elems == 0)
        return;

    float min_value = data[0];
    float max_value = data[0];

    double sum = 0.0;

    for (uint32_t i = 0;
         i < n_elems;
         i++)
    {
        float value = data[i];

        if (value < min_value)
            min_value = value;

        if (value > max_value)
            max_value = value;

        sum += value;
    }

    double mean =
        sum / (double)n_elems;

    printf(
        "\noutput[%u]\n",
        index
    );

    printf(
        "elements = %u\n",
        n_elems
    );

    printf(
        "min      = %f\n",
        min_value
    );

    printf(
        "max      = %f\n",
        max_value
    );

    printf(
        "mean     = %f\n",
        mean
    );

    uint32_t print_num =
        n_elems < 5 ? n_elems : 5;

    printf("first 5  = ");

    for (uint32_t i = 0;
         i < print_num;
         i++)
    {
        printf(
            "%f ",
            data[i]
        );
    }

    printf("\n");
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
static void decode_bbox_at(
    const float *bbox_data,

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
     * output 是 NCHW
     *
     * 同一个 (x,y) 的 64 个 channel
     * 在内存中并不连续
     *
     * 所以逐个取出来
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
            bbox_data[index];
    }

    /*
     * channel:
     *
     * 0  ~ 15  → left
     * 16 ~ 31  → top
     * 32 ~ 47  → right
     * 48 ~ 63  → bottom
     */
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


    /*
     * 计算 stride
     *
     * 80x80 → 8
     * 40x40 → 16
     * 20x20 → 32
     */
    float stride_x =
        (float)input_w /
        (float)width;

    float stride_y =
        (float)input_h /
        (float)height;


    /*
     * 当前网格中心点
     */
    float center_x =
        ((float)grid_x + 0.5f)
        * stride_x;

    float center_y =
        ((float)grid_y + 0.5f)
        * stride_y;


    /*
     * DFL 距离
     * 转换成真实像素坐标
     */
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


    /*
     * 限制到输入图范围
     */
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
static void collect_candidates(
    const float *bbox_data,
    const float *class_data,

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
     * 遍历整个 feature map
     */
    for (uint32_t y = 0;
         y < height;
         y++)
    {
        for (uint32_t x = 0;
             x < width;
             x++)
        {
            /*
             * 当前网格位置
             *
             * 先从 class 0 开始
             */
            uint32_t first_index =
                y * width + x;

            float best_score =
                class_data[
                    first_index
                ];

            uint32_t best_class =
                0;


            /*
             * 查看剩余 79 个类别
             */
            for (uint32_t c = 1;
                 c < num_classes;
                 c++)
            {
                uint32_t index =
                    c * height * width
                    + y * width
                    + x;

                float score =
                    class_data[index];


                if (score >
                    best_score)
                {
                    best_score =
                        score;

                    best_class =
                        c;
                }
            }


            /*
             * 低于置信度阈值
             * 直接跳过
             */
            if (best_score <
                conf_threshold)
            {
                continue;
            }


            /*
             * 防止候选数组溢出
             */
            if (*candidate_count >=
                max_candidates)
            {
                return;
            }


            DetectionCandidate *det =
                &candidates[
                    *candidate_count
                ];


            /*
             * 保存类别信息
             */
            det->class_id =
                best_class;

            det->score =
                best_score;

            det->grid_x =
                x;

            det->grid_y =
                y;

            det->feature_size =
                width;


            /*
             * 自动去对应 bbox Tensor
             * 解码当前网格的 64 个 bbox channel
             */
            decode_bbox_at(
                bbox_data,

                x,
                y,

                height,
                width,

                640,
                640,

                &det->x1,
                &det->y1,
                &det->x2,
                &det->y2
            );


            /*
             * Candidate 数量 +1
             */
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
static void postprocess_and_print(
    rknn_output *outputs,
    const LetterboxInfo *letterbox)
{
    const float conf_threshold =
        0.25f;

    const float nms_threshold =
        0.45f;


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
        return;
    }


    uint32_t candidate_count =
        0;


    collect_candidates(
        (const float *)outputs[0].buf,
        (const float *)outputs[1].buf,
        80,
        80,
        conf_threshold,
        candidates,
        max_candidates,
        &candidate_count
    );


    collect_candidates(
        (const float *)outputs[3].buf,
        (const float *)outputs[4].buf,
        40,
        40,
        conf_threshold,
        candidates,
        max_candidates,
        &candidate_count
    );


    collect_candidates(
        (const float *)outputs[6].buf,
        (const float *)outputs[7].buf,
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

        return;
    }


    uint32_t final_count =
        nms(
            candidates,
            candidate_count,
            nms_threshold,
            final_results,
            candidate_count
        );


    printf(
        "\n========== DETECTION ==========\n"
    );

    printf(
        "candidate count = %u\n",
        candidate_count
    );

    printf(
        "after NMS       = %u\n",
        final_count
    );


    for (uint32_t i = 0;
         i < final_count;
         i++)
    {
        DetectionCandidate *det =
            &final_results[i];


        float x1;
        float y1;
        float x2;
        float y2;


        restore_bbox_to_original(
            det,
            letterbox,
            &x1,
            &y1,
            &x2,
            &y2
        );


        printf(
            "\nDetection %u\n",
            i
        );

        printf(
            "class      = %s (%u)\n",
            coco_labels[
                det->class_id
            ],
            det->class_id
        );

        printf(
            "confidence = %.6f\n",
            det->score
        );

        printf(
            "model bbox = "
            "(%.1f, %.1f) "
            "(%.1f, %.1f)\n",
            det->x1,
            det->y1,
            det->x2,
            det->y2
        );

        printf(
            "camera bbox = "
            "(%.1f, %.1f) "
            "(%.1f, %.1f)\n",
            x1,
            y1,
            x2,
            y2
        );
    }


    free(final_results);
    free(candidates);
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
     * 当前仍然只处理一帧。
     * 下一阶段再改为真正实时循环。
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
     * 第 6 步：DQBUF
     *
     * 前 9 帧 warm-up；
     * 第 10 帧直接进入 RGA + RKNN。
     * ========================================================
     */

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


    for (uint32_t frame = 0;
         frame < WARMUP_FRAMES;
         frame++)
    {
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
            perror("VIDIOC_DQBUF");

            return -1;
        }


        printf(
            "DQBUF %u/%u index=%u\n",
            frame + 1,
            WARMUP_FRAMES,
            frame_buf.index
        );


        if (frame + 1 <
            WARMUP_FRAMES)
        {
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
    }


    /*
     * ========================================================
     * 第 7 步：Camera DMA-BUF → RGA
     * ========================================================
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

        return -1;
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

        return -1;
    }


    printf(
        "RGA success\n"
    );


    /*
     * ========================================================
     * 第 8 步：RGB DMA-BUF → CPU Letterbox
     *
     * 这里是当前 baseline 仍保留的一次小图 CPU copy。
     * ========================================================
     */

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


    printf(
        "Letterbox success: "
        "%u x %u RGB UINT8\n",
        MODEL_W,
        MODEL_H
    );


    /*
     * ========================================================
     * 第 9 步：直接把内存交给 RKNN
     *
     * 这里已经没有：
     *
     * fwrite(camera_640_rgb.raw)
     * load_file(camera_640_rgb.raw)
     *
     * model_input 直接成为 input.buf。
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


    ret =
        rknn_inputs_set(
            ctx,
            1,
            &input
        );


    if (ret != RKNN_SUCC)
    {
        printf(
            "rknn_inputs_set failed: %d\n",
            ret
        );

        return -1;
    }


    ret =
        rknn_run(
            ctx,
            NULL
        );


    if (ret != RKNN_SUCC)
    {
        printf(
            "rknn_run failed: %d\n",
            ret
        );

        return -1;
    }


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
            1;

        outputs[i].is_prealloc =
            0;
    }


    ret =
        rknn_outputs_get(
            ctx,
            io_num.n_output,
            outputs,
            NULL
        );


    if (ret != RKNN_SUCC)
    {
        printf(
            "rknn_outputs_get failed: %d\n",
            ret
        );

        return -1;
    }


    printf(
        "RKNN inference success\n"
    );


    /*
     * ========================================================
     * 第 10 步：DFL + NMS + Camera 坐标恢复
     * ========================================================
     */

    postprocess_and_print(
        outputs,
        &letterbox
    );


    /*
     * Runtime 分配的 outputs[i].buf
     */
    rknn_outputs_release(
        ctx,
        io_num.n_output,
        outputs
    );


    /*
     * ========================================================
     * 第 11 步：当前 Camera Buffer 归还驱动
     * ========================================================
     */

    if (ioctl(
            camera_fd,
            VIDIOC_QBUF,
            &frame_buf) < 0)
    {
        perror("VIDIOC_QBUF final");

        return -1;
    }


    /*
     * ========================================================
     * 第 12 步：停止并释放
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