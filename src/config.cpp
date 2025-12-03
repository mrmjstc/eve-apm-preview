#include "config.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QPoint>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QKeySequence>

Config::Config()
{
    loadGlobalSettings();
    
    migrateToProfileSystem();
    
    QString profileToLoad = m_currentProfileName.isEmpty() ? "default" : m_currentProfileName;
    
    if (!profileExists(profileToLoad)) {
        qDebug() << "Profile" << profileToLoad << "does not exist. Creating default profile.";
        initializeDefaultProfile();
        profileToLoad = "default";
    }
    
    QString profilePath = getProfileFilePath(profileToLoad);
    m_settings = std::make_unique<QSettings>(profilePath, QSettings::IniFormat);
    m_currentProfileName = profileToLoad;
    
    if (!m_settings->contains(KEY_CONFIG_VERSION)) {
        initializeDefaultProfile();
    }
    
    saveGlobalSettings();
}

Config::~Config()
{
    save();
}

Config& Config::instance()
{
    static Config instance;
    return instance;
}

void Config::refreshCache() const
{
    if (m_cacheValid) {
        return;
    }
    
    m_cachedHighlightActive = m_settings->value(KEY_UI_HIGHLIGHT_ACTIVE, DEFAULT_UI_HIGHLIGHT_ACTIVE).toBool();
    m_cachedHideActiveThumbnail = m_settings->value(KEY_UI_HIDE_ACTIVE_THUMBNAIL, DEFAULT_UI_HIDE_ACTIVE_THUMBNAIL).toBool();
    m_cachedHighlightColor = QColor(m_settings->value(KEY_UI_HIGHLIGHT_COLOR, DEFAULT_UI_HIGHLIGHT_COLOR).toString());
    m_cachedHighlightBorderWidth = m_settings->value(KEY_UI_HIGHLIGHT_BORDER_WIDTH, DEFAULT_UI_HIGHLIGHT_BORDER_WIDTH).toInt();
    
    m_cachedThumbnailWidth = m_settings->value(KEY_THUMBNAIL_WIDTH, DEFAULT_THUMBNAIL_WIDTH).toInt();
    m_cachedThumbnailHeight = m_settings->value(KEY_THUMBNAIL_HEIGHT, DEFAULT_THUMBNAIL_HEIGHT).toInt();
    m_cachedRefreshInterval = m_settings->value(KEY_THUMBNAIL_REFRESH_INTERVAL, DEFAULT_THUMBNAIL_REFRESH_INTERVAL).toInt();
    m_cachedThumbnailOpacity = qBound(OPACITY_MIN, m_settings->value(KEY_THUMBNAIL_OPACITY, DEFAULT_THUMBNAIL_OPACITY).toInt(), OPACITY_MAX);
    
    m_cachedShowNotLoggedIn = m_settings->value(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN, DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN).toBool();
    m_cachedNotLoggedInStackMode = m_settings->value(KEY_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE, DEFAULT_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE).toInt();
    m_cachedNotLoggedInReferencePosition = m_settings->value(KEY_THUMBNAIL_NOT_LOGGED_IN_REF_POSITION, 
        QPoint(DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_X, DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_Y)).toPoint();
    m_cachedShowNotLoggedInOverlay = m_settings->value(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY, DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY).toBool();
    m_cachedShowNonEVEOverlay = m_settings->value(KEY_THUMBNAIL_SHOW_NON_EVE_OVERLAY, DEFAULT_THUMBNAIL_SHOW_NON_EVE_OVERLAY).toBool();
    
    QStringList defaultProcessNames;
    defaultProcessNames << DEFAULT_THUMBNAIL_PROCESS_NAME;
    m_cachedProcessNames = m_settings->value(KEY_THUMBNAIL_PROCESS_NAMES, defaultProcessNames).toStringList();
    
    m_cachedAlwaysOnTop = m_settings->value(KEY_WINDOW_ALWAYS_ON_TOP, DEFAULT_WINDOW_ALWAYS_ON_TOP).toBool();
    m_cachedMinimizeInactive = m_settings->value(KEY_WINDOW_MINIMIZE_INACTIVE, DEFAULT_WINDOW_MINIMIZE_INACTIVE).toBool();
    m_cachedMinimizeDelay = m_settings->value(KEY_WINDOW_MINIMIZE_DELAY, DEFAULT_WINDOW_MINIMIZE_DELAY).toInt();
    m_cachedNeverMinimizeCharacters = m_settings->value(KEY_WINDOW_NEVER_MINIMIZE_CHARACTERS, QStringList()).toStringList();
    m_cachedSaveClientLocation = m_settings->value(KEY_WINDOW_SAVE_CLIENT_LOCATION, DEFAULT_WINDOW_SAVE_CLIENT_LOCATION).toBool();
    
    m_cachedRememberPositions = m_settings->value(KEY_POSITION_REMEMBER, DEFAULT_POSITION_REMEMBER).toBool();
    m_cachedPreserveLogoutPositions = m_settings->value(KEY_POSITION_PRESERVE_LOGOUT, DEFAULT_POSITION_PRESERVE_LOGOUT).toBool();
    m_cachedEnableSnapping = m_settings->value(KEY_POSITION_ENABLE_SNAPPING, DEFAULT_POSITION_ENABLE_SNAPPING).toBool();
    m_cachedSnapDistance = m_settings->value(KEY_POSITION_SNAP_DISTANCE, DEFAULT_POSITION_SNAP_DISTANCE).toInt();
    m_cachedLockPositions = m_settings->value(KEY_POSITION_LOCK, DEFAULT_POSITION_LOCK).toBool();
    
    m_cachedWildcardHotkeys = m_settings->value(KEY_HOTKEY_WILDCARD, DEFAULT_HOTKEY_WILDCARD).toBool();
    m_cachedHotkeysOnlyWhenEVEFocused = m_settings->value(KEY_HOTKEY_ONLY_WHEN_EVE_FOCUSED, DEFAULT_HOTKEY_ONLY_WHEN_EVE_FOCUSED).toBool();
    
    m_cachedShowCharacterName = m_settings->value(KEY_OVERLAY_SHOW_CHARACTER, DEFAULT_OVERLAY_SHOW_CHARACTER).toBool();
    m_cachedCharacterNameColor = QColor(m_settings->value(KEY_OVERLAY_CHARACTER_COLOR, DEFAULT_OVERLAY_CHARACTER_COLOR).toString());
    m_cachedCharacterNamePosition = m_settings->value(KEY_OVERLAY_CHARACTER_POSITION, DEFAULT_OVERLAY_CHARACTER_POSITION).toInt();
    QFont defaultCharFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE);
    m_cachedCharacterNameFont.fromString(m_settings->value(KEY_OVERLAY_CHARACTER_FONT, defaultCharFont.toString()).toString());
    
    m_cachedShowSystemName = m_settings->value(KEY_OVERLAY_SHOW_SYSTEM, DEFAULT_OVERLAY_SHOW_SYSTEM).toBool();
    m_cachedSystemNameColor = QColor(m_settings->value(KEY_OVERLAY_SYSTEM_COLOR, DEFAULT_OVERLAY_SYSTEM_COLOR).toString());
    m_cachedSystemNamePosition = m_settings->value(KEY_OVERLAY_SYSTEM_POSITION, DEFAULT_OVERLAY_SYSTEM_POSITION).toInt();
    QFont defaultSysFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE);
    m_cachedSystemNameFont.fromString(m_settings->value(KEY_OVERLAY_SYSTEM_FONT, defaultSysFont.toString()).toString());
    
    m_cachedShowOverlayBackground = m_settings->value(KEY_OVERLAY_SHOW_BACKGROUND, DEFAULT_OVERLAY_SHOW_BACKGROUND).toBool();
    m_cachedOverlayBackgroundColor = QColor(m_settings->value(KEY_OVERLAY_BACKGROUND_COLOR, DEFAULT_OVERLAY_BACKGROUND_COLOR).toString());
    m_cachedOverlayBackgroundOpacity = qBound(OPACITY_MIN, m_settings->value(KEY_OVERLAY_BACKGROUND_OPACITY, DEFAULT_OVERLAY_BACKGROUND_OPACITY).toInt(), OPACITY_MAX);
    
    QFont defaultFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE);
    m_cachedOverlayFont.fromString(m_settings->value(KEY_OVERLAY_FONT, defaultFont.toString()).toString());
    
    m_cachedEnableChatLogMonitoring = m_settings->value(KEY_CHATLOG_ENABLE_MONITORING, DEFAULT_CHATLOG_ENABLE_MONITORING).toBool();
    m_cachedChatLogDirectory = m_settings->value(KEY_CHATLOG_DIRECTORY, getDefaultChatLogDirectory()).toString();
    m_cachedEnableGameLogMonitoring = m_settings->value(KEY_GAMELOG_ENABLE_MONITORING, DEFAULT_GAMELOG_ENABLE_MONITORING).toBool();
    m_cachedGameLogDirectory = m_settings->value(KEY_GAMELOG_DIRECTORY, getDefaultGameLogDirectory()).toString();
    m_cachedFileChangeDebounceMs = m_settings->value(KEY_CHATLOG_FILEDEBOUNCE_MS, DEFAULT_CHATLOG_FILEDEBOUNCE_MS).toInt();
    
    m_cachedShowCombatMessages = m_settings->value(KEY_COMBAT_ENABLED, DEFAULT_COMBAT_MESSAGES_ENABLED).toBool();
    m_cachedCombatMessagePosition = m_settings->value(KEY_COMBAT_POSITION, DEFAULT_COMBAT_MESSAGE_POSITION).toInt();
    QFont defaultCombatFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE);
    defaultCombatFont.setBold(true);
    m_cachedCombatMessageFont = m_settings->value(KEY_COMBAT_FONT, defaultCombatFont).value<QFont>();
    
    m_cachedCombatEventColors.clear();
    m_cachedCombatEventDurations.clear();
    m_cachedCombatEventBorderHighlights.clear();
    QStringList eventTypes = DEFAULT_COMBAT_MESSAGE_EVENT_TYPES();
    for (const QString& eventType : eventTypes) {
        QString colorKey = combatEventColorKey(eventType);
        QString defaultColor = DEFAULT_EVENT_COLORS().value(eventType, DEFAULT_COMBAT_MESSAGE_COLOR);
        m_cachedCombatEventColors[eventType] = m_settings->value(colorKey, QColor(defaultColor)).value<QColor>();
        
        QString durationKey = combatEventDurationKey(eventType);
        m_cachedCombatEventDurations[eventType] = m_settings->value(durationKey, DEFAULT_COMBAT_MESSAGE_DURATION).toInt();
        
        QString borderKey = combatEventBorderHighlightKey(eventType);
        m_cachedCombatEventBorderHighlights[eventType] = m_settings->value(borderKey, DEFAULT_COMBAT_EVENT_BORDER_HIGHLIGHT).toBool();
    }
    
    m_cachedEnabledCombatEventTypes = m_settings->value(KEY_COMBAT_ENABLED_EVENT_TYPES, DEFAULT_COMBAT_MESSAGE_EVENT_TYPES()).toStringList();
    m_cachedMiningTimeoutSeconds = m_settings->value(KEY_MINING_TIMEOUT_SECONDS, DEFAULT_MINING_TIMEOUT_SECONDS).toInt();
    
    m_cacheValid = true;
}

void Config::invalidateCache()
{
    m_cacheValid = false;
}

int Config::fileChangeDebounceMs() const
{
    refreshCache();
    return m_cachedFileChangeDebounceMs;
}

void Config::setFileChangeDebounceMs(int milliseconds)
{
    m_settings->setValue(KEY_CHATLOG_FILEDEBOUNCE_MS, milliseconds);
    invalidateCache();
}

int Config::fileChangeDebounceForCharacter(const QString& characterName) const
{
    QString key = QString("%1/%2").arg(KEY_CHATLOG_FILEDEBOUNCE_CHAR_GROUP).arg(characterName);
    if (m_settings->contains(key)) {
        return m_settings->value(key, -1).toInt();
    }
    return -1; 
}

void Config::setFileChangeDebounceForCharacter(const QString& characterName, int milliseconds)
{
    QString key = QString("%1/%2").arg(KEY_CHATLOG_FILEDEBOUNCE_CHAR_GROUP).arg(characterName);
    m_settings->setValue(key, milliseconds);
}

void Config::removeFileChangeDebounceForCharacter(const QString& characterName)
{
    QString key = QString("%1/%2").arg(KEY_CHATLOG_FILEDEBOUNCE_CHAR_GROUP).arg(characterName);
    m_settings->remove(key);
}

QHash<QString, int> Config::getAllFileChangeDebounceThresholds() const
{
    QHash<QString, int> thresholds;
    m_settings->beginGroup(KEY_CHATLOG_FILEDEBOUNCE_CHAR_GROUP);
    QStringList keys = m_settings->childKeys();
    for (const QString& k : keys) {
        int v = m_settings->value(k).toInt();
        thresholds.insert(k, v);
    }
    m_settings->endGroup();
    return thresholds;
}

bool Config::highlightActiveWindow() const
{
    refreshCache();
    return m_cachedHighlightActive;
}

void Config::setHighlightActiveWindow(bool enabled)
{
    m_settings->setValue(KEY_UI_HIGHLIGHT_ACTIVE, enabled);
    invalidateCache();
}

bool Config::hideActiveClientThumbnail() const
{
    refreshCache();
    return m_cachedHideActiveThumbnail;
}

void Config::setHideActiveClientThumbnail(bool enabled)
{
    m_settings->setValue(KEY_UI_HIDE_ACTIVE_THUMBNAIL, enabled);
    invalidateCache();
}

QColor Config::highlightColor() const
{
    refreshCache();
    return m_cachedHighlightColor;
}

void Config::setHighlightColor(const QColor& color)
{
    m_settings->setValue(KEY_UI_HIGHLIGHT_COLOR, color.name());
    invalidateCache();
}

int Config::highlightBorderWidth() const
{
    refreshCache();
    return m_cachedHighlightBorderWidth;
}

void Config::setHighlightBorderWidth(int width)
{
    m_settings->setValue(KEY_UI_HIGHLIGHT_BORDER_WIDTH, width);
    invalidateCache();
}

int Config::thumbnailWidth() const
{
    refreshCache();
    return m_cachedThumbnailWidth;
}

void Config::setThumbnailWidth(int width)
{
    m_settings->setValue(KEY_THUMBNAIL_WIDTH, width);
    invalidateCache();
}

int Config::thumbnailHeight() const
{
    refreshCache();
    return m_cachedThumbnailHeight;
}

void Config::setThumbnailHeight(int height)
{
    m_settings->setValue(KEY_THUMBNAIL_HEIGHT, height);
    invalidateCache();
}

int Config::refreshInterval() const
{
    refreshCache();
    return m_cachedRefreshInterval;
}

void Config::setRefreshInterval(int milliseconds)
{
    m_settings->setValue(KEY_THUMBNAIL_REFRESH_INTERVAL, milliseconds);
    invalidateCache();
}

int Config::thumbnailOpacity() const
{
    refreshCache();
    return m_cachedThumbnailOpacity;
}

void Config::setThumbnailOpacity(int opacity)
{
    m_settings->setValue(KEY_THUMBNAIL_OPACITY, qBound(OPACITY_MIN, opacity, OPACITY_MAX));
    invalidateCache();
}

bool Config::showNotLoggedInClients() const
{
    refreshCache();
    return m_cachedShowNotLoggedIn;
}

void Config::setShowNotLoggedInClients(bool enabled)
{
    m_settings->setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN, enabled);
    invalidateCache();
}

int Config::notLoggedInStackMode() const
{
    refreshCache();
    return m_cachedNotLoggedInStackMode;
}

void Config::setNotLoggedInStackMode(int mode)
{
    m_settings->setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE, mode);
    invalidateCache();
}

QPoint Config::notLoggedInReferencePosition() const
{
    refreshCache();
    return m_cachedNotLoggedInReferencePosition;
}

void Config::setNotLoggedInReferencePosition(const QPoint& pos)
{
    m_settings->setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_REF_POSITION, pos);
    invalidateCache();
}

bool Config::showNotLoggedInOverlay() const
{
    refreshCache();
    return m_cachedShowNotLoggedInOverlay;
}

void Config::setShowNotLoggedInOverlay(bool show)
{
    m_settings->setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY, show);
    invalidateCache();
}

bool Config::showNonEVEOverlay() const
{
    refreshCache();
    return m_cachedShowNonEVEOverlay;
}

void Config::setShowNonEVEOverlay(bool show)
{
    m_settings->setValue(KEY_THUMBNAIL_SHOW_NON_EVE_OVERLAY, show);
    invalidateCache();
}

QStringList Config::processNames() const
{
    refreshCache();
    return m_cachedProcessNames;
}

void Config::setProcessNames(const QStringList& names)
{
    m_settings->setValue(KEY_THUMBNAIL_PROCESS_NAMES, names);
    invalidateCache();
}

void Config::addProcessName(const QString& name)
{
    QStringList names = processNames();
    if (!names.contains(name, Qt::CaseInsensitive)) {
        names.append(name);
        setProcessNames(names);
    }
}

void Config::removeProcessName(const QString& name)
{
    QStringList names = processNames();
    names.removeAll(name);
    setProcessNames(names);
}

bool Config::alwaysOnTop() const
{
    refreshCache();
    return m_cachedAlwaysOnTop;
}

void Config::setAlwaysOnTop(bool enabled)
{
    m_settings->setValue(KEY_WINDOW_ALWAYS_ON_TOP, enabled);
    invalidateCache();
}

bool Config::minimizeInactiveClients() const
{
    refreshCache();
    return m_cachedMinimizeInactive;
}

void Config::setMinimizeInactiveClients(bool enabled)
{
    m_settings->setValue(KEY_WINDOW_MINIMIZE_INACTIVE, enabled);
    invalidateCache();
}

int Config::minimizeDelay() const
{
    refreshCache();
    return m_cachedMinimizeDelay;
}

void Config::setMinimizeDelay(int delayMs)
{
    m_settings->setValue(KEY_WINDOW_MINIMIZE_DELAY, delayMs);
    invalidateCache();
}

QStringList Config::neverMinimizeCharacters() const
{
    refreshCache();
    return m_cachedNeverMinimizeCharacters;
}

void Config::setNeverMinimizeCharacters(const QStringList& characters)
{
    m_settings->setValue(KEY_WINDOW_NEVER_MINIMIZE_CHARACTERS, characters);
    invalidateCache();
}

void Config::addNeverMinimizeCharacter(const QString& characterName)
{
    QStringList characters = neverMinimizeCharacters();
    if (!characters.contains(characterName, Qt::CaseInsensitive)) {
        characters.append(characterName);
        setNeverMinimizeCharacters(characters);
    }
}

void Config::removeNeverMinimizeCharacter(const QString& characterName)
{
    QStringList characters = neverMinimizeCharacters();
    characters.removeAll(characterName);
    setNeverMinimizeCharacters(characters);
}

bool Config::isCharacterNeverMinimize(const QString& characterName) const
{
    QStringList characters = neverMinimizeCharacters();
    return characters.contains(characterName, Qt::CaseInsensitive);
}

bool Config::saveClientLocation() const
{
    refreshCache();
    return m_cachedSaveClientLocation;
}

void Config::setSaveClientLocation(bool enabled)
{
    m_settings->setValue(KEY_WINDOW_SAVE_CLIENT_LOCATION, enabled);
    invalidateCache();
}

QRect Config::getClientWindowRect(const QString& characterName) const
{
    QString key = QString("clientWindowRects/%1").arg(characterName);
    QRect rect = m_settings->value(key, QRect()).toRect();
    return rect;
}

void Config::setClientWindowRect(const QString& characterName, const QRect& rect)
{
    QString key = QString("clientWindowRects/%1").arg(characterName);
    m_settings->setValue(key, rect);
}

bool Config::rememberPositions() const
{
    refreshCache();
    return m_cachedRememberPositions;
}

void Config::setRememberPositions(bool enabled)
{
    m_settings->setValue(KEY_POSITION_REMEMBER, enabled);
    invalidateCache();
}

bool Config::preserveLogoutPositions() const
{
    refreshCache();
    return m_cachedPreserveLogoutPositions;
}

void Config::setPreserveLogoutPositions(bool enabled)
{
    m_settings->setValue(KEY_POSITION_PRESERVE_LOGOUT, enabled);
    invalidateCache();
}

QPoint Config::getThumbnailPosition(const QString& characterName) const
{
    QString key = QString("thumbnailPositions/%1").arg(characterName);
    QPoint pos = m_settings->value(key, QPoint(-1, -1)).toPoint();
    return pos;
}

void Config::setThumbnailPosition(const QString& characterName, const QPoint& pos)
{
    QString key = QString("thumbnailPositions/%1").arg(characterName);
    m_settings->setValue(key, pos);
}

QColor Config::getCharacterBorderColor(const QString& characterName) const
{
    QString key = QString("characterBorderColors/%1").arg(characterName);
    return m_settings->value(key, QColor()).value<QColor>();
}

void Config::setCharacterBorderColor(const QString& characterName, const QColor& color)
{
    QString key = QString("characterBorderColors/%1").arg(characterName);
    m_settings->setValue(key, color.name());
}

void Config::removeCharacterBorderColor(const QString& characterName)
{
    QString key = QString("characterBorderColors/%1").arg(characterName);
    m_settings->remove(key);
}

QHash<QString, QColor> Config::getAllCharacterBorderColors() const
{
    QHash<QString, QColor> colors;
    
    m_settings->beginGroup("characterBorderColors");
    
    QStringList characterNames = m_settings->childKeys();
    
    for (const QString& characterName : characterNames) {
        QColor color = m_settings->value(characterName).value<QColor>();
        if (color.isValid()) {
            colors[characterName] = color;
        }
    }
    
    m_settings->endGroup();
    
    return colors;
}

bool Config::enableSnapping() const
{
    refreshCache();
    return m_cachedEnableSnapping;
}

void Config::setEnableSnapping(bool enabled)
{
    m_settings->setValue(KEY_POSITION_ENABLE_SNAPPING, enabled);
    invalidateCache();
}

int Config::snapDistance() const
{
    refreshCache();
    return m_cachedSnapDistance;
}

void Config::setSnapDistance(int distance)
{
    m_settings->setValue(KEY_POSITION_SNAP_DISTANCE, distance);
    invalidateCache();
}

bool Config::lockThumbnailPositions() const
{
    refreshCache();
    return m_cachedLockPositions;
}

void Config::setLockThumbnailPositions(bool locked)
{
    m_settings->setValue(KEY_POSITION_LOCK, locked);
    invalidateCache();
}

bool Config::wildcardHotkeys() const
{
    refreshCache();
    return m_cachedWildcardHotkeys;
}

void Config::setWildcardHotkeys(bool enabled)
{
    m_settings->setValue(KEY_HOTKEY_WILDCARD, enabled);
    invalidateCache();
}

bool Config::hotkeysOnlyWhenEVEFocused() const
{
    refreshCache();
    return m_cachedHotkeysOnlyWhenEVEFocused;
}

void Config::setHotkeysOnlyWhenEVEFocused(bool enabled)
{
    m_settings->setValue(KEY_HOTKEY_ONLY_WHEN_EVE_FOCUSED, enabled);
    invalidateCache();
}

bool Config::isConfigDialogOpen() const
{
    return m_configDialogOpen;
}

void Config::setConfigDialogOpen(bool open)
{
    m_configDialogOpen = open;
}

bool Config::showCharacterName() const
{
    refreshCache();
    return m_cachedShowCharacterName;
}

void Config::setShowCharacterName(bool enabled)
{
    m_settings->setValue(KEY_OVERLAY_SHOW_CHARACTER, enabled);
    invalidateCache();
}

QColor Config::characterNameColor() const
{
    refreshCache();
    return m_cachedCharacterNameColor;
}

void Config::setCharacterNameColor(const QColor& color)
{
    m_settings->setValue(KEY_OVERLAY_CHARACTER_COLOR, color.name());
    invalidateCache();
}

int Config::characterNamePosition() const
{
    refreshCache();
    return m_cachedCharacterNamePosition;
}

void Config::setCharacterNamePosition(int position)
{
    m_settings->setValue(KEY_OVERLAY_CHARACTER_POSITION, position);
    invalidateCache();
}

bool Config::showSystemName() const
{
    refreshCache();
    return m_cachedShowSystemName;
}

void Config::setShowSystemName(bool enabled)
{
    m_settings->setValue(KEY_OVERLAY_SHOW_SYSTEM, enabled);
    invalidateCache();
}

QColor Config::systemNameColor() const
{
    refreshCache();
    return m_cachedSystemNameColor;
}

void Config::setSystemNameColor(const QColor& color)
{
    m_settings->setValue(KEY_OVERLAY_SYSTEM_COLOR, color.name());
    invalidateCache();
}

int Config::systemNamePosition() const
{
    refreshCache();
    return m_cachedSystemNamePosition;
}

void Config::setSystemNamePosition(int position)
{
    m_settings->setValue(KEY_OVERLAY_SYSTEM_POSITION, position);
    invalidateCache();
}

bool Config::showOverlayBackground() const
{
    refreshCache();
    return m_cachedShowOverlayBackground;
}

void Config::setShowOverlayBackground(bool enabled)
{
    m_settings->setValue(KEY_OVERLAY_SHOW_BACKGROUND, enabled);
    invalidateCache();
}

QColor Config::overlayBackgroundColor() const
{
    refreshCache();
    return m_cachedOverlayBackgroundColor;
}

void Config::setOverlayBackgroundColor(const QColor& color)
{
    m_settings->setValue(KEY_OVERLAY_BACKGROUND_COLOR, color.name());
    invalidateCache();
}

int Config::overlayBackgroundOpacity() const
{
    refreshCache();
    return m_cachedOverlayBackgroundOpacity;
}

void Config::setOverlayBackgroundOpacity(int opacity)
{
    m_settings->setValue(KEY_OVERLAY_BACKGROUND_OPACITY, opacity);
    invalidateCache();
}

QFont Config::characterNameFont() const
{
    refreshCache();
    return m_cachedCharacterNameFont;
}

void Config::setCharacterNameFont(const QFont& font)
{
    m_settings->setValue(KEY_OVERLAY_CHARACTER_FONT, font.toString());
    invalidateCache();
}

QFont Config::systemNameFont() const
{
    refreshCache();
    return m_cachedSystemNameFont;
}

void Config::setSystemNameFont(const QFont& font)
{
    m_settings->setValue(KEY_OVERLAY_SYSTEM_FONT, font.toString());
    invalidateCache();
}

QFont Config::overlayFont() const
{
    refreshCache();
    return m_cachedOverlayFont;
}

void Config::setOverlayFont(const QFont& font)
{
    m_settings->setValue(KEY_OVERLAY_FONT, font.toString());
    invalidateCache();
}

QString Config::configFilePath() const
{
    return m_settings->fileName();
}

void Config::save()
{
    m_settings->sync();
}


QString Config::getProfilesDirectory() const
{
    QString exePath = QCoreApplication::applicationDirPath();
    return exePath + "/profiles";
}

QString Config::getProfileFilePath(const QString& profileName) const
{
    return getProfilesDirectory() + "/" + profileName + ".ini";
}

QString Config::getGlobalSettingsPath() const
{
    QString exePath = QCoreApplication::applicationDirPath();
    return exePath + "/settings.global.ini";
}

void Config::ensureProfilesDirectoryExists() const
{
    QDir dir;
    QString profilesDir = getProfilesDirectory();
    if (!dir.exists(profilesDir)) {
        if (dir.mkpath(profilesDir)) {
            qDebug() << "Created profiles directory:" << profilesDir;
        } else {
            qWarning() << "Failed to create profiles directory:" << profilesDir;
        }
    }
}

void Config::loadGlobalSettings()
{
    QString globalPath = getGlobalSettingsPath();
    m_globalSettings = std::make_unique<QSettings>(globalPath, QSettings::IniFormat);
    
    m_currentProfileName = m_globalSettings->value(KEY_GLOBAL_LAST_USED_PROFILE, DEFAULT_GLOBAL_LAST_USED_PROFILE).toString();
}

void Config::saveGlobalSettings()
{
    if (m_globalSettings) {
    m_globalSettings->setValue(KEY_GLOBAL_LAST_USED_PROFILE, m_currentProfileName);
        m_globalSettings->sync();
    }
}

void Config::migrateToProfileSystem()
{
    QString exePath = QCoreApplication::applicationDirPath();
    QString oldSettingsPath = exePath + "/settings.ini";
    QString profilesDir = getProfilesDirectory();
    QString defaultProfilePath = getProfileFilePath("default");
    
    QFile oldSettingsFile(oldSettingsPath);
    QDir dir;
    
    if (oldSettingsFile.exists() && !dir.exists(profilesDir)) {
        qDebug() << "Migrating from old settings.ini to profile system...";
        
        ensureProfilesDirectoryExists();
        
        if (QFile::copy(oldSettingsPath, defaultProfilePath)) {
            qDebug() << "Successfully migrated settings to" << defaultProfilePath;
            
            QString backupPath = exePath + "/settings.ini.backup";
            if (QFile::exists(backupPath)) {
                QFile::remove(backupPath);
            }
            if (QFile::rename(oldSettingsPath, backupPath)) {
                qDebug() << "Backed up old settings to" << backupPath;
            }
            
            m_currentProfileName = "default";
        } else {
            qWarning() << "Failed to migrate settings from" << oldSettingsPath << "to" << defaultProfilePath;
        }
    }
}

void Config::initializeDefaultProfile()
{
    ensureProfilesDirectoryExists();
    
    QString defaultProfilePath = getProfileFilePath("default");
    m_settings = std::make_unique<QSettings>(defaultProfilePath, QSettings::IniFormat);
    
    m_settings->setValue(KEY_CONFIG_VERSION, CONFIG_VERSION);
    
    m_settings->setValue(KEY_UI_HIGHLIGHT_ACTIVE, DEFAULT_UI_HIGHLIGHT_ACTIVE);
    m_settings->setValue(KEY_UI_HIDE_ACTIVE_THUMBNAIL, DEFAULT_UI_HIDE_ACTIVE_THUMBNAIL);
    m_settings->setValue(KEY_UI_HIGHLIGHT_COLOR, DEFAULT_UI_HIGHLIGHT_COLOR);
    m_settings->setValue(KEY_UI_HIGHLIGHT_BORDER_WIDTH, DEFAULT_UI_HIGHLIGHT_BORDER_WIDTH);
    
    m_settings->setValue(KEY_THUMBNAIL_WIDTH, DEFAULT_THUMBNAIL_WIDTH);
    m_settings->setValue(KEY_THUMBNAIL_HEIGHT, DEFAULT_THUMBNAIL_HEIGHT);
    m_settings->setValue(KEY_THUMBNAIL_REFRESH_INTERVAL, DEFAULT_THUMBNAIL_REFRESH_INTERVAL);
    m_settings->setValue(KEY_THUMBNAIL_OPACITY, DEFAULT_THUMBNAIL_OPACITY);
    
    QStringList defaultProcessNames;
    defaultProcessNames << DEFAULT_THUMBNAIL_PROCESS_NAME;
    m_settings->setValue(KEY_THUMBNAIL_PROCESS_NAMES, defaultProcessNames);
    m_settings->setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN, DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN);
    m_settings->setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE, DEFAULT_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE);
    m_settings->setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_REF_POSITION, QPoint(DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_X, DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_Y));
    m_settings->setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY, DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY);
    m_settings->setValue(KEY_THUMBNAIL_SHOW_NON_EVE_OVERLAY, DEFAULT_THUMBNAIL_SHOW_NON_EVE_OVERLAY);
    
    m_settings->setValue(KEY_WINDOW_ALWAYS_ON_TOP, DEFAULT_WINDOW_ALWAYS_ON_TOP);
    m_settings->setValue(KEY_WINDOW_MINIMIZE_INACTIVE, DEFAULT_WINDOW_MINIMIZE_INACTIVE);
    m_settings->setValue(KEY_WINDOW_MINIMIZE_DELAY, DEFAULT_WINDOW_MINIMIZE_DELAY);
    
    m_settings->setValue(KEY_POSITION_REMEMBER, DEFAULT_POSITION_REMEMBER);
    m_settings->setValue(KEY_POSITION_PRESERVE_LOGOUT, DEFAULT_POSITION_PRESERVE_LOGOUT);
    m_settings->setValue(KEY_POSITION_ENABLE_SNAPPING, DEFAULT_POSITION_ENABLE_SNAPPING);
    m_settings->setValue(KEY_POSITION_SNAP_DISTANCE, DEFAULT_POSITION_SNAP_DISTANCE);
    
    m_settings->setValue(KEY_OVERLAY_SHOW_CHARACTER, DEFAULT_OVERLAY_SHOW_CHARACTER);
    m_settings->setValue(KEY_OVERLAY_CHARACTER_COLOR, DEFAULT_OVERLAY_CHARACTER_COLOR);
    m_settings->setValue(KEY_OVERLAY_CHARACTER_POSITION, DEFAULT_OVERLAY_CHARACTER_POSITION);
    m_settings->setValue(KEY_OVERLAY_CHARACTER_FONT, QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE).toString());
    m_settings->setValue(KEY_OVERLAY_SHOW_SYSTEM, DEFAULT_OVERLAY_SHOW_SYSTEM);
    m_settings->setValue(KEY_OVERLAY_SYSTEM_COLOR, DEFAULT_OVERLAY_SYSTEM_COLOR);
    m_settings->setValue(KEY_OVERLAY_SYSTEM_POSITION, DEFAULT_OVERLAY_SYSTEM_POSITION);
    m_settings->setValue(KEY_OVERLAY_SYSTEM_FONT, QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE).toString());
    m_settings->setValue(KEY_OVERLAY_SHOW_BACKGROUND, DEFAULT_OVERLAY_SHOW_BACKGROUND);
    m_settings->setValue(KEY_OVERLAY_BACKGROUND_COLOR, DEFAULT_OVERLAY_BACKGROUND_COLOR);
    m_settings->setValue(KEY_OVERLAY_BACKGROUND_OPACITY, DEFAULT_OVERLAY_BACKGROUND_OPACITY);
    m_settings->setValue(KEY_OVERLAY_FONT, QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE).toString());

    m_settings->setValue(KEY_COMBAT_ENABLED, DEFAULT_COMBAT_MESSAGES_ENABLED);
    m_settings->setValue(KEY_COMBAT_DURATION, DEFAULT_COMBAT_MESSAGE_DURATION);
    m_settings->setValue(KEY_COMBAT_POSITION, DEFAULT_COMBAT_MESSAGE_POSITION);
    m_settings->setValue(KEY_COMBAT_COLOR, DEFAULT_COMBAT_MESSAGE_COLOR);
    QFont defaultCombatFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE);
    defaultCombatFont.setBold(true);
    m_settings->setValue(KEY_COMBAT_FONT, defaultCombatFont.toString());
    m_settings->setValue(KEY_COMBAT_ENABLED_EVENT_TYPES, DEFAULT_COMBAT_MESSAGE_EVENT_TYPES());
    m_settings->setValue(KEY_MINING_TIMEOUT_SECONDS, DEFAULT_MINING_TIMEOUT_SECONDS);
    
    m_settings->sync();
    
    m_currentProfileName = "default";
    qDebug() << "Initialized default profile";
}

QStringList Config::listProfiles() const
{
    QStringList profiles;
    QDir profilesDir(getProfilesDirectory());
    
    if (!profilesDir.exists()) {
        return profiles;
    }
    
    QStringList filters;
    filters << "*.ini";
    QFileInfoList fileList = profilesDir.entryInfoList(filters, QDir::Files);
    
    for (const QFileInfo& fileInfo : fileList) {
        profiles.append(fileInfo.baseName());
    }
    
    profiles.sort();
    return profiles;
}

QString Config::getCurrentProfileName() const
{
    return m_currentProfileName;
}

bool Config::profileExists(const QString& profileName) const
{
    if (profileName.isEmpty()) {
        return false;
    }
    
    QString profilePath = getProfileFilePath(profileName);
    return QFile::exists(profilePath);
}

void Config::migrateLegacyCombatKeys()
{
    QPair<const char*, const char*> keys[] = {
        { "CombatMessages/Enabled", KEY_COMBAT_ENABLED },
        { "CombatMessages/Duration", KEY_COMBAT_DURATION },
        { "CombatMessages/Position", KEY_COMBAT_POSITION },
        { "CombatMessages/Color", KEY_COMBAT_COLOR },
        { "CombatMessages/Font", KEY_COMBAT_FONT },
        { "CombatMessages/EnabledEventTypes", KEY_COMBAT_ENABLED_EVENT_TYPES }
    };

    for (const auto& pair : keys) {
        const char* oldKey = pair.first;
        const char* newKey = pair.second;
        if (m_settings->contains(oldKey) && !m_settings->contains(newKey)) {
            QVariant v = m_settings->value(oldKey);
            m_settings->setValue(newKey, v);
            m_settings->remove(oldKey);
        }
    }
}

bool Config::loadProfile(const QString& profileName)
{
    if (profileName.isEmpty()) {
        qWarning() << "Cannot load profile: empty profile name";
        return false;
    }
    
    if (!profileExists(profileName)) {
        qWarning() << "Profile does not exist:" << profileName;
        return false;
    }
    
    if (m_settings) {
        m_settings->sync();
    }
    
    QString profilePath = getProfileFilePath(profileName);
    m_settings = std::make_unique<QSettings>(profilePath, QSettings::IniFormat);
    m_currentProfileName = profileName;
    
    migrateLegacyCombatKeys();

    invalidateCache();
    
    saveGlobalSettings();
    
    qDebug() << "Loaded profile:" << profileName;
    return true;
}

bool Config::createProfile(const QString& profileName, bool useDefaults)
{
    if (profileName.isEmpty()) {
        qWarning() << "Cannot create profile: empty profile name";
        return false;
    }
    
    if (profileExists(profileName)) {
        qWarning() << "Profile already exists:" << profileName;
        return false;
    }
    
    ensureProfilesDirectoryExists();
    
    QString profilePath = getProfileFilePath(profileName);
    QSettings newProfile(profilePath, QSettings::IniFormat);
    
    if (useDefaults) {
        newProfile.setValue(KEY_CONFIG_VERSION, CONFIG_VERSION);

        newProfile.setValue(KEY_UI_HIGHLIGHT_ACTIVE, DEFAULT_UI_HIGHLIGHT_ACTIVE);
        newProfile.setValue(KEY_UI_HIDE_ACTIVE_THUMBNAIL, DEFAULT_UI_HIDE_ACTIVE_THUMBNAIL);
        newProfile.setValue(KEY_UI_HIGHLIGHT_COLOR, DEFAULT_UI_HIGHLIGHT_COLOR);
        newProfile.setValue(KEY_UI_HIGHLIGHT_BORDER_WIDTH, DEFAULT_UI_HIGHLIGHT_BORDER_WIDTH);

        newProfile.setValue(KEY_THUMBNAIL_WIDTH, DEFAULT_THUMBNAIL_WIDTH);
        newProfile.setValue(KEY_THUMBNAIL_HEIGHT, DEFAULT_THUMBNAIL_HEIGHT);
        newProfile.setValue(KEY_THUMBNAIL_REFRESH_INTERVAL, DEFAULT_THUMBNAIL_REFRESH_INTERVAL);
        newProfile.setValue(KEY_THUMBNAIL_OPACITY, DEFAULT_THUMBNAIL_OPACITY);

        QStringList defaultProcessNames;
        defaultProcessNames << DEFAULT_THUMBNAIL_PROCESS_NAME;
        newProfile.setValue(KEY_THUMBNAIL_PROCESS_NAMES, defaultProcessNames);
        newProfile.setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN, DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN);
        newProfile.setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE, DEFAULT_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE);
        newProfile.setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_REF_POSITION, QPoint(DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_X, DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_Y));
        newProfile.setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY, DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY);
        newProfile.setValue(KEY_THUMBNAIL_SHOW_NON_EVE_OVERLAY, DEFAULT_THUMBNAIL_SHOW_NON_EVE_OVERLAY);

        newProfile.setValue(KEY_WINDOW_ALWAYS_ON_TOP, DEFAULT_WINDOW_ALWAYS_ON_TOP);
        newProfile.setValue(KEY_WINDOW_MINIMIZE_INACTIVE, DEFAULT_WINDOW_MINIMIZE_INACTIVE);
        newProfile.setValue(KEY_WINDOW_MINIMIZE_DELAY, DEFAULT_WINDOW_MINIMIZE_DELAY);

        newProfile.setValue(KEY_POSITION_REMEMBER, DEFAULT_POSITION_REMEMBER);
        newProfile.setValue(KEY_POSITION_PRESERVE_LOGOUT, DEFAULT_POSITION_PRESERVE_LOGOUT);
        newProfile.setValue(KEY_POSITION_ENABLE_SNAPPING, DEFAULT_POSITION_ENABLE_SNAPPING);
        newProfile.setValue(KEY_POSITION_SNAP_DISTANCE, DEFAULT_POSITION_SNAP_DISTANCE);

        newProfile.setValue(KEY_OVERLAY_SHOW_CHARACTER, DEFAULT_OVERLAY_SHOW_CHARACTER);
        newProfile.setValue(KEY_OVERLAY_CHARACTER_COLOR, DEFAULT_OVERLAY_CHARACTER_COLOR);
        newProfile.setValue(KEY_OVERLAY_CHARACTER_POSITION, DEFAULT_OVERLAY_CHARACTER_POSITION);
        newProfile.setValue(KEY_OVERLAY_CHARACTER_FONT, QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE).toString());
        newProfile.setValue(KEY_OVERLAY_SHOW_SYSTEM, DEFAULT_OVERLAY_SHOW_SYSTEM);
        newProfile.setValue(KEY_OVERLAY_SYSTEM_COLOR, DEFAULT_OVERLAY_SYSTEM_COLOR);
        newProfile.setValue(KEY_OVERLAY_SYSTEM_POSITION, DEFAULT_OVERLAY_SYSTEM_POSITION);
        newProfile.setValue(KEY_OVERLAY_SYSTEM_FONT, QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE).toString());
        newProfile.setValue(KEY_OVERLAY_SHOW_BACKGROUND, DEFAULT_OVERLAY_SHOW_BACKGROUND);
        newProfile.setValue(KEY_OVERLAY_BACKGROUND_COLOR, DEFAULT_OVERLAY_BACKGROUND_COLOR);
        newProfile.setValue(KEY_OVERLAY_BACKGROUND_OPACITY, DEFAULT_OVERLAY_BACKGROUND_OPACITY);
        newProfile.setValue(KEY_OVERLAY_FONT, QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE).toString());

        newProfile.setValue(KEY_COMBAT_ENABLED, DEFAULT_COMBAT_MESSAGES_ENABLED);
        newProfile.setValue(KEY_COMBAT_DURATION, DEFAULT_COMBAT_MESSAGE_DURATION);
        newProfile.setValue(KEY_COMBAT_POSITION, DEFAULT_COMBAT_MESSAGE_POSITION);
        newProfile.setValue(KEY_COMBAT_COLOR, DEFAULT_COMBAT_MESSAGE_COLOR);
        QFont defaultCombatFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE);
        defaultCombatFont.setBold(true);
        newProfile.setValue(KEY_COMBAT_FONT, defaultCombatFont.toString());
        newProfile.setValue(KEY_COMBAT_ENABLED_EVENT_TYPES, DEFAULT_COMBAT_MESSAGE_EVENT_TYPES());
        newProfile.setValue(KEY_MINING_TIMEOUT_SECONDS, DEFAULT_MINING_TIMEOUT_SECONDS);
    }
    
    newProfile.sync();
    
    qDebug() << "Created profile:" << profileName;
    return true;
}

bool Config::cloneProfile(const QString& sourceName, const QString& destName)
{
    if (sourceName.isEmpty() || destName.isEmpty()) {
        qWarning() << "Cannot clone profile: empty source or destination name";
        return false;
    }
    
    if (!profileExists(sourceName)) {
        qWarning() << "Source profile does not exist:" << sourceName;
        return false;
    }
    
    if (profileExists(destName)) {
        qWarning() << "Destination profile already exists:" << destName;
        return false;
    }
    
    ensureProfilesDirectoryExists();
    
    QString sourcePath = getProfileFilePath(sourceName);
    QString destPath = getProfileFilePath(destName);
    
    if (QFile::copy(sourcePath, destPath)) {
        qDebug() << "Cloned profile from" << sourceName << "to" << destName;
        return true;
    } else {
        qWarning() << "Failed to clone profile from" << sourceName << "to" << destName;
        return false;
    }
}

bool Config::deleteProfile(const QString& profileName)
{
    if (profileName.isEmpty()) {
        qWarning() << "Cannot delete profile: empty profile name";
        return false;
    }
    
    if (profileName == "default") {
        qWarning() << "Cannot delete the default profile";
        return false;
    }
    
    if (!profileExists(profileName)) {
        qWarning() << "Profile does not exist:" << profileName;
        return false;
    }
    
    if (profileName == m_currentProfileName) {
        if (!loadProfile("default")) {
            qWarning() << "Failed to switch to default profile before deletion";
            return false;
        }
    }
    
    QString profilePath = getProfileFilePath(profileName);
    if (QFile::remove(profilePath)) {
        clearProfileHotkey(profileName);
        
        qDebug() << "Deleted profile:" << profileName;
        return true;
    } else {
        qWarning() << "Failed to delete profile:" << profileName;
        return false;
    }
}

bool Config::renameProfile(const QString& oldName, const QString& newName)
{
    if (oldName.isEmpty() || newName.isEmpty()) {
        qWarning() << "Cannot rename profile: empty old or new name";
        return false;
    }
    
    if (oldName == newName) {
        return true; 
    }
    
    if (!profileExists(oldName)) {
        qWarning() << "Source profile does not exist:" << oldName;
        return false;
    }
    
    if (profileExists(newName)) {
        qWarning() << "Destination profile already exists:" << newName;
        return false;
    }
    
    QString oldPath = getProfileFilePath(oldName);
    QString newPath = getProfileFilePath(newName);
    
    if (QFile::rename(oldPath, newPath)) {
        if (oldName == m_currentProfileName) {
            m_currentProfileName = newName;
            saveGlobalSettings();
        }
        
        if (m_globalSettings) {
            QString hotkeyKey = QString("ProfileHotkeys/%1").arg(oldName);
            if (m_globalSettings->contains(hotkeyKey)) {
                QString hotkey = m_globalSettings->value(hotkeyKey).toString();
                m_globalSettings->remove(hotkeyKey);
                m_globalSettings->setValue(QString("ProfileHotkeys/%1").arg(newName), hotkey);
                m_globalSettings->sync();
            }
        }
        
        qDebug() << "Renamed profile from" << oldName << "to" << newName;
        return true;
    } else {
        qWarning() << "Failed to rename profile from" << oldName << "to" << newName;
        return false;
    }
}

QString Config::getProfileHotkey(const QString& profileName) const
{
    if (!m_globalSettings) {
        return QString();
    }
    
    QString key = QString("ProfileHotkeys/%1").arg(profileName);
    return m_globalSettings->value(key, QString()).toString();
}

void Config::setProfileHotkey(const QString& profileName, int key, int modifiers)
{
    if (!m_globalSettings) {
        return;
    }
    
    if (!profileExists(profileName)) {
        qWarning() << "Cannot set hotkey for non-existent profile:" << profileName;
        return;
    }
    
    QString hotkeyString = QString("%1,%2").arg(key).arg(modifiers);
    
    QString settingsKey = QString("ProfileHotkeys/%1").arg(profileName);
    m_globalSettings->setValue(settingsKey, hotkeyString);
    m_globalSettings->sync();
    
    qDebug() << "Set hotkey for profile" << profileName << "to VK" << key << "with modifiers" << modifiers;
}

void Config::clearProfileHotkey(const QString& profileName)
{
    if (!m_globalSettings) {
        return;
    }
    
    QString key = QString("ProfileHotkeys/%1").arg(profileName);
    m_globalSettings->remove(key);
    m_globalSettings->sync();
    
    qDebug() << "Cleared hotkey for profile" << profileName;
}

QMap<QString, QPair<int, int>> Config::getAllProfileHotkeys() const
{
    QMap<QString, QPair<int, int>> hotkeys;
    
    if (!m_globalSettings) {
        return hotkeys;
    }
    
    m_globalSettings->beginGroup("ProfileHotkeys");
    QStringList profileNames = m_globalSettings->childKeys();
    m_globalSettings->endGroup();
    
    for (const QString& profileName : profileNames) {
        QString hotkeyString = getProfileHotkey(profileName);
        if (hotkeyString.isEmpty()) {
            continue;
        }
        
        QStringList parts = hotkeyString.split(',');
        if (parts.size() == 2) {
            int key = parts[0].toInt();
            int modifiers = parts[1].toInt();
            hotkeys[profileName] = qMakePair(key, modifiers);
        }
    }
    
    return hotkeys;
}

bool Config::enableChatLogMonitoring() const
{
    refreshCache();
    return m_cachedEnableChatLogMonitoring;
}

void Config::setEnableChatLogMonitoring(bool enabled)
{
    m_settings->setValue(KEY_CHATLOG_ENABLE_MONITORING, enabled);
    invalidateCache();
}

QString Config::chatLogDirectory() const
{
    refreshCache();
    return m_cachedChatLogDirectory;
}

void Config::setChatLogDirectory(const QString& directory)
{
    m_settings->setValue(KEY_CHATLOG_DIRECTORY, directory);
    invalidateCache();
}

QString Config::getDefaultChatLogDirectory()
{
    QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString evePath = documentsPath + "/EVE/logs/Chatlogs";
    
    QDir eveDir(evePath);
    if (eveDir.exists()) {
        return evePath;
    }
    
    return evePath;
}

QString Config::getDefaultGameLogDirectory()
{
    QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString evePath = documentsPath + "/EVE/logs/Gamelogs";
    
    QDir eveDir(evePath);
    if (eveDir.exists()) {
        return evePath;
    }
    
    return evePath;
}

QString Config::gameLogDirectory() const
{
    refreshCache();
    return m_cachedGameLogDirectory;
}

void Config::setGameLogDirectory(const QString& directory)
{
    m_settings->setValue(KEY_GAMELOG_DIRECTORY, directory);
    invalidateCache();
}

bool Config::enableGameLogMonitoring() const
{
    refreshCache();
    return m_cachedEnableGameLogMonitoring;
}

void Config::setEnableGameLogMonitoring(bool enabled)
{
    m_settings->setValue(KEY_GAMELOG_ENABLE_MONITORING, enabled);
    invalidateCache();
}

bool Config::showCombatMessages() const
{
    refreshCache();
    return m_cachedShowCombatMessages;
}

void Config::setShowCombatMessages(bool enabled)
{
    m_settings->setValue(KEY_COMBAT_ENABLED, enabled);
    invalidateCache();
}

int Config::combatMessagePosition() const
{
    refreshCache();
    return m_cachedCombatMessagePosition;
}

void Config::setCombatMessagePosition(int position)
{
    m_settings->setValue(KEY_COMBAT_POSITION, position);
    invalidateCache();
}

QFont Config::combatMessageFont() const
{
    refreshCache();
    return m_cachedCombatMessageFont;
}

void Config::setCombatMessageFont(const QFont& font)
{
    m_settings->setValue(KEY_COMBAT_FONT, font);
    invalidateCache();
}

QStringList Config::enabledCombatEventTypes() const
{
    refreshCache();
    return m_cachedEnabledCombatEventTypes;
}

void Config::setEnabledCombatEventTypes(const QStringList& types)
{
    m_settings->setValue(KEY_COMBAT_ENABLED_EVENT_TYPES, types);
    invalidateCache();
}

bool Config::isCombatEventTypeEnabled(const QString& eventType) const
{
    QStringList enabled = enabledCombatEventTypes();
    return enabled.contains(eventType);
}

int Config::miningTimeoutSeconds() const
{
    refreshCache();
    return m_cachedMiningTimeoutSeconds;
}

void Config::setMiningTimeoutSeconds(int seconds)
{
    m_settings->setValue(KEY_MINING_TIMEOUT_SECONDS, seconds);
    invalidateCache();
}

QColor Config::combatEventColor(const QString& eventType) const
{
    refreshCache();
    return m_cachedCombatEventColors.value(eventType, QColor(DEFAULT_EVENT_COLORS().value(eventType, DEFAULT_COMBAT_MESSAGE_COLOR)));
}

void Config::setCombatEventColor(const QString& eventType, const QColor& color)
{
    QString key = combatEventColorKey(eventType);
    m_settings->setValue(key, color);
    invalidateCache();
}

int Config::combatEventDuration(const QString& eventType) const
{
    refreshCache();
    return m_cachedCombatEventDurations.value(eventType, DEFAULT_COMBAT_MESSAGE_DURATION);
}

void Config::setCombatEventDuration(const QString& eventType, int milliseconds)
{
    QString key = combatEventDurationKey(eventType);
    m_settings->setValue(key, milliseconds);
    invalidateCache();
}

bool Config::combatEventBorderHighlight(const QString& eventType) const
{
    refreshCache();
    return m_cachedCombatEventBorderHighlights.value(eventType, DEFAULT_COMBAT_EVENT_BORDER_HIGHLIGHT);
}

void Config::setCombatEventBorderHighlight(const QString& eventType, bool enabled)
{
    QString key = combatEventBorderHighlightKey(eventType);
    m_settings->setValue(key, enabled);
    invalidateCache();
}



