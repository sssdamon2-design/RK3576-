#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "rknn_api.h"


static unsigned char *load_model(const char *path, uint32_t *model_size)
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


    if (st.st_size <= 0)
    {
        printf("Invalid model size: %ld\n",
               (long)st.st_size);

        close(fd);
        return NULL;
    }


    if ((uint64_t)st.st_size > UINT32_MAX)
    {
        printf("Model file is too large\n");

        close(fd);
        return NULL;
    }


    size_t size = (size_t)st.st_size;

    unsigned char *data =
        (unsigned char *)malloc(size);

    if (data == NULL)
    {
        perror("malloc");

        close(fd);
        return NULL;
    }


    size_t total_read = 0;

    while (total_read < size)
    {
        ssize_t ret = read(
            fd,
            data + total_read,
            size - total_read
        );

        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("read");

            free(data);
            close(fd);

            return NULL;
        }


        if (ret == 0)
        {
            break;
        }


        total_read += (size_t)ret;
    }


    close(fd);


    if (total_read != size)
    {
        printf(
            "Read model failed: %zu / %zu bytes\n",
            total_read,
            size
        );

        free(data);
        return NULL;
    }


    *model_size = (uint32_t)size;

    return data;
}


static void print_tensor_attr(const rknn_tensor_attr *attr)
{
    printf("index      : %u\n", attr->index);
    printf("name       : %s\n", attr->name);

    printf("dims       : ");

    for (uint32_t i = 0; i < attr->n_dims; i++)
    {
        printf("%u", attr->dims[i]);

        if (i + 1 < attr->n_dims)
        {
            printf(" x ");
        }
    }

    printf("\n");

    printf("n_elems    : %u\n",
           attr->n_elems);

    printf("size       : %u bytes\n",
           attr->size);

    printf("format     : %s\n",
           get_format_string(attr->fmt));

    printf("type       : %s\n",
           get_type_string(attr->type));

    printf("quant type : %s\n",
           get_qnt_type_string(attr->qnt_type));

    printf("zero point : %d\n",
           attr->zp);

    printf("scale      : %f\n",
           attr->scale);

    printf("w_stride   : %u\n",
           attr->w_stride);

    printf("h_stride   : %u\n",
           attr->h_stride);

    printf("size_stride: %u bytes\n",
           attr->size_with_stride);

    printf("\n");
}


int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf(
            "Usage: %s model.rknn\n",
            argv[0]
        );

        return -1;
    }


    const char *model_path = argv[1];

    uint32_t model_size = 0;


    unsigned char *model_data =
        load_model(
            model_path,
            &model_size
        );


    if (model_data == NULL)
    {
        return -1;
    }


    printf("Model loaded into CPU memory\n");

    printf(
        "Model size: %u bytes\n\n",
        model_size
    );


    rknn_context ctx = 0;


    int ret = rknn_init(
        &ctx,
        model_data,
        model_size,
        0,
        NULL
    );


    if (ret != RKNN_SUCC)
    {
        printf(
            "rknn_init failed, ret = %d\n",
            ret
        );

        free(model_data);

        return -1;
    }


    printf("rknn_init success\n");

    printf(
        "context = %llu\n\n",
        (unsigned long long)ctx
    );


    rknn_sdk_version version;

    memset(
        &version,
        0,
        sizeof(version)
    );


    ret = rknn_query(
        ctx,
        RKNN_QUERY_SDK_VERSION,
        &version,
        sizeof(version)
    );


    if (ret == RKNN_SUCC)
    {
        printf(
            "========== VERSION ==========\n"
        );

        printf(
            "RKNN API version    : %s\n",
            version.api_version
        );

        printf(
            "RKNPU driver version: %s\n\n",
            version.drv_version
        );
    }
    else
    {
        printf(
            "Query SDK version failed, ret = %d\n\n",
            ret
        );
    }


    rknn_input_output_num io_num;

    memset(
        &io_num,
        0,
        sizeof(io_num)
    );


    ret = rknn_query(
        ctx,
        RKNN_QUERY_IN_OUT_NUM,
        &io_num,
        sizeof(io_num)
    );


    if (ret != RKNN_SUCC)
    {
        printf(
            "Query input/output number failed, ret = %d\n",
            ret
        );

        rknn_destroy(ctx);
        free(model_data);

        return -1;
    }


    printf(
        "========== MODEL IO ==========\n"
    );

    printf(
        "Input number : %u\n",
        io_num.n_input
    );

    printf(
        "Output number: %u\n\n",
        io_num.n_output
    );


    printf(
        "========== INPUT TENSORS ==========\n\n"
    );


    for (uint32_t i = 0;
         i < io_num.n_input;
         i++)
    {
        rknn_tensor_attr attr;

        memset(
            &attr,
            0,
            sizeof(attr)
        );

        attr.index = i;


        ret = rknn_query(
            ctx,
            RKNN_QUERY_INPUT_ATTR,
            &attr,
            sizeof(attr)
        );


        if (ret != RKNN_SUCC)
        {
            printf(
                "Query input tensor %u failed, ret = %d\n",
                i,
                ret
            );

            continue;
        }


        print_tensor_attr(&attr);
    }


    printf(
        "========== OUTPUT TENSORS ==========\n\n"
    );


    for (uint32_t i = 0;
         i < io_num.n_output;
         i++)
    {
        rknn_tensor_attr attr;

        memset(
            &attr,
            0,
            sizeof(attr)
        );

        attr.index = i;


        ret = rknn_query(
            ctx,
            RKNN_QUERY_OUTPUT_ATTR,
            &attr,
            sizeof(attr)
        );


        if (ret != RKNN_SUCC)
        {
            printf(
                "Query output tensor %u failed, ret = %d\n",
                i,
                ret
            );

            continue;
        }


        print_tensor_attr(&attr);
    }


    rknn_destroy(ctx);

    free(model_data);


    printf(
        "RKNN context destroyed\n"
    );


    return 0;
}