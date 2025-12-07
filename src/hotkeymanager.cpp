#include "hotkeymanager.h"
#include "config.h"
#include "windowcapture.h"
#include <Psapi.h>
#include <QSettings>
#include <QStringList>

QPointer<HotkeyManager> HotkeyManager::s_instance;

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent), m_nextHotkeyId(1000), m_suspendHotkeyId(-1),
      m_suspended(false), m_notLoggedInForwardHotkeyId(-1),
      m_notLoggedInBackwardHotkeyId(-1), m_nonEVEForwardHotkeyId(-1),
      m_nonEVEBackwardHotkeyId(-1), m_closeAllClientsHotkeyId(-1) {
  s_instance = this;
  loadFromConfig();
}

HotkeyManager::~HotkeyManager() {
  unregisterHotkeys();
  s_instance.clear();
}

bool HotkeyManager::registerHotkey(const HotkeyBinding &binding,
                                   int &outHotkeyId) {
  if (!binding.enabled)
    return false;

  UINT modifiers = 0;
  if (binding.ctrl)
    modifiers |= MOD_CONTROL;
  if (binding.alt)
    modifiers |= MOD_ALT;
  if (binding.shift)
    modifiers |= MOD_SHIFT;

  int hotkeyId = m_nextHotkeyId++;

  const Config &cfg = Config::instance();
  bool wildcardMode = cfg.wildcardHotkeys();

  if (RegisterHotKey(nullptr, hotkeyId, modifiers, binding.keyCode)) {
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
        if (RegisterHotKey(nullptr, extraHotkeyId, extraMod, binding.keyCode)) {
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
  UnregisterHotKey(nullptr, hotkeyId);
}

void HotkeyManager::registerHotkeyList(
    const QVector<HotkeyBinding> &multiHotkeys,
    const HotkeyBinding &legacyHotkey, int &legacyHotkeyId) {
  if (!multiHotkeys.isEmpty()) {
    for (const HotkeyBinding &binding : multiHotkeys) {
      if (!binding.enabled)
        continue;
      int hotkeyId;
      registerHotkey(binding, hotkeyId);
    }
  } else if (legacyHotkey.enabled) {
    registerHotkey(legacyHotkey, legacyHotkeyId);
  }
}

void HotkeyManager::unregisterAndReset(int &hotkeyId) {
  if (hotkeyId != -1) {
    unregisterHotkey(hotkeyId);
    hotkeyId = -1;
  }
}

void HotkeyManager::saveHotkeyList(QSettings &settings, const QString &key,
                                   const QVector<HotkeyBinding> &multiHotkeys,
                                   const HotkeyBinding &legacyHotkey) {
  settings.remove(key);
  if (!multiHotkeys.isEmpty()) {
    QStringList bindingStrs;
    for (const HotkeyBinding &binding : multiHotkeys) {
      bindingStrs.append(binding.toString());
    }
    settings.setValue(key, bindingStrs.join('|'));
  } else {
    settings.setValue(key, legacyHotkey.toString());
  }
}

QVector<HotkeyBinding>
HotkeyManager::loadHotkeyList(QSettings &settings, const QString &key,
                              HotkeyBinding &outLegacyHotkey) {
  QString value = settings.value(key, QString()).toString();
  QVector<HotkeyBinding> result;

  if (value.contains('|')) {
    QStringList bindingStrs = value.split('|', Qt::SkipEmptyParts);
    for (const QString &bindingStr : bindingStrs) {
      HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
      if (binding.enabled && binding.keyCode != 0) {
        if (!result.contains(binding)) {
          result.append(binding);
        }
      }
    }
    if (!result.isEmpty()) {
      outLegacyHotkey = result.first();
    }
  } else if (!value.isEmpty()) {
    outLegacyHotkey = HotkeyBinding::fromString(value);
  }

  return result;
}

bool HotkeyManager::registerHotkeys() {
  unregisterHotkeys();
  m_hotkeyIdToCharacter.clear();
  m_hotkeyIdToCharacters.clear();
  m_hotkeyIdToCycleGroup.clear();
  m_hotkeyIdIsForward.clear();

  if (!m_suspendHotkeys.isEmpty()) {
    for (const HotkeyBinding &binding : m_suspendHotkeys) {
      if (!binding.enabled)
        continue;
      int hotkeyId;
      registerHotkey(binding, hotkeyId);
    }
  } else if (m_suspendHotkey.enabled) {
    registerHotkey(m_suspendHotkey, m_suspendHotkeyId);
  }

  if (m_suspended) {
    return true;
  }

  // Group characters by their hotkey bindings
  QHash<HotkeyBinding, QVector<QString>> bindingToCharacters;

  // Process multi-hotkeys first
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

  // Process legacy single hotkeys (skip if character already has multi-hotkeys)
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

  // Register each unique binding once, mapping to all characters that use it
  for (auto it = bindingToCharacters.begin(); it != bindingToCharacters.end();
       ++it) {
    const HotkeyBinding &binding = it.key();
    const QVector<QString> &characters = it.value();

    int hotkeyId;
    if (registerHotkey(binding, hotkeyId)) {
      if (characters.size() == 1) {
        // Single character - use legacy mapping for compatibility
        m_hotkeyIdToCharacter.insert(hotkeyId, characters.first());
      } else {
        // Multiple characters - use new multi-character mapping
        m_hotkeyIdToCharacters.insert(hotkeyId, characters);
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
        }
      }
    } else if (group.forwardBinding.enabled) {
      int hotkeyId;
      if (registerHotkey(group.forwardBinding, hotkeyId)) {
        m_hotkeyIdToCycleGroup.insert(hotkeyId, groupName);
        m_hotkeyIdIsForward.insert(hotkeyId, true);
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
        }
      }
    } else if (group.backwardBinding.enabled) {
      int hotkeyId;
      if (registerHotkey(group.backwardBinding, hotkeyId)) {
        m_hotkeyIdToCycleGroup.insert(hotkeyId, groupName);
        m_hotkeyIdIsForward.insert(hotkeyId, false);
      }
    }
  }

  registerHotkeyList(m_notLoggedInForwardHotkeys, m_notLoggedInForwardHotkey,
                     m_notLoggedInForwardHotkeyId);
  registerHotkeyList(m_notLoggedInBackwardHotkeys, m_notLoggedInBackwardHotkey,
                     m_notLoggedInBackwardHotkeyId);

  registerHotkeyList(m_nonEVEForwardHotkeys, m_nonEVEForwardHotkey,
                     m_nonEVEForwardHotkeyId);
  registerHotkeyList(m_nonEVEBackwardHotkeys, m_nonEVEBackwardHotkey,
                     m_nonEVEBackwardHotkeyId);

  registerHotkeyList(m_closeAllClientsHotkeys, m_closeAllClientsHotkey,
                     m_closeAllClientsHotkeyId);

  registerProfileHotkeys();

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

  unregisterAndReset(m_suspendHotkeyId);
  unregisterAndReset(m_notLoggedInForwardHotkeyId);
  unregisterAndReset(m_notLoggedInBackwardHotkeyId);
  unregisterAndReset(m_nonEVEForwardHotkeyId);
  unregisterAndReset(m_nonEVEBackwardHotkeyId);
  unregisterAndReset(m_closeAllClientsHotkeyId);

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

bool HotkeyManager::nativeEventFilter(void *message, long *result) {
  if (s_instance.isNull())
    return false;

  MSG *msg = static_cast<MSG *>(message);

  if (msg->message == WM_HOTKEY) {
    int hotkeyId = static_cast<int>(msg->wParam);

    if (s_instance->m_wildcardAliases.contains(hotkeyId)) {
      hotkeyId = s_instance->m_wildcardAliases.value(hotkeyId);
    }

    if (hotkeyId == s_instance->m_suspendHotkeyId) {
      s_instance->toggleSuspended();
      return true;
    }

    if (s_instance->m_suspended) {
      return true;
    }

    bool onlyWhenEVEFocused = Config::instance().hotkeysOnlyWhenEVEFocused();
    if (onlyWhenEVEFocused && !isForegroundWindowEVEClient()) {
      return false;
    }

    // Handle single-character hotkeys (legacy behavior)
    if (s_instance->m_hotkeyIdToCharacter.contains(hotkeyId)) {
      QString characterName = s_instance->m_hotkeyIdToCharacter.value(hotkeyId);
      emit s_instance->characterHotkeyPressed(characterName);
      return false;
    }

    // Handle multi-character hotkeys (cycling behavior)
    if (s_instance->m_hotkeyIdToCharacters.contains(hotkeyId)) {
      const QVector<QString> &characterNames =
          s_instance->m_hotkeyIdToCharacters.value(hotkeyId);

      // Emit signal with all character names - MainWindow will handle cycling
      emit s_instance->characterHotkeyCyclePressed(characterNames);
      return false;
    }

    if (s_instance->m_hotkeyIdToCycleGroup.contains(hotkeyId)) {
      QString groupName = s_instance->m_hotkeyIdToCycleGroup.value(hotkeyId);
      bool isForward = s_instance->m_hotkeyIdIsForward.value(hotkeyId, true);

      if (isForward)
        emit s_instance->namedCycleForwardPressed(groupName);
      else
        emit s_instance->namedCycleBackwardPressed(groupName);

      return false;
    }

    if (hotkeyId == s_instance->m_notLoggedInForwardHotkeyId) {
      emit s_instance->notLoggedInCycleForwardPressed();
      return false;
    }

    if (hotkeyId == s_instance->m_notLoggedInBackwardHotkeyId) {
      emit s_instance->notLoggedInCycleBackwardPressed();
      return false;
    }

    if (hotkeyId == s_instance->m_nonEVEForwardHotkeyId) {
      emit s_instance->nonEVECycleForwardPressed();
      return false;
    }

    if (hotkeyId == s_instance->m_nonEVEBackwardHotkeyId) {
      emit s_instance->nonEVECycleBackwardPressed();
      return false;
    }

    if (hotkeyId == s_instance->m_closeAllClientsHotkeyId) {
      emit s_instance->closeAllClientsRequested();
      return false;
    }

    if (s_instance->m_hotkeyIdToProfile.contains(hotkeyId)) {
      QString profileName = s_instance->m_hotkeyIdToProfile.value(hotkeyId);
      emit s_instance->profileSwitchRequested(profileName);
      return false;
    }
  }

  return false;
}

void HotkeyManager::setSuspended(bool suspended) {
  if (m_suspended == suspended)
    return;

  m_suspended = suspended;
  registerHotkeys();
  emit suspendedChanged(m_suspended);
}

void HotkeyManager::toggleSuspended() { setSuspended(!m_suspended); }

void HotkeyManager::setSuspendHotkey(const HotkeyBinding &binding) {
  m_suspendHotkey = binding;
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
    const HotkeyBinding &forwardKey, const HotkeyBinding &backwardKey) {
  m_notLoggedInForwardHotkey = forwardKey;
  m_notLoggedInBackwardHotkey = backwardKey;
  registerHotkeys();
}

void HotkeyManager::setNonEVECycleHotkeys(const HotkeyBinding &forwardKey,
                                          const HotkeyBinding &backwardKey) {
  m_nonEVEForwardHotkey = forwardKey;
  m_nonEVEBackwardHotkey = backwardKey;
  registerHotkeys();
}

void HotkeyManager::setCloseAllClientsHotkey(const HotkeyBinding &binding) {
  m_closeAllClientsHotkey = binding;
  registerHotkeys();
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
    m_suspendHotkey = !m_suspendHotkeys.isEmpty()
                          ? m_suspendHotkeys.first()
                          : HotkeyBinding(VK_F12, true, true, true, true);
  } else {
    m_suspendHotkey = HotkeyBinding(VK_F12, true, true, true, true);
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
  m_notLoggedInForwardHotkeys =
      loadHotkeyList(settings, "forward", m_notLoggedInForwardHotkey);
  m_notLoggedInBackwardHotkeys =
      loadHotkeyList(settings, "backward", m_notLoggedInBackwardHotkey);
  settings.endGroup();

  m_nonEVEForwardHotkeys.clear();
  m_nonEVEBackwardHotkeys.clear();
  settings.beginGroup("nonEVEHotkeys");
  m_nonEVEForwardHotkeys =
      loadHotkeyList(settings, "forward", m_nonEVEForwardHotkey);
  m_nonEVEBackwardHotkeys =
      loadHotkeyList(settings, "backward", m_nonEVEBackwardHotkey);
  settings.endGroup();

  m_closeAllClientsHotkeys.clear();
  settings.beginGroup("closeAllHotkeys");
  m_closeAllClientsHotkeys =
      loadHotkeyList(settings, "closeAllClients", m_closeAllClientsHotkey);
  settings.endGroup();

  registerHotkeys();
}

void HotkeyManager::saveToConfig() {
  QSettings settings(Config::instance().configFilePath(), QSettings::IniFormat);

  settings.beginGroup("hotkeys");
  settings.remove("suspendHotkey");
  if (!m_suspendHotkeys.isEmpty()) {
    QStringList suspendBindingStrs;
    for (const HotkeyBinding &binding : m_suspendHotkeys) {
      suspendBindingStrs.append(binding.toString());
    }
    settings.setValue("suspendHotkey", suspendBindingStrs.join('|'));
  } else {
    settings.setValue("suspendHotkey", m_suspendHotkey.toString());
  }
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
  saveHotkeyList(settings, "forward", m_notLoggedInForwardHotkeys,
                 m_notLoggedInForwardHotkey);
  saveHotkeyList(settings, "backward", m_notLoggedInBackwardHotkeys,
                 m_notLoggedInBackwardHotkey);
  settings.endGroup();

  settings.beginGroup("nonEVEHotkeys");
  saveHotkeyList(settings, "forward", m_nonEVEForwardHotkeys,
                 m_nonEVEForwardHotkey);
  saveHotkeyList(settings, "backward", m_nonEVEBackwardHotkeys,
                 m_nonEVEBackwardHotkey);
  settings.endGroup();

  settings.beginGroup("closeAllHotkeys");
  saveHotkeyList(settings, "closeAllClients", m_closeAllClientsHotkeys,
                 m_closeAllClientsHotkey);
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
  QMap<QString, QPair<int, int>> profileHotkeys =
      Config::instance().getAllProfileHotkeys();

  for (auto it = profileHotkeys.constBegin(); it != profileHotkeys.constEnd();
       ++it) {
    const QString &profileName = it.key();
    int key = it.value().first;
    int modifiers = it.value().second;

    HotkeyBinding binding;
    binding.keyCode = key;
    binding.ctrl = (modifiers & Qt::ControlModifier) != 0;
    binding.alt = (modifiers & Qt::AltModifier) != 0;
    binding.shift = (modifiers & Qt::ShiftModifier) != 0;
    binding.enabled = true;

    QString conflict = findHotkeyConflict(binding, profileName);
    if (!conflict.isEmpty()) {
      qWarning() << "Profile hotkey for" << profileName << "conflicts with"
                 << conflict;
    }

    int hotkeyId;
    if (registerHotkey(binding, hotkeyId)) {
      m_hotkeyIdToProfile.insert(hotkeyId, profileName);
      qDebug() << "Registered profile hotkey for" << profileName << "with ID"
               << hotkeyId;
    } else {
      qWarning() << "Failed to register profile hotkey for" << profileName
                 << "- hotkey may already be in use";
    }
  }
}

void HotkeyManager::unregisterProfileHotkeys() {
  for (int hotkeyId : m_hotkeyIdToProfile.keys()) {
    unregisterHotkey(hotkeyId);
  }
  m_hotkeyIdToProfile.clear();
}

QString HotkeyManager::findHotkeyConflict(const HotkeyBinding &binding,
                                          const QString &excludeProfile) const {
  if (!binding.enabled)
    return QString();

  if (m_suspendHotkey.enabled && m_suspendHotkey == binding) {
    return "Suspend/Resume Hotkey";
  }

  for (auto it = m_characterHotkeys.constBegin();
       it != m_characterHotkeys.constEnd(); ++it) {
    if (it.value() == binding) {
      return QString("Character: %1").arg(it.key());
    }
  }

  for (auto it = m_cycleGroups.constBegin(); it != m_cycleGroups.constEnd();
       ++it) {
    if (it.value().forwardBinding == binding) {
      return QString("Cycle Group '%1' (Forward)").arg(it.key());
    }
    if (it.value().backwardBinding == binding) {
      return QString("Cycle Group '%1' (Backward)").arg(it.key());
    }
  }

  if (m_notLoggedInForwardHotkey.enabled &&
      m_notLoggedInForwardHotkey == binding) {
    return "Not Logged In Cycle (Forward)";
  }
  if (m_notLoggedInBackwardHotkey.enabled &&
      m_notLoggedInBackwardHotkey == binding) {
    return "Not Logged In Cycle (Backward)";
  }

  if (m_nonEVEForwardHotkey.enabled && m_nonEVEForwardHotkey == binding) {
    return "Non-EVE Cycle (Forward)";
  }
  if (m_nonEVEBackwardHotkey.enabled && m_nonEVEBackwardHotkey == binding) {
    return "Non-EVE Cycle (Backward)";
  }

  QMap<QString, QPair<int, int>> profileHotkeys =
      Config::instance().getAllProfileHotkeys();
  for (auto it = profileHotkeys.constBegin(); it != profileHotkeys.constEnd();
       ++it) {
    const QString &profileName = it.key();

    if (!excludeProfile.isEmpty() && profileName == excludeProfile)
      continue;

    int key = it.value().first;
    int modifiers = it.value().second;

    HotkeyBinding profileBinding;
    profileBinding.keyCode = key;
    profileBinding.ctrl = (modifiers & Qt::ControlModifier) != 0;
    profileBinding.alt = (modifiers & Qt::AltModifier) != 0;
    profileBinding.shift = (modifiers & Qt::ShiftModifier) != 0;
    profileBinding.enabled = true;

    if (profileBinding == binding) {
      return QString("Profile: %1").arg(profileName);
    }
  }

  return QString();
}

bool HotkeyManager::hasHotkeyConflict(const HotkeyBinding &binding,
                                      const QString &excludeProfile) const {
  return !findHotkeyConflict(binding, excludeProfile).isEmpty();
}
