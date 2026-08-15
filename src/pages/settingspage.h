#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H


#include <QWidget>

class QLabel;
class QPushButton;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent=nullptr);
    ~SettingsPage() override;

signals:
    void backRequested();

private slots:
    void count_add();

private:
    void initializeUi();

    QLabel *m_titleLabel;
    QLabel *m_touchCountLabel;

    QPushButton *m_touchButton;
    QPushButton *m_backButton;

    int m_touchCount;
};

#endif
