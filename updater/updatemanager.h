#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

class UpdateManager : public QObject
{
    Q_OBJECT

public:
    explicit UpdateManager(QObject *parent = nullptr);
    ~UpdateManager();

    void checkForUpdates();
    void downloadAndInstallUpdate();
    QString getCurrentVersion();

signals:
    void updateCheckFinished(bool updateAvailable, const QString& latestVersion, const QString& releaseNotes);
    void updateCheckError(const QString& error);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished();
    void downloadError(const QString& error);
    void installationFinished(bool success, const QString& message);
    void logMessage(const QString& message);

private slots:
    void onUpdateCheckReplyFinished();
    void onDownloadReplyFinished();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
    bool compareVersions(const QString& version1, const QString& version2);
    void extractUpdate(const QString& zipFilePath);
    bool createBackup();
    bool copyDirectoryRecursively(const QString& srcPath, const QString& dstPath);
    void cleanupTempFiles();
    QString getExecutablePath();
    QString getApplicationDirectory();

    QNetworkAccessManager* m_networkManager;
    QNetworkReply* m_currentReply;
    QFile* m_downloadFile;
    
    // GitHub API configuration
    QString m_repoOwner;
    QString m_repoName;
    QString m_apiUrl;
    
    // Update information
    QString m_currentVersion;
    QString m_latestVersion;
    QString m_downloadUrl;
    QString m_releaseNotes;
    QString m_downloadFilePath;
    
    // Paths
    QString m_appDirectory;
    QString m_tempDirectory;
    QString m_backupDirectory;
};

#endif // UPDATEMANAGER_H
