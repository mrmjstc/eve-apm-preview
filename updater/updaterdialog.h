#ifndef UPDATERDIALOG_H
#define UPDATERDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include "updatemanager.h"

class UpdaterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UpdaterDialog(bool silentMode = false, bool autoLaunch = false, QWidget *parent = nullptr);
    ~UpdaterDialog();

signals:
    void updateCheckComplete(bool updateAvailable);

private slots:
    void onUpdateCheckFinished(bool updateAvailable, const QString& latestVersion, const QString& releaseNotes);
    void onUpdateCheckError(const QString& error);
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    void onDownloadError(const QString& error);
    void onInstallationFinished(bool success, const QString& message);
    void installUpdate();
    void skipUpdate();

private:
    void setupUI();
    void applyDarkTheme();
    void checkForUpdates();
    QString formatFileSize(qint64 bytes);

    UpdateManager* m_updateManager;
    
    // UI Elements
    QLabel* m_logoLabel;
    QLabel* m_statusLabel;
    QLabel* m_versionLabel;
    QTextEdit* m_releaseNotesText;
    QProgressBar* m_progressBar;
    QPushButton* m_installButton;
    QPushButton* m_skipButton;
    QPushButton* m_closeButton;
    
    // Mode flags
    bool m_silentMode;
    bool m_autoLaunch;
    
    // Update info
    QString m_latestVersion;
    QString m_downloadUrl;
    
    // Helper methods
    void loadSkippedVersion();
    void saveSkippedVersion(const QString& version);
    QString getSkippedVersion();
    void launchApplication();
};

#endif // UPDATERDIALOG_H
