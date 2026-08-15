#ifndef AUDIOPLAYERSERVICE_H
#define AUDIOPLAYERSERVICE_H

#include <QObject>
#include <QMediaPlayer>
#include <QString>

class AudioPlayerService : public QObject
{
    Q_OBJECT

public:
    /*
     * 对外暴露自己的播放状态，而不是让页面直接依赖
     * QMediaPlayer::State。
     *
     * 以后即使底层从 QMediaPlayer 换成 GStreamer 或 FFmpeg，
     * 页面使用的状态类型也可以保持不变。
     */
    enum class PlaybackState {
        Stopped,
        Playing,
        Paused
    };
    Q_ENUM(PlaybackState)

    explicit AudioPlayerService(QObject *parent = nullptr);

    /*
     * 加载一个本地音频文件。
     *
     * 返回 true：
     * 路径存在，并且已经交给 QMediaPlayer 加载。
     *
     * 返回 false：
     * 路径为空、文件不存在，或者目标不是普通文件。
     */
    bool loadFile(const QString &filePath);

    void play();
    void pause();
    void stop();

    /*
     * QMediaPlayer 的音量范围是 0～100。
     */
    void setVolume(int volume);

    /*
     * 单位为毫秒，后续用于进度条拖动。
     */
    void setPosition(qint64 position);

    qint64 position() const;
    qint64 duration() const;
    int volume() const;

    bool isPlaying() const;

    QString currentFilePath() const;

signals:
    /*
     * 服务层对页面发出的状态通知。
     */
    void playbackStateChanged(AudioPlayerService::PlaybackState state);

    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);

    void fileLoaded(const QString &filePath);

    void errorOccurred(const QString &message);

private slots:
    /*
     * 接收 QMediaPlayer 的底层状态，再转换成服务层状态。
     */
    void handlePlayerStateChanged(QMediaPlayer::State state);

    void handlePlayerError(QMediaPlayer::Error error);

private:
    QMediaPlayer *m_player;

    PlaybackState m_state;

    QString m_currentFilePath;
};

#endif
