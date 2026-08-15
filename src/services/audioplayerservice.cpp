
#include "audioplayerservice.h"
#include <QFileInfo>
#include <QMediaContent>
#include <QUrl>
#include <QtGlobal>

AudioPlayerService::AudioPlayerService(QObject *parent)
    : QObject(parent),
      m_player(new QMediaPlayer(this)),   //直接在初始化列表中生成
      m_state(PlaybackState::Stopped)
{
    /*
     * QMediaPlayer 状态改变后，先进入服务层转换函数。
     */
    connect(m_player,
            &QMediaPlayer::stateChanged,   //Qt 的 QMediaPlayer 类自带的一个 signal（信号）
            this,
            &AudioPlayerService::handlePlayerStateChanged);

    /*
     * QMediaPlayer 的进度和总时长信号可以直接转发给页面。
     * position 和 duration 的单位都是毫秒。
     */
    connect(m_player,
            &QMediaPlayer::positionChanged,
            this,
            &AudioPlayerService::positionChanged);

    connect(m_player,
            &QMediaPlayer::durationChanged,
            this,
            &AudioPlayerService::durationChanged);

    /*
     * Qt 5.12 中 QMediaPlayer::error 是重载成员，
     * 所以需要使用 QOverload 指明这里连接的是错误信号。
     */
    connect(m_player,
            QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error),
            this,
            &AudioPlayerService::handlePlayerError);

    /*
     * 默认使用一个较安全的音量，避免首次播放突然过响。
     */
    m_player->setVolume(60);
}

bool AudioPlayerService::loadFile(const QString &filePath)
{
    /*
     * QFileInfo 只读取文件信息，不会修改或打开音频内容。
     */
    const QFileInfo fileInfo(filePath);  //QT提供的QFileInfo类

    if (filePath.trimmed().isEmpty()) 
    {
        emit errorOccurred(QStringLiteral("音频文件路径为空"));  //发出信号
        return false;
    }

    if (!fileInfo.exists())
     {
        emit errorOccurred(QStringLiteral("音频文件不存在：%1").arg(filePath));
        return false;
    }

    if (!fileInfo.isFile())
     {
        emit errorOccurred(QStringLiteral("目标路径不是普通文件：%1").arg(filePath));
        return false;
    }

    /*
     * 更换音频文件前先停止当前播放。
     */
    m_player->stop();

    m_currentFilePath = fileInfo.absoluteFilePath();

    /*
     * GStreamer 接收本地媒体时通常使用 file:// URI，
     * 因此不能直接把普通路径字符串传给 setMedia()。
     */
    const QUrl mediaUrl =QUrl::fromLocalFile(m_currentFilePath);   //QUrl类处理url类

    m_player->setMedia(QMediaContent(mediaUrl));

    emit fileLoaded(m_currentFilePath);

    return true;
}

void AudioPlayerService::play()
{
    if (m_currentFilePath.isEmpty())
     {
        emit errorOccurred(
            QStringLiteral("尚未加载音频文件"));
        return;
    }

    m_player->play();
}

void AudioPlayerService::pause()
{
    m_player->pause();
}

void AudioPlayerService::stop()
{
    /*
     * 停止当前播放。
     */
    m_player->stop();

    /*
     * 明确回到音频开头。
     *
     * QMediaPlayer 会通过 positionChanged(0)
     * 通知 MusicPage 更新进度条和时间。
     */
    m_player->setPosition(0);
}

void AudioPlayerService::setVolume(int volume)
{
    /*
     * qBound() 将输入限制在 0～100。
     *
     * 即使页面错误地传入 -10 或 200，
     * 也不会把非法值交给 QMediaPlayer。
     */
    const int safeVolume = qBound(0, volume, 100);

    m_player->setVolume(safeVolume);
}

void AudioPlayerService::setPosition(qint64 position)
{
    if (position < 0) {
        position = 0;
    }

    m_player->setPosition(position);
}

qint64 AudioPlayerService::position() const
{
    return m_player->position();
}

qint64 AudioPlayerService::duration() const
{
    return m_player->duration();
}

int AudioPlayerService::volume() const
{
    return m_player->volume();
}

bool AudioPlayerService::isPlaying() const
{
    return m_state == PlaybackState::Playing;
}

QString AudioPlayerService::currentFilePath() const
{
    return m_currentFilePath;
}

void AudioPlayerService::handlePlayerStateChanged(QMediaPlayer::State state)
{
    switch (state) {
    case QMediaPlayer::PlayingState:
        m_state = PlaybackState::Playing;
        break;

    case QMediaPlayer::PausedState:
        m_state = PlaybackState::Paused;
        break;

    case QMediaPlayer::StoppedState:
    default:
        m_state = PlaybackState::Stopped;
        break;
    }

    emit playbackStateChanged(m_state);
}

void AudioPlayerService::handlePlayerError(QMediaPlayer::Error error)
{
    if (error == QMediaPlayer::NoError) {
        return;
    }

    QString message = m_player->errorString();

    if (message.isEmpty()) {
        message = QStringLiteral("音频播放发生未知错误");
    }

    emit errorOccurred(message);
}
