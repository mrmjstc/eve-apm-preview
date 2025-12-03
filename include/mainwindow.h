#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QVector>
#include <QPoint>
#include <QSystemTrayIcon>
#include <QMenu>
#include <memory>
#include <Windows.h>

class ThumbnailWidget;
class WindowCapture;
class HotkeyManager;
class ConfigDialog;
class ChatLogReader;
struct CycleGroup;

class MainWindow : public QObject
{
    Q_OBJECT

public:
    explicit MainWindow(QObject *parent = nullptr);
    ~MainWindow();
    
    void applySettings();

signals:
    void profileSwitchedExternally(const QString& profileName);

private slots:
    void refreshWindows();
    void updateActiveWindow();
    void onThumbnailClicked(quintptr windowId);
    void onThumbnailPositionChanged(quintptr windowId, QPoint position);
    void onGroupDragStarted(quintptr windowId);
    void onGroupDragMoved(quintptr windowId, QPoint delta);
    void onGroupDragEnded(quintptr windowId);
    void minimizeInactiveWindows();
    void showSettings();
    void exitApplication();
    void activateProfile();
    void onCharacterSystemChanged(const QString& characterName, const QString& systemName);
    void onCombatEventDetected(const QString& characterName, const QString& eventType, const QString& eventText);
    void onHotkeysSuspendedChanged(bool suspended);
    void toggleSuspendHotkeys();
    void closeAllEVEClients();

private:
    QTimer *refreshTimer;
    QTimer *minimizeTimer;
    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    QMenu *m_profilesMenu;
    QAction *m_suspendHotkeysAction;
    ConfigDialog *m_configDialog = nullptr;
    
    std::unique_ptr<WindowCapture> windowCapture;
    std::unique_ptr<HotkeyManager> hotkeyManager;
    std::unique_ptr<ChatLogReader> m_chatLogReader;
    QHash<HWND, ThumbnailWidget*> thumbnails;
    QHash<QString, HWND> m_characterToWindow;
    QHash<HWND, QString> m_windowToCharacter;
    QHash<QString, QString> m_characterSystems;  
    QHash<QString, int> m_cycleIndexByGroup;
    QHash<QString, HWND> m_lastActivatedWindowByGroup;  
    QHash<HWND, qint64> m_windowCreationTimes;
    
    QHash<HWND, bool> m_clientLocationMoveAttempted;
    
    QVector<HWND> m_notLoggedInWindows;
    int m_notLoggedInCycleIndex;
    
    QVector<HWND> m_nonEVEWindows;
    int m_nonEVECycleIndex;
    
    HWND m_hwndToActivate = nullptr;
    HWND m_hwndJustRestored = nullptr;  
    HWND m_hwndPendingRefresh = nullptr;  
    
    HWINEVENTHOOK m_eventHook = nullptr;
    HWINEVENTHOOK m_createHook = nullptr;
    HWINEVENTHOOK m_destroyHook = nullptr;
    HWINEVENTHOOK m_showHook = nullptr;  
    
    bool m_needsEnumeration = true;
    int m_enumerationCounter = 0;
    bool m_needsMappingUpdate = false;  
    
    QHash<HWND, QString> m_lastKnownTitles;
    QHash<HWND, QString> m_windowProcessNames;  
    
    QVector<ThumbnailWidget*> m_cachedThumbnailList;
    int m_lastThumbnailListSize = 0;
    
    QHash<quintptr, QPoint> m_groupDragInitialPositions;
    
    static MainWindow* s_instance;
    static void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, 
                                      LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
    static void CALLBACK WindowEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                                         LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
    
    void handleNamedCycleForward(const QString& groupName);
    void handleNamedCycleBackward(const QString& groupName);
    void handleNotLoggedInCycleForward();
    void handleNotLoggedInCycleBackward();
    void handleNonEVECycleForward();
    void handleNonEVECycleBackward();
    void handleProfileSwitch(const QString& profileName);
    void activateWindow(HWND hwnd);
    void activateCharacter(const QString& characterName);
    void updateCharacterMappings();
    void updateSnappingLists();
    void refreshSingleThumbnail(HWND hwnd);  
    QPoint calculateNotLoggedInPosition(int index);
    void updateProfilesMenu();  
    QVector<HWND> buildCycleWindowList(const CycleGroup& group);
    void saveCurrentClientLocations();
    bool tryRestoreClientLocation(HWND hwnd, const QString& characterName);
    bool isWindowRectValid(const QRect& rect);
};

#endif 
