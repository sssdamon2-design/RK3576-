#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <vector>

#include "rknn_api.h"

#define WARMUP_RUNS  10U
#define MEASURE_RUNS 100U

struct TimingStats {
    double input_ms = 0.0;
    double run_ms = 0.0;
    double outputs_get_ms = 0.0;
    double outputs_release_ms = 0.0;
    double total_ms = 0.0;
};

static double now_ms()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(
        clock::now().time_since_epoch()).count();
}

static unsigned char *load_file(const char *path, uint32_t *size)
{
    FILE *fp = std::fopen(path, "rb");
    if (fp == nullptr) {
        std::perror(path);
        return nullptr;
    }

    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::perror("fseek");
        std::fclose(fp);
        return nullptr;
    }

    long file_size = std::ftell(fp);
    if (file_size <= 0) {
        std::fprintf(stderr, "invalid file size: %s\n", path);
        std::fclose(fp);
        return nullptr;
    }

    std::rewind(fp);

    unsigned char *data = static_cast<unsigned char *>(
        std::malloc(static_cast<size_t>(file_size)));
    if (data == nullptr) {
        std::perror("malloc");
        std::fclose(fp);
        return nullptr;
    }

    size_t n = std::fread(data, 1, static_cast<size_t>(file_size), fp);
    std::fclose(fp);

    if (n != static_cast<size_t>(file_size)) {
        std::fprintf(stderr, "short read: %s\n", path);
        std::free(data);
        return nullptr;
    }

    *size = static_cast<uint32_t>(file_size);
    return data;
}

static int prepare_outputs(
    std::vector<rknn_output> &outputs,
    uint32_t output_count)
{
    std::memset(
        outputs.data(),
        0,
        output_count * sizeof(rknn_output));

    for (uint32_t i = 0; i < output_count; ++i) {
        outputs[i].index = i;
        outputs[i].want_float = 1;
        outputs[i].is_prealloc = 0;
    }

    return 0;
}

static int run_one_model(
    const char *label,
    const char *model_path,
    const unsigned char *input_data,
    uint32_t input_size,
    TimingStats *stats)
{
    uint32_t model_size = 0;
    unsigned char *model_data = load_file(model_path, &model_size);
    if (model_data == nullptr)
        return -1;

    rknn_context ctx = 0;
    int ret = rknn_init(
        &ctx,
        model_data,
        model_size,
        0,
        nullptr);

    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "%s: rknn_init failed: %d\n", label, ret);
        std::free(model_data);
        return -1;
    }

    rknn_input_output_num io_num;
    std::memset(&io_num, 0, sizeof(io_num));

    ret = rknn_query(
        ctx,
        RKNN_QUERY_IN_OUT_NUM,
        &io_num,
        sizeof(io_num));

    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "%s: RKNN_QUERY_IN_OUT_NUM failed: %d\n", label, ret);
        rknn_destroy(ctx);
        std::free(model_data);
        return -1;
    }

    rknn_tensor_attr input_attr;
    std::memset(&input_attr, 0, sizeof(input_attr));
    input_attr.index = 0;

    ret = rknn_query(
        ctx,
        RKNN_QUERY_INPUT_ATTR,
        &input_attr,
        sizeof(input_attr));

    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "%s: RKNN_QUERY_INPUT_ATTR failed: %d\n", label, ret);
        rknn_destroy(ctx);
        std::free(model_data);
        return -1;
    }

    std::printf("\n========== %s ==========\n", label);
    std::printf("model              = %s\n", model_path);
    std::printf("model size         = %u bytes\n", model_size);
    std::printf("model input type   = %d\n", input_attr.type);
    std::printf("model input size   = %u bytes\n", input_attr.size);
    std::printf("application input  = UINT8 NHWC, %u bytes\n", input_size);
    std::printf("pass_through       = 0\n");
    std::printf("warm-up runs       = %u\n", WARMUP_RUNS);
    std::printf("measure runs       = %u\n", MEASURE_RUNS);

    rknn_input input;
    std::memset(&input, 0, sizeof(input));
    input.index = 0;
    input.buf = const_cast<unsigned char *>(input_data);
    input.size = input_size;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    input.pass_through = 0;

    std::vector<rknn_output> outputs(io_num.n_output);

    // Warm-up: not included in statistics.
    for (uint32_t i = 0; i < WARMUP_RUNS; ++i) {
        ret = rknn_inputs_set(ctx, 1, &input);
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "%s warmup inputs_set failed at %u: %d\n", label, i, ret);
            rknn_destroy(ctx);
            std::free(model_data);
            return -1;
        }

        ret = rknn_run(ctx, nullptr);
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "%s warmup run failed at %u: %d\n", label, i, ret);
            rknn_destroy(ctx);
            std::free(model_data);
            return -1;
        }

        prepare_outputs(outputs, io_num.n_output);
        ret = rknn_outputs_get(ctx, io_num.n_output, outputs.data(), nullptr);
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "%s warmup outputs_get failed at %u: %d\n", label, i, ret);
            rknn_destroy(ctx);
            std::free(model_data);
            return -1;
        }

        ret = rknn_outputs_release(ctx, io_num.n_output, outputs.data());
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "%s warmup outputs_release failed at %u: %d\n", label, i, ret);
            rknn_destroy(ctx);
            std::free(model_data);
            return -1;
        }
    }

    std::printf("warm-up finished\n");

    for (uint32_t i = 0; i < MEASURE_RUNS; ++i) {
        double total_start = now_ms();

        double t0 = now_ms();
        ret = rknn_inputs_set(ctx, 1, &input);
        double t1 = now_ms();
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "%s inputs_set failed at %u: %d\n", label, i, ret);
            rknn_destroy(ctx);
            std::free(model_data);
            return -1;
        }

        ret = rknn_run(ctx, nullptr);
        double t2 = now_ms();
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "%s run failed at %u: %d\n", label, i, ret);
            rknn_destroy(ctx);
            std::free(model_data);
            return -1;
        }

        prepare_outputs(outputs, io_num.n_output);
        ret = rknn_outputs_get(ctx, io_num.n_output, outputs.data(), nullptr);
        double t3 = now_ms();
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "%s outputs_get failed at %u: %d\n", label, i, ret);
            rknn_destroy(ctx);
            std::free(model_data);
            return -1;
        }

        ret = rknn_outputs_release(ctx, io_num.n_output, outputs.data());
        double t4 = now_ms();
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "%s outputs_release failed at %u: %d\n", label, i, ret);
            rknn_destroy(ctx);
            std::free(model_data);
            return -1;
        }

        double total_end = now_ms();

        stats->input_ms += t1 - t0;
        stats->run_ms += t2 - t1;
        stats->outputs_get_ms += t3 - t2;
        stats->outputs_release_ms += t4 - t3;
        stats->total_ms += total_end - total_start;
    }

    const double n = static_cast<double>(MEASURE_RUNS);
    const double avg_total = stats->total_ms / n;

    std::printf("\n---------- %s RESULT ----------\n", label);
    std::printf("avg rknn_inputs_set   = %.3f ms\n", stats->input_ms / n);
    std::printf("avg rknn_run          = %.3f ms\n", stats->run_ms / n);
    std::printf("avg rknn_outputs_get  = %.3f ms\n", stats->outputs_get_ms / n);
    std::printf("avg outputs_release   = %.3f ms\n", stats->outputs_release_ms / n);
    std::printf("avg runtime total     = %.3f ms\n", avg_total);
    std::printf("equivalent FPS        = %.2f\n", avg_total > 0.0 ? 1000.0 / avg_total : 0.0);

    rknn_destroy(ctx);
    std::free(model_data);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        std::fprintf(
            stderr,
            "Usage: %s fp16_model.rknn int8_model.rknn input_640x640_rgb.raw\n",
            argv[0]);
        return 1;
    }

    uint32_t input_size = 0;
    unsigned char *input_data = load_file(argv[3], &input_size);
    if (input_data == nullptr)
        return 1;

    const uint32_t expected_input_size = 640U * 640U * 3U;
    if (input_size != expected_input_size) {
        std::fprintf(
            stderr,
            "input size mismatch: got %u bytes, expected %u bytes\n",
            input_size,
            expected_input_size);
        std::free(input_data);
        return 1;
    }

    std::printf("========== FP16 vs INT8 RKNN BENCHMARK ==========\n");
    std::printf("input size   = %u bytes (640x640 RGB UINT8)\n", input_size);
    std::printf("warm-up      = %u runs/model\n", WARMUP_RUNS);
    std::printf("measurement  = %u runs/model\n", MEASURE_RUNS);
    std::printf("output mode  = want_float=1, is_prealloc=0\n");

    TimingStats fp16_stats;
    TimingStats int8_stats;

    if (run_one_model(
            "FP16",
            argv[1],
            input_data,
            input_size,
            &fp16_stats) != 0) {
        std::free(input_data);
        return 1;
    }

    if (run_one_model(
            "INT8",
            argv[2],
            input_data,
            input_size,
            &int8_stats) != 0) {
        std::free(input_data);
        return 1;
    }

    const double n = static_cast<double>(MEASURE_RUNS);
    const double fp16_total = fp16_stats.total_ms / n;
    const double int8_total = int8_stats.total_ms / n;

    std::printf("\n========== A/B SUMMARY ==========\n");
    std::printf("FP16 avg total        = %.3f ms\n", fp16_total);
    std::printf("INT8 avg total        = %.3f ms\n", int8_total);
    std::printf("absolute saved        = %.3f ms\n", fp16_total - int8_total);
    std::printf(
        "total improvement    = %.2f %%\n",
        fp16_total > 0.0 ?
            (fp16_total - int8_total) / fp16_total * 100.0 :
            0.0);
    std::printf(
        "speedup              = %.3fx\n",
        int8_total > 0.0 ? fp16_total / int8_total : 0.0);

    std::printf("\nNote:\n");
    std::printf("- Same UINT8 NHWC input is used for both models.\n");
    std::printf("- pass_through=0, so Runtime performs model-required input conversion.\n");
    std::printf("- want_float=1, so outputs_get includes conversion/dequantization to FP32.\n");
    std::printf("- Model init time is intentionally excluded from per-frame latency.\n");

    std::free(input_data);
    return 0;
}
