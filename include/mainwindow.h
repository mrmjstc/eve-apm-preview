#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QLocalServer>
#include <QMenu>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSoundEffect>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVector>
#include <Windows.h>
#include <memory>

class ThumbnailWidget;
class WindowCapture;
class HotkeyManager;
class ConfigDialog;
class ChatLogReader;
class ProtocolHandler;
struct CycleGroup;

class MainWindow : public QObject {
  Q_OBJECT

public:
  explicit MainWindow(QObject *parent = nullptr);
  ~MainWindow();

  void applySettings();
  void processProtocolUrl(const QString &url);

signals:
  void profileSwitchedExternally(const QString &profileName);
  void requestRestart();

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
  void restartApplication();
  void reloadThumbnails();
  void exitApplication();
  void activateProfile();
  void onCharacterSystemChanged(const QString &characterName,
                                const QString &systemName);
  void onCombatEventDetected(const QString &characterName,
                             const QString &eventType,
                             const QString &eventText);
  void onHotkeysSuspendedChanged(bool suspended);
  void toggleSuspendHotkeys();
  void closeAllEVEClients();
  void minimizeAllEVEClients();
  void toggleThumbnailsVisibility();
  void handleCycleProfileForward();
  void handleCycleProfileBackward();

private:
  QTimer *refreshTimer;
  QTimer *minimizeTimer;
  QTimer *m_cycleThrottleTimer;
  QSystemTrayIcon *m_trayIcon;
  QMenu *m_trayMenu;
  QMenu *m_profilesMenu;
  QAction *m_suspendHotkeysAction;
  QAction *m_hideThumbnailsAction;
  ConfigDialog *m_configDialog = nullptr;

  std::unique_ptr<WindowCapture> windowCapture;
  std::unique_ptr<HotkeyManager> hotkeyManager;
  std::unique_ptr<ChatLogReader> m_chatLogReader;
  std::unique_ptr<QSoundEffect> m_soundEffect;
  std::unique_ptr<ProtocolHandler> m_protocolHandler;
  std::unique_ptr<QLocalServer> m_ipcServer;
  QHash<HWND, ThumbnailWidget *> thumbnails;
  QHash<QString, HWND> m_characterToWindow;
  QHash<HWND, QString> m_windowToCharacter;
  QHash<QString, QString> m_characterSystems;
  QHash<QString, int> m_cycleIndexByGroup;
  QHash<QString, HWND> m_lastActivatedWindowByGroup;
  QHash<HWND, qint64> m_windowCreationTimes;

  QHash<QString, int> m_characterHotkeyCycleIndex;
  QHash<QString, HWND> m_lastActivatedCharacterHotkeyWindow;

  QHash<HWND, bool> m_clientLocationMoveAttempted;
  QHash<HWND, int> m_clientLocationRetryCount;
  QHash<HWND, QTimer *> m_clientLocationRetryTimers;

  QVector<HWND> m_notLoggedInWindows;
  int m_notLoggedInCycleIndex;

  QVector<HWND> m_nonEVEWindows;
  int m_nonEVECycleIndex;

  HWND m_hwndToActivate = nullptr;
  HWND m_hwndPendingRefresh = nullptr;
  HWND m_lastActiveWindow = nullptr;

  HWINEVENTHOOK m_eventHook = nullptr;
  HWINEVENTHOOK m_createHook = nullptr;
  HWINEVENTHOOK m_destroyHook = nullptr;
  HWINEVENTHOOK m_showHook = nullptr;
  HWINEVENTHOOK m_nameChangeHook = nullptr;
  HWINEVENTHOOK m_locationHook = nullptr;
  HWINEVENTHOOK m_minimizeStartHook = nullptr;
  HWINEVENTHOOK m_minimizeEndHook = nullptr;
  HWINEVENTHOOK m_moveSizeStartHook = nullptr;
  HWINEVENTHOOK m_moveSizeEndHook = nullptr;

  bool m_needsEnumeration = true;
  bool m_needsMappingUpdate = false;
  bool m_thumbnailsManuallyHidden = false;

  QHash<HWND, QString> m_lastKnownTitles;
  QHash<HWND, QString> m_windowProcessNames;
  QHash<HWND, bool> m_windowsBeingMoved;
  QHash<HWND, QTimer *> m_locationRefreshTimers;

  QVector<ThumbnailWidget *> m_cachedThumbnailList;
  int m_lastThumbnailListSize = 0;

  QHash<quintptr, QPoint> m_groupDragInitialPositions;

  static QPointer<MainWindow> s_instance;
  static void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event,
                                    HWND hwnd, LONG idObject, LONG idChild,
                                    DWORD dwEventThread, DWORD dwmsEventTime);
  static void CALLBACK WindowEventProc(HWINEVENTHOOK hWinEventHook, DWORD event,
                                       HWND hwnd, LONG idObject, LONG idChild,
                                       DWORD dwEventThread,
                                       DWORD dwmsEventTime);

  void handleNamedCycleForward(const QString &groupName);
  void handleNamedCycleBackward(const QString &groupName);
  void handleCharacterHotkeyCycle(const QVector<QString> &characterNames);
  void handleNotLoggedInCycleForward();
  void handleNotLoggedInCycleBackward();
  void handleNonEVECycleForward();
  void handleNonEVECycleBackward();
  void handleProfileSwitch(const QString &profileName);
  void handleProtocolProfileSwitch(const QString &profileName);
  void handleProtocolCharacterActivation(const QString &characterName);
  void handleProtocolHotkeySuspend();
  void handleProtocolHotkeyResume();
  void handleProtocolThumbnailHide();
  void handleProtocolThumbnailShow();
  void handleProtocolConfigOpen();
  void handleProtocolError(const QString &url, const QString &reason);
  void handleIpcConnection();
  void updateAllCycleIndices(HWND hwnd);
  void updateCharacterHotkeyCycleIndices(HWND hwnd);
  void activateWindow(HWND hwnd);
  void activateCharacter(const QString &characterName);
  void updateCharacterMappings();
  void updateSnappingLists();
  void refreshSingleThumbnail(HWND hwnd);
  void handleWindowTitleChange(HWND hwnd);
  void scheduleLocationRefresh(HWND hwnd);
  void cleanupLocationRefreshTimer(HWND hwnd);
  QPoint calculateNotLoggedInPosition(int index);
  void updateProfilesMenu();
  QVector<HWND> buildCycleWindowList(const CycleGroup &group);
  void saveCurrentClientLocations();
  bool tryRestoreClientLocation(HWND hwnd, const QString &characterName);
  bool isWindowRectValid(const QRect &rect);
  void invalidateCycleIndicesForWindow(HWND hwnd);
};

#endif
