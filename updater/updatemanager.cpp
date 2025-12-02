#include "updatemanager.h"
#include <QJsonArray>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QProcess>
#include <QMessageBox>
#include <QDateTime>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent)
    , m_currentReply(nullptr)
    , m_downloadFile(nullptr)
{
    m_networkManager = new QNetworkAccessManager(this);
    
    // GitHub configuration
    m_repoOwner = "mrmjstc";
    m_repoName = "eve-apm-preview";
    m_apiUrl = QString("https://api.github.com/repos/%1/%2/releases/latest")
                   .arg(m_repoOwner, m_repoName);
    
    // Get current version
    m_currentVersion = getCurrentVersion();
    
    // Setup paths
    m_appDirectory = getApplicationDirectory();
    m_tempDirectory = QDir::tempPath() + "/EVEAPMPreview_Update";
    m_backupDirectory = m_appDirectory + "/backup";
    
    // Ensure temp directory exists
    QDir().mkpath(m_tempDirectory);
}

UpdateManager::~UpdateManager()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }
    
    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
    }
}

QString UpdateManager::getCurrentVersion()
{
    // Try to read version from version.txt in application directory
    QString versionFile = getApplicationDirectory() + "/version.txt";
    QFile file(versionFile);
    
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString version = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
        return version.isEmpty() ? "0.0.0" : version;
    }
    
    return "0.0.0";
}

void UpdateManager::checkForUpdates()
{
    QNetworkRequest request(m_apiUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "EVE-APM-Preview-Updater");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    
    m_currentReply = m_networkManager->get(request);
    connect(m_currentReply, &QNetworkReply::finished, 
            this, &UpdateManager::onUpdateCheckReplyFinished);
}

void UpdateManager::onUpdateCheckReplyFinished()
{
    if (m_currentReply->error() != QNetworkReply::NoError) {
        QString error = m_currentReply->errorString();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        emit updateCheckError(error);
        return;
    }
    
    QByteArray responseData = m_currentReply->readAll();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
    
    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        emit updateCheckError("Invalid response from GitHub API");
        return;
    }
    
    QJsonObject releaseObj = jsonDoc.object();
    
    // Get version tag (remove 'v' prefix if present)
    m_latestVersion = releaseObj["tag_name"].toString();
    if (m_latestVersion.startsWith("v", Qt::CaseInsensitive)) {
        m_latestVersion = m_latestVersion.mid(1);
    }
    
    // Get release notes
    m_releaseNotes = releaseObj["body"].toString();
    if (m_releaseNotes.isEmpty()) {
        m_releaseNotes = "No release notes available.";
    }
    
    // Get download URL for the zip asset
    QJsonArray assets = releaseObj["assets"].toArray();
    m_downloadUrl.clear();
    
    for (const QJsonValue& assetVal : assets) {
        QJsonObject asset = assetVal.toObject();
        QString assetName = asset["name"].toString();
        
        // Look for zip file
        if (assetName.endsWith(".zip", Qt::CaseInsensitive)) {
            m_downloadUrl = asset["browser_download_url"].toString();
            break;
        }
    }
    
    if (m_downloadUrl.isEmpty()) {
        emit updateCheckError("No downloadable asset found in latest release");
        return;
    }
    
    // Compare versions
    bool updateAvailable = compareVersions(m_currentVersion, m_latestVersion);
    emit updateCheckFinished(updateAvailable, m_latestVersion, m_releaseNotes);
}

bool UpdateManager::compareVersions(const QString& version1, const QString& version2)
{
    // Parse version strings (format: major.minor.patch)
    QStringList v1Parts = version1.split('.');
    QStringList v2Parts = version2.split('.');
    
    // Ensure we have at least 3 parts
    while (v1Parts.size() < 3) v1Parts.append("0");
    while (v2Parts.size() < 3) v2Parts.append("0");
    
    // Compare each part
    for (int i = 0; i < 3; i++) {
        int num1 = v1Parts[i].toInt();
        int num2 = v2Parts[i].toInt();
        
        if (num2 > num1) {
            return true; // version2 is newer
        } else if (num2 < num1) {
            return false; // version1 is newer or equal
        }
    }
    
    return false; // versions are equal
}

void UpdateManager::downloadAndInstallUpdate()
{
    if (m_downloadUrl.isEmpty()) {
        emit downloadError("No download URL available");
        return;
    }
    
    // Setup download file
    m_downloadFilePath = m_tempDirectory + "/update.zip";
    m_downloadFile = new QFile(m_downloadFilePath);
    
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        emit downloadError("Failed to create temporary file");
        delete m_downloadFile;
        m_downloadFile = nullptr;
        return;
    }
    
    // Start download
    QNetworkRequest request(m_downloadUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "EVE-APM-Preview-Updater");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, 
                        QNetworkRequest::NoLessSafeRedirectPolicy);
    
    m_currentReply = m_networkManager->get(request);
    
    connect(m_currentReply, &QNetworkReply::downloadProgress, 
            this, &UpdateManager::onDownloadProgress);
    connect(m_currentReply, &QNetworkReply::finished, 
            this, &UpdateManager::onDownloadReplyFinished);
    connect(m_currentReply, &QNetworkReply::readyRead, [this]() {
        if (m_downloadFile) {
            m_downloadFile->write(m_currentReply->readAll());
        }
    });
}

void UpdateManager::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    emit downloadProgress(bytesReceived, bytesTotal);
}

void UpdateManager::onDownloadReplyFinished()
{
    if (!m_downloadFile) {
        return;
    }
    
    // Write any remaining data
    if (m_currentReply) {
        m_downloadFile->write(m_currentReply->readAll());
    }
    
    m_downloadFile->close();
    
    if (m_currentReply && m_currentReply->error() != QNetworkReply::NoError) {
        QString error = m_currentReply->errorString();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        emit downloadError(error);
        return;
    }
    
    if (m_currentReply) {
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    
    emit downloadFinished();
    
    // Start installation
    extractUpdate(m_downloadFilePath);
}

void UpdateManager::extractUpdate(const QString& zipFilePath)
{
    // Create backup first
    if (!createBackup()) {
        emit installationFinished(false, "Failed to create backup");
        return;
    }
    
    // Extract to temporary location first
    QString tempExtractPath = m_tempDirectory + "/extracted";
    QDir().mkpath(tempExtractPath);
    
    // Use PowerShell to extract the zip file to temp location
    QString extractScript = QString(
        "try { "
        "Expand-Archive -Path '%1' -DestinationPath '%2' -Force; "
        "exit 0; "
        "} catch { "
        "Write-Error $_.Exception.Message; "
        "exit 1; "
        "}"
    ).arg(zipFilePath, tempExtractPath);
    
    QProcess process;
    process.start("powershell.exe", QStringList() 
                  << "-NoProfile" 
                  << "-ExecutionPolicy" << "Bypass" 
                  << "-Command" << extractScript);
    
    if (!process.waitForStarted()) {
        emit installationFinished(false, "Failed to start extraction process");
        return;
    }
    
    if (!process.waitForFinished(60000)) { // 60 second timeout
        process.kill();
        emit installationFinished(false, "Extraction process timed out");
        return;
    }
    
    int exitCode = process.exitCode();
    QString errorOutput = QString::fromLocal8Bit(process.readAllStandardError());
    
    if (exitCode != 0) {
        emit installationFinished(false, QString("Extraction failed: %1").arg(errorOutput));
        return;
    }
    
    // Check if extracted files are in a nested folder
    QDir extractDir(tempExtractPath);
    QFileInfoList entries = extractDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    
    QString sourcePath = tempExtractPath;
    
    // If there's only one directory at the root level, use that as source
    if (entries.size() == 1 && entries[0].isDir()) {
        sourcePath = entries[0].absoluteFilePath();
        emit logMessage("Detected nested folder structure, using: " + entries[0].fileName());
    }
    
    // Now copy files from source to app directory, preserving user files
    QStringList preserveFiles;
    preserveFiles << "settings.global.ini" << "profiles" << "backup" << "version.txt";
    
    QDir srcDir(sourcePath);
    QFileInfoList files = srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    
    for (const QFileInfo& fileInfo : files) {
        QString fileName = fileInfo.fileName();
        
        // Skip preserved files
        if (preserveFiles.contains(fileName)) {
            continue;
        }
        
        QString srcPath = fileInfo.absoluteFilePath();
        QString dstPath = m_appDirectory + "/" + fileName;
        
        if (fileInfo.isDir()) {
            // Remove old directory if exists
            QDir oldDir(dstPath);
            if (oldDir.exists()) {
                oldDir.removeRecursively();
            }
            // Copy new directory
            copyDirectoryRecursively(srcPath, dstPath);
        } else {
            // Remove old file and copy new one
            QFile::remove(dstPath);
            QFile::copy(srcPath, dstPath);
        }
    }
    
    // Update version file
    QString versionFile = m_appDirectory + "/version.txt";
    QFile file(versionFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(m_latestVersion.toUtf8());
        file.close();
    }
    
    // Cleanup
    cleanupTempFiles();
    
    emit installationFinished(true, "Update installed successfully. Please restart the application.");
}

bool UpdateManager::createBackup()
{
    // Ensure backup directory exists
    QDir backupDir(m_backupDirectory);
    if (!backupDir.exists()) {
        if (!backupDir.mkpath(".")) {
            return false;
        }
    }
    
    // Create timestamped backup subfolder
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString backupPath = m_backupDirectory + "/backup_" + timestamp;
    
    if (!QDir().mkpath(backupPath)) {
        return false;
    }
    
    // Copy important files to backup
    QStringList filesToBackup;
    filesToBackup << "settings.global.ini"
                  << "version.txt"
                  << "EVE_Wildcard_Switch.ahk"
                  << "RunEVEAPMPreview.bat"
                  << "RunEVEAPMPreviewWithUpdates.bat";
    
    QDir appDir(m_appDirectory);
    for (const QString& fileName : filesToBackup) {
        QString srcPath = appDir.filePath(fileName);
        QString dstPath = backupPath + "/" + fileName;
        
        if (QFile::exists(srcPath)) {
            QFile::copy(srcPath, dstPath);
        }
    }
    
    // Also backup profiles directory
    QString profilesSrc = m_appDirectory + "/profiles";
    QString profilesDst = backupPath + "/profiles";
    
    if (QDir(profilesSrc).exists()) {
        copyDirectoryRecursively(profilesSrc, profilesDst);
    }
    
    return true;
}

bool UpdateManager::copyDirectoryRecursively(const QString& srcPath, const QString& dstPath)
{
    QDir srcDir(srcPath);
    if (!srcDir.exists()) {
        return false;
    }
    
    QDir dstDir(dstPath);
    if (!dstDir.exists()) {
        if (!dstDir.mkpath(".")) {
            return false;
        }
    }
    
    QStringList files = srcDir.entryList(QDir::Files);
    for (const QString& fileName : files) {
        QString srcFilePath = srcDir.filePath(fileName);
        QString dstFilePath = dstDir.filePath(fileName);
        QFile::copy(srcFilePath, dstFilePath);
    }
    
    QStringList subdirs = srcDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& subdir : subdirs) {
        QString srcSubdirPath = srcDir.filePath(subdir);
        QString dstSubdirPath = dstDir.filePath(subdir);
        copyDirectoryRecursively(srcSubdirPath, dstSubdirPath);
    }
    
    return true;
}

void UpdateManager::cleanupTempFiles()
{
    // Remove downloaded zip file
    if (!m_downloadFilePath.isEmpty()) {
        QFile::remove(m_downloadFilePath);
    }
    
    // Try to remove temp directory (may fail if not empty, which is okay)
    QDir(m_tempDirectory).removeRecursively();
}

QString UpdateManager::getExecutablePath()
{
    return QCoreApplication::applicationFilePath();
}

QString UpdateManager::getApplicationDirectory()
{
    QString exePath = QCoreApplication::applicationDirPath();
    
    // If we're in the updater subdirectory, go up one level
    if (exePath.endsWith("/updater") || exePath.endsWith("\\updater")) {
        QDir dir(exePath);
        dir.cdUp();
        return dir.absolutePath();
    }
    
    return exePath;
}
