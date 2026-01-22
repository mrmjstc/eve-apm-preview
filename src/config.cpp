#include "config.h"
#include "hotkeymanager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QPoint>
#include <QRegularExpression>
#include <QStandardPaths>

Config::Config() {
  loadGlobalSettings();

  migrateToProfileSystem();

  QString profileToLoad =
      m_currentProfileName.isEmpty() ? "default" : m_currentProfileName;

  if (!profileExists(profileToLoad)) {
    qDebug() << "Profile" << profileToLoad
             << "does not exist. Creating default profile.";
    initializeDefaultProfile();
    profileToLoad = "default";
  }

  QString profilePath = getProfileFilePath(profileToLoad);
  m_settings = std::make_unique<QSettings>(profilePath, QSettings::IniFormat);
  m_currentProfileName = profileToLoad;

  if (!m_settings->contains(KEY_CONFIG_VERSION)) {
    initializeDefaultProfile();
  }

  loadCacheFromSettings();

  saveGlobalSettings();
}

Config::~Config() { save(); }

/// Helper function to expand environment variables in paths
/// Supports both Windows (%VAR%) and Unix ($VAR or ${VAR}) formats
static QString expandEnvironmentVariables(const QString &path) {
  QString result = path;

#ifdef Q_OS_WIN
  // Windows format: %USERPROFILE%, %APPDATA%, etc.
  QRegularExpression winEnvVar(R"(%([^%]+)%)");
  QRegularExpressionMatchIterator it = winEnvVar.globalMatch(path);
  while (it.hasNext()) {
    QRegularExpressionMatch match = it.next();
    QString varName = match.captured(1);
    QString varValue = qEnvironmentVariable(varName.toUtf8().constData());
    if (!varValue.isEmpty()) {
      result.replace("%" + varName + "%", varValue);
    }
  }
#endif

  // Unix format: $VAR or ${VAR}
  QRegularExpression unixEnvVar(R"(\$\{?([A-Za-z_][A-Za-z0-9_]*)\}?)");
  QRegularExpressionMatchIterator it2 = unixEnvVar.globalMatch(result);
  while (it2.hasNext()) {
    QRegularExpressionMatch match = it2.next();
    QString varName = match.captured(1);
    QString varValue = qEnvironmentVariable(varName.toUtf8().constData());
    if (!varValue.isEmpty()) {
      result.replace(match.captured(0), varValue);
    }
  }

  return result;
}

Config &Config::instance() {
  static Config instance;
  return instance;
}

void Config::loadCacheFromSettings() {
  qDebug() << "Config::loadCacheFromSettings() - START";

  m_cachedHighlightActive =
      m_settings->value(KEY_UI_HIGHLIGHT_ACTIVE, DEFAULT_UI_HIGHLIGHT_ACTIVE)
          .toBool();
  m_cachedHideActiveThumbnail = m_settings
                                    ->value(KEY_UI_HIDE_ACTIVE_THUMBNAIL,
                                            DEFAULT_UI_HIDE_ACTIVE_THUMBNAIL)
                                    .toBool();
  m_cachedHideThumbnailsWhenEVENotFocused =
      m_settings
          ->value(KEY_UI_HIDE_THUMBNAILS_WHEN_EVE_NOT_FOCUSED,
                  DEFAULT_UI_HIDE_THUMBNAILS_WHEN_EVE_NOT_FOCUSED)
          .toBool();
  m_cachedHighlightColor = QColor(
      m_settings->value(KEY_UI_HIGHLIGHT_COLOR, DEFAULT_UI_HIGHLIGHT_COLOR)
          .toString());
  m_cachedHighlightBorderWidth = m_settings
                                     ->value(KEY_UI_HIGHLIGHT_BORDER_WIDTH,
                                             DEFAULT_UI_HIGHLIGHT_BORDER_WIDTH)
                                     .toInt();
  m_cachedActiveBorderStyle = static_cast<BorderStyle>(
      m_settings->value(KEY_UI_ACTIVE_BORDER_STYLE, DEFAULT_ACTIVE_BORDER_STYLE)
          .toInt());

  m_cachedShowInactiveBorders = m_settings
                                    ->value(KEY_UI_SHOW_INACTIVE_BORDERS,
                                            DEFAULT_UI_SHOW_INACTIVE_BORDERS)
                                    .toBool();
  m_cachedInactiveBorderColor =
      QColor(m_settings
                 ->value(KEY_UI_INACTIVE_BORDER_COLOR,
                         DEFAULT_UI_INACTIVE_BORDER_COLOR)
                 .toString());
  m_cachedInactiveBorderWidth = m_settings
                                    ->value(KEY_UI_INACTIVE_BORDER_WIDTH,
                                            DEFAULT_UI_INACTIVE_BORDER_WIDTH)
                                    .toInt();
  m_cachedInactiveBorderStyle = static_cast<BorderStyle>(
      m_settings
          ->value(KEY_UI_INACTIVE_BORDER_STYLE, DEFAULT_INACTIVE_BORDER_STYLE)
          .toInt());

  m_cachedThumbnailWidth =
      m_settings->value(KEY_THUMBNAIL_WIDTH, DEFAULT_THUMBNAIL_WIDTH).toInt();
  m_cachedThumbnailHeight =
      m_settings->value(KEY_THUMBNAIL_HEIGHT, DEFAULT_THUMBNAIL_HEIGHT).toInt();
  m_cachedThumbnailOpacity =
      qBound(OPACITY_MIN,
             m_settings->value(KEY_THUMBNAIL_OPACITY, DEFAULT_THUMBNAIL_OPACITY)
                 .toInt(),
             OPACITY_MAX);

  m_cachedShowNotLoggedIn = m_settings
                                ->value(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN,
                                        DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN)
                                .toBool();
  m_cachedNotLoggedInStackMode =
      m_settings
          ->value(KEY_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE,
                  DEFAULT_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE)
          .toInt();
  m_cachedNotLoggedInReferencePosition =
      m_settings
          ->value(KEY_THUMBNAIL_NOT_LOGGED_IN_REF_POSITION,
                  QPoint(DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_X,
                         DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_Y))
          .toPoint();
  m_cachedShowNotLoggedInOverlay =
      m_settings
          ->value(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY,
                  DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY)
          .toBool();
  m_cachedShowNonEVEOverlay =
      m_settings
          ->value(KEY_THUMBNAIL_SHOW_NON_EVE_OVERLAY,
                  DEFAULT_THUMBNAIL_SHOW_NON_EVE_OVERLAY)
          .toBool();

  QStringList defaultProcessNames;
  defaultProcessNames << DEFAULT_THUMBNAIL_PROCESS_NAME;
  m_cachedProcessNames =
      m_settings->value(KEY_THUMBNAIL_PROCESS_NAMES, defaultProcessNames)
          .toStringList();

  m_cachedAlwaysOnTop =
      m_settings->value(KEY_WINDOW_ALWAYS_ON_TOP, DEFAULT_WINDOW_ALWAYS_ON_TOP)
          .toBool();
  m_cachedSwitchOnMouseDown = m_settings
                                  ->value(KEY_WINDOW_SWITCH_ON_MOUSE_DOWN,
                                          DEFAULT_WINDOW_SWITCH_ON_MOUSE_DOWN)
                                  .toBool();
  m_cachedMinimizeInactive = m_settings
                                 ->value(KEY_WINDOW_MINIMIZE_INACTIVE,
                                         DEFAULT_WINDOW_MINIMIZE_INACTIVE)
                                 .toBool();
  m_cachedMinimizeDelay =
      m_settings
          ->value(KEY_WINDOW_MINIMIZE_DELAY, DEFAULT_WINDOW_MINIMIZE_DELAY)
          .toInt();
  m_cachedNeverMinimizeCharacters =
      m_settings->value(KEY_WINDOW_NEVER_MINIMIZE_CHARACTERS, QStringList())
          .toStringList();
  m_cachedHiddenCharacters =
      m_settings->value(KEY_THUMBNAIL_HIDDEN_CHARACTERS, QStringList())
          .toStringList();
  m_cachedSaveClientLocation = m_settings
                                   ->value(KEY_WINDOW_SAVE_CLIENT_LOCATION,
                                           DEFAULT_WINDOW_SAVE_CLIENT_LOCATION)
                                   .toBool();

  m_cachedRememberPositions =
      m_settings->value(KEY_POSITION_REMEMBER, DEFAULT_POSITION_REMEMBER)
          .toBool();
  m_cachedPreserveLogoutPositions =
      m_settings
          ->value(KEY_POSITION_PRESERVE_LOGOUT,
                  DEFAULT_POSITION_PRESERVE_LOGOUT)
          .toBool();
  m_cachedEnableSnapping = m_settings
                               ->value(KEY_POSITION_ENABLE_SNAPPING,
                                       DEFAULT_POSITION_ENABLE_SNAPPING)
                               .toBool();
  m_cachedSnapDistance =
      m_settings
          ->value(KEY_POSITION_SNAP_DISTANCE, DEFAULT_POSITION_SNAP_DISTANCE)
          .toInt();
  m_cachedLockPositions =
      m_settings->value(KEY_POSITION_LOCK, DEFAULT_POSITION_LOCK).toBool();

  m_cachedWildcardHotkeys =
      m_settings->value(KEY_HOTKEY_WILDCARD, DEFAULT_HOTKEY_WILDCARD).toBool();
  m_cachedHotkeysOnlyWhenEVEFocused =
      m_settings
          ->value(KEY_HOTKEY_ONLY_WHEN_EVE_FOCUSED,
                  DEFAULT_HOTKEY_ONLY_WHEN_EVE_FOCUSED)
          .toBool();

  m_cachedShowCharacterName =
      m_settings
          ->value(KEY_OVERLAY_SHOW_CHARACTER, DEFAULT_OVERLAY_SHOW_CHARACTER)
          .toBool();
  m_cachedCharacterNameColor = QColor(
      m_settings
          ->value(KEY_OVERLAY_CHARACTER_COLOR, DEFAULT_OVERLAY_CHARACTER_COLOR)
          .toString());
  m_cachedCharacterNamePosition =
      m_settings
          ->value(KEY_OVERLAY_CHARACTER_POSITION,
                  DEFAULT_OVERLAY_CHARACTER_POSITION)
          .toInt();
  QFont defaultCharFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE);
  m_cachedCharacterNameFont.fromString(
      m_settings->value(KEY_OVERLAY_CHARACTER_FONT, defaultCharFont.toString())
          .toString());
  m_cachedCharacterNameOffsetX =
      m_settings
          ->value(KEY_OVERLAY_CHARACTER_OFFSET_X, DEFAULT_OVERLAY_OFFSET_X)
          .toInt();
  m_cachedCharacterNameOffsetY =
      m_settings
          ->value(KEY_OVERLAY_CHARACTER_OFFSET_Y, DEFAULT_OVERLAY_OFFSET_Y)
          .toInt();

  m_cachedShowSystemName =
      m_settings->value(KEY_OVERLAY_SHOW_SYSTEM, DEFAULT_OVERLAY_SHOW_SYSTEM)
          .toBool();
  m_cachedUniqueSystemNameColors =
      m_settings
          ->value(KEY_OVERLAY_UNIQUE_SYSTEM_COLORS,
                  DEFAULT_OVERLAY_UNIQUE_SYSTEM_COLORS)
          .toBool();
  m_cachedSystemNameColor = QColor(
      m_settings->value(KEY_OVERLAY_SYSTEM_COLOR, DEFAULT_OVERLAY_SYSTEM_COLOR)
          .toString());
  m_cachedSystemNamePosition =
      m_settings
          ->value(KEY_OVERLAY_SYSTEM_POSITION, DEFAULT_OVERLAY_SYSTEM_POSITION)
          .toInt();
  QFont defaultSysFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE);
  m_cachedSystemNameFont.fromString(
      m_settings->value(KEY_OVERLAY_SYSTEM_FONT, defaultSysFont.toString())
          .toString());
  m_cachedSystemNameOffsetX =
      m_settings->value(KEY_OVERLAY_SYSTEM_OFFSET_X, DEFAULT_OVERLAY_OFFSET_X)
          .toInt();
  m_cachedSystemNameOffsetY =
      m_settings->value(KEY_OVERLAY_SYSTEM_OFFSET_Y, DEFAULT_OVERLAY_OFFSET_Y)
          .toInt();

  m_cachedShowOverlayBackground =
      m_settings
          ->value(KEY_OVERLAY_SHOW_BACKGROUND, DEFAULT_OVERLAY_SHOW_BACKGROUND)
          .toBool();
  m_cachedOverlayBackgroundColor =
      QColor(m_settings
                 ->value(KEY_OVERLAY_BACKGROUND_COLOR,
                         DEFAULT_OVERLAY_BACKGROUND_COLOR)
                 .toString());
  m_cachedOverlayBackgroundOpacity =
      qBound(OPACITY_MIN,
             m_settings
                 ->value(KEY_OVERLAY_BACKGROUND_OPACITY,
                         DEFAULT_OVERLAY_BACKGROUND_OPACITY)
                 .toInt(),
             OPACITY_MAX);

  QFont defaultFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE);
  m_cachedOverlayFont.fromString(
      m_settings->value(KEY_OVERLAY_FONT, defaultFont.toString()).toString());

  m_cachedEnableChatLogMonitoring =
      m_settings
          ->value(KEY_CHATLOG_ENABLE_MONITORING,
                  DEFAULT_CHATLOG_ENABLE_MONITORING)
          .toBool();
  m_cachedChatLogDirectory =
      m_settings->value(KEY_CHATLOG_DIRECTORY, getDefaultChatLogDirectory())
          .toString();
  m_cachedEnableGameLogMonitoring =
      m_settings
          ->value(KEY_GAMELOG_ENABLE_MONITORING,
                  DEFAULT_GAMELOG_ENABLE_MONITORING)
          .toBool();
  qDebug() << "Config::loadCacheFromSettings() - Loaded "
              "enableGameLogMonitoring from disk:"
           << m_cachedEnableGameLogMonitoring;
  m_cachedGameLogDirectory =
      m_settings->value(KEY_GAMELOG_DIRECTORY, getDefaultGameLogDirectory())
          .toString();

  m_cachedShowCombatMessages =
      m_settings->value(KEY_COMBAT_ENABLED, DEFAULT_COMBAT_MESSAGES_ENABLED)
          .toBool();
  m_cachedCombatMessagePosition =
      m_settings->value(KEY_COMBAT_POSITION, DEFAULT_COMBAT_MESSAGE_POSITION)
          .toInt();
  QFont defaultCombatFont(DEFAULT_OVERLAY_FONT_FAMILY,
                          DEFAULT_OVERLAY_FONT_SIZE);
  defaultCombatFont.setBold(true);
  m_cachedCombatMessageFont =
      m_settings->value(KEY_COMBAT_FONT, defaultCombatFont).value<QFont>();
  m_cachedCombatMessageOffsetX =
      m_settings->value(KEY_COMBAT_OFFSET_X, DEFAULT_OVERLAY_OFFSET_X).toInt();
  m_cachedCombatMessageOffsetY =
      m_settings->value(KEY_COMBAT_OFFSET_Y, DEFAULT_OVERLAY_OFFSET_Y).toInt();
  m_cachedSuppressCombatWhenFocused =
      m_settings
          ->value(KEY_COMBAT_SUPPRESS_FOCUSED, DEFAULT_COMBAT_SUPPRESS_FOCUSED)
          .toBool();

  m_cachedCombatEventColors.clear();
  m_cachedCombatEventDurations.clear();
  m_cachedCombatEventBorderHighlights.clear();
  m_cachedCombatBorderStyles.clear();
  QStringList eventTypes = DEFAULT_COMBAT_MESSAGE_EVENT_TYPES();
  for (const QString &eventType : eventTypes) {
    QString colorKey = combatEventColorKey(eventType);
    QString defaultColor =
        DEFAULT_EVENT_COLORS().value(eventType, DEFAULT_COMBAT_MESSAGE_COLOR);
    m_cachedCombatEventColors[eventType] =
        m_settings->value(colorKey, QColor(defaultColor)).value<QColor>();

    QString durationKey = combatEventDurationKey(eventType);
    m_cachedCombatEventDurations[eventType] =
        m_settings->value(durationKey, DEFAULT_COMBAT_MESSAGE_DURATION).toInt();

    QString borderKey = combatEventBorderHighlightKey(eventType);
    m_cachedCombatEventBorderHighlights[eventType] =
        m_settings->value(borderKey, DEFAULT_COMBAT_EVENT_BORDER_HIGHLIGHT)
            .toBool();

    QString styleKey = combatBorderStyleKey(eventType);
    m_cachedCombatBorderStyles[eventType] = static_cast<BorderStyle>(
        m_settings->value(styleKey, DEFAULT_COMBAT_BORDER_STYLE).toInt());

    QString soundEnabledKey = combatEventSoundEnabledKey(eventType);
    m_cachedCombatEventSoundsEnabled[eventType] =
        m_settings->value(soundEnabledKey, DEFAULT_COMBAT_SOUND_ENABLED)
            .toBool();

    QString soundFileKey = combatEventSoundFileKey(eventType);
    m_cachedCombatEventSoundFiles[eventType] =
        m_settings->value(soundFileKey, QString()).toString();

    QString soundVolumeKey = combatEventSoundVolumeKey(eventType);
    m_cachedCombatEventSoundVolumes[eventType] =
        m_settings->value(soundVolumeKey, DEFAULT_COMBAT_SOUND_VOLUME).toInt();
  }

  m_cachedEnabledCombatEventTypes =
      m_settings
          ->value(KEY_COMBAT_ENABLED_EVENT_TYPES,
                  DEFAULT_COMBAT_MESSAGE_EVENT_TYPES())
          .toStringList();
  m_cachedMiningTimeoutSeconds =
      m_settings
          ->value(KEY_MINING_TIMEOUT_SECONDS, DEFAULT_MINING_TIMEOUT_SECONDS)
          .toInt();

  m_cachedCharacterBorderColors.clear();
  m_settings->beginGroup("characterBorderColors");
  QStringList characterNames = m_settings->childKeys();
  for (const QString &characterName : characterNames) {
    QColor color = m_settings->value(characterName).value<QColor>();
    if (color.isValid()) {
      m_cachedCharacterBorderColors[characterName] = color;
    }
  }
  m_settings->endGroup();

  m_cachedCharacterInactiveBorderColors.clear();
  m_settings->beginGroup("characterInactiveBorderColors");
  QStringList inactiveCharacterNames = m_settings->childKeys();
  for (const QString &characterName : inactiveCharacterNames) {
    QColor color = m_settings->value(characterName).value<QColor>();
    if (color.isValid()) {
      m_cachedCharacterInactiveBorderColors[characterName] = color;
    }
  }
  m_settings->endGroup();

  m_cachedSystemNameColors.clear();
  m_settings->beginGroup("systemNameColors");
  QStringList systemNames = m_settings->childKeys();
  for (const QString &systemName : systemNames) {
    QColor color = m_settings->value(systemName).value<QColor>();
    if (color.isValid()) {
      m_cachedSystemNameColors[systemName] = color;
    }
  }
  m_settings->endGroup();

  m_cachedThumbnailPositions.clear();
  m_settings->beginGroup("thumbnailPositions");
  QStringList thumbnailCharNames = m_settings->childKeys();
  for (const QString &characterName : thumbnailCharNames) {
    QPoint pos = m_settings->value(characterName).toPoint();
    m_cachedThumbnailPositions[characterName] = pos;
  }
  m_settings->endGroup();

  m_cachedThumbnailSizes.clear();
  m_settings->beginGroup("thumbnailSizes");
  QStringList sizeCharNames = m_settings->childKeys();
  for (const QString &characterName : sizeCharNames) {
    QSize size = m_settings->value(characterName).toSize();
    if (size.isValid()) {
      m_cachedThumbnailSizes[characterName] = size;
    }
  }
  m_settings->endGroup();

  m_cachedProcessThumbnailSizes.clear();
  m_settings->beginGroup("processThumbnailSizes");
  QStringList processNames = m_settings->childKeys();
  for (const QString &processName : processNames) {
    QSize size = m_settings->value(processName).toSize();
    if (size.isValid()) {
      m_cachedProcessThumbnailSizes[processName] = size;
    }
  }
  m_settings->endGroup();

  m_cachedCustomThumbnailNames.clear();
  m_settings->beginGroup("thumbnailCustomNames");
  QStringList customNameCharNames = m_settings->childKeys();
  for (const QString &characterName : customNameCharNames) {
    QString customName = m_settings->value(characterName).toString();
    if (!customName.isEmpty()) {
      m_cachedCustomThumbnailNames[characterName] = customName;
    }
  }
  m_settings->endGroup();

  m_cachedClientWindowRects.clear();
  m_settings->beginGroup("clientWindowRects");
  QStringList clientCharNames = m_settings->childKeys();
  for (const QString &characterName : clientCharNames) {
    QRect rect = m_settings->value(characterName).toRect();
    qDebug() << "Loading client window rect for" << characterName << ":" << rect
             << "isValid:" << rect.isValid() << "isEmpty:" << rect.isEmpty();
    if (rect.isValid()) {
      m_cachedClientWindowRects[characterName] = rect;
      qDebug() << "  -> Cached successfully";
    } else {
      qDebug() << "  -> Rejected (invalid)";
    }
  }
  m_settings->endGroup();
}

bool Config::highlightActiveWindow() const { return m_cachedHighlightActive; }

void Config::setHighlightActiveWindow(bool enabled) {
  m_settings->setValue(KEY_UI_HIGHLIGHT_ACTIVE, enabled);
  m_cachedHighlightActive = enabled;
}

bool Config::hideActiveClientThumbnail() const {
  return m_cachedHideActiveThumbnail;
}

void Config::setHideActiveClientThumbnail(bool enabled) {
  m_settings->setValue(KEY_UI_HIDE_ACTIVE_THUMBNAIL, enabled);
  m_cachedHideActiveThumbnail = enabled;
}

bool Config::hideThumbnailsWhenEVENotFocused() const {
  return m_cachedHideThumbnailsWhenEVENotFocused;
}

void Config::setHideThumbnailsWhenEVENotFocused(bool enabled) {
  m_settings->setValue(KEY_UI_HIDE_THUMBNAILS_WHEN_EVE_NOT_FOCUSED, enabled);
  m_cachedHideThumbnailsWhenEVENotFocused = enabled;
}

QColor Config::highlightColor() const { return m_cachedHighlightColor; }

void Config::setHighlightColor(const QColor &color) {
  m_settings->setValue(KEY_UI_HIGHLIGHT_COLOR, color.name());
  m_cachedHighlightColor = color;
}

int Config::highlightBorderWidth() const {
  return m_cachedHighlightBorderWidth;
}

void Config::setHighlightBorderWidth(int width) {
  m_settings->setValue(KEY_UI_HIGHLIGHT_BORDER_WIDTH, width);
  m_cachedHighlightBorderWidth = width;
}

BorderStyle Config::activeBorderStyle() const {
  return m_cachedActiveBorderStyle;
}

void Config::setActiveBorderStyle(BorderStyle style) {
  m_settings->setValue(KEY_UI_ACTIVE_BORDER_STYLE, static_cast<int>(style));
  m_cachedActiveBorderStyle = style;
}

bool Config::showInactiveBorders() const { return m_cachedShowInactiveBorders; }

void Config::setShowInactiveBorders(bool enabled) {
  m_settings->setValue(KEY_UI_SHOW_INACTIVE_BORDERS, enabled);
  m_cachedShowInactiveBorders = enabled;
}

QColor Config::inactiveBorderColor() const {
  return m_cachedInactiveBorderColor;
}

void Config::setInactiveBorderColor(const QColor &color) {
  m_settings->setValue(KEY_UI_INACTIVE_BORDER_COLOR, color.name());
  m_cachedInactiveBorderColor = color;
}

int Config::inactiveBorderWidth() const { return m_cachedInactiveBorderWidth; }

void Config::setInactiveBorderWidth(int width) {
  m_settings->setValue(KEY_UI_INACTIVE_BORDER_WIDTH, width);
  m_cachedInactiveBorderWidth = width;
}

BorderStyle Config::inactiveBorderStyle() const {
  return m_cachedInactiveBorderStyle;
}

void Config::setInactiveBorderStyle(BorderStyle style) {
  m_settings->setValue(KEY_UI_INACTIVE_BORDER_STYLE, static_cast<int>(style));
  m_cachedInactiveBorderStyle = style;
}

int Config::thumbnailWidth() const { return m_cachedThumbnailWidth; }

void Config::setThumbnailWidth(int width) {
  m_settings->setValue(KEY_THUMBNAIL_WIDTH, width);
  m_cachedThumbnailWidth = width;
}

int Config::thumbnailHeight() const { return m_cachedThumbnailHeight; }

void Config::setThumbnailHeight(int height) {
  m_settings->setValue(KEY_THUMBNAIL_HEIGHT, height);
  m_cachedThumbnailHeight = height;
}

int Config::thumbnailOpacity() const { return m_cachedThumbnailOpacity; }

void Config::setThumbnailOpacity(int opacity) {
  int boundedOpacity = qBound(OPACITY_MIN, opacity, OPACITY_MAX);
  m_settings->setValue(KEY_THUMBNAIL_OPACITY, boundedOpacity);
  m_cachedThumbnailOpacity = boundedOpacity;
}

bool Config::showNotLoggedInClients() const { return m_cachedShowNotLoggedIn; }

void Config::setShowNotLoggedInClients(bool enabled) {
  m_settings->setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN, enabled);
  m_cachedShowNotLoggedIn = enabled;
}

int Config::notLoggedInStackMode() const {
  return m_cachedNotLoggedInStackMode;
}

void Config::setNotLoggedInStackMode(int mode) {
  m_settings->setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE, mode);
  m_cachedNotLoggedInStackMode = mode;
}

QPoint Config::notLoggedInReferencePosition() const {
  return m_cachedNotLoggedInReferencePosition;
}

void Config::setNotLoggedInReferencePosition(const QPoint &pos) {
  m_settings->setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_REF_POSITION, pos);
  m_cachedNotLoggedInReferencePosition = pos;
}

bool Config::showNotLoggedInOverlay() const {
  return m_cachedShowNotLoggedInOverlay;
}

void Config::setShowNotLoggedInOverlay(bool show) {
  m_settings->setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY, show);
  m_cachedShowNotLoggedInOverlay = show;
}

bool Config::showNonEVEOverlay() const { return m_cachedShowNonEVEOverlay; }

void Config::setShowNonEVEOverlay(bool show) {
  m_settings->setValue(KEY_THUMBNAIL_SHOW_NON_EVE_OVERLAY, show);
  m_cachedShowNonEVEOverlay = show;
}

QStringList Config::processNames() const { return m_cachedProcessNames; }

void Config::setProcessNames(const QStringList &names) {
  m_settings->setValue(KEY_THUMBNAIL_PROCESS_NAMES, names);
  m_cachedProcessNames = names;
}

void Config::addProcessName(const QString &name) {
  QStringList names = processNames();
  if (!names.contains(name, Qt::CaseInsensitive)) {
    names.append(name);
    setProcessNames(names);
  }
}

void Config::removeProcessName(const QString &name) {
  QStringList names = processNames();
  names.removeAll(name);
  setProcessNames(names);
}

bool Config::alwaysOnTop() const { return m_cachedAlwaysOnTop; }

void Config::setAlwaysOnTop(bool enabled) {
  m_settings->setValue(KEY_WINDOW_ALWAYS_ON_TOP, enabled);
  m_cachedAlwaysOnTop = enabled;
}

bool Config::switchOnMouseDown() const { return m_cachedSwitchOnMouseDown; }

void Config::setSwitchOnMouseDown(bool enabled) {
  m_settings->setValue(KEY_WINDOW_SWITCH_ON_MOUSE_DOWN, enabled);
  m_cachedSwitchOnMouseDown = enabled;
}

bool Config::useDragWithRightClick() const {
  return m_cachedDragWithRightClick;
}

void Config::setUseDragWithRightClick(bool enabled) {
  m_settings->setValue(KEY_WINDOW_DRAG_WITH_RIGHT_CLICK, enabled);
  m_cachedDragWithRightClick = enabled;
}

bool Config::minimizeInactiveClients() const {
  return m_cachedMinimizeInactive;
}

void Config::setMinimizeInactiveClients(bool enabled) {
  m_settings->setValue(KEY_WINDOW_MINIMIZE_INACTIVE, enabled);
  m_cachedMinimizeInactive = enabled;
}

int Config::minimizeDelay() const { return m_cachedMinimizeDelay; }

void Config::setMinimizeDelay(int delayMs) {
  m_settings->setValue(KEY_WINDOW_MINIMIZE_DELAY, delayMs);
  m_cachedMinimizeDelay = delayMs;
}

QStringList Config::neverMinimizeCharacters() const {
  return m_cachedNeverMinimizeCharacters;
}

void Config::setNeverMinimizeCharacters(const QStringList &characters) {
  m_settings->setValue(KEY_WINDOW_NEVER_MINIMIZE_CHARACTERS, characters);
  m_cachedNeverMinimizeCharacters = characters;
}

void Config::addNeverMinimizeCharacter(const QString &characterName) {
  QStringList characters = neverMinimizeCharacters();
  if (!characters.contains(characterName, Qt::CaseInsensitive)) {
    characters.append(characterName);
    setNeverMinimizeCharacters(characters);
  }
}

void Config::removeNeverMinimizeCharacter(const QString &characterName) {
  QStringList characters = neverMinimizeCharacters();
  characters.removeAll(characterName);
  setNeverMinimizeCharacters(characters);
}

bool Config::isCharacterNeverMinimize(const QString &characterName) const {
  QStringList characters = neverMinimizeCharacters();
  return characters.contains(characterName, Qt::CaseInsensitive);
}

QStringList Config::hiddenCharacters() const {
  return m_cachedHiddenCharacters;
}

void Config::setHiddenCharacters(const QStringList &characters) {
  m_settings->setValue(KEY_THUMBNAIL_HIDDEN_CHARACTERS, characters);
  m_cachedHiddenCharacters = characters;
}

void Config::addHiddenCharacter(const QString &characterName) {
  QStringList characters = hiddenCharacters();
  if (!characters.contains(characterName, Qt::CaseInsensitive)) {
    characters.append(characterName);
    setHiddenCharacters(characters);
  }
}

void Config::removeHiddenCharacter(const QString &characterName) {
  QStringList characters = hiddenCharacters();
  characters.removeAll(characterName);
  setHiddenCharacters(characters);
}

bool Config::isCharacterHidden(const QString &characterName) const {
  QStringList characters = hiddenCharacters();
  return characters.contains(characterName, Qt::CaseInsensitive);
}

bool Config::saveClientLocation() const { return m_cachedSaveClientLocation; }

void Config::setSaveClientLocation(bool enabled) {
  m_settings->setValue(KEY_WINDOW_SAVE_CLIENT_LOCATION, enabled);
  m_cachedSaveClientLocation = enabled;
}

QRect Config::getClientWindowRect(const QString &characterName) const {
  return m_cachedClientWindowRects.value(characterName, QRect());
}

void Config::setClientWindowRect(const QString &characterName,
                                 const QRect &rect) {
  QString key = QString("clientWindowRects/%1").arg(characterName);
  qDebug() << "Saving client window rect for" << characterName << ":" << rect
           << "isValid:" << rect.isValid() << "isEmpty:" << rect.isEmpty();
  m_settings->setValue(key, rect);
  m_cachedClientWindowRects[characterName] = rect;
}

bool Config::rememberPositions() const { return m_cachedRememberPositions; }

void Config::setRememberPositions(bool enabled) {
  m_settings->setValue(KEY_POSITION_REMEMBER, enabled);
  m_cachedRememberPositions = enabled;
}

bool Config::preserveLogoutPositions() const {
  return m_cachedPreserveLogoutPositions;
}

void Config::setPreserveLogoutPositions(bool enabled) {
  m_settings->setValue(KEY_POSITION_PRESERVE_LOGOUT, enabled);
  m_cachedPreserveLogoutPositions = enabled;
}

QPoint Config::getThumbnailPosition(const QString &characterName) const {
  return m_cachedThumbnailPositions.value(characterName, QPoint(-1, -1));
}

void Config::setThumbnailPosition(const QString &characterName,
                                  const QPoint &pos) {
  QString key = QString("thumbnailPositions/%1").arg(characterName);
  m_settings->setValue(key, pos);
  m_cachedThumbnailPositions[characterName] = pos;
}

QColor Config::getCharacterBorderColor(const QString &characterName) const {
  return m_cachedCharacterBorderColors.value(characterName, QColor());
}

void Config::setCharacterBorderColor(const QString &characterName,
                                     const QColor &color) {
  QString key = QString("characterBorderColors/%1").arg(characterName);
  m_settings->setValue(key, color.name());
  m_cachedCharacterBorderColors[characterName] = color;
}

void Config::removeCharacterBorderColor(const QString &characterName) {
  QString key = QString("characterBorderColors/%1").arg(characterName);
  m_settings->remove(key);
  m_cachedCharacterBorderColors.remove(characterName);
}

QHash<QString, QColor> Config::getAllCharacterBorderColors() const {
  return m_cachedCharacterBorderColors;
}

QColor
Config::getCharacterInactiveBorderColor(const QString &characterName) const {
  return m_cachedCharacterInactiveBorderColors.value(characterName, QColor());
}

void Config::setCharacterInactiveBorderColor(const QString &characterName,
                                             const QColor &color) {
  QString key = QString("characterInactiveBorderColors/%1").arg(characterName);
  m_settings->setValue(key, color.name());
  m_cachedCharacterInactiveBorderColors[characterName] = color;
}

void Config::removeCharacterInactiveBorderColor(const QString &characterName) {
  QString key = QString("characterInactiveBorderColors/%1").arg(characterName);
  m_settings->remove(key);
  m_cachedCharacterInactiveBorderColors.remove(characterName);
}

QHash<QString, QColor> Config::getAllCharacterInactiveBorderColors() const {
  return m_cachedCharacterInactiveBorderColors;
}

QSize Config::getThumbnailSize(const QString &characterName) const {
  return m_cachedThumbnailSizes.value(characterName, QSize(-1, -1));
}

void Config::setThumbnailSize(const QString &characterName, const QSize &size) {
  QString key = QString("thumbnailSizes/%1").arg(characterName);
  m_settings->setValue(key, size);
  m_cachedThumbnailSizes[characterName] = size;
}

void Config::removeThumbnailSize(const QString &characterName) {
  QString key = QString("thumbnailSizes/%1").arg(characterName);
  m_settings->remove(key);
  m_cachedThumbnailSizes.remove(characterName);
}

bool Config::hasCustomThumbnailSize(const QString &characterName) const {
  QSize size = m_cachedThumbnailSizes.value(characterName, QSize(-1, -1));
  return size.isValid() && size.width() > 0 && size.height() > 0;
}

QHash<QString, QSize> Config::getAllCustomThumbnailSizes() const {
  return m_cachedThumbnailSizes;
}

QSize Config::getProcessThumbnailSize(const QString &processName) const {
  return m_cachedProcessThumbnailSizes.value(processName, QSize(-1, -1));
}

void Config::setProcessThumbnailSize(const QString &processName,
                                     const QSize &size) {
  QString key = QString("processThumbnailSizes/%1").arg(processName);
  m_settings->setValue(key, size);
  m_cachedProcessThumbnailSizes[processName] = size;
}

void Config::removeProcessThumbnailSize(const QString &processName) {
  QString key = QString("processThumbnailSizes/%1").arg(processName);
  m_settings->remove(key);
  m_cachedProcessThumbnailSizes.remove(processName);
}

bool Config::hasCustomProcessThumbnailSize(const QString &processName) const {
  QSize size = m_cachedProcessThumbnailSizes.value(processName, QSize(-1, -1));
  return size.isValid() && size.width() > 0 && size.height() > 0;
}

QHash<QString, QSize> Config::getAllCustomProcessThumbnailSizes() const {
  return m_cachedProcessThumbnailSizes;
}

QString Config::getCustomThumbnailName(const QString &characterName) const {
  return m_cachedCustomThumbnailNames.value(characterName, QString());
}

void Config::setCustomThumbnailName(const QString &characterName,
                                    const QString &customName) {
  QString key = QString("thumbnailCustomNames/%1").arg(characterName);
  m_settings->setValue(key, customName);
  m_cachedCustomThumbnailNames[characterName] = customName;
}

void Config::removeCustomThumbnailName(const QString &characterName) {
  QString key = QString("thumbnailCustomNames/%1").arg(characterName);
  m_settings->remove(key);
  m_cachedCustomThumbnailNames.remove(characterName);
}

bool Config::hasCustomThumbnailName(const QString &characterName) const {
  return m_cachedCustomThumbnailNames.contains(characterName) &&
         !m_cachedCustomThumbnailNames[characterName].isEmpty();
}

QHash<QString, QString> Config::getAllCustomThumbnailNames() const {
  return m_cachedCustomThumbnailNames;
}

bool Config::enableSnapping() const { return m_cachedEnableSnapping; }

void Config::setEnableSnapping(bool enabled) {
  m_settings->setValue(KEY_POSITION_ENABLE_SNAPPING, enabled);
  m_cachedEnableSnapping = enabled;
}

int Config::snapDistance() const { return m_cachedSnapDistance; }

void Config::setSnapDistance(int distance) {
  m_settings->setValue(KEY_POSITION_SNAP_DISTANCE, distance);
  m_cachedSnapDistance = distance;
}

bool Config::lockThumbnailPositions() const { return m_cachedLockPositions; }

void Config::setLockThumbnailPositions(bool locked) {
  m_settings->setValue(KEY_POSITION_LOCK, locked);
  m_cachedLockPositions = locked;
}

bool Config::wildcardHotkeys() const { return m_cachedWildcardHotkeys; }

void Config::setWildcardHotkeys(bool enabled) {
  m_settings->setValue(KEY_HOTKEY_WILDCARD, enabled);
  m_cachedWildcardHotkeys = enabled;
}

bool Config::hotkeysOnlyWhenEVEFocused() const {
  return m_cachedHotkeysOnlyWhenEVEFocused;
}

void Config::setHotkeysOnlyWhenEVEFocused(bool enabled) {
  m_settings->setValue(KEY_HOTKEY_ONLY_WHEN_EVE_FOCUSED, enabled);
  m_cachedHotkeysOnlyWhenEVEFocused = enabled;
}

bool Config::isConfigDialogOpen() const { return m_configDialogOpen; }

void Config::setConfigDialogOpen(bool open) { m_configDialogOpen = open; }

bool Config::showCharacterName() const { return m_cachedShowCharacterName; }

void Config::setShowCharacterName(bool enabled) {
  m_settings->setValue(KEY_OVERLAY_SHOW_CHARACTER, enabled);
  m_cachedShowCharacterName = enabled;
}

QColor Config::characterNameColor() const { return m_cachedCharacterNameColor; }

void Config::setCharacterNameColor(const QColor &color) {
  m_settings->setValue(KEY_OVERLAY_CHARACTER_COLOR, color.name());
  m_cachedCharacterNameColor = color;
}

int Config::characterNamePosition() const {
  return m_cachedCharacterNamePosition;
}

void Config::setCharacterNamePosition(int position) {
  m_settings->setValue(KEY_OVERLAY_CHARACTER_POSITION, position);
  m_cachedCharacterNamePosition = position;
}

bool Config::showSystemName() const { return m_cachedShowSystemName; }

void Config::setShowSystemName(bool enabled) {
  m_settings->setValue(KEY_OVERLAY_SHOW_SYSTEM, enabled);
  m_cachedShowSystemName = enabled;
}

bool Config::useUniqueSystemNameColors() const {
  return m_cachedUniqueSystemNameColors;
}

void Config::setUseUniqueSystemNameColors(bool enabled) {
  m_settings->setValue(KEY_OVERLAY_UNIQUE_SYSTEM_COLORS, enabled);
  m_cachedUniqueSystemNameColors = enabled;
}

QColor Config::systemNameColor() const { return m_cachedSystemNameColor; }

void Config::setSystemNameColor(const QColor &color) {
  m_settings->setValue(KEY_OVERLAY_SYSTEM_COLOR, color.name());
  m_cachedSystemNameColor = color;
}

int Config::systemNamePosition() const { return m_cachedSystemNamePosition; }

void Config::setSystemNamePosition(int position) {
  m_settings->setValue(KEY_OVERLAY_SYSTEM_POSITION, position);
  m_cachedSystemNamePosition = position;
}

bool Config::showOverlayBackground() const {
  return m_cachedShowOverlayBackground;
}

void Config::setShowOverlayBackground(bool enabled) {
  m_settings->setValue(KEY_OVERLAY_SHOW_BACKGROUND, enabled);
  m_cachedShowOverlayBackground = enabled;
}

QColor Config::overlayBackgroundColor() const {
  return m_cachedOverlayBackgroundColor;
}

void Config::setOverlayBackgroundColor(const QColor &color) {
  m_settings->setValue(KEY_OVERLAY_BACKGROUND_COLOR, color.name());
  m_cachedOverlayBackgroundColor = color;
}

int Config::overlayBackgroundOpacity() const {
  return m_cachedOverlayBackgroundOpacity;
}

void Config::setOverlayBackgroundOpacity(int opacity) {
  m_settings->setValue(KEY_OVERLAY_BACKGROUND_OPACITY, opacity);
  m_cachedOverlayBackgroundOpacity = opacity;
}

QFont Config::characterNameFont() const { return m_cachedCharacterNameFont; }

void Config::setCharacterNameFont(const QFont &font) {
  m_settings->setValue(KEY_OVERLAY_CHARACTER_FONT, font.toString());
  m_cachedCharacterNameFont = font;
}

int Config::characterNameOffsetX() const {
  return m_cachedCharacterNameOffsetX;
}

void Config::setCharacterNameOffsetX(int offset) {
  m_settings->setValue(KEY_OVERLAY_CHARACTER_OFFSET_X, offset);
  m_cachedCharacterNameOffsetX = offset;
}

int Config::characterNameOffsetY() const {
  return m_cachedCharacterNameOffsetY;
}

void Config::setCharacterNameOffsetY(int offset) {
  m_settings->setValue(KEY_OVERLAY_CHARACTER_OFFSET_Y, offset);
  m_cachedCharacterNameOffsetY = offset;
}

QFont Config::systemNameFont() const { return m_cachedSystemNameFont; }

void Config::setSystemNameFont(const QFont &font) {
  m_settings->setValue(KEY_OVERLAY_SYSTEM_FONT, font.toString());
  m_cachedSystemNameFont = font;
}

int Config::systemNameOffsetX() const { return m_cachedSystemNameOffsetX; }

void Config::setSystemNameOffsetX(int offset) {
  m_settings->setValue(KEY_OVERLAY_SYSTEM_OFFSET_X, offset);
  m_cachedSystemNameOffsetX = offset;
}

int Config::systemNameOffsetY() const { return m_cachedSystemNameOffsetY; }

void Config::setSystemNameOffsetY(int offset) {
  m_settings->setValue(KEY_OVERLAY_SYSTEM_OFFSET_Y, offset);
  m_cachedSystemNameOffsetY = offset;
}

QColor Config::getSystemNameColor(const QString &systemName) const {
  return m_cachedSystemNameColors.value(systemName, QColor());
}

void Config::setSystemNameColor(const QString &systemName,
                                const QColor &color) {
  QString key = QString("systemNameColors/%1").arg(systemName);
  m_settings->setValue(key, color.name());
  m_cachedSystemNameColors[systemName] = color;
}

void Config::removeSystemNameColor(const QString &systemName) {
  QString key = QString("systemNameColors/%1").arg(systemName);
  m_settings->remove(key);
  m_cachedSystemNameColors.remove(systemName);
}

QHash<QString, QColor> Config::getAllSystemNameColors() const {
  return m_cachedSystemNameColors;
}

QFont Config::overlayFont() const { return m_cachedOverlayFont; }

void Config::setOverlayFont(const QFont &font) {
  m_settings->setValue(KEY_OVERLAY_FONT, font.toString());
  m_cachedOverlayFont = font;
}

QString Config::configFilePath() const { return m_settings->fileName(); }

void Config::save() { m_settings->sync(); }

QString Config::getProfilesDirectory() const {
  QString exePath = QCoreApplication::applicationDirPath();
  return exePath + "/profiles";
}

QString Config::getProfileFilePath(const QString &profileName) const {
  return getProfilesDirectory() + "/" + profileName + ".ini";
}

QString Config::getGlobalSettingsPath() const {
  return getProfilesDirectory() + "/settings.global.ini";
}

void Config::ensureProfilesDirectoryExists() const {
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

void Config::loadGlobalSettings() {
  QString exePath = QCoreApplication::applicationDirPath();
  QString oldGlobalPath = exePath + "/settings.global.ini";
  QString newGlobalPath = getGlobalSettingsPath();

  // Migrate existing settings.global.ini to profiles directory
  if (QFile::exists(oldGlobalPath) && !QFile::exists(newGlobalPath)) {
    ensureProfilesDirectoryExists();
    if (QFile::copy(oldGlobalPath, newGlobalPath)) {
      qDebug() << "Migrated settings.global.ini to profiles directory";
      // Remove old file after successful copy
      QFile::remove(oldGlobalPath);
    } else {
      qWarning() << "Failed to migrate settings.global.ini";
    }
  }

  m_globalSettings =
      std::make_unique<QSettings>(newGlobalPath, QSettings::IniFormat);

  m_currentProfileName = m_globalSettings
                             ->value(KEY_GLOBAL_LAST_USED_PROFILE,
                                     DEFAULT_GLOBAL_LAST_USED_PROFILE)
                             .toString();
}

void Config::saveGlobalSettings() {
  if (m_globalSettings) {
    m_globalSettings->setValue(KEY_GLOBAL_LAST_USED_PROFILE,
                               m_currentProfileName);
    m_globalSettings->sync();
  }
}

void Config::migrateToProfileSystem() {
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
      qWarning() << "Failed to migrate settings from" << oldSettingsPath << "to"
                 << defaultProfilePath;
    }
  }
}

void Config::initializeDefaultProfile() {
  ensureProfilesDirectoryExists();

  QString defaultProfilePath = getProfileFilePath("default");
  m_settings =
      std::make_unique<QSettings>(defaultProfilePath, QSettings::IniFormat);

  m_settings->setValue(KEY_CONFIG_VERSION, CONFIG_VERSION);

  m_settings->setValue(KEY_UI_HIGHLIGHT_ACTIVE, DEFAULT_UI_HIGHLIGHT_ACTIVE);
  m_settings->setValue(KEY_UI_HIDE_ACTIVE_THUMBNAIL,
                       DEFAULT_UI_HIDE_ACTIVE_THUMBNAIL);
  m_settings->setValue(KEY_UI_HIGHLIGHT_COLOR, DEFAULT_UI_HIGHLIGHT_COLOR);
  m_settings->setValue(KEY_UI_HIGHLIGHT_BORDER_WIDTH,
                       DEFAULT_UI_HIGHLIGHT_BORDER_WIDTH);
  m_settings->setValue(KEY_UI_ACTIVE_BORDER_STYLE, DEFAULT_ACTIVE_BORDER_STYLE);

  m_settings->setValue(KEY_UI_SHOW_INACTIVE_BORDERS,
                       DEFAULT_UI_SHOW_INACTIVE_BORDERS);
  m_settings->setValue(KEY_UI_INACTIVE_BORDER_COLOR,
                       DEFAULT_UI_INACTIVE_BORDER_COLOR);
  m_settings->setValue(KEY_UI_INACTIVE_BORDER_WIDTH,
                       DEFAULT_UI_INACTIVE_BORDER_WIDTH);
  m_settings->setValue(KEY_UI_INACTIVE_BORDER_STYLE,
                       DEFAULT_INACTIVE_BORDER_STYLE);

  m_settings->setValue(KEY_THUMBNAIL_WIDTH, DEFAULT_THUMBNAIL_WIDTH);
  m_settings->setValue(KEY_THUMBNAIL_HEIGHT, DEFAULT_THUMBNAIL_HEIGHT);
  m_settings->setValue(KEY_THUMBNAIL_OPACITY, DEFAULT_THUMBNAIL_OPACITY);

  QStringList defaultProcessNames;
  defaultProcessNames << DEFAULT_THUMBNAIL_PROCESS_NAME;
  m_settings->setValue(KEY_THUMBNAIL_PROCESS_NAMES, defaultProcessNames);
  m_settings->setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN,
                       DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN);
  m_settings->setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE,
                       DEFAULT_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE);
  m_settings->setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_REF_POSITION,
                       QPoint(DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_X,
                              DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_Y));
  m_settings->setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY,
                       DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY);
  m_settings->setValue(KEY_THUMBNAIL_SHOW_NON_EVE_OVERLAY,
                       DEFAULT_THUMBNAIL_SHOW_NON_EVE_OVERLAY);

  m_settings->setValue(KEY_WINDOW_ALWAYS_ON_TOP, DEFAULT_WINDOW_ALWAYS_ON_TOP);
  m_settings->setValue(KEY_WINDOW_MINIMIZE_INACTIVE,
                       DEFAULT_WINDOW_MINIMIZE_INACTIVE);
  m_settings->setValue(KEY_WINDOW_MINIMIZE_DELAY,
                       DEFAULT_WINDOW_MINIMIZE_DELAY);

  m_settings->setValue(KEY_POSITION_REMEMBER, DEFAULT_POSITION_REMEMBER);
  m_settings->setValue(KEY_POSITION_PRESERVE_LOGOUT,
                       DEFAULT_POSITION_PRESERVE_LOGOUT);
  m_settings->setValue(KEY_POSITION_ENABLE_SNAPPING,
                       DEFAULT_POSITION_ENABLE_SNAPPING);
  m_settings->setValue(KEY_POSITION_SNAP_DISTANCE,
                       DEFAULT_POSITION_SNAP_DISTANCE);

  m_settings->setValue(KEY_OVERLAY_SHOW_CHARACTER,
                       DEFAULT_OVERLAY_SHOW_CHARACTER);
  m_settings->setValue(KEY_OVERLAY_CHARACTER_COLOR,
                       DEFAULT_OVERLAY_CHARACTER_COLOR);
  m_settings->setValue(KEY_OVERLAY_CHARACTER_POSITION,
                       DEFAULT_OVERLAY_CHARACTER_POSITION);
  m_settings->setValue(
      KEY_OVERLAY_CHARACTER_FONT,
      QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE).toString());
  m_settings->setValue(KEY_OVERLAY_SHOW_SYSTEM, DEFAULT_OVERLAY_SHOW_SYSTEM);
  m_settings->setValue(KEY_OVERLAY_SYSTEM_COLOR, DEFAULT_OVERLAY_SYSTEM_COLOR);
  m_settings->setValue(KEY_OVERLAY_SYSTEM_POSITION,
                       DEFAULT_OVERLAY_SYSTEM_POSITION);
  m_settings->setValue(
      KEY_OVERLAY_SYSTEM_FONT,
      QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE).toString());
  m_settings->setValue(KEY_OVERLAY_SHOW_BACKGROUND,
                       DEFAULT_OVERLAY_SHOW_BACKGROUND);
  m_settings->setValue(KEY_OVERLAY_BACKGROUND_COLOR,
                       DEFAULT_OVERLAY_BACKGROUND_COLOR);
  m_settings->setValue(KEY_OVERLAY_BACKGROUND_OPACITY,
                       DEFAULT_OVERLAY_BACKGROUND_OPACITY);
  m_settings->setValue(
      KEY_OVERLAY_FONT,
      QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE).toString());

  m_settings->setValue(KEY_COMBAT_ENABLED, DEFAULT_COMBAT_MESSAGES_ENABLED);
  m_settings->setValue(KEY_COMBAT_DURATION, DEFAULT_COMBAT_MESSAGE_DURATION);
  m_settings->setValue(KEY_COMBAT_POSITION, DEFAULT_COMBAT_MESSAGE_POSITION);
  m_settings->setValue(KEY_COMBAT_COLOR, DEFAULT_COMBAT_MESSAGE_COLOR);
  QFont defaultCombatFont(DEFAULT_OVERLAY_FONT_FAMILY,
                          DEFAULT_OVERLAY_FONT_SIZE);
  defaultCombatFont.setBold(true);
  m_settings->setValue(KEY_COMBAT_FONT, defaultCombatFont.toString());
  m_settings->setValue(KEY_COMBAT_SUPPRESS_FOCUSED,
                       DEFAULT_COMBAT_SUPPRESS_FOCUSED);
  m_settings->setValue(KEY_COMBAT_ENABLED_EVENT_TYPES,
                       DEFAULT_COMBAT_MESSAGE_EVENT_TYPES());
  m_settings->setValue(KEY_MINING_TIMEOUT_SECONDS,
                       DEFAULT_MINING_TIMEOUT_SECONDS);

  m_settings->sync();

  m_currentProfileName = "default";
  qDebug() << "Initialized default profile";
}

QStringList Config::listProfiles() const {
  QStringList profiles;
  QDir profilesDir(getProfilesDirectory());

  if (!profilesDir.exists()) {
    return profiles;
  }

  QStringList filters;
  filters << "*.ini";
  QFileInfoList fileList = profilesDir.entryInfoList(filters, QDir::Files);

  for (const QFileInfo &fileInfo : fileList) {
    QString baseName = fileInfo.baseName();
    // Exclude the global settings file(s) from the profile list
    if (baseName.compare("settings.global", Qt::CaseInsensitive) != 0 &&
        baseName.compare("settings", Qt::CaseInsensitive) != 0) {
      profiles.append(baseName);
    }
  }

  profiles.sort();
  return profiles;
}

QString Config::getCurrentProfileName() const { return m_currentProfileName; }

bool Config::profileExists(const QString &profileName) const {
  if (profileName.isEmpty()) {
    return false;
  }

  QString profilePath = getProfileFilePath(profileName);
  return QFile::exists(profilePath);
}

void Config::migrateLegacyCombatKeys() {
  QPair<const char *, const char *> keys[] = {
      {"CombatMessages/Enabled", KEY_COMBAT_ENABLED},
      {"CombatMessages/Duration", KEY_COMBAT_DURATION},
      {"CombatMessages/Position", KEY_COMBAT_POSITION},
      {"CombatMessages/Color", KEY_COMBAT_COLOR},
      {"CombatMessages/Font", KEY_COMBAT_FONT},
      {"CombatMessages/EnabledEventTypes", KEY_COMBAT_ENABLED_EVENT_TYPES}};

  for (const auto &pair : keys) {
    const char *oldKey = pair.first;
    const char *newKey = pair.second;
    if (m_settings->contains(oldKey) && !m_settings->contains(newKey)) {
      QVariant v = m_settings->value(oldKey);
      m_settings->setValue(newKey, v);
      m_settings->remove(oldKey);
    }
  }
}

bool Config::loadProfile(const QString &profileName) {
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

  loadCacheFromSettings();

  saveGlobalSettings();

  qDebug() << "Loaded profile:" << profileName;
  return true;
}

bool Config::createProfile(const QString &profileName, bool useDefaults) {
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
    newProfile.setValue(KEY_UI_HIDE_ACTIVE_THUMBNAIL,
                        DEFAULT_UI_HIDE_ACTIVE_THUMBNAIL);
    newProfile.setValue(KEY_UI_HIGHLIGHT_COLOR, DEFAULT_UI_HIGHLIGHT_COLOR);
    newProfile.setValue(KEY_UI_HIGHLIGHT_BORDER_WIDTH,
                        DEFAULT_UI_HIGHLIGHT_BORDER_WIDTH);

    newProfile.setValue(KEY_THUMBNAIL_WIDTH, DEFAULT_THUMBNAIL_WIDTH);
    newProfile.setValue(KEY_THUMBNAIL_HEIGHT, DEFAULT_THUMBNAIL_HEIGHT);
    newProfile.setValue(KEY_THUMBNAIL_OPACITY, DEFAULT_THUMBNAIL_OPACITY);

    QStringList defaultProcessNames;
    defaultProcessNames << DEFAULT_THUMBNAIL_PROCESS_NAME;
    newProfile.setValue(KEY_THUMBNAIL_PROCESS_NAMES, defaultProcessNames);
    newProfile.setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN,
                        DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN);
    newProfile.setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE,
                        DEFAULT_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE);
    newProfile.setValue(KEY_THUMBNAIL_NOT_LOGGED_IN_REF_POSITION,
                        QPoint(DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_X,
                               DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_Y));
    newProfile.setValue(KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY,
                        DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY);
    newProfile.setValue(KEY_THUMBNAIL_SHOW_NON_EVE_OVERLAY,
                        DEFAULT_THUMBNAIL_SHOW_NON_EVE_OVERLAY);

    newProfile.setValue(KEY_WINDOW_ALWAYS_ON_TOP, DEFAULT_WINDOW_ALWAYS_ON_TOP);
    newProfile.setValue(KEY_WINDOW_MINIMIZE_INACTIVE,
                        DEFAULT_WINDOW_MINIMIZE_INACTIVE);
    newProfile.setValue(KEY_WINDOW_MINIMIZE_DELAY,
                        DEFAULT_WINDOW_MINIMIZE_DELAY);

    newProfile.setValue(KEY_POSITION_REMEMBER, DEFAULT_POSITION_REMEMBER);
    newProfile.setValue(KEY_POSITION_PRESERVE_LOGOUT,
                        DEFAULT_POSITION_PRESERVE_LOGOUT);
    newProfile.setValue(KEY_POSITION_ENABLE_SNAPPING,
                        DEFAULT_POSITION_ENABLE_SNAPPING);
    newProfile.setValue(KEY_POSITION_SNAP_DISTANCE,
                        DEFAULT_POSITION_SNAP_DISTANCE);

    newProfile.setValue(KEY_OVERLAY_SHOW_CHARACTER,
                        DEFAULT_OVERLAY_SHOW_CHARACTER);
    newProfile.setValue(KEY_OVERLAY_CHARACTER_COLOR,
                        DEFAULT_OVERLAY_CHARACTER_COLOR);
    newProfile.setValue(KEY_OVERLAY_CHARACTER_POSITION,
                        DEFAULT_OVERLAY_CHARACTER_POSITION);
    newProfile.setValue(
        KEY_OVERLAY_CHARACTER_FONT,
        QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE)
            .toString());
    newProfile.setValue(KEY_OVERLAY_SHOW_SYSTEM, DEFAULT_OVERLAY_SHOW_SYSTEM);
    newProfile.setValue(KEY_OVERLAY_SYSTEM_COLOR, DEFAULT_OVERLAY_SYSTEM_COLOR);
    newProfile.setValue(KEY_OVERLAY_SYSTEM_POSITION,
                        DEFAULT_OVERLAY_SYSTEM_POSITION);
    newProfile.setValue(
        KEY_OVERLAY_SYSTEM_FONT,
        QFont(DEFAULT_OVERLAY_FONT_FAMILY, DEFAULT_OVERLAY_FONT_SIZE)
            .toString());
    newProfile.setValue(KEY_OVERLAY_SHOW_BACKGROUND,
                        DEFAULT_OVERLAY_SHOW_BACKGROUND);
    newProfile.setValue(KEY_OVERLAY_BACKGROUND_COLOR,
                        DEFAULT_OVERLAY_BACKGROUND_COLOR);
    newProfile.setValue(KEY_OVERLAY_BACKGROUND_OPACITY,
                        DEFAULT_OVERLAY_BACKGROUND_OPACITY);
    newProfile.setValue(KEY_OVERLAY_FONT, QFont(DEFAULT_OVERLAY_FONT_FAMILY,
                                                DEFAULT_OVERLAY_FONT_SIZE)
                                              .toString());

    newProfile.setValue(KEY_COMBAT_ENABLED, DEFAULT_COMBAT_MESSAGES_ENABLED);
    newProfile.setValue(KEY_COMBAT_DURATION, DEFAULT_COMBAT_MESSAGE_DURATION);
    newProfile.setValue(KEY_COMBAT_POSITION, DEFAULT_COMBAT_MESSAGE_POSITION);
    newProfile.setValue(KEY_COMBAT_COLOR, DEFAULT_COMBAT_MESSAGE_COLOR);
    QFont defaultCombatFont(DEFAULT_OVERLAY_FONT_FAMILY,
                            DEFAULT_OVERLAY_FONT_SIZE);
    defaultCombatFont.setBold(true);
    newProfile.setValue(KEY_COMBAT_FONT, defaultCombatFont.toString());
    newProfile.setValue(KEY_COMBAT_SUPPRESS_FOCUSED,
                        DEFAULT_COMBAT_SUPPRESS_FOCUSED);
    newProfile.setValue(KEY_COMBAT_ENABLED_EVENT_TYPES,
                        DEFAULT_COMBAT_MESSAGE_EVENT_TYPES());
    newProfile.setValue(KEY_MINING_TIMEOUT_SECONDS,
                        DEFAULT_MINING_TIMEOUT_SECONDS);
  }

  newProfile.sync();

  qDebug() << "Created profile:" << profileName;
  return true;
}

bool Config::cloneProfile(const QString &sourceName, const QString &destName) {
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
    qWarning() << "Failed to clone profile from" << sourceName << "to"
               << destName;
    return false;
  }
}

bool Config::deleteProfile(const QString &profileName) {
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

bool Config::renameProfile(const QString &oldName, const QString &newName) {
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
        m_globalSettings->setValue(QString("ProfileHotkeys/%1").arg(newName),
                                   hotkey);
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

QVector<HotkeyBinding>
Config::getProfileHotkeys(const QString &profileName) const {
  QVector<HotkeyBinding> hotkeys;

  if (!m_globalSettings) {
    return hotkeys;
  }

  QString key = QString("ProfileHotkeys/%1").arg(profileName);
  QString hotkeyString = m_globalSettings->value(key, QString()).toString();

  if (hotkeyString.isEmpty()) {
    return hotkeys;
  }

  QStringList hotkeyParts = hotkeyString.split('|', Qt::SkipEmptyParts);

  for (const QString &part : hotkeyParts) {
    QStringList components = part.split(',');
    if (components.size() >= 4) {
      int keyCode = components[0].toInt();
      bool ctrl = components[1].toInt() != 0;
      bool alt = components[2].toInt() != 0;
      bool shift = components[3].toInt() != 0;

      if (keyCode != 0) {
        hotkeys.append(HotkeyBinding{keyCode, ctrl, alt, shift, true});
      }
    }
  }

  return hotkeys;
}

void Config::setProfileHotkeys(const QString &profileName,
                               const QVector<HotkeyBinding> &hotkeys) {
  if (!m_globalSettings) {
    return;
  }

  if (!profileExists(profileName)) {
    qWarning() << "Cannot set hotkeys for non-existent profile:" << profileName;
    return;
  }

  QStringList hotkeyParts;
  for (const HotkeyBinding &binding : hotkeys) {
    if (binding.keyCode != 0) {
      hotkeyParts.append(QString("%1,%2,%3,%4")
                             .arg(binding.keyCode)
                             .arg(binding.ctrl ? 1 : 0)
                             .arg(binding.alt ? 1 : 0)
                             .arg(binding.shift ? 1 : 0));
    }
  }

  QString hotkeyString = hotkeyParts.join('|');

  QString settingsKey = QString("ProfileHotkeys/%1").arg(profileName);
  m_globalSettings->setValue(settingsKey, hotkeyString);
  m_globalSettings->sync();

  qDebug() << "Set" << hotkeys.size() << "hotkey(s) for profile" << profileName;
}

void Config::clearProfileHotkey(const QString &profileName) {
  if (!m_globalSettings) {
    return;
  }

  QString key = QString("ProfileHotkeys/%1").arg(profileName);
  m_globalSettings->remove(key);
  m_globalSettings->sync();

  qDebug() << "Cleared hotkey for profile" << profileName;
}

bool Config::enableChatLogMonitoring() const {
  return m_cachedEnableChatLogMonitoring;
}

void Config::setEnableChatLogMonitoring(bool enabled) {
  m_settings->setValue(KEY_CHATLOG_ENABLE_MONITORING, enabled);
  m_cachedEnableChatLogMonitoring = enabled;
}

QString Config::chatLogDirectory() const {
  return expandEnvironmentVariables(m_cachedChatLogDirectory);
}

QString Config::chatLogDirectoryRaw() const { return m_cachedChatLogDirectory; }

void Config::setChatLogDirectory(const QString &directory) {
  m_settings->setValue(KEY_CHATLOG_DIRECTORY, directory);
  m_cachedChatLogDirectory = directory;
}

QString Config::getDefaultChatLogDirectory() {
  QString documentsPath =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  QString evePath = documentsPath + "/EVE/logs/Chatlogs";

  QDir eveDir(evePath);
  if (eveDir.exists()) {
    return evePath;
  }

  return evePath;
}

QString Config::getDefaultGameLogDirectory() {
  QString documentsPath =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  QString evePath = documentsPath + "/EVE/logs/Gamelogs";

  QDir eveDir(evePath);
  if (eveDir.exists()) {
    return evePath;
  }

  return evePath;
}

QString Config::gameLogDirectory() const {
  return expandEnvironmentVariables(m_cachedGameLogDirectory);
}

QString Config::gameLogDirectoryRaw() const { return m_cachedGameLogDirectory; }

void Config::setGameLogDirectory(const QString &directory) {
  m_settings->setValue(KEY_GAMELOG_DIRECTORY, directory);
  m_cachedGameLogDirectory = directory;
}

bool Config::enableGameLogMonitoring() const {
  return m_cachedEnableGameLogMonitoring;
}

void Config::setEnableGameLogMonitoring(bool enabled) {
  qDebug() << "Config::setEnableGameLogMonitoring called with:" << enabled;
  m_settings->setValue(KEY_GAMELOG_ENABLE_MONITORING, enabled);
  m_cachedEnableGameLogMonitoring = enabled;
  qDebug() << "Config::setEnableGameLogMonitoring - cached value now:"
           << m_cachedEnableGameLogMonitoring;
}

bool Config::showCombatMessages() const { return m_cachedShowCombatMessages; }

void Config::setShowCombatMessages(bool enabled) {
  m_settings->setValue(KEY_COMBAT_ENABLED, enabled);
  m_cachedShowCombatMessages = enabled;
}

int Config::combatMessagePosition() const {
  return m_cachedCombatMessagePosition;
}

void Config::setCombatMessagePosition(int position) {
  m_settings->setValue(KEY_COMBAT_POSITION, position);
  m_cachedCombatMessagePosition = position;
}

QFont Config::combatMessageFont() const { return m_cachedCombatMessageFont; }

void Config::setCombatMessageFont(const QFont &font) {
  m_settings->setValue(KEY_COMBAT_FONT, font);
  m_cachedCombatMessageFont = font;
}

int Config::combatMessageOffsetX() const {
  return m_cachedCombatMessageOffsetX;
}

void Config::setCombatMessageOffsetX(int offset) {
  m_settings->setValue(KEY_COMBAT_OFFSET_X, offset);
  m_cachedCombatMessageOffsetX = offset;
}

int Config::combatMessageOffsetY() const {
  return m_cachedCombatMessageOffsetY;
}

void Config::setCombatMessageOffsetY(int offset) {
  m_settings->setValue(KEY_COMBAT_OFFSET_Y, offset);
  m_cachedCombatMessageOffsetY = offset;
}

QStringList Config::enabledCombatEventTypes() const {
  return m_cachedEnabledCombatEventTypes;
}

void Config::setEnabledCombatEventTypes(const QStringList &types) {
  m_settings->setValue(KEY_COMBAT_ENABLED_EVENT_TYPES, types);
  m_cachedEnabledCombatEventTypes = types;
}

bool Config::isCombatEventTypeEnabled(const QString &eventType) const {
  QStringList enabled = enabledCombatEventTypes();
  return enabled.contains(eventType);
}

int Config::miningTimeoutSeconds() const {
  return m_cachedMiningTimeoutSeconds;
}

void Config::setMiningTimeoutSeconds(int seconds) {
  m_settings->setValue(KEY_MINING_TIMEOUT_SECONDS, seconds);
  m_cachedMiningTimeoutSeconds = seconds;
}

QColor Config::combatEventColor(const QString &eventType) const {
  return m_cachedCombatEventColors.value(
      eventType, QColor(DEFAULT_EVENT_COLORS().value(
                     eventType, DEFAULT_COMBAT_MESSAGE_COLOR)));
}

void Config::setCombatEventColor(const QString &eventType,
                                 const QColor &color) {
  QString key = combatEventColorKey(eventType);
  m_settings->setValue(key, color);
  m_cachedCombatEventColors[eventType] = color;
}

int Config::combatEventDuration(const QString &eventType) const {
  return m_cachedCombatEventDurations.value(eventType,
                                            DEFAULT_COMBAT_MESSAGE_DURATION);
}

void Config::setCombatEventDuration(const QString &eventType,
                                    int milliseconds) {
  QString key = combatEventDurationKey(eventType);
  m_settings->setValue(key, milliseconds);
  m_cachedCombatEventDurations[eventType] = milliseconds;
}

bool Config::combatEventBorderHighlight(const QString &eventType) const {
  return m_cachedCombatEventBorderHighlights.value(
      eventType, DEFAULT_COMBAT_EVENT_BORDER_HIGHLIGHT);
}

void Config::setCombatEventBorderHighlight(const QString &eventType,
                                           bool enabled) {
  QString key = combatEventBorderHighlightKey(eventType);
  m_settings->setValue(key, enabled);
  m_cachedCombatEventBorderHighlights[eventType] = enabled;
}

bool Config::combatEventSuppressFocused(const QString &eventType) const {
  if (!m_cachedCombatEventSuppressFocused.contains(eventType)) {
    QString key = combatEventSuppressFocusedKey(eventType);
    bool value = m_settings->value(key, false).toBool();
    m_cachedCombatEventSuppressFocused[eventType] = value;
  }
  return m_cachedCombatEventSuppressFocused.value(eventType, false);
}

void Config::setCombatEventSuppressFocused(const QString &eventType,
                                           bool enabled) {
  QString key = combatEventSuppressFocusedKey(eventType);
  m_settings->setValue(key, enabled);
  m_cachedCombatEventSuppressFocused[eventType] = enabled;
}

bool Config::suppressCombatWhenFocused() const {
  return m_cachedSuppressCombatWhenFocused;
}

void Config::setSuppressCombatWhenFocused(bool enabled) {
  m_settings->setValue(KEY_COMBAT_SUPPRESS_FOCUSED, enabled);
  m_cachedSuppressCombatWhenFocused = enabled;
}

BorderStyle Config::combatBorderStyle(const QString &eventType) const {
  return m_cachedCombatBorderStyles.value(
      eventType, static_cast<BorderStyle>(DEFAULT_COMBAT_BORDER_STYLE));
}

void Config::setCombatBorderStyle(const QString &eventType, BorderStyle style) {
  QString key = combatBorderStyleKey(eventType);
  m_settings->setValue(key, static_cast<int>(style));
  m_cachedCombatBorderStyles[eventType] = style;
}

bool Config::combatEventSoundEnabled(const QString &eventType) const {
  return m_cachedCombatEventSoundsEnabled.value(eventType,
                                                DEFAULT_COMBAT_SOUND_ENABLED);
}

void Config::setCombatEventSoundEnabled(const QString &eventType,
                                        bool enabled) {
  QString key = combatEventSoundEnabledKey(eventType);
  m_settings->setValue(key, enabled);
  m_cachedCombatEventSoundsEnabled[eventType] = enabled;
}

QString Config::combatEventSoundFile(const QString &eventType) const {
  return m_cachedCombatEventSoundFiles.value(eventType, QString());
}

void Config::setCombatEventSoundFile(const QString &eventType,
                                     const QString &filePath) {
  QString key = combatEventSoundFileKey(eventType);
  m_settings->setValue(key, filePath);
  m_cachedCombatEventSoundFiles[eventType] = filePath;
}

int Config::combatEventSoundVolume(const QString &eventType) const {
  return m_cachedCombatEventSoundVolumes.value(eventType,
                                               DEFAULT_COMBAT_SOUND_VOLUME);
}

void Config::setCombatEventSoundVolume(const QString &eventType, int volume) {
  QString key = combatEventSoundVolumeKey(eventType);
  m_settings->setValue(key, volume);
  m_cachedCombatEventSoundVolumes[eventType] = volume;
}
