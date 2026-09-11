#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <SDL2/SDL.h>
#include <time.h>
#include <rga/im2d.h>
#include <rga/rga.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/dma-heap.h>
#include <linux/types.h>
#include <linux/dma-buf.h>
int main()
{
    const char *device = "/dev/video11";
    int fd;

    fd = open(device, O_RDWR);

    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    printf("open %s successfully\n", device);

    struct v4l2_capability cap;

    memset(&cap, 0, sizeof(cap));

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return 1;
    }

    printf("driver=%s\n", cap.driver);
    printf("card=%s\n", cap.card);
    printf("capabilities=0x%x\n", cap.capabilities);

    if (!(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE))
    {
        printf("do not support V4L2_CAP_VIDEO_CAPTURE_MPLANE\n");
        close(fd);
        return 1;
    }

    if (!(cap.device_caps & V4L2_CAP_STREAMING))
    {
        printf("do not support V4L2_CAP_STREAMING\n");
        close(fd);
        return 1;
    }


    /*
     * Camera输出格式
     */
    struct v4l2_format fmt;

    memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = 3840;
    fmt.fmt.pix_mp.height = 2160;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        perror("VIDIOC_S_FMT");
        close(fd);
        return 1;
    }

    printf("width=%u\n", fmt.fmt.pix_mp.width);
    printf("height=%u\n", fmt.fmt.pix_mp.height);
    printf("Number of planes: %u\n", fmt.fmt.pix_mp.num_planes);

    unsigned int width;
    unsigned int height;
    unsigned int stride;

    width = fmt.fmt.pix_mp.width;
    height = fmt.fmt.pix_mp.height;
    stride = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;

    printf("width = %u\n", width);
    printf("height = %u\n", height);
    printf("stride = %u\n", stride);


    /*
     * V4L2 Buffer
     */
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof(req));

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0)
    {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return 1;
    }

    printf("we have %u buffers\n", req.count);


    struct Buffer
    {
        void *start;
        size_t length;
        int dma_fd;
    };

    struct Buffer *buffers;

    buffers =(struct Buffer *)calloc(req.count,sizeof(struct Buffer)); //calloc创建4个 buffer长度的内存  buffer[i+1]每次
    //加一个 sizeof（）的长度
    
    
    if (buffers == NULL)
    {
        perror("calloc");
        close(fd);
        return 1;
    }


    /*
     * QUERYBUF + mmap
     */
    unsigned int i;

    for (i = 0; i < req.count; i++)
{
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));

        buf.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        buf.memory =
            V4L2_MEMORY_MMAP;

        buf.index = i;

        buf.m.planes = planes;

        buf.length = 1;

        if (ioctl(
                fd,
                VIDIOC_QUERYBUF,
                &buf) < 0)
        {
            perror("VIDIOC_QUERYBUF");
            return 1;
        }

        buffers[i].length =planes[0].length;

        buffers[i].start =mmap(
                NULL,
                planes[0].length,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                fd,
                planes[0].m.mem_offset
            );

        if (buffers[i].start == MAP_FAILED)
        {
            perror("mmap");
            return 1;
        }

        printf(
            "Buffer %u: address=%p length=%zu\n",
            i,
            buffers[i].start,
            buffers[i].length
        );
        buffers[i].dma_fd=-1;
        struct v4l2_exportbuffer expbuf;
        memset(&expbuf,0,sizeof(expbuf));
        expbuf.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        expbuf.index=i;
        expbuf.plane=0;
        expbuf.flags = O_CLOEXEC;

        if(ioctl(fd,VIDIOC_EXPBUF,&expbuf))
        {
            perror("expbuf");
            return -1;
        }
    
        buffers[i].dma_fd=expbuf.fd;
        printf("buffers %d 的 dma_buf fd= %d\n",i,buffers[i].dma_fd);
    



}


        
    
    
    /*
     * RGA resize输出：
     * 1280x720 NV12
     */
    int dst_width = 1920;
    int dst_height = 1080;
    int dst_stride = 1920;


    int dst_dma_fd;

    
        dst_dma_fd=open("/dev/dma_heap/system-uncached",O_RDWR);
    if (dst_dma_fd<0)
        {
        perror("dst_dma_fd");
        return -1;
    }
    

    size_t dst_size;

    dst_size =
        dst_width *
        dst_height *
        3 / 2;

    

    struct dma_heap_allocation_data  dst_dma;
    memset(&dst_dma,0,sizeof(dst_dma));
    dst_dma.len=dst_size;
    dst_dma.fd_flags=O_RDWR|O_CLOEXEC;
    dst_dma.heap_flags  =0;


    if(ioctl(dst_dma_fd,DMA_HEAP_IOCTL_ALLOC,&dst_dma)<0)
    {
        perror("ioctl DMA_HEAP");
        return -1;
    }
    printf("dst_fd= %d\n",dst_dma.fd);
    
    //dst_data =(unsigned char *)malloc(dst_size);

   // if (dst_data == NULL)
   // {
       // perror("malloc dst_data");
       // return 1;
  //  }


    /*
     * RGB888输出Buffer
     */

int dstRGB888_dma_fd;
     dstRGB888_dma_fd=open("/dev/dma_heap/system",O_RDWR);
    if (dstRGB888_dma_fd<0)
        {
        perror("dstRGB888_dma_fd");
        return -1;
    }
    size_t dst_RGB888_size;

    dst_RGB888_size =
        dst_width *
        dst_height *
        3;

//unsigned char *dst_data_RGB888;

    //dst_data_RGB888 =(unsigned char *)malloc(dst_RGB888_size);

    //if (dst_data_RGB888 == NULL)
    //{
     //   perror("malloc dst_data_RGB888");
        
     //   return -1;
   // }
    
struct dma_heap_allocation_data dst_RGB888_data;
memset(
    &dst_RGB888_data,
    0,
    sizeof(dst_RGB888_data)
);
dst_RGB888_data.len=dst_RGB888_size;
dst_RGB888_data.fd_flags=O_CLOEXEC |O_RDWR;
dst_RGB888_data.heap_flags=0;

if(ioctl(dstRGB888_dma_fd,DMA_HEAP_IOCTL_ALLOC,&dst_RGB888_data)<0)
    {
        perror("dstRGB888_dma_fd ioctl DMA_HEAP");
        return -1;
    }
    printf("dst_fd= %d\n",dst_RGB888_data.fd);



    /*
     * RGA RGB888 Buffer描述
     */
    rga_buffer_t dst_RGB888;

    dst_RGB888 =
        wrapbuffer_fd(
            dst_RGB888_data.fd,
            dst_width,
            dst_height,
            RK_FORMAT_RGB_888
        );



    /*
     * RGA NV12 resize目标Buffer
     */
    rga_buffer_t dst;

    dst =
        wrapbuffer_fd(
            dst_dma.fd,
            dst_width,
            dst_height,
            RK_FORMAT_YCbCr_420_SP
        );


    /*
     * 所有Camera Buffer入队
     */
    for (i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));

        buf.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        buf.memory =
            V4L2_MEMORY_MMAP;

        buf.index = i;

        buf.m.planes = planes;

        buf.length = 1;

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0)
        {
            perror("VIDIOC_QBUF");
            return 1;
        }

        printf("入队 %u\n", i);
    }


    /*
     * SDL初始化
     */
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf(
            "SDL_Init failed: %s\n",
            SDL_GetError()
        );

        return 1;
    }

    SDL_Window *window;

    window =
        SDL_CreateWindow(
            "RK3576 RGB888",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            dst_width,
            dst_height,
            SDL_WINDOW_SHOWN
        );

    if (window == NULL)
    {
        printf(
            "SDL_CreateWindow failed: %s\n",
            SDL_GetError()
        );

        SDL_Quit();
        return 1;
    }


    SDL_Renderer *renderer;

    renderer =
        SDL_CreateRenderer(
            window,
            -1,
            SDL_RENDERER_SOFTWARE
        );

    if (renderer == NULL)
    {
        printf(
            "SDL_CreateRenderer failed: %s\n",
            SDL_GetError()
        );

        SDL_DestroyWindow(window);
        SDL_Quit();

        return 1;
    }


    SDL_Texture *texture;

    texture =
        SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            dst_width,
            dst_height
        );

    if (texture == NULL)
    {
        printf(
            "SDL_CreateTexture failed: %s\n",
            SDL_GetError()
        );

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();

        return 1;
    }


    /*
     * STREAMON
     */
    enum v4l2_buf_type type;

    type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (ioctl(
            fd,
            VIDIOC_STREAMON,
            &type) < 0)
    {
        perror("VIDIOC_STREAMON");
        return 1;
    }

    printf("Streaming started\n");


    /*
     * =====================================
     * 整个Pipeline FPS统计
     * =====================================
     */
    struct timespec start_time;
    struct timespec current_time;

    unsigned int fps_frame_count = 0;

    clock_gettime(
        CLOCK_MONOTONIC,
        &start_time
    );


    /*
     * =====================================
     * DQBUF性能统计
     * =====================================
     */
    double dq_total_ms = 0.0;

    unsigned int dq_count = 0;


    /*
     * =====================================
     * RGA性能统计
     *
     * resize：
     * 4K NV12 → 720p NV12
     *
     * cvtcolor：
     * 720p NV12 → 720p RGB888
     * =====================================
     */
    double resize_total_ms = 0.0;
    double color_total_ms = 0.0;
    double rga_total_ms = 0.0;

    unsigned int rga_count = 0;


    /*
     * =====================================
     * SDL性能统计
     * =====================================
     */
    double update_total_ms = 0.0;
    double render_total_ms = 0.0;
    double sdl_total_ms = 0.0;

    unsigned int sdl_count = 0;

    unsigned char* dst_data_RGB888;

    dst_data_RGB888=(unsigned char*)mmap(NULL,dst_RGB888_size,PROT_READ | PROT_WRITE,MAP_SHARED,dst_RGB888_data.fd,0); 
    if (dst_data_RGB888 == MAP_FAILED)
{
    perror("mmap RGB888");
    return 1;
}
    /*
     * 处理300帧
     */
    int number;


    struct dma_buf_sync  sync;
    memset(&sync,0,sizeof(sync));

    double process_total_ms = 0.0;
    unsigned int process_count = 0;


    for (number = 0; number < 300; number++)
    {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));

        buf.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        buf.memory =
            V4L2_MEMORY_MMAP;

        buf.m.planes = planes;

        buf.length = 1;


        /*
         * =====================================
         * DQBUF计时
         *
         * dq_t0
         *   ↓
         * VIDIOC_DQBUF
         *   ↓
         * dq_t1
         * =====================================
         */
        struct timespec dq_t0;
        struct timespec dq_t1;
clock_gettime(
            CLOCK_MONOTONIC,
            &dq_t0 );
        

        if (ioctl(
                fd,
                VIDIOC_DQBUF,
                &buf) < 0)
        {
            perror("VIDIOC_DQBUF");
            break;
        }


     
 clock_gettime(
            CLOCK_MONOTONIC,
            &dq_t1
        );
       
        struct timespec process_t0;
    struct timespec process_t1;

clock_gettime(
    CLOCK_MONOTONIC,
    &process_t0
);

       


        double dq_time_ms;

        dq_time_ms =
            (dq_t1.tv_sec -
             dq_t0.tv_sec) * 1000.0
            +
            (dq_t1.tv_nsec -
             dq_t0.tv_nsec) / 1000000.0;


        dq_total_ms +=
            dq_time_ms;

        dq_count++;


        if (dq_count % 30 == 0)
        {
            double dq_avg_ms;

            dq_avg_ms =
                dq_total_ms /
                dq_count;

            printf(
                "DQBUF: avg=%.3f ms\n",
                dq_avg_ms
            );
        }


        /*
         * 当前Camera帧地址
         */
       


        /*
         * RGA输入Buffer
         */
        rga_buffer_t src;

        // 改为dma_fd
        src =wrapbuffer_fd(buffers[buf.index].dma_fd,width,height,RK_FORMAT_YCbCr_420_SP);
                
               


        /*
         * =====================================
         * RGA计时
         *
         * rga_t0
         *     ↓
         * imresize()
         *     ↓
         * rga_t1
         *     ↓
         * imcvtcolor()
         *     ↓
         * rga_t2
         *
         * resize   = t1 - t0
         * cvtcolor = t2 - t1
         * total    = t2 - t0
         * =====================================
         */

        struct timespec rga_t0;
        struct timespec rga_t1;
        struct timespec rga_t2;


        /*
         * t0
         */
        clock_gettime(
            CLOCK_MONOTONIC,
            &rga_t0
        );


        /*
         * 4K NV12
         * →
         * 720p NV12
         */
        IM_STATUS resize_status;

        resize_status =imresize(src,dst);


        /*
         * t1
         */
        clock_gettime(
            CLOCK_MONOTONIC,
            &rga_t1
        );


        if (resize_status != IM_STATUS_SUCCESS)
        {
            printf(
                "imresize failed: %s\n",
                imStrError(resize_status)
            );
       

        


            ioctl(
                fd,
                VIDIOC_QBUF,
                &buf
            );

            break;
         }


        /*
         * 720p NV12
         * →
         * 720p RGB888
         */
        IM_STATUS color_status;

        color_status =
            imcvtcolor(
                dst,
                dst_RGB888,
                RK_FORMAT_YCbCr_420_SP,
                RK_FORMAT_RGB_888
            );


        /*
         * t2
         */
        clock_gettime(
            CLOCK_MONOTONIC,
            &rga_t2
        );


        if (color_status != IM_STATUS_SUCCESS)
        {
            printf(
                "imcvtcolor failed: %s\n",
                imStrError(color_status)
            );

            ioctl(
                fd,
                VIDIOC_QBUF,
                &buf
            );

            break;
        }


        /*
         * resize耗时
         */
        double resize_time_ms;

        resize_time_ms =
            (rga_t1.tv_sec -
             rga_t0.tv_sec) * 1000.0
            +
            (rga_t1.tv_nsec -
             rga_t0.tv_nsec) / 1000000.0;


        /*
         * cvtcolor耗时
         */
        double color_time_ms;

        color_time_ms =
            (rga_t2.tv_sec -
             rga_t1.tv_sec) * 1000.0
            +
            (rga_t2.tv_nsec -
             rga_t1.tv_nsec) / 1000000.0;


        /*
         * RGA总耗时
         */
        double rga_time_ms;

        rga_time_ms =
            (rga_t2.tv_sec -
             rga_t0.tv_sec) * 1000.0
            +
            (rga_t2.tv_nsec -
             rga_t0.tv_nsec) / 1000000.0;


        resize_total_ms +=
            resize_time_ms;

        color_total_ms +=
            color_time_ms;

        rga_total_ms +=
            rga_time_ms;

        rga_count++;


        if (rga_count % 30 == 0)
        {
            double resize_avg_ms;
            double color_avg_ms;
            double rga_avg_ms;

            double rga_theoretical_fps;


            resize_avg_ms =
                resize_total_ms /
                rga_count;

            color_avg_ms =
                color_total_ms /
                rga_count;

            rga_avg_ms =
                rga_total_ms /
                rga_count;

            rga_theoretical_fps =
                1000.0 /
                rga_avg_ms;


            printf(
                "RGA: Resize=%.3f ms, CvtColor=%.3f ms, Total=%.3f ms, theoretical=%.2f FPS\n",
                resize_avg_ms,
                color_avg_ms,
                rga_avg_ms,
                rga_theoretical_fps
            );
        }


        /*
         * =====================================
         * SDL计时
         *
         * sdl_t0
         *    ↓
         * SDL_UpdateTexture
         *    ↓
         * sdl_t1
         *    ↓
         * RenderClear
         * RenderCopy
         * RenderPresent
         *    ↓
         * sdl_t2
         *
         * Update = t1 - t0
         * Render = t2 - t1
         * Total  = t2 - t0
         * =====================================
         */
        struct timespec sdl_t0;
        struct timespec sdl_t1;
        struct timespec sdl_t2;


        /*
         * t0
         */
        clock_gettime(
            CLOCK_MONOTONIC,
            &sdl_t0
        );

      

        sync.flags=DMA_BUF_SYNC_START |DMA_BUF_SYNC_READ;

        ioctl(dst_RGB888_data.fd,     DMA_BUF_IOCTL_SYNC,     &sync );



    
        /*
         * RGB888 → SDL Texture
         */
        if (SDL_UpdateTexture(texture,NULL,dst_data_RGB888,dst_width * 3) < 0)
        {
            printf(
                "SDL_UpdateTexture failed: %s\n",
                SDL_GetError()
            );

            ioctl(
                fd,
                VIDIOC_QBUF,
                &buf
            );

            break;
        }

        sync.flags =DMA_BUF_SYNC_END |DMA_BUF_SYNC_READ;

        ioctl(dst_RGB888_data.fd,DMA_BUF_IOCTL_SYNC,&sync);

        /*
         * t1
         */
        clock_gettime(
            CLOCK_MONOTONIC,
            &sdl_t1
        );


        /*
         * Render
         */
        SDL_RenderClear(renderer);

        SDL_RenderCopy(
            renderer,
            texture,
            NULL,
            NULL
        );

        SDL_RenderPresent(renderer);


        /*
         * t2
         */
        clock_gettime(
            CLOCK_MONOTONIC,
            &sdl_t2
        );

        
      


clock_gettime(
    CLOCK_MONOTONIC,
    &process_t1
);


double process_time_ms;

process_time_ms =
    (process_t1.tv_sec -
     process_t0.tv_sec) * 1000.0
    +
    (process_t1.tv_nsec -
     process_t0.tv_nsec) / 1000000.0;

process_total_ms += process_time_ms;
process_count++;

if (process_count % 30 == 0)
{
    printf(
        "Process: avg=%.3f ms\n",
        process_total_ms / process_count
    );}

        /*
         * Update耗时
         */
        double update_time_ms;

        update_time_ms =
            (sdl_t1.tv_sec -
             sdl_t0.tv_sec) * 1000.0
            +
            (sdl_t1.tv_nsec -
             sdl_t0.tv_nsec) / 1000000.0;


        /*
         * Render耗时
         */
        double render_time_ms;

        render_time_ms =
            (sdl_t2.tv_sec -
             sdl_t1.tv_sec) * 1000.0
            +
            (sdl_t2.tv_nsec -
             sdl_t1.tv_nsec) / 1000000.0;


        /*
         * SDL总耗时
         */
        double sdl_time_ms;

        sdl_time_ms =
            (sdl_t2.tv_sec -
             sdl_t0.tv_sec) * 1000.0
            +
            (sdl_t2.tv_nsec -
             sdl_t0.tv_nsec) / 1000000.0;


        update_total_ms +=
            update_time_ms;

        render_total_ms +=
            render_time_ms;

        sdl_total_ms +=
            sdl_time_ms;

        sdl_count++;


        if (sdl_count % 30 == 0)
        {
            double update_avg_ms;
            double render_avg_ms;
            double sdl_avg_ms;

            update_avg_ms =
                update_total_ms /
                sdl_count;

            render_avg_ms =
                render_total_ms /
                sdl_count;

            sdl_avg_ms =
                sdl_total_ms /
                sdl_count;


            printf(
                "SDL: Update=%.3f ms, Render=%.3f ms, Total=%.3f ms\n",
                update_avg_ms,
                render_avg_ms,
                sdl_avg_ms
            );
        }


        /*
         * =====================================
         * 整个Pipeline FPS
         * =====================================
         */
        fps_frame_count++;

        clock_gettime(
            CLOCK_MONOTONIC,
            &current_time
        );


        double elapsed_time;

        elapsed_time =
            (current_time.tv_sec -
             start_time.tv_sec)
            +
            (current_time.tv_nsec -
             start_time.tv_nsec) /
            1000000000.0;


        if (elapsed_time >= 1.0)
        {
            double fps;

            fps =
                fps_frame_count /
                elapsed_time;

            printf(
                "Pipeline FPS = %.2f\n",
                fps
            );

            fps_frame_count = 0;

            start_time =
                current_time;
        }


        /*
         * Buffer归还驱动
         */
        if (ioctl(
                fd,
                VIDIOC_QBUF,
                &buf) < 0)
        {
            perror("VIDIOC_QBUF");
            break;
        }
    }


    /*
     * STREAMOFF
     */
    if (ioctl(
            fd,
            VIDIOC_STREAMOFF,
            &type) < 0)
    {
        perror("VIDIOC_STREAMOFF");
    }

    printf("Streaming stopped\n");


    /*
     * SDL资源释放
     */
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();


    /*
     * RGA输出Buffer释放
     */
    
    


    /*
     * V4L2 mmap释放
     */
    for (i = 0; i < req.count; i++)
    {
        munmap(
            buffers[i].start,
            buffers[i].length
        );
        if (buffers[i].dma_fd >= 0)
{
    close(buffers[i].dma_fd);
    buffers[i].dma_fd = -1;
}
    }
    
    munmap(
    dst_data_RGB888,
    dst_RGB888_size
);
    free(buffers);
    close(dst_dma.fd);
   close(dst_RGB888_data.fd);
    close(fd);
close(dst_dma_fd);
close(dstRGB888_dma_fd);
    return 0;
}