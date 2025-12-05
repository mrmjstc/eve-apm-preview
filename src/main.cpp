#include "hotkeymanager.h"
#include "mainwindow.h"
#include "version.h"
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QMessageBox>
#include <Windows.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

class HotkeyEventFilter : public QAbstractNativeEventFilter {
public:
  bool nativeEventFilter(const QByteArray & /*eventType*/, void *message,
                         qintptr *result) override {
    return HotkeyManager::nativeEventFilter(message,
                                            reinterpret_cast<long *>(result));
  }
};

int main(int argc, char *argv[]) {
  HANDLE hMutex =
      CreateMutexW(nullptr, TRUE, L"Global\\EVE-APM-Preview-SingleInstance");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (hMutex) {
      CloseHandle(hMutex);
    }
    return 0;
  }

  SetProcessDPIAware();

  BOOL compositionEnabled = FALSE;
  if (FAILED(DwmIsCompositionEnabled(&compositionEnabled)) ||
      !compositionEnabled) {
    QApplication tempApp(argc, argv);
    QMessageBox::critical(nullptr, "DWM Required",
                          "This application requires Desktop Window Manager "
                          "(DWM) to be enabled.\n\n"
                          "DWM is available on Windows Vista and later, and is "
                          "always enabled on Windows 8+.\n"
                          "Please ensure DWM composition is enabled or upgrade "
                          "your operating system.");
    return 1;
  }

  QApplication app(argc, argv);

  app.setApplicationName("EVE-APM Preview");
  app.setApplicationVersion(APP_VERSION);
  app.setOrganizationName("EVEAPMPreview");

  QIcon icon(":/bee.png");
  if (!icon.isNull()) {
    app.setWindowIcon(icon);
  }

  app.setQuitOnLastWindowClosed(false);

  HotkeyEventFilter hotkeyFilter;
  app.installNativeEventFilter(&hotkeyFilter);

  MainWindow manager;

  int exitCode = app.exec();

  if (hMutex) {
    CloseHandle(hMutex);
  }

  return exitCode;
}
