#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>

#include <linux/dma-heap.h>
#include <linux/dma-buf.h>

#include <SDL2/SDL.h>


static double diff_ms(
    const struct timespec *start,
    const struct timespec *end
)
{
    return
        (end->tv_sec - start->tv_sec) * 1000.0
        +
        (end->tv_nsec - start->tv_nsec) / 1000000.0;
}


static int allocate_dma_buffer(
    int heap_fd,
    size_t size,
    struct dma_heap_allocation_data *alloc
)
{
    memset(
        alloc,
        0,
        sizeof(*alloc)
    );

    alloc->len = size;

    alloc->fd_flags =
        O_RDWR |
        O_CLOEXEC;

    alloc->heap_flags = 0;


    if (ioctl(
            heap_fd,
            DMA_HEAP_IOCTL_ALLOC,
            alloc) < 0)
    {
        perror("DMA_HEAP_IOCTL_ALLOC");

        return -1;
    }


    return 0;
}


static int dma_buf_sync(
    int dma_fd,
    __u64 flags
)
{
    struct dma_buf_sync sync;

    memset(
        &sync,
        0,
        sizeof(sync)
    );

    sync.flags = flags;


    if (ioctl(
            dma_fd,
            DMA_BUF_IOCTL_SYNC,
            &sync) < 0)
    {
        perror("DMA_BUF_IOCTL_SYNC");

        return -1;
    }


    return 0;
}


static void print_renderer_info(
    SDL_Renderer *renderer
)
{
    SDL_RendererInfo info;

    memset(
        &info,
        0,
        sizeof(info)
    );


    if (SDL_GetRendererInfo(
            renderer,
            &info) < 0)
    {
        printf(
            "SDL_GetRendererInfo failed: %s\n",
            SDL_GetError()
        );

        return;
    }


    printf(
        "Renderer name    : %s\n",
        info.name ?
        info.name :
        "unknown"
    );


    printf(
        "Renderer flags   : 0x%x\n",
        info.flags
    );


    printf(
        "  SOFTWARE       : %s\n",
        (info.flags &
         SDL_RENDERER_SOFTWARE) ?
        "yes" :
        "no"
    );


    printf(
        "  ACCELERATED    : %s\n",
        (info.flags &
         SDL_RENDERER_ACCELERATED) ?
        "yes" :
        "no"
    );


    printf(
        "  PRESENTVSYNC   : %s\n",
        (info.flags &
         SDL_RENDERER_PRESENTVSYNC) ?
        "yes" :
        "no"
    );


    printf(
        "  TARGETTEXTURE  : %s\n",
        (info.flags &
         SDL_RENDERER_TARGETTEXTURE) ?
        "yes" :
        "no"
    );
}


static int run_sdl_case(
    int heap_fd,
    int width,
    int height,
    int warmup_count,
    int test_count
)
{
    /*
     * ==========================================
     * RGB888 Buffer
     * ==========================================
     */

    size_t frame_size;

    frame_size =
        (size_t)width *
        height *
        3;


    struct dma_heap_allocation_data alloc;


    if (allocate_dma_buffer(
            heap_fd,
            frame_size,
            &alloc) < 0)
    {
        return -1;
    }


    void *rgb_addr =
        mmap(
            NULL,
            frame_size,
            PROT_READ |
            PROT_WRITE,
            MAP_SHARED,
            alloc.fd,
            0
        );


    if (rgb_addr == MAP_FAILED)
    {
        perror("mmap");

        close(
            alloc.fd
        );

        return -1;
    }


    /*
     * ==========================================
     * 初始化RGB Buffer
     *
     * CPU WRITE：
     * START
     * ↓
     * memset
     * ↓
     * END
     * ==========================================
     */

    if (dma_buf_sync(
            alloc.fd,
            DMA_BUF_SYNC_START |
            DMA_BUF_SYNC_WRITE) < 0)
    {
        munmap(
            rgb_addr,
            frame_size
        );

        close(
            alloc.fd
        );

        return -1;
    }


    memset(
        rgb_addr,
        0x80,
        frame_size
    );


    if (dma_buf_sync(
            alloc.fd,
            DMA_BUF_SYNC_END |
            DMA_BUF_SYNC_WRITE) < 0)
    {
        munmap(
            rgb_addr,
            frame_size
        );

        close(
            alloc.fd
        );

        return -1;
    }


    /*
     * ==========================================
     * SDL Window
     * ==========================================
     */

    char title[128];

    snprintf(
        title,
        sizeof(title),
        "SDL Benchmark %dx%d",
        width,
        height
    );


    SDL_Window *window =
        SDL_CreateWindow(
            title,
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            width,
            height,
            SDL_WINDOW_SHOWN
        );


    if (window == NULL)
    {
        printf(
            "SDL_CreateWindow failed: %s\n",
            SDL_GetError()
        );

        munmap(
            rgb_addr,
            frame_size
        );

        close(
            alloc.fd
        );

        return -1;
    }


    /*
     * ==========================================
     * Software Renderer
     *
     * 与当前Camera程序保持一致
     * ==========================================
     */

    SDL_Renderer *renderer =
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

        SDL_DestroyWindow(
            window
        );

        munmap(
            rgb_addr,
            frame_size
        );

        close(
            alloc.fd
        );

        return -1;
    }


    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255
    );


    /*
     * ==========================================
     * Streaming RGB24 Texture
     * ==========================================
     */

    SDL_Texture *texture =
        SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height
        );


    if (texture == NULL)
    {
        printf(
            "SDL_CreateTexture failed: %s\n",
            SDL_GetError()
        );

        SDL_DestroyRenderer(
            renderer
        );

        SDL_DestroyWindow(
            window
        );

        munmap(
            rgb_addr,
            frame_size
        );

        close(
            alloc.fd
        );

        return -1;
    }


    printf(
        "\n"
        "=================================================\n"
    );


    printf(
        "SDL Benchmark: %dx%d RGB888\n",
        width,
        height
    );


    printf(
        "Frame size       : %.3f MB\n",
        (double)frame_size /
        1000000.0
    );


    print_renderer_info(
        renderer
    );


    /*
     * ==========================================
     * Warm-up
     * ==========================================
     */

    for (int i = 0;
         i < warmup_count;
         i++)
    {
        if (dma_buf_sync(
                alloc.fd,
                DMA_BUF_SYNC_START |
                DMA_BUF_SYNC_READ) < 0)
        {
            SDL_DestroyTexture(texture);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);

            munmap(
                rgb_addr,
                frame_size
            );

            close(
                alloc.fd
            );

            return -1;
        }


        if (SDL_UpdateTexture(
                texture,
                NULL,
                rgb_addr,
                width * 3) < 0)
        {
            printf(
                "SDL_UpdateTexture failed: %s\n",
                SDL_GetError()
            );

            SDL_DestroyTexture(texture);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);

            munmap(
                rgb_addr,
                frame_size
            );

            close(
                alloc.fd
            );

            return -1;
        }


        if (dma_buf_sync(
                alloc.fd,
                DMA_BUF_SYNC_END |
                DMA_BUF_SYNC_READ) < 0)
        {
            SDL_DestroyTexture(texture);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);

            munmap(
                rgb_addr,
                frame_size
            );

            close(
                alloc.fd
            );

            return -1;
        }


        SDL_RenderClear(
            renderer
        );


        SDL_RenderCopy(
            renderer,
            texture,
            NULL,
            NULL
        );


        SDL_RenderPresent(
            renderer
        );
    }


    /*
     * ==========================================
     * Statistics
     * ==========================================
     */

    double sync_total_ms = 0.0;

    double update_total_ms = 0.0;
    double clear_total_ms = 0.0;
    double copy_total_ms = 0.0;
    double present_total_ms = 0.0;
    double total_total_ms = 0.0;


    double update_min_ms = 1000000.0;
    double update_max_ms = 0.0;

    double copy_min_ms = 1000000.0;
    double copy_max_ms = 0.0;

    double present_min_ms = 1000000.0;
    double present_max_ms = 0.0;

    double total_min_ms = 1000000.0;
    double total_max_ms = 0.0;


    int completed = 0;


    /*
     * ==========================================
     * Formal Benchmark
     * ==========================================
     */

    for (int i = 0;
         i < test_count;
         i++)
    {
        struct timespec total_t0;
        struct timespec total_t1;

        struct timespec sync_start_t0;
        struct timespec sync_start_t1;

        struct timespec update_t0;
        struct timespec update_t1;

        struct timespec sync_end_t0;
        struct timespec sync_end_t1;

        struct timespec clear_t0;
        struct timespec clear_t1;

        struct timespec copy_t0;
        struct timespec copy_t1;

        struct timespec present_t0;
        struct timespec present_t1;


        /*
         * --------------------------------------
         * Full SDL path start
         * --------------------------------------
         */

        clock_gettime(
            CLOCK_MONOTONIC,
            &total_t0
        );


        /*
         * --------------------------------------
         * DMA-BUF SYNC START
         * --------------------------------------
         */

        clock_gettime(
            CLOCK_MONOTONIC,
            &sync_start_t0
        );


        if (dma_buf_sync(
                alloc.fd,
                DMA_BUF_SYNC_START |
                DMA_BUF_SYNC_READ) < 0)
        {
            break;
        }


        clock_gettime(
            CLOCK_MONOTONIC,
            &sync_start_t1
        );


        /*
         * --------------------------------------
         * SDL_UpdateTexture
         * --------------------------------------
         */

        clock_gettime(
            CLOCK_MONOTONIC,
            &update_t0
        );


        if (SDL_UpdateTexture(
                texture,
                NULL,
                rgb_addr,
                width * 3) < 0)
        {
            printf(
                "SDL_UpdateTexture failed: %s\n",
                SDL_GetError()
            );

            break;
        }


        clock_gettime(
            CLOCK_MONOTONIC,
            &update_t1
        );


        /*
         * --------------------------------------
         * DMA-BUF SYNC END
         * --------------------------------------
         */

        clock_gettime(
            CLOCK_MONOTONIC,
            &sync_end_t0
        );


        if (dma_buf_sync(
                alloc.fd,
                DMA_BUF_SYNC_END |
                DMA_BUF_SYNC_READ) < 0)
        {
            break;
        }


        clock_gettime(
            CLOCK_MONOTONIC,
            &sync_end_t1
        );


        /*
         * --------------------------------------
         * SDL_RenderClear
         * --------------------------------------
         */

        clock_gettime(
            CLOCK_MONOTONIC,
            &clear_t0
        );


        if (SDL_RenderClear(
                renderer) < 0)
        {
            printf(
                "SDL_RenderClear failed: %s\n",
                SDL_GetError()
            );

            break;
        }


        clock_gettime(
            CLOCK_MONOTONIC,
            &clear_t1
        );


        /*
         * --------------------------------------
         * SDL_RenderCopy
         * --------------------------------------
         */

        clock_gettime(
            CLOCK_MONOTONIC,
            &copy_t0
        );


        if (SDL_RenderCopy(
                renderer,
                texture,
                NULL,
                NULL) < 0)
        {
            printf(
                "SDL_RenderCopy failed: %s\n",
                SDL_GetError()
            );

            break;
        }


        clock_gettime(
            CLOCK_MONOTONIC,
            &copy_t1
        );


        /*
         * --------------------------------------
         * SDL_RenderPresent
         * --------------------------------------
         */

        clock_gettime(
            CLOCK_MONOTONIC,
            &present_t0
        );


        SDL_RenderPresent(
            renderer
        );


        clock_gettime(
            CLOCK_MONOTONIC,
            &present_t1
        );


        /*
         * --------------------------------------
         * Full SDL path end
         * --------------------------------------
         */

        clock_gettime(
            CLOCK_MONOTONIC,
            &total_t1
        );


        /*
         * ======================================
         * Calculate latency
         * ======================================
         */

        double sync_ms =
            diff_ms(
                &sync_start_t0,
                &sync_start_t1
            )
            +
            diff_ms(
                &sync_end_t0,
                &sync_end_t1
            );


        double update_ms =
            diff_ms(
                &update_t0,
                &update_t1
            );


        double clear_ms =
            diff_ms(
                &clear_t0,
                &clear_t1
            );


        double copy_ms =
            diff_ms(
                &copy_t0,
                &copy_t1
            );


        double present_ms =
            diff_ms(
                &present_t0,
                &present_t1
            );


        double total_ms =
            diff_ms(
                &total_t0,
                &total_t1
            );


        sync_total_ms +=
            sync_ms;


        update_total_ms +=
            update_ms;


        clear_total_ms +=
            clear_ms;


        copy_total_ms +=
            copy_ms;


        present_total_ms +=
            present_ms;


        total_total_ms +=
            total_ms;


        /*
         * Update min/max
         */

        if (update_ms < update_min_ms)
        {
            update_min_ms =
                update_ms;
        }


        if (update_ms > update_max_ms)
        {
            update_max_ms =
                update_ms;
        }


        /*
         * Copy min/max
         */

        if (copy_ms < copy_min_ms)
        {
            copy_min_ms =
                copy_ms;
        }


        if (copy_ms > copy_max_ms)
        {
            copy_max_ms =
                copy_ms;
        }


        /*
         * Present min/max
         */

        if (present_ms < present_min_ms)
        {
            present_min_ms =
                present_ms;
        }


        if (present_ms > present_max_ms)
        {
            present_max_ms =
                present_ms;
        }


        /*
         * Total min/max
         */

        if (total_ms < total_min_ms)
        {
            total_min_ms =
                total_ms;
        }


        if (total_ms > total_max_ms)
        {
            total_max_ms =
                total_ms;
        }


        completed++;


        /*
         * Event处理不计入benchmark时间
         */

        SDL_Event event;


        while (SDL_PollEvent(
                &event))
        {
            if (event.type ==
                SDL_QUIT)
            {
                i =
                    test_count;

                break;
            }
        }
    }


    /*
     * ==========================================
     * Result
     * ==========================================
     */

    if (completed > 0)
    {
        double avg_sync_ms =
            sync_total_ms /
            completed;


        double avg_update_ms =
            update_total_ms /
            completed;


        double avg_clear_ms =
            clear_total_ms /
            completed;


        double avg_copy_ms =
            copy_total_ms /
            completed;


        double avg_present_ms =
            present_total_ms /
            completed;


        double avg_total_ms =
            total_total_ms /
            completed;


        /*
         * SDL_UpdateTexture处理的逻辑数据量
         */

        double upload_rate_gbps =
            ((double)frame_size /
             1000000.0)
            /
            avg_update_ms;


        printf(
            "\n"
            "---------------- RESULT ----------------\n"
        );


        printf(
            "Resolution          : %dx%d\n",
            width,
            height
        );


        printf(
            "Frame size          : %.3f MB\n",
            (double)frame_size /
            1000000.0
        );


        printf(
            "Frames tested       : %d\n",
            completed
        );


        printf(
            "\n"
        );


        printf(
            "DMA-BUF sync avg    : %.3f ms\n",
            avg_sync_ms
        );


        printf(
            "UpdateTexture avg   : %.3f ms\n",
            avg_update_ms
        );


        printf(
            "UpdateTexture min   : %.3f ms\n",
            update_min_ms
        );


        printf(
            "UpdateTexture max   : %.3f ms\n",
            update_max_ms
        );


        printf(
            "Upload payload rate : %.3f GB/s\n",
            upload_rate_gbps
        );


        printf(
            "\n"
        );


        printf(
            "RenderClear avg     : %.3f ms\n",
            avg_clear_ms
        );


        printf(
            "RenderCopy avg      : %.3f ms\n",
            avg_copy_ms
        );


        printf(
            "RenderCopy min      : %.3f ms\n",
            copy_min_ms
        );


        printf(
            "RenderCopy max      : %.3f ms\n",
            copy_max_ms
        );


        printf(
            "\n"
        );


        printf(
            "RenderPresent avg   : %.3f ms\n",
            avg_present_ms
        );


        printf(
            "RenderPresent min   : %.3f ms\n",
            present_min_ms
        );


        printf(
            "RenderPresent max   : %.3f ms\n",
            present_max_ms
        );


        printf(
            "\n"
        );


        printf(
            "SDL total avg       : %.3f ms\n",
            avg_total_ms
        );


        printf(
            "SDL total min       : %.3f ms\n",
            total_min_ms
        );


        printf(
            "SDL total max       : %.3f ms\n",
            total_max_ms
        );


        printf(
            "SDL-only throughput : %.2f FPS\n",
            1000.0 /
            avg_total_ms
        );


        printf(
            "----------------------------------------\n"
        );
    }


    /*
     * ==========================================
     * Cleanup
     * ==========================================
     */

    SDL_DestroyTexture(
        texture
    );


    SDL_DestroyRenderer(
        renderer
    );


    SDL_DestroyWindow(
        window
    );


    munmap(
        rgb_addr,
        frame_size
    );


    close(
        alloc.fd
    );


    return 0;
}


int main()
{
    /*
     * ==========================================
     * SDL
     * ==========================================
     */

    if (SDL_Init(
            SDL_INIT_VIDEO) < 0)
    {
        printf(
            "SDL_Init failed: %s\n",
            SDL_GetError()
        );

        return 1;
    }


    printf(
        "===============================================\n"
        " RK3576 SDL Display Microbenchmark\n"
        "===============================================\n"
    );


    printf(
        "Video driver     : %s\n",
        SDL_GetCurrentVideoDriver() ?
        SDL_GetCurrentVideoDriver() :
        "unknown"
    );


    /*
     * ==========================================
     * DMA-HEAP
     *
     * 使用cached system heap，
     * 与当前优化后的RGB888路径保持一致。
     * ==========================================
     */

    int heap_fd =
        open(
            "/dev/dma_heap/system",
            O_RDWR
        );


    if (heap_fd < 0)
    {
        perror("open /dev/dma_heap/system");

        SDL_Quit();

        return 1;
    }


    /*
     * ==========================================
     * Benchmark Parameters
     * ==========================================
     */

    const int warmup_count = 30;
    const int test_count = 300;


    printf(
        "Warm-up          : %d frames\n",
        warmup_count
    );


    printf(
        "Tests            : %d frames\n",
        test_count
    );


    /*
     * ==========================================
     * Case 1
     *
     * 640x360 RGB888
     * ==========================================
     */

    if (run_sdl_case(
            heap_fd,
            640,
            360,
            warmup_count,
            test_count) < 0)
    {
        close(
            heap_fd
        );

        SDL_Quit();

        return 1;
    }


    /*
     * ==========================================
     * Case 2
     *
     * 1280x720 RGB888
     * ==========================================
     */

    if (run_sdl_case(
            heap_fd,
            1280,
            720,
            warmup_count,
            test_count) < 0)
    {
        close(
            heap_fd
        );

        SDL_Quit();

        return 1;
    }


    /*
     * ==========================================
     * Case 3
     *
     * 1920x1080 RGB888
     * ==========================================
     */

    if (run_sdl_case(
            heap_fd,
            1920,
            1080,
            warmup_count,
            test_count) < 0)
    {
        close(
            heap_fd
        );

        SDL_Quit();

        return 1;
    }


    close(
        heap_fd
    );


    SDL_Quit();


    printf(
        "\nSDL benchmark finished\n"
    );


    return 0;
}