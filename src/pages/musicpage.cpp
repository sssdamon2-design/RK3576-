#include "musicpage.h"

#include "../services/audioplayerservice.h"
#include<QListWidget>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QtGlobal>

MusicPage::MusicPage(
    AudioPlayerService *audioPlayerService,QDir MusicDir,
    QWidget *parent)
    : QWidget(parent),
      m_audioPlayerService(audioPlayerService),
      m_musicDir(MusicDir),
      m_backButton(nullptr),
      m_titleLabel(nullptr),
      m_fileNameLabel(nullptr),
      m_positionSlider(nullptr),
      m_currentTimeLabel(nullptr),
      m_durationLabel(nullptr),
      m_nextSongButton(nullptr),
      m_previousSongButton(nullptr),
      m_playPauseButton(nullptr),
      m_stopButton(nullptr),
      m_currentSongLabel(nullptr),
      m_volumeSlider(nullptr),
      m_volumeValueLabel(nullptr),
      m_mysonlist(nullptr)
{
    Q_ASSERT(m_audioPlayerService != nullptr);

    initializeUi();
    connectServiceSignals();
    connectUiSignals();
    scanMusicDir();
}

void MusicPage::initializeUi()
{

    m_currentSongLabel=new QLabel("当前歌曲:--",this);
    m_currentSongLabel->setAlignment(Qt::AlignCenter);
    m_mysonlist= new QListWidget(this);
    m_backButton = new QPushButton(
        QStringLiteral("返回"),
        this);
    m_backButton->setFocusPolicy(Qt::NoFocus);

    m_titleLabel = new QLabel(
        QStringLiteral("音乐播放器"),
        this);
    m_titleLabel->setAlignment(Qt::AlignCenter);

    m_previousSongButton=new QPushButton("上一首",this);
    m_nextSongButton=new QPushButton("下一首",this);

    /*
     * 标题行左侧放返回按钮。
     */
    QHBoxLayout *titleLayout = new QHBoxLayout;
    titleLayout->addWidget(m_backButton);
    titleLayout->addStretch();
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();

    m_fileNameLabel = new QLabel(
        QStringLiteral("尚未加载音频文件"),
        this);
    m_fileNameLabel->setAlignment(Qt::AlignCenter);
    m_fileNameLabel->setWordWrap(true);

    m_currentTimeLabel = new QLabel(
        QStringLiteral("00:00"),
        this);

    m_durationLabel = new QLabel(
        QStringLiteral("00:00"),
        this);

    m_positionSlider = new QSlider(
        Qt::Horizontal,
        this);
    m_positionSlider->setRange(0, 0);
    m_positionSlider->setValue(0);
    m_positionSlider->setEnabled(false);

    QHBoxLayout *positionLayout = new QHBoxLayout;
    positionLayout->addWidget(m_currentTimeLabel);
    positionLayout->addWidget(m_positionSlider, 1);
    positionLayout->addWidget(m_durationLabel);

    m_playPauseButton = new QPushButton(
        QStringLiteral("播放"),
        this);

    m_stopButton = new QPushButton(
        QStringLiteral("停止"),
        this);

    m_playPauseButton->setEnabled(false);
    m_stopButton->setEnabled(false);

    QHBoxLayout *controlLayout = new QHBoxLayout;
    controlLayout->addStretch();
    controlLayout->addWidget(m_playPauseButton);
    controlLayout->addWidget(m_stopButton);
    controlLayout->addStretch();

    QLabel *volumeTitleLabel = new QLabel(
        QStringLiteral("音量"),
        this);

    m_volumeSlider = new QSlider(
        Qt::Horizontal,
        this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(
        m_audioPlayerService->volume());

    m_volumeValueLabel = new QLabel(
        QString::number(m_volumeSlider->value()),
        this);
    m_volumeValueLabel->setMinimumWidth(35);
    m_volumeValueLabel->setAlignment(Qt::AlignCenter);

    QHBoxLayout *volumeLayout = new QHBoxLayout;
    volumeLayout->addWidget(volumeTitleLabel);
    volumeLayout->addWidget(m_volumeSlider, 1);
    volumeLayout->addWidget(m_volumeValueLabel);

    QHBoxLayout *last_prevous_song = new QHBoxLayout;
    last_prevous_song->addWidget(m_previousSongButton);
    last_prevous_song->addWidget(m_nextSongButton);

    /*
     * QVBoxLayout(this) 已经把布局安装到 MusicPage，
     * 因此不需要再调用 setLayout(mainLayout)。
     */
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(60, 35, 60, 35);
    mainLayout->setSpacing(22);

    mainLayout->addLayout(titleLayout);
    mainLayout->addStretch();
    mainLayout->addWidget(m_currentSongLabel);
    mainLayout->addStretch();
    mainLayout->addWidget(m_fileNameLabel);
    mainLayout->addLayout(positionLayout);
    mainLayout->addLayout(controlLayout);
    mainLayout->addLayout(volumeLayout);
    mainLayout->addStretch();
    mainLayout->addLayout(last_prevous_song);
    mainLayout->addStretch();
    mainLayout->addWidget(m_mysonlist);
}

void MusicPage::connectServiceSignals()
{
    connect(m_audioPlayerService,
            &AudioPlayerService::fileLoaded,
            this,
            [this](const QString &filePath)   //匿名函数
            {
                const QFileInfo fileInfo(filePath);

                m_fileNameLabel->setText(fileInfo.fileName());

                m_playPauseButton->setEnabled(true);
                m_stopButton->setEnabled(true);

                /*
                 * 进度条要等 durationChanged() 得到有效时长后再启用。
                 */
                m_positionSlider->setEnabled(false);
            }
        );

    connect(m_audioPlayerService,
            &AudioPlayerService::playbackStateChanged,
            this,
            [this](AudioPlayerService::PlaybackState state)
             {
                switch (state)
                 {
                case AudioPlayerService::PlaybackState::Playing:
                    m_playPauseButton->setText(
                        QStringLiteral("暂停"));
                    m_stopButton->setEnabled(true);
                    break;

                case AudioPlayerService::PlaybackState::Paused:
                    m_playPauseButton->setText(
                        QStringLiteral("继续"));
                    m_stopButton->setEnabled(true);
                    break;

                case AudioPlayerService::PlaybackState::Stopped:
                default:
                 {
                    m_playPauseButton->setText(
                        QStringLiteral("播放"));

                    const bool hasFile =
                        !m_audioPlayerService
                             ->currentFilePath()
                             .isEmpty();

                    m_playPauseButton->setEnabled(hasFile);
                    m_stopButton->setEnabled(hasFile);
                    break;
                }
                }
            }
        );

    connect(m_audioPlayerService,
            &AudioPlayerService::positionChanged,
            this,
            [this](qint64 position) 
            {
                m_currentTimeLabel->setText(formatTime(position));

                if (!m_positionSlider->isSliderDown()) 
                {
                    m_positionSlider->setValue(
                        static_cast<int>(position));
                }
            });

    connect(m_audioPlayerService,
            &AudioPlayerService::durationChanged,
            this,
            [this](qint64 duration) {
                const int sliderMaximum =
                    duration > 0
                        ? static_cast<int>(duration)
                        : 0;

                m_positionSlider->setRange(
                    0,
                    sliderMaximum);

                m_durationLabel->setText(
                    formatTime(duration));

                m_positionSlider->setEnabled(
                    duration > 0);
            });

    connect(m_audioPlayerService,
            &AudioPlayerService::errorOccurred,
            this,
            [this](const QString &message) {
                m_fileNameLabel->setText(
                    QStringLiteral("播放错误：%1")
                        .arg(message));

                m_playPauseButton->setEnabled(false);
                m_stopButton->setEnabled(false);
                m_positionSlider->setEnabled(false);
            });
}

void MusicPage::connectUiSignals()
{
    connect(m_backButton,
            &QPushButton::clicked,
            this,
            &MusicPage::backRequested);

    connect(m_playPauseButton,
            &QPushButton::clicked,
            this,
            [this]() {
                if (m_audioPlayerService->isPlaying()) {
                    m_audioPlayerService->pause();
                } else {
                    m_audioPlayerService->play();
                }
            });

    connect(m_stopButton,
            &QPushButton::clicked,
            this,
            [this]() {
                m_audioPlayerService->stop();
            });

    connect(m_volumeSlider,
            &QSlider::valueChanged,
            this,
            [this](int value) {
                m_audioPlayerService->setVolume(value);
                m_volumeValueLabel->setText(
                    QString::number(value));
            });

    connect(m_positionSlider,
            &QSlider::sliderMoved,
            this,
            [this](int position) {
                m_currentTimeLabel->setText(
                    formatTime(position));
            });

    connect(m_positionSlider,
            &QSlider::sliderReleased,
            this,
            [this]() {
                m_audioPlayerService->setPosition(
                    m_positionSlider->value());
            });



    connect(m_mysonlist,&QListWidget::currentRowChanged,this,
        [this](int row){
            if (row<0)
            {
                return;
            }
            QListWidgetItem *Item=m_mysonlist->item(row);
            QString musicName=Item->text();
            
            QString filepath=m_musicDir.filePath(musicName);

            bool wasplaying = m_audioPlayerService->isPlaying();

            if(m_audioPlayerService->loadFile(filepath))
            {
                m_currentSongLabel->setText(QString("当前播放：%1").arg(musicName))  ;

            }

            if(wasplaying)
            {
                m_audioPlayerService->play();
            }

        });

    connect(m_nextSongButton,&QPushButton::clicked,this,
        [this]()
        {
            int musiccount = m_mysonlist->count();

            int currentmusic = m_mysonlist->currentRow();

            int nextmusic;
            if(musiccount-1==currentmusic)
            {
                nextmusic=0;
            }
            else
            nextmusic=currentmusic+1;

            m_mysonlist->setCurrentRow(nextmusic);

        });

    connect(m_previousSongButton,&QPushButton::clicked,this,
        [this](){
            int musiccount = m_mysonlist->count();

            int currentmusic = m_mysonlist->currentRow();

            int nextmusic;
            if(currentmusic==0)
            {
                nextmusic=musiccount-1;
            }
            else
            nextmusic=currentmusic-1;

            m_mysonlist->setCurrentRow(nextmusic);

        });



    
}

void MusicPage::scanMusicDir()
{
    if(!m_musicDir.exists())
    {
        m_currentSongLabel->setText(QString("当前音乐目录无文件，%1").arg(m_musicDir.absolutePath()));
        return;
    }

    QStringList filters;
    filters <<"*.mp3";
    QFileInfoList files=m_musicDir.entryInfoList(filters ,QDir::Files) ;

    for(const QFileInfo &fileInfo : files)
    {
        m_mysonlist->addItem(fileInfo.fileName());
    }

    m_mysonlist->setCurrentRow(0);
}


QString MusicPage::formatTime(qint64 milliseconds) const
{
    if (milliseconds < 0) {
        milliseconds = 0;
    }

    const qint64 totalSeconds =
        milliseconds / 1000;

    const qint64 minutes =
        totalSeconds / 60;

    const qint64 seconds =
        totalSeconds % 60;

    return QStringLiteral("%1:%2")
        .arg(minutes,
             2,
             10,
             QLatin1Char('0'))
        .arg(seconds,
             2,
             10,
             QLatin1Char('0'));
}