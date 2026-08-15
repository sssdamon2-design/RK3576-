#include "monitorpage.h"
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QString>

MonitorPage::MonitorPage(QWidget *parent)
    : QWidget(parent),
      m_titleLabel(nullptr),
      m_cpuUsageLabel(nullptr),
      m_cpuTemperatureLabel(nullptr),
      m_cpuFrequencyLabel(nullptr),
      m_memoryLabel(nullptr),
      m_uptimeLabel(nullptr),
      m_Load_average(nullptr),
      m_Processes(nullptr),
      m_backButton(nullptr),
      m_refreshTimer(nullptr)

#ifdef ENABLE_DHT11
    , m_dht11Timer(nullptr)
    , m_temperatureLabel(nullptr)
    , m_humidityLabel(nullptr)
    , m_dht11StatusLabel(nullptr)
    , m_dht11Service(nullptr)
#endif
{
    initializeUi();

    m_refreshTimer = new QTimer(this);

    connect(m_refreshTimer,
            &QTimer::timeout,
            this,
            &MonitorPage::refreshSystemInfo);

    refreshSystemInfo();
    m_refreshTimer->start(1000);

#ifdef ENABLE_DHT11

    /*
     * 创建DHT11业务对象。
     *
     * 原代码只把m_dht11Service初始化为nullptr，
     * 之后却直接调用readOnce()，存在空指针问题。
     */
    m_dht11Service = new DHT11Service(this);

    m_dht11Timer = new QTimer(this);
    m_dht11Timer->setInterval(2000);

    connect(m_dht11Timer,
            &QTimer::timeout,
            this,
            &MonitorPage::refreshDht11Data);

#endif
}

MonitorPage::~MonitorPage() = default;

void MonitorPage::initializeUi()
{
    setObjectName("monitorPage");

    


    m_titleLabel = new QLabel("Environment Monitor", this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setObjectName("titleLabel");

   // m_placeholderLabel =new QLabel("Sensor module will be added later", this);
   // m_placeholderLabel->setAlignment(Qt::AlignCenter);
   // m_placeholderLabel->setObjectName("placeholderLabel");

    m_backButton = new QPushButton("Back", this);
    m_backButton->setFocusPolicy(Qt::NoFocus);

    m_cpuUsageLabel =new QLabel("CPU Usage: --", this);

    m_cpuTemperatureLabel =new QLabel("CPU Temperature: --", this);

    m_cpuFrequencyLabel =new QLabel("CPU Frequency: --", this);

    m_memoryLabel =new QLabel("Memory: --", this);

    m_uptimeLabel =new QLabel("Uptime: --", this);

    m_Load_average =new QLabel("Load Average: --", this);
    m_Processes=   new QLabel("Processes: --", this);

    #ifdef ENABLE_DHT11

m_temperatureLabel =
    new QLabel("环境温度: --", this);

m_humidityLabel =
    new QLabel("环境湿度: --", this);

m_dht11StatusLabel =
    new QLabel("DHT11 状态: Waiting", this);

#endif


    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(60, 15, 60, 15);
    mainLayout->setSpacing(8);

    mainLayout->addWidget(m_titleLabel);
    mainLayout->addWidget(m_cpuUsageLabel);
    mainLayout->addWidget(m_cpuTemperatureLabel);
    mainLayout->addWidget(m_cpuFrequencyLabel);
    mainLayout->addWidget(m_memoryLabel);
    mainLayout->addWidget(m_uptimeLabel);
    mainLayout->addWidget(m_Load_average);
    mainLayout->addWidget(m_Processes);
    
    //mainLayout->addWidget(m_placeholderLabel);
    #ifdef ENABLE_DHT11

mainLayout->addWidget(m_temperatureLabel);
mainLayout->addWidget(m_humidityLabel);
mainLayout->addWidget(m_dht11StatusLabel);

#endif
    mainLayout->addStretch();
    mainLayout->addWidget(m_backButton);

    connect(m_backButton,
            &QPushButton::clicked,
            this,
            &MonitorPage::backRequested);

    setStyleSheet(
        "#monitorPage {"
        "    background-color: #202733;"
        "}"
        "#titleLabel {"
        "    color: white;"
        "    font-size: 38px;"
        "    font-weight: bold;"
        "}"
        "#placeholderLabel {"
        "    color: #b8c4d6;"
        "    font-size: 25px;"
        "}"
        "QPushButton {"
        "    min-height: 80px;"
        "    color: white;"
        "    background-color: #334155;"
        "    border: 2px solid #52637a;"
        "    border-radius: 16px;"
        "    font-size: 26px;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #ef4444;"
        "    border: 4px solid white;"
        "}"
    );
}


void MonitorPage::refreshSystemInfo()
{
    UptimeInfo uptime_info;
    MemoryInfo memory_info;
    LoadAverageInfo load_info;
    double cpu_usage;
    double cpu_temperature;
    long cpu_frequency;

    /*
     * CPU使用率
     *
     * 第一次调用只建立采样基准，因此通常显示0.0%。
     * 加入QTimer后，第二次调用开始显示实际使用率。
     */
    if (m_systemInfoService.read_CpuUsage(&cpu_usage))
    {
        m_cpuUsageLabel->setText(QString("CPU Usage: %1%").arg(cpu_usage, 0, 'f', 1));
    }
    else
    {
        m_cpuUsageLabel->setText("CPU Usage: N/A");
    }

    /*
     * CPU芯片温度
     */
    if (m_systemInfoService.read_CpuTemperature(&cpu_temperature))
    {
        m_cpuTemperatureLabel->setText(QString("CPU Temperature: %1 °C").arg(cpu_temperature, 0, 'f', 1));
    }
    else
    {
        m_cpuTemperatureLabel->setText("CPU Temperature: N/A");
    }

    /*
     * CPU当前频率
     */
    if (m_systemInfoService.read_CpuFrequency(&cpu_frequency))
    {
        m_cpuFrequencyLabel->setText(QString("CPU Frequency: %1 MHz").arg(cpu_frequency));
    }
    else
    {
        m_cpuFrequencyLabel->setText("CPU Frequency: N/A");
    }

    /*
     * 内存状态
     *
     * /proc/meminfo中的单位为kB，
     * 界面显示时除以1024转换成MB。
     */
    if (m_systemInfoService.read_Memory(&memory_info))
    {
        long used_mb =
            memory_info.used_kb / 1024;

        long total_mb =
            memory_info.total_kb / 1024;

        m_memoryLabel->setText(
            QString("Memory: %1 / %2 MB (%3%)")
                .arg(used_mb)
                .arg(total_mb)
                .arg(memory_info.usage_percent,
                     0,
                     'f',
                     1));
    }
    else
    {
        m_memoryLabel->setText("Memory: N/A");
    }

    /*
     * 系统运行时间
     */
    if (m_systemInfoService.read_Uptime(&uptime_info))
    {
        m_uptimeLabel->setText(
            QString("Uptime: %1:%2:%3")
            .arg(uptime_info.hours,
                     2,
                     10,
                     QChar('0'))
                .arg(uptime_info.minutes,
                     2,
                     10,
                     QChar('0'))
                .arg(uptime_info.seconds,
                     2,
                     10,
                     QChar('0')));
    }
    else
    {
        m_uptimeLabel->setText("Uptime: N/A");
    }

    if(m_systemInfoService.read_LoadAverage(&load_info))
    {
        m_Load_average->setText(QString("Load Average: %1 / %2 / %3")
    .arg(load_info.load_avg_1_minute,0,'f',2)
    .arg(load_info.load_avg_5_minutes,0,'f',2)
    .arg(load_info.load_avg_15_minutes,0,'f',2));
        m_Processes->setText(QString("Processes: %1 running / %2 total")
    .arg(load_info.process_ing)
    .arg(load_info.process_total));
    }
else
    {
        m_Load_average->setText("Load Average: N/A");
        m_Processes->setText("Processes: N/A");
    }




}


#ifdef ENABLE_DHT11
void MonitorPage::refreshDht11Data()
{
    int humidity=0;
    int temperature=0;
    QString errorMessage;
    bool success;
    success =m_dht11Service->readOnce(&humidity,&temperature,errorMessage);
    if(success)
    {
        m_humidityLabel->setText(QString("环境湿度: %1%").arg(humidity));
        m_temperatureLabel->setText(QString("环境温度: %1℃").arg(temperature));
        m_dht11StatusLabel->setText("DHT11 状态: Normal");
        return;
    }
    
        m_dht11StatusLabel->setText(QString("DHT11 Status: %1").arg(errorMessage));
    

}


void MonitorPage::startDht11Monitoring()
{
    if(m_dht11Timer->isActive())
    {
        return;
    }
    
    refreshDht11Data();

    m_dht11Timer->start();
}

void MonitorPage::stopDht11Monitoring()
{
    if(m_dht11Timer->isActive())
    {
       m_dht11Timer->stop();
    }
}
#endif