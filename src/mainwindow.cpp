#include "mainwindow.h"
#include "chatlogreader.h"
#include "config.h"
#include "configdialog.h"
#include "hotkeymanager.h"
#include "overlayinfo.h"
#include "protocolhandler.h"
#include "thumbnailwidget.h"
#include "windowcapture.h"
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QScreen>
#include <QSet>
#include <QUrl>
#include <algorithm>

static const QString NOT_LOGGED_IN_TEXT = QStringLiteral("Not Logged In");
static const QString SETTINGS_TEXT = QStringLiteral("Settings");
static const QString EXIT_TEXT = QStringLiteral("Exit");
static const QString EVEO_PREVIEW_TEXT = QStringLiteral("EVE-APM Preview");
static const QString EVE_TEXT = QStringLiteral("EVE");

QPointer<MainWindow> MainWindow::s_instance;

MainWindow::MainWindow(QObject *parent)
    : QObject(parent), windowCapture(std::make_unique<WindowCapture>()),
      hotkeyManager(std::make_unique<HotkeyManager>()),
      m_soundEffect(std::make_unique<QSoundEffect>()),
      m_notLoggedInCycleIndex(-1), m_nonEVECycleIndex(-1) {
  s_instance = this;

  connect(hotkeyManager.get(), &HotkeyManager::characterHotkeyPressed, this,
          &MainWindow::activateCharacter);
  connect(hotkeyManager.get(), &HotkeyManager::characterHotkeyCyclePressed,
          this, &MainWindow::handleCharacterHotkeyCycle);
  connect(hotkeyManager.get(), &HotkeyManager::namedCycleForwardPressed, this,
          &MainWindow::handleNamedCycleForward);
  connect(hotkeyManager.get(), &HotkeyManager::namedCycleBackwardPressed, this,
          &MainWindow::handleNamedCycleBackward);
  connect(hotkeyManager.get(), &HotkeyManager::notLoggedInCycleForwardPressed,
          this, &MainWindow::handleNotLoggedInCycleForward);
  connect(hotkeyManager.get(), &HotkeyManager::notLoggedInCycleBackwardPressed,
          this, &MainWindow::handleNotLoggedInCycleBackward);
  connect(hotkeyManager.get(), &HotkeyManager::nonEVECycleForwardPressed, this,
          &MainWindow::handleNonEVECycleForward);
  connect(hotkeyManager.get(), &HotkeyManager::nonEVECycleBackwardPressed, this,
          &MainWindow::handleNonEVECycleBackward);
  connect(hotkeyManager.get(), &HotkeyManager::profileSwitchRequested, this,
          &MainWindow::handleProfileSwitch);
  connect(hotkeyManager.get(), &HotkeyManager::suspendedChanged, this,
          &MainWindow::onHotkeysSuspendedChanged);
  connect(hotkeyManager.get(), &HotkeyManager::closeAllClientsRequested, this,
          &MainWindow::closeAllEVEClients);
  connect(hotkeyManager.get(), &HotkeyManager::minimizeAllClientsRequested,
          this, &MainWindow::minimizeAllEVEClients);
  connect(hotkeyManager.get(),
          &HotkeyManager::toggleThumbnailsVisibilityRequested, this,
          &MainWindow::toggleThumbnailsVisibility);
  connect(hotkeyManager.get(), &HotkeyManager::cycleProfileForwardRequested,
          this, &MainWindow::handleCycleProfileForward);
  connect(hotkeyManager.get(), &HotkeyManager::cycleProfileBackwardRequested,
          this, &MainWindow::handleCycleProfileBackward);

  refreshTimer = new QTimer(this);
  connect(refreshTimer, &QTimer::timeout, this, &MainWindow::refreshWindows);
  refreshTimer->start(60000);

  minimizeTimer = new QTimer(this);
  minimizeTimer->setSingleShot(true);
  connect(minimizeTimer, &QTimer::timeout, this,
          &MainWindow::minimizeInactiveWindows);

  m_cycleThrottleTimer = new QTimer(this);
  m_cycleThrottleTimer->setSingleShot(true);
  m_cycleThrottleTimer->setInterval(30);

  m_eveFocusDebounceTimer = new QTimer(this);
  m_eveFocusDebounceTimer->setSingleShot(true);
  m_eveFocusDebounceTimer->setInterval(100);
  connect(m_eveFocusDebounceTimer, &QTimer::timeout, this,
          &MainWindow::processEVEFocusChange);

  m_trayMenu = new QMenu();

  QAction *settingsAction = new QAction(SETTINGS_TEXT, this);
  connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
  m_trayMenu->addAction(settingsAction);

  m_profilesMenu = new QMenu("Profiles", m_trayMenu);
  m_trayMenu->addMenu(m_profilesMenu);
  updateProfilesMenu();

  m_suspendHotkeysAction = new QAction("Suspend Hotkeys", this);
  m_suspendHotkeysAction->setCheckable(true);
  m_suspendHotkeysAction->setChecked(false);
  connect(m_suspendHotkeysAction, &QAction::triggered, this,
          &MainWindow::toggleSuspendHotkeys);
  m_trayMenu->addAction(m_suspendHotkeysAction);

  m_hideThumbnailsAction = new QAction("Hide Thumbnails", this);
  m_hideThumbnailsAction->setCheckable(true);
  m_hideThumbnailsAction->setChecked(false);
  connect(m_hideThumbnailsAction, &QAction::triggered, this,
          &MainWindow::toggleThumbnailsVisibility);
  m_trayMenu->addAction(m_hideThumbnailsAction);

  m_trayMenu->addSeparator();

  QAction *restartAction = new QAction("Reload", this);
  connect(restartAction, &QAction::triggered, this,
          &MainWindow::reloadThumbnails);
  m_trayMenu->addAction(restartAction);

  QAction *exitAction = new QAction(EXIT_TEXT, this);
  connect(exitAction, &QAction::triggered, this, &MainWindow::exitApplication);
  m_trayMenu->addAction(exitAction);

  m_trayIcon = new QSystemTrayIcon(this);
  m_trayIcon->setContextMenu(m_trayMenu);
  m_trayIcon->setToolTip(EVEO_PREVIEW_TEXT);

  QIcon beeIcon(":/bee.png");
  if (!beeIcon.isNull()) {
    m_trayIcon->setIcon(beeIcon);
  } else {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setBrush(QColor("#00c8ff"));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(4, 4, 56, 56);

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPixelSize(20);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, EVE_TEXT);

    m_trayIcon->setIcon(QIcon(pixmap));
  }

  m_trayIcon->show();

  connect(m_trayIcon, &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick) {
              showSettings();
            }
          });

  m_eventHook = SetWinEventHook(
      EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc,
      0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  m_createHook =
      SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr,
                      WindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  m_destroyHook =
      SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, nullptr,
                      WindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  m_showHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                               WindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  m_nameChangeHook =
      SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr,
                      WindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  m_locationHook =
      SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                      nullptr, WindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  m_minimizeStartHook =
      SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZESTART,
                      nullptr, WindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  m_minimizeEndHook =
      SetWinEventHook(EVENT_SYSTEM_MINIMIZEEND, EVENT_SYSTEM_MINIMIZEEND,
                      nullptr, WindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  m_moveSizeStartHook =
      SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART,
                      nullptr, WindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  m_moveSizeEndHook =
      SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND,
                      nullptr, WindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  m_chatLogReader = std::make_unique<ChatLogReader>();

  const Config &cfgChatLog = Config::instance();
  QString chatLogDirectory = cfgChatLog.chatLogDirectory();
  QString gameLogDirectory = cfgChatLog.gameLogDirectory();
  m_chatLogReader->setLogDirectory(chatLogDirectory);
  m_chatLogReader->setGameLogDirectory(gameLogDirectory);

  bool enableChatLog = cfgChatLog.enableChatLogMonitoring();
  bool enableGameLog = cfgChatLog.enableGameLogMonitoring();
  m_chatLogReader->setEnableChatLogMonitoring(enableChatLog);
  m_chatLogReader->setEnableGameLogMonitoring(enableGameLog);

  QDir chatLogDir(chatLogDirectory);
  if (chatLogDir.exists()) {
    qDebug() << "ChatLog: Chatlog directory found:" << chatLogDirectory;
  } else {
    qDebug() << "ChatLog: Chatlog directory not found:" << chatLogDirectory;
  }

  QDir gameLogDir(gameLogDirectory);
  if (gameLogDir.exists()) {
    qDebug() << "ChatLog: Gamelog directory found:" << gameLogDirectory;
  } else {
    qDebug() << "ChatLog: Gamelog directory not found:" << gameLogDirectory;
  }

  connect(m_chatLogReader.get(), &ChatLogReader::systemChanged, this,
          &MainWindow::onCharacterSystemChanged);
  connect(m_chatLogReader.get(), &ChatLogReader::combatEventDetected, this,
          &MainWindow::onCombatEventDetected);

  if (enableChatLog || enableGameLog) {
    m_chatLogReader->start();
    qDebug() << "ChatLog: Monitoring started (ChatLog:" << enableChatLog
             << ", GameLog:" << enableGameLog << ")";
  } else {
    qDebug() << "ChatLog: Monitoring disabled in config";
  }

  // Initialize protocol handler
  m_protocolHandler = std::make_unique<ProtocolHandler>(this);

  // Connect protocol handler signals
  connect(m_protocolHandler.get(), &ProtocolHandler::profileRequested, this,
          &MainWindow::handleProtocolProfileSwitch);
  connect(m_protocolHandler.get(), &ProtocolHandler::characterRequested, this,
          &MainWindow::handleProtocolCharacterActivation);
  connect(m_protocolHandler.get(), &ProtocolHandler::hotkeySuspendRequested,
          this, &MainWindow::handleProtocolHotkeySuspend);
  connect(m_protocolHandler.get(), &ProtocolHandler::hotkeyResumeRequested,
          this, &MainWindow::handleProtocolHotkeyResume);
  connect(m_protocolHandler.get(), &ProtocolHandler::thumbnailHideRequested,
          this, &MainWindow::handleProtocolThumbnailHide);
  connect(m_protocolHandler.get(), &ProtocolHandler::thumbnailShowRequested,
          this, &MainWindow::handleProtocolThumbnailShow);
  connect(m_protocolHandler.get(), &ProtocolHandler::configOpenRequested, this,
          &MainWindow::handleProtocolConfigOpen);
  connect(m_protocolHandler.get(), &ProtocolHandler::invalidUrl, this,
          &MainWindow::handleProtocolError);

  // Always register/update protocol to ensure it points to current executable
  bool needsRegistration = !m_protocolHandler->isProtocolRegistered();

  if (!needsRegistration) {
    // Check if registered path matches current executable
    QString currentExe = QCoreApplication::applicationFilePath();
    QString registeredCommand = m_protocolHandler->getRegistryValue(
        "HKEY_CURRENT_USER\\Software\\Classes\\eveapm\\shell\\open\\command",
        "", "");

    // Check if the registered command contains the current executable path
    if (!registeredCommand.contains(currentExe, Qt::CaseInsensitive)) {
      qDebug() << "Protocol points to different executable, re-registering...";
      qDebug() << "Current exe:" << currentExe;
      qDebug() << "Registered command:" << registeredCommand;
      needsRegistration = true;
    }
  }

  if (needsRegistration) {
    qDebug() << "Registering protocol...";
    if (m_protocolHandler->registerProtocol()) {
      qDebug() << "Protocol registered successfully";
    } else {
      qWarning() << "Failed to register protocol";
    }
  } else {
    qDebug() << "Protocol already registered to current executable";
  }

  // Set up IPC server for single-instance communication
  m_ipcServer = std::make_unique<QLocalServer>(this);

  // Remove any existing server (in case of crash)
  QLocalServer::removeServer("EVE-APM-Preview-IPC");

  if (m_ipcServer->listen("EVE-APM-Preview-IPC")) {
    connect(m_ipcServer.get(), &QLocalServer::newConnection, this,
            &MainWindow::handleIpcConnection);
    qDebug() << "IPC server started for protocol handling";
  } else {
    qWarning() << "Failed to start IPC server:" << m_ipcServer->errorString();
  }

  hotkeyManager->registerHotkeys();

  // Initialize visibility context before any visibility checks
  updateVisibilityContext();

  refreshWindows();
}

MainWindow::~MainWindow() {
  s_instance.clear();

  if (m_chatLogReader) {
    m_chatLogReader->stop();
  }

  if (windowCapture) {
    windowCapture->clearCache();
  }
  OverlayInfo::clearCache();

  if (m_eventHook) {
    UnhookWinEvent(m_eventHook);
  }
  if (m_createHook) {
    UnhookWinEvent(m_createHook);
  }
  if (m_destroyHook) {
    UnhookWinEvent(m_destroyHook);
  }
  if (m_showHook) {
    UnhookWinEvent(m_showHook);
  }
  if (m_nameChangeHook) {
    UnhookWinEvent(m_nameChangeHook);
  }
  if (m_locationHook) {
    UnhookWinEvent(m_locationHook);
  }
  if (m_minimizeStartHook) {
    UnhookWinEvent(m_minimizeStartHook);
  }
  if (m_minimizeEndHook) {
    UnhookWinEvent(m_minimizeEndHook);
  }
  if (m_moveSizeStartHook) {
    UnhookWinEvent(m_moveSizeStartHook);
  }
  if (m_moveSizeEndHook) {
    UnhookWinEvent(m_moveSizeEndHook);
  }

  for (auto it = m_locationRefreshTimers.begin();
       it != m_locationRefreshTimers.end(); ++it) {
    if (it.value()) {
      it.value()->stop();
      it.value()->deleteLater();
    }
  }
  m_locationRefreshTimers.clear();

  for (ThumbnailWidget *thumbnail : thumbnails) {
    if (thumbnail) {
      thumbnail->closeImmediately();
    }
  }

  qDeleteAll(thumbnails);
  thumbnails.clear();
}

void CALLBACK MainWindow::WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG,
                                       DWORD, DWORD) {
  if (!s_instance.isNull()) {
    QMetaObject::invokeMethod(s_instance, "updateActiveWindow",
                              Qt::QueuedConnection);
  }
}

void CALLBACK MainWindow::WindowEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                          LONG idObject, LONG idChild, DWORD,
                                          DWORD) {
  if (idObject != OBJID_WINDOW) {
    return;
  }

  if (!s_instance.isNull()) {
    if (event == EVENT_OBJECT_LOCATIONCHANGE) {
      if (!s_instance->thumbnails.contains(hwnd)) {
        return;
      }
    }

    if (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_DESTROY) {
      s_instance->m_needsEnumeration = true;
      QMetaObject::invokeMethod(s_instance.data(), "refreshWindows",
                                Qt::QueuedConnection);
    } else if (event == EVENT_OBJECT_NAMECHANGE) {
      if (s_instance->thumbnails.contains(hwnd)) {
        QMetaObject::invokeMethod(
            s_instance.data(),
            [hwnd]() {
              if (!s_instance.isNull()) {
                s_instance->handleWindowTitleChange(hwnd);
              }
            },
            Qt::QueuedConnection);
      }
    } else if (event == EVENT_OBJECT_SHOW) {
      if (hwnd == s_instance->m_hwndPendingRefresh) {
        QMetaObject::invokeMethod(
            s_instance.data(),
            [hwnd]() {
              if (!s_instance.isNull()) {
                s_instance->refreshSingleThumbnail(hwnd);
                s_instance->m_hwndPendingRefresh = nullptr;
              }
            },
            Qt::QueuedConnection);
      }
    } else if (event == EVENT_OBJECT_LOCATIONCHANGE) {
      if (s_instance->thumbnails.contains(hwnd)) {
        QMetaObject::invokeMethod(
            s_instance.data(),
            [hwnd]() {
              if (!s_instance.isNull()) {
                if (!s_instance->m_windowsBeingMoved.value(hwnd, false)) {
                  s_instance->scheduleLocationRefresh(hwnd);
                }
              }
            },
            Qt::QueuedConnection);
      }
    } else if (event == EVENT_SYSTEM_MOVESIZESTART) {
      if (s_instance->thumbnails.contains(hwnd)) {
        QMetaObject::invokeMethod(
            s_instance.data(),
            [hwnd]() {
              if (!s_instance.isNull()) {
                s_instance->m_windowsBeingMoved[hwnd] = true;
              }
            },
            Qt::QueuedConnection);
      }
    } else if (event == EVENT_SYSTEM_MOVESIZEEND) {
      if (s_instance->thumbnails.contains(hwnd)) {
        QMetaObject::invokeMethod(
            s_instance.data(),
            [hwnd]() {
              if (!s_instance.isNull()) {
                s_instance->m_windowsBeingMoved.remove(hwnd);
                s_instance->refreshSingleThumbnail(hwnd);
              }
            },
            Qt::QueuedConnection);
      }
    } else if (event == EVENT_SYSTEM_MINIMIZESTART ||
               event == EVENT_SYSTEM_MINIMIZEEND) {
      if (s_instance->thumbnails.contains(hwnd)) {
        QMetaObject::invokeMethod(
            s_instance.data(),
            [hwnd]() {
              if (!s_instance.isNull()) {
                s_instance->refreshSingleThumbnail(hwnd);
              }
            },
            Qt::QueuedConnection);
      }
    }
  }
}

void MainWindow::refreshWindows() {
  const Config &cfg = Config::instance();
  const int thumbWidth = cfg.thumbnailWidth();
  const int thumbHeight = cfg.thumbnailHeight();
  const bool rememberPos = cfg.rememberPositions();
  const bool showNotLoggedIn = cfg.showNotLoggedInClients();
  const bool showNotLoggedInOverlay = cfg.showNotLoggedInOverlay();
  const bool showNonEVEOverlay = cfg.showNonEVEOverlay();
  const double thumbnailOpacity = cfg.thumbnailOpacity() / 100.0;

  QVector<WindowInfo> windows;

  if (m_needsEnumeration) {
    windows = windowCapture->getEVEWindows();
    m_needsEnumeration = false;
  } else {
    windows.reserve(thumbnails.size());
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
      HWND hwnd = it.key();

      if (!IsWindow(hwnd)) {
        m_needsEnumeration = true;
        windows = windowCapture->getEVEWindows();
        break;
      }

      QString title = m_lastKnownTitles.value(hwnd, "");

      if (title.isEmpty()) {
        title = windowCapture->getWindowTitle(hwnd);
        m_lastKnownTitles.insert(hwnd, title);
      }

      QString processName = m_windowProcessNames.value(hwnd, "exefile.exe");
      qint64 creationTime = m_windowCreationTimes.value(hwnd, 0);

      windows.append(WindowInfo(hwnd, title, processName, creationTime));
    }
  }

  QSet<HWND> currentWindows;
  QSet<HWND> newWindows;
  for (const auto &window : windows) {
    currentWindows.insert(window.handle);
    if (!thumbnails.contains(window.handle)) {
      newWindows.insert(window.handle);
    }
  }

  if (!newWindows.isEmpty()) {
    std::sort(windows.begin(), windows.end(),
              [](const WindowInfo &a, const WindowInfo &b) {
                return a.creationTime < b.creationTime;
              });
  }

  auto it = thumbnails.begin();
  while (it != thumbnails.end()) {
    if (!currentWindows.contains(it.key())) {
      HWND removedWindow = it.key();
      m_lastKnownTitles.remove(removedWindow);
      invalidateCycleIndicesForWindow(removedWindow);
      cleanupLocationRefreshTimer(removedWindow);
      m_clientLocationMoveAttempted.remove(removedWindow);
      m_clientLocationRetryCount.remove(removedWindow);
      it.value()->deleteLater();
      it = thumbnails.erase(it);
    } else {
      ++it;
    }
  }

  int loggedInClientsWithoutSavedPos = 0;
  for (const auto &window : windows) {
    QString charName = OverlayInfo::extractCharacterName(window.title);
    if (!charName.isEmpty()) {
      QPoint savedPos =
          rememberPos ? cfg.getThumbnailPosition(charName) : QPoint(-1, -1);

      if (thumbnails.contains(window.handle) &&
          (savedPos.x() < 0 || savedPos.y() < 0)) {
        loggedInClientsWithoutSavedPos++;
      }
    }
  }

  QScreen *primaryScreen = QGuiApplication::primaryScreen();
  QRect screenGeometry = primaryScreen->geometry();
  int screenWidth = screenGeometry.width();
  int screenHeight = screenGeometry.height();
  int margin = 10;

  int xOffset = margin;
  int yOffset = margin;
  int currentRow = 0;

  int thumbnailsPerRow = (screenWidth - margin) / (thumbWidth + margin);
  if (thumbnailsPerRow < 1)
    thumbnailsPerRow = 1;

  if (loggedInClientsWithoutSavedPos > 0) {
    currentRow = loggedInClientsWithoutSavedPos / thumbnailsPerRow;
    int colInRow = loggedInClientsWithoutSavedPos % thumbnailsPerRow;
    xOffset = margin + (colInRow * (thumbWidth + margin));
    yOffset = margin + (currentRow * (thumbHeight + margin));
  }

  int notLoggedInCount = 0;

  // Update visibility context once before processing all windows
  updateVisibilityContext();

  for (const auto &window : windows) {
    m_windowCreationTimes[window.handle] = window.creationTime;
    m_windowProcessNames[window.handle] = window.processName;

    bool isEVEClient =
        window.processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;

    QString characterName;
    QString displayName;

    if (isEVEClient) {
      characterName = OverlayInfo::extractCharacterName(window.title);

      if (characterName.isEmpty() && !showNotLoggedIn) {
        continue;
      }

      if (characterName.isEmpty() && showNotLoggedInOverlay) {
        displayName = NOT_LOGGED_IN_TEXT;
      } else {
        displayName = characterName;
      }
    } else {
      characterName = window.title;
      displayName = showNonEVEOverlay ? window.title : "";
    }

    ThumbnailWidget *thumbWidget = thumbnails.value(window.handle, nullptr);

    if (!thumbWidget) {
      int actualThumbWidth = thumbWidth;
      int actualThumbHeight = thumbHeight;

      if (isEVEClient && !characterName.isEmpty() &&
          cfg.hasCustomThumbnailSize(characterName)) {
        QSize customSize = cfg.getThumbnailSize(characterName);
        actualThumbWidth = customSize.width();
        actualThumbHeight = customSize.height();
      } else if (!isEVEClient &&
                 cfg.hasCustomProcessThumbnailSize(window.processName)) {
        QSize customSize = cfg.getProcessThumbnailSize(window.processName);
        actualThumbWidth = customSize.width();
        actualThumbHeight = customSize.height();
      }

      thumbWidget = new ThumbnailWidget(window.id, window.title, nullptr);
      thumbWidget->setFixedSize(actualThumbWidth, actualThumbHeight);

      // Hide by default - updateThumbnailVisibility() will show if appropriate
      thumbWidget->hide();

      thumbWidget->setCharacterName(displayName);
      thumbWidget->setWindowOpacity(thumbnailOpacity);
      thumbWidget->updateWindowFlags(cfg.alwaysOnTop());

      if (isEVEClient && !characterName.isEmpty()) {
        QString customName = cfg.getCustomThumbnailName(characterName);
        if (!customName.isEmpty()) {
          thumbWidget->setCustomName(customName);
        }

        QString cachedSystem = m_characterSystems.value(characterName);
        if (!cachedSystem.isEmpty()) {
          thumbWidget->setSystemName(cachedSystem);
        }
      }

      connect(thumbWidget, &ThumbnailWidget::clicked, this,
              &MainWindow::onThumbnailClicked);
      connect(thumbWidget, &ThumbnailWidget::positionChanged, this,
              &MainWindow::onThumbnailPositionChanged);
      connect(thumbWidget, &ThumbnailWidget::groupDragStarted, this,
              &MainWindow::onGroupDragStarted);
      connect(thumbWidget, &ThumbnailWidget::groupDragMoved, this,
              &MainWindow::onGroupDragMoved);
      connect(thumbWidget, &ThumbnailWidget::groupDragEnded, this,
              &MainWindow::onGroupDragEnded);

      thumbnails.insert(window.handle, thumbWidget);

      // Immediately update mapping cache for visibility logic to avoid cache
      // miss during initial updateThumbnailVisibility() call
      if (isEVEClient && !characterName.isEmpty()) {
        m_windowToCharacter[window.handle] = characterName;
      }

      m_needsMappingUpdate = true;

      updateSnappingLists();

      QPoint savedPos(-1, -1);
      bool hasSavedPosition = false;
      if (rememberPos) {
        if (isEVEClient && !characterName.isEmpty()) {
          savedPos = cfg.getThumbnailPosition(characterName);
          hasSavedPosition = (savedPos != QPoint(-1, -1));
        } else if (!isEVEClient) {
          // Use only process name as key for non-EVE apps to avoid issues with
          // dynamic window titles
          savedPos = cfg.getThumbnailPosition(window.processName);
          hasSavedPosition = (savedPos != QPoint(-1, -1));
        }
      }

      bool shouldHide = false;
      if (isEVEClient && !characterName.isEmpty()) {
        shouldHide = cfg.isCharacterHidden(characterName);
      }

      if (hasSavedPosition) {
        QRect thumbRect(savedPos, QSize(actualThumbWidth, actualThumbHeight));
        QScreen *targetScreen = nullptr;
        for (QScreen *screen : QGuiApplication::screens()) {
          if (screen->geometry().intersects(thumbRect)) {
            targetScreen = screen;
            break;
          }
        }

        if (targetScreen) {
          thumbWidget->move(savedPos);
        } else {
          if (isEVEClient && characterName.isEmpty()) {
            QPoint pos = calculateNotLoggedInPosition(notLoggedInCount);
            thumbWidget->move(pos);
            notLoggedInCount++;
          } else {
            if (xOffset + thumbWidth > screenWidth - margin) {
              xOffset = margin;
              yOffset += thumbHeight + margin;
            }

            if (yOffset + thumbHeight > screenHeight - margin) {
              yOffset = margin;
            }

            thumbWidget->move(xOffset, yOffset);
            xOffset += thumbWidth + margin;
          }
        }
      } else if (isEVEClient && characterName.isEmpty()) {
        QPoint pos = calculateNotLoggedInPosition(notLoggedInCount);
        thumbWidget->move(pos);
        notLoggedInCount++;
      } else {
        if (xOffset + thumbWidth > screenWidth - margin) {
          xOffset = margin;
          yOffset += thumbHeight + margin;
        }

        if (yOffset + thumbHeight > screenHeight - margin) {
          yOffset = margin;
        }

        thumbWidget->move(xOffset, yOffset);
        xOffset += thumbWidth + margin;
      }

      // Set initial visibility using centralized logic
      updateThumbnailVisibility(window.handle);
    } else {
      QString lastTitle = m_lastKnownTitles.value(window.handle, "");
      if (lastTitle != window.title) {
        m_lastKnownTitles.insert(window.handle, window.title);

        thumbWidget->setTitle(window.title);

        if (isEVEClient) {
          QString lastCharacterName =
              OverlayInfo::extractCharacterName(lastTitle);
          bool wasNotLoggedIn = lastCharacterName.isEmpty();
          bool isNowLoggedIn = !characterName.isEmpty();
          bool wasLoggedIn = !lastCharacterName.isEmpty();
          bool isNowNotLoggedIn = characterName.isEmpty();

          if (wasNotLoggedIn && isNowLoggedIn) {
            thumbWidget->setCharacterName(characterName);

            QString customName = cfg.getCustomThumbnailName(characterName);
            if (!customName.isEmpty()) {
              thumbWidget->setCustomName(customName);
            }

            m_characterToWindow[characterName] = window.handle;
            m_windowToCharacter[window.handle] = characterName;

            QString cachedSystem = m_characterSystems.value(characterName);
            if (!cachedSystem.isEmpty()) {
              thumbWidget->setSystemName(cachedSystem);
            } else {
              thumbWidget->setSystemName(QString());
            }

            tryRestoreClientLocation(window.handle, characterName);

            if (rememberPos) {
              QPoint savedPos = cfg.getThumbnailPosition(characterName);
              if (savedPos != QPoint(-1, -1)) {
                QRect thumbRect(savedPos, QSize(thumbWidth, thumbHeight));
                for (QScreen *screen : QGuiApplication::screens()) {
                  if (screen->geometry().intersects(thumbRect)) {
                    thumbWidget->move(savedPos);
                    break;
                  }
                }
              }
            }

            m_needsMappingUpdate = true;
          } else if (wasLoggedIn && isNowNotLoggedIn) {
            QString newDisplayName =
                showNotLoggedInOverlay ? NOT_LOGGED_IN_TEXT : "";
            thumbWidget->setCharacterName(newDisplayName);
            thumbWidget->setSystemName(QString());

            if (!cfg.preserveLogoutPositions()) {
              QPoint pos = calculateNotLoggedInPosition(notLoggedInCount);
              thumbWidget->move(pos);
              notLoggedInCount++;
            }
            m_needsMappingUpdate = true;
          }
        } else {
          thumbWidget->setCharacterName(showNonEVEOverlay ? window.title : "");
        }
      }

      // Determine if this thumbnail should be visible
      bool shouldHideThisThumbnail = false;

      if (isEVEClient && characterName.isEmpty()) {
        QString newDisplayName =
            showNotLoggedInOverlay ? NOT_LOGGED_IN_TEXT : "";
        thumbWidget->setCharacterName(newDisplayName);
        thumbWidget->setSystemName(QString());
      } else if (!isEVEClient) {
        thumbWidget->setCharacterName(showNonEVEOverlay ? window.title : "");
      } else if (isEVEClient && !characterName.isEmpty()) {
        // Re-extract character name from current window title to ensure
        // accuracy
        QString currentCharacterName = characterName;
        wchar_t titleBuf[256];
        if (GetWindowTextW(window.handle, titleBuf, 256) > 0) {
          QString freshTitle = QString::fromWCharArray(titleBuf);
          QString freshCharName = OverlayInfo::extractCharacterName(freshTitle);
          if (!freshCharName.isEmpty()) {
            currentCharacterName = freshCharName;
          }
        }

        if (cfg.isCharacterHidden(currentCharacterName)) {
          shouldHideThisThumbnail = true;
        }
      }

      // Apply visibility using centralized logic
      updateThumbnailVisibility(window.handle);
    }
  }

  updateSnappingLists();
  updateCharacterMappings();
  updateActiveWindow();
}

void MainWindow::updateSnappingLists() {
  if (thumbnails.size() != m_lastThumbnailListSize) {
    m_cachedThumbnailList = thumbnails.values().toVector();
    m_lastThumbnailListSize = thumbnails.size();

    for (auto *thumb : m_cachedThumbnailList) {
      thumb->setOtherThumbnails(m_cachedThumbnailList);
    }
  }
}

void MainWindow::updateCharacterMappings() {
  static int lastThumbnailCount = -1;
  if (thumbnails.size() == lastThumbnailCount && lastThumbnailCount > 0 &&
      !m_needsMappingUpdate) {
    return;
  }
  lastThumbnailCount = thumbnails.size();
  m_needsMappingUpdate = false;

  m_characterToWindow.clear();
  m_windowToCharacter.clear();
  m_notLoggedInWindows.clear();
  m_nonEVEWindows.clear();

  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    HWND hwnd = it.key();
    ThumbnailWidget *widget = it.value();

    QString processName = m_windowProcessNames.value(hwnd, "");
    bool isEVEClient =
        processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;

    if (!isEVEClient) {
      m_nonEVEWindows.append(hwnd);
      continue;
    }

    QString title = m_lastKnownTitles.value(hwnd, "");
    if (title.isEmpty()) {
      title = widget->property("windowTitle").toString();
    }

    QString characterName = OverlayInfo::extractCharacterName(title);

    if (!characterName.isEmpty()) {
      m_characterToWindow[characterName] = hwnd;
      m_windowToCharacter[hwnd] = characterName;
    } else {
      m_notLoggedInWindows.append(hwnd);
    }
  }

  static int lastNotLoggedInCount = 0;
  if (m_notLoggedInWindows.size() != lastNotLoggedInCount) {
    std::sort(m_notLoggedInWindows.begin(), m_notLoggedInWindows.end(),
              [this](HWND a, HWND b) {
                return m_windowCreationTimes.value(a, 0) <
                       m_windowCreationTimes.value(b, 0);
              });
    lastNotLoggedInCount = m_notLoggedInWindows.size();
  }

  static int lastNonEVECount = 0;
  if (m_nonEVEWindows.size() != lastNonEVECount) {
    std::sort(m_nonEVEWindows.begin(), m_nonEVEWindows.end(),
              [this](HWND a, HWND b) {
                return m_windowCreationTimes.value(a, 0) <
                       m_windowCreationTimes.value(b, 0);
              });
    lastNonEVECount = m_nonEVEWindows.size();
  }

  hotkeyManager->updateCharacterWindows(m_characterToWindow);

  const Config &cfgLog = Config::instance();
  if (m_chatLogReader &&
      (cfgLog.enableChatLogMonitoring() || cfgLog.enableGameLogMonitoring())) {
    QStringList characterNames = m_characterToWindow.keys();
    m_chatLogReader->setCharacterNames(characterNames);
  }
}

void MainWindow::refreshSingleThumbnail(HWND hwnd) {
  auto it = thumbnails.find(hwnd);
  if (it == thumbnails.end()) {
    return;
  }

  // Skip refresh if any thumbnail is currently being dragged to prevent flicker
  for (auto thumbIt = thumbnails.constBegin(); thumbIt != thumbnails.constEnd();
       ++thumbIt) {
    ThumbnailWidget *thumb = thumbIt.value();
    if (thumb && (thumb->isDragging() || thumb->isGroupDragging() ||
                  thumb->isMousePressed())) {
      return;
    }
  }

  ThumbnailWidget *thumbWidget = it.value();
  thumbWidget->forceUpdate();
}

/// Update the cached visibility context with current state
void MainWindow::updateVisibilityContext() {
  const Config &cfg = Config::instance();

  m_cachedVisibilityContext.activeWindow = GetForegroundWindow();
  m_cachedVisibilityContext.isEVEFocused =
      thumbnails.contains(m_cachedVisibilityContext.activeWindow);
  m_cachedVisibilityContext.hideWhenEVENotFocused =
      cfg.hideThumbnailsWhenEVENotFocused();
  m_cachedVisibilityContext.hideActive = cfg.hideActiveClientThumbnail();
  m_cachedVisibilityContext.manuallyHidden = m_thumbnailsManuallyHidden;
  m_cachedVisibilityContext.configDialogOpen = (m_configDialog != nullptr);

  // Check dragging state
  m_cachedVisibilityContext.anyThumbnailDragging = false;
  for (auto it = thumbnails.constBegin(); it != thumbnails.constEnd(); ++it) {
    ThumbnailWidget *thumb = it.value();
    if (thumb && (thumb->isDragging() || thumb->isGroupDragging() ||
                  thumb->isMousePressed())) {
      m_cachedVisibilityContext.anyThumbnailDragging = true;
      break;
    }
  }
}

/// Calculate whether a thumbnail should be visible based on current rules
/// Returns true if should be visible, false if should be hidden
bool MainWindow::calculateThumbnailVisibility(HWND hwnd) {
  ThumbnailWidget *thumbnail = thumbnails.value(hwnd, nullptr);
  if (!thumbnail) {
    return false;
  }

  const Config &cfg = Config::instance();

  // Use cached context (updated by caller)
  const HWND activeWindow = m_cachedVisibilityContext.activeWindow;
  const bool isEVEFocused = m_cachedVisibilityContext.isEVEFocused;
  const bool hideWhenEVENotFocused =
      m_cachedVisibilityContext.hideWhenEVENotFocused;
  const bool hideActive = m_cachedVisibilityContext.hideActive;
  const bool anyThumbnailDragging =
      m_cachedVisibilityContext.anyThumbnailDragging;
  const bool manuallyHidden = m_cachedVisibilityContext.manuallyHidden;
  const bool configDialogOpen = m_cachedVisibilityContext.configDialogOpen;

  // Get current window info
  QString processName = m_windowProcessNames.value(hwnd, "");
  bool isEVEClient =
      processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;

  // Extract character name - use cache first, query only if empty
  QString characterName = m_windowToCharacter.value(hwnd);
  if (isEVEClient && characterName.isEmpty()) {
    // Cache miss - query window title
    wchar_t titleBuf[256];
    if (GetWindowTextW(hwnd, titleBuf, 256) > 0) {
      QString currentTitle = QString::fromWCharArray(titleBuf);
      characterName = OverlayInfo::extractCharacterName(currentTitle);
    }
  }

  // Apply visibility rules in priority order
  bool shouldShow = true;

  // Rule 1: Manual hide (highest priority)
  if (manuallyHidden) {
    shouldShow = false;
  }
  // Rule 2: Character-specific hiding
  else if (isEVEClient && !characterName.isEmpty() &&
           cfg.isCharacterHidden(characterName)) {
    shouldShow = false;
  }
  // Rule 3: Hide when EVE not focused
  else if (hideWhenEVENotFocused && !isEVEFocused && !configDialogOpen) {
    if (!anyThumbnailDragging) {
      shouldShow = false;
    }
  }
  // Rule 4: Hide active client
  if (shouldShow && hideActive) {
    if (hwnd == activeWindow) {
      shouldShow = false;
    }
  }

  return shouldShow;
}

/// Update single thumbnail visibility (used for individual updates)
void MainWindow::updateThumbnailVisibility(HWND hwnd) {
  ThumbnailWidget *thumbnail = thumbnails.value(hwnd, nullptr);
  if (!thumbnail) {
    return;
  }

  bool shouldShow = calculateThumbnailVisibility(hwnd);

  // Apply visibility
  if (shouldShow) {
    // Only call show() if not already visible to prevent flicker
    if (!thumbnail->isVisible()) {
      thumbnail->show();
    }
  } else {
    // Only call hide() if currently visible
    if (thumbnail->isVisible()) {
      thumbnail->hide();
    }
  }
}

/// Update all thumbnails' visibility using shared state for efficiency
void MainWindow::updateAllThumbnailsVisibility() {
  if (thumbnails.isEmpty()) {
    return;
  }

  // Update cached context once
  updateVisibilityContext();

  // Skip visibility updates entirely if any thumbnail is being dragged
  if (m_cachedVisibilityContext.anyThumbnailDragging) {
    return;
  }

  // Batch visibility changes to make them appear/disappear simultaneously
  // First pass: collect visibility decisions without triggering window updates
  QVector<QPair<HWND, bool>> visibilityChanges;
  visibilityChanges.reserve(thumbnails.size());

  for (auto it = thumbnails.constBegin(); it != thumbnails.constEnd(); ++it) {
    HWND hwnd = it.key();
    ThumbnailWidget *thumbnail = it.value();
    if (!thumbnail) {
      continue;
    }

    bool shouldShow = calculateThumbnailVisibility(hwnd);

    // Only add to batch if visibility needs to change
    if (shouldShow != thumbnail->isVisible()) {
      visibilityChanges.append(qMakePair(hwnd, shouldShow));
    }
  }

  // Second pass: apply all visibility changes at once
  for (const auto &change : visibilityChanges) {
    ThumbnailWidget *thumbnail = thumbnails.value(change.first, nullptr);
    if (thumbnail) {
      if (change.second) {
        thumbnail->show();
      } else {
        thumbnail->hide();
      }
    }
  }
}

void MainWindow::handleWindowTitleChange(HWND hwnd) {
  if (!IsWindow(hwnd) || !thumbnails.contains(hwnd)) {
    return;
  }

  QString newTitle = windowCapture->getWindowTitle(hwnd);
  QString lastTitle = m_lastKnownTitles.value(hwnd, "");

  if (lastTitle == newTitle) {
    return;
  }

  m_lastKnownTitles.insert(hwnd, newTitle);
  ThumbnailWidget *thumbWidget = thumbnails[hwnd];
  thumbWidget->setTitle(newTitle);

  QString processName = m_windowProcessNames.value(hwnd, "");
  bool isEVEClient =
      processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;

  if (!isEVEClient) {
    const Config &cfgOverlay = Config::instance();
    if (cfgOverlay.showNonEVEOverlay()) {
      thumbWidget->setCharacterName(newTitle);
    }
    return;
  }

  QString lastCharacterName = OverlayInfo::extractCharacterName(lastTitle);
  QString newCharacterName = OverlayInfo::extractCharacterName(newTitle);

  bool wasNotLoggedIn = lastCharacterName.isEmpty();
  bool isNowLoggedIn = !newCharacterName.isEmpty();
  bool wasLoggedIn = !lastCharacterName.isEmpty();
  bool isNowNotLoggedIn = newCharacterName.isEmpty();

  const Config &cfg = Config::instance();

  if (wasNotLoggedIn && isNowLoggedIn) {
    m_characterToWindow[newCharacterName] = hwnd;
    m_windowToCharacter[hwnd] = newCharacterName;

    thumbWidget->setCharacterName(newCharacterName);

    QString cachedSystem = m_characterSystems.value(newCharacterName);
    if (!cachedSystem.isEmpty()) {
      thumbWidget->setSystemName(cachedSystem);
    } else {
      thumbWidget->setSystemName(QString());
    }

    tryRestoreClientLocation(hwnd, newCharacterName);

    if (cfg.hasCustomThumbnailSize(newCharacterName)) {
      QSize customSize = cfg.getThumbnailSize(newCharacterName);
      thumbWidget->setFixedSize(customSize);
      thumbWidget->forceUpdate();
    }

    if (cfg.rememberPositions()) {
      QPoint savedPos = cfg.getThumbnailPosition(newCharacterName);
      if (savedPos != QPoint(-1, -1)) {
        QSize thumbSize;
        if (cfg.hasCustomThumbnailSize(newCharacterName)) {
          thumbSize = cfg.getThumbnailSize(newCharacterName);
        } else {
          thumbSize = QSize(cfg.thumbnailWidth(), cfg.thumbnailHeight());
        }

        QRect thumbRect(savedPos, thumbSize);
        QScreen *targetScreen = nullptr;
        for (QScreen *screen : QGuiApplication::screens()) {
          if (screen->geometry().intersects(thumbRect)) {
            targetScreen = screen;
            break;
          }
        }
        if (targetScreen) {
          thumbWidget->move(savedPos);
        }
      }
    }

    m_needsMappingUpdate = true;
    updateCharacterMappings();

  } else if (wasLoggedIn && isNowNotLoggedIn) {
    QString displayName =
        cfg.showNotLoggedInOverlay() ? NOT_LOGGED_IN_TEXT : "";
    thumbWidget->setCharacterName(displayName);
    thumbWidget->setSystemName(QString());

    if (!cfg.preserveLogoutPositions()) {
      int notLoggedInIndex = m_notLoggedInWindows.size();
      QPoint pos = calculateNotLoggedInPosition(notLoggedInIndex);
      thumbWidget->move(pos);
    }

    m_needsMappingUpdate = true;
    updateCharacterMappings();
  }

  // Update thumbnail visibility based on current settings
  updateActiveWindow();
}

QPoint MainWindow::calculateNotLoggedInPosition(int index) {
  const Config &cfg = Config::instance();
  int stackMode = cfg.notLoggedInStackMode();

  int thumbWidth = cfg.thumbnailWidth();
  int thumbHeight = cfg.thumbnailHeight();
  int spacing = 10;

  QPoint refPos = cfg.notLoggedInReferencePosition();
  int baseX = refPos.x();
  int baseY = refPos.y();

  int offsetX = 0, offsetY = 0;

  switch (stackMode) {
  case 0:
    offsetX = index * (thumbWidth + spacing);
    break;
  case 1:
    offsetY = index * (thumbHeight + spacing);
    break;
  case 2:
    offsetX = 0;
    offsetY = 0;
    break;
  }

  return QPoint(baseX + offsetX, baseY + offsetY);
}

void MainWindow::updateActiveWindow() {
  const Config &cfg = Config::instance();
  HWND activeWindow = GetForegroundWindow();
  bool hideActive = cfg.hideActiveClientThumbnail();
  bool hideWhenEVENotFocused = cfg.hideThumbnailsWhenEVENotFocused();
  bool highlightActive = cfg.highlightActiveWindow();

  static bool lastHideActive = false;
  bool hideActiveChanged = (lastHideActive != hideActive);
  lastHideActive = hideActive;

  if (activeWindow == m_lastActiveWindow && !hideActiveChanged) {
    return;
  }

  HWND previousActiveWindow = m_lastActiveWindow;
  m_lastActiveWindow = activeWindow;

  bool isEVEFocused = thumbnails.contains(activeWindow);

  static bool wasEVEFocused = true;
  bool eveFocusChanged = (wasEVEFocused != isEVEFocused);

  // Debounce EVE focus changes to prevent flickering during rapid window
  // switching
  if (hideWhenEVENotFocused && eveFocusChanged) {
    // Store the pending focus state
    m_pendingEVEFocusState = isEVEFocused;
    m_hasPendingEVEFocusChange = true;

    // Start/restart the debounce timer
    m_eveFocusDebounceTimer->start();

    // Update wasEVEFocused to prevent multiple timer starts
    wasEVEFocused = isEVEFocused;

    // Don't process the change immediately - let the timer handle it
    // But still handle other visibility updates for the active window
    if (activeWindow != nullptr && thumbnails.contains(activeWindow)) {
      ThumbnailWidget *thumbnail = thumbnails.value(activeWindow);
      if (thumbnail) {
        bool isActive = true;

        if (highlightActive) {
          thumbnail->setActive(isActive);
        } else {
          thumbnail->setActive(false);
        }

        thumbnail->forceUpdate();

        // Immediately restore topmost after DWM update which can disrupt
        // Z-order
        if (cfg.alwaysOnTop()) {
          thumbnail->ensureTopmost();
        }

        if (thumbnail->hasCombatEvent()) {
          QString currentEventType = thumbnail->getCombatEventType();
          if (!currentEventType.isEmpty() &&
              cfg.combatEventSuppressFocused(currentEventType)) {
            thumbnail->setCombatMessage("", "");
            qDebug() << "MainWindow: Cleared combat event for focused window:"
                     << currentEventType;
          }
        }
      }
      updateAllCycleIndices(activeWindow);
    }
    return;
  }

  wasEVEFocused = isEVEFocused;

  if (activeWindow != nullptr && !thumbnails.contains(activeWindow)) {
    if (previousActiveWindow != nullptr &&
        thumbnails.contains(previousActiveWindow)) {
      auto it = thumbnails.find(previousActiveWindow);
      if (it != thumbnails.end()) {
        it.value()->setActive(false);
        // Update visibility using centralized logic
        updateThumbnailVisibility(previousActiveWindow);
      }
    }

    m_needsEnumeration = true;
    refreshWindows();
    return;
  }

  auto updateWindow = [&](HWND hwnd) {
    auto it = thumbnails.find(hwnd);
    if (it == thumbnails.end()) {
      return;
    }

    bool isActive = (hwnd == activeWindow);

    if (highlightActive) {
      it.value()->setActive(isActive);
    } else {
      it.value()->setActive(false);
    }

    if (isActive) {
      it.value()->forceUpdate();

      // Immediately restore topmost after DWM update which can disrupt Z-order
      if (cfg.alwaysOnTop()) {
        it.value()->ensureTopmost();
      }

      if (it.value()->hasCombatEvent()) {
        QString currentEventType = it.value()->getCombatEventType();
        if (!currentEventType.isEmpty() &&
            cfg.combatEventSuppressFocused(currentEventType)) {
          it.value()->setCombatMessage("", "");
          qDebug() << "MainWindow: Cleared combat event for focused window:"
                   << currentEventType;
        }
      }
    }

    // Use centralized visibility
    updateThumbnailVisibility(hwnd);
  };

  // Update context once before processing windows
  if (previousActiveWindow != nullptr || activeWindow != nullptr) {
    updateVisibilityContext();
  }

  if (previousActiveWindow != nullptr && previousActiveWindow != activeWindow) {
    updateWindow(previousActiveWindow);
  }

  if (activeWindow != nullptr) {
    updateWindow(activeWindow);

    if (thumbnails.contains(activeWindow)) {
      updateAllCycleIndices(activeWindow);
    }
  }
}

void MainWindow::onThumbnailClicked(quintptr windowId) {
  HWND hwnd = reinterpret_cast<HWND>(windowId);
  activateWindow(hwnd);

  updateAllCycleIndices(hwnd);
}

void MainWindow::updateAllCycleIndices(HWND hwnd) {
  QHash<QString, CycleGroup> allGroups = hotkeyManager->getAllCycleGroups();
  for (auto it = allGroups.begin(); it != allGroups.end(); ++it) {
    const QString &groupName = it.key();
    const CycleGroup &group = it.value();

    QVector<HWND> windowsToCycle = buildCycleWindowList(group);

    int index = windowsToCycle.indexOf(hwnd);
    if (index != -1) {
      m_cycleIndexByGroup[groupName] = index;
      m_lastActivatedWindowByGroup[groupName] = hwnd;
    }
  }

  updateCharacterHotkeyCycleIndices(hwnd);

  int notLoggedInIndex = m_notLoggedInWindows.indexOf(hwnd);
  if (notLoggedInIndex != -1) {
    m_notLoggedInCycleIndex = notLoggedInIndex;
  }

  int nonEVEIndex = m_nonEVEWindows.indexOf(hwnd);
  if (nonEVEIndex != -1) {
    m_nonEVECycleIndex = nonEVEIndex;
  }
}

void MainWindow::updateCharacterHotkeyCycleIndices(HWND hwnd) {
  QString activatedCharacter = m_windowToCharacter.value(hwnd);
  if (activatedCharacter.isEmpty()) {
    return;
  }

  QHash<QString, QVector<HotkeyBinding>> allCharacterMultiHotkeys =
      hotkeyManager->getAllCharacterMultiHotkeys();
  QHash<QString, HotkeyBinding> allCharacterHotkeys =
      hotkeyManager->getAllCharacterHotkeys();

  QHash<HotkeyBinding, QVector<QString>> bindingToCharacters;

  for (auto it = allCharacterMultiHotkeys.begin();
       it != allCharacterMultiHotkeys.end(); ++it) {
    const QString &characterName = it.key();
    const QVector<HotkeyBinding> &bindings = it.value();

    for (const HotkeyBinding &binding : bindings) {
      if (binding.enabled) {
        bindingToCharacters[binding].append(characterName);
      }
    }
  }

  for (auto it = allCharacterHotkeys.begin(); it != allCharacterHotkeys.end();
       ++it) {
    const QString &characterName = it.key();
    const HotkeyBinding &binding = it.value();

    if (!allCharacterMultiHotkeys.contains(characterName) && binding.enabled) {
      bindingToCharacters[binding].append(characterName);
    }
  }

  for (auto it = bindingToCharacters.begin(); it != bindingToCharacters.end();
       ++it) {
    const QVector<QString> &characterNames = it.value();

    if (characterNames.size() > 1 &&
        characterNames.contains(activatedCharacter)) {
      QVector<HWND> windowsToCycle;
      for (const QString &charName : characterNames) {
        HWND characterHwnd = hotkeyManager->getWindowForCharacter(charName);
        if (characterHwnd && IsWindow(characterHwnd)) {
          windowsToCycle.append(characterHwnd);
        }
      }

      std::sort(windowsToCycle.begin(), windowsToCycle.end(),
                [this](HWND a, HWND b) {
                  return m_windowCreationTimes.value(a, 0) <
                         m_windowCreationTimes.value(b, 0);
                });

      QVector<QString> sortedNames = characterNames;
      std::sort(sortedNames.begin(), sortedNames.end());
      QString groupKey = sortedNames.join("|");

      int index = windowsToCycle.indexOf(hwnd);
      if (index != -1) {
        m_characterHotkeyCycleIndex[groupKey] = index;
        m_lastActivatedCharacterHotkeyWindow[groupKey] = hwnd;
      }
    }
  }
}

QVector<HWND> MainWindow::buildCycleWindowList(const CycleGroup &group) {
  QVector<HWND> windowsToCycle;

  for (const QString &characterName : group.characterNames) {
    HWND hwnd = hotkeyManager->getWindowForCharacter(characterName);
    if (hwnd && IsWindow(hwnd)) {
      windowsToCycle.append(hwnd);
    }
  }

  if (group.includeNotLoggedIn) {
    for (HWND hwnd : m_notLoggedInWindows) {
      if (!windowsToCycle.contains(hwnd)) {
        windowsToCycle.append(hwnd);
      }
    }
  }

  return windowsToCycle;
}

void MainWindow::handleNamedCycleForward(const QString &groupName) {
  if (m_cycleThrottleTimer->isActive()) {
    return;
  }

  CycleGroup group = hotkeyManager->getCycleGroup(groupName);
  QVector<HWND> windowsToCycle = buildCycleWindowList(group);

  if (windowsToCycle.isEmpty()) {
    return;
  }

  int currentIndex = -1;
  HWND lastActivatedWindow =
      m_lastActivatedWindowByGroup.value(groupName, nullptr);
  if (lastActivatedWindow) {
    currentIndex = windowsToCycle.indexOf(lastActivatedWindow);
    if (currentIndex == -1) {
      lastActivatedWindow = nullptr;
    }
  }

  if (currentIndex == -1) {
    int storedIndex = m_cycleIndexByGroup.value(groupName, -1);
    if (storedIndex >= 0 && storedIndex < windowsToCycle.size()) {
      currentIndex = storedIndex;
    } else {
      currentIndex = -1;
    }
  }

  currentIndex++;

  if (group.noLoop && currentIndex >= windowsToCycle.size()) {
    return;
  }

  if (currentIndex >= windowsToCycle.size()) {
    currentIndex = 0;
  }

  HWND hwnd = windowsToCycle[currentIndex];
  m_cycleIndexByGroup[groupName] = currentIndex;
  m_lastActivatedWindowByGroup[groupName] = hwnd;
  activateWindow(hwnd);

  m_cycleThrottleTimer->start();
}

void MainWindow::handleNamedCycleBackward(const QString &groupName) {
  if (m_cycleThrottleTimer->isActive()) {
    return;
  }

  CycleGroup group = hotkeyManager->getCycleGroup(groupName);
  QVector<HWND> windowsToCycle = buildCycleWindowList(group);

  if (windowsToCycle.isEmpty()) {
    return;
  }

  int currentIndex = -1;
  HWND lastActivatedWindow =
      m_lastActivatedWindowByGroup.value(groupName, nullptr);
  if (lastActivatedWindow) {
    currentIndex = windowsToCycle.indexOf(lastActivatedWindow);
    if (currentIndex == -1) {
      lastActivatedWindow = nullptr;
    }
  }

  if (currentIndex == -1) {
    int storedIndex = m_cycleIndexByGroup.value(groupName, 0);
    if (storedIndex >= 0 && storedIndex < windowsToCycle.size()) {
      currentIndex = storedIndex;
    } else {
      currentIndex = 0;
    }
  }

  currentIndex--;

  if (group.noLoop && currentIndex < 0) {
    return;
  }

  if (currentIndex < 0) {
    currentIndex = windowsToCycle.size() - 1;
  }

  HWND hwnd = windowsToCycle[currentIndex];
  m_cycleIndexByGroup[groupName] = currentIndex;
  m_lastActivatedWindowByGroup[groupName] = hwnd;
  activateWindow(hwnd);

  m_cycleThrottleTimer->start();
}

void MainWindow::handleNotLoggedInCycleForward() {
  if (m_cycleThrottleTimer->isActive()) {
    return;
  }

  if (m_notLoggedInWindows.isEmpty()) {
    return;
  }

  m_notLoggedInWindows.erase(
      std::remove_if(m_notLoggedInWindows.begin(), m_notLoggedInWindows.end(),
                     [](HWND hwnd) { return !IsWindow(hwnd); }),
      m_notLoggedInWindows.end());

  if (m_notLoggedInWindows.isEmpty()) {
    return;
  }

  if (m_notLoggedInCycleIndex >= m_notLoggedInWindows.size()) {
    m_notLoggedInCycleIndex = -1;
  }

  m_notLoggedInCycleIndex++;
  if (m_notLoggedInCycleIndex >= m_notLoggedInWindows.size()) {
    m_notLoggedInCycleIndex = 0;
  }

  HWND hwnd = m_notLoggedInWindows[m_notLoggedInCycleIndex];
  activateWindow(hwnd);

  m_cycleThrottleTimer->start();
}

void MainWindow::handleNotLoggedInCycleBackward() {
  if (m_cycleThrottleTimer->isActive()) {
    return;
  }

  if (m_notLoggedInWindows.isEmpty()) {
    return;
  }

  m_notLoggedInWindows.erase(
      std::remove_if(m_notLoggedInWindows.begin(), m_notLoggedInWindows.end(),
                     [](HWND hwnd) { return !IsWindow(hwnd); }),
      m_notLoggedInWindows.end());

  if (m_notLoggedInWindows.isEmpty()) {
    return;
  }

  if (m_notLoggedInCycleIndex >= m_notLoggedInWindows.size()) {
    m_notLoggedInCycleIndex = m_notLoggedInWindows.size();
  }

  m_notLoggedInCycleIndex--;
  if (m_notLoggedInCycleIndex < 0) {
    m_notLoggedInCycleIndex = m_notLoggedInWindows.size() - 1;
  }

  HWND hwnd = m_notLoggedInWindows[m_notLoggedInCycleIndex];
  activateWindow(hwnd);

  m_cycleThrottleTimer->start();
}

void MainWindow::handleNonEVECycleForward() {
  if (m_cycleThrottleTimer->isActive()) {
    return;
  }

  if (m_nonEVEWindows.isEmpty()) {
    return;
  }

  m_nonEVEWindows.erase(
      std::remove_if(m_nonEVEWindows.begin(), m_nonEVEWindows.end(),
                     [](HWND hwnd) { return !IsWindow(hwnd); }),
      m_nonEVEWindows.end());

  if (m_nonEVEWindows.isEmpty()) {
    return;
  }

  if (m_nonEVECycleIndex >= m_nonEVEWindows.size()) {
    m_nonEVECycleIndex = -1;
  }

  m_nonEVECycleIndex++;
  if (m_nonEVECycleIndex >= m_nonEVEWindows.size()) {
    m_nonEVECycleIndex = 0;
  }

  HWND hwnd = m_nonEVEWindows[m_nonEVECycleIndex];
  activateWindow(hwnd);

  m_cycleThrottleTimer->start();
}

void MainWindow::handleNonEVECycleBackward() {
  if (m_cycleThrottleTimer->isActive()) {
    return;
  }

  if (m_nonEVEWindows.isEmpty()) {
    return;
  }

  m_nonEVEWindows.erase(
      std::remove_if(m_nonEVEWindows.begin(), m_nonEVEWindows.end(),
                     [](HWND hwnd) { return !IsWindow(hwnd); }),
      m_nonEVEWindows.end());

  if (m_nonEVEWindows.isEmpty()) {
    return;
  }

  if (m_nonEVECycleIndex >= m_nonEVEWindows.size()) {
    m_nonEVECycleIndex = m_nonEVEWindows.size();
  }

  m_nonEVECycleIndex--;
  if (m_nonEVECycleIndex < 0) {
    m_nonEVECycleIndex = m_nonEVEWindows.size() - 1;
  }

  HWND hwnd = m_nonEVEWindows[m_nonEVECycleIndex];
  activateWindow(hwnd);

  m_cycleThrottleTimer->start();
}

void MainWindow::handleCharacterHotkeyCycle(
    const QVector<QString> &characterNames) {
  if (m_cycleThrottleTimer->isActive()) {
    return;
  }

  QVector<HWND> windowsToCycle;

  for (const QString &characterName : characterNames) {
    HWND hwnd = hotkeyManager->getWindowForCharacter(characterName);
    if (hwnd && IsWindow(hwnd)) {
      windowsToCycle.append(hwnd);
    }
  }

  if (windowsToCycle.isEmpty()) {
    return;
  }

  std::sort(windowsToCycle.begin(), windowsToCycle.end(),
            [this](HWND a, HWND b) {
              return m_windowCreationTimes.value(a, 0) <
                     m_windowCreationTimes.value(b, 0);
            });

  QVector<QString> sortedNames = characterNames;
  std::sort(sortedNames.begin(), sortedNames.end());
  QString groupKey = sortedNames.join("|");

  int currentIndex = -1;
  HWND lastActivatedWindow =
      m_lastActivatedCharacterHotkeyWindow.value(groupKey, nullptr);
  if (lastActivatedWindow) {
    currentIndex = windowsToCycle.indexOf(lastActivatedWindow);
    if (currentIndex == -1) {
      lastActivatedWindow = nullptr;
    }
  }

  if (currentIndex == -1) {
    int storedIndex = m_characterHotkeyCycleIndex.value(groupKey, -1);
    if (storedIndex >= 0 && storedIndex < windowsToCycle.size()) {
      currentIndex = storedIndex;
    } else {
      currentIndex = -1;
    }
  }

  currentIndex++;
  if (currentIndex >= windowsToCycle.size()) {
    currentIndex = 0;
  }

  HWND hwnd = windowsToCycle[currentIndex];
  m_characterHotkeyCycleIndex[groupKey] = currentIndex;
  m_lastActivatedCharacterHotkeyWindow[groupKey] = hwnd;
  activateWindow(hwnd);

  m_cycleThrottleTimer->start();
}

void MainWindow::activateCharacter(const QString &characterName) {
  HWND hwnd = hotkeyManager->getWindowForCharacter(characterName);
  if (hwnd) {
    activateWindow(hwnd);

    QHash<QString, CycleGroup> allGroups = hotkeyManager->getAllCycleGroups();
    for (auto it = allGroups.begin(); it != allGroups.end(); ++it) {
      const QString &groupName = it.key();
      const CycleGroup &group = it.value();

      QVector<HWND> windowsToCycle = buildCycleWindowList(group);

      int index = windowsToCycle.indexOf(hwnd);
      if (index != -1) {
        m_cycleIndexByGroup[groupName] = index;
        m_lastActivatedWindowByGroup[groupName] = hwnd;
      }
    }
  }
}

void MainWindow::activateWindow(HWND hwnd) {
  const Config &cfg = Config::instance();

  // Check if window needs restoration for refresh tracking
  bool wasMinimized = IsIconic(hwnd);

  // Use WindowCapture's activation logic (handles all window states properly)
  WindowCapture::activateWindow(hwnd);

  // Track restored window for thumbnail refresh
  if (wasMinimized) {
    m_hwndPendingRefresh = hwnd;
  }

  // Handle minimize inactive clients feature
  if (cfg.minimizeInactiveClients()) {
    m_hwndToActivate = hwnd;
    minimizeTimer->start(cfg.minimizeDelay());
  }

  updateActiveWindow();

  // Ensure thumbnails remain on top after window activation
  // BringWindowToTop can disrupt Z-order even with Qt::WindowStaysOnTopHint
  // Use multiple restoration attempts at different intervals for reliability
  // Windows processes Z-order changes asynchronously and timing varies by
  // system
  ensureThumbnailsOnTop();
  QTimer::singleShot(10, this, &MainWindow::ensureThumbnailsOnTop);
  QTimer::singleShot(50, this, &MainWindow::ensureThumbnailsOnTop);
  QTimer::singleShot(100, this, &MainWindow::ensureThumbnailsOnTop);
}

void MainWindow::minimizeInactiveWindows() {
  const Config &cfg = Config::instance();
  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    HWND otherHwnd = it.key();
    if (otherHwnd != m_hwndToActivate && IsWindow(otherHwnd)) {
      QString characterName = m_windowToCharacter.value(otherHwnd);
      if (!characterName.isEmpty() &&
          cfg.isCharacterNeverMinimize(characterName)) {
        continue;
      }

      ShowWindowAsync(otherHwnd, 11);
    }
  }
}

void MainWindow::onThumbnailPositionChanged(quintptr windowId,
                                            QPoint position) {
  Config &cfg = Config::instance();
  if (!cfg.rememberPositions()) {
    return;
  }

  HWND hwnd = reinterpret_cast<HWND>(windowId);

  QString processName = m_windowProcessNames.value(hwnd, "");
  bool isEVEClient =
      processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;

  if (isEVEClient) {
    QString characterName = m_windowToCharacter.value(hwnd);
    if (!characterName.isEmpty()) {
      cfg.setThumbnailPosition(characterName, position);
    }
  } else {
    // Use only process name as key for non-EVE apps to avoid issues with
    // dynamic window titles
    if (!processName.isEmpty()) {
      cfg.setThumbnailPosition(processName, position);
    }
  }
}

void MainWindow::onGroupDragStarted(quintptr windowId) {
  m_groupDragInitialPositions.clear();

  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    ThumbnailWidget *thumb = it.value();
    m_groupDragInitialPositions.insert(thumb->getWindowId(), thumb->pos());

    thumb->hideOverlay();
  }

  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    ThumbnailWidget *thumb = it.value();
    if (thumb->getWindowId() != windowId) {
      thumb->hideOverlay();
    }
  }
}

void MainWindow::onGroupDragMoved(quintptr windowId, QPoint delta) {
  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    ThumbnailWidget *thumb = it.value();

    if (thumb->getWindowId() == windowId) {
      continue;
    }

    QPoint initialPos = m_groupDragInitialPositions.value(thumb->getWindowId());
    QPoint newPos = initialPos + delta;

    thumb->move(newPos);
  }
}

void MainWindow::onGroupDragEnded(quintptr) {
  Config &cfg = Config::instance();

  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    ThumbnailWidget *thumb = it.value();
    thumb->showOverlay();
  }

  if (!cfg.rememberPositions()) {
    m_groupDragInitialPositions.clear();
    // Update visibility now that dragging has ended
    updateAllThumbnailsVisibility();
    return;
  }

  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    HWND hwnd = it.key();
    ThumbnailWidget *thumb = it.value();

    QString processName = m_windowProcessNames.value(hwnd, "");
    bool isEVEClient =
        processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;

    if (isEVEClient) {
      QString characterName = m_windowToCharacter.value(hwnd);
      if (!characterName.isEmpty()) {
        cfg.setThumbnailPosition(characterName, thumb->pos());
      }
    } else {
      // Use only process name as key for non-EVE apps to avoid issues with
      // dynamic window titles
      if (!processName.isEmpty()) {
        cfg.setThumbnailPosition(processName, thumb->pos());
      }
    }
  }

  m_groupDragInitialPositions.clear();

  // Update visibility now that dragging has ended to ensure correct state
  // (in case EVE focus changed during drag)
  updateAllThumbnailsVisibility();
}

void MainWindow::showSettings() {
  if (m_configDialog) {
    if (m_configDialog->isMinimized()) {
      m_configDialog->showNormal();
    }

    m_configDialog->raise();
    m_configDialog->activateWindow();

    HWND hwnd = reinterpret_cast<HWND>(m_configDialog->winId());
    if (hwnd) {
      if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
      }
      SetForegroundWindow(hwnd);
    }

    return;
  }

  m_configDialog = new ConfigDialog();
  m_configDialog->setAttribute(Qt::WA_DeleteOnClose);
  m_configDialog->setWindowModality(Qt::NonModal);

  connect(m_configDialog, &ConfigDialog::settingsApplied, this,
          &MainWindow::applySettings);
  connect(m_configDialog, &ConfigDialog::saveClientLocationsRequested, this,
          &MainWindow::saveCurrentClientLocations);

  connect(this, &MainWindow::profileSwitchedExternally, m_configDialog,
          &ConfigDialog::onExternalProfileSwitch);

  hotkeyManager->setSuspended(true);

  if (!m_thumbnailsManuallyHidden) {
    // Update all thumbnail visibility when opening settings
    updateAllThumbnailsVisibility();
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
      it.value()->forceOverlayRender();
    }
  } else {
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
      it.value()->forceOverlayRender();
    }
  }

  connect(m_configDialog, &QObject::destroyed, this, [this]() {
    m_configDialog = nullptr;

    hotkeyManager->setSuspended(false);

    hotkeyManager->registerHotkeys();

    updateActiveWindow();

    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
      it.value()->forceOverlayRender();
    }
    updateProfilesMenu();
  });

  m_configDialog->show();
}

void MainWindow::handleProfileSwitch(const QString &profileName) {
  Config &cfg = Config::instance();
  QString currentProfile = cfg.getCurrentProfileName();

  if (profileName == currentProfile) {
    qDebug() << "Already on profile:" << profileName;
    return;
  }

  qDebug() << "Switching from profile" << currentProfile << "to" << profileName;

  cfg.save();
  hotkeyManager->saveToConfig();

  if (cfg.loadProfile(profileName)) {
    applySettings();

    emit profileSwitchedExternally(profileName);

    updateProfilesMenu();

    qDebug() << "Successfully switched to profile:" << profileName;
  } else {
    qWarning() << "Failed to switch to profile:" << profileName;
  }
}

void MainWindow::handleCycleProfileForward() {
  Config &cfg = Config::instance();
  QStringList profiles = cfg.listProfiles();

  if (profiles.isEmpty()) {
    qWarning() << "No profiles available to cycle";
    return;
  }

  QString currentProfile = cfg.getCurrentProfileName();
  int currentIndex = profiles.indexOf(currentProfile);

  if (currentIndex < 0) {
    qWarning() << "Current profile not found in profiles list";
    currentIndex = 0;
  }

  int nextIndex = (currentIndex + 1) % profiles.size();
  QString nextProfile = profiles[nextIndex];

  qDebug() << "Cycling profiles forward from" << currentProfile << "to"
           << nextProfile;

  handleProfileSwitch(nextProfile);
}

void MainWindow::handleCycleProfileBackward() {
  Config &cfg = Config::instance();
  QStringList profiles = cfg.listProfiles();

  if (profiles.isEmpty()) {
    qWarning() << "No profiles available to cycle";
    return;
  }

  QString currentProfile = cfg.getCurrentProfileName();
  int currentIndex = profiles.indexOf(currentProfile);

  if (currentIndex < 0) {
    qWarning() << "Current profile not found in profiles list";
    currentIndex = 0;
  }

  int prevIndex = (currentIndex - 1 + profiles.size()) % profiles.size();
  QString prevProfile = profiles[prevIndex];

  qDebug() << "Cycling profiles backward from" << currentProfile << "to"
           << prevProfile;

  handleProfileSwitch(prevProfile);
}

void MainWindow::applySettings() {
  qDebug()
      << "MainWindow::applySettings - updating thumbnails and overlay caches";
  const Config &cfg = Config::instance();
  m_cycleIndexByGroup.clear();
  m_lastActivatedWindowByGroup.clear();
  m_notLoggedInCycleIndex = -1;
  m_nonEVECycleIndex = -1;
  m_clientLocationMoveAttempted.clear();
  m_clientLocationRetryCount.clear();

  hotkeyManager->loadFromConfig();

  if (!cfg.minimizeInactiveClients()) {
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
      HWND hwnd = it.key();
      if (IsWindow(hwnd) && IsIconic(hwnd)) {
        ShowWindowAsync(hwnd, SW_RESTORE);
      }
    }
  }

  QScreen *primaryScreen = QGuiApplication::primaryScreen();
  QRect screenGeometry = primaryScreen->geometry();
  int screenWidth = screenGeometry.width();
  int screenHeight = screenGeometry.height();
  int margin = 10;
  int thumbWidth = cfg.thumbnailWidth();
  int thumbHeight = cfg.thumbnailHeight();

  int xOffset = margin;
  int yOffset = margin;
  int thumbnailsPerRow = (screenWidth - margin) / (thumbWidth + margin);
  if (thumbnailsPerRow < 1)
    thumbnailsPerRow = 1;

  int notLoggedInCount = 0;
  bool rememberPos = cfg.rememberPositions();

  const bool hideWhenEVENotFocused = cfg.hideThumbnailsWhenEVENotFocused();
  const bool hideActive = cfg.hideActiveClientThumbnail();
  const HWND currentActiveWindow = GetForegroundWindow();
  const bool isEVECurrentlyFocused = thumbnails.contains(currentActiveWindow);

  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    HWND hwnd = it.key();
    ThumbnailWidget *thumb = it.value();

    QString processName = m_windowProcessNames.value(hwnd, "");
    bool isEVEClient =
        processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;

    QSize newSize(thumbWidth, thumbHeight);
    if (isEVEClient) {
      QString characterName = m_windowToCharacter.value(hwnd);
      if (!characterName.isEmpty()) {
        if (cfg.hasCustomThumbnailSize(characterName)) {
          newSize = cfg.getThumbnailSize(characterName);
        }

        QString customName = cfg.getCustomThumbnailName(characterName);
        if (!customName.isEmpty()) {
          thumb->setCustomName(customName);
        } else {
          thumb->setCustomName(QString());
        }
      }
    } else if (!isEVEClient && cfg.hasCustomProcessThumbnailSize(processName)) {
      newSize = cfg.getProcessThumbnailSize(processName);
    }

    QSize currentSize = thumb->size();
    if (currentSize != newSize) {
      thumb->setFixedSize(newSize);
      thumb->forceUpdate();
    }

    thumb->setWindowOpacity(cfg.thumbnailOpacity() / 100.0);

    thumb->updateWindowFlags(cfg.alwaysOnTop());

    QPoint savedPos(-1, -1);
    bool hasSavedPosition = false;

    if (rememberPos) {
      if (isEVEClient) {
        QString characterName = m_windowToCharacter.value(hwnd);
        if (!characterName.isEmpty()) {
          savedPos = cfg.getThumbnailPosition(characterName);
          hasSavedPosition = (savedPos != QPoint(-1, -1));
        }
      } else {
        // Use only process name as key for non-EVE apps to avoid issues with
        // dynamic window titles
        if (!processName.isEmpty()) {
          savedPos = cfg.getThumbnailPosition(processName);
          hasSavedPosition = (savedPos != QPoint(-1, -1));
        }
      }
    }

    if (hasSavedPosition) {
      QRect thumbRect(savedPos, newSize);
      QScreen *targetScreen = nullptr;
      for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->geometry().intersects(thumbRect)) {
          targetScreen = screen;
          break;
        }
      }

      if (targetScreen) {
        thumb->move(savedPos);
      } else {
        hasSavedPosition = false;
      }
    }

    if (!hasSavedPosition) {
      QString characterName = m_windowToCharacter.value(hwnd);
      bool isNotLoggedIn = isEVEClient && characterName.isEmpty();

      if (isNotLoggedIn) {
        QPoint pos = calculateNotLoggedInPosition(notLoggedInCount);
        thumb->move(pos);
        notLoggedInCount++;
      } else {
        if (xOffset + thumbWidth > screenWidth - margin) {
          xOffset = margin;
          yOffset += thumbHeight + margin;
        }

        if (yOffset + thumbHeight > screenHeight - margin) {
          yOffset = margin;
        }

        thumb->move(xOffset, yOffset);
        xOffset += thumbWidth + margin;
      }
    }

    thumb->refreshSystemColor();
    thumb->updateOverlays();
    thumb->forceOverlayRender();

    thumb->QWidget::update();

    // Apply visibility using centralized logic
    updateThumbnailVisibility(hwnd);
  }

  if (m_chatLogReader) {
    QString chatLogDirectory = cfg.chatLogDirectory();
    QString gameLogDirectory = cfg.gameLogDirectory();
    m_chatLogReader->setLogDirectory(chatLogDirectory);
    m_chatLogReader->setGameLogDirectory(gameLogDirectory);

    bool enableChatLog = cfg.enableChatLogMonitoring();
    bool enableGameLog = cfg.enableGameLogMonitoring();
    m_chatLogReader->setEnableChatLogMonitoring(enableChatLog);
    m_chatLogReader->setEnableGameLogMonitoring(enableGameLog);

    bool shouldMonitor = enableChatLog || enableGameLog;

    if (shouldMonitor) {
      QStringList characterNames = m_characterToWindow.keys();
      m_chatLogReader->setCharacterNames(characterNames);
    }

    if (shouldMonitor && !m_chatLogReader->isMonitoring()) {
      m_chatLogReader->start();
      qDebug() << "ChatLog: Monitoring started via settings (ChatLog:"
               << enableChatLog << ", GameLog:" << enableGameLog << ")";
    } else if (!shouldMonitor && m_chatLogReader->isMonitoring()) {
      m_chatLogReader->stop();
      qDebug() << "ChatLog: Monitoring stopped via settings";
    } else if (shouldMonitor && m_chatLogReader->isMonitoring()) {
      m_chatLogReader->refreshMonitoring();
      qDebug() << "ChatLog: Monitoring refreshed via settings (ChatLog:"
               << enableChatLog << ", GameLog:" << enableGameLog << ")";
    }
  }

  updateActiveWindow();

  refreshWindows();
}

void MainWindow::restartApplication() {
  emit requestRestart();
  QCoreApplication::exit(1000);
}

void MainWindow::reloadThumbnails() {
  qDebug() << "MainWindow: Reloading thumbnails to fresh state";

  qDeleteAll(thumbnails);
  thumbnails.clear();

  m_characterToWindow.clear();
  m_windowToCharacter.clear();
  m_characterSystems.clear();
  m_cycleIndexByGroup.clear();
  m_lastActivatedWindowByGroup.clear();
  m_windowCreationTimes.clear();
  m_characterHotkeyCycleIndex.clear();
  m_lastActivatedCharacterHotkeyWindow.clear();
  m_clientLocationMoveAttempted.clear();
  m_clientLocationRetryCount.clear();
  m_lastKnownTitles.clear();
  m_windowProcessNames.clear();
  m_windowsBeingMoved.clear();
  m_cachedThumbnailList.clear();
  m_groupDragInitialPositions.clear();

  m_notLoggedInWindows.clear();
  m_notLoggedInCycleIndex = -1;
  m_nonEVEWindows.clear();
  m_nonEVECycleIndex = -1;

  m_hwndToActivate = nullptr;
  m_hwndPendingRefresh = nullptr;
  m_lastActiveWindow = nullptr;

  m_needsEnumeration = true;
  m_needsMappingUpdate = false;
  m_lastThumbnailListSize = 0;

  if (windowCapture) {
    windowCapture->clearCache();
  }
  OverlayInfo::clearCache();

  qDebug() << "MainWindow: Refreshing windows after reload";
  refreshWindows();

  if (m_chatLogReader && m_chatLogReader->isMonitoring()) {
    QStringList characterNames = m_characterToWindow.keys();
    if (!characterNames.isEmpty()) {
      m_chatLogReader->setCharacterNames(characterNames);
      m_chatLogReader->refreshMonitoring();
      qDebug() << "MainWindow: ChatLog monitoring refreshed after reload";
    }
  }
}

void MainWindow::exitApplication() {
  for (ThumbnailWidget *thumbnail : thumbnails) {
    if (thumbnail) {
      thumbnail->closeImmediately();
    }
  }

  if (hotkeyManager) {
    hotkeyManager->uninstallMouseHook();
  }

  QTimer::singleShot(0, []() { QCoreApplication::quit(); });
}

void MainWindow::onCharacterSystemChanged(const QString &characterName,
                                          const QString &systemName) {
  qDebug() << "MainWindow: Character" << characterName << "moved to system"
           << systemName;

  m_characterSystems[characterName] = systemName;

  HWND hwnd = m_characterToWindow.value(characterName);
  if (hwnd && thumbnails.contains(hwnd)) {
    ThumbnailWidget *widget = thumbnails[hwnd];
    widget->setSystemName(systemName);
    qDebug() << "MainWindow: Updated thumbnail for" << characterName
             << "with system:" << systemName;
  }
}

void MainWindow::onCombatEventDetected(const QString &characterName,
                                       const QString &eventType,
                                       const QString &eventText) {
  qDebug() << "MainWindow: Combat event for" << characterName
           << "- Type:" << eventType << "- Text:" << eventText;

  const Config &cfg = Config::instance();
  if (!cfg.showCombatMessages()) {
    qDebug() << "MainWindow: Combat messages disabled in settings";
    return;
  }

  if (!cfg.isCombatEventTypeEnabled(eventType)) {
    qDebug() << "MainWindow: Event type" << eventType
             << "is disabled in settings";
    return;
  }

  HWND hwnd = m_characterToWindow.value(characterName);
  if (hwnd && thumbnails.contains(hwnd)) {
    HWND activeWindow = GetForegroundWindow();
    if (cfg.combatEventSuppressFocused(eventType) && hwnd == activeWindow) {
      qDebug() << "MainWindow: Suppressing combat event for focused window:"
               << characterName << "event:" << eventType;
      return;
    }

    ThumbnailWidget *widget = thumbnails[hwnd];
    widget->setCombatMessage(eventText, eventType);
    qDebug() << "MainWindow: Updated thumbnail for" << characterName
             << "with combat message:" << eventText;

    // Play sound notification if enabled
    if (cfg.combatEventSoundEnabled(eventType)) {
      QString soundFile = cfg.combatEventSoundFile(eventType);
      if (!soundFile.isEmpty() && QFile::exists(soundFile)) {
        m_soundEffect->setSource(QUrl::fromLocalFile(soundFile));
        qreal volume = cfg.combatEventSoundVolume(eventType) / 100.0;
        m_soundEffect->setVolume(volume);
        m_soundEffect->play();
        qDebug() << "MainWindow: Playing sound for" << eventType
                 << "- File:" << soundFile << "- Volume:" << volume;
      } else {
        qDebug() << "MainWindow: Sound file not found or not set for"
                 << eventType;
      }
    }
  }
}

void MainWindow::updateProfilesMenu() {
  if (!m_profilesMenu) {
    return;
  }

  const Config &cfg = Config::instance();
  m_profilesMenu->clear();

  QStringList profiles = cfg.listProfiles();
  QString currentProfile = cfg.getCurrentProfileName();

  if (profiles.isEmpty()) {
    QAction *noProfilesAction =
        new QAction("No profiles available", m_profilesMenu);
    noProfilesAction->setEnabled(false);
    m_profilesMenu->addAction(noProfilesAction);
    return;
  }

  for (const QString &profileName : profiles) {
    QAction *profileAction = new QAction(profileName, m_profilesMenu);
    profileAction->setCheckable(true);

    if (profileName == currentProfile) {
      profileAction->setChecked(true);
    }

    profileAction->setData(profileName);

    connect(profileAction, &QAction::triggered, this,
            &MainWindow::activateProfile);

    m_profilesMenu->addAction(profileAction);
  }
}

void MainWindow::activateProfile() {
  QAction *action = qobject_cast<QAction *>(sender());
  if (!action) {
    return;
  }

  QString profileName = action->data().toString();
  if (profileName.isEmpty()) {
    return;
  }

  handleProfileSwitch(profileName);

  updateProfilesMenu();
}

void MainWindow::toggleSuspendHotkeys() {
  if (hotkeyManager) {
    hotkeyManager->toggleSuspended();
  }
}

void MainWindow::onHotkeysSuspendedChanged(bool suspended) {
  if (m_suspendHotkeysAction) {
    m_suspendHotkeysAction->setChecked(suspended);
  }
}

void MainWindow::closeAllEVEClients() {
  QVector<WindowInfo> windows = windowCapture->getEVEWindows();

  // Only close actual EVE clients (exefile.exe), not other monitored
  // applications Users may have added other process names for thumbnail support
  const QString eveProcessName = QStringLiteral("exefile.exe");
  const Config &cfg = Config::instance();

  for (const WindowInfo &window : windows) {
    if (window.handle && IsWindow(window.handle)) {
      if (window.processName.compare(eveProcessName, Qt::CaseInsensitive) ==
          0) {
        // Check if character is in exception list
        QString characterName = m_windowToCharacter.value(window.handle);
        if (!characterName.isEmpty() &&
            cfg.isCharacterNeverClose(characterName)) {
          continue; // Skip closing this character
        }
        PostMessage(window.handle, WM_CLOSE, 0, 0);
      }
    }
  }
}

void MainWindow::minimizeAllEVEClients() {
  QVector<WindowInfo> windows = windowCapture->getEVEWindows();

  // Only minimize actual EVE clients (exefile.exe), not other monitored
  // applications Users may have added other process names for thumbnail support
  const QString eveProcessName = QStringLiteral("exefile.exe");

  for (const WindowInfo &window : windows) {
    if (window.handle && IsWindow(window.handle)) {
      if (window.processName.compare(eveProcessName, Qt::CaseInsensitive) ==
          0) {
        ShowWindowAsync(window.handle, SW_FORCEMINIMIZE);
      }
    }
  }
}

void MainWindow::toggleThumbnailsVisibility() {
  m_thumbnailsManuallyHidden = !m_thumbnailsManuallyHidden;

  if (m_hideThumbnailsAction) {
    m_hideThumbnailsAction->setChecked(m_thumbnailsManuallyHidden);
  }

  // Update all thumbnail visibility using centralized logic
  updateAllThumbnailsVisibility();
}

void MainWindow::saveCurrentClientLocations() {
  Config &cfg = Config::instance();
  int savedCount = 0;

  for (auto it = m_windowToCharacter.constBegin();
       it != m_windowToCharacter.constEnd(); ++it) {
    HWND hwnd = it.key();
    QString characterName = it.value();

    if (!IsWindow(hwnd) || characterName.isEmpty()) {
      continue;
    }

    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
      QRect qRect(rect.left, rect.top, rect.right - rect.left,
                  rect.bottom - rect.top);
      cfg.setClientWindowRect(characterName, qRect);
      savedCount++;
      qDebug() << "Saved window location for" << characterName << ":" << qRect;
    }
  }

  cfg.save();
  qDebug() << "Saved" << savedCount << "client window locations";
}

bool MainWindow::isWindowRectValid(const QRect &rect) {
  if (rect.isNull() || rect.isEmpty()) {
    return false;
  }

  RECT winRect = {rect.left(), rect.top(), rect.right(), rect.bottom()};
  HMONITOR hMonitor = MonitorFromRect(&winRect, MONITOR_DEFAULTTONULL);

  return (hMonitor != nullptr);
}

void MainWindow::scheduleLocationRefresh(HWND hwnd) {
  QTimer *&timer = m_locationRefreshTimers[hwnd];
  if (!timer) {
    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, hwnd]() {
      if (thumbnails.contains(hwnd)) {
        refreshSingleThumbnail(hwnd);
      }
    });
  }

  timer->start();
}

void MainWindow::cleanupLocationRefreshTimer(HWND hwnd) {
  QTimer *timer = m_locationRefreshTimers.value(hwnd, nullptr);
  if (timer) {
    timer->stop();
    timer->deleteLater();
    m_locationRefreshTimers.remove(hwnd);
  }
}

void MainWindow::invalidateCycleIndicesForWindow(HWND hwnd) {
  QHash<QString, CycleGroup> allGroups = hotkeyManager->getAllCycleGroups();
  for (auto it = allGroups.begin(); it != allGroups.end(); ++it) {
    const QString &groupName = it.key();
    HWND lastActivated = m_lastActivatedWindowByGroup.value(groupName, nullptr);
    if (lastActivated == hwnd) {
      m_lastActivatedWindowByGroup.remove(groupName);
    }
  }
}

bool MainWindow::tryRestoreClientLocation(HWND hwnd,
                                          const QString &characterName) {
  const Config &cfg = Config::instance();

  if (!cfg.saveClientLocation()) {
    qDebug() << "tryRestoreClientLocation: saveClientLocation is disabled";
    return false;
  }

  if (m_clientLocationMoveAttempted.value(hwnd, false)) {
    qDebug() << "tryRestoreClientLocation: already attempted for"
             << characterName;
    return false;
  }

  QRect savedRect = cfg.getClientWindowRect(characterName);
  qDebug() << "tryRestoreClientLocation: attempting to restore" << characterName
           << "to" << savedRect;

  if (!isWindowRectValid(savedRect)) {
    qDebug() << "tryRestoreClientLocation: Saved window location for"
             << characterName << "is invalid or off-screen";
    m_clientLocationMoveAttempted[hwnd] = true;
    return false;
  }

  RECT currentRect;
  if (GetWindowRect(hwnd, &currentRect)) {
    qDebug() << "tryRestoreClientLocation: current position is"
             << QRect(currentRect.left, currentRect.top,
                      currentRect.right - currentRect.left,
                      currentRect.bottom - currentRect.top);
  }

  BOOL result = SetWindowPos(hwnd, nullptr, savedRect.x(), savedRect.y(),
                             savedRect.width(), savedRect.height(),
                             SWP_NOZORDER | SWP_NOACTIVATE);

  if (!result) {
    DWORD error = GetLastError();
    qDebug() << "tryRestoreClientLocation: SetWindowPos failed for"
             << characterName << "with error code" << error;
    m_clientLocationMoveAttempted[hwnd] = true;
    return false;
  }

  qDebug() << "tryRestoreClientLocation: SetWindowPos succeeded for"
           << characterName;

  QTimer *verifyTimer = new QTimer(this);
  verifyTimer->setSingleShot(true);
  verifyTimer->setInterval(500);

  connect(verifyTimer, &QTimer::timeout, this,
          [this, hwnd, characterName, savedRect]() {
            if (!IsWindow(hwnd)) {
              return;
            }

            RECT actualRect;
            if (GetWindowRect(hwnd, &actualRect)) {
              QRect actual(actualRect.left, actualRect.top,
                           actualRect.right - actualRect.left,
                           actualRect.bottom - actualRect.top);

              int deltaX = abs(actual.x() - savedRect.x());
              int deltaY = abs(actual.y() - savedRect.y());

              qDebug() << "Verification: Position after 500ms:" << actual
                       << "Delta from target:" << deltaX << deltaY;

              if (deltaX > 0 || deltaY > 0) {
                int retryCount = m_clientLocationRetryCount.value(hwnd, 0);
                if (retryCount < 3) {
                  qDebug() << "Window position drifted, attempting retry"
                           << (retryCount + 1);
                  m_clientLocationRetryCount[hwnd] = retryCount + 1;
                  m_clientLocationMoveAttempted[hwnd] = false;
                  tryRestoreClientLocation(hwnd, characterName);
                } else {
                  qDebug() << "Maximum retry attempts reached for"
                           << characterName;
                  m_clientLocationRetryCount.remove(hwnd);
                }
              } else {
                qDebug() << "Position verified successfully for"
                         << characterName;
                m_clientLocationRetryCount.remove(hwnd);
              }
            }
          });

  verifyTimer->start();
  m_clientLocationMoveAttempted[hwnd] = true;
  return true;
}

void MainWindow::handleProtocolProfileSwitch(const QString &profileName) {
  qDebug() << "Protocol request: switch to profile" << profileName;

  // Check if profile exists
  if (!Config::instance().profileExists(profileName)) {
    QString msg = QString("Profile '%1' does not exist.\n\n"
                          "Create this profile in Settings first.")
                      .arg(profileName);
    QMessageBox::warning(nullptr, "Profile Not Found", msg);
    qWarning() << "Protocol handler: Profile not found:" << profileName;
    return;
  }

  // Use existing profile switch method
  handleProfileSwitch(profileName);

  qDebug() << "Protocol handler: Successfully switched to profile"
           << profileName;
}

void MainWindow::handleProtocolCharacterActivation(
    const QString &characterName) {
  qDebug() << "Protocol request: activate character" << characterName;

  // Check if character window exists
  HWND hwnd = hotkeyManager->getWindowForCharacter(characterName);
  if (!hwnd) {
    QString msg = QString("Character '%1' is not running or not recognized.\n\n"
                          "Make sure the character is logged in.")
                      .arg(characterName);
    QMessageBox::warning(nullptr, "Character Not Found", msg);
    qWarning() << "Protocol handler: Character not found:" << characterName;
    return;
  }

  // Use existing character activation method
  activateCharacter(characterName);

  qDebug() << "Protocol handler: Successfully activated character"
           << characterName;
}

void MainWindow::handleProtocolHotkeySuspend() {
  qDebug() << "Protocol request: suspend hotkeys";

  if (hotkeyManager) {
    if (!hotkeyManager->isSuspended()) {
      hotkeyManager->setSuspended(true);
      qDebug() << "Protocol handler: Hotkeys suspended";
    } else {
      qDebug() << "Protocol handler: Hotkeys already suspended";
    }
  }
}

void MainWindow::handleProtocolHotkeyResume() {
  qDebug() << "Protocol request: resume hotkeys";

  if (hotkeyManager) {
    if (hotkeyManager->isSuspended()) {
      hotkeyManager->setSuspended(false);
      qDebug() << "Protocol handler: Hotkeys resumed";
    } else {
      qDebug() << "Protocol handler: Hotkeys already active";
    }
  }
}

void MainWindow::handleProtocolThumbnailHide() {
  qDebug() << "Protocol request: hide thumbnails";

  if (!m_thumbnailsManuallyHidden) {
    m_thumbnailsManuallyHidden = true;

    // Update all thumbnail visibility using centralized logic
    updateAllThumbnailsVisibility();

    if (m_hideThumbnailsAction) {
      m_hideThumbnailsAction->setChecked(true);
    }

    qDebug() << "Protocol handler: Thumbnails hidden";
  } else {
    qDebug() << "Protocol handler: Thumbnails already hidden";
  }
}

void MainWindow::handleProtocolThumbnailShow() {
  qDebug() << "Protocol request: show thumbnails";

  if (m_thumbnailsManuallyHidden) {
    m_thumbnailsManuallyHidden = false;

    // Update all thumbnail visibility using centralized logic
    updateAllThumbnailsVisibility();

    if (m_hideThumbnailsAction) {
      m_hideThumbnailsAction->setChecked(false);
    }

    qDebug() << "Protocol handler: Thumbnails shown";
  } else {
    qDebug() << "Protocol handler: Thumbnails already visible";
  }
}

void MainWindow::handleProtocolConfigOpen() {
  qDebug() << "Protocol request: open config dialog";
  showSettings();
  qDebug() << "Protocol handler: Config dialog opened";
}

void MainWindow::handleProtocolError(const QString &url,
                                     const QString &reason) {
  qWarning() << "Protocol error:" << reason << "- URL:" << url;

  QString msg = QString("Failed to process protocol URL:\n%1\n\n"
                        "Reason: %2")
                    .arg(url)
                    .arg(reason);
  QMessageBox::warning(nullptr, "Invalid Protocol URL", msg);
}

void MainWindow::handleIpcConnection() {
  QLocalSocket *clientSocket = m_ipcServer->nextPendingConnection();

  if (!clientSocket) {
    return;
  }

  qDebug() << "IPC connection received";

  connect(clientSocket, &QLocalSocket::readyRead, this, [this, clientSocket]() {
    QByteArray data = clientSocket->readAll();
    QString url = QString::fromUtf8(data).trimmed();

    qDebug() << "Received URL via IPC:" << url;

    // Process the URL
    processProtocolUrl(url);

    clientSocket->disconnectFromServer();
  });

  connect(clientSocket, &QLocalSocket::disconnected, clientSocket,
          &QLocalSocket::deleteLater);
}

void MainWindow::processProtocolUrl(const QString &url) {
  if (m_protocolHandler) {
    m_protocolHandler->handleUrl(url);
  } else {
    qWarning() << "Protocol handler not initialized, cannot process URL:"
               << url;
  }
}

/// Ensure all thumbnails remain on top after window operations
/// BringWindowToTop() calls can disrupt Z-order even with
/// Qt::WindowStaysOnTopHint
void MainWindow::ensureThumbnailsOnTop() {
  const Config &cfg = Config::instance();
  if (!cfg.alwaysOnTop()) {
    return; // Only restore if always-on-top is enabled
  }

  // Restore TOPMOST status for all thumbnails (and their overlay windows)
  // ThumbnailWidget::ensureTopmost() handles both the thumbnail and overlay
  for (auto it = thumbnails.constBegin(); it != thumbnails.constEnd(); ++it) {
    ThumbnailWidget *thumbnail = it.value();
    if (thumbnail && thumbnail->isVisible()) {
      thumbnail->ensureTopmost();
    }
  }
}

/// Process debounced EVE focus state changes to prevent flickering
void MainWindow::processEVEFocusChange() {
  if (!m_hasPendingEVEFocusChange) {
    return;
  }

  m_hasPendingEVEFocusChange = false;
  const Config &cfg = Config::instance();
  bool isEVEFocused = m_pendingEVEFocusState;
  bool hideWhenEVENotFocused = cfg.hideThumbnailsWhenEVENotFocused();
  bool highlightActive = cfg.highlightActiveWindow();
  HWND activeWindow = GetForegroundWindow();

  // Re-validate the focus state in case it changed during debounce
  bool currentIsEVEFocused = thumbnails.contains(activeWindow);

  // If focus state changed again during debounce, don't process the stale state
  if (currentIsEVEFocused != isEVEFocused) {
    return;
  }

  if (hideWhenEVENotFocused && !isEVEFocused && !m_thumbnailsManuallyHidden &&
      !m_configDialog) {
    // Don't hide thumbnails if any of them are being dragged or mouse pressed
    bool anyThumbnailDragging = false;
    for (auto it = thumbnails.constBegin(); it != thumbnails.constEnd(); ++it) {
      ThumbnailWidget *thumb = it.value();
      if (thumb && (thumb->isDragging() || thumb->isGroupDragging() ||
                    thumb->isMousePressed())) {
        anyThumbnailDragging = true;
        break;
      }
    }

    if (!anyThumbnailDragging) {
      updateAllThumbnailsVisibility();
    }
    return;
  }

  if (hideWhenEVENotFocused && isEVEFocused) {
    // EVE regained focus - update all thumbnail visibility
    for (auto it = thumbnails.constBegin(); it != thumbnails.constEnd(); ++it) {
      HWND hwnd = it.key();
      ThumbnailWidget *thumbnail = it.value();
      if (!thumbnail)
        continue;

      bool isActive = (hwnd == activeWindow);

      if (highlightActive) {
        thumbnail->setActive(isActive);
      } else {
        thumbnail->setActive(false);
      }

      if (isActive) {
        thumbnail->forceUpdate();

        // Immediately restore topmost after DWM update which can disrupt
        // Z-order
        if (cfg.alwaysOnTop()) {
          thumbnail->ensureTopmost();
        }

        if (thumbnail->hasCombatEvent()) {
          QString currentEventType = thumbnail->getCombatEventType();
          if (!currentEventType.isEmpty() &&
              cfg.combatEventSuppressFocused(currentEventType)) {
            thumbnail->setCombatMessage("", "");
            qDebug() << "MainWindow: Cleared combat event for focused window:"
                     << currentEventType;
          }
        }
      }
    }

    // Use centralized visibility update
    updateAllThumbnailsVisibility();
  }
}
