#include "hotkeymanager.h"
#include "config.h"
#include "windowcapture.h"
#include <QStringList>
#include <QSettings>
#include <Psapi.h>

HotkeyManager* HotkeyManager::s_instance = nullptr;

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
    , m_nextHotkeyId(1000)
    , m_suspendHotkeyId(-1)
    , m_suspended(false)
    , m_notLoggedInForwardHotkeyId(-1)
    , m_notLoggedInBackwardHotkeyId(-1)
    , m_nonEVEForwardHotkeyId(-1)
    , m_nonEVEBackwardHotkeyId(-1)
    , m_closeAllClientsHotkeyId(-1)
{
    s_instance = this;
    loadFromConfig();
}

HotkeyManager::~HotkeyManager()
{
    unregisterHotkeys();
    s_instance = nullptr;
}

bool HotkeyManager::registerHotkey(const HotkeyBinding& binding, int& outHotkeyId)
{
    if (!binding.enabled)
        return false;

    UINT modifiers = 0;
    if (binding.ctrl) modifiers |= MOD_CONTROL;
    if (binding.alt) modifiers |= MOD_ALT;
    if (binding.shift) modifiers |= MOD_SHIFT;

    int hotkeyId = m_nextHotkeyId++;
    
    bool wildcardMode = Config::instance().wildcardHotkeys();
    
    if (RegisterHotKey(nullptr, hotkeyId, modifiers, binding.keyCode))
    {
        outHotkeyId = hotkeyId;
        
        if (wildcardMode)
        {
            QVector<UINT> additionalMods;
            
            if (!binding.ctrl)
            {
                additionalMods.append(modifiers | MOD_CONTROL);
            }
            
            if (!binding.alt)
            {
                additionalMods.append(modifiers | MOD_ALT);
            }
            
            if (!binding.shift)
            {
                additionalMods.append(modifiers | MOD_SHIFT);
            }
            
            if (!binding.ctrl && !binding.alt)
            {
                additionalMods.append(modifiers | MOD_CONTROL | MOD_ALT);
            }
            if (!binding.ctrl && !binding.shift)
            {
                additionalMods.append(modifiers | MOD_CONTROL | MOD_SHIFT);
            }
            if (!binding.alt && !binding.shift)
            {
                additionalMods.append(modifiers | MOD_ALT | MOD_SHIFT);
            }
            if (!binding.ctrl && !binding.alt && !binding.shift)
            {
                additionalMods.append(modifiers | MOD_CONTROL | MOD_ALT | MOD_SHIFT);
            }
            
            for (UINT extraMod : additionalMods)
            {
                int extraHotkeyId = m_nextHotkeyId++;
                if (RegisterHotKey(nullptr, extraHotkeyId, extraMod, binding.keyCode))
                {
                    m_wildcardAliases.insert(extraHotkeyId, hotkeyId);
                }
            }
        }
        
        return true;
    }
    else
    {
        return false;
    }
}

void HotkeyManager::unregisterHotkey(int hotkeyId)
{
    UnregisterHotKey(nullptr, hotkeyId);
}

bool HotkeyManager::registerHotkeys()
{
    unregisterHotkeys();
    m_hotkeyIdToCharacter.clear();
    m_hotkeyIdToCycleGroup.clear();
    m_hotkeyIdIsForward.clear();

    // Register all suspend hotkeys
    if (!m_suspendHotkeys.isEmpty())
    {
        for (const HotkeyBinding& binding : m_suspendHotkeys)
        {
            if (!binding.enabled) continue;
            int hotkeyId;
            registerHotkey(binding, hotkeyId);
        }
    }
    else if (m_suspendHotkey.enabled)
    {
        registerHotkey(m_suspendHotkey, m_suspendHotkeyId);
    }

    if (m_suspended)
    {
        return true;
    }

    // Register all hotkeys for each character (including multiple bindings)
    for (auto it = m_characterMultiHotkeys.begin(); it != m_characterMultiHotkeys.end(); ++it)
    {
        const QString& characterName = it.key();
        const QVector<HotkeyBinding>& bindings = it.value();
        
        for (const HotkeyBinding& binding : bindings)
        {
            if (!binding.enabled) continue;
            
            int hotkeyId;
            if (registerHotkey(binding, hotkeyId))
            {
                m_hotkeyIdToCharacter.insert(hotkeyId, characterName);
            }
        }
    }
    
    // Also register legacy single hotkeys that aren't in multi-hotkeys
    for (auto it = m_characterHotkeys.begin(); it != m_characterHotkeys.end(); ++it)
    {
        const QString& characterName = it.key();
        
        // Skip if already registered via multi-hotkeys
        if (m_characterMultiHotkeys.contains(characterName)) {
            continue;
        }
        
        const HotkeyBinding& binding = it.value();
        
        int hotkeyId;
        if (registerHotkey(binding, hotkeyId))
        {
            m_hotkeyIdToCharacter.insert(hotkeyId, characterName);
        }
    }

    for (auto it = m_cycleGroups.begin(); it != m_cycleGroups.end(); ++it)
    {
        const QString& groupName = it.key();
        const CycleGroup& group = it.value();
        
        // Register all forward hotkeys (including multiple bindings)
        if (!group.forwardBindings.isEmpty())
        {
            for (const HotkeyBinding& binding : group.forwardBindings)
            {
                if (!binding.enabled) continue;
                
                int hotkeyId;
                if (registerHotkey(binding, hotkeyId))
                {
                    m_hotkeyIdToCycleGroup.insert(hotkeyId, groupName);
                    m_hotkeyIdIsForward.insert(hotkeyId, true);
                }
            }
        }
        else if (group.forwardBinding.enabled)
        {
            // Fallback to single binding for backward compatibility
            int hotkeyId;
            if (registerHotkey(group.forwardBinding, hotkeyId))
            {
                m_hotkeyIdToCycleGroup.insert(hotkeyId, groupName);
                m_hotkeyIdIsForward.insert(hotkeyId, true);
            }
        }
        
        // Register all backward hotkeys (including multiple bindings)
        if (!group.backwardBindings.isEmpty())
        {
            for (const HotkeyBinding& binding : group.backwardBindings)
            {
                if (!binding.enabled) continue;
                
                int hotkeyId;
                if (registerHotkey(binding, hotkeyId))
                {
                    m_hotkeyIdToCycleGroup.insert(hotkeyId, groupName);
                    m_hotkeyIdIsForward.insert(hotkeyId, false);
                }
            }
        }
        else if (group.backwardBinding.enabled)
        {
            // Fallback to single binding for backward compatibility
            int hotkeyId;
            if (registerHotkey(group.backwardBinding, hotkeyId))
            {
                m_hotkeyIdToCycleGroup.insert(hotkeyId, groupName);
                m_hotkeyIdIsForward.insert(hotkeyId, false);
            }
        }
    }
    
    // Register all not-logged-in forward hotkeys
    if (!m_notLoggedInForwardHotkeys.isEmpty())
    {
        for (const HotkeyBinding& binding : m_notLoggedInForwardHotkeys)
        {
            if (!binding.enabled) continue;
            int hotkeyId;
            registerHotkey(binding, hotkeyId);
        }
    }
    else if (m_notLoggedInForwardHotkey.enabled)
    {
        registerHotkey(m_notLoggedInForwardHotkey, m_notLoggedInForwardHotkeyId);
    }
    
    // Register all not-logged-in backward hotkeys
    if (!m_notLoggedInBackwardHotkeys.isEmpty())
    {
        for (const HotkeyBinding& binding : m_notLoggedInBackwardHotkeys)
        {
            if (!binding.enabled) continue;
            int hotkeyId;
            registerHotkey(binding, hotkeyId);
        }
    }
    else if (m_notLoggedInBackwardHotkey.enabled)
    {
        registerHotkey(m_notLoggedInBackwardHotkey, m_notLoggedInBackwardHotkeyId);
    }
    
    // Register all non-EVE forward hotkeys
    if (!m_nonEVEForwardHotkeys.isEmpty())
    {
        for (const HotkeyBinding& binding : m_nonEVEForwardHotkeys)
        {
            if (!binding.enabled) continue;
            int hotkeyId;
            registerHotkey(binding, hotkeyId);
        }
    }
    else if (m_nonEVEForwardHotkey.enabled)
    {
        registerHotkey(m_nonEVEForwardHotkey, m_nonEVEForwardHotkeyId);
    }
    
    // Register all non-EVE backward hotkeys
    if (!m_nonEVEBackwardHotkeys.isEmpty())
    {
        for (const HotkeyBinding& binding : m_nonEVEBackwardHotkeys)
        {
            if (!binding.enabled) continue;
            int hotkeyId;
            registerHotkey(binding, hotkeyId);
        }
    }
    else if (m_nonEVEBackwardHotkey.enabled)
    {
        registerHotkey(m_nonEVEBackwardHotkey, m_nonEVEBackwardHotkeyId);
    }
    
    // Register all close-all-clients hotkeys
    if (!m_closeAllClientsHotkeys.isEmpty())
    {
        for (const HotkeyBinding& binding : m_closeAllClientsHotkeys)
        {
            if (!binding.enabled) continue;
            int hotkeyId;
            registerHotkey(binding, hotkeyId);
        }
    }
    else if (m_closeAllClientsHotkey.enabled)
    {
        registerHotkey(m_closeAllClientsHotkey, m_closeAllClientsHotkeyId);
    }
    
    registerProfileHotkeys();
    
    return true;
}

void HotkeyManager::unregisterHotkeys()
{
    for (int hotkeyId : m_hotkeyIdToCharacter.keys())
    {
        unregisterHotkey(hotkeyId);
    }

    for (int hotkeyId : m_hotkeyIdToCycleGroup.keys())
    {
        unregisterHotkey(hotkeyId);
    }
    
    unregisterProfileHotkeys();
    
    for (int aliasId : m_wildcardAliases.keys())
    {
        unregisterHotkey(aliasId);
    }
    
    if (m_suspendHotkeyId != -1)
    {
        unregisterHotkey(m_suspendHotkeyId);
        m_suspendHotkeyId = -1;
    }
    
    if (m_notLoggedInForwardHotkeyId != -1)
    {
        unregisterHotkey(m_notLoggedInForwardHotkeyId);
        m_notLoggedInForwardHotkeyId = -1;
    }
    
    if (m_notLoggedInBackwardHotkeyId != -1)
    {
        unregisterHotkey(m_notLoggedInBackwardHotkeyId);
        m_notLoggedInBackwardHotkeyId = -1;
    }
    
    if (m_nonEVEForwardHotkeyId != -1)
    {
        unregisterHotkey(m_nonEVEForwardHotkeyId);
        m_nonEVEForwardHotkeyId = -1;
    }
    
    if (m_nonEVEBackwardHotkeyId != -1)
    {
        unregisterHotkey(m_nonEVEBackwardHotkeyId);
        m_nonEVEBackwardHotkeyId = -1;
    }
    
    if (m_closeAllClientsHotkeyId != -1)
    {
        unregisterHotkey(m_closeAllClientsHotkeyId);
        m_closeAllClientsHotkeyId = -1;
    }
    
    m_hotkeyIdToCharacter.clear();
    m_hotkeyIdToCycleGroup.clear();
    m_hotkeyIdIsForward.clear();
    m_wildcardAliases.clear();
}

static bool isForegroundWindowEVEClient()
{
    HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow)
        return false;
    
    DWORD processId = 0;
    GetWindowThreadProcessId(foregroundWindow, &processId);
    
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
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
    for (const QString& allowedName : allowedProcessNames) {
        if (processName.compare(allowedName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    
    return false;
}

bool HotkeyManager::nativeEventFilter(void* message, long* result)
{
    if (!s_instance)
        return false;
        
    MSG* msg = static_cast<MSG*>(message);
    
    if (msg->message == WM_HOTKEY)
    {
        int hotkeyId = static_cast<int>(msg->wParam);
        
        if (s_instance->m_wildcardAliases.contains(hotkeyId))
        {
            hotkeyId = s_instance->m_wildcardAliases.value(hotkeyId);
        }
        
        if (hotkeyId == s_instance->m_suspendHotkeyId)
        {
            s_instance->toggleSuspended();
            return true;
        }
        
        if (s_instance->m_suspended)
        {
            return true;
        }
        
        bool onlyWhenEVEFocused = Config::instance().hotkeysOnlyWhenEVEFocused();
        if (onlyWhenEVEFocused && !isForegroundWindowEVEClient())
        {
            return false;
        }
        
        if (s_instance->m_hotkeyIdToCharacter.contains(hotkeyId))
        {
            QString characterName = s_instance->m_hotkeyIdToCharacter.value(hotkeyId);
            emit s_instance->characterHotkeyPressed(characterName);
            return false;  // Allow message to continue, preserving keyboard state
        }
        
        if (s_instance->m_hotkeyIdToCycleGroup.contains(hotkeyId))
        {
            QString groupName = s_instance->m_hotkeyIdToCycleGroup.value(hotkeyId);
            bool isForward = s_instance->m_hotkeyIdIsForward.value(hotkeyId, true);
            
            if (isForward)
                emit s_instance->namedCycleForwardPressed(groupName);
            else
                emit s_instance->namedCycleBackwardPressed(groupName);
            
            return false;
        }
        
        if (hotkeyId == s_instance->m_notLoggedInForwardHotkeyId)
        {
            emit s_instance->notLoggedInCycleForwardPressed();
            return false;
        }
        
        if (hotkeyId == s_instance->m_notLoggedInBackwardHotkeyId)
        {
            emit s_instance->notLoggedInCycleBackwardPressed();
            return false;
        }
        
        if (hotkeyId == s_instance->m_nonEVEForwardHotkeyId)
        {
            emit s_instance->nonEVECycleForwardPressed();
            return false;
        }
        
        if (hotkeyId == s_instance->m_nonEVEBackwardHotkeyId)
        {
            emit s_instance->nonEVECycleBackwardPressed();
            return false;
        }
        
        if (hotkeyId == s_instance->m_closeAllClientsHotkeyId)
        {
            emit s_instance->closeAllClientsRequested();
            return false;
        }
        
        if (s_instance->m_hotkeyIdToProfile.contains(hotkeyId))
        {
            QString profileName = s_instance->m_hotkeyIdToProfile.value(hotkeyId);
            emit s_instance->profileSwitchRequested(profileName);
            return false;
        }
    }
    
    return false;
}

void HotkeyManager::setSuspended(bool suspended)
{
    if (m_suspended == suspended)
        return;
        
    m_suspended = suspended;
    registerHotkeys();
    emit suspendedChanged(m_suspended);
}

void HotkeyManager::toggleSuspended()
{
    setSuspended(!m_suspended);
}

void HotkeyManager::setSuspendHotkey(const HotkeyBinding& binding)
{
    m_suspendHotkey = binding;
    registerHotkeys();
}

void HotkeyManager::setCharacterHotkey(const QString& characterName, const HotkeyBinding& binding)
{
    m_characterHotkeys.insert(characterName, binding);
    // Also update multi-hotkeys with single binding
    QVector<HotkeyBinding> bindings;
    bindings.append(binding);
    m_characterMultiHotkeys.insert(characterName, bindings);
    registerHotkeys();
}

void HotkeyManager::setCharacterHotkeys(const QString& characterName, const QVector<HotkeyBinding>& bindings)
{
    m_characterMultiHotkeys.insert(characterName, bindings);
    // Keep first binding for legacy compatibility
    if (!bindings.isEmpty()) {
        m_characterHotkeys.insert(characterName, bindings.first());
    } else {
        m_characterHotkeys.remove(characterName);
    }
    registerHotkeys();
}

void HotkeyManager::addCharacterHotkey(const QString& characterName, const HotkeyBinding& binding)
{
    QVector<HotkeyBinding> bindings = m_characterMultiHotkeys.value(characterName);
    
    // Check if hotkey already exists
    for (const HotkeyBinding& existing : bindings) {
        if (existing == binding) {
            return; // Already exists
        }
    }
    
    bindings.append(binding);
    setCharacterHotkeys(characterName, bindings);
}

void HotkeyManager::removeCharacterHotkey(const QString& characterName)
{
    m_characterHotkeys.remove(characterName);
    m_characterMultiHotkeys.remove(characterName);
    registerHotkeys();
}

HotkeyBinding HotkeyManager::getCharacterHotkey(const QString& characterName) const
{
    return m_characterHotkeys.value(characterName, HotkeyBinding());
}

QVector<HotkeyBinding> HotkeyManager::getCharacterHotkeys(const QString& characterName) const
{
    if (m_characterMultiHotkeys.contains(characterName)) {
        return m_characterMultiHotkeys.value(characterName);
    }
    
    // Fallback to legacy single hotkey
    if (m_characterHotkeys.contains(characterName)) {
        QVector<HotkeyBinding> bindings;
        bindings.append(m_characterHotkeys.value(characterName));
        return bindings;
    }
    
    return QVector<HotkeyBinding>();
}

QHash<QString, QVector<HotkeyBinding>> HotkeyManager::getAllCharacterMultiHotkeys() const
{
    return m_characterMultiHotkeys;
}

QString HotkeyManager::getCharacterForHotkey(const HotkeyBinding& binding) const
{
    for (auto it = m_characterHotkeys.begin(); it != m_characterHotkeys.end(); ++it)
    {
        if (it.value() == binding)
            return it.key();
    }
    return QString();
}

void HotkeyManager::createCycleGroup(const QString& groupName, const QVector<QString>& characterNames,
                                     const HotkeyBinding& forwardKey, const HotkeyBinding& backwardKey)
{
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

void HotkeyManager::createCycleGroup(const CycleGroup& group)
{
    m_cycleGroups.insert(group.groupName, group);
    registerHotkeys();
}

void HotkeyManager::removeCycleGroup(const QString& groupName)
{
    m_cycleGroups.remove(groupName);
    registerHotkeys();
}

CycleGroup HotkeyManager::getCycleGroup(const QString& groupName) const
{
    return m_cycleGroups.value(groupName, CycleGroup());
}

void HotkeyManager::setNotLoggedInCycleHotkeys(const HotkeyBinding& forwardKey, const HotkeyBinding& backwardKey)
{
    m_notLoggedInForwardHotkey = forwardKey;
    m_notLoggedInBackwardHotkey = backwardKey;
    registerHotkeys();
}

void HotkeyManager::setNonEVECycleHotkeys(const HotkeyBinding& forwardKey, const HotkeyBinding& backwardKey)
{
    m_nonEVEForwardHotkey = forwardKey;
    m_nonEVEBackwardHotkey = backwardKey;
    registerHotkeys();
}

void HotkeyManager::setCloseAllClientsHotkey(const HotkeyBinding& binding)
{
    m_closeAllClientsHotkey = binding;
    registerHotkeys();
}

void HotkeyManager::updateCharacterWindows(const QHash<QString, HWND>& characterWindows)
{
    m_characterWindows = characterWindows;
}

HWND HotkeyManager::getWindowForCharacter(const QString& characterName) const
{
    return m_characterWindows.value(characterName, nullptr);
}

QString HotkeyManager::getCharacterForWindow(HWND hwnd) const
{
    for (auto it = m_characterWindows.constBegin(); it != m_characterWindows.constEnd(); ++it) {
        if (it.value() == hwnd) {
            return it.key();
        }
    }
    return QString(); 
}

int HotkeyManager::generateHotkeyId()
{
    return m_nextHotkeyId++;
}

void HotkeyManager::loadFromConfig()
{
    QSettings settings(Config::instance().configFilePath(), QSettings::IniFormat);
    
    // Clear previous hotkey data to prevent accumulation
    m_suspendHotkeys.clear();
    
    settings.beginGroup("hotkeys");
    QVariant suspendVar = settings.value("suspendHotkey");
    
    QString suspendStr;
    if (suspendVar.canConvert<QStringList>()) {
        suspendStr = suspendVar.toStringList().join('|');
    } else {
        suspendStr = suspendVar.toString();
    }
    
    // Load suspend hotkeys (might be multi-hotkey with pipe separator)
    if (!suspendStr.isEmpty())
    {
        QStringList suspendBindingStrs = suspendStr.split('|', Qt::SkipEmptyParts);
        for (const QString& bindingStr : suspendBindingStrs) {
            HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
            if (binding.enabled && binding.keyCode != 0) {
                // Only add if not already present (deduplicate)
                if (!m_suspendHotkeys.contains(binding)) {
                    m_suspendHotkeys.append(binding);
                }
            }
        }
        m_suspendHotkey = !m_suspendHotkeys.isEmpty() ? m_suspendHotkeys.first() : HotkeyBinding(VK_F12, true, true, true, true);
    }
    else
    {
        m_suspendHotkey = HotkeyBinding(VK_F12, true, true, true, true);
    }
    settings.endGroup();
    
    m_characterHotkeys.clear();  
    m_characterMultiHotkeys.clear();
    settings.beginGroup("characterHotkeys");
    QStringList characterKeys = settings.childKeys();
    for (const QString& characterName : characterKeys)
    {
        QVariant value = settings.value(characterName);
        
        // Try to load as multi-hotkey format (pipe-separated)
        QString valueStr;
        if (value.canConvert<QStringList>()) {
            valueStr = value.toStringList().join('|');
        } else {
            valueStr = value.toString();
        }
        
        QVector<HotkeyBinding> bindings;
        QStringList bindingStrs = valueStr.split('|', Qt::SkipEmptyParts);
        
        for (const QString& bindingStr : bindingStrs)
        {
            HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
            if (binding.enabled)
            {
                // Only add if not already present (deduplicate)
                if (!bindings.contains(binding)) {
                    bindings.append(binding);
                }
            }
        }
        
        if (!bindings.isEmpty())
        {
            m_characterMultiHotkeys.insert(characterName, bindings);
            // Keep first binding for legacy compatibility
            m_characterHotkeys.insert(characterName, bindings.first());
        }
    }
    settings.endGroup();
    
    m_cycleGroups.clear();  
    
    settings.beginGroup("cycleGroups");
    QStringList groupKeys = settings.childKeys();
    for (const QString& groupName : groupKeys)
    {
        QVariant groupValue = settings.value(groupName);
        
        QString groupStr;
        if (groupValue.canConvert<QStringList>())
        {
            QStringList parts = groupValue.toStringList();
            groupStr = parts.join(',');
        }
        else
        {
            groupStr = groupValue.toString();
        }
        
        QStringList parts = groupStr.split('|');
        if (parts.size() >= 3)
        {
            CycleGroup group;
            group.groupName = groupName;
            QStringList charNames = parts[0].split(',', Qt::SkipEmptyParts);
            for (const QString& name : charNames)
                group.characterNames.append(name);
            
            // Parse forward hotkeys (might be multi-hotkey with semicolon separator)
            QStringList forwardBindingStrs = parts[1].split(';', Qt::SkipEmptyParts);
            for (const QString& bindingStr : forwardBindingStrs) {
                HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
                if (binding.enabled && binding.keyCode != 0) {
                    // Only add if not already present (deduplicate)
                    if (!group.forwardBindings.contains(binding)) {
                        group.forwardBindings.append(binding);
                    }
                }
            }
            group.forwardBinding = !group.forwardBindings.isEmpty() ? group.forwardBindings.first() : HotkeyBinding();
            
            // Parse backward hotkeys (might be multi-hotkey with semicolon separator)
            QStringList backwardBindingStrs = parts[2].split(';', Qt::SkipEmptyParts);
            for (const QString& bindingStr : backwardBindingStrs) {
                HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
                if (binding.enabled && binding.keyCode != 0) {
                    // Only add if not already present (deduplicate)
                    if (!group.backwardBindings.contains(binding)) {
                        group.backwardBindings.append(binding);
                    }
                }
            }
            group.backwardBinding = !group.backwardBindings.isEmpty() ? group.backwardBindings.first() : HotkeyBinding();
            
            group.includeNotLoggedIn = (parts.size() >= 4) ? (parts[3].toInt() != 0) : false;
            group.noLoop = (parts.size() >= 5) ? (parts[4].toInt() != 0) : false;
            
            m_cycleGroups.insert(groupName, group);
        }
    }
    settings.endGroup();
    
    m_notLoggedInForwardHotkeys.clear();
    m_notLoggedInBackwardHotkeys.clear();
    settings.beginGroup("notLoggedInHotkeys");
    // Load forward hotkeys (might be multi-hotkey with pipe separator)
    QVariant forwardVar = settings.value("forward");
    QString forwardStr = forwardVar.canConvert<QStringList>() ? forwardVar.toStringList().join('|') : forwardVar.toString();
    if (!forwardStr.isEmpty()) {
        QStringList forwardBindingStrs = forwardStr.split('|', Qt::SkipEmptyParts);
        for (const QString& bindingStr : forwardBindingStrs) {
            HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
            if (binding.enabled && binding.keyCode != 0) {
                // Only add if not already present (deduplicate)
                if (!m_notLoggedInForwardHotkeys.contains(binding)) {
                    m_notLoggedInForwardHotkeys.append(binding);
                }
            }
        }
        m_notLoggedInForwardHotkey = !m_notLoggedInForwardHotkeys.isEmpty() ? m_notLoggedInForwardHotkeys.first() : HotkeyBinding();
    }
    
    // Load backward hotkeys (might be multi-hotkey with pipe separator)
    QVariant backwardVar = settings.value("backward");
    QString backwardStr = backwardVar.canConvert<QStringList>() ? backwardVar.toStringList().join('|') : backwardVar.toString();
    if (!backwardStr.isEmpty()) {
        QStringList backwardBindingStrs = backwardStr.split('|', Qt::SkipEmptyParts);
        for (const QString& bindingStr : backwardBindingStrs) {
            HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
            if (binding.enabled && binding.keyCode != 0) {
                // Only add if not already present (deduplicate)
                if (!m_notLoggedInBackwardHotkeys.contains(binding)) {
                    m_notLoggedInBackwardHotkeys.append(binding);
                }
            }
        }
        m_notLoggedInBackwardHotkey = !m_notLoggedInBackwardHotkeys.isEmpty() ? m_notLoggedInBackwardHotkeys.first() : HotkeyBinding();
    }
    settings.endGroup();
    
    m_nonEVEForwardHotkeys.clear();
    m_nonEVEBackwardHotkeys.clear();
    settings.beginGroup("nonEVEHotkeys");
    // Load forward hotkeys (might be multi-hotkey with pipe separator)
    QVariant nonEVEForwardVar = settings.value("forward");
    QString nonEVEForwardStr = nonEVEForwardVar.canConvert<QStringList>() ? nonEVEForwardVar.toStringList().join('|') : nonEVEForwardVar.toString();
    if (!nonEVEForwardStr.isEmpty()) {
        QStringList nonEVEForwardBindingStrs = nonEVEForwardStr.split('|', Qt::SkipEmptyParts);
        for (const QString& bindingStr : nonEVEForwardBindingStrs) {
            HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
            if (binding.enabled && binding.keyCode != 0) {
                // Only add if not already present (deduplicate)
                if (!m_nonEVEForwardHotkeys.contains(binding)) {
                    m_nonEVEForwardHotkeys.append(binding);
                }
            }
        }
        m_nonEVEForwardHotkey = !m_nonEVEForwardHotkeys.isEmpty() ? m_nonEVEForwardHotkeys.first() : HotkeyBinding();
    }
    
    // Load backward hotkeys (might be multi-hotkey with pipe separator)
    QVariant nonEVEBackwardVar = settings.value("backward");
    QString nonEVEBackwardStr = nonEVEBackwardVar.canConvert<QStringList>() ? nonEVEBackwardVar.toStringList().join('|') : nonEVEBackwardVar.toString();
    if (!nonEVEBackwardStr.isEmpty()) {
        QStringList nonEVEBackwardBindingStrs = nonEVEBackwardStr.split('|', Qt::SkipEmptyParts);
        for (const QString& bindingStr : nonEVEBackwardBindingStrs) {
            HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
            if (binding.enabled && binding.keyCode != 0) {
                // Only add if not already present (deduplicate)
                if (!m_nonEVEBackwardHotkeys.contains(binding)) {
                    m_nonEVEBackwardHotkeys.append(binding);
                }
            }
        }
        m_nonEVEBackwardHotkey = !m_nonEVEBackwardHotkeys.isEmpty() ? m_nonEVEBackwardHotkeys.first() : HotkeyBinding();
    }
    settings.endGroup();
    
    m_closeAllClientsHotkeys.clear();
    settings.beginGroup("closeAllHotkeys");
    // Load close all hotkeys (might be multi-hotkey with pipe separator)
    QVariant closeAllVar = settings.value("closeAllClients");
    QString closeAllStr = closeAllVar.canConvert<QStringList>() ? closeAllVar.toStringList().join('|') : closeAllVar.toString();
    if (!closeAllStr.isEmpty()) {
        QStringList closeAllBindingStrs = closeAllStr.split('|', Qt::SkipEmptyParts);
        for (const QString& bindingStr : closeAllBindingStrs) {
            HotkeyBinding binding = HotkeyBinding::fromString(bindingStr);
            if (binding.enabled && binding.keyCode != 0) {
                // Only add if not already present (deduplicate)
                if (!m_closeAllClientsHotkeys.contains(binding)) {
                    m_closeAllClientsHotkeys.append(binding);
                }
            }
        }
        m_closeAllClientsHotkey = !m_closeAllClientsHotkeys.isEmpty() ? m_closeAllClientsHotkeys.first() : HotkeyBinding();
    }
    settings.endGroup();
    
    registerHotkeys();
}

void HotkeyManager::saveToConfig()
{
    QSettings settings(Config::instance().configFilePath(), QSettings::IniFormat);
    
    settings.beginGroup("hotkeys");
    // Save suspend hotkeys (multi-hotkey support)
    // Remove the old value first to prevent appending
    settings.remove("suspendHotkey");
    if (!m_suspendHotkeys.isEmpty()) {
        QStringList suspendBindingStrs;
        for (const HotkeyBinding& binding : m_suspendHotkeys) {
            suspendBindingStrs.append(binding.toString());
        }
        settings.setValue("suspendHotkey", suspendBindingStrs.join('|'));
    } else {
        settings.setValue("suspendHotkey", m_suspendHotkey.toString());
    }
    settings.endGroup();
    
    settings.beginGroup("characterHotkeys");
    settings.remove("");
    
    // Save multi-hotkeys (prioritize this over legacy single hotkeys)
    for (auto it = m_characterMultiHotkeys.begin(); it != m_characterMultiHotkeys.end(); ++it)
    {
        QStringList bindingStrs;
        for (const HotkeyBinding& binding : it.value())
        {
            bindingStrs.append(binding.toString());
        }
        settings.setValue(it.key(), bindingStrs.join('|'));
    }
    
    // Also save legacy single hotkeys that aren't in multi-hotkeys (for backward compatibility)
    for (auto it = m_characterHotkeys.begin(); it != m_characterHotkeys.end(); ++it)
    {
        if (!m_characterMultiHotkeys.contains(it.key())) {
            settings.setValue(it.key(), it.value().toString());
        }
    }
    settings.endGroup();
    
    settings.beginGroup("cycleGroups");
    settings.remove("");
    for (auto it = m_cycleGroups.begin(); it != m_cycleGroups.end(); ++it)
    {
        const CycleGroup& group = it.value();
        QStringList charNames;
        for (const QString& name : group.characterNames)
            charNames.append(name);
        
        // Save multi-hotkeys if available
        QStringList forwardBindingStrs;
        QStringList backwardBindingStrs;
        
        if (!group.forwardBindings.isEmpty()) {
            for (const HotkeyBinding& binding : group.forwardBindings) {
                forwardBindingStrs.append(binding.toString());
            }
        } else {
            // Fallback to single binding for backward compatibility
            forwardBindingStrs.append(group.forwardBinding.toString());
        }
        
        if (!group.backwardBindings.isEmpty()) {
            for (const HotkeyBinding& binding : group.backwardBindings) {
                backwardBindingStrs.append(binding.toString());
            }
        } else {
            // Fallback to single binding for backward compatibility
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
    // Remove old values to prevent appending
    settings.remove("forward");
    settings.remove("backward");
    
    // Save not-logged-in forward hotkeys (multi-hotkey support)
    if (!m_notLoggedInForwardHotkeys.isEmpty()) {
        QStringList forwardBindingStrs;
        for (const HotkeyBinding& binding : m_notLoggedInForwardHotkeys) {
            forwardBindingStrs.append(binding.toString());
        }
        settings.setValue("forward", forwardBindingStrs.join('|'));
    } else {
        settings.setValue("forward", m_notLoggedInForwardHotkey.toString());
    }
    
    // Save not-logged-in backward hotkeys (multi-hotkey support)
    if (!m_notLoggedInBackwardHotkeys.isEmpty()) {
        QStringList backwardBindingStrs;
        for (const HotkeyBinding& binding : m_notLoggedInBackwardHotkeys) {
            backwardBindingStrs.append(binding.toString());
        }
        settings.setValue("backward", backwardBindingStrs.join('|'));
    } else {
        settings.setValue("backward", m_notLoggedInBackwardHotkey.toString());
    }
    settings.endGroup();
    
    settings.beginGroup("nonEVEHotkeys");
    // Remove old values to prevent appending
    settings.remove("forward");
    settings.remove("backward");
    
    // Save non-EVE forward hotkeys (multi-hotkey support)
    if (!m_nonEVEForwardHotkeys.isEmpty()) {
        QStringList forwardBindingStrs;
        for (const HotkeyBinding& binding : m_nonEVEForwardHotkeys) {
            forwardBindingStrs.append(binding.toString());
        }
        settings.setValue("forward", forwardBindingStrs.join('|'));
    } else {
        settings.setValue("forward", m_nonEVEForwardHotkey.toString());
    }
    
    // Save non-EVE backward hotkeys (multi-hotkey support)
    if (!m_nonEVEBackwardHotkeys.isEmpty()) {
        QStringList backwardBindingStrs;
        for (const HotkeyBinding& binding : m_nonEVEBackwardHotkeys) {
            backwardBindingStrs.append(binding.toString());
        }
        settings.setValue("backward", backwardBindingStrs.join('|'));
    } else {
        settings.setValue("backward", m_nonEVEBackwardHotkey.toString());
    }
    settings.endGroup();
    
    settings.beginGroup("closeAllHotkeys");
    // Remove old value to prevent appending
    settings.remove("closeAllClients");
    
    // Save close-all hotkeys (multi-hotkey support)
    if (!m_closeAllClientsHotkeys.isEmpty()) {
        QStringList closeAllBindingStrs;
        for (const HotkeyBinding& binding : m_closeAllClientsHotkeys) {
            closeAllBindingStrs.append(binding.toString());
        }
        settings.setValue("closeAllClients", closeAllBindingStrs.join('|'));
    } else {
        settings.setValue("closeAllClients", m_closeAllClientsHotkey.toString());
    }
    settings.endGroup();
    
    settings.sync();
}

bool HotkeyBinding::operator<(const HotkeyBinding& other) const
{
    if (keyCode != other.keyCode) return keyCode < other.keyCode;
    if (ctrl != other.ctrl) return ctrl < other.ctrl;
    if (alt != other.alt) return alt < other.alt;
    if (shift != other.shift) return shift < other.shift;
    return enabled < other.enabled;
}

bool HotkeyBinding::operator==(const HotkeyBinding& other) const
{
    return enabled == other.enabled &&
           keyCode == other.keyCode &&
           ctrl == other.ctrl &&
           alt == other.alt &&
           shift == other.shift;
}

QString HotkeyBinding::toString() const
{
    return QString("%1,%2,%3,%4,%5")
        .arg(enabled ? 1 : 0)
        .arg(keyCode)
        .arg(ctrl ? 1 : 0)
        .arg(alt ? 1 : 0)
        .arg(shift ? 1 : 0);
}

HotkeyBinding HotkeyBinding::fromString(const QString& str)
{
    QStringList parts = str.split(',');
    if (parts.size() == 5)
    {
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

void HotkeyManager::registerProfileHotkeys()
{
    QMap<QString, QPair<int, int>> profileHotkeys = Config::instance().getAllProfileHotkeys();
    
    for (auto it = profileHotkeys.constBegin(); it != profileHotkeys.constEnd(); ++it)
    {
        const QString& profileName = it.key();
        int key = it.value().first;
        int modifiers = it.value().second;
        
        HotkeyBinding binding;
        binding.keyCode = key;
        binding.ctrl = (modifiers & Qt::ControlModifier) != 0;
        binding.alt = (modifiers & Qt::AltModifier) != 0;
        binding.shift = (modifiers & Qt::ShiftModifier) != 0;
        binding.enabled = true;
        
        QString conflict = findHotkeyConflict(binding, profileName);
        if (!conflict.isEmpty())
        {
            qWarning() << "Profile hotkey for" << profileName << "conflicts with" << conflict;
        }
        
        int hotkeyId;
        if (registerHotkey(binding, hotkeyId))
        {
            m_hotkeyIdToProfile.insert(hotkeyId, profileName);
            qDebug() << "Registered profile hotkey for" << profileName << "with ID" << hotkeyId;
        }
        else
        {
            qWarning() << "Failed to register profile hotkey for" << profileName << "- hotkey may already be in use";
        }
    }
}

void HotkeyManager::unregisterProfileHotkeys()
{
    for (int hotkeyId : m_hotkeyIdToProfile.keys())
    {
        unregisterHotkey(hotkeyId);
    }
    m_hotkeyIdToProfile.clear();
}

QString HotkeyManager::findHotkeyConflict(const HotkeyBinding& binding, const QString& excludeProfile) const
{
    if (!binding.enabled)
        return QString();
    
    if (m_suspendHotkey.enabled && m_suspendHotkey == binding)
    {
        return "Suspend/Resume Hotkey";
    }
    
    for (auto it = m_characterHotkeys.constBegin(); it != m_characterHotkeys.constEnd(); ++it)
    {
        if (it.value() == binding)
        {
            return QString("Character: %1").arg(it.key());
        }
    }
    
    for (auto it = m_cycleGroups.constBegin(); it != m_cycleGroups.constEnd(); ++it)
    {
        if (it.value().forwardBinding == binding)
        {
            return QString("Cycle Group '%1' (Forward)").arg(it.key());
        }
        if (it.value().backwardBinding == binding)
        {
            return QString("Cycle Group '%1' (Backward)").arg(it.key());
        }
    }
    
    if (m_notLoggedInForwardHotkey.enabled && m_notLoggedInForwardHotkey == binding)
    {
        return "Not Logged In Cycle (Forward)";
    }
    if (m_notLoggedInBackwardHotkey.enabled && m_notLoggedInBackwardHotkey == binding)
    {
        return "Not Logged In Cycle (Backward)";
    }
    
    if (m_nonEVEForwardHotkey.enabled && m_nonEVEForwardHotkey == binding)
    {
        return "Non-EVE Cycle (Forward)";
    }
    if (m_nonEVEBackwardHotkey.enabled && m_nonEVEBackwardHotkey == binding)
    {
        return "Non-EVE Cycle (Backward)";
    }
    
    QMap<QString, QPair<int, int>> profileHotkeys = Config::instance().getAllProfileHotkeys();
    for (auto it = profileHotkeys.constBegin(); it != profileHotkeys.constEnd(); ++it)
    {
        const QString& profileName = it.key();
        
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
        
        if (profileBinding == binding)
        {
            return QString("Profile: %1").arg(profileName);
        }
    }
    
    return QString();  
}

bool HotkeyManager::hasHotkeyConflict(const HotkeyBinding& binding, const QString& excludeProfile) const
{
    return !findHotkeyConflict(binding, excludeProfile).isEmpty();
}
