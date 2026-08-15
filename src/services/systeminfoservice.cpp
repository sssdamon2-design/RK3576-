#include "systeminfoservice.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
/*   获得CPU运行时间参数
    内存参数
    cpu温度*/

SystemInfoservice::SystemInfoservice()
     :m_previous_total_time(0),
      m_previous_idle_time(0),
      m_has_previous_cpu_sample(false)
{
}

bool SystemInfoservice::read_Uptime(UptimeInfo *Info) const
{
int fd;
char buf[100];
ssize_t read_back;
double uptime_seconds;
long long_uptime_seconds;
long minutes,hours,seconds;
if(Info == nullptr)
{
    fprintf(stderr, "UptimeInfo pointer is null\n");
    return false;
}


fd= open("/proc/uptime",O_RDONLY);
if(fd<0)
    {   
        printf("can not open file");
       
        return false;
    }

    read_back=read(fd,buf,sizeof(buf)-1);

    if(read_back <= 0)
    {
        printf("can not read file");
        close(fd);
        return false;
    }
    buf[read_back]='\0';

    if(      ( sscanf(buf,"%lf",&uptime_seconds) )   !=1     )
    {
        printf("can not get uptime");
        close(fd);
        return false;
    }
    
    long_uptime_seconds=(long)uptime_seconds;
    hours=long_uptime_seconds /3600;
    minutes=(long_uptime_seconds % 3600) / 60;   
    seconds=long_uptime_seconds % 60; //取余

    close(fd);

    Info->long_uptime_seconds=long_uptime_seconds;
    Info->hours=hours;
    Info->minutes=minutes;
    Info->seconds=seconds;
    return true;
}


bool SystemInfoservice::read_Memory(MemoryInfo *Info) const
{
int fd;
ssize_t read_back;
char buf[4096];
char *total_position,*available_position;
long total_kb,available_kb,used_kb;
double usage_percent;

if (Info == nullptr)
{
    fprintf(stderr, "Memory pointer is null\n");
    return false;
}


fd= open("/proc/meminfo",O_RDONLY);
if(fd<0)
    {   
        printf("can not open file /proc/meminfo \n");
       
        return false;
    }

    read_back=read(fd,buf,sizeof(buf)-1);

    if(read_back <= 0)
    {
        printf("can not read file /proc/meminfo \n");
        close(fd);
        return false;
    }
    close (fd);
    buf[read_back]='\0';

    
    total_position= strstr(buf,"MemTotal:");
    available_position=strstr(buf, "MemAvailable:");

     if (total_position == nullptr)
    {
        fprintf(stderr, "MemTotal not found\n");
        return false;
    }

    if (available_position == nullptr)
    {
        fprintf(stderr, "MemAvailable not found\n");
        return false;
    }

    if(sscanf(total_position,"MemTotal: %ld kB",&total_kb)!=1)
    {
        fprintf(stderr, "Failed to parse MemTotal\n");
        return false;
    }

    if (sscanf(available_position,
               "MemAvailable: %ld kB",
               &available_kb) != 1)
    {
        fprintf(stderr, "Failed to parse MemAvailable\n");
        return false;
    }

    if (total_kb <= 0)
    {
        fprintf(stderr, "Invalid MemTotal value\n");
        return false;
    }

    if (available_kb < 0 || available_kb > total_kb)
    {
        fprintf(stderr, "Invalid MemAvailable value\n");
        return false;
    }

    used_kb=total_kb-available_kb;
    usage_percent=(double)used_kb/(double)total_kb *100.0;


    Info->total_kb = total_kb;
    Info->available_kb = available_kb;
    Info->used_kb = used_kb;
    Info->usage_percent = usage_percent;

    return true;
}

bool SystemInfoservice::read_CpuTemperature(double *temperature_celsius) const
{
    int fd;
    char buf[64];
    ssize_t read_back;
    long raw_temperature;

    if (temperature_celsius == nullptr)
    {
        fprintf(stderr,
                "temperature_celsius pointer is null\n");
        return false;
    }

    fd = open("/sys/class/thermal/thermal_zone0/temp",
              O_RDONLY);

    if (fd < 0)
    {
        perror("open cpu temperature");
        return false;
    }

    read_back = read(fd, buf, sizeof(buf) - 1);

    if (read_back < 0)
    {
        perror("read cpu temperature");
        close(fd);
        return false;
    }

    if (read_back == 0)
    {
        fprintf(stderr,
                "cpu temperature file is empty\n");
        close(fd);
        return false;
    }

    close(fd);

    buf[read_back] = '\0';

    if (sscanf(buf, "%ld", &raw_temperature) != 1)
    {
        fprintf(stderr,
                "failed to parse cpu temperature\n");
        return false;
    }

    /*
     * thermal节点的单位是毫摄氏度。
     * 例如47030表示47.030摄氏度。
     */
    *temperature_celsius =
        (double)raw_temperature / 1000.0;

    return true;
}


bool SystemInfoservice::read_CpuFrequency(long *frequency_mhz) const
{
    int fd;
    char buf[64];
    ssize_t read_back;
    long frequency_khz;

    if (frequency_mhz == nullptr)
    {
        fprintf(stderr,
                "frequency_mhz pointer is null\n");
        return false;
    }

    fd = open(
        "/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq",
        O_RDONLY);

    if (fd < 0)
    {
        perror("open cpu frequency");
        return false;
    }

    read_back = read(fd, buf, sizeof(buf) - 1);

    if (read_back < 0)
    {
        perror("read cpu frequency");
        close(fd);
        return false;
    }

    if (read_back == 0)
    {
        fprintf(stderr,
                "cpu frequency file is empty\n");
        close(fd);
        return false;
    }

    close(fd);

    /*
     * read()读取的是原始字节，
     * 需要手动添加C字符串结束符。
     */
    buf[read_back] = '\0';

    if (sscanf(buf, "%ld", &frequency_khz) != 1)
    {
        fprintf(stderr,
                "failed to parse cpu frequency\n");
        return false;
    }

    if (frequency_khz <= 0)
    {
        fprintf(stderr,
                "invalid cpu frequency value\n");
        return false;
    }

    /*
     * scaling_cur_freq的单位是kHz。
     * 1000 kHz = 1 MHz。
     */
    *frequency_mhz = frequency_khz / 1000;

    return true;
}




bool SystemInfoservice::read_CpuTimes(CpuTimesInfo *Info) const
{
    int fd;
    char buf[256];
    ssize_t read_back;

    unsigned long long user_time = 0;
    unsigned long long nice_time = 0;
    unsigned long long system_time = 0;
    unsigned long long idle_time = 0;
    unsigned long long iowait_time = 0;
    unsigned long long irq_time = 0;
    unsigned long long softirq_time = 0;
    unsigned long long steal_time = 0;

    unsigned long long total_idle;
    unsigned long long total_non_idle;
    unsigned long long total_time;

    int parse_count;

    if (Info == nullptr)
    {
        fprintf(stderr, "CpuTimesInfo pointer is null\n");
        return false;
    }

    fd = open("/proc/stat", O_RDONLY);

    if (fd < 0)
    {
        perror("open /proc/stat");
        return false;
    }

    read_back = read(fd, buf, sizeof(buf) - 1);

    if (read_back < 0)
    {
        perror("read /proc/stat");
        close(fd);
        return false;
    }

    if (read_back == 0)
    {
        fprintf(stderr, "/proc/stat is empty\n");
        close(fd);
        return false;
    }

    close(fd);

    /*
     * read()不会自动添加C字符串结束符。
     */
    buf[read_back] = '\0';

    parse_count = sscanf(
        buf,
        "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
        &user_time,
        &nice_time,
        &system_time,
        &idle_time,
        &iowait_time,
        &irq_time,
        &softirq_time,
        &steal_time
    );

    if (parse_count != 8)
    {
        fprintf(stderr, "failed to parse /proc/stat\n");
        return false;
    }

    /*
     * idle和iowait都归入空闲时间。
     */
    total_idle = idle_time + iowait_time;

    /*
     * CPU实际执行任务的累计时间。
     */
    total_non_idle =
        user_time +
        nice_time +
        system_time +
        irq_time +
        softirq_time +
        steal_time;

    total_time = total_idle + total_non_idle;

    if (total_time < total_idle)
    {
        fprintf(stderr, "invalid cpu time values\n");
        return false;
    }

    Info->total_time = total_time;
    Info->idle_time = total_idle;

    return true;
}



bool SystemInfoservice::read_CpuUsage(double *usage_percent)
{
    CpuTimesInfo current_info;

    unsigned long long total_delta;
    unsigned long long idle_delta;
    unsigned long long busy_delta;

    if (usage_percent == nullptr)
    {
        fprintf(stderr,"usage_percent pointer is null\n");
        return false;
    }

    if (read_CpuTimes(&current_info) == false)
    {
        fprintf(stderr,"failed to read cpu times\n");
        return false;
    }

    /*
     * 第一次调用时没有上一组数据，
     * 只能保存当前值，暂时无法计算使用率。
     */
    if (m_has_previous_cpu_sample == false)
    {
        m_previous_total_time = current_info.total_time;
        m_previous_idle_time = current_info.idle_time;
        m_has_previous_cpu_sample = true;

        *usage_percent = 0.0;

        return true;
    }

    /*
     * 正常情况下累计值只会增加。
     * 如果反而减小，重新建立采样基准。
     */
    if (current_info.total_time < m_previous_total_time ||
        current_info.idle_time < m_previous_idle_time)
    {
        m_previous_total_time = current_info.total_time;
        m_previous_idle_time = current_info.idle_time;

        *usage_percent = 0.0;

        return true;
    }

    total_delta =
        current_info.total_time -
        m_previous_total_time;

    idle_delta =
        current_info.idle_time -
        m_previous_idle_time;

    /*
     * 当前采样完成后，为下一次调用保存基准。
     */
    m_previous_total_time = current_info.total_time;
    m_previous_idle_time = current_info.idle_time;

    /*
     * 两次调用间隔太短时，
     * CPU累计时间可能尚未发生变化。
     */
    if (total_delta == 0)
    {
        *usage_percent = 0.0;
        return true;
    }

    if (idle_delta > total_delta)
    {
        fprintf(stderr,
                "invalid cpu time delta\n");
        return false;
    }

    busy_delta = total_delta - idle_delta;

    *usage_percent =
        (double)busy_delta /
        (double)total_delta *
        100.0;

    return true;
}




bool SystemInfoservice::read_LoadAverage(LoadAverageInfo *Info) const
{
    int fd;
    char buf[100];
    ssize_t read_back;
    int parse_count;
    float one_minute;
    float five_minutes;
    float fifteen_minutes;
    int process_ing;
    int process_total;
    if (Info == nullptr)
    {
        fprintf(stderr, "LoadAverage pointer is null\n");
        return false;
    }

    fd=open("/proc/loadavg", O_RDONLY);
    if (fd < 0)
    {
        perror("open /proc/loadavg");
        return false;
    }
    read_back= read(fd,buf,sizeof(buf)-1);
     if (read_back < 0)
    {
        perror("read /proc/loadavg");
        close(fd);
        return false;
    }

    if (read_back == 0)
    {
        fprintf(stderr, "/proc/loadavg is empty\n");
        close(fd);
        return false;
    }

    close(fd);

    buf[read_back]='\0';
    parse_count=sscanf(buf,"%f %f %f %d/%d",
        &one_minute,
        &five_minutes,
        &fifteen_minutes,
        &process_ing,
        &process_total);

    if (parse_count != 5)
    {
        fprintf(stderr, "failed to parse /proc/loadavg\n");
        return false;
    }

    Info->load_avg_1_minute = one_minute;
    Info->load_avg_5_minutes = five_minutes;
    Info->load_avg_15_minutes = fifteen_minutes;
    Info->process_ing=process_ing;
    Info->process_total=process_total;
    return true;
}