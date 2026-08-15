#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QWidget>

class QLabel;   //显示文字
class QPushButton;//用来创建按钮，
class QString;//是 Qt 提供的字符串类。
class HomePage : public QWidget   //继承QWidget   这是另一种类 定义一个名为 HomePage 的类，并让它公开继承 Qt 的 QWidget 类。
/*因为继承了 QWidget，所以 HomePage 就具备了成为图形页面的基础能力，例如：

可以显示；
可以设置背景；
可以放按钮；
可以放文字；
可以响应触摸操作；
可以加入布局。*/
{
    Q_OBJECT //用于启用 Qt 的元对象系统。

public:
    explicit HomePage(QWidget *parent = nullptr); //构造函数  表示创建首页对象时，可以指定一个父对象。
    ~HomePage() override;//析构函数 当首页对象销毁时自动调用。
 

signals:
    void monitorRequested();
    void settingRequested();
     void musicRequested();
private:
    void initializeUi();  //初始化函数
    QPushButton *createMenuButton(const QString &text); //根据传入的文字创建一个菜单按钮，并返回按钮对象的地址。
    /*例如：
createMenuButton("Monitor");
会创建一个显示 Monitor 的按钮。
返回值
QPushButton *
表示函数返回一个 QPushButton 对象的指针。
参数
const QString &text
可以拆成：
QString：Qt字符串类型
&：引用，不复制整个字符串
const：函数只能读取，不能修改字符串
text：参数名称
后面会专门深入讲引用和 const。目前可以理解成：
把按钮文字交给函数使用，但函数不能修改原文字。*/

    QLabel *m_titleLabel;
    QLabel *m_statusLabel;  //两个标签指针m_titleLabel：指向首页顶部标题 m_statusLabel：指向首页底部状态文字

    QPushButton *m_monitorButton;
    QPushButton *m_musicButton;
    QPushButton *m_settingsButton;
    QPushButton *m_networkButton;
    QPushButton *m_cameraButton;
    QPushButton *m_aiButton;  //声明6个按钮指针
    
};

#endif