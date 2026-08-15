/* 与驱动直接连接的文件，负责open...等等*/

#include "dht11service.h"
#include <fcntl.h>
#include <unistd.h>

DHT11Service::DHT11Service(QObject *parent)
    :QObject(parent)
{
}
DHT11Service::~DHT11Service() = default;


bool DHT11Service::readOnce(int *humidity,int *temperature,QString &errorMessage)
{
    if (humidity == nullptr || temperature == nullptr)
{
    return false;
}
    int fd;
    unsigned  char buf[2];
    ssize_t read_back;
    fd=open("/dev/vehicle_dht11",O_RDONLY);
    if(fd<0)
    {   
        errorMessage = "无法打开DHT11设备";
       
        return false;
    }

    read_back=read(fd,buf,2);
    if(read_back <= 0)
    {
        errorMessage = "驱动没有返回数据";
        close(fd);
        return false;
    }
    if(read_back != 2)
    {
        errorMessage = "驱动返回数据个数不对";
        close(fd);
        return false;
    }
    
    close(fd);

    *humidity=buf[0];
    *temperature=buf[1];

    return true;







}