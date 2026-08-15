#ifndef SYSTEMINFOSERVICE_H
#define SYSTEMINFOSERVICE_H

struct UptimeInfo
{
    long long_uptime_seconds;
    long minutes,hours,seconds;
};

struct MemoryInfo
{
    long total_kb;
    long available_kb;
    long used_kb;
    double usage_percent;
};
struct CpuTimesInfo
{
    unsigned long long total_time;
    unsigned long long idle_time;
};

struct LoadAverageInfo
{
    float load_avg_1_minute;
    float load_avg_5_minutes;
    float load_avg_15_minutes;
    int process_ing;
    int process_total;
};
class SystemInfoservice
{
    public:
        SystemInfoservice();
        bool read_Uptime(UptimeInfo *Info) const;
        bool read_Memory(MemoryInfo *Info) const;
        bool read_CpuTemperature(double *temperature_celsius) const;
        bool read_CpuFrequency(long *frequency_mhz) const;
        bool read_CpuTimes(CpuTimesInfo *Info) const;
        bool read_CpuUsage(double *usage_percent);
        bool read_LoadAverage(LoadAverageInfo *Info)const;


    private:
        unsigned long long m_previous_total_time;
        unsigned long long m_previous_idle_time;
        bool m_has_previous_cpu_sample;

};





#endif