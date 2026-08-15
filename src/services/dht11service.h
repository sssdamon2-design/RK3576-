/* 与驱动直接连接的文件，负责open...等等*/
#ifndef DHT11SERVICE_H
#define DHT11SERVICE_H
#include <QObject>
#include <QString>
class DHT11Service : public QObject  //QObject 是基础类 没有图形化的东西，只提供信号与槽等等基础功能
{
    Q_OBJECT

public:
    explicit DHT11Service(QObject *parent=nullptr);
    ~DHT11Service()override;
    
    bool readOnce(int *humidity,int *temperature,QString &errorMessage);




};







#endif