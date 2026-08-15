#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
int main()
{
    int fd;
    char buf[100];
    ssize_t read_back;
    double uptime_seconds;
    long long_uptime_seconds;
    long minutes,hours,seconds;
    fd= open("/proc/uptime",O_RDONLY);
    
    if(fd<0)
    {   
        printf("can not open file");
       
        return -1;
    }

    read_back=read(fd,buf,sizeof(buf)-1);

    if(read_back <= 0)
    {
        printf("can not read file");
        close(fd);
        return -1;
    }
    buf[read_back]='\0';

    if(      ( sscanf(buf,"%lf",&uptime_seconds) )   !=1     )
    {
        printf("can not get uptime");
        close(fd);
        return -1;
    }
    
    long_uptime_seconds=(long)uptime_seconds;
    hours=long_uptime_seconds /3600;
    minutes=(long_uptime_seconds % 3600) / 60;   
    seconds=long_uptime_seconds % 60; //取余

    printf("uptime_seconds=%.2f\n",uptime_seconds);
    printf("uptime_format=%02ld:%02ld:%02ld\n",hours,minutes,seconds);
    close(fd);
    return 0;
}