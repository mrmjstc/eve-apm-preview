#include "updaterdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QApplication>
#include <QCoreApplication>
#include <QProcess>
#include <QFile>

UpdaterDialog::UpdaterDialog(bool silentMode, bool autoLaunch, QWidget *parent)
    : QDialog(parent)
    , m_silentMode(silentMode)
    , m_autoLaunch(autoLaunch)
{
    setupUI();
    applyDarkTheme();
    
    m_updateManager = new UpdateManager(this);
    
    connect(m_updateManager, &UpdateManager::updateCheckFinished, 
            this, &UpdaterDialog::onUpdateCheckFinished);
    connect(m_updateManager, &UpdateManager::updateCheckError, 
            this, &UpdaterDialog::onUpdateCheckError);
    connect(m_updateManager, &UpdateManager::downloadProgress, 
            this, &UpdaterDialog::onDownloadProgress);
    connect(m_updateManager, &UpdateManager::downloadFinished, 
            this, &UpdaterDialog::onDownloadFinished);
    connect(m_updateManager, &UpdateManager::downloadError, 
            this, &UpdaterDialog::onDownloadError);
    connect(m_updateManager, &UpdateManager::installationFinished, 
            this, &UpdaterDialog::onInstallationFinished);
    
    loadSkippedVersion();
    
    // Start checking for updates immediately
    checkForUpdates();
}

UpdaterDialog::~UpdaterDialog()
{
}

void UpdaterDialog::setupUI()
{
    setWindowTitle("EVE APM Preview - Auto Updater");
    setFixedSize(600, 600);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Logo
    m_logoLabel = new QLabel(this);
    QPixmap logoPixmap(":/logo.png");
    if (!logoPixmap.isNull()) {
        m_logoLabel->setPixmap(logoPixmap.scaled(300, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_logoLabel->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(m_logoLabel);
    }
    
    // Status label
    m_statusLabel = new QLabel("Checking for updates...", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    QFont statusFont = m_statusLabel->font();
    statusFont.setPointSize(12);
    statusFont.setBold(true);
    m_statusLabel->setFont(statusFont);
    mainLayout->addWidget(m_statusLabel);
    
    // Version label
    m_versionLabel = new QLabel("", this);
    m_versionLabel->setAlignment(Qt::AlignCenter);
    QFont versionFont = m_versionLabel->font();
    versionFont.setPointSize(10);
    m_versionLabel->setFont(versionFont);
    mainLayout->addWidget(m_versionLabel);
    
    // Release notes
    QLabel* notesLabel = new QLabel("Release Notes:", this);
    notesLabel->setStyleSheet("color: #FFD700; font-weight: bold;");
    mainLayout->addWidget(notesLabel);
    
    m_releaseNotesText = new QTextEdit(this);
    m_releaseNotesText->setReadOnly(true);
    m_releaseNotesText->setMinimumHeight(150);
    mainLayout->addWidget(m_releaseNotesText);
    
    // Progress bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    m_progressBar->setTextVisible(true);
    mainLayout->addWidget(m_progressBar);
    
    // Spacer
    mainLayout->addStretch();
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    
    m_installButton = new QPushButton("Install Update", this);
    m_installButton->setEnabled(false);
    m_installButton->setMinimumHeight(35);
    connect(m_installButton, &QPushButton::clicked, this, &UpdaterDialog::installUpdate);
    buttonLayout->addWidget(m_installButton);
    
    m_skipButton = new QPushButton("Skip", this);
    m_skipButton->setEnabled(false);
    m_skipButton->setMinimumHeight(35);
    connect(m_skipButton, &QPushButton::clicked, this, &UpdaterDialog::skipUpdate);
    buttonLayout->addWidget(m_skipButton);
    
    m_closeButton = new QPushButton("Close", this);
    m_closeButton->setMinimumHeight(35);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
    buttonLayout->addWidget(m_closeButton);
    
    mainLayout->addLayout(buttonLayout);
}

void UpdaterDialog::applyDarkTheme()
{
    QString styleSheet = R"(
        QDialog {
            background-color: #1E1E1E;
            color: #FFFFFF;
        }
        
        QLabel {
            color: #FFFFFF;
            background-color: transparent;
        }
        
        QTextEdit {
            background-color: #2D2D30;
            color: #FFFFFF;
            border: 1px solid #3E3E42;
            border-radius: 4px;
            padding: 8px;
            selection-background-color: #FFD700;
            selection-color: #000000;
        }
        
        QProgressBar {
            border: 1px solid #3E3E42;
            border-radius: 4px;
            background-color: #2D2D30;
            text-align: center;
            color: #FFFFFF;
            height: 24px;
        }
        
        QProgressBar::chunk {
            background-color: #FFD700;
            border-radius: 3px;
        }
        
        QPushButton {
            background-color: #3E3E42;
            color: #FFFFFF;
            border: 1px solid #555555;
            border-radius: 4px;
            padding: 8px 16px;
            font-weight: bold;
        }
        
        QPushButton:hover {
            background-color: #505050;
            border: 1px solid #FFD700;
        }
        
        QPushButton:pressed {
            background-color: #2D2D30;
        }
        
        QPushButton:disabled {
            background-color: #2D2D30;
            color: #666666;
            border: 1px solid #3E3E42;
        }
        
        QPushButton#installButton {
            background-color: #FFD700;
            color: #000000;
            border: 1px solid #FFD700;
        }
        
        QPushButton#installButton:hover {
            background-color: #FFED4E;
            border: 1px solid #FFED4E;
        }
        
        QPushButton#installButton:pressed {
            background-color: #CCA800;
        }
        
        QPushButton#installButton:disabled {
            background-color: #2D2D30;
            color: #666666;
            border: 1px solid #3E3E42;
        }
    )";
    
    setStyleSheet(styleSheet);
    m_installButton->setObjectName("installButton");
}

void UpdaterDialog::checkForUpdates()
{
    m_statusLabel->setText("Checking for updates...");
    m_versionLabel->setText("");
    m_releaseNotesText->clear();
    m_updateManager->checkForUpdates();
}

void UpdaterDialog::onUpdateCheckFinished(bool updateAvailable, const QString& latestVersion, const QString& releaseNotes)
{
    m_latestVersion = latestVersion;
    
    // Check if this version was skipped
    QString skippedVersion = getSkippedVersion();
    bool isSkipped = (!skippedVersion.isEmpty() && skippedVersion == latestVersion);
    
    // If version is skipped in autoLaunch mode, treat as no update
    if (updateAvailable && m_autoLaunch && isSkipped) {
        updateAvailable = false;
    }
    
    // Emit signal for auto-launch mode
    emit updateCheckComplete(updateAvailable);
    
    if (updateAvailable) {
        m_statusLabel->setText("Update Available!");
        m_statusLabel->setStyleSheet("color: #FFD700;");
        m_versionLabel->setText(QString("New Version: %1").arg(latestVersion));
        m_releaseNotesText->setPlainText(releaseNotes);
        
        m_installButton->setEnabled(true);
        m_skipButton->setEnabled(true);
        
        if (m_silentMode && !m_autoLaunch) {
            // In silent mode (but not autoLaunch), automatically install
            installUpdate();
        }
    } else {
        m_statusLabel->setText("Your software is up to date!");
        m_statusLabel->setStyleSheet("color: #00FF00;");
        m_versionLabel->setText(QString("Current Version: %1").arg(latestVersion));
        m_releaseNotesText->setPlainText("No updates available at this time.");
        
        if (m_silentMode) {
            QApplication::quit();
        }
    }
}

void UpdaterDialog::onUpdateCheckError(const QString& error)
{
    // Emit signal indicating no update (treat error as no update for auto-launch)
    emit updateCheckComplete(false);
    
    m_statusLabel->setText("Error checking for updates");
    m_statusLabel->setStyleSheet("color: #FF4444;");
    m_releaseNotesText->setPlainText(QString("Error: %1\n\nPlease check your internet connection and try again.").arg(error));
    
    if (m_silentMode) {
        QApplication::quit();
    }
}

void UpdaterDialog::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal > 0) {
        int percentage = static_cast<int>((bytesReceived * 100) / bytesTotal);
        m_progressBar->setValue(percentage);
        
        QString status = QString("Downloading: %1 / %2 (%3%)")
            .arg(formatFileSize(bytesReceived))
            .arg(formatFileSize(bytesTotal))
            .arg(percentage);
        m_statusLabel->setText(status);
    }
}

void UpdaterDialog::onDownloadFinished()
{
    m_statusLabel->setText("Download complete! Installing...");
    m_progressBar->setValue(100);
}

void UpdaterDialog::onDownloadError(const QString& error)
{
    m_statusLabel->setText("Download failed!");
    m_statusLabel->setStyleSheet("color: #FF4444;");
    m_progressBar->setVisible(false);
    
    QMessageBox::critical(this, "Download Error", 
        QString("Failed to download update:\n%1").arg(error));
    
    m_installButton->setEnabled(true);
    m_skipButton->setEnabled(true);
    m_closeButton->setEnabled(true);
}

void UpdaterDialog::onInstallationFinished(bool success, const QString& message)
{
    if (success) {
        m_statusLabel->setText("Update installed successfully!");
        m_statusLabel->setStyleSheet("color: #00FF00;");
        
        // Clear skipped version since update was installed
        saveSkippedVersion("");
        
        if (m_silentMode || m_autoLaunch) {
            // Launch application and exit
            launchApplication();
            QApplication::quit();
        } else {
            QMessageBox::information(this, "Update Complete",
                "The update has been installed successfully.\n\nThe application will now launch.");
            launchApplication();
            QApplication::quit();
        }
    } else {
        m_statusLabel->setText("Installation failed!");
        m_statusLabel->setStyleSheet("color: #FF4444;");
        
        QMessageBox::critical(this, "Installation Error", 
            QString("Failed to install update:\n%1").arg(message));
        
        m_installButton->setEnabled(true);
        m_skipButton->setEnabled(true);
        m_closeButton->setEnabled(true);
    }
}

void UpdaterDialog::installUpdate()
{
    m_installButton->setEnabled(false);
    m_skipButton->setEnabled(false);
    m_closeButton->setEnabled(false);
    
    m_statusLabel->setText("Starting download...");
    m_statusLabel->setStyleSheet("color: #00C8FF;");
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    
    m_updateManager->downloadAndInstallUpdate();
}

void UpdaterDialog::skipUpdate()
{
    // Save the skipped version
    saveSkippedVersion(m_latestVersion);
    
    // If in autoLaunch mode, launch the application
    if (m_autoLaunch) {
        launchApplication();
    }
    
    QApplication::quit();
}

QString UpdaterDialog::formatFileSize(qint64 bytes)
{
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;
    
    if (bytes >= GB) {
        return QString("%1 GB").arg(QString::number(bytes / static_cast<double>(GB), 'f', 2));
    } else if (bytes >= MB) {
        return QString("%1 MB").arg(QString::number(bytes / static_cast<double>(MB), 'f', 2));
    } else if (bytes >= KB) {
        return QString("%1 KB").arg(QString::number(bytes / static_cast<double>(KB), 'f', 2));
    } else {
        return QString("%1 bytes").arg(bytes);
    }
}

void UpdaterDialog::loadSkippedVersion()
{
    // Load skipped version from file
    QString appPath = QCoreApplication::applicationDirPath();
    QString skipFile = appPath + "/skipped_version.txt";
    
    QFile file(skipFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString version = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
    }
}

void UpdaterDialog::saveSkippedVersion(const QString& version)
{
    QString appPath = QCoreApplication::applicationDirPath();
    QString skipFile = appPath + "/skipped_version.txt";
    
    if (version.isEmpty()) {
        // Clear skipped version
        QFile::remove(skipFile);
    } else {
        // Save skipped version
        QFile file(skipFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(version.toUtf8());
            file.close();
        }
    }
}

QString UpdaterDialog::getSkippedVersion()
{
    QString appPath = QCoreApplication::applicationDirPath();
    QString skipFile = appPath + "/skipped_version.txt";
    
    QFile file(skipFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString version = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
        return version;
    }
    
    return QString();
}

void UpdaterDialog::launchApplication()
{
    QString appPath = QCoreApplication::applicationDirPath();
    QString exePath = appPath + "/EVEAPMPreview.exe";
    
    if (QFile::exists(exePath)) {
        QProcess::startDetached(exePath, QStringList(), appPath);
    }
}
