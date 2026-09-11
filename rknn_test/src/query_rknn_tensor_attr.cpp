#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rknn_api.h"


static void print_dims(
    const rknn_tensor_attr *attr)
{
    printf("dims              = [");

    for (uint32_t i = 0;
         i < attr->n_dims;
         i++)
    {
        printf("%u", attr->dims[i]);

        if (i + 1 < attr->n_dims)
        {
            printf(", ");
        }
    }

    printf("]\n");
}


static void print_tensor_attr(
    const char *title,
    const rknn_tensor_attr *attr)
{
    printf(
        "\n========== %s ==========\n",
        title
    );

    printf("index             = %u\n", attr->index);
    printf("name              = %s\n", attr->name);
    printf("n_dims            = %u\n", attr->n_dims);

    print_dims(attr);

    printf("n_elems           = %u\n", attr->n_elems);
    printf("size              = %u bytes\n", attr->size);
    printf("size_with_stride  = %u bytes\n", attr->size_with_stride);
    printf("w_stride          = %u\n", attr->w_stride);
    printf("fmt               = %s\n", get_format_string(attr->fmt));
    printf("type              = %s\n", get_type_string(attr->type));
    printf("qnt_type          = %s\n", get_qnt_type_string(attr->qnt_type));
    printf("zp                = %d\n", attr->zp);
    printf("scale             = %.9f\n", attr->scale);
    printf("pass_through      = %u\n", attr->pass_through);
}


static int query_and_print(
    rknn_context ctx,
    rknn_query_cmd cmd,
    uint32_t index,
    const char *title)
{
    rknn_tensor_attr attr;

    memset(
        &attr,
        0,
        sizeof(attr)
    );

    attr.index =
        index;


    int ret =
        rknn_query(
            ctx,
            cmd,
            &attr,
            sizeof(attr)
        );


    if (ret != RKNN_SUCC)
    {
        printf(
            "\n========== %s ==========\n",
            title
        );

        printf(
            "rknn_query failed: %d\n",
            ret
        );

        return ret;
    }


    print_tensor_attr(
        title,
        &attr
    );


    return RKNN_SUCC;
}


int main(
    int argc,
    char *argv[])
{
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
     * size = 0 时，model 参数作为 RKNN 文件路径。
     * 这个程序只查询属性，不执行推理。
     */
    rknn_context ctx =
        0;


    int ret =
        rknn_init(
            &ctx,
            (void *)argv[1],
            0,
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


    printf(
        "rknn_init success\n"
    );


    rknn_sdk_version sdk_version;

    memset(
        &sdk_version,
        0,
        sizeof(sdk_version)
    );


    ret =
        rknn_query(
            ctx,
            RKNN_QUERY_SDK_VERSION,
            &sdk_version,
            sizeof(sdk_version)
        );


    if (ret == RKNN_SUCC)
    {
        printf(
            "API version    = %s\n",
            sdk_version.api_version
        );

        printf(
            "Driver version = %s\n",
            sdk_version.drv_version
        );
    }


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

        return -1;
    }


    printf(
        "\ninput count  = %u\n",
        io_num.n_input
    );

    printf(
        "output count = %u\n",
        io_num.n_output
    );


    for (uint32_t i = 0;
         i < io_num.n_input;
         i++)
    {
        char title[128];

        snprintf(
            title,
            sizeof(title),
            "NORMAL INPUT [%u]",
            i
        );

        query_and_print(
            ctx,
            RKNN_QUERY_INPUT_ATTR,
            i,
            title
        );
    }


    for (uint32_t i = 0;
         i < io_num.n_input;
         i++)
    {
        char title[128];

        snprintf(
            title,
            sizeof(title),
            "NATIVE INPUT [%u]",
            i
        );

        query_and_print(
            ctx,
            RKNN_QUERY_NATIVE_INPUT_ATTR,
            i,
            title
        );
    }


    for (uint32_t i = 0;
         i < io_num.n_output;
         i++)
    {
        char title[128];

        snprintf(
            title,
            sizeof(title),
            "NORMAL OUTPUT [%u]",
            i
        );

        query_and_print(
            ctx,
            RKNN_QUERY_OUTPUT_ATTR,
            i,
            title
        );
    }


    for (uint32_t i = 0;
         i < io_num.n_output;
         i++)
    {
        char title[128];

        snprintf(
            title,
            sizeof(title),
            "NATIVE OUTPUT [%u]",
            i
        );

        query_and_print(
            ctx,
            RKNN_QUERY_NATIVE_OUTPUT_ATTR,
            i,
            title
        );
    }


    rknn_destroy(ctx);

    return 0;
}
