#include "homepage.h"

#include <QGridLayout>  //网格布局，把按钮排列成行和列
#include <QLabel>       //显示文字
#include <QPushButton>  //创建按钮
#include <QVBoxLayout>  //从上到下垂直排列控件

HomePage::HomePage(QWidget *parent)
    : QWidget(parent),
      m_titleLabel(nullptr),
      m_statusLabel(nullptr),
      m_monitorButton(nullptr),
      m_musicButton(nullptr),
      m_settingsButton(nullptr),
      m_networkButton(nullptr),
      m_cameraButton(nullptr),
      m_aiButton(nullptr)           //初始化指针 全部设置空指针
{
    initializeUi();             //初始化函数
}

HomePage::~HomePage() = default;  //析构函数  自动释放

void HomePage::initializeUi()        
{
    setObjectName("homePage");  //设置名字 -ID

    m_titleLabel = new QLabel("IMX6ULL Vehicle Terminal", this);//创建一个显示“IMX6ULL Vehicle Terminal”的文字控件，让当前
                                                            //首页负责管理它(文件夹与文件)，并把控件地址保存到 m_titleLabel。
    m_titleLabel->setAlignment(Qt::AlignCenter);//表示标题文字居中显示。
    m_titleLabel->setObjectName("titleLabel"); //给标题标签命名为：titleLabel 方便后面单独设置标题样式

    m_statusLabel = new QLabel("System ready", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setObjectName("statusLabel"); //System ready 

    m_monitorButton = createMenuButton("Monitor");//创建一个文字为“Monitor”的按钮，并把按钮地址保存到 m_monitorButton。
    m_musicButton = createMenuButton("Music");
    m_settingsButton = createMenuButton("Settings");
    m_networkButton = createMenuButton("Network");
    m_cameraButton = createMenuButton("Camera");
    m_aiButton = createMenuButton("AI");  //6个按钮

    connect(m_monitorButton,
        &QPushButton::clicked,
        this,
        &HomePage::monitorRequested);
    
    connect(m_settingsButton,
        &QPushButton::clicked,
        this,
        &HomePage::settingRequested);

    connect(m_musicButton,
        &QPushButton::clicked,
        this,
        &HomePage::musicRequested);

    QGridLayout *menuLayout = new QGridLayout;/*创建一个可以按照“行和列”排列控件的网格布局。

它类似表格：

第0行第0列    第0行第1列    第0行第2列
第1行第0列    第1行第1列    第1行第2列*/
    

    menuLayout->setSpacing(20);  //表示各按钮之间保留20像素的距离。

    menuLayout->addWidget(m_monitorButton, 0, 0);
    menuLayout->addWidget(m_musicButton, 0, 1);
    menuLayout->addWidget(m_settingsButton, 0, 2);
    menuLayout->addWidget(m_networkButton, 1, 0);
    menuLayout->addWidget(m_cameraButton, 1, 1);
    menuLayout->addWidget(m_aiButton, 1, 2);
/*addWidget()后面的两个数字分别表示：
行号，列号
例如：
menuLayout->addWidget(m_monitorButton, 0, 0);
表示：
把Monitor按钮放到第0行、第0列。
最终排列为：
行0：Monitor    Music      Settings
行1：Network    Camera     AI
C++和Qt中的索引通常从0开始，因此第一行是第0行，第一列是第0列。*/


    QVBoxLayout *mainLayout = new QVBoxLayout(this); //意思是从上到下排列控件。
    mainLayout->setContentsMargins(40, 30, 40, 30);//四个参数依次是：左边距：40 上边距：30 右边距：40 下边距：30
    mainLayout->setSpacing(20);//设置各部分之间的距离：
    mainLayout->addWidget(m_titleLabel);
    mainLayout->addLayout(menuLayout);
    mainLayout->addWidget(m_statusLabel);
/*这里按照加入顺序，从上到下排列：
m_titleLabel
      ↓
menuLayout
      ↓
m_statusLabel
注意：
addWidget(...)用于加入具体控件。
addLayout(...)用于加入另一个布局。
所以：mainLayout->addLayout(menuLayout);
表示把整个按钮网格放进垂直布局中。
最终布局层级：
HomePage
└── QVBoxLayout
    ├── 标题 QLabel
    ├── QGridLayout
    │   ├── Monitor
    │   ├── Music
    │   ├── Settings
    │   ├── Network
    │   ├── Camera
    │   └── AI
    └── 状态 QLabel*/

    setStyleSheet(
        "#homePage {"
        "    background-color: #202733;"
        "}"
        "#titleLabel {"
        "    color: white;"
        "    font-size: 38px;"
        "    font-weight: bold;"
        "}"
        "#statusLabel {"
        "    color: #b8c4d6;"
        "    font-size: 20px;"
        "}"
        "QPushButton {"
        "    min-width: 220px;"
        "    min-height: 150px;"
        "    color: white;"
        "    background-color: #334155;"
        "    border: 2px solid #52637a;"
        "    border-radius: 18px;"
        "    font-size: 26px;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #ef4444;"
        "    border: 4px solid white;"
        "}"
    );
}
/*这叫Qt样式表，语法类似CSS。

首页背景
"#homePage {"
"    background-color: #202733;"
"}"

表示给对象名为 homePage 的控件设置深色背景。

标题样式
"#titleLabel {"
"    color: white;"
"    font-size: 38px;"
"    font-weight: bold;"
"}"

表示：

文字颜色：白色
字号：38像素
字体：粗体
状态文字样式
"#statusLabel {"
"    color: #b8c4d6;"
"    font-size: 20px;"
"}"
按钮样式
"QPushButton {"

表示对当前页面中的所有 QPushButton 生效。

主要设置：

最小宽度：220像素
最小高度：150像素
文字颜色：白色
背景颜色：深灰蓝
边框宽度：2像素
圆角：18像素
字体大小：26像素
按下按钮时
"QPushButton:pressed {"
"    background-color: #475569;"
"}"

表示用户触摸或按下按钮时，改变按钮背景色，提供视觉反馈。*/



QPushButton *HomePage::createMenuButton(const QString &text)//QPushButton * 表示这个函数最终返回一个按钮指针。const QString &text 
                                                            //表示接收一个Qt字符串作为按钮文字。
{
    QPushButton *button = new QPushButton(text, this);//创建一个按钮，显示传入的文字，把当前首页设为父对象，并用局部指针 button 保存按钮地址。
    button->setFocusPolicy(Qt::NoFocus);//表示按钮不通过键盘Tab键获得焦点。 这是触摸屏应用中常见的设置，可以减少按钮周围不必要的焦点框。
    return button;  //把创建出的按钮地址交还给调用者。
}