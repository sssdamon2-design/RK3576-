#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rga/im2d.h>
#include <rga/rga.h>


int main()
{
    int width = 1280;
    int height = 720;

    size_t size;

    size = width * height * 3 / 2;


    /*
     * 申请真正存放NV12图像的内存
     */
    unsigned char *data;

    data = (unsigned char *)malloc(size);

    if (data == NULL)
    {
        perror("malloc");
        return 1;
    }


    memset(data, 0, size);


    /*
     * 创建RGA Buffer描述对象
     */
    rga_buffer_t buffer;

    buffer = wrapbuffer_virtualaddr(
        data,
        width,
        height,
        RK_FORMAT_YCbCr_420_SP
    );


    printf("data address = %p\n", data);

    printf("buffer.vir_addr = %p\n",
           buffer.vir_addr);

    printf("width = %d\n",
           buffer.width);

    printf("height = %d\n",
           buffer.height);

    printf("wstride = %d\n",
           buffer.wstride);

    printf("hstride = %d\n",
           buffer.hstride);

    printf("format = 0x%x\n",
           buffer.format);


    free(data);

    return 0;
}