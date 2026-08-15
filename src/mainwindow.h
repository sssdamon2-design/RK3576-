/*我们定义了一个MainWindow类
它继承QMainWindow
它有构造函数和析构函数
它有一个初始化界面的函数
它保存页面容器和首页指针*/


#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>    //QT提供的QMAINdow类
class QStackedWidget;  //声明使用的类
class HomePage;

class MonitorPage;
class SettingsPage;

class AudioPlayerService;
class MusicPage;

class MainWindow : public QMainWindow  //声明一个MainWindow 公开继承QMinWindow 类
{
    Q_OBJECT/*Q_OBJECT是Qt提供的宏，用于启用Qt元对象系统。

它支持：

自定义信号；
自定义槽；
运行时类型信息；
Qt属性系统；
对象反射能力。

虽然我们当前还没有自定义信号，但后面页面切换会用到，所以现在保留。*/
    public:  //public后面的成员可以从类外部访问  C++类常见的访问权限有：public      类外可以访问private     
                                                //只有类内部可以访问protected   类内部和子类可以访问
        explicit MainWindow(QWidget *parent =nullptr);
    /*
    构造函数 构造函数名称必须和类名相同：类名：MainWindow
构造函数：MainWindow(...)  构造函数没有返回类型，连void也不写。
参数是什么意思
QWidget *parent
表示可以给主窗口指定一个父对象。= nullptr表示默认没有父对象。
因此下面两种调用都可以：MainWindow window;和：MainWindow window(someParent);
nullptr是什么
nullptr是C++11中的空指针。
它比旧式C代码中的：
NULL
类型更明确，更适合C++使用。
explicit是什么
explicit用于阻止编译器进行不期望的隐式类型转换。
对于只有一个可选参数的构造函数，这是一种良好的C++习惯。现在先记住：
单参数构造函数通常建议加explicit，避免对象被意外转换出来。
后续遇到隐式转换时会再深入讲。
    */
        ~MainWindow() override;
    /*
    析构函数
~MainWindow() override;析构函数名称是在类名前加~：
构造函数：MainWindow()析构函数：~MainWindow()

对象销毁时，析构函数会自动执行。

它常用于释放：

动态内存；
文件；
-设备句柄；
线程；
网络连接。
override是什么意思

QMainWindow已经有虚析构函数。

我们声明：

~MainWindow() override;

是在告诉编译器：

这个析构函数要覆盖父类中的虚函数。

如果函数签名写错，编译器会直接报错，所以override有助于提前发现问题。
    */
    


    private:    //声明私有成员  下面这些变量只允许在MainWindow内部访问
    /*例如在mainwindow.cpp里可以使用：

m_pageStack
m_homePage

但是其他类不能随意直接修改它们。

这就是封装：

把类内部实现隐藏起来，只对外公开必要接口。*/
        void initializeUi();
        QStackedWidget *m_pageStack;
        HomePage *m_homePage;
        MonitorPage *m_monitorPage;         //这些类定义在各自的cpp文件里
        SettingsPage *m_settingsPage;
        AudioPlayerService *m_audioPlayerService;
        MusicPage *m_musicPage;
    private slots:
    void showHomePage();
    void showMonitorPage();
    void showSettingsPage();
    void showMusicPage();
            /*initializeUi()
void initializeUi();

这是我们自己定义的成员函数，用于创建界面。

void：函数没有返回值；
initializeUi：函数名；
()：当前不需要参数；
最后的;表示这里只声明，不实现。

它的具体代码以后写在：

src/mainwindow.cpp

里面。

为什么不把所有代码都写进构造函数？

因为构造函数如果过长，会不容易阅读。我们把界面创建逻辑单独封装成：

initializeUi();

构造函数只负责调用它。

页面容器指针
QStackedWidget *m_pageStack;

这表示：

类型是QStackedWidget;
*表示这是一个指针；
变量名是m_pageStack。

它以后会指向真正创建出来的页面容器对象。

变量名前的m_表示：

m = member

即这是一个成员变量。

这是一种命名习惯，不是C++强制要求。

首页指针
HomePage *m_homePage;

它以后指向首页对象。

调用关系会是：

MainWindow
├── m_pageStack  → 页面容器
└── m_homePage   → 首页

后面还会增加：

MonitorPage *m_monitorPage;
MusicPage *m_musicPage;
SettingsPage *m_settingsPage;*/
};

#endif