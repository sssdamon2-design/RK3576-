#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#include "rknn_api.h"

#define MODEL_W 640U
#define MODEL_H 640U
#define INPUT_SIZE (MODEL_W * MODEL_H * 3U)
#define NUM_CLASSES 80U
#define DFL_BINS 16U
#define BBOX_CHANNELS 64U

#define CONF_THRESHOLD 0.001f
#define NMS_THRESHOLD 0.70f
#define MAX_DETECTIONS 300U

typedef struct {
    float x1, y1, x2, y2;
    float score;
    uint32_t class_id;
} DetectionCandidate;

typedef struct {
    long long image_id;
    char file_name[256];
    char raw_name[256];
    uint32_t original_w;
    uint32_t original_h;
    float scale_from_meta;
    uint32_t pad_left;
    uint32_t pad_top;
    uint32_t resized_w;
    uint32_t resized_h;
    float scale_x;
    float scale_y;
} ImageMeta;

static const int YOLO_TO_COCO_CATEGORY[NUM_CLASSES] = {
     1,  2,  3,  4,  5,  6,  7,  8,  9, 10,
    11, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 27, 28, 31, 32, 33, 34,
    35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
    46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
    67, 70, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 84, 85, 86, 87, 88, 89, 90
};

static double now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static int read_full(int fd, void *buf, size_t size)
{
    unsigned char *p = (unsigned char *)buf;
    size_t done = 0;
    while (done < size) {
        ssize_t ret = read(fd, p + done, size - done);
        if (ret < 0) {
            perror("read");
            return -1;
        }
        if (ret == 0) {
            printf("unexpected EOF: need=%zu read=%zu\n", size, done);
            return -1;
        }
        done += (size_t)ret;
    }
    return 0;
}

static unsigned char *load_file(const char *path, uint32_t *file_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return NULL;
    }

    if (st.st_size <= 0 || (uint64_t)st.st_size > UINT32_MAX) {
        printf("invalid file size: %s\n", path);
        close(fd);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)st.st_size);
    if (data == NULL) {
        perror("malloc model");
        close(fd);
        return NULL;
    }

    if (read_full(fd, data, (size_t)st.st_size) != 0) {
        free(data);
        close(fd);
        return NULL;
    }

    close(fd);
    *file_size = (uint32_t)st.st_size;
    return data;
}

static int load_raw_rgb(const char *path, unsigned char *buf, size_t expected_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat raw");
        close(fd);
        return -1;
    }

    if ((size_t)st.st_size != expected_size) {
        printf("RAW size mismatch: %s actual=%lld expected=%zu\n",
               path, (long long)st.st_size, expected_size);
        close(fd);
        return -1;
    }

    int ret = read_full(fd, buf, expected_size);
    close(fd);
    return ret;
}

static float dfl_decode(const float *logits, uint32_t count)
{
    float max_value = logits[0];
    for (uint32_t i = 1; i < count; i++) {
        if (logits[i] > max_value) max_value = logits[i];
    }

    float sum_exp = 0.0f;
    float weighted_sum = 0.0f;
    for (uint32_t i = 0; i < count; i++) {
        float value = expf(logits[i] - max_value);
        sum_exp += value;
        weighted_sum += value * (float)i;
    }

    if (sum_exp <= 0.0f) return 0.0f;
    return weighted_sum / sum_exp;
}

static void decode_bbox_at(const float *bbox_data,
                           uint32_t grid_x, uint32_t grid_y,
                           uint32_t height, uint32_t width,
                           float *x1, float *y1, float *x2, float *y2)
{
    float dfl[BBOX_CHANNELS];

    for (uint32_t c = 0; c < BBOX_CHANNELS; c++) {
        size_t index = (size_t)c * height * width +
                       (size_t)grid_y * width + grid_x;
        dfl[c] = bbox_data[index];
    }

    float left   = dfl_decode(&dfl[0],  DFL_BINS);
    float top    = dfl_decode(&dfl[16], DFL_BINS);
    float right  = dfl_decode(&dfl[32], DFL_BINS);
    float bottom = dfl_decode(&dfl[48], DFL_BINS);

    float stride_x = (float)MODEL_W / (float)width;
    float stride_y = (float)MODEL_H / (float)height;

    float center_x = ((float)grid_x + 0.5f) * stride_x;
    float center_y = ((float)grid_y + 0.5f) * stride_y;

    *x1 = center_x - left * stride_x;
    *y1 = center_y - top * stride_y;
    *x2 = center_x + right * stride_x;
    *y2 = center_y + bottom * stride_y;

    if (*x1 < 0.0f) *x1 = 0.0f;
    if (*y1 < 0.0f) *y1 = 0.0f;
    if (*x2 > (float)MODEL_W) *x2 = (float)MODEL_W;
    if (*y2 > (float)MODEL_H) *y2 = (float)MODEL_H;
}

static void collect_candidates(const float *bbox_data,
                               const float *class_data,
                               uint32_t height, uint32_t width,
                               DetectionCandidate *candidates,
                               uint32_t max_candidates,
                               uint32_t *candidate_count)
{
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            size_t first_index = (size_t)y * width + x;
            float best_score = class_data[first_index];
            uint32_t best_class = 0;

            for (uint32_t c = 1; c < NUM_CLASSES; c++) {
                size_t index = (size_t)c * height * width +
                               (size_t)y * width + x;
                float score = class_data[index];
                if (score > best_score) {
                    best_score = score;
                    best_class = c;
                }
            }

            if (best_score < CONF_THRESHOLD) continue;
            if (*candidate_count >= max_candidates) return;

            DetectionCandidate *det = &candidates[*candidate_count];
            det->score = best_score;
            det->class_id = best_class;

            decode_bbox_at(bbox_data, x, y, height, width,
                           &det->x1, &det->y1, &det->x2, &det->y2);

            (*candidate_count)++;
        }
    }
}

static int compare_candidate_score(const void *a, const void *b)
{
    const DetectionCandidate *det_a = (const DetectionCandidate *)a;
    const DetectionCandidate *det_b = (const DetectionCandidate *)b;
    if (det_a->score < det_b->score) return 1;
    if (det_a->score > det_b->score) return -1;
    return 0;
}

static float compute_iou(const DetectionCandidate *a,
                         const DetectionCandidate *b)
{
    float inter_x1 = fmaxf(a->x1, b->x1);
    float inter_y1 = fmaxf(a->y1, b->y1);
    float inter_x2 = fminf(a->x2, b->x2);
    float inter_y2 = fminf(a->y2, b->y2);

    float inter_w = inter_x2 - inter_x1;
    float inter_h = inter_y2 - inter_y1;

    if (inter_w <= 0.0f || inter_h <= 0.0f) return 0.0f;

    float inter_area = inter_w * inter_h;
    float area_a = (a->x2 - a->x1) * (a->y2 - a->y1);
    float area_b = (b->x2 - b->x1) * (b->y2 - b->y1);
    float union_area = area_a + area_b - inter_area;

    if (union_area <= 0.0f) return 0.0f;
    return inter_area / union_area;
}

/*
 * Greedy class-aware NMS.
 * candidates must be sorted by score descending.
 * Each new candidate is compared only with already-kept higher-score boxes.
 */
static uint32_t nms(const DetectionCandidate *candidates,
                    uint32_t candidate_count,
                    DetectionCandidate *results,
                    uint32_t max_results)
{
    uint32_t result_count = 0;

    for (uint32_t i = 0; i < candidate_count; i++) {
        const DetectionCandidate *candidate = &candidates[i];
        int suppressed = 0;

        for (uint32_t j = 0; j < result_count; j++) {
            if (candidate->class_id != results[j].class_id) continue;

            float iou = compute_iou(candidate, &results[j]);
            if (iou > NMS_THRESHOLD) {
                suppressed = 1;
                break;
            }
        }

        if (suppressed) continue;

        results[result_count] = *candidate;
        result_count++;

        if (result_count >= max_results) break;
    }

    return result_count;
}

static void restore_bbox_to_original(const DetectionCandidate *det,
                                     const ImageMeta *meta,
                                     float *x1, float *y1,
                                     float *x2, float *y2)
{
    *x1 = (det->x1 - (float)meta->pad_left) / meta->scale_x;
    *y1 = (det->y1 - (float)meta->pad_top)  / meta->scale_y;
    *x2 = (det->x2 - (float)meta->pad_left) / meta->scale_x;
    *y2 = (det->y2 - (float)meta->pad_top)  / meta->scale_y;

    if (*x1 < 0.0f) *x1 = 0.0f;
    if (*y1 < 0.0f) *y1 = 0.0f;
    if (*x2 < 0.0f) *x2 = 0.0f;
    if (*y2 < 0.0f) *y2 = 0.0f;

    if (*x1 > (float)meta->original_w) *x1 = (float)meta->original_w;
    if (*x2 > (float)meta->original_w) *x2 = (float)meta->original_w;
    if (*y1 > (float)meta->original_h) *y1 = (float)meta->original_h;
    if (*y2 > (float)meta->original_h) *y2 = (float)meta->original_h;
}

static int read_next_meta(FILE *fp, ImageMeta *meta)
{
    int ret = fscanf(fp,
                     "%lld %255s %255s %u %u %f %u %u %u %u",
                     &meta->image_id,
                     meta->file_name,
                     meta->raw_name,
                     &meta->original_w,
                     &meta->original_h,
                     &meta->scale_from_meta,
                     &meta->pad_left,
                     &meta->pad_top,
                     &meta->resized_w,
                     &meta->resized_h);

    if (ret == EOF) return 0;

    if (ret != 10) {
        printf("metadata parse failed: fields=%d\n", ret);
        return -1;
    }

    if (meta->original_w == 0 || meta->original_h == 0 ||
        meta->resized_w == 0 || meta->resized_h == 0) {
        printf("invalid metadata for image_id=%lld\n", meta->image_id);
        return -1;
    }

    meta->scale_x = (float)meta->resized_w / (float)meta->original_w;
    meta->scale_y = (float)meta->resized_h / (float)meta->original_h;
    return 1;
}

static int process_outputs(rknn_output *outputs,
                           const ImageMeta *meta,
                           FILE *json_fp,
                           int *first_json_item,
                           uint32_t *written_detections)
{
    const uint32_t max_candidates = 80U * 80U + 40U * 40U + 20U * 20U;

    DetectionCandidate *candidates =
        (DetectionCandidate *)calloc(max_candidates, sizeof(DetectionCandidate));

    DetectionCandidate *results =
        (DetectionCandidate *)calloc(MAX_DETECTIONS, sizeof(DetectionCandidate));

    if (candidates == NULL || results == NULL) {
        perror("calloc postprocess");
        free(candidates);
        free(results);
        return -1;
    }

    uint32_t candidate_count = 0;

    collect_candidates((const float *)outputs[0].buf,
                       (const float *)outputs[1].buf,
                       80, 80,
                       candidates, max_candidates, &candidate_count);

    collect_candidates((const float *)outputs[3].buf,
                       (const float *)outputs[4].buf,
                       40, 40,
                       candidates, max_candidates, &candidate_count);

    collect_candidates((const float *)outputs[6].buf,
                       (const float *)outputs[7].buf,
                       20, 20,
                       candidates, max_candidates, &candidate_count);

    if (candidate_count > 1) {
        qsort(candidates,
              candidate_count,
              sizeof(DetectionCandidate),
              compare_candidate_score);
    }

    uint32_t result_count =
        nms(candidates, candidate_count, results, MAX_DETECTIONS);

    for (uint32_t i = 0; i < result_count; i++) {
        DetectionCandidate *det = &results[i];
        if (det->class_id >= NUM_CLASSES) continue;

        float x1, y1, x2, y2;
        restore_bbox_to_original(det, meta, &x1, &y1, &x2, &y2);

        float width = x2 - x1;
        float height = y2 - y1;
        if (width <= 0.0f || height <= 0.0f) continue;

        if (!(*first_json_item)) fprintf(json_fp, ",\n");

        fprintf(json_fp,
                "  {\"image_id\": %lld, \"category_id\": %d, "
                "\"bbox\": [%.6f, %.6f, %.6f, %.6f], "
                "\"score\": %.9f}",
                meta->image_id,
                YOLO_TO_COCO_CATEGORY[det->class_id],
                x1, y1, width, height, det->score);

        *first_json_item = 0;
        (*written_detections)++;
    }

    free(results);
    free(candidates);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 5) {
        printf("Usage:\n"
               "  %s model.rknn raw_dir meta.txt predictions.json\n\n"
               "Example:\n"
               "  %s ../model/yolov8n_fp_230.rknn "
               "./rknn_raw500 ./rknn_raw500_meta.txt "
               "./fp16_subset500_predictions.json\n",
               argv[0], argv[0]);
        return 1;
    }

    const char *model_path = argv[1];
    const char *raw_dir = argv[2];
    const char *meta_path = argv[3];
    const char *json_path = argv[4];

    printf("========== RKNN COCO EVALUATION ==========\n");
    printf("model           = %s\n", model_path);
    printf("raw dir         = %s\n", raw_dir);
    printf("metadata        = %s\n", meta_path);
    printf("prediction json = %s\n", json_path);
    printf("conf threshold  = %.6f\n", CONF_THRESHOLD);
    printf("NMS threshold   = %.2f\n", NMS_THRESHOLD);
    printf("max detections  = %u\n\n", MAX_DETECTIONS);

    uint32_t model_size = 0;
    unsigned char *model_data = load_file(model_path, &model_size);
    if (model_data == NULL) return 1;

    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, model_data, model_size, 0, NULL);
    if (ret != RKNN_SUCC) {
        printf("rknn_init failed: %d\n", ret);
        free(model_data);
        return 1;
    }

    rknn_input_output_num io_num;
    memset(&io_num, 0, sizeof(io_num));

    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC) {
        printf("RKNN_QUERY_IN_OUT_NUM failed: %d\n", ret);
        rknn_destroy(ctx);
        free(model_data);
        return 1;
    }

    printf("RKNN input=%u output=%u\n", io_num.n_input, io_num.n_output);

    if (io_num.n_input != 1 || io_num.n_output < 9) {
        printf("unexpected model I/O count\n");
        rknn_destroy(ctx);
        free(model_data);
        return 1;
    }

    FILE *meta_fp = fopen(meta_path, "r");
    if (meta_fp == NULL) {
        perror(meta_path);
        rknn_destroy(ctx);
        free(model_data);
        return 1;
    }

    char header[1024];
    if (fgets(header, sizeof(header), meta_fp) == NULL) {
        printf("metadata header read failed\n");
        fclose(meta_fp);
        rknn_destroy(ctx);
        free(model_data);
        return 1;
    }

    FILE *json_fp = fopen(json_path, "w");
    if (json_fp == NULL) {
        perror(json_path);
        fclose(meta_fp);
        rknn_destroy(ctx);
        free(model_data);
        return 1;
    }

    unsigned char *input_data = (unsigned char *)malloc(INPUT_SIZE);
    rknn_output *outputs =
        (rknn_output *)calloc(io_num.n_output, sizeof(rknn_output));

    if (input_data == NULL || outputs == NULL) {
        perror("malloc/calloc");
        free(outputs);
        free(input_data);
        fclose(json_fp);
        fclose(meta_fp);
        rknn_destroy(ctx);
        free(model_data);
        return 1;
    }

    fprintf(json_fp, "[\n");

    uint32_t image_count = 0;
    uint32_t written_detections = 0;
    int first_json_item = 1;
    double total_start_ms = now_ms();

    ret = RKNN_SUCC;

    for (;;) {
        ImageMeta meta;
        memset(&meta, 0, sizeof(meta));

        int meta_ret = read_next_meta(meta_fp, &meta);
        if (meta_ret == 0) break;
        if (meta_ret < 0) {
            ret = -1;
            break;
        }

        char raw_path[1024];
        int path_ret = snprintf(raw_path, sizeof(raw_path),
                                "%s/%s", raw_dir, meta.raw_name);

        if (path_ret < 0 || (size_t)path_ret >= sizeof(raw_path)) {
            printf("raw path too long\n");
            ret = -1;
            break;
        }

        if (load_raw_rgb(raw_path, input_data, INPUT_SIZE) != 0) {
            ret = -1;
            break;
        }

        rknn_input input;
        memset(&input, 0, sizeof(input));
        input.index = 0;
        input.buf = input_data;
        input.size = INPUT_SIZE;
        input.type = RKNN_TENSOR_UINT8;
        input.fmt = RKNN_TENSOR_NHWC;
        input.pass_through = 0;

        ret = rknn_inputs_set(ctx, 1, &input);
        if (ret != RKNN_SUCC) {
            printf("rknn_inputs_set failed image_id=%lld ret=%d\n",
                   meta.image_id, ret);
            break;
        }

        ret = rknn_run(ctx, NULL);
        if (ret != RKNN_SUCC) {
            printf("rknn_run failed image_id=%lld ret=%d\n",
                   meta.image_id, ret);
            break;
        }

        memset(outputs, 0, io_num.n_output * sizeof(rknn_output));
        for (uint32_t i = 0; i < io_num.n_output; i++) {
            outputs[i].index = i;
            outputs[i].want_float = 1;
            outputs[i].is_prealloc = 0;
        }

        ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL);
        if (ret != RKNN_SUCC) {
            printf("rknn_outputs_get failed image_id=%lld ret=%d\n",
                   meta.image_id, ret);
            break;
        }

        int post_ret = process_outputs(outputs,
                                       &meta,
                                       json_fp,
                                       &first_json_item,
                                       &written_detections);

        int release_ret =
            rknn_outputs_release(ctx, io_num.n_output, outputs);

        if (release_ret != RKNN_SUCC) {
            printf("rknn_outputs_release failed image_id=%lld ret=%d\n",
                   meta.image_id, release_ret);
            ret = release_ret;
            break;
        }

        if (post_ret != 0) {
            ret = -1;
            break;
        }

        image_count++;

        if (image_count == 1 || image_count % 10 == 0) {
            double elapsed_ms = now_ms() - total_start_ms;
            printf("[%u] image_id=%lld detections_written=%u elapsed=%.1f s\n",
                   image_count,
                   meta.image_id,
                   written_detections,
                   elapsed_ms / 1000.0);
            fflush(stdout);
        }
    }

    fprintf(json_fp, "\n]\n");
    fclose(json_fp);
    fclose(meta_fp);

    double total_elapsed_ms = now_ms() - total_start_ms;

    printf("\n========== FINISHED ==========\n");
    printf("images processed   = %u\n", image_count);
    printf("detections written = %u\n", written_detections);
    printf("elapsed            = %.3f s\n", total_elapsed_ms / 1000.0);

    if (image_count > 0) {
        printf("avg wall/image     = %.3f ms\n",
               total_elapsed_ms / (double)image_count);
    }

    printf("prediction JSON    = %s\n", json_path);

    free(outputs);
    free(input_data);
    rknn_destroy(ctx);
    free(model_data);

    if (ret != RKNN_SUCC) {
        printf("evaluation stopped due to error: %d\n", ret);
        return 1;
    }

    return 0;
}
