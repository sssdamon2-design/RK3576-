#ifndef MUSICPAGE_H
#define MUSICPAGE_H

#include <QString>
#include <QWidget>
#include <QDir>
#include <QListWidget>
class AudioPlayerService;
class QLabel;
class QPushButton;
class QSlider;

class MusicPage : public QWidget
{
    Q_OBJECT

public:
    explicit MusicPage(
        AudioPlayerService *audioPlayerService,QDir MusicDir,
        QWidget *parent = nullptr);

signals:
    /*
     * 用户点击“返回”按钮时发出。
     * 页面本身不负责切换页面，由 MainWindow 统一处理。
     */
    void backRequested();

private:
    void initializeUi();
    void connectServiceSignals();
    void connectUiSignals();
    void scanMusicDir();
    /*
     * 将毫秒转换为 mm:ss。
     */
    QString formatTime(qint64 milliseconds) const;

private:
    AudioPlayerService *m_audioPlayerService;
    QDir m_musicDir;
    QPushButton *m_backButton;

    QLabel *m_titleLabel;
    QLabel *m_fileNameLabel;

    QSlider *m_positionSlider;
    QLabel *m_currentTimeLabel;
    QLabel *m_durationLabel;

    QPushButton *m_nextSongButton;
    QPushButton *m_previousSongButton;
    QPushButton *m_playPauseButton;
    QPushButton *m_stopButton;
    
    QLabel *m_currentSongLabel;
    
    QSlider *m_volumeSlider;
    QLabel *m_volumeValueLabel;

    QListWidget *m_mysonlist;
};

#endif