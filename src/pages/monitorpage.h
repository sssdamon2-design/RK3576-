#ifndef MONITORPAGE_H
#define MONITORPAGE_H

#include <QWidget>
#include "services/systeminfoservice.h"
#ifdef ENABLE_DHT11
#include "services/dht11service.h"
#endif
class QLabel;
class QPushButton;
class QTimer;
class MonitorPage : public QWidget

{
    Q_OBJECT

public:
    explicit MonitorPage(QWidget *parent = nullptr);
    ~MonitorPage() override;

signals:
    void backRequested();

private slots:
    void refreshSystemInfo();
    #ifdef ENABLE_DHT11
    void refreshDht11Data();
#endif
public slots:
    #ifdef ENABLE_DHT11
    void startDht11Monitoring();
    void stopDht11Monitoring();
#endif


private:
    void initializeUi();

    QLabel *m_titleLabel;
    //QLabel *m_placeholderLabel;
    QLabel *m_cpuUsageLabel;
    QLabel *m_cpuTemperatureLabel;
    QLabel *m_cpuFrequencyLabel;
    QLabel *m_memoryLabel;
    QLabel *m_uptimeLabel;
    QLabel *m_Load_average;
    QLabel *m_Processes;
    QPushButton *m_backButton;
    QTimer *m_refreshTimer;
    SystemInfoservice m_systemInfoService;
    #ifdef ENABLE_DHT11
    QTimer *m_dht11Timer;

    QLabel *m_temperatureLabel;
    QLabel *m_humidityLabel;
    QLabel *m_dht11StatusLabel;

    DHT11Service *m_dht11Service;
#endif
    //这里是创建一个指向DHT11Service类的指针
};

#endif