#include "settingspage.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QString>
SettingsPage::SettingsPage(QWidget *parent)
    :QWidget(parent),
     m_titleLabel(nullptr),
     m_touchCountLabel(nullptr),
     m_backButton(nullptr),
     m_touchButton(nullptr),
    
     
     m_touchCount(0)
{
    initializeUi();
}
    SettingsPage::~SettingsPage() =default;

void SettingsPage::initializeUi()
{
    setObjectName("settingsPage");
    m_titleLabel= new QLabel("System Settings",this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setObjectName("titleLabel");
    
    
    
    m_touchCountLabel= new QLabel("Touch test count:0",this);
    m_touchCountLabel->setAlignment(Qt::AlignCenter);
    m_touchCountLabel->setObjectName("countlabel");

    m_touchButton=new QPushButton(" [Touch Test] ",this);
    m_touchButton->setFocusPolicy(Qt::NoFocus);

    m_backButton=new QPushButton("Back", this);
    m_backButton->setFocusPolicy(Qt::NoFocus);

    QVBoxLayout *mainLayout =new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(m_titleLabel);
    mainLayout->addStretch();
    mainLayout->addWidget(m_touchCountLabel);
    mainLayout->addStretch();
    mainLayout->addWidget(m_touchButton);
    mainLayout->addStretch();
    mainLayout->addWidget(m_backButton);

    connect(m_touchButton,&QPushButton::clicked,this,
    &SettingsPage::count_add);

    connect(m_backButton,&QPushButton::clicked,this,
    &SettingsPage::backRequested);

}

void SettingsPage::count_add()
{
    m_touchCount++;
    QString text=QString("Touch test count:%1").arg(m_touchCount);

    m_touchCountLabel->setText(text);


}