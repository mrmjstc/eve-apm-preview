#include "mainwindow.h"
#include "windowcapture.h"
#include "thumbnailwidget.h"
#include "hotkeymanager.h"
#include "overlayinfo.h"
#include "config.h"
#include "configdialog.h"
#include "chatlogreader.h"
#include <QSet>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QIcon>
#include <QAction>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QDir>
#include <algorithm>

static const QString NOT_LOGGED_IN_TEXT = QStringLiteral("Not Logged In");
static const QString SETTINGS_TEXT = QStringLiteral("Settings");
static const QString EXIT_TEXT = QStringLiteral("Exit");
static const QString EVEO_PREVIEW_TEXT = QStringLiteral("EVE-APM Preview");
static const QString EVE_TEXT = QStringLiteral("EVE");

MainWindow* MainWindow::s_instance = nullptr;

MainWindow::MainWindow(QObject *parent)
    : QObject(parent)
    , windowCapture(std::make_unique<WindowCapture>())
    , hotkeyManager(std::make_unique<HotkeyManager>())
    , m_notLoggedInCycleIndex(-1)
    , m_nonEVECycleIndex(-1)
{
    s_instance = this;
    
    connect(hotkeyManager.get(), &HotkeyManager::characterHotkeyPressed, this, &MainWindow::activateCharacter);
    connect(hotkeyManager.get(), &HotkeyManager::namedCycleForwardPressed, this, &MainWindow::handleNamedCycleForward);
    connect(hotkeyManager.get(), &HotkeyManager::namedCycleBackwardPressed, this, &MainWindow::handleNamedCycleBackward);
    connect(hotkeyManager.get(), &HotkeyManager::notLoggedInCycleForwardPressed, this, &MainWindow::handleNotLoggedInCycleForward);
    connect(hotkeyManager.get(), &HotkeyManager::notLoggedInCycleBackwardPressed, this, &MainWindow::handleNotLoggedInCycleBackward);
    connect(hotkeyManager.get(), &HotkeyManager::nonEVECycleForwardPressed, this, &MainWindow::handleNonEVECycleForward);
    connect(hotkeyManager.get(), &HotkeyManager::nonEVECycleBackwardPressed, this, &MainWindow::handleNonEVECycleBackward);
    connect(hotkeyManager.get(), &HotkeyManager::profileSwitchRequested, this, &MainWindow::handleProfileSwitch);
    connect(hotkeyManager.get(), &HotkeyManager::suspendedChanged, this, &MainWindow::onHotkeysSuspendedChanged);
    connect(hotkeyManager.get(), &HotkeyManager::closeAllClientsRequested, this, &MainWindow::closeAllEVEClients);
    
    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &MainWindow::refreshWindows);
    refreshTimer->start(Config::instance().refreshInterval());
    
    minimizeTimer = new QTimer(this);
    minimizeTimer->setSingleShot(true);
    connect(minimizeTimer, &QTimer::timeout, this, &MainWindow::minimizeInactiveWindows);
    
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
    connect(m_suspendHotkeysAction, &QAction::triggered, this, &MainWindow::toggleSuspendHotkeys);
    m_trayMenu->addAction(m_suspendHotkeysAction);
    
    m_trayMenu->addSeparator();
    
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
    
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            showSettings();
        }
    });
    
    m_eventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr,
        WinEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );
    
    m_createHook = SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE,
        nullptr,
        WindowEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT
    );
    
    m_destroyHook = SetWinEventHook(
        EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY,
        nullptr,
        WindowEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT
    );
    
    m_showHook = SetWinEventHook(
        EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
        nullptr,
        WindowEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT
    );
    
    m_chatLogReader = std::make_unique<ChatLogReader>();
    
    QString chatLogDirectory = Config::instance().chatLogDirectory();
    QString gameLogDirectory = Config::instance().gameLogDirectory();
    m_chatLogReader->setLogDirectory(chatLogDirectory);
    m_chatLogReader->setGameLogDirectory(gameLogDirectory);
    
    bool enableChatLog = Config::instance().enableChatLogMonitoring();
    bool enableGameLog = Config::instance().enableGameLogMonitoring();
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
    
    connect(m_chatLogReader.get(), &ChatLogReader::systemChanged,
            this, &MainWindow::onCharacterSystemChanged);
    connect(m_chatLogReader.get(), &ChatLogReader::combatEventDetected,
            this, &MainWindow::onCombatEventDetected);
    
    if (enableChatLog || enableGameLog) {
        m_chatLogReader->start();
        qDebug() << "ChatLog: Monitoring started (ChatLog:" << enableChatLog << ", GameLog:" << enableGameLog << ")";
    } else {
        qDebug() << "ChatLog: Monitoring disabled in config";
    }
    
    hotkeyManager->registerHotkeys();
    refreshWindows();
}

MainWindow::~MainWindow()
{
    s_instance = nullptr;
    
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
    
    qDeleteAll(thumbnails);
    thumbnails.clear();
}

void CALLBACK MainWindow::WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                                       LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    Q_UNUSED(hWinEventHook);
    Q_UNUSED(event);
    Q_UNUSED(hwnd);
    Q_UNUSED(idObject);
    Q_UNUSED(idChild);
    Q_UNUSED(dwEventThread);
    Q_UNUSED(dwmsEventTime);
    
    if (s_instance) {
        QMetaObject::invokeMethod(s_instance, "updateActiveWindow", Qt::QueuedConnection);
    }
}

void CALLBACK MainWindow::WindowEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                                          LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    Q_UNUSED(hWinEventHook);
    Q_UNUSED(dwEventThread);
    Q_UNUSED(dwmsEventTime);
    
    if (idObject != OBJID_WINDOW) {
        return;
    }
    
    if (s_instance) {
        if (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_DESTROY) {
            s_instance->m_needsEnumeration = true;
        }
        else if (event == EVENT_OBJECT_SHOW) {
            if (hwnd == s_instance->m_hwndPendingRefresh) {
                QMetaObject::invokeMethod(s_instance, [hwnd]() {
                    if (s_instance) {
                        s_instance->refreshSingleThumbnail(hwnd);
                        s_instance->m_hwndPendingRefresh = nullptr;
                    }
                }, Qt::QueuedConnection);
            }
        }
    }
}

void MainWindow::refreshWindows()
{
    const Config& cfg = Config::instance();
    const int thumbWidth = cfg.thumbnailWidth();
    const int thumbHeight = cfg.thumbnailHeight();
    const bool rememberPos = cfg.rememberPositions();
    const bool showNotLoggedIn = cfg.showNotLoggedInClients();
    const bool showNotLoggedInOverlay = cfg.showNotLoggedInOverlay();
    const bool showNonEVEOverlay = cfg.showNonEVEOverlay();
    const double thumbnailOpacity = cfg.thumbnailOpacity() / 100.0;  
    
    m_enumerationCounter++;
    if (m_enumerationCounter >= 10) {
        m_needsEnumeration = true;
        m_enumerationCounter = 0;
    }
    
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
            
            QString title = windowCapture->getWindowTitle(hwnd);
            QString processName = m_windowProcessNames.value(hwnd, "exefile.exe");  
            qint64 creationTime = m_windowCreationTimes.value(hwnd, 0);
            
            windows.append(WindowInfo(hwnd, title, processName, creationTime));
        }
    }
    
    QSet<HWND> currentWindows;
    QSet<HWND> newWindows;
    for (const auto& window : windows) {
        currentWindows.insert(window.handle);
        if (!thumbnails.contains(window.handle)) {
            newWindows.insert(window.handle);
        }
    }
    
    if (!newWindows.isEmpty()) {
        std::sort(windows.begin(), windows.end(), [](const WindowInfo& a, const WindowInfo& b) {
            return a.creationTime < b.creationTime;
        });
    }
    
    auto it = thumbnails.begin();
    while (it != thumbnails.end()) {
        if (!currentWindows.contains(it.key())) {
            m_lastKnownTitles.remove(it.key());
            it.value()->deleteLater();
            it = thumbnails.erase(it);
        } else {
            ++it;
        }
    }
    
    int loggedInClientsWithoutSavedPos = 0;
    for (const auto& window : windows) {
        QString charName = OverlayInfo::extractCharacterName(window.title);
        if (!charName.isEmpty()) {
            QPoint savedPos = rememberPos 
                ? cfg.getThumbnailPosition(charName) 
                : QPoint(-1, -1);
            
            if (thumbnails.contains(window.handle) && (savedPos.x() < 0 || savedPos.y() < 0)) {
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
    if (thumbnailsPerRow < 1) thumbnailsPerRow = 1;
    
    if (loggedInClientsWithoutSavedPos > 0) {
        currentRow = loggedInClientsWithoutSavedPos / thumbnailsPerRow;
        int colInRow = loggedInClientsWithoutSavedPos % thumbnailsPerRow;
        xOffset = margin + (colInRow * (thumbWidth + margin));
        yOffset = margin + (currentRow * (thumbHeight + margin));
    }
    
    int notLoggedInCount = 0;
    
    for (const auto& window : windows) {
        m_windowCreationTimes[window.handle] = window.creationTime;
        m_windowProcessNames[window.handle] = window.processName;
        
        bool isEVEClient = window.processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;
        
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
            thumbWidget = new ThumbnailWidget(window.id, window.title, nullptr);
            thumbWidget->setFixedSize(thumbWidth, thumbHeight);
            
            thumbWidget->setCharacterName(displayName);
            thumbWidget->setWindowOpacity(thumbnailOpacity);
            
            if (isEVEClient && !characterName.isEmpty()) {
                QString cachedSystem = m_characterSystems.value(characterName);
                if (!cachedSystem.isEmpty()) {
                    thumbWidget->setSystemName(cachedSystem);
                }
            }
            
            connect(thumbWidget, &ThumbnailWidget::clicked, this, &MainWindow::onThumbnailClicked);
            connect(thumbWidget, &ThumbnailWidget::positionChanged, this, &MainWindow::onThumbnailPositionChanged);
            connect(thumbWidget, &ThumbnailWidget::groupDragStarted, this, &MainWindow::onGroupDragStarted);
            connect(thumbWidget, &ThumbnailWidget::groupDragMoved, this, &MainWindow::onGroupDragMoved);
            connect(thumbWidget, &ThumbnailWidget::groupDragEnded, this, &MainWindow::onGroupDragEnded);
            
            thumbnails.insert(window.handle, thumbWidget);
            
            m_needsMappingUpdate = true;
            
            QPoint savedPos(-1, -1);
            bool hasSavedPosition = false;
            if (rememberPos) {
                if (isEVEClient && !characterName.isEmpty()) {
                    savedPos = cfg.getThumbnailPosition(characterName);
                    hasSavedPosition = (savedPos != QPoint(-1, -1));
                } else if (!isEVEClient) {
                    QString uniqueId = QString("%1::%2").arg(window.processName, window.title);
                    savedPos = cfg.getThumbnailPosition(uniqueId);
                    hasSavedPosition = (savedPos != QPoint(-1, -1));
                }
            }
            
            thumbWidget->show();
            
            if (hasSavedPosition) {
                QRect thumbRect(savedPos, QSize(thumbWidth, thumbHeight));
                QScreen* targetScreen = nullptr;
                for (QScreen* screen : QGuiApplication::screens()) {
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
                thumbWidget->setTitle(window.title);
                m_lastKnownTitles.insert(window.handle, window.title);
                
                if (isEVEClient) {
                    QString lastCharacterName = OverlayInfo::extractCharacterName(lastTitle);
                    bool wasNotLoggedIn = lastCharacterName.isEmpty();
                    bool isNowLoggedIn = !characterName.isEmpty();
                    bool wasLoggedIn = !lastCharacterName.isEmpty();
                    bool isNowNotLoggedIn = characterName.isEmpty();
                    
                    if (wasNotLoggedIn && isNowLoggedIn) {
                        thumbWidget->setCharacterName(characterName);
                        
                        QString cachedSystem = m_characterSystems.value(characterName);
                        if (!cachedSystem.isEmpty()) {
                            thumbWidget->setSystemName(cachedSystem);
                        } else {
                            thumbWidget->setSystemName(QString());
                        }
                        
                        // Try to restore client window location
                        tryRestoreClientLocation(window.handle, characterName);
                        
                        if (rememberPos) {
                            QPoint savedPos = cfg.getThumbnailPosition(characterName);
                            if (savedPos != QPoint(-1, -1)) {
                                QRect thumbRect(savedPos, QSize(thumbWidth, thumbHeight));
                                for (QScreen* screen : QGuiApplication::screens()) {
                                    if (screen->geometry().intersects(thumbRect)) {
                                        thumbWidget->move(savedPos);
                                        break;
                                    }
                                }
                            }
                        }
                        m_needsMappingUpdate = true;
                    } else if (wasLoggedIn && isNowNotLoggedIn) {
                        QString newDisplayName = showNotLoggedInOverlay ? NOT_LOGGED_IN_TEXT : "";
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
            
            if (isEVEClient && characterName.isEmpty()) {
                QString newDisplayName = showNotLoggedInOverlay ? NOT_LOGGED_IN_TEXT : "";
                thumbWidget->setCharacterName(newDisplayName);
                thumbWidget->setSystemName(QString());
            } else if (!isEVEClient) {
                thumbWidget->setCharacterName(showNonEVEOverlay ? window.title : "");
            }
        }
    }
    
    updateSnappingLists();
    updateActiveWindow();
    updateCharacterMappings();
}

void MainWindow::updateSnappingLists()
{
    if (thumbnails.size() != m_lastThumbnailListSize) {
        m_cachedThumbnailList = thumbnails.values().toVector();
        m_lastThumbnailListSize = thumbnails.size();
        
        for (auto* thumb : m_cachedThumbnailList) {
            thumb->setOtherThumbnails(m_cachedThumbnailList);
        }
    }
}

void MainWindow::updateCharacterMappings()
{
    static int lastThumbnailCount = -1;
    if (thumbnails.size() == lastThumbnailCount && lastThumbnailCount > 0 && !m_needsMappingUpdate) {
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
        ThumbnailWidget* widget = it.value();
        
        QString processName = m_windowProcessNames.value(hwnd, "");
        bool isEVEClient = processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;
        
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
                return m_windowCreationTimes.value(a, 0) < m_windowCreationTimes.value(b, 0);
            });
        lastNotLoggedInCount = m_notLoggedInWindows.size();
    }
    
    static int lastNonEVECount = 0;
    if (m_nonEVEWindows.size() != lastNonEVECount) {
        std::sort(m_nonEVEWindows.begin(), m_nonEVEWindows.end(), 
            [this](HWND a, HWND b) {
                return m_windowCreationTimes.value(a, 0) < m_windowCreationTimes.value(b, 0);
            });
        lastNonEVECount = m_nonEVEWindows.size();
    }
    
    hotkeyManager->updateCharacterWindows(m_characterToWindow);
    
    if (m_chatLogReader && (Config::instance().enableChatLogMonitoring() || Config::instance().enableGameLogMonitoring())) {
        QStringList characterNames = m_characterToWindow.keys();
        m_chatLogReader->setCharacterNames(characterNames);
    }
}

void MainWindow::refreshSingleThumbnail(HWND hwnd)
{
    auto it = thumbnails.find(hwnd);
    if (it == thumbnails.end()) {
        return;  
    }
    
    ThumbnailWidget* thumbWidget = it.value();
    thumbWidget->forceUpdate();
}

QPoint MainWindow::calculateNotLoggedInPosition(int index)
{
    const Config& cfg = Config::instance();
    int stackMode = cfg.notLoggedInStackMode();  
    
    int thumbWidth = cfg.thumbnailWidth();
    int thumbHeight = cfg.thumbnailHeight();
    int spacing = 10;
    
    // Use the custom reference position
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

void MainWindow::updateActiveWindow()
{
    const Config& cfg = Config::instance();
    HWND activeWindow = GetForegroundWindow();
    bool hideActive = cfg.hideActiveClientThumbnail();
    bool highlightActive = cfg.highlightActiveWindow();
    
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
        bool isActive = (it.key() == activeWindow);
        
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
        
        if (hideActive && isActive) {
            it.value()->hide();
        } else {
            it.value()->show();
        }
    }
}

void MainWindow::onThumbnailClicked(quintptr windowId)
{
    HWND hwnd = reinterpret_cast<HWND>(windowId);
    activateWindow(hwnd);
    
    QHash<QString, CycleGroup> allGroups = hotkeyManager->getAllCycleGroups();
    for (auto it = allGroups.begin(); it != allGroups.end(); ++it) {
        const QString& groupName = it.key();
        const CycleGroup& group = it.value();
        
        QVector<HWND> windowsToCycle = buildCycleWindowList(group);
        
        int index = windowsToCycle.indexOf(hwnd);
        if (index != -1) {
            m_cycleIndexByGroup[groupName] = index;
            m_lastActivatedWindowByGroup[groupName] = hwnd;  
        }
    }
    
    int notLoggedInIndex = m_notLoggedInWindows.indexOf(hwnd);
    if (notLoggedInIndex != -1) {
        m_notLoggedInCycleIndex = notLoggedInIndex;
    }
}

QVector<HWND> MainWindow::buildCycleWindowList(const CycleGroup& group)
{
    QVector<HWND> windowsToCycle;
    
    for (const QString& characterName : group.characterNames) {
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

void MainWindow::handleNamedCycleForward(const QString& groupName)
{
    CycleGroup group = hotkeyManager->getCycleGroup(groupName);
    QVector<HWND> windowsToCycle = buildCycleWindowList(group);
    
    if (windowsToCycle.isEmpty()) {
        return;
    }
    
    int currentIndex = -1;
    HWND lastActivatedWindow = m_lastActivatedWindowByGroup.value(groupName, nullptr);
    if (lastActivatedWindow) {
        currentIndex = windowsToCycle.indexOf(lastActivatedWindow);
    }
    
    if (currentIndex == -1) {
        currentIndex = m_cycleIndexByGroup.value(groupName, -1);
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

void MainWindow::handleNamedCycleBackward(const QString& groupName)
{
    CycleGroup group = hotkeyManager->getCycleGroup(groupName);
    QVector<HWND> windowsToCycle = buildCycleWindowList(group);
    
    if (windowsToCycle.isEmpty()) {
        return;
    }
    
    int currentIndex = -1;
    HWND lastActivatedWindow = m_lastActivatedWindowByGroup.value(groupName, nullptr);
    if (lastActivatedWindow) {
        currentIndex = windowsToCycle.indexOf(lastActivatedWindow);
    }
    
    if (currentIndex == -1) {
        currentIndex = m_cycleIndexByGroup.value(groupName, 0);
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

void MainWindow::handleNotLoggedInCycleForward()
{
    if (m_notLoggedInWindows.isEmpty()) {
        return;
    }
    
    m_notLoggedInWindows.erase(
        std::remove_if(m_notLoggedInWindows.begin(), m_notLoggedInWindows.end(),
            [](HWND hwnd) { return !IsWindow(hwnd); }),
        m_notLoggedInWindows.end()
    );
    
    if (m_notLoggedInWindows.isEmpty()) {
        return;
    }
    
    m_notLoggedInCycleIndex++;
    if (m_notLoggedInCycleIndex >= m_notLoggedInWindows.size()) {
        m_notLoggedInCycleIndex = 0;
    }
    
    HWND hwnd = m_notLoggedInWindows[m_notLoggedInCycleIndex];
    activateWindow(hwnd);
}

void MainWindow::handleNotLoggedInCycleBackward()
{
    if (m_notLoggedInWindows.isEmpty()) {
        return;
    }
    
    m_notLoggedInWindows.erase(
        std::remove_if(m_notLoggedInWindows.begin(), m_notLoggedInWindows.end(),
            [](HWND hwnd) { return !IsWindow(hwnd); }),
        m_notLoggedInWindows.end()
    );
    
    if (m_notLoggedInWindows.isEmpty()) {
        return;
    }
    
    m_notLoggedInCycleIndex--;
    if (m_notLoggedInCycleIndex < 0) {
        m_notLoggedInCycleIndex = m_notLoggedInWindows.size() - 1;
    }
    
    HWND hwnd = m_notLoggedInWindows[m_notLoggedInCycleIndex];
    activateWindow(hwnd);
}

void MainWindow::handleNonEVECycleForward()
{
    if (m_nonEVEWindows.isEmpty()) {
        return;
    }
    
    m_nonEVEWindows.erase(
        std::remove_if(m_nonEVEWindows.begin(), m_nonEVEWindows.end(),
            [](HWND hwnd) { return !IsWindow(hwnd); }),
        m_nonEVEWindows.end()
    );
    
    if (m_nonEVEWindows.isEmpty()) {
        return;
    }
    
    m_nonEVECycleIndex++;
    if (m_nonEVECycleIndex >= m_nonEVEWindows.size()) {
        m_nonEVECycleIndex = 0;
    }
    
    HWND hwnd = m_nonEVEWindows[m_nonEVECycleIndex];
    activateWindow(hwnd);
}

void MainWindow::handleNonEVECycleBackward()
{
    if (m_nonEVEWindows.isEmpty()) {
        return;
    }
    
    m_nonEVEWindows.erase(
        std::remove_if(m_nonEVEWindows.begin(), m_nonEVEWindows.end(),
            [](HWND hwnd) { return !IsWindow(hwnd); }),
        m_nonEVEWindows.end()
    );
    
    if (m_nonEVEWindows.isEmpty()) {
        return;
    }
    
    m_nonEVECycleIndex--;
    if (m_nonEVECycleIndex < 0) {
        m_nonEVECycleIndex = m_nonEVEWindows.size() - 1;
    }
    
    HWND hwnd = m_nonEVEWindows[m_nonEVECycleIndex];
    activateWindow(hwnd);
}

void MainWindow::activateCharacter(const QString& characterName)
{
    HWND hwnd = hotkeyManager->getWindowForCharacter(characterName);
    if (hwnd) {
        activateWindow(hwnd);
        
        QHash<QString, CycleGroup> allGroups = hotkeyManager->getAllCycleGroups();
        for (auto it = allGroups.begin(); it != allGroups.end(); ++it)
        {
            const QString& groupName = it.key();
            const CycleGroup& group = it.value();
            
            QVector<HWND> windowsToCycle = buildCycleWindowList(group);
            
            int index = windowsToCycle.indexOf(hwnd);
            if (index != -1)
            {
                m_cycleIndexByGroup[groupName] = index;
                m_lastActivatedWindowByGroup[groupName] = hwnd;  
            }
        }
    }
}

void MainWindow::activateWindow(HWND hwnd)
{
    const Config& cfg = Config::instance();
    if (cfg.minimizeInactiveClients()) {
        bool wasMinimized = IsIconic(hwnd);
        
        if (wasMinimized) {
            ShowWindowAsync(hwnd, SW_RESTORE);
            m_hwndJustRestored = hwnd;  
            
            m_hwndPendingRefresh = hwnd;
        }
        
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        
        m_hwndToActivate = hwnd;
        
        minimizeTimer->start(cfg.minimizeDelay());
    } else {
        WindowCapture::activateWindow(hwnd);
    }
}

void MainWindow::minimizeInactiveWindows()
{
    const Config& cfg = Config::instance();
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
        HWND otherHwnd = it.key();
        if (otherHwnd != m_hwndToActivate && IsWindow(otherHwnd)) {
            QString characterName = m_windowToCharacter.value(otherHwnd);
            if (!characterName.isEmpty() && cfg.isCharacterNeverMinimize(characterName)) {
                continue;
            }
            
            ShowWindowAsync(otherHwnd, 11);
        }
    }
}

void MainWindow::onThumbnailPositionChanged(quintptr windowId, QPoint position)
{
    Config& cfg = Config::instance();
    if (!cfg.rememberPositions()) {
        return;
    }
    
    HWND hwnd = reinterpret_cast<HWND>(windowId);
    
    QString processName = m_windowProcessNames.value(hwnd, "");
    bool isEVEClient = processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;
    
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

void MainWindow::onGroupDragStarted(quintptr windowId)
{
    m_groupDragInitialPositions.clear();
    
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
        ThumbnailWidget* thumb = it.value();
        m_groupDragInitialPositions.insert(thumb->getWindowId(), thumb->pos());
    }
}

void MainWindow::onGroupDragMoved(quintptr windowId, QPoint delta)
{
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
        ThumbnailWidget* thumb = it.value();
        
        if (thumb->getWindowId() == windowId) {
            continue;
        }
        
        QPoint initialPos = m_groupDragInitialPositions.value(thumb->getWindowId());
        QPoint newPos = initialPos + delta;
        
        thumb->move(newPos);
    }
}

void MainWindow::onGroupDragEnded(quintptr windowId)
{
    Q_UNUSED(windowId);
    
    Config& cfg = Config::instance();
    if (!cfg.rememberPositions()) {
        m_groupDragInitialPositions.clear();
        return;
    }
    
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
        HWND hwnd = it.key();
        ThumbnailWidget* thumb = it.value();
        
        QString processName = m_windowProcessNames.value(hwnd, "");
        bool isEVEClient = processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;
        
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

void MainWindow::showSettings()
{
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
    
    connect(m_configDialog, &ConfigDialog::settingsApplied, this, &MainWindow::applySettings);
    connect(m_configDialog, &ConfigDialog::saveClientLocationsRequested, this, &MainWindow::saveCurrentClientLocations);
    
    connect(this, &MainWindow::profileSwitchedExternally, m_configDialog, &ConfigDialog::onExternalProfileSwitch);
    
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
        it.value()->update();
    }
    
    connect(m_configDialog, &QObject::destroyed, this, [this]() {
        m_configDialog = nullptr;
        for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
            it.value()->update();
        }
        updateProfilesMenu();
    });
    
    m_configDialog->show();
}

void MainWindow::handleProfileSwitch(const QString& profileName)
{
    Config& cfg = Config::instance();
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

void MainWindow::applySettings()
{
    qDebug() << "MainWindow::applySettings - updating thumbnails and overlay caches";
    const Config& cfg = Config::instance();
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
    
    for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
        HWND hwnd = it.key();
        ThumbnailWidget* thumb = it.value();
        
        QSize currentSize = thumb->size();
        QSize newSize(cfg.thumbnailWidth(), cfg.thumbnailHeight());
        if (currentSize != newSize) {
            thumb->setFixedSize(newSize);
        }
        
        thumb->setWindowOpacity(cfg.thumbnailOpacity() / 100.0);  
        
        thumb->updateWindowFlags(cfg.alwaysOnTop());
        
        if (cfg.rememberPositions()) {
            QString processName = m_windowProcessNames.value(hwnd, "");
            bool isEVEClient = processName.compare("exefile.exe", Qt::CaseInsensitive) == 0;
            
            QPoint savedPos(-1, -1);
            bool hasSavedPosition = false;
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
            
            if (hasSavedPosition) {
                int thumbWidth = cfg.thumbnailWidth();
                int thumbHeight = cfg.thumbnailHeight();
                QRect thumbRect(savedPos, QSize(thumbWidth, thumbHeight));
                QScreen* targetScreen = nullptr;
                for (QScreen* screen : QGuiApplication::screens()) {
                    if (screen->geometry().intersects(thumbRect)) {
                        targetScreen = screen;
                        break;
                    }
                }
                
                if (targetScreen) {
                    thumb->move(savedPos);
                }
            }
        }
        
    thumb->updateOverlays();
    thumb->forceOverlayRender();
        
        thumb->QWidget::update();
    }
    
    refreshTimer->setInterval(cfg.refreshInterval());
    
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
        
        // Update character names before starting/refreshing monitoring
        if (shouldMonitor) {
            QStringList characterNames = m_characterToWindow.keys();
            m_chatLogReader->setCharacterNames(characterNames);
        }
        
        if (shouldMonitor && !m_chatLogReader->isMonitoring()) {
            m_chatLogReader->start();
            qDebug() << "ChatLog: Monitoring started via settings (ChatLog:" << enableChatLog << ", GameLog:" << enableGameLog << ")";
        } else if (!shouldMonitor && m_chatLogReader->isMonitoring()) {
            m_chatLogReader->stop();
            qDebug() << "ChatLog: Monitoring stopped via settings";
        } else if (shouldMonitor && m_chatLogReader->isMonitoring()) {
            // Monitoring is active and should remain active, but settings may have changed
            m_chatLogReader->refreshMonitoring();
            qDebug() << "ChatLog: Monitoring refreshed via settings (ChatLog:" << enableChatLog << ", GameLog:" << enableGameLog << ")";
        }
    }
    
    updateActiveWindow();
    
    refreshWindows();
}

void MainWindow::exitApplication()
{
    QCoreApplication::quit();
}

void MainWindow::onCharacterSystemChanged(const QString& characterName, const QString& systemName)
{
    qDebug() << "MainWindow: Character" << characterName << "moved to system" << systemName;
    
    m_characterSystems[characterName] = systemName;
    
    HWND hwnd = m_characterToWindow.value(characterName);
    if (hwnd && thumbnails.contains(hwnd)) {
        ThumbnailWidget* widget = thumbnails[hwnd];
        widget->setSystemName(systemName);
        qDebug() << "MainWindow: Updated thumbnail for" << characterName << "with system:" << systemName;
    }
}

void MainWindow::onCombatEventDetected(const QString& characterName, const QString& eventType, const QString& eventText)
{
    qDebug() << "MainWindow: Combat event for" << characterName << "- Type:" << eventType << "- Text:" << eventText;
    
    const Config& cfg = Config::instance();
    if (!cfg.showCombatMessages()) {
        qDebug() << "MainWindow: Combat messages disabled in settings";
        return;
    }
    
    if (!cfg.isCombatEventTypeEnabled(eventType)) {
        qDebug() << "MainWindow: Event type" << eventType << "is disabled in settings";
        return;
    }
    
    HWND hwnd = m_characterToWindow.value(characterName);
    if (hwnd && thumbnails.contains(hwnd)) {
        HWND activeWindow = GetForegroundWindow();
        if (hwnd == activeWindow) {
            qDebug() << "MainWindow: Suppressing combat event for focused window:" << characterName;
            return;
        }
        
        ThumbnailWidget* widget = thumbnails[hwnd];
        widget->setCombatMessage(eventText, eventType);
        qDebug() << "MainWindow: Updated thumbnail for" << characterName << "with combat message:" << eventText;
    }
}

void MainWindow::updateProfilesMenu()
{
    if (!m_profilesMenu) {
        return;
    }
    
    const Config& cfg = Config::instance();
    m_profilesMenu->clear();
    
    QStringList profiles = cfg.listProfiles();
    QString currentProfile = cfg.getCurrentProfileName();
    
    if (profiles.isEmpty()) {
        QAction* noProfilesAction = new QAction("No profiles available", m_profilesMenu);
        noProfilesAction->setEnabled(false);
        m_profilesMenu->addAction(noProfilesAction);
        return;
    }
    
    for (const QString& profileName : profiles) {
        QAction* profileAction = new QAction(profileName, m_profilesMenu);
        profileAction->setCheckable(true);
        
        if (profileName == currentProfile) {
            profileAction->setChecked(true);
        }
        
        profileAction->setData(profileName);
        
        connect(profileAction, &QAction::triggered, this, &MainWindow::activateProfile);
        
        m_profilesMenu->addAction(profileAction);
    }
}

void MainWindow::activateProfile()
{
    QAction* action = qobject_cast<QAction*>(sender());
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

void MainWindow::toggleSuspendHotkeys()
{
    if (hotkeyManager) {
        hotkeyManager->toggleSuspended();
    }
}

void MainWindow::onHotkeysSuspendedChanged(bool suspended)
{
    if (m_suspendHotkeysAction) {
        m_suspendHotkeysAction->setChecked(suspended);
    }
}

void MainWindow::closeAllEVEClients()
{
    QVector<WindowInfo> windows = windowCapture->getEVEWindows();
    
    for (const WindowInfo& window : windows)
    {
        if (window.handle && IsWindow(window.handle))
        {
            PostMessage(window.handle, WM_CLOSE, 0, 0);
        }
    }
}

void MainWindow::saveCurrentClientLocations()
{
    Config& cfg = Config::instance();
    int savedCount = 0;
    
    // Iterate through all known windows
    for (auto it = m_windowToCharacter.constBegin(); it != m_windowToCharacter.constEnd(); ++it) {
        HWND hwnd = it.key();
        QString characterName = it.value();
        
        if (!IsWindow(hwnd) || characterName.isEmpty()) {
            continue;
        }
        
        // Get window rectangle
        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
            QRect qRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
            cfg.setClientWindowRect(characterName, qRect);
            savedCount++;
            qDebug() << "Saved window location for" << characterName << ":" << qRect;
        }
    }
    
    cfg.save();
    qDebug() << "Saved" << savedCount << "client window locations";
}

bool MainWindow::isWindowRectValid(const QRect& rect)
{
    if (rect.isNull() || rect.isEmpty()) {
        return false;
    }
    
    // Check if any part of the window is on a valid monitor
    RECT winRect = { rect.left(), rect.top(), rect.right(), rect.bottom() };
    HMONITOR hMonitor = MonitorFromRect(&winRect, MONITOR_DEFAULTTONULL);
    
    return (hMonitor != nullptr);
}

bool MainWindow::tryRestoreClientLocation(HWND hwnd, const QString& characterName)
{
    const Config& cfg = Config::instance();
    
    if (!cfg.saveClientLocation()) {
        return false;
    }
    
    // Check if we've already attempted to move this window
    if (m_clientLocationMoveAttempted.value(hwnd, false)) {
        return false;
    }
    
    // Get saved window rect
    QRect savedRect = cfg.getClientWindowRect(characterName);
    
    if (!isWindowRectValid(savedRect)) {
        qDebug() << "Saved window location for" << characterName << "is invalid or off-screen";
        m_clientLocationMoveAttempted[hwnd] = true;
        return false;
    }
    
    // Attempt to move the window
    BOOL result = SetWindowPos(
        hwnd,
        nullptr,
        savedRect.x(),
        savedRect.y(),
        savedRect.width(),
        savedRect.height(),
        SWP_NOZORDER | SWP_NOACTIVATE
    );
    
    m_clientLocationMoveAttempted[hwnd] = true;
    
    if (result) {
        qDebug() << "Restored window location for" << characterName << "to" << savedRect;
        return true;
    } else {
        qDebug() << "Failed to restore window location for" << characterName;
        return false;
    }
}

