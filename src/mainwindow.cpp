#include "mainwindow.h"
#include "chatlogreader.h"
#include "config.h"
#include "configdialog.h"
#include "hotkeymanager.h"
#include "overlayinfo.h"
#include "thumbnailwidget.h"
#include "windowcapture.h"
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QScreen>
#include <QSet>
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

  refreshTimer = new QTimer(this);
  connect(refreshTimer, &QTimer::timeout, this, &MainWindow::refreshWindows);
  refreshTimer->start(60000);

  minimizeTimer = new QTimer(this);
  minimizeTimer->setSingleShot(true);
  connect(minimizeTimer, &QTimer::timeout, this,
          &MainWindow::minimizeInactiveWindows);

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

  hotkeyManager->registerHotkeys();
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
      }

      thumbWidget = new ThumbnailWidget(window.id, window.title, nullptr);
      thumbWidget->setFixedSize(actualThumbWidth, actualThumbHeight);

      thumbWidget->setCharacterName(displayName);
      thumbWidget->setWindowOpacity(thumbnailOpacity);
      thumbWidget->updateWindowFlags(cfg.alwaysOnTop());

      if (isEVEClient && !characterName.isEmpty()) {
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

      m_needsMappingUpdate = true;

      QPoint savedPos(-1, -1);
      bool hasSavedPosition = false;
      if (rememberPos) {
        if (isEVEClient && !characterName.isEmpty()) {
          savedPos = cfg.getThumbnailPosition(characterName);
          hasSavedPosition = (savedPos != QPoint(-1, -1));
        } else if (!isEVEClient) {
          QString uniqueId =
              QString("%1::%2").arg(window.processName, window.title);
          savedPos = cfg.getThumbnailPosition(uniqueId);
          hasSavedPosition = (savedPos != QPoint(-1, -1));
        }
      }

      bool shouldHide = false;
      if (isEVEClient && !characterName.isEmpty()) {
        shouldHide = cfg.isCharacterHidden(characterName);
      }

      if (shouldHide) {
        thumbWidget->hide();
      } else {
        thumbWidget->show();
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

            thumbWidget->show();

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

      if (isEVEClient && characterName.isEmpty()) {
        QString newDisplayName =
            showNotLoggedInOverlay ? NOT_LOGGED_IN_TEXT : "";
        thumbWidget->setCharacterName(newDisplayName);
        thumbWidget->setSystemName(QString());
        thumbWidget->show();
      } else if (!isEVEClient) {
        thumbWidget->setCharacterName(showNonEVEOverlay ? window.title : "");
      } else if (isEVEClient && !characterName.isEmpty()) {
        if (cfg.isCharacterHidden(characterName)) {
          thumbWidget->hide();
        } else {
          thumbWidget->show();
        }
      }
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

  ThumbnailWidget *thumbWidget = it.value();
  thumbWidget->forceUpdate();
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
    if (cfg.isCharacterHidden(newCharacterName)) {
      thumbWidget->hide();
      m_characterToWindow[newCharacterName] = hwnd;
      m_windowToCharacter[hwnd] = newCharacterName;
      m_needsMappingUpdate = true;
      updateCharacterMappings();
      return;
    }

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
  bool highlightActive = cfg.highlightActiveWindow();

  if (activeWindow == m_lastActiveWindow && !hideActive) {
    return;
  }

  HWND previousActiveWindow = m_lastActiveWindow;
  m_lastActiveWindow = activeWindow;

  if (activeWindow != nullptr && !thumbnails.contains(activeWindow)) {
    if (previousActiveWindow != nullptr &&
        thumbnails.contains(previousActiveWindow)) {
      auto it = thumbnails.find(previousActiveWindow);
      if (it != thumbnails.end()) {
        it.value()->setActive(false);
        if (hideActive) {
          it.value()->show();
        }
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

      if (it.value()->hasCombatEvent()) {
        it.value()->setCombatMessage("", "");
        qDebug() << "MainWindow: Cleared combat event for focused window";
      }
    }

    QString characterName = m_windowToCharacter.value(hwnd);
    bool isHidden =
        !characterName.isEmpty() && cfg.isCharacterHidden(characterName);

    if (isHidden) {
      it.value()->hide();
    } else if (hideActive && isActive) {
      it.value()->hide();
    } else {
      it.value()->show();
    }
  };

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
}

void MainWindow::handleNamedCycleBackward(const QString &groupName) {
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
}

void MainWindow::handleNotLoggedInCycleForward() {
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
}

void MainWindow::handleNotLoggedInCycleBackward() {
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
}

void MainWindow::handleNonEVECycleForward() {
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
}

void MainWindow::handleNonEVECycleBackward() {
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
}

void MainWindow::handleCharacterHotkeyCycle(
    const QVector<QString> &characterNames) {
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
  if (cfg.minimizeInactiveClients()) {
    bool wasMinimized = IsIconic(hwnd);

    if (wasMinimized) {
      ShowWindowAsync(hwnd, SW_RESTORE);

      m_hwndPendingRefresh = hwnd;
    }

    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    m_hwndToActivate = hwnd;

    minimizeTimer->start(cfg.minimizeDelay());
  } else {
    WindowCapture::activateWindow(hwnd);
  }

  updateActiveWindow();
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
    QString title = m_lastKnownTitles.value(hwnd, "");
    if (!title.isEmpty() && !processName.isEmpty()) {
      QString uniqueId = QString("%1::%2").arg(processName, title);
      cfg.setThumbnailPosition(uniqueId, position);
    }
  }
}

void MainWindow::onGroupDragStarted(quintptr windowId) {
  m_groupDragInitialPositions.clear();

  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    ThumbnailWidget *thumb = it.value();
    m_groupDragInitialPositions.insert(thumb->getWindowId(), thumb->pos());
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
  if (!cfg.rememberPositions()) {
    m_groupDragInitialPositions.clear();
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
      QString title = m_lastKnownTitles.value(hwnd, "");
      if (!title.isEmpty() && !processName.isEmpty()) {
        QString uniqueId = QString("%1::%2").arg(processName, title);
        cfg.setThumbnailPosition(uniqueId, thumb->pos());
      }
    }
  }

  m_groupDragInitialPositions.clear();
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

  // Suspend hotkeys while the configuration dialog is open to prevent
  // accidental character switches when rebinding hotkeys
  hotkeyManager->setSuspended(true);

  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    it.value()->forceOverlayRender();
  }

  connect(m_configDialog, &QObject::destroyed, this, [this]() {
    m_configDialog = nullptr;

    // Resume hotkeys when configuration dialog is closed
    hotkeyManager->setSuspended(false);

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

void MainWindow::applySettings() {
  qDebug()
      << "MainWindow::applySettings - updating thumbnails and overlay caches";
  const Config &cfg = Config::instance();
  m_cycleIndexByGroup.clear();
  m_lastActivatedWindowByGroup.clear();
  m_notLoggedInCycleIndex = -1;
  m_nonEVECycleIndex = -1;
  m_clientLocationMoveAttempted.clear();

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

  for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
    HWND hwnd = it.key();
    ThumbnailWidget *thumb = it.value();

    QString processName = m_windowProcessNames.value(hwnd, "");
    bool isEVEClient =
        processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;

    QSize newSize(thumbWidth, thumbHeight);
    if (isEVEClient) {
      QString characterName = m_windowToCharacter.value(hwnd);
      if (!characterName.isEmpty() &&
          cfg.hasCustomThumbnailSize(characterName)) {
        newSize = cfg.getThumbnailSize(characterName);
      }
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
        QString title = m_lastKnownTitles.value(hwnd, "");
        if (!title.isEmpty() && !processName.isEmpty()) {
          QString uniqueId = QString("%1::%2").arg(processName, title);
          savedPos = cfg.getThumbnailPosition(uniqueId);
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

    thumb->updateOverlays();
    thumb->forceOverlayRender();

    thumb->QWidget::update();

    if (isEVEClient) {
      QString characterName = m_windowToCharacter.value(hwnd);
      if (!characterName.isEmpty() && cfg.isCharacterHidden(characterName)) {
        thumb->hide();
      } else {

        if (thumb->isHidden()) {
          wchar_t titleBuf[256];
          if (GetWindowTextW(hwnd, titleBuf, 256) > 0) {
            QString currentTitle = QString::fromWCharArray(titleBuf);
            QString charName = OverlayInfo::extractCharacterName(currentTitle);
            if (!charName.isEmpty()) {
              thumb->setTitle(currentTitle);
              thumb->setCharacterName(charName);
            }
          }
        }

        thumb->show();
      }
    }
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

/// Reload all thumbnails to a fresh state without restarting the application
void MainWindow::reloadThumbnails() {
  qDebug() << "MainWindow: Reloading thumbnails to fresh state";

  // Clear all existing thumbnails
  qDeleteAll(thumbnails);
  thumbnails.clear();

  // Clear all state mappings
  m_characterToWindow.clear();
  m_windowToCharacter.clear();
  m_characterSystems.clear();
  m_cycleIndexByGroup.clear();
  m_lastActivatedWindowByGroup.clear();
  m_windowCreationTimes.clear();
  m_characterHotkeyCycleIndex.clear();
  m_lastActivatedCharacterHotkeyWindow.clear();
  m_clientLocationMoveAttempted.clear();
  m_lastKnownTitles.clear();
  m_windowProcessNames.clear();
  m_windowsBeingMoved.clear();
  m_cachedThumbnailList.clear();
  m_groupDragInitialPositions.clear();

  // Clear cycle indices
  m_notLoggedInWindows.clear();
  m_notLoggedInCycleIndex = -1;
  m_nonEVEWindows.clear();
  m_nonEVECycleIndex = -1;

  // Reset window handles
  m_hwndToActivate = nullptr;
  m_hwndPendingRefresh = nullptr;
  m_lastActiveWindow = nullptr;

  // Reset flags
  m_needsEnumeration = true;
  m_needsMappingUpdate = false;
  m_lastThumbnailListSize = 0;

  // Clear caches
  if (windowCapture) {
    windowCapture->clearCache();
  }
  OverlayInfo::clearCache();

  // Refresh to rebuild all thumbnails
  qDebug() << "MainWindow: Refreshing windows after reload";
  refreshWindows();
}

void MainWindow::exitApplication() { QCoreApplication::quit(); }

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
    if (hwnd == activeWindow) {
      qDebug() << "MainWindow: Suppressing combat event for focused window:"
               << characterName;
      return;
    }

    ThumbnailWidget *widget = thumbnails[hwnd];
    widget->setCombatMessage(eventText, eventType);
    qDebug() << "MainWindow: Updated thumbnail for" << characterName
             << "with combat message:" << eventText;
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

  for (const WindowInfo &window : windows) {
    if (window.handle && IsWindow(window.handle)) {
      PostMessage(window.handle, WM_CLOSE, 0, 0);
    }
  }
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
  static QHash<HWND, QTimer *> debounceTimers;

  auto &timer = debounceTimers[hwnd];
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
    return false;
  }

  if (m_clientLocationMoveAttempted.value(hwnd, false)) {
    return false;
  }

  QRect savedRect = cfg.getClientWindowRect(characterName);

  if (!isWindowRectValid(savedRect)) {
    qDebug() << "Saved window location for" << characterName
             << "is invalid or off-screen";
    m_clientLocationMoveAttempted[hwnd] = true;
    return false;
  }

  BOOL result = SetWindowPos(hwnd, nullptr, savedRect.x(), savedRect.y(),
                             savedRect.width(), savedRect.height(),
                             SWP_NOZORDER | SWP_NOACTIVATE);

  m_clientLocationMoveAttempted[hwnd] = true;

  if (result) {
    qDebug() << "Restored window location for" << characterName << "to"
             << savedRect;
    return true;
  } else {
    qDebug() << "Failed to restore window location for" << characterName;
    return false;
  }
}
