#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "rknn_api.h"


/* =========================================================
 * 读取整个文件到内存
 * ========================================================= */
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
int main(
    int argc,
    char *argv[])
{
    /*
     * argv[1] = RKNN 模型
     * argv[2] = 640x640 RGB RAW
     * argv[3] = 原始图片宽度
     * argv[4] = 原始图片高度
     */
    if (argc != 5)
    {
        printf(
            "Usage: %s model.rknn input.raw original_width original_height\n",
            argv[0]
        );

        printf(
            "Example: %s ../model/yolov8n_fp_230.rknn "
            "R_640_rgb.raw 1280 720\n",
            argv[0]
        );

        return -1;
    }


    char *endptr_w = NULL;
    char *endptr_h = NULL;

    unsigned long parsed_w =
        strtoul(
            argv[3],
            &endptr_w,
            10
        );

    unsigned long parsed_h =
        strtoul(
            argv[4],
            &endptr_h,
            10
        );


    if (endptr_w == argv[3] ||
        *endptr_w != '\0' ||
        endptr_h == argv[4] ||
        *endptr_h != '\0' ||
        parsed_w == 0 ||
        parsed_h == 0 ||
        parsed_w > UINT32_MAX ||
        parsed_h > UINT32_MAX)
    {
        printf(
            "Invalid original image size: %s x %s\n",
            argv[3],
            argv[4]
        );

        return -1;
    }


    uint32_t original_width =
        (uint32_t)parsed_w;

    uint32_t original_height =
        (uint32_t)parsed_h;


    LetterboxInfo letterbox;

    calculate_letterbox_info(
        original_width,
        original_height,
        640,
        640,
        &letterbox
    );


    printf(
        "original image = %u x %u\n",
        original_width,
        original_height
    );

    printf(
        "letterbox resized = %u x %u\n",
        letterbox.resized_w,
        letterbox.resized_h
    );

    printf(
        "letterbox padding = "
        "left:%u top:%u right:%u bottom:%u\n",
        letterbox.pad_left,
        letterbox.pad_top,
        letterbox.pad_right,
        letterbox.pad_bottom
    );

    printf(
        "letterbox scale = %.6f "
        "(scale_x=%.6f scale_y=%.6f)\n",
        letterbox.scale,
        letterbox.scale_x,
        letterbox.scale_y
    );


    /* =====================================================
     * 1. 读取模型
     * ===================================================== */

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


    /* =====================================================
     * 2. 读取输入图片
     * ===================================================== */

    uint32_t input_size =
        0;

    unsigned char *input_data =
        load_file(
            argv[2],
            &input_size
        );

    if (input_data == NULL)
    {
        free(model_data);
        return -1;
    }


    /*
     * RGB UINT8
     *
     * 640 x 640 x 3
     */
    if (input_size !=
        640 * 640 * 3)
    {
        printf(
            "Invalid input size: %u\n",
            input_size
        );

        free(input_data);
        free(model_data);

        return -1;
    }


    /* =====================================================
     * 3. 初始化 RKNN
     * ===================================================== */

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

        free(input_data);
        free(model_data);

        return -1;
    }


    printf(
        "rknn_init success\n"
    );


    /* =====================================================
     * 4. 查询输入输出数量
     * ===================================================== */

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
            "rknn_query failed\n"
        );

        rknn_destroy(ctx);
        free(input_data);
        free(model_data);

        return -1;
    }


    printf(
        "input = %u, output = %u\n",
        io_num.n_input,
        io_num.n_output
    );


    /*
     * 当前这份 Rockchip YOLOv8
     * 需要 9 个输出
     */
    if (io_num.n_output < 9)
    {
        printf(
            "Unexpected output number: %u\n",
            io_num.n_output
        );

        rknn_destroy(ctx);
        free(input_data);
        free(model_data);

        return -1;
    }


    /* =====================================================
     * 5. 查询 Output Tensor 属性
     * ===================================================== */

    rknn_tensor_attr *output_attrs =
        (rknn_tensor_attr *)calloc(
            io_num.n_output,
            sizeof(rknn_tensor_attr)
        );


    if (output_attrs == NULL)
    {
        perror("calloc");

        rknn_destroy(ctx);
        free(input_data);
        free(model_data);

        return -1;
    }


    for (uint32_t i = 0;
         i < io_num.n_output;
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
                "query output[%u] failed: %d\n",
                i,
                ret
            );

            free(output_attrs);
            rknn_destroy(ctx);
            free(input_data);
            free(model_data);

            return -1;
        }
    }


    /* =====================================================
     * 6. 设置输入
     * ===================================================== */

    rknn_input input;

    memset(
        &input,
        0,
        sizeof(input)
    );


    input.index =
        0;

    input.buf =
        input_data;

    input.size =
        input_size;

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

        free(output_attrs);
        rknn_destroy(ctx);
        free(input_data);
        free(model_data);

        return -1;
    }


    printf(
        "rknn_inputs_set success\n"
    );


    /* =====================================================
     * 7. 准备输出
     * ===================================================== */

    rknn_output *outputs =
        (rknn_output *)calloc(
            io_num.n_output,
            sizeof(rknn_output)
        );


    if (outputs == NULL)
    {
        perror("calloc");

        free(output_attrs);
        rknn_destroy(ctx);
        free(input_data);
        free(model_data);

        return -1;
    }


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


    /* =====================================================
     * 8. NPU 推理
     * ===================================================== */

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

        free(outputs);
        free(output_attrs);
        rknn_destroy(ctx);
        free(input_data);
        free(model_data);

        return -1;
    }


    printf(
        "rknn_run success\n"
    );


    /* =====================================================
     * 9. 获取输出
     * ===================================================== */

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

        free(outputs);
        free(output_attrs);
        rknn_destroy(ctx);
        free(input_data);
        free(model_data);

        return -1;
    }


    printf(
        "rknn_outputs_get success\n"
    );


    /* =====================================================
     * 10. 输出 Tensor 调试信息
     * ===================================================== */

    printf(
        "\n========== OUTPUT DATA ==========\n"
    );


    for (uint32_t i = 0;
         i < io_num.n_output;
         i++)
    {
        const float *data =
            (const float *)
            outputs[i].buf;


        print_output(
            i,
            data,
            output_attrs[i].n_elems
        );
    }


    /* =====================================================
     * 11. 收集三个尺度的所有候选框
     * ===================================================== */

    const float conf_threshold =
        0.25f;


    /*
     * 每个网格最多保留一个类别
     *
     * 80x80 = 6400
     * 40x40 = 1600
     * 20x20 = 400
     *
     * 总共 8400
     */
    const uint32_t max_candidates =
        80 * 80 +
        40 * 40 +
        20 * 20;


    DetectionCandidate *candidates =
        (DetectionCandidate *)calloc(
            max_candidates,
            sizeof(DetectionCandidate)
        );


    if (candidates == NULL)
    {
        perror("calloc");

        rknn_outputs_release(
            ctx,
            io_num.n_output,
            outputs
        );

        free(outputs);
        free(output_attrs);

        rknn_destroy(ctx);

        free(input_data);
        free(model_data);

        return -1;
    }


    uint32_t candidate_count =
        0;


    /*
     * -------------------------------------
     * 80x80
     *
     * bbox  = output[0]
     * class = output[1]
     *
     * stride = 8
     * -------------------------------------
     */
    collect_candidates(
        (const float *)
        outputs[0].buf,

        (const float *)
        outputs[1].buf,

        80,
        80,

        conf_threshold,

        candidates,
        max_candidates,
        &candidate_count
    );


    /*
     * -------------------------------------
     * 40x40
     *
     * bbox  = output[3]
     * class = output[4]
     *
     * stride = 16
     * -------------------------------------
     */
    collect_candidates(
        (const float *)
        outputs[3].buf,

        (const float *)
        outputs[4].buf,

        40,
        40,

        conf_threshold,

        candidates,
        max_candidates,
        &candidate_count
    );


    /*
     * -------------------------------------
     * 20x20
     *
     * bbox  = output[6]
     * class = output[7]
     *
     * stride = 32
     * -------------------------------------
     */
    collect_candidates(
        (const float *)
        outputs[6].buf,

        (const float *)
        outputs[7].buf,

        20,
        20,

        conf_threshold,

        candidates,
        max_candidates,
        &candidate_count
    );


    printf(
        "\n========== CANDIDATES ==========\n"
    );

    printf(
        "confidence threshold = %.2f\n",
        conf_threshold
    );

    printf(
        "candidate count      = %u\n",
        candidate_count
    );


    /* =====================================================
     * 12. 按置信度从高到低排序
     * ===================================================== */

    if (candidate_count > 1)
    {
        qsort(
            candidates,
            candidate_count,
            sizeof(DetectionCandidate),
            compare_candidate_score
        );
    }


    /* =====================================================
     * 13. 打印前 20 个 Candidate
     * ===================================================== */

    uint32_t print_count =
        candidate_count < 20 ?
        candidate_count : 20;


    printf(
        "\n========== TOP CANDIDATES ==========\n"
    );


    for (uint32_t i = 0;
         i < print_count;
         i++)
    {
        DetectionCandidate *det =
            &candidates[i];


        printf(
            "\nCandidate %u\n",
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
            "feature    = %ux%u\n",
            det->feature_size,
            det->feature_size
        );

        printf(
            "grid       = (%u, %u)\n",
            det->grid_x,
            det->grid_y
        );

        printf(
            "bbox       = "
            "(%.1f, %.1f) "
            "(%.1f, %.1f)\n",
            det->x1,
            det->y1,
            det->x2,
            det->y2
        );
    }


    /* =====================================================
     * 14. NMS 去除重复框
     * ===================================================== */

    const float nms_iou_threshold =
        0.45f;


    DetectionCandidate *final_results =
        (DetectionCandidate *)calloc(
            candidate_count > 0 ?
            candidate_count : 1,
            sizeof(DetectionCandidate)
        );


    if (final_results == NULL)
    {
        perror("calloc");

        free(candidates);

        rknn_outputs_release(
            ctx,
            io_num.n_output,
            outputs
        );

        free(outputs);
        free(output_attrs);

        rknn_destroy(ctx);

        free(input_data);
        free(model_data);

        return -1;
    }


    uint32_t final_count =
        nms(
            candidates,
            candidate_count,

            nms_iou_threshold,

            final_results,
            candidate_count
        );


    printf(
        "\n========== NMS RESULT ==========\n"
    );

    printf(
        "NMS IoU threshold = %.2f\n",
        nms_iou_threshold
    );

    printf(
        "before NMS        = %u\n",
        candidate_count
    );

    printf(
        "after NMS         = %u\n",
        final_count
    );


    for (uint32_t i = 0;
         i < final_count;
         i++)
    {
        DetectionCandidate *det =
            &final_results[i];


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
            "feature    = %ux%u\n",
            det->feature_size,
            det->feature_size
        );

        printf(
            "grid       = (%u, %u)\n",
            det->grid_x,
            det->grid_y
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


        float original_x1;
        float original_y1;
        float original_x2;
        float original_y2;


        restore_bbox_to_original(
            det,
            &letterbox,

            &original_x1,
            &original_y1,
            &original_x2,
            &original_y2
        );


        printf(
            "original bbox = "
            "(%.1f, %.1f) "
            "(%.1f, %.1f)\n",
            original_x1,
            original_y1,
            original_x2,
            original_y2
        );
    }


    /* =====================================================
     * 16. 释放资源
     * ===================================================== */

    free(final_results);

    free(candidates);


    /*
     * outputs[i].buf
     * 是 Runtime 分配的
     */
    rknn_outputs_release(
        ctx,
        io_num.n_output,
        outputs
    );


    /*
     * outputs / output_attrs
     * 是我们自己 calloc 的
     */
    free(outputs);

    free(output_attrs);


    /*
     * 销毁 RKNN Context
     */
    rknn_destroy(ctx);


    /*
     * load_file() 中 malloc 的内存
     */
    free(input_data);

    free(model_data);


    return 0;
}