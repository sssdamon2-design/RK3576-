#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "rknn_api.h"


static unsigned char *load_file(const char *path,uint32_t *file_size)    //加载文件并且返回文件大小，用来加载模型和照片文件
{
    int fd= open(path,O_RDONLY);

    if (fd < 0)
    {
        perror("open");
        return NULL;
    }

    struct stat st;         //专门储存文件信息的结构体

    if (fstat(fd, &st) < 0)  //读取文件信息存入st
    {
        perror("fstat");
        close(fd);
        return NULL;
    }

    unsigned char *data =(unsigned char *)malloc(st.st_size);  //根据文件大小创建内存并且返回对应的虚拟地址

    if (data == NULL)
    {
        perror("malloc");
        close(fd);
        return NULL;
    }

    ssize_t ret = read(fd,data,st.st_size);   //读取文件数据存到data里，读st.st_size大小的数据

    close(fd);

    if (ret != st.st_size)
    {
        printf("read file failed\n");
        free(data);
        return NULL;
    }

    *file_size = (uint32_t)st.st_size;   //借助指针返回文件大小数据

    return data;  //返回数据的地址


}

int main(int argc,char *argv[])
{
     if (argc != 3)
    {
        printf(
            "Usage: %s model.rknn input.raw\n",
            argv[0]
        );

        return -1;
    }

/*
     * 1. 读取模型
     */

    uint32_t model_size = 0;

    unsigned char *model_data =load_file(argv[1],&model_size);

    if (model_data == NULL)
        return -1;


    /*
     * 2. 读取输入图片
     */

    uint32_t input_size = 0;

    unsigned char *input_data =load_file(argv[2],&input_size);

    if (input_data == NULL)
    {
        free(model_data);
        return -1;
    }


    if (input_size != 640 * 640 * 3)
    {
        printf(
            "Invalid input size: %u\n",
            input_size
        );

        free(input_data);
        free(model_data);

        return -1;
    }


    /*
     * 3. 初始化模型
     */

    rknn_context ctx = 0;              //ctx 是 RKNN Runtime 给这个“模型实例”创建的句柄。

    int ret = rknn_init(     //用 model_data 指向的这份 RKNN 模型创建一个 RKNN Runtime Context，并把 Context 句柄写到 ctx 中
        &ctx,
        model_data,               // 模型二进制数据在哪里。
        model_size,                     //模型数据有多大
        0,                 // 表示初始化 flags。现在不启用特殊功能。
        NULL                //当前不使用额外扩展初始化参数。
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

    printf("rknn_init success\n");



    /*
     * 4. 查询输入输出数量
     */

    rknn_input_output_num io_num;       //装模型的输入输出数量的结构体

    memset(&io_num,0,sizeof(io_num));  

    ret = rknn_query(ctx,RKNN_QUERY_IN_OUT_NUM,&io_num,sizeof(io_num));      //向 RKNN Runtime 也就是ctx 查询模型信息。

    if (ret != RKNN_SUCC)
    {
        printf("rknn_query failed\n");
        return -1;
    }

    printf(
        "input = %u, output = %u\n",
        io_num.n_input,
        io_num.n_output
    );


     /*
     * 5. 设置输入
     */

    rknn_input input; //rknn_input 结构体变量 input。它不是图片本身，而是描述这份输入数据的信息
    //第几个输入  数据在哪里 数据多大 数据是什么类型 数据怎么排列 Runtime 是否需要帮我转换

    memset(&input,0,sizeof(input));

    input.index = 0;                    //    这份数据对应模型的 第 0 个输入 Tensor。
    input.buf = input_data;               //  真正的输入像素数据在 input_data 指向的这块内存里。
    input.size = input_size;              //  input.buf 指向的数据有多少字节。
    input.type = RKNN_TENSOR_UINT8;       //  我当前提供的输入 Buffer 中，每个元素是 UINT8。        
    input.fmt = RKNN_TENSOR_NHWC;         //  我的数据在内存里的排列方式是 NHWC。
    input.pass_through = 0;            // 不要直接把这块数据原样塞给 NPU，让 RKNN Runtime 根据我声明的 type 和 fmt 做必要的输入转换。
                                        //也就是UINT8 要先进行转换等等再输入到npu


    ret = rknn_inputs_set(ctx,1,&input); //这才是真正把前面准备好的输入描述提交给 RKNN Runtime。

    if (ret != RKNN_SUCC)
    {
        printf(
            "rknn_inputs_set failed: %d\n",
            ret
        );

        return -1;
    }

    printf("rknn_inputs_set success\n");


     /*
     * 6. 准备输出
     */

    rknn_output *outputs =(rknn_output *)calloc(io_num.n_output,sizeof(rknn_output)); 
    //申请io_num.n_output个数量 rknn_output大小的连续内存     再用 rknn_output 初始化为结构体，按照内存相除为io_num.n_output个结构体

    if (outputs == NULL)
    {
        perror("calloc");
        return -1;
    }


    for (uint32_t i = 0;i < io_num.n_output;i++)   //逐个打印输出tensor
    {
        outputs[i].index = i;           //这是第几个输出 Tensor。
        outputs[i].want_float = 1;  //我希望拿到 FP32 的输出，方便 CPU 后处理。
        outputs[i].is_prealloc = 0;  //我没有自己准备输出内存，请 Runtime 帮我分配。
    }
//output中的buf是输出数据的地址


    /*
     * 7. NPU 推理
     */

    ret = rknn_run(ctx,NULL);            //让当前 ctx 对应的 RKNN 模型真正执行一次推理。
 
    if (ret != RKNN_SUCC)
    {
        printf(
            "rknn_run failed: %d\n",
            ret
        );

        return -1;
    }

    printf("rknn_run success\n");




    /*
     * 8. 获取输出
     */

    ret = rknn_outputs_get(ctx,io_num.n_output,outputs,NULL);  //运行结束取结果数据
    //把这次推理产生的所有输出 Tensor 从 RKNN Runtime 取回来，并把结果填进 outputs 数组。 runtime会把outputs[i].buf outputs[i].size填好返回

    if (ret != RKNN_SUCC)
    {
        printf("rknn_outputs_get failed: %d\n",ret);

        return -1;
    }

    printf("rknn_outputs_get success\n");


    for (uint32_t i = 0;i < io_num.n_output;i++)
    {
        printf(
            "output[%u] buf = %p\n",i,outputs[i].buf          //输出 Tensor 的真正数据。
        );
    }

      /*
     * 9. 释放资源
     */

    rknn_outputs_release(ctx,io_num.n_output,outputs);       //释放Runtime 分配的输出数据 刚才你给我的那 9 个输出 Buffer 我已经用完了，你可以回收了
                                                        //释放  outputs[0].buf 等等 
    free(outputs);   //释放自己申请的outputs内存

    rknn_destroy(ctx);        //销毁之前通过 rknn_init() 创建的 RKNN Context。

    free(input_data);         //释放加载文件时用malloc申请的内存
    free(model_data);

    return 0;
}