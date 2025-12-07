#ifndef HOTKEYMANAGER_H
#define HOTKEYMANAGER_H

#include <QHash>
#include <QObject>
#include <QPointer>
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

/// Hash function for HotkeyBinding to enable use as QHash key
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

  void setSuspendHotkey(const HotkeyBinding &binding);
  HotkeyBinding getSuspendHotkey() const { return m_suspendHotkey; }

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
  HotkeyBinding getNotLoggedInForwardHotkey() const {
    return m_notLoggedInForwardHotkey;
  }
  HotkeyBinding getNotLoggedInBackwardHotkey() const {
    return m_notLoggedInBackwardHotkey;
  }

  void setNonEVECycleHotkeys(const HotkeyBinding &forwardKey,
                             const HotkeyBinding &backwardKey);
  HotkeyBinding getNonEVEForwardHotkey() const { return m_nonEVEForwardHotkey; }
  HotkeyBinding getNonEVEBackwardHotkey() const {
    return m_nonEVEBackwardHotkey;
  }

  void setCloseAllClientsHotkey(const HotkeyBinding &binding);
  HotkeyBinding getCloseAllClientsHotkey() const {
    return m_closeAllClientsHotkey;
  }

  void registerProfileHotkeys();
  void unregisterProfileHotkeys();

  QString findHotkeyConflict(const HotkeyBinding &binding,
                             const QString &excludeProfile = QString()) const;
  bool hasHotkeyConflict(const HotkeyBinding &binding,
                         const QString &excludeProfile = QString()) const;

  void loadFromConfig();
  void saveToConfig();

  void updateCharacterWindows(const QHash<QString, HWND> &characterWindows);
  HWND getWindowForCharacter(const QString &characterName) const;
  QString getCharacterForWindow(HWND hwnd) const;

  static bool nativeEventFilter(void *message, long *result);

signals:
  void characterHotkeyPressed(QString characterName);
  void characterHotkeyCyclePressed(
      QVector<QString> characterNames); // Multiple characters on same hotkey
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
  QHash<int, QVector<QString>>
      m_hotkeyIdToCharacters; // Maps hotkey ID to multiple characters
  QHash<int, QString> m_hotkeyIdToCycleGroup;
  QHash<int, bool> m_hotkeyIdIsForward;
  QHash<int, int> m_wildcardAliases;
  QHash<int, QString> m_hotkeyIdToProfile;

  QHash<QString, HWND> m_characterWindows;
  QHash<QString, CycleGroup> m_cycleGroups;

  HotkeyBinding m_suspendHotkey;
  QVector<HotkeyBinding> m_suspendHotkeys;
  int m_suspendHotkeyId;
  bool m_suspended;

  HotkeyBinding m_notLoggedInForwardHotkey;
  HotkeyBinding m_notLoggedInBackwardHotkey;
  QVector<HotkeyBinding> m_notLoggedInForwardHotkeys;
  QVector<HotkeyBinding> m_notLoggedInBackwardHotkeys;
  int m_notLoggedInForwardHotkeyId;
  int m_notLoggedInBackwardHotkeyId;

  HotkeyBinding m_nonEVEForwardHotkey;
  HotkeyBinding m_nonEVEBackwardHotkey;
  QVector<HotkeyBinding> m_nonEVEForwardHotkeys;
  QVector<HotkeyBinding> m_nonEVEBackwardHotkeys;
  int m_nonEVEForwardHotkeyId;
  int m_nonEVEBackwardHotkeyId;

  HotkeyBinding m_closeAllClientsHotkey;
  QVector<HotkeyBinding> m_closeAllClientsHotkeys;
  int m_closeAllClientsHotkeyId;

  int m_nextHotkeyId;

  static QPointer<HotkeyManager> s_instance;

  int generateHotkeyId();

  bool registerHotkey(const HotkeyBinding &binding, int &outHotkeyId);
  void unregisterHotkey(int hotkeyId);

  void registerHotkeyList(const QVector<HotkeyBinding> &multiHotkeys,
                          const HotkeyBinding &legacyHotkey,
                          int &legacyHotkeyId);
  void unregisterAndReset(int &hotkeyId);
  void saveHotkeyList(QSettings &settings, const QString &key,
                      const QVector<HotkeyBinding> &multiHotkeys,
                      const HotkeyBinding &legacyHotkey);
  QVector<HotkeyBinding> loadHotkeyList(QSettings &settings, const QString &key,
                                        HotkeyBinding &outLegacyHotkey);
};

#endif
