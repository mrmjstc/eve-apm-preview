#ifndef HOTKEYMANAGER_H
#define HOTKEYMANAGER_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>
#include <Windows.h>

class QSettings;

struct HotkeyBinding {
  int keyCode;
  bool ctrl;
  bool alt;
  bool shift;
  bool enabled;

  HotkeyBinding()
      : keyCode(0), ctrl(false), alt(false), shift(false), enabled(false) {}

  HotkeyBinding(int key, bool c = false, bool a = false, bool s = false,
                bool en = true)
      : keyCode(key), ctrl(c), alt(a), shift(s), enabled(en) {}

  UINT getModifiers() const {
    UINT mods = 0;
    if (ctrl)
      mods |= MOD_CONTROL;
    if (alt)
      mods |= MOD_ALT;
    if (shift)
      mods |= MOD_SHIFT;
    return mods;
  }

  QString toString() const;
  static HotkeyBinding fromString(const QString &str);

  bool operator<(const HotkeyBinding &other) const;
  bool operator==(const HotkeyBinding &other) const;
};

inline size_t qHash(const HotkeyBinding &key, size_t seed = 0) {
  return qHashMulti(seed, key.keyCode, key.ctrl, key.alt, key.shift,
                    key.enabled);
}

struct CharacterHotkey {
  QString characterName;
  HotkeyBinding binding;

  CharacterHotkey() {}
  CharacterHotkey(const QString &name, const HotkeyBinding &b)
      : characterName(name), binding(b) {}
};

struct CycleGroup {
  QString groupName;
  QVector<QString> characterNames;
  HotkeyBinding forwardBinding;
  HotkeyBinding backwardBinding;
  QVector<HotkeyBinding> forwardBindings;
  QVector<HotkeyBinding> backwardBindings;
  bool includeNotLoggedIn;
  bool noLoop;

  CycleGroup() : includeNotLoggedIn(false), noLoop(false) {}
  CycleGroup(const QString &name)
      : groupName(name), includeNotLoggedIn(false), noLoop(false) {}
};

class HotkeyManager : public QObject {
  Q_OBJECT

public:
  explicit HotkeyManager(QObject *parent = nullptr);
  ~HotkeyManager();

  static HotkeyManager *instance() { return s_instance.data(); }

  bool registerHotkeys();
  void unregisterHotkeys();

  void setSuspended(bool suspended);
  bool isSuspended() const { return m_suspended; }
  void toggleSuspended();

  void setSuspendHotkeys(const QVector<HotkeyBinding> &bindings);
  QVector<HotkeyBinding> getSuspendHotkeys() const { return m_suspendHotkeys; }

  void setCharacterHotkey(const QString &characterName,
                          const HotkeyBinding &binding);
  void setCharacterHotkeys(const QString &characterName,
                           const QVector<HotkeyBinding> &bindings);
  void addCharacterHotkey(const QString &characterName,
                          const HotkeyBinding &binding);
  void removeCharacterHotkey(const QString &characterName);
  HotkeyBinding getCharacterHotkey(const QString &characterName) const;
  QVector<HotkeyBinding>
  getCharacterHotkeys(const QString &characterName) const;
  QString getCharacterForHotkey(const HotkeyBinding &binding) const;
  QHash<QString, HotkeyBinding> getAllCharacterHotkeys() const {
    return m_characterHotkeys;
  }
  QHash<QString, QVector<HotkeyBinding>> getAllCharacterMultiHotkeys() const;

  void createCycleGroup(const QString &groupName,
                        const QVector<QString> &characterNames,
                        const HotkeyBinding &forwardKey,
                        const HotkeyBinding &backwardKey);
  void createCycleGroup(const CycleGroup &group);
  void removeCycleGroup(const QString &groupName);
  CycleGroup getCycleGroup(const QString &groupName) const;
  QHash<QString, CycleGroup> getAllCycleGroups() const { return m_cycleGroups; }

  void setNotLoggedInCycleHotkeys(const HotkeyBinding &forwardKey,
                                  const HotkeyBinding &backwardKey);
  void setNotLoggedInCycleHotkeys(const QVector<HotkeyBinding> &forwardKeys,
                                  const QVector<HotkeyBinding> &backwardKeys);
  QVector<HotkeyBinding> getNotLoggedInForwardHotkeys() const {
    return m_notLoggedInForwardHotkeys;
  }
  QVector<HotkeyBinding> getNotLoggedInBackwardHotkeys() const {
    return m_notLoggedInBackwardHotkeys;
  }

  void setNonEVECycleHotkeys(const QVector<HotkeyBinding> &forwardKeys,
                             const HotkeyBinding &backwardKey);
  void setNonEVECycleHotkeys(const QVector<HotkeyBinding> &forwardKeys,
                             const QVector<HotkeyBinding> &backwardKeys);
  QVector<HotkeyBinding> getNonEVEForwardHotkeys() const {
    return m_nonEVEForwardHotkeys;
  }
  QVector<HotkeyBinding> getNonEVEBackwardHotkeys() const {
    return m_nonEVEBackwardHotkeys;
  }

  void setCloseAllClientsHotkeys(const QVector<HotkeyBinding> &bindings);
  QVector<HotkeyBinding> getCloseAllClientsHotkeys() const {
    return m_closeAllClientsHotkeys;
  }

  void setProfileHotkeys(const QString &profileName,
                         const QVector<HotkeyBinding> &bindings);
  QVector<HotkeyBinding> getProfileHotkeys(const QString &profileName) const;
  void registerProfileHotkeys();
  void unregisterProfileHotkeys();

  void uninstallMouseHook();

  void loadFromConfig();
  void saveToConfig();

  void updateCharacterWindows(const QHash<QString, HWND> &characterWindows);
  HWND getWindowForCharacter(const QString &characterName) const;
  QString getCharacterForWindow(HWND hwnd) const;

signals:
  void characterHotkeyPressed(QString characterName);
  void characterHotkeyCyclePressed(QVector<QString> characterNames);
  void namedCycleForwardPressed(QString groupName);
  void namedCycleBackwardPressed(QString groupName);
  void notLoggedInCycleForwardPressed();
  void notLoggedInCycleBackwardPressed();
  void nonEVECycleForwardPressed();
  void nonEVECycleBackwardPressed();
  void suspendedChanged(bool suspended);
  void profileSwitchRequested(QString profileName);
  void closeAllClientsRequested();

private:
  QHash<QString, HotkeyBinding> m_characterHotkeys;
  QHash<QString, QVector<HotkeyBinding>> m_characterMultiHotkeys;
  QHash<int, QString> m_hotkeyIdToCharacter;
  QHash<int, QVector<QString>> m_hotkeyIdToCharacters;
  QHash<int, QString> m_hotkeyIdToCycleGroup;
  QHash<int, bool> m_hotkeyIdIsForward;
  QHash<int, int> m_wildcardAliases;
  QHash<int, QString> m_hotkeyIdToProfile;
  QHash<QString, QVector<HotkeyBinding>> m_profileHotkeys;

  QHash<QString, HWND> m_characterWindows;
  QHash<QString, CycleGroup> m_cycleGroups;

  QVector<HotkeyBinding> m_suspendHotkeys;
  QVector<int> m_suspendHotkeyIds;
  bool m_suspended;

  QVector<HotkeyBinding> m_notLoggedInForwardHotkeys;
  QVector<HotkeyBinding> m_notLoggedInBackwardHotkeys;
  QSet<int> m_notLoggedInForwardHotkeyIds;
  QSet<int> m_notLoggedInBackwardHotkeyIds;

  QVector<HotkeyBinding> m_nonEVEForwardHotkeys;
  QVector<HotkeyBinding> m_nonEVEBackwardHotkeys;
  QSet<int> m_nonEVEForwardHotkeyIds;
  QSet<int> m_nonEVEBackwardHotkeyIds;

  QVector<HotkeyBinding> m_closeAllClientsHotkeys;
  QSet<int> m_closeAllClientsHotkeyIds;

  int m_nextHotkeyId;

  static QPointer<HotkeyManager> s_instance;
  static HHOOK s_mouseHook;

  HWND m_messageWindow;
  void createMessageWindow();
  void destroyMessageWindow();
  static LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam);

  int generateHotkeyId();

  bool registerHotkey(const HotkeyBinding &binding, int &outHotkeyId,
                      bool allowWildcard = true);
  void unregisterHotkey(int hotkeyId);

  void installMouseHook();
  static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam,
                                            LPARAM lParam);
  bool isMouseButton(int keyCode) const;
  bool hasMouseButtonHotkeys() const;
  void checkMouseButtonBindings(int vkCode, bool ctrl, bool alt, bool shift);

  void registerHotkeyList(const QVector<HotkeyBinding> &multiHotkeys);
  void registerHotkeyList(const QVector<HotkeyBinding> &multiHotkeys,
                          QSet<int> &outHotkeyIds);
  void registerHotkeyList(const QVector<HotkeyBinding> &multiHotkeys,
                          QSet<int> &outHotkeyIds, bool allowWildcard);
  void unregisterAndReset(int &hotkeyId);
  void saveHotkeyList(QSettings &settings, const QString &key,
                      const QVector<HotkeyBinding> &multiHotkeys);
  QVector<HotkeyBinding> loadHotkeyList(QSettings &settings,
                                        const QString &key);
};

#endif
