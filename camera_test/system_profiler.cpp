#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>


struct ProcStat
{
    unsigned long long minflt;
    unsigned long long majflt;

    unsigned long long utime;
    unsigned long long stime;
};


struct ProcStatus
{
    unsigned long long vm_size_kb;
    unsigned long long vm_rss_kb;

    unsigned long long voluntary_ctxt;
    unsigned long long nonvoluntary_ctxt;

    int threads;
};


struct MemInfo
{
    unsigned long long mem_total_kb;
    unsigned long long mem_available_kb;
    unsigned long long buffers_kb;
    unsigned long long cached_kb;
};


static double monotonic_seconds()
{
    struct timespec ts;

    clock_gettime(
        CLOCK_MONOTONIC,
        &ts
    );

    return
        (double)ts.tv_sec
        +
        (double)ts.tv_nsec /
        1000000000.0;
}


static int read_proc_stat(
    int pid,
    struct ProcStat *stat
)
{
    char path[128];

    snprintf(
        path,
        sizeof(path),
        "/proc/%d/stat",
        pid
    );


    FILE *fp =
        fopen(
            path,
            "r"
        );


    if (fp == NULL)
    {
        return -1;
    }


    char line[8192];


    if (fgets(
            line,
            sizeof(line),
            fp) == NULL)
    {
        fclose(fp);

        return -1;
    }


    fclose(fp);


    /*
     * /proc/PID/stat 的第二项 comm：
     *
     * (process name)
     *
     * 进程名可能包含空格。
     *
     * 所以不能直接从头 strtok。
     *
     * 我们先找最后一个 ')'
     */
    char *right_paren =
        strrchr(
            line,
            ')'
        );


    if (right_paren == NULL)
    {
        return -1;
    }


    /*
     * ')' 后面：
     *
     * field 3 = state
     */
    char *data =
        right_paren +
        2;


    char *tokens[64];

    int token_count = 0;


    char *saveptr = NULL;


    char *token =
        strtok_r(
            data,
            " ",
            &saveptr
        );


    while (
        token != NULL &&
        token_count < 64
    )
    {
        tokens[token_count++] =
            token;


        token =
            strtok_r(
                NULL,
                " ",
                &saveptr
            );
    }


    /*
     * token[0] 对应 field 3
     *
     * /proc/PID/stat:
     *
     * field 10 = minflt
     * field 12 = majflt
     * field 14 = utime
     * field 15 = stime
     *
     * 所以：
     *
     * minflt -> token[7]
     * majflt -> token[9]
     * utime  -> token[11]
     * stime  -> token[12]
     */

    if (token_count < 13)
    {
        return -1;
    }


    stat->minflt =
        strtoull(
            tokens[7],
            NULL,
            10
        );


    stat->majflt =
        strtoull(
            tokens[9],
            NULL,
            10
        );


    stat->utime =
        strtoull(
            tokens[11],
            NULL,
            10
        );


    stat->stime =
        strtoull(
            tokens[12],
            NULL,
            10
        );


    return 0;
}


static int read_proc_status(
    int pid,
    struct ProcStatus *status
)
{
    char path[128];

    snprintf(
        path,
        sizeof(path),
        "/proc/%d/status",
        pid
    );


    FILE *fp =
        fopen(
            path,
            "r"
        );


    if (fp == NULL)
    {
        return -1;
    }


    memset(
        status,
        0,
        sizeof(*status)
    );


    char line[512];


    while (fgets(
            line,
            sizeof(line),
            fp) != NULL)
    {
        if (strncmp(
                line,
                "VmSize:",
                7) == 0)
        {
            sscanf(
                line +
                7,
                "%llu",
                &status->vm_size_kb
            );
        }


        else if (strncmp(
                     line,
                     "VmRSS:",
                     6) == 0)
        {
            sscanf(
                line +
                6,
                "%llu",
                &status->vm_rss_kb
            );
        }


        else if (strncmp(
                     line,
                     "Threads:",
                     8) == 0)
        {
            sscanf(
                line +
                8,
                "%d",
                &status->threads
            );
        }


        else if (strncmp(
                     line,
                     "voluntary_ctxt_switches:",
                     24) == 0)
        {
            sscanf(
                line +
                24,
                "%llu",
                &status->voluntary_ctxt
            );
        }


        else if (strncmp(
                     line,
                     "nonvoluntary_ctxt_switches:",
                     27) == 0)
        {
            sscanf(
                line +
                27,
                "%llu",
                &status->nonvoluntary_ctxt
            );
        }
    }


    fclose(fp);


    return 0;
}


static int read_meminfo(
    struct MemInfo *info
)
{
    FILE *fp =
        fopen(
            "/proc/meminfo",
            "r"
        );


    if (fp == NULL)
    {
        return -1;
    }


    memset(
        info,
        0,
        sizeof(*info)
    );


    char line[512];


    while (fgets(
            line,
            sizeof(line),
            fp) != NULL)
    {
        if (strncmp(
                line,
                "MemTotal:",
                9) == 0)
        {
            sscanf(
                line +
                9,
                "%llu",
                &info->mem_total_kb
            );
        }


        else if (strncmp(
                     line,
                     "MemAvailable:",
                     13) == 0)
        {
            sscanf(
                line +
                13,
                "%llu",
                &info->mem_available_kb
            );
        }


        else if (strncmp(
                     line,
                     "Buffers:",
                     8) == 0)
        {
            sscanf(
                line +
                8,
                "%llu",
                &info->buffers_kb
            );
        }


        else if (strncmp(
                     line,
                     "Cached:",
                     7) == 0)
        {
            sscanf(
                line +
                7,
                "%llu",
                &info->cached_kb
            );
        }
    }


    fclose(fp);


    return 0;
}


static void sleep_seconds(
    double seconds
)
{
    struct timespec req;


    req.tv_sec =
        (time_t)seconds;


    req.tv_nsec =
        (long)(
            (seconds -
             req.tv_sec)
            *
            1000000000.0
        );


    while (nanosleep(
               &req,
               &req) < 0)
    {
        if (errno != EINTR)
        {
            break;
        }
    }
}


int main(
    int argc,
    char *argv[]
)
{
    /*
     * Usage:
     *
     * ./system_profiler PID [interval] [count]
     *
     * Example:
     *
     * ./system_profiler 3421 1 30
     */

    if (argc < 2)
    {
        printf(
            "Usage:\n"
        );


        printf(
            "  %s PID [interval_seconds] [sample_count]\n",
            argv[0]
        );


        printf(
            "\nExample:\n"
        );


        printf(
            "  %s 3421 1 30\n",
            argv[0]
        );


        return 1;
    }


    int pid =
        atoi(
            argv[1]
        );


    double interval =
        1.0;


    int sample_count =
        30;


    if (argc >= 3)
    {
        interval =
            atof(
                argv[2]
            );
    }


    if (argc >= 4)
    {
        sample_count =
            atoi(
                argv[3]
            );
    }


    if (pid <= 0)
    {
        printf(
            "Invalid PID\n"
        );

        return 1;
    }


    if (interval <= 0.0)
    {
        printf(
            "Invalid interval\n"
        );

        return 1;
    }


    if (sample_count <= 0)
    {
        printf(
            "Invalid sample count\n"
        );

        return 1;
    }


    long ticks_per_second =
        sysconf(
            _SC_CLK_TCK
        );


    if (ticks_per_second <= 0)
    {
        perror(
            "sysconf(_SC_CLK_TCK)"
        );

        return 1;
    }


    struct ProcStat prev_stat;
    struct ProcStatus prev_status;


    if (read_proc_stat(
            pid,
            &prev_stat) < 0)
    {
        printf(
            "Cannot read /proc/%d/stat\n",
            pid
        );

        return 1;
    }


    if (read_proc_status(
            pid,
            &prev_status) < 0)
    {
        printf(
            "Cannot read /proc/%d/status\n",
            pid
        );

        return 1;
    }


    double prev_time =
        monotonic_seconds();


    printf(
        "============================================================\n"
    );


    printf(
        " Linux Process System Profiler\n"
    );


    printf(
        "============================================================\n"
    );


    printf(
        "PID              : %d\n",
        pid
    );


    printf(
        "Sample interval  : %.3f s\n",
        interval
    );


    printf(
        "Sample count     : %d\n",
        sample_count
    );


    printf(
        "CLK_TCK          : %ld ticks/s\n",
        ticks_per_second
    );


    printf(
        "============================================================\n"
    );


    printf(
        "\n"
    );


    printf(
        "%-4s "
        "%8s "
        "%8s "
        "%8s "
        "%9s "
        "%9s "
        "%7s "
        "%10s "
        "%10s "
        "%10s "
        "%10s "
        "%11s\n",
        "#",
        "USR%",
        "SYS%",
        "CPU%",
        "RSS_MB",
        "VSZ_MB",
        "THR",
        "VCSW/s",
        "NVCSW/s",
        "MINFLT/s",
        "MAJFLT/s",
        "MEMAVAIL"
    );


    for (int sample = 1;
         sample <= sample_count;
         sample++)
    {
        sleep_seconds(
            interval
        );


        struct ProcStat current_stat;
        struct ProcStatus current_status;
        struct MemInfo meminfo;


        if (read_proc_stat(
                pid,
                &current_stat) < 0)
        {
            printf(
                "\nProcess %d exited or stat is unavailable\n",
                pid
            );

            break;
        }


        if (read_proc_status(
                pid,
                &current_status) < 0)
        {
            printf(
                "\nProcess %d exited or status is unavailable\n",
                pid
            );

            break;
        }


        if (read_meminfo(
                &meminfo) < 0)
        {
            printf(
                "\nCannot read /proc/meminfo\n"
            );

            break;
        }


        double current_time =
            monotonic_seconds();


        double elapsed =
            current_time -
            prev_time;


        if (elapsed <= 0.0)
        {
            elapsed =
                interval;
        }


        /*
         * ========================================
         * CPU
         * ========================================
         */

        unsigned long long delta_utime =
            current_stat.utime -
            prev_stat.utime;


        unsigned long long delta_stime =
            current_stat.stime -
            prev_stat.stime;


        double usr_seconds =
            (double)delta_utime /
            ticks_per_second;


        double sys_seconds =
            (double)delta_stime /
            ticks_per_second;


        double usr_percent =
            usr_seconds /
            elapsed *
            100.0;


        double sys_percent =
            sys_seconds /
            elapsed *
            100.0;


        double cpu_percent =
            usr_percent +
            sys_percent;


        /*
         * ========================================
         * Context Switch
         * ========================================
         */

        unsigned long long delta_vcsw =
            current_status.voluntary_ctxt -
            prev_status.voluntary_ctxt;


        unsigned long long delta_nvcsw =
            current_status.nonvoluntary_ctxt -
            prev_status.nonvoluntary_ctxt;


        double vcsw_per_sec =
            delta_vcsw /
            elapsed;


        double nvcsw_per_sec =
            delta_nvcsw /
            elapsed;


        /*
         * ========================================
         * Page Fault
         * ========================================
         */

        unsigned long long delta_minflt =
            current_stat.minflt -
            prev_stat.minflt;


        unsigned long long delta_majflt =
            current_stat.majflt -
            prev_stat.majflt;


        double minflt_per_sec =
            delta_minflt /
            elapsed;


        double majflt_per_sec =
            delta_majflt /
            elapsed;


        /*
         * ========================================
         * Memory
         * ========================================
         */

        double rss_mb =
            (double)current_status.vm_rss_kb /
            1024.0;


        double vsz_mb =
            (double)current_status.vm_size_kb /
            1024.0;


        double mem_available_mb =
            (double)meminfo.mem_available_kb /
            1024.0;


        /*
         * ========================================
         * Print
         * ========================================
         */

        printf(
            "%-4d "
            "%8.2f "
            "%8.2f "
            "%8.2f "
            "%9.2f "
            "%9.2f "
            "%7d "
            "%10.1f "
            "%10.1f "
            "%10.1f "
            "%10.1f "
            "%11.1f\n",
            sample,
            usr_percent,
            sys_percent,
            cpu_percent,
            rss_mb,
            vsz_mb,
            current_status.threads,
            vcsw_per_sec,
            nvcsw_per_sec,
            minflt_per_sec,
            majflt_per_sec,
            mem_available_mb
        );


        /*
         * Current -> Previous
         */

        prev_stat =
            current_stat;


        prev_status =
            current_status;


        prev_time =
            current_time;
    }


    printf(
        "\n"
        "============================================================\n"
        " Profiling finished\n"
        "============================================================\n"
    );


    return 0;
}