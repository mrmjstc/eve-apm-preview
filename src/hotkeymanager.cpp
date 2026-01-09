#include "hotkeymanager.h"
#include "config.h"
#include "uiohookmanager.h"
#include "windowcapture.h"
#include <Psapi.h>
#include <QSettings>
#include <QStringList>

QPointer<HotkeyManager> HotkeyManager::s_instance;

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent), m_nextHotkeyId(1000), m_suspended(false),
      m_messageWindow(nullptr), m_uiohookManager(nullptr) {
  s_instance = this;
  createMessageWindow();

  // Create and configure UIohookManager
  m_uiohookManager = new UIohookManager(this);
  m_uiohookManager->setHotkeyManager(this);

  loadFromConfig();
}

HotkeyManager::~HotkeyManager() {
  unregisterHotkeys();
  uninstallMouseHook();
  destroyMessageWindow();
  s_instance.clear();
}

bool HotkeyManager::registerHotkey(const HotkeyBinding &binding,
                                   int &outHotkeyId, bool allowWildcard) {
  if (!binding.enabled)
    return false;

  if (isMouseButton(binding.keyCode)) {
    outHotkeyId = -1;
    return true;
  }

  UINT modifiers = 0;
  if (binding.ctrl)
    modifiers |= MOD_CONTROL;
  if (binding.alt)
    modifiers |= MOD_ALT;
  if (binding.shift)
    modifiers |= MOD_SHIFT;

  int hotkeyId = m_nextHotkeyId++;

  const Config &cfg = Config::instance();
  bool wildcardMode = cfg.wildcardHotkeys() && allowWildcard;

  if (RegisterHotKey(m_messageWindow, hotkeyId, modifiers, binding.keyCode)) {
    outHotkeyId = hotkeyId;

    if (wildcardMode) {
      QVector<UINT> additionalMods;

      if (!binding.ctrl) {
        additionalMods.append(modifiers | MOD_CONTROL);
      }

      if (!binding.alt) {
        additionalMods.append(modifiers | MOD_ALT);
      }

      if (!binding.shift) {
        additionalMods.append(modifiers | MOD_SHIFT);
      }

      if (!binding.ctrl && !binding.alt) {
        additionalMods.append(modifiers | MOD_CONTROL | MOD_ALT);
      }
      if (!binding.ctrl && !binding.shift) {
        additionalMods.append(modifiers | MOD_CONTROL | MOD_SHIFT);
      }
      if (!binding.alt && !binding.shift) {
        additionalMods.append(modifiers | MOD_ALT | MOD_SHIFT);
      }
      if (!binding.ctrl && !binding.alt && !binding.shift) {
        additionalMods.append(modifiers | MOD_CONTROL | MOD_ALT | MOD_SHIFT);
      }

      for (UINT extraMod : additionalMods) {
        int extraHotkeyId = m_nextHotkeyId++;
        if (RegisterHotKey(m_messageWindow, extraHotkeyId, extraMod,
                           binding.keyCode)) {
          m_wildcardAliases.insert(extraHotkeyId, hotkeyId);
        }
      }
    }

    return true;
  } else {
    return false;
  }
}

void HotkeyManager::unregisterHotkey(int hotkeyId) {
  UnregisterHotKey(m_messageWindow, hotkeyId);
}

void HotkeyManager::registerHotkeyList(
    const QVector<HotkeyBinding> &multiHotkeys) {
  for (const HotkeyBinding &binding : multiHotkeys) {
    if (!binding.enabled)
      continue;
    int hotkeyId;
    registerHotkey(binding, hotkeyId);
  }
}

void HotkeyManager::registerHotkeyList(
    const QVector<HotkeyBinding> &multiHotkeys, QSet<int> &outHotkeyIds) {
  outHotkeyIds.clear();
  for (const HotkeyBinding &binding : multiHotkeys) {
    if (!binding.enabled)
      continue;
    int hotkeyId;
    if (registerHotkey(binding, hotkeyId)) {
      outHotkeyIds.insert(hotkeyId);
    }
  }
}

void HotkeyManager::registerHotkeyList(
    const QVector<HotkeyBinding> &multiHotkeys, QSet<int> &outHotkeyIds,
    bool allowWildcard) {
  outHotkeyIds.clear();
  for (const HotkeyBinding &binding : multiHotkeys) {
    if (!binding.enabled)
      continue;
    int hotkeyId;
    if (registerHotkey(binding, hotkeyId, allowWildcard)) {
      outHotkeyIds.insert(hotkeyId);
    }
  }
}

void HotkeyManager::unregisterAndReset(int &hotkeyId) {
  if (hotkeyId != -1) {
    unregisterHotkey(hotkeyId);
    hotkeyId = -1;
  }
}

void HotkeyManager::saveHotkeyList(QSettings &settings, const QString &key,
                                   const QVector<HotkeyBinding> &multiHotkeys) {
  settings.remove(key);
  QStringList bindingStrs;
  for (const HotkeyBinding &binding : multiHotkeys) {
    bindingStrs.append(binding.toString());
  }
  settings.setValue(key, bindingStrs.join('|'));
}

QVector<HotkeyBinding> HotkeyManager::loadHotkeyList(QSettings &settings,
                                                     const QString &key) {
  QString value = settings.value(key, QString()).toString();
  QVector<HotkeyBinding> result;

  QStringList bindingStrs = value.split('|', Qt::SkipEmptyParts);
  for (const QString &bindingStr : bindingStrs) {
    HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
    if (binding.enabled && binding.keyCode != 0) {
      if (!result.contains(binding)) {
        result.append(binding);
      }
    }
  }

  return result;
}

bool HotkeyManager::registerHotkeys() {
  unregisterHotkeys();
  m_hotkeyIdToCharacter.clear();
  m_hotkeyIdToCharacters.clear();
  m_hotkeyIdToCycleGroup.clear();
  m_hotkeyIdIsForward.clear();

  // Clear mouse button hash maps
  m_mouseButtonToCharacter.clear();
  m_mouseButtonToCharacters.clear();
  m_mouseButtonToCycleGroup.clear();

  m_suspendHotkeyIds.clear();
  for (const HotkeyBinding &binding : m_suspendHotkeys) {
    if (!binding.enabled)
      continue;
    int hotkeyId;
    if (registerHotkey(binding, hotkeyId)) {
      m_suspendHotkeyIds.append(hotkeyId);
    }
  }

  if (m_suspended) {
    return true;
  }

  QHash<HotkeyBinding, QVector<QString>> bindingToCharacters;

  for (auto it = m_characterMultiHotkeys.begin();
       it != m_characterMultiHotkeys.end(); ++it) {
    const QString &characterName = it.key();
    const QVector<HotkeyBinding> &bindings = it.value();

    for (const HotkeyBinding &binding : bindings) {
      if (!binding.enabled)
        continue;
      bindingToCharacters[binding].append(characterName);
    }
  }

  for (auto it = m_characterHotkeys.begin(); it != m_characterHotkeys.end();
       ++it) {
    const QString &characterName = it.key();

    if (m_characterMultiHotkeys.contains(characterName)) {
      continue;
    }

    const HotkeyBinding &binding = it.value();
    if (!binding.enabled)
      continue;

    bindingToCharacters[binding].append(characterName);
  }

  for (auto it = bindingToCharacters.begin(); it != bindingToCharacters.end();
       ++it) {
    const HotkeyBinding &binding = it.key();
    const QVector<QString> &characters = it.value();

    int hotkeyId;
    if (registerHotkey(binding, hotkeyId)) {
      if (characters.size() == 1) {
        m_hotkeyIdToCharacter.insert(hotkeyId, characters.first());
      } else {
        m_hotkeyIdToCharacters.insert(hotkeyId, characters);
      }

      // Build mouse button hash maps for O(1) lookup
      if (isMouseButton(binding.keyCode)) {
        if (characters.size() == 1) {
          m_mouseButtonToCharacter.insert(binding, characters.first());
        } else {
          m_mouseButtonToCharacters.insert(binding, characters);
        }
      }
    }
  }

  for (auto it = m_cycleGroups.begin(); it != m_cycleGroups.end(); ++it) {
    const QString &groupName = it.key();
    const CycleGroup &group = it.value();

    if (!group.forwardBindings.isEmpty()) {
      for (const HotkeyBinding &binding : group.forwardBindings) {
        if (!binding.enabled)
          continue;

        int hotkeyId;
        if (registerHotkey(binding, hotkeyId)) {
          m_hotkeyIdToCycleGroup.insert(hotkeyId, groupName);
          m_hotkeyIdIsForward.insert(hotkeyId, true);

          // Build mouse button hash map for O(1) lookup
          if (isMouseButton(binding.keyCode)) {
            m_mouseButtonToCycleGroup.insert(binding,
                                             qMakePair(groupName, true));
          }
        }
      }
    } else if (group.forwardBinding.enabled) {
      int hotkeyId;
      if (registerHotkey(group.forwardBinding, hotkeyId)) {
        m_hotkeyIdToCycleGroup.insert(hotkeyId, groupName);
        m_hotkeyIdIsForward.insert(hotkeyId, true);

        // Build mouse button hash map for O(1) lookup
        if (isMouseButton(group.forwardBinding.keyCode)) {
          m_mouseButtonToCycleGroup.insert(group.forwardBinding,
                                           qMakePair(groupName, true));
        }
      }
    }

    if (!group.backwardBindings.isEmpty()) {
      for (const HotkeyBinding &binding : group.backwardBindings) {
        if (!binding.enabled)
          continue;

        int hotkeyId;
        if (registerHotkey(binding, hotkeyId)) {
          m_hotkeyIdToCycleGroup.insert(hotkeyId, groupName);
          m_hotkeyIdIsForward.insert(hotkeyId, false);

          // Build mouse button hash map for O(1) lookup
          if (isMouseButton(binding.keyCode)) {
            m_mouseButtonToCycleGroup.insert(binding,
                                             qMakePair(groupName, false));
          }
        }
      }
    } else if (group.backwardBinding.enabled) {
      int hotkeyId;
      if (registerHotkey(group.backwardBinding, hotkeyId)) {
        m_hotkeyIdToCycleGroup.insert(hotkeyId, groupName);
        m_hotkeyIdIsForward.insert(hotkeyId, false);

        // Build mouse button hash map for O(1) lookup
        if (isMouseButton(group.backwardBinding.keyCode)) {
          m_mouseButtonToCycleGroup.insert(group.backwardBinding,
                                           qMakePair(groupName, false));
        }
      }
    }
  }

  registerHotkeyList(m_notLoggedInForwardHotkeys,
                     m_notLoggedInForwardHotkeyIds);
  registerHotkeyList(m_notLoggedInBackwardHotkeys,
                     m_notLoggedInBackwardHotkeyIds);

  registerHotkeyList(m_nonEVEForwardHotkeys, m_nonEVEForwardHotkeyIds);
  registerHotkeyList(m_nonEVEBackwardHotkeys, m_nonEVEBackwardHotkeyIds);

  registerHotkeyList(m_closeAllClientsHotkeys, m_closeAllClientsHotkeyIds,
                     false);

  registerHotkeyList(m_minimizeAllClientsHotkeys, m_minimizeAllClientsHotkeyIds,
                     false);

  registerHotkeyList(m_toggleThumbnailsVisibilityHotkeys,
                     m_toggleThumbnailsVisibilityHotkeyIds, false);

  registerProfileHotkeys();

  if (hasMouseButtonHotkeys()) {
    installMouseHook();
  }

  return true;
}

void HotkeyManager::unregisterHotkeys() {
  for (int hotkeyId : m_hotkeyIdToCharacter.keys()) {
    unregisterHotkey(hotkeyId);
  }

  for (int hotkeyId : m_hotkeyIdToCharacters.keys()) {
    unregisterHotkey(hotkeyId);
  }

  for (int hotkeyId : m_hotkeyIdToCycleGroup.keys()) {
    unregisterHotkey(hotkeyId);
  }

  unregisterProfileHotkeys();

  for (int aliasId : m_wildcardAliases.keys()) {
    unregisterHotkey(aliasId);
  }

  for (int hotkeyId : m_suspendHotkeyIds) {
    unregisterHotkey(hotkeyId);
  }
  m_suspendHotkeyIds.clear();

  for (int hotkeyId : m_notLoggedInForwardHotkeyIds) {
    unregisterHotkey(hotkeyId);
  }
  m_notLoggedInForwardHotkeyIds.clear();

  for (int hotkeyId : m_notLoggedInBackwardHotkeyIds) {
    unregisterHotkey(hotkeyId);
  }
  m_notLoggedInBackwardHotkeyIds.clear();

  for (int hotkeyId : m_nonEVEForwardHotkeyIds) {
    unregisterHotkey(hotkeyId);
  }
  m_nonEVEForwardHotkeyIds.clear();

  for (int hotkeyId : m_nonEVEBackwardHotkeyIds) {
    unregisterHotkey(hotkeyId);
  }
  m_nonEVEBackwardHotkeyIds.clear();

  for (int hotkeyId : m_closeAllClientsHotkeyIds) {
    unregisterHotkey(hotkeyId);
  }
  m_closeAllClientsHotkeyIds.clear();

  for (int hotkeyId : m_minimizeAllClientsHotkeyIds) {
    unregisterHotkey(hotkeyId);
  }
  m_minimizeAllClientsHotkeyIds.clear();

  for (int hotkeyId : m_toggleThumbnailsVisibilityHotkeyIds) {
    unregisterHotkey(hotkeyId);
  }
  m_toggleThumbnailsVisibilityHotkeyIds.clear();

  m_hotkeyIdToCharacter.clear();
  m_hotkeyIdToCharacters.clear();
  m_hotkeyIdToCycleGroup.clear();
  m_hotkeyIdIsForward.clear();
  m_wildcardAliases.clear();
}

static bool isForegroundWindowEVEClient() {
  HWND foregroundWindow = GetForegroundWindow();
  if (!foregroundWindow)
    return false;

  DWORD processId = 0;
  GetWindowThreadProcessId(foregroundWindow, &processId);

  HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                FALSE, processId);
  if (!hProcess)
    return false;

  wchar_t processNameBuffer[MAX_PATH];
  QString processName;
  if (GetModuleBaseNameW(hProcess, NULL, processNameBuffer, MAX_PATH)) {
    processName = QString::fromWCharArray(processNameBuffer);
  }
  CloseHandle(hProcess);

  if (processName.isEmpty())
    return false;

  QStringList allowedProcessNames = Config::instance().processNames();
  for (const QString &allowedName : allowedProcessNames) {
    if (processName.compare(allowedName, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }

  return false;
}

#if 0
static bool isMouseOverEVEClientOrThumbnail() {
  POINT pt;
  if (!GetCursorPos(&pt)) {
    return false;
  }

  HWND windowUnderMouse = WindowFromPoint(pt);
  if (!windowUnderMouse) {
    return false;
  }

  wchar_t className[256];
  if (GetClassNameW(windowUnderMouse, className, 256)) {
    QString classNameStr = QString::fromWCharArray(className);
    if (classNameStr.startsWith("Qt") || classNameStr.contains("QWindow") ||
        classNameStr == "EVEAPMPreviewHotkeyWindow") {
      return true;
    }
  }

  DWORD processId = 0;
  GetWindowThreadProcessId(windowUnderMouse, &processId);

  HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                FALSE, processId);
  if (!hProcess) {
    return false;
  }

  wchar_t processNameBuffer[MAX_PATH];
  QString processName;
  if (GetModuleBaseNameW(hProcess, NULL, processNameBuffer, MAX_PATH)) {
    processName = QString::fromWCharArray(processNameBuffer);
  }
  CloseHandle(hProcess);

  if (processName.isEmpty()) {
    return false;
  }

  QStringList allowedProcessNames = Config::instance().processNames();
  for (const QString &allowedName : allowedProcessNames) {
    if (processName.compare(allowedName, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }

  return false;
}
#endif

void HotkeyManager::setSuspended(bool suspended) {
  if (m_suspended == suspended)
    return;

  m_suspended = suspended;
  registerHotkeys();
  emit suspendedChanged(m_suspended);
}

void HotkeyManager::toggleSuspended() { setSuspended(!m_suspended); }

void HotkeyManager::setSuspendHotkeys(const QVector<HotkeyBinding> &bindings) {
  m_suspendHotkeys = bindings;
  registerHotkeys();
}

void HotkeyManager::setCharacterHotkey(const QString &characterName,
                                       const HotkeyBinding &binding) {
  m_characterHotkeys.insert(characterName, binding);
  QVector<HotkeyBinding> bindings;
  bindings.append(binding);
  m_characterMultiHotkeys.insert(characterName, bindings);
  registerHotkeys();
}

void HotkeyManager::setCharacterHotkeys(
    const QString &characterName, const QVector<HotkeyBinding> &bindings) {
  m_characterMultiHotkeys.insert(characterName, bindings);
  if (!bindings.isEmpty()) {
    m_characterHotkeys.insert(characterName, bindings.first());
  } else {
    m_characterHotkeys.remove(characterName);
  }
  registerHotkeys();
}

void HotkeyManager::addCharacterHotkey(const QString &characterName,
                                       const HotkeyBinding &binding) {
  QVector<HotkeyBinding> bindings =
      m_characterMultiHotkeys.value(characterName);

  for (const HotkeyBinding &existing : bindings) {
    if (existing == binding) {
      return;
    }
  }

  bindings.append(binding);
  setCharacterHotkeys(characterName, bindings);
}

void HotkeyManager::removeCharacterHotkey(const QString &characterName) {
  m_characterHotkeys.remove(characterName);
  m_characterMultiHotkeys.remove(characterName);
  registerHotkeys();
}

HotkeyBinding
HotkeyManager::getCharacterHotkey(const QString &characterName) const {
  return m_characterHotkeys.value(characterName, HotkeyBinding());
}

QVector<HotkeyBinding>
HotkeyManager::getCharacterHotkeys(const QString &characterName) const {
  if (m_characterMultiHotkeys.contains(characterName)) {
    return m_characterMultiHotkeys.value(characterName);
  }

  if (m_characterHotkeys.contains(characterName)) {
    QVector<HotkeyBinding> bindings;
    bindings.append(m_characterHotkeys.value(characterName));
    return bindings;
  }

  return QVector<HotkeyBinding>();
}

QHash<QString, QVector<HotkeyBinding>>
HotkeyManager::getAllCharacterMultiHotkeys() const {
  return m_characterMultiHotkeys;
}

QString
HotkeyManager::getCharacterForHotkey(const HotkeyBinding &binding) const {
  for (auto it = m_characterHotkeys.begin(); it != m_characterHotkeys.end();
       ++it) {
    if (it.value() == binding)
      return it.key();
  }
  return QString();
}

void HotkeyManager::createCycleGroup(const QString &groupName,
                                     const QVector<QString> &characterNames,
                                     const HotkeyBinding &forwardKey,
                                     const HotkeyBinding &backwardKey) {
  CycleGroup group;
  group.groupName = groupName;
  group.characterNames = characterNames;
  group.forwardBinding = forwardKey;
  group.backwardBinding = backwardKey;
  group.includeNotLoggedIn = false;

  m_cycleGroups.insert(groupName, group);
  registerHotkeys();
  saveToConfig();
}

void HotkeyManager::createCycleGroup(const CycleGroup &group) {
  m_cycleGroups.insert(group.groupName, group);
  registerHotkeys();
}

void HotkeyManager::removeCycleGroup(const QString &groupName) {
  m_cycleGroups.remove(groupName);
  registerHotkeys();
}

CycleGroup HotkeyManager::getCycleGroup(const QString &groupName) const {
  return m_cycleGroups.value(groupName, CycleGroup());
}

void HotkeyManager::setNotLoggedInCycleHotkeys(
    const QVector<HotkeyBinding> &forwardKeys,
    const QVector<HotkeyBinding> &backwardKeys) {
  m_notLoggedInForwardHotkeys = forwardKeys;
  m_notLoggedInBackwardHotkeys = backwardKeys;
  registerHotkeys();
}

void HotkeyManager::setNonEVECycleHotkeys(
    const QVector<HotkeyBinding> &forwardKeys,
    const QVector<HotkeyBinding> &backwardKeys) {
  m_nonEVEForwardHotkeys = forwardKeys;
  m_nonEVEBackwardHotkeys = backwardKeys;
  registerHotkeys();
}

void HotkeyManager::setCloseAllClientsHotkeys(
    const QVector<HotkeyBinding> &bindings) {
  m_closeAllClientsHotkeys = bindings;
  registerHotkeys();
}

void HotkeyManager::setMinimizeAllClientsHotkeys(
    const QVector<HotkeyBinding> &bindings) {
  m_minimizeAllClientsHotkeys = bindings;
  registerHotkeys();
}

void HotkeyManager::setToggleThumbnailsVisibilityHotkeys(
    const QVector<HotkeyBinding> &bindings) {
  m_toggleThumbnailsVisibilityHotkeys = bindings;
  registerHotkeys();
}

void HotkeyManager::setProfileHotkeys(const QString &profileName,
                                      const QVector<HotkeyBinding> &bindings) {
  if (bindings.isEmpty()) {
    m_profileHotkeys.remove(profileName);
  } else {
    m_profileHotkeys[profileName] = bindings;
  }
  registerHotkeys();
}

QVector<HotkeyBinding>
HotkeyManager::getProfileHotkeys(const QString &profileName) const {
  return m_profileHotkeys.value(profileName);
}

void HotkeyManager::updateCharacterWindows(
    const QHash<QString, HWND> &characterWindows) {
  m_characterWindows = characterWindows;
}

HWND HotkeyManager::getWindowForCharacter(const QString &characterName) const {
  return m_characterWindows.value(characterName, nullptr);
}

QString HotkeyManager::getCharacterForWindow(HWND hwnd) const {
  for (auto it = m_characterWindows.constBegin();
       it != m_characterWindows.constEnd(); ++it) {
    if (it.value() == hwnd) {
      return it.key();
    }
  }
  return QString();
}

int HotkeyManager::generateHotkeyId() { return m_nextHotkeyId++; }

void HotkeyManager::loadFromConfig() {
  QSettings settings(Config::instance().configFilePath(), QSettings::IniFormat);

  m_suspendHotkeys.clear();

  settings.beginGroup("hotkeys");
  QVariant suspendVar = settings.value("suspendHotkey");

  QString suspendStr;
  if (suspendVar.canConvert<QStringList>()) {
    suspendStr = suspendVar.toStringList().join('|');
  } else {
    suspendStr = suspendVar.toString();
  }

  if (!suspendStr.isEmpty()) {
    QStringList suspendBindingStrs = suspendStr.split('|', Qt::SkipEmptyParts);
    for (const QString &bindingStr : suspendBindingStrs) {
      HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
      if (binding.enabled && binding.keyCode != 0) {
        if (!m_suspendHotkeys.contains(binding)) {
          m_suspendHotkeys.append(binding);
        }
      }
    }
  }
  settings.endGroup();

  m_characterHotkeys.clear();
  m_characterMultiHotkeys.clear();
  settings.beginGroup("characterHotkeys");
  QStringList characterKeys = settings.childKeys();
  for (const QString &characterName : characterKeys) {
    QVariant value = settings.value(characterName);

    QString valueStr;
    if (value.canConvert<QStringList>()) {
      valueStr = value.toStringList().join('|');
    } else {
      valueStr = value.toString();
    }

    QVector<HotkeyBinding> bindings;
    QStringList bindingStrs = valueStr.split('|', Qt::SkipEmptyParts);

    for (const QString &bindingStr : bindingStrs) {
      HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
      if (binding.enabled) {
        if (!bindings.contains(binding)) {
          bindings.append(binding);
        }
      }
    }

    if (!bindings.isEmpty()) {
      m_characterMultiHotkeys.insert(characterName, bindings);
      m_characterHotkeys.insert(characterName, bindings.first());
    }
  }
  settings.endGroup();

  m_cycleGroups.clear();

  settings.beginGroup("cycleGroups");
  QStringList groupKeys = settings.childKeys();
  for (const QString &groupName : groupKeys) {
    QVariant groupValue = settings.value(groupName);

    QString groupStr;
    if (groupValue.canConvert<QStringList>()) {
      QStringList parts = groupValue.toStringList();
      groupStr = parts.join(',');
    } else {
      groupStr = groupValue.toString();
    }

    QStringList parts = groupStr.split('|');
    if (parts.size() >= 3) {
      CycleGroup group;
      group.groupName = groupName;
      QStringList charNames = parts[0].split(',', Qt::SkipEmptyParts);
      for (const QString &name : charNames)
        group.characterNames.append(name);

      QStringList forwardBindingStrs = parts[1].split(';', Qt::SkipEmptyParts);
      for (const QString &bindingStr : forwardBindingStrs) {
        HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
        if (binding.enabled && binding.keyCode != 0) {
          if (!group.forwardBindings.contains(binding)) {
            group.forwardBindings.append(binding);
          }
        }
      }
      group.forwardBinding = !group.forwardBindings.isEmpty()
                                 ? group.forwardBindings.first()
                                 : HotkeyBinding();

      QStringList backwardBindingStrs = parts[2].split(';', Qt::SkipEmptyParts);
      for (const QString &bindingStr : backwardBindingStrs) {
        HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
        if (binding.enabled && binding.keyCode != 0) {
          if (!group.backwardBindings.contains(binding)) {
            group.backwardBindings.append(binding);
          }
        }
      }
      group.backwardBinding = !group.backwardBindings.isEmpty()
                                  ? group.backwardBindings.first()
                                  : HotkeyBinding();

      group.includeNotLoggedIn =
          (parts.size() >= 4) ? (parts[3].toInt() != 0) : false;
      group.noLoop = (parts.size() >= 5) ? (parts[4].toInt() != 0) : false;

      m_cycleGroups.insert(groupName, group);
    }
  }
  settings.endGroup();

  m_notLoggedInForwardHotkeys.clear();
  m_notLoggedInBackwardHotkeys.clear();
  settings.beginGroup("notLoggedInHotkeys");
  m_notLoggedInForwardHotkeys = loadHotkeyList(settings, "forward");
  m_notLoggedInBackwardHotkeys = loadHotkeyList(settings, "backward");
  settings.endGroup();

  m_nonEVEForwardHotkeys.clear();
  m_nonEVEBackwardHotkeys.clear();
  settings.beginGroup("nonEVEHotkeys");
  m_nonEVEForwardHotkeys = loadHotkeyList(settings, "forward");
  m_nonEVEBackwardHotkeys = loadHotkeyList(settings, "backward");
  settings.endGroup();

  m_closeAllClientsHotkeys.clear();
  settings.beginGroup("closeAllHotkeys");
  m_closeAllClientsHotkeys = loadHotkeyList(settings, "closeAllClients");
  settings.endGroup();

  m_minimizeAllClientsHotkeys.clear();
  settings.beginGroup("minimizeAllHotkeys");
  m_minimizeAllClientsHotkeys = loadHotkeyList(settings, "minimizeAllClients");
  settings.endGroup();

  m_toggleThumbnailsVisibilityHotkeys.clear();
  settings.beginGroup("toggleThumbnailsVisibilityHotkeys");
  m_toggleThumbnailsVisibilityHotkeys =
      loadHotkeyList(settings, "toggleThumbnailsVisibility");
  settings.endGroup();

  m_profileHotkeys.clear();
  QStringList profiles = Config::instance().listProfiles();
  for (const QString &profileName : profiles) {
    QVector<HotkeyBinding> bindings =
        Config::instance().getProfileHotkeys(profileName);
    if (!bindings.isEmpty()) {
      m_profileHotkeys.insert(profileName, bindings);
    }
  }

  registerHotkeys();
}

void HotkeyManager::saveToConfig() {
  QSettings settings(Config::instance().configFilePath(), QSettings::IniFormat);

  settings.beginGroup("hotkeys");
  settings.remove("suspendHotkey");
  QStringList suspendBindingStrs;
  for (const HotkeyBinding &binding : m_suspendHotkeys) {
    suspendBindingStrs.append(binding.toString());
  }
  settings.setValue("suspendHotkey", suspendBindingStrs.join('|'));
  settings.endGroup();

  settings.beginGroup("characterHotkeys");
  settings.remove("");

  for (auto it = m_characterMultiHotkeys.begin();
       it != m_characterMultiHotkeys.end(); ++it) {
    QStringList bindingStrs;
    for (const HotkeyBinding &binding : it.value()) {
      bindingStrs.append(binding.toString());
    }
    settings.setValue(it.key(), bindingStrs.join('|'));
  }

  for (auto it = m_characterHotkeys.begin(); it != m_characterHotkeys.end();
       ++it) {
    if (!m_characterMultiHotkeys.contains(it.key())) {
      settings.setValue(it.key(), it.value().toString());
    }
  }
  settings.endGroup();

  settings.beginGroup("cycleGroups");
  settings.remove("");
  for (auto it = m_cycleGroups.begin(); it != m_cycleGroups.end(); ++it) {
    const CycleGroup &group = it.value();
    QStringList charNames;
    for (const QString &name : group.characterNames)
      charNames.append(name);

    QStringList forwardBindingStrs;
    QStringList backwardBindingStrs;

    if (!group.forwardBindings.isEmpty()) {
      for (const HotkeyBinding &binding : group.forwardBindings) {
        forwardBindingStrs.append(binding.toString());
      }
    } else {
      forwardBindingStrs.append(group.forwardBinding.toString());
    }

    if (!group.backwardBindings.isEmpty()) {
      for (const HotkeyBinding &binding : group.backwardBindings) {
        backwardBindingStrs.append(binding.toString());
      }
    } else {
      backwardBindingStrs.append(group.backwardBinding.toString());
    }

    QString groupStr = charNames.join(',') + "|" +
                       forwardBindingStrs.join(';') + "|" +
                       backwardBindingStrs.join(';') + "|" +
                       QString::number(group.includeNotLoggedIn ? 1 : 0) + "|" +
                       QString::number(group.noLoop ? 1 : 0);
    settings.setValue(it.key(), groupStr);
  }
  settings.endGroup();

  settings.beginGroup("notLoggedInHotkeys");
  saveHotkeyList(settings, "forward", m_notLoggedInForwardHotkeys);
  saveHotkeyList(settings, "backward", m_notLoggedInBackwardHotkeys);
  settings.endGroup();

  settings.beginGroup("nonEVEHotkeys");
  saveHotkeyList(settings, "forward", m_nonEVEForwardHotkeys);
  saveHotkeyList(settings, "backward", m_nonEVEBackwardHotkeys);
  settings.endGroup();

  settings.beginGroup("closeAllHotkeys");
  saveHotkeyList(settings, "closeAllClients", m_closeAllClientsHotkeys);
  settings.endGroup();

  settings.beginGroup("minimizeAllHotkeys");
  saveHotkeyList(settings, "minimizeAllClients", m_minimizeAllClientsHotkeys);
  settings.endGroup();

  settings.beginGroup("toggleThumbnailsVisibilityHotkeys");
  saveHotkeyList(settings, "toggleThumbnailsVisibility",
                 m_toggleThumbnailsVisibilityHotkeys);
  settings.endGroup();

  settings.sync();
}

bool HotkeyBinding::operator<(const HotkeyBinding &other) const {
  if (keyCode != other.keyCode)
    return keyCode < other.keyCode;
  if (ctrl != other.ctrl)
    return ctrl < other.ctrl;
  if (alt != other.alt)
    return alt < other.alt;
  if (shift != other.shift)
    return shift < other.shift;
  return enabled < other.enabled;
}

bool HotkeyBinding::operator==(const HotkeyBinding &other) const {
  return enabled == other.enabled && keyCode == other.keyCode &&
         ctrl == other.ctrl && alt == other.alt && shift == other.shift;
}

void HotkeyManager::createMessageWindow() {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = MessageWindowProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"EVEAPMPreviewHotkeyWindow";

  RegisterClassExW(&wc);

  m_messageWindow = CreateWindowExW(
      0, L"EVEAPMPreviewHotkeyWindow", L"EVE APM Preview Hotkey Window", 0, 0,
      0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr);

  if (m_messageWindow) {
    SetWindowLongPtrW(m_messageWindow, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
  }
}

void HotkeyManager::destroyMessageWindow() {
  if (m_messageWindow) {
    DestroyWindow(m_messageWindow);
    m_messageWindow = nullptr;
  }
}

LRESULT CALLBACK HotkeyManager::MessageWindowProc(HWND hwnd, UINT msg,
                                                  WPARAM wParam,
                                                  LPARAM lParam) {
  HotkeyManager *manager =
      reinterpret_cast<HotkeyManager *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_HOTKEY) {
    if (manager && !s_instance.isNull()) {
      int hotkeyId = static_cast<int>(wParam);

      if (manager->m_wildcardAliases.contains(hotkeyId)) {
        hotkeyId = manager->m_wildcardAliases.value(hotkeyId);
      }

      if (manager->m_suspendHotkeyIds.contains(hotkeyId)) {
        manager->toggleSuspended();
        return 0;
      }

      if (manager->m_suspended) {
        return 0;
      }

      bool onlyWhenEVEFocused = Config::instance().hotkeysOnlyWhenEVEFocused();
      if (onlyWhenEVEFocused && !isForegroundWindowEVEClient()) {
        return 0;
      }

      if (manager->m_hotkeyIdToCharacter.contains(hotkeyId)) {
        QString characterName = manager->m_hotkeyIdToCharacter.value(hotkeyId);
        emit manager->characterHotkeyPressed(characterName);
        return 0;
      }

      if (manager->m_hotkeyIdToCharacters.contains(hotkeyId)) {
        QVector<QString> characterNames =
            manager->m_hotkeyIdToCharacters.value(hotkeyId);
        emit manager->characterHotkeyCyclePressed(characterNames);
        return 0;
      }

      if (manager->m_hotkeyIdToCycleGroup.contains(hotkeyId)) {
        QString groupName = manager->m_hotkeyIdToCycleGroup.value(hotkeyId);
        bool isForward = manager->m_hotkeyIdIsForward.value(hotkeyId, true);

        if (isForward) {
          emit manager->namedCycleForwardPressed(groupName);
        } else {
          emit manager->namedCycleBackwardPressed(groupName);
        }
        return 0;
      }

      if (manager->m_notLoggedInForwardHotkeyIds.contains(hotkeyId)) {
        emit manager->notLoggedInCycleForwardPressed();
        return 0;
      }

      if (manager->m_notLoggedInBackwardHotkeyIds.contains(hotkeyId)) {
        emit manager->notLoggedInCycleBackwardPressed();
        return 0;
      }

      if (manager->m_nonEVEForwardHotkeyIds.contains(hotkeyId)) {
        emit manager->nonEVECycleForwardPressed();
        return 0;
      }

      if (manager->m_nonEVEBackwardHotkeyIds.contains(hotkeyId)) {
        emit manager->nonEVECycleBackwardPressed();
        return 0;
      }

      if (manager->m_closeAllClientsHotkeyIds.contains(hotkeyId)) {
        emit manager->closeAllClientsRequested();
        return 0;
      }

      if (manager->m_minimizeAllClientsHotkeyIds.contains(hotkeyId)) {
        emit manager->minimizeAllClientsRequested();
        return 0;
      }

      if (manager->m_toggleThumbnailsVisibilityHotkeyIds.contains(hotkeyId)) {
        emit manager->toggleThumbnailsVisibilityRequested();
        return 0;
      }

      if (manager->m_hotkeyIdToProfile.contains(hotkeyId)) {
        QString profileName = manager->m_hotkeyIdToProfile.value(hotkeyId);
        emit manager->profileSwitchRequested(profileName);
        return 0;
      }
    }
    return 0;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

QString HotkeyBinding::toString() const {
  return QString("%1,%2,%3,%4,%5")
      .arg(enabled ? 1 : 0)
      .arg(keyCode)
      .arg(ctrl ? 1 : 0)
      .arg(alt ? 1 : 0)
      .arg(shift ? 1 : 0);
}

HotkeyBinding HotkeyBinding::fromString(const QString &str) {
  QStringList parts = str.split(',');
  if (parts.size() == 5) {
    HotkeyBinding binding;
    binding.enabled = parts[0].toInt() != 0;
    binding.keyCode = parts[1].toInt();
    binding.ctrl = parts[2].toInt() != 0;
    binding.alt = parts[3].toInt() != 0;
    binding.shift = parts[4].toInt() != 0;
    return binding;
  }
  return HotkeyBinding();
}

void HotkeyManager::registerProfileHotkeys() {
  for (auto it = m_profileHotkeys.begin(); it != m_profileHotkeys.end(); ++it) {
    const QString &profileName = it.key();
    const QVector<HotkeyBinding> &bindings = it.value();

    for (const HotkeyBinding &binding : bindings) {
      if (!binding.enabled)
        continue;

      int hotkeyId;
      if (registerHotkey(binding, hotkeyId)) {
        m_hotkeyIdToProfile.insert(hotkeyId, profileName);
      }
    }
  }
}

void HotkeyManager::unregisterProfileHotkeys() {
  for (int hotkeyId : m_hotkeyIdToProfile.keys()) {
    unregisterHotkey(hotkeyId);
  }
  m_hotkeyIdToProfile.clear();
}

bool HotkeyManager::isMouseButton(int keyCode) const {
  return keyCode == VK_MBUTTON || keyCode == VK_XBUTTON1 ||
         keyCode == VK_XBUTTON2;
}

bool HotkeyManager::hasMouseButtonHotkeys() const {
  for (const HotkeyBinding &binding : m_characterHotkeys.values()) {
    if (binding.enabled && isMouseButton(binding.keyCode)) {
      return true;
    }
  }

  for (const QVector<HotkeyBinding> &bindings :
       m_characterMultiHotkeys.values()) {
    for (const HotkeyBinding &binding : bindings) {
      if (binding.enabled && isMouseButton(binding.keyCode)) {
        return true;
      }
    }
  }

  for (const CycleGroup &group : m_cycleGroups.values()) {
    if (group.forwardBinding.enabled &&
        isMouseButton(group.forwardBinding.keyCode)) {
      return true;
    }
    if (group.backwardBinding.enabled &&
        isMouseButton(group.backwardBinding.keyCode)) {
      return true;
    }
    for (const HotkeyBinding &binding : group.forwardBindings) {
      if (binding.enabled && isMouseButton(binding.keyCode)) {
        return true;
      }
    }
    for (const HotkeyBinding &binding : group.backwardBindings) {
      if (binding.enabled && isMouseButton(binding.keyCode)) {
        return true;
      }
    }
  }

  for (const HotkeyBinding &binding : m_notLoggedInForwardHotkeys) {
    if (binding.enabled && isMouseButton(binding.keyCode)) {
      return true;
    }
  }
  for (const HotkeyBinding &binding : m_notLoggedInBackwardHotkeys) {
    if (binding.enabled && isMouseButton(binding.keyCode)) {
      return true;
    }
  }

  for (const HotkeyBinding &binding : m_nonEVEForwardHotkeys) {
    if (binding.enabled && isMouseButton(binding.keyCode)) {
      return true;
    }
  }
  for (const HotkeyBinding &binding : m_nonEVEBackwardHotkeys) {
    if (binding.enabled && isMouseButton(binding.keyCode)) {
      return true;
    }
  }

  for (const HotkeyBinding &binding : m_closeAllClientsHotkeys) {
    if (binding.enabled && isMouseButton(binding.keyCode)) {
      return true;
    }
  }

  for (const HotkeyBinding &binding : m_suspendHotkeys) {
    if (binding.enabled && isMouseButton(binding.keyCode)) {
      return true;
    }
  }

  for (const QVector<HotkeyBinding> &bindings : m_profileHotkeys.values()) {
    for (const HotkeyBinding &binding : bindings) {
      if (binding.enabled && isMouseButton(binding.keyCode)) {
        return true;
      }
    }
  }

  return false;
}

void HotkeyManager::installMouseHook() {
  if (m_uiohookManager && !m_uiohookManager->isRunning()) {
    m_uiohookManager->start();
  }
}

void HotkeyManager::uninstallMouseHook() {
  if (m_uiohookManager && m_uiohookManager->isRunning()) {
    m_uiohookManager->stop();
  }
}

void HotkeyManager::checkMouseButtonBindings(int vkCode, bool ctrl, bool alt,
                                             bool shift) {
  HotkeyBinding pressedBinding(vkCode, ctrl, alt, shift, true);

  // Check suspend hotkeys first
  if (!m_suspendHotkeys.isEmpty()) {
    for (const HotkeyBinding &binding : m_suspendHotkeys) {
      if (binding.enabled && binding.keyCode == vkCode &&
          binding.ctrl == ctrl && binding.alt == alt &&
          binding.shift == shift) {
        toggleSuspended();
        return;
      }
    }
  }

  if (m_suspended) {
    return;
  }

  // Check EVE focus requirement once
  bool onlyWhenEVEFocused = Config::instance().hotkeysOnlyWhenEVEFocused();
  if (onlyWhenEVEFocused && !isForegroundWindowEVEClient()) {
    return;
  }

  // O(1) hash lookup for character hotkeys
  if (m_mouseButtonToCharacter.contains(pressedBinding)) {
    QString characterName = m_mouseButtonToCharacter.value(pressedBinding);
    emit characterHotkeyPressed(characterName);
    return;
  }

  if (m_mouseButtonToCharacters.contains(pressedBinding)) {
    QVector<QString> characterNames =
        m_mouseButtonToCharacters.value(pressedBinding);
    emit characterHotkeyCyclePressed(characterNames);
    return;
  }

  // O(1) hash lookup for cycle group hotkeys
  if (m_mouseButtonToCycleGroup.contains(pressedBinding)) {
    QPair<QString, bool> cycleInfo =
        m_mouseButtonToCycleGroup.value(pressedBinding);
    const QString &groupName = cycleInfo.first;
    bool isForward = cycleInfo.second;

    if (isForward) {
      emit namedCycleForwardPressed(groupName);
    } else {
      emit namedCycleBackwardPressed(groupName);
    }
    return;
  }

  // Check other hotkey lists (these typically have few entries)
  for (const HotkeyBinding &binding : m_notLoggedInForwardHotkeys) {
    if (binding.enabled && binding.keyCode == vkCode && binding.ctrl == ctrl &&
        binding.alt == alt && binding.shift == shift) {
      emit notLoggedInCycleForwardPressed();
      return;
    }
  }

  for (const HotkeyBinding &binding : m_notLoggedInBackwardHotkeys) {
    if (binding.enabled && binding.keyCode == vkCode && binding.ctrl == ctrl &&
        binding.alt == alt && binding.shift == shift) {
      emit notLoggedInCycleBackwardPressed();
      return;
    }
  }

  for (const HotkeyBinding &binding : m_nonEVEForwardHotkeys) {
    if (binding.enabled && binding.keyCode == vkCode && binding.ctrl == ctrl &&
        binding.alt == alt && binding.shift == shift) {
      emit nonEVECycleForwardPressed();
      return;
    }
  }

  for (const HotkeyBinding &binding : m_nonEVEBackwardHotkeys) {
    if (binding.enabled && binding.keyCode == vkCode && binding.ctrl == ctrl &&
        binding.alt == alt && binding.shift == shift) {
      emit nonEVECycleBackwardPressed();
      return;
    }
  }

  for (const HotkeyBinding &binding : m_closeAllClientsHotkeys) {
    if (binding.enabled && binding.keyCode == vkCode && binding.ctrl == ctrl &&
        binding.alt == alt && binding.shift == shift) {
      emit closeAllClientsRequested();
      return;
    }
  }
}
