#include "updaterdialog.h"
#include <QApplication>
#include <QProcess>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    // Check for command line arguments
    bool silentMode = false;
    bool autoLaunch = false;
    
    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromLocal8Bit(argv[i]).toLower();
        if (arg == "-silent") {
            silentMode = true;
        } else if (arg == "-autolaunch") {
            autoLaunch = true;
        }
    }
    
    UpdaterDialog w(silentMode, autoLaunch);
    
    if (autoLaunch) {
        // Connect to know when update check is finished
        QObject::connect(&w, &UpdaterDialog::updateCheckComplete, [&](bool updateAvailable) {
            if (!updateAvailable) {
                // No update available, launch EVEAPMPreview.exe and exit
                QString appPath = QCoreApplication::applicationDirPath();
                QString exePath = appPath + "/EVEAPMPreview.exe";
                
                if (QFile::exists(exePath)) {
                    QProcess::startDetached(exePath, QStringList(), appPath);
                }
                
                QApplication::quit();
            } else {
                // Update available, show the dialog
                w.show();
            }
        });
    } else if (!silentMode) {
        w.show();
    }
    
    return a.exec();
}
