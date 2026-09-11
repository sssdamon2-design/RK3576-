#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "rknn_api.h"


#define WARMUP_RUNS   10U
#define MEASURE_RUNS 100U


typedef struct
{
    double input_ms;
    double run_ms;
    double outputs_get_ms;
    double outputs_release_ms;
    double total_ms;

} BenchStats;


/* ============================================================
 * 使用 CLOCK_MONOTONIC 做 wall-clock 计时
 * ============================================================ */
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


/* ============================================================
 * POSIX open/read/fstat 读取整个文件
 * ============================================================ */
static unsigned char *load_file(
    const char *path,
    uint32_t *file_size)
{
    int fd =
        open(
            path,
            O_RDONLY
        );

    if (fd < 0)
    {
        perror("open");
        return NULL;
    }


    struct stat st;

    if (fstat(
            fd,
            &st) < 0)
    {
        perror("fstat");

        close(fd);

        return NULL;
    }


    if (st.st_size <= 0 ||
        st.st_size > UINT32_MAX)
    {
        printf(
            "invalid file size: %lld\n",
            (long long)st.st_size
        );

        close(fd);

        return NULL;
    }


    unsigned char *data =
        (unsigned char *)malloc(
            (size_t)st.st_size
        );

    if (data == NULL)
    {
        perror("malloc");

        close(fd);

        return NULL;
    }


    size_t done =
        0;

    while (done <
           (size_t)st.st_size)
    {
        ssize_t n =
            read(
                fd,
                data + done,
                (size_t)st.st_size - done
            );

        if (n < 0)
        {
            perror("read");

            free(data);
            close(fd);

            return NULL;
        }

        if (n == 0)
        {
            break;
        }

        done +=
            (size_t)n;
    }


    close(fd);


    if (done !=
        (size_t)st.st_size)
    {
        printf(
            "short read: %zu / %lld\n",
            done,
            (long long)st.st_size
        );

        free(data);

        return NULL;
    }


    *file_size =
        (uint32_t)st.st_size;

    return data;
}


/* ============================================================
 * 每次 outputs_get 前都重新初始化 rknn_output 描述符
 * ============================================================ */
static void prepare_outputs(
    rknn_output *outputs,
    uint32_t n_output)
{
    memset(
        outputs,
        0,
        n_output *
        sizeof(rknn_output)
    );


    for (uint32_t i = 0;
         i < n_output;
         i++)
    {
        outputs[i].index =
            i;

        /*
         * 两种输入路径都保持相同：
         * 仍然让 Runtime 给 CPU 返回 FP32 输出。
         */
        outputs[i].want_float =
            1;

        outputs[i].is_prealloc =
            0;
    }
}


/* ============================================================
 * 累加统计
 * ============================================================ */
static void add_stats(
    BenchStats *sum,
    const BenchStats *one)
{
    sum->input_ms +=
        one->input_ms;

    sum->run_ms +=
        one->run_ms;

    sum->outputs_get_ms +=
        one->outputs_get_ms;

    sum->outputs_release_ms +=
        one->outputs_release_ms;

    sum->total_ms +=
        one->total_ms;
}


/* ============================================================
 * 打印平均结果
 * ============================================================ */
static void print_stats(
    const char *title,
    const BenchStats *sum)
{
    double n =
        (double)MEASURE_RUNS;


    printf(
        "\n"
        "========== %s ==========\n",
        title
    );

    printf(
        "measure runs         = %u\n",
        MEASURE_RUNS
    );

    printf(
        "avg input stage      = %.3f ms\n",
        sum->input_ms / n
    );

    printf(
        "avg rknn_run         = %.3f ms\n",
        sum->run_ms / n
    );

    printf(
        "avg outputs_get      = %.3f ms\n",
        sum->outputs_get_ms / n
    );

    printf(
        "avg outputs_release  = %.3f ms\n",
        sum->outputs_release_ms / n
    );

    printf(
        "avg input+run        = %.3f ms\n",
        (sum->input_ms +
         sum->run_ms) / n
    );

    printf(
        "avg runtime total    = %.3f ms\n",
        sum->total_ms / n
    );

    printf(
        "equivalent FPS       = %.2f\n",
        sum->total_ms > 0.0 ?
        1000.0 /
        (sum->total_ms / n) :
        0.0
    );
}


/* ============================================================
 * 创建一个新的 RKNN context
 *
 * 每种路径各用自己的 context，
 * 避免 rknn_inputs_set 和 rknn_set_io_mem 在同一个 context
 * 中互相影响。
 * ============================================================ */
static int create_context(
    const unsigned char *model_data,
    uint32_t model_size,
    rknn_context *ctx,
    rknn_input_output_num *io_num)
{
    *ctx =
        0;


    int ret =
        rknn_init(
            ctx,
            (void *)model_data,
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

        return -1;
    }


    memset(
        io_num,
        0,
        sizeof(*io_num)
    );


    ret =
        rknn_query(
            *ctx,
            RKNN_QUERY_IN_OUT_NUM,
            io_num,
            sizeof(*io_num)
        );


    if (ret != RKNN_SUCC)
    {
        printf(
            "RKNN_QUERY_IN_OUT_NUM failed: %d\n",
            ret
        );

        rknn_destroy(*ctx);

        *ctx =
            0;

        return -1;
    }


    if (io_num->n_input != 1)
    {
        printf(
            "this benchmark expects 1 input, got %u\n",
            io_num->n_input
        );

        rknn_destroy(*ctx);

        *ctx =
            0;

        return -1;
    }


    return 0;
}


/* ============================================================
 * A：普通 rknn_inputs_set 路径
 *
 * 每次：
 *
 * raw UINT8
 *    ↓
 * rknn_inputs_set()
 *    ↓
 * rknn_run()
 *    ↓
 * rknn_outputs_get()
 *
 * input stage 统计的就是 rknn_inputs_set() wall time。
 * ============================================================ */
static int benchmark_inputs_set(
    const unsigned char *model_data,
    uint32_t model_size,
    unsigned char *input_data,
    uint32_t input_size,
    BenchStats *result)
{
    rknn_context ctx;
    rknn_input_output_num io_num;


    if (create_context(
            model_data,
            model_size,
            &ctx,
            &io_num) < 0)
    {
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

        return -1;
    }


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


    /*
     * ---------------- Warm-up ----------------
     */
    for (uint32_t i = 0;
         i < WARMUP_RUNS;
         i++)
    {
        int ret =
            rknn_inputs_set(
                ctx,
                1,
                &input
            );


        if (ret != RKNN_SUCC)
        {
            printf(
                "warmup rknn_inputs_set failed: %d\n",
                ret
            );

            free(outputs);
            rknn_destroy(ctx);

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
                "warmup rknn_run failed: %d\n",
                ret
            );

            free(outputs);
            rknn_destroy(ctx);

            return -1;
        }


        prepare_outputs(
            outputs,
            io_num.n_output
        );


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
                "warmup outputs_get failed: %d\n",
                ret
            );

            free(outputs);
            rknn_destroy(ctx);

            return -1;
        }


        rknn_outputs_release(
            ctx,
            io_num.n_output,
            outputs
        );
    }


    printf(
        "NORMAL path warm-up finished: %u runs\n",
        WARMUP_RUNS
    );


    memset(
        result,
        0,
        sizeof(*result)
    );


    /*
     * ---------------- Measurement ----------------
     */
    for (uint32_t i = 0;
         i < MEASURE_RUNS;
         i++)
    {
        BenchStats one;

        memset(
            &one,
            0,
            sizeof(one)
        );


        double total_start =
            now_ms();


        double t0 =
            now_ms();


        int ret =
            rknn_inputs_set(
                ctx,
                1,
                &input
            );


        double t1 =
            now_ms();


        one.input_ms =
            t1 - t0;


        if (ret != RKNN_SUCC)
        {
            printf(
                "rknn_inputs_set failed at %u: %d\n",
                i,
                ret
            );

            free(outputs);
            rknn_destroy(ctx);

            return -1;
        }


        t0 =
            now_ms();


        ret =
            rknn_run(
                ctx,
                NULL
            );


        t1 =
            now_ms();


        one.run_ms =
            t1 - t0;


        if (ret != RKNN_SUCC)
        {
            printf(
                "rknn_run failed at %u: %d\n",
                i,
                ret
            );

            free(outputs);
            rknn_destroy(ctx);

            return -1;
        }


        prepare_outputs(
            outputs,
            io_num.n_output
        );


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


        one.outputs_get_ms =
            t1 - t0;


        if (ret != RKNN_SUCC)
        {
            printf(
                "outputs_get failed at %u: %d\n",
                i,
                ret
            );

            free(outputs);
            rknn_destroy(ctx);

            return -1;
        }


        t0 =
            now_ms();


        ret =
            rknn_outputs_release(
                ctx,
                io_num.n_output,
                outputs
            );


        t1 =
            now_ms();


        one.outputs_release_ms =
            t1 - t0;


        if (ret != RKNN_SUCC)
        {
            printf(
                "outputs_release failed at %u: %d\n",
                i,
                ret
            );

            free(outputs);
            rknn_destroy(ctx);

            return -1;
        }


        one.total_ms =
            now_ms() -
            total_start;


        add_stats(
            result,
            &one
        );
    }


    free(outputs);

    rknn_destroy(ctx);


    return 0;
}


/* ============================================================
 * B：rknn_tensor_mem + rknn_set_io_mem 路径
 *
 * 初始化一次：
 *
 * rknn_create_mem()
 *    ↓
 * rknn_set_io_mem()
 *
 * 每次：
 *
 * raw UINT8
 *    ↓
 * memcpy(input_mem->virt_addr)
 *    ↓
 * rknn_run()
 *    ↓
 * rknn_outputs_get()
 *
 * input stage 统计的是 memcpy wall time。
 *
 * 注意：
 * UINT8 -> 模型 FP16 的 Runtime preprocessing 仍然存在。
 * 本实验只改变 Buffer 提交方式。
 * ============================================================ */
static int benchmark_io_mem(
    const unsigned char *model_data,
    uint32_t model_size,
    const unsigned char *input_data,
    uint32_t input_size,
    BenchStats *result)
{
    rknn_context ctx;
    rknn_input_output_num io_num;


    if (create_context(
            model_data,
            model_size,
            &ctx,
            &io_num) < 0)
    {
        return -1;
    }


    rknn_tensor_attr input_attr;

    memset(
        &input_attr,
        0,
        sizeof(input_attr)
    );

    input_attr.index =
        0;


    int ret =
        rknn_query(
            ctx,
            RKNN_QUERY_INPUT_ATTR,
            &input_attr,
            sizeof(input_attr)
        );


    if (ret != RKNN_SUCC)
    {
        printf(
            "RKNN_QUERY_INPUT_ATTR failed: %d\n",
            ret
        );

        rknn_destroy(ctx);

        return -1;
    }


    printf(
        "IO MEM model input: "
        "type=%s fmt=%s "
        "size=%u size_with_stride=%u "
        "w_stride=%u\n",
        get_type_string(
            input_attr.type
        ),
        get_format_string(
            input_attr.fmt
        ),
        input_attr.size,
        input_attr.size_with_stride,
        input_attr.w_stride
    );


    /*
     * 描述“应用提供给 Runtime 的 Buffer”。
     *
     * 模型本身仍是 FP16。
     */
    input_attr.type =
        RKNN_TENSOR_UINT8;

    input_attr.fmt =
        RKNN_TENSOR_NHWC;

    input_attr.pass_through =
        0;


    rknn_tensor_mem *input_mem =
        rknn_create_mem(
            ctx,
            input_attr.size_with_stride
        );


    if (input_mem == NULL)
    {
        printf(
            "rknn_create_mem failed\n"
        );

        rknn_destroy(ctx);

        return -1;
    }


    if (input_size >
        input_mem->size)
    {
        printf(
            "input raw too large: "
            "%u > input_mem %u\n",
            input_size,
            input_mem->size
        );

        rknn_destroy_mem(
            ctx,
            input_mem
        );

        rknn_destroy(ctx);

        return -1;
    }


    printf(
        "IO MEM input buffer: "
        "virt_addr=%p fd=%d size=%u\n",
        input_mem->virt_addr,
        input_mem->fd,
        input_mem->size
    );


    ret =
        rknn_set_io_mem(
            ctx,
            input_mem,
            &input_attr
        );


    if (ret != RKNN_SUCC)
    {
        printf(
            "rknn_set_io_mem failed: %d\n",
            ret
        );

        rknn_destroy_mem(
            ctx,
            input_mem
        );

        rknn_destroy(ctx);

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

        rknn_destroy_mem(
            ctx,
            input_mem
        );

        rknn_destroy(ctx);

        return -1;
    }


    /*
     * ---------------- Warm-up ----------------
     */
    for (uint32_t i = 0;
         i < WARMUP_RUNS;
         i++)
    {
        memcpy(
            input_mem->virt_addr,
            input_data,
            input_size
        );


        ret =
            rknn_run(
                ctx,
                NULL
            );


        if (ret != RKNN_SUCC)
        {
            printf(
                "IO warmup rknn_run failed: %d\n",
                ret
            );

            free(outputs);

            rknn_destroy_mem(
                ctx,
                input_mem
            );

            rknn_destroy(ctx);

            return -1;
        }


        prepare_outputs(
            outputs,
            io_num.n_output
        );


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
                "IO warmup outputs_get failed: %d\n",
                ret
            );

            free(outputs);

            rknn_destroy_mem(
                ctx,
                input_mem
            );

            rknn_destroy(ctx);

            return -1;
        }


        rknn_outputs_release(
            ctx,
            io_num.n_output,
            outputs
        );
    }


    printf(
        "IO MEM path warm-up finished: %u runs\n",
        WARMUP_RUNS
    );


    memset(
        result,
        0,
        sizeof(*result)
    );


    /*
     * ---------------- Measurement ----------------
     */
    for (uint32_t i = 0;
         i < MEASURE_RUNS;
         i++)
    {
        BenchStats one;

        memset(
            &one,
            0,
            sizeof(one)
        );


        double total_start =
            now_ms();


        double t0 =
            now_ms();


        memcpy(
            input_mem->virt_addr,
            input_data,
            input_size
        );


        double t1 =
            now_ms();


        one.input_ms =
            t1 - t0;


        t0 =
            now_ms();


        ret =
            rknn_run(
                ctx,
                NULL
            );


        t1 =
            now_ms();


        one.run_ms =
            t1 - t0;


        if (ret != RKNN_SUCC)
        {
            printf(
                "IO rknn_run failed at %u: %d\n",
                i,
                ret
            );

            free(outputs);

            rknn_destroy_mem(
                ctx,
                input_mem
            );

            rknn_destroy(ctx);

            return -1;
        }


        prepare_outputs(
            outputs,
            io_num.n_output
        );


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


        one.outputs_get_ms =
            t1 - t0;


        if (ret != RKNN_SUCC)
        {
            printf(
                "IO outputs_get failed at %u: %d\n",
                i,
                ret
            );

            free(outputs);

            rknn_destroy_mem(
                ctx,
                input_mem
            );

            rknn_destroy(ctx);

            return -1;
        }


        t0 =
            now_ms();


        ret =
            rknn_outputs_release(
                ctx,
                io_num.n_output,
                outputs
            );


        t1 =
            now_ms();


        one.outputs_release_ms =
            t1 - t0;


        if (ret != RKNN_SUCC)
        {
            printf(
                "IO outputs_release failed at %u: %d\n",
                i,
                ret
            );

            free(outputs);

            rknn_destroy_mem(
                ctx,
                input_mem
            );

            rknn_destroy(ctx);

            return -1;
        }


        one.total_ms =
            now_ms() -
            total_start;


        add_stats(
            result,
            &one
        );
    }


    free(outputs);

    rknn_destroy_mem(
        ctx,
        input_mem
    );

    rknn_destroy(ctx);


    return 0;
}


/* ============================================================
 * 主程序
 *
 * Usage:
 *   ./rknn_input_ab_benchmark model.rknn input.raw
 *
 * 这个程序不测：
 *   Camera
 *   RGA
 *   Letterbox
 *   DFL/NMS
 *
 * 它只做 RKNN 输入路径 microbenchmark。
 * ============================================================ */
int main(
    int argc,
    char *argv[])
{
    if (argc != 3)
    {
        printf(
            "Usage: %s model.rknn input.raw\n",
            argv[0]
        );

        return -1;
    }


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


    const uint32_t expected_input_size =
        640U * 640U * 3U;


    if (input_size !=
        expected_input_size)
    {
        printf(
            "input raw size mismatch: "
            "got=%u expected=%u\n",
            input_size,
            expected_input_size
        );

        free(input_data);
        free(model_data);

        return -1;
    }


    printf(
        "model size = %u bytes\n",
        model_size
    );

    printf(
        "input size = %u bytes "
        "(640x640 RGB UINT8)\n",
        input_size
    );

    printf(
        "warm-up    = %u runs\n",
        WARMUP_RUNS
    );

    printf(
        "measure    = %u runs\n",
        MEASURE_RUNS
    );


    BenchStats normal_stats;
    BenchStats io_stats;


    /*
     * A：普通输入 API
     */
    if (benchmark_inputs_set(
            model_data,
            model_size,
            input_data,
            input_size,
            &normal_stats) < 0)
    {
        free(input_data);
        free(model_data);

        return -1;
    }


    /*
     * B：I/O Memory API
     */
    if (benchmark_io_mem(
            model_data,
            model_size,
            input_data,
            input_size,
            &io_stats) < 0)
    {
        free(input_data);
        free(model_data);

        return -1;
    }


    print_stats(
        "A. NORMAL rknn_inputs_set",
        &normal_stats
    );

    print_stats(
        "B. IO MEMORY rknn_set_io_mem",
        &io_stats
    );


    double normal_total =
        normal_stats.total_ms /
        (double)MEASURE_RUNS;

    double io_total =
        io_stats.total_ms /
        (double)MEASURE_RUNS;


    double saved_ms =
        normal_total -
        io_total;

    double improvement =
        normal_total > 0.0 ?
        saved_ms /
        normal_total *
        100.0 :
        0.0;


    printf(
        "\n"
        "========== A/B COMPARISON ==========\n"
    );

    printf(
        "normal avg total     = %.3f ms\n",
        normal_total
    );

    printf(
        "io mem avg total     = %.3f ms\n",
        io_total
    );

    printf(
        "saved per inference  = %.3f ms\n",
        saved_ms
    );

    printf(
        "total improvement    = %.2f %%\n",
        improvement
    );


    printf(
        "\n"
        "Important:\n"
        "A input stage = rknn_inputs_set()\n"
        "B input stage = memcpy() into input_mem\n"
        "Both still use Runtime preprocessing "
        "from UINT8 to the FP16 model input.\n"
    );


    free(input_data);
    free(model_data);


    return 0;
}
