#ifndef CONFIG_H
#define CONFIG_H

#include "borderstyle.h"
#include <QColor>
#include <QFont>
#include <QHash>
#include <QMap>
#include <QPair>
#include <QPoint>
#include <QRect>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

struct HotkeyBinding;

class Config {
public:
  static Config &instance();

  bool highlightActiveWindow() const;
  void setHighlightActiveWindow(bool enabled);

  bool hideActiveClientThumbnail() const;
  void setHideActiveClientThumbnail(bool enabled);

  QColor highlightColor() const;
  void setHighlightColor(const QColor &color);

  int highlightBorderWidth() const;
  void setHighlightBorderWidth(int width);

  BorderStyle activeBorderStyle() const;
  void setActiveBorderStyle(BorderStyle style);

  int thumbnailWidth() const;
  void setThumbnailWidth(int width);

  int thumbnailHeight() const;
  void setThumbnailHeight(int height);

  int thumbnailOpacity() const;
  void setThumbnailOpacity(int opacity);

  bool showNotLoggedInClients() const;
  void setShowNotLoggedInClients(bool enabled);

  int notLoggedInStackMode() const;
  void setNotLoggedInStackMode(int mode);

  QPoint notLoggedInReferencePosition() const;
  void setNotLoggedInReferencePosition(const QPoint &pos);

  bool showNotLoggedInOverlay() const;
  void setShowNotLoggedInOverlay(bool show);

  bool showNonEVEOverlay() const;
  void setShowNonEVEOverlay(bool show);

  QStringList processNames() const;
  void setProcessNames(const QStringList &names);
  void addProcessName(const QString &name);
  void removeProcessName(const QString &name);

  bool alwaysOnTop() const;
  void setAlwaysOnTop(bool enabled);

  bool minimizeInactiveClients() const;
  void setMinimizeInactiveClients(bool enabled);

  int minimizeDelay() const;
  void setMinimizeDelay(int delayMs);

  QStringList neverMinimizeCharacters() const;
  void setNeverMinimizeCharacters(const QStringList &characters);
  void addNeverMinimizeCharacter(const QString &characterName);
  void removeNeverMinimizeCharacter(const QString &characterName);
  bool isCharacterNeverMinimize(const QString &characterName) const;

  QStringList hiddenCharacters() const;
  void setHiddenCharacters(const QStringList &characters);
  void addHiddenCharacter(const QString &characterName);
  void removeHiddenCharacter(const QString &characterName);
  bool isCharacterHidden(const QString &characterName) const;

  bool saveClientLocation() const;
  void setSaveClientLocation(bool enabled);

  QRect getClientWindowRect(const QString &characterName) const;
  void setClientWindowRect(const QString &characterName, const QRect &rect);

  bool rememberPositions() const;
  void setRememberPositions(bool enabled);

  bool preserveLogoutPositions() const;
  void setPreserveLogoutPositions(bool enabled);

  QPoint getThumbnailPosition(const QString &characterName) const;
  void setThumbnailPosition(const QString &characterName, const QPoint &pos);

  QColor getCharacterBorderColor(const QString &characterName) const;
  void setCharacterBorderColor(const QString &characterName,
                               const QColor &color);
  void removeCharacterBorderColor(const QString &characterName);
  QHash<QString, QColor> getAllCharacterBorderColors() const;

  QSize getThumbnailSize(const QString &characterName) const;
  void setThumbnailSize(const QString &characterName, const QSize &size);
  void removeThumbnailSize(const QString &characterName);
  bool hasCustomThumbnailSize(const QString &characterName) const;
  QHash<QString, QSize> getAllCustomThumbnailSizes() const;

  QString getCustomThumbnailName(const QString &characterName) const;
  void setCustomThumbnailName(const QString &characterName,
                              const QString &customName);
  void removeCustomThumbnailName(const QString &characterName);
  bool hasCustomThumbnailName(const QString &characterName) const;
  QHash<QString, QString> getAllCustomThumbnailNames() const;

  bool enableSnapping() const;
  void setEnableSnapping(bool enabled);

  int snapDistance() const;
  void setSnapDistance(int distance);

  bool lockThumbnailPositions() const;
  void setLockThumbnailPositions(bool locked);

  bool wildcardHotkeys() const;
  void setWildcardHotkeys(bool enabled);

  bool hotkeysOnlyWhenEVEFocused() const;
  void setHotkeysOnlyWhenEVEFocused(bool enabled);

  bool isConfigDialogOpen() const;
  void setConfigDialogOpen(bool open);

  bool showCharacterName() const;
  void setShowCharacterName(bool enabled);

  QColor characterNameColor() const;
  void setCharacterNameColor(const QColor &color);

  int characterNamePosition() const;
  void setCharacterNamePosition(int position);

  QFont characterNameFont() const;
  void setCharacterNameFont(const QFont &font);

  bool showSystemName() const;
  void setShowSystemName(bool enabled);

  QColor systemNameColor() const;
  void setSystemNameColor(const QColor &color);

  int systemNamePosition() const;
  void setSystemNamePosition(int position);

  QFont systemNameFont() const;
  void setSystemNameFont(const QFont &font);

  bool showOverlayBackground() const;
  void setShowOverlayBackground(bool enabled);

  QColor overlayBackgroundColor() const;
  void setOverlayBackgroundColor(const QColor &color);

  int overlayBackgroundOpacity() const;
  void setOverlayBackgroundOpacity(int opacity);

  QFont overlayFont() const;
  void setOverlayFont(const QFont &font);

  bool enableChatLogMonitoring() const;
  void setEnableChatLogMonitoring(bool enabled);

  QString chatLogDirectory() const;
  void setChatLogDirectory(const QString &directory);

  bool enableGameLogMonitoring() const;
  void setEnableGameLogMonitoring(bool enabled);

  QString gameLogDirectory() const;
  void setGameLogDirectory(const QString &directory);

  static QString getDefaultChatLogDirectory();
  static QString getDefaultGameLogDirectory();

  bool showCombatMessages() const;
  void setShowCombatMessages(bool enabled);

  int combatMessagePosition() const;
  void setCombatMessagePosition(int position);

  QFont combatMessageFont() const;
  void setCombatMessageFont(const QFont &font);

  QStringList enabledCombatEventTypes() const;
  void setEnabledCombatEventTypes(const QStringList &types);
  bool isCombatEventTypeEnabled(const QString &eventType) const;

  QColor combatEventColor(const QString &eventType) const;
  void setCombatEventColor(const QString &eventType, const QColor &color);

  int combatEventDuration(const QString &eventType) const;
  void setCombatEventDuration(const QString &eventType, int milliseconds);

  bool combatEventBorderHighlight(const QString &eventType) const;
  void setCombatEventBorderHighlight(const QString &eventType, bool enabled);

  BorderStyle combatBorderStyle(const QString &eventType) const;
  void setCombatBorderStyle(const QString &eventType, BorderStyle style);

  int miningTimeoutSeconds() const;
  void setMiningTimeoutSeconds(int seconds);

  QString configFilePath() const;

  void save();

  QStringList listProfiles() const;
  QString getCurrentProfileName() const;
  bool loadProfile(const QString &profileName);
  bool createProfile(const QString &profileName, bool useDefaults = true);
  bool cloneProfile(const QString &sourceName, const QString &destName);
  bool deleteProfile(const QString &profileName);
  bool renameProfile(const QString &oldName, const QString &newName);
  bool profileExists(const QString &profileName) const;

  QVector<HotkeyBinding> getProfileHotkeys(const QString &profileName) const;
  void setProfileHotkeys(const QString &profileName,
                         const QVector<HotkeyBinding> &hotkeys);
  void clearProfileHotkey(const QString &profileName);

  static constexpr const char *DEFAULT_OVERLAY_FONT_FAMILY = "Segoe UI";
  static constexpr int DEFAULT_OVERLAY_FONT_SIZE = 10;
  static constexpr const char *KEY_GLOBAL_LAST_USED_PROFILE =
      "global/lastUsedProfile";
  static constexpr const char *DEFAULT_GLOBAL_LAST_USED_PROFILE = "default";
  static constexpr const char *KEY_UI_SKIP_PROFILE_SWITCH_CONFIRMATION =
      "ui/skipProfileSwitchConfirmation";
  static constexpr bool DEFAULT_UI_SKIP_PROFILE_SWITCH_CONFIRMATION = false;

  static constexpr const char *CONFIG_VERSION = "1.0";

  static constexpr bool DEFAULT_UI_HIGHLIGHT_ACTIVE = true;
  static constexpr const char *DEFAULT_UI_HIGHLIGHT_COLOR = "#FFFFFF";
  static constexpr int DEFAULT_UI_HIGHLIGHT_BORDER_WIDTH = 2;
  static constexpr bool DEFAULT_UI_HIDE_ACTIVE_THUMBNAIL = false;
  static constexpr int DEFAULT_ACTIVE_BORDER_STYLE =
      static_cast<int>(BorderStyle::Solid);

  static constexpr int DEFAULT_THUMBNAIL_WIDTH = 240;
  static constexpr int DEFAULT_THUMBNAIL_HEIGHT = 135;
  static constexpr int DEFAULT_THUMBNAIL_OPACITY = 100;
  static constexpr const char *DEFAULT_THUMBNAIL_PROCESS_NAME = "exefile.exe";
  static constexpr bool DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN = true;
  static constexpr int DEFAULT_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE = 0;
  static constexpr int DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_X = 10;
  static constexpr int DEFAULT_THUMBNAIL_NOT_LOGGED_IN_REF_Y = 10;
  static constexpr bool DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY = true;
  static constexpr bool DEFAULT_THUMBNAIL_SHOW_NON_EVE_OVERLAY = true;

  static constexpr bool DEFAULT_WINDOW_ALWAYS_ON_TOP = true;
  static constexpr bool DEFAULT_WINDOW_MINIMIZE_INACTIVE = false;
  static constexpr int DEFAULT_WINDOW_MINIMIZE_DELAY = 100;
  static constexpr bool DEFAULT_WINDOW_SAVE_CLIENT_LOCATION = false;

  static constexpr bool DEFAULT_POSITION_REMEMBER = true;
  static constexpr bool DEFAULT_POSITION_PRESERVE_LOGOUT = false;
  static constexpr bool DEFAULT_POSITION_ENABLE_SNAPPING = true;
  static constexpr int DEFAULT_POSITION_SNAP_DISTANCE = 10;
  static constexpr bool DEFAULT_POSITION_LOCK = false;

  static constexpr bool DEFAULT_HOTKEY_WILDCARD = false;
  static constexpr bool DEFAULT_HOTKEY_ONLY_WHEN_EVE_FOCUSED = false;

  static constexpr bool DEFAULT_OVERLAY_SHOW_CHARACTER = true;
  static constexpr const char *DEFAULT_OVERLAY_CHARACTER_COLOR = "#FFFFFF";
  static constexpr int DEFAULT_OVERLAY_CHARACTER_POSITION = 0;
  static constexpr bool DEFAULT_OVERLAY_SHOW_SYSTEM = false;
  static constexpr const char *DEFAULT_OVERLAY_SYSTEM_COLOR = "#C8C8C8";
  static constexpr int DEFAULT_OVERLAY_SYSTEM_POSITION = 3;
  static constexpr bool DEFAULT_OVERLAY_SHOW_BACKGROUND = true;
  static constexpr const char *DEFAULT_OVERLAY_BACKGROUND_COLOR = "#000000";
  static constexpr int DEFAULT_OVERLAY_BACKGROUND_OPACITY = 70;

  static constexpr int OPACITY_MIN = 0;
  static constexpr int OPACITY_MAX = 100;

  static constexpr bool DEFAULT_CHATLOG_ENABLE_MONITORING = false;
  static constexpr bool DEFAULT_GAMELOG_ENABLE_MONITORING = false;

  static constexpr bool DEFAULT_COMBAT_MESSAGES_ENABLED = false;
  static constexpr int DEFAULT_COMBAT_MESSAGE_DURATION = 5000;
  static constexpr int DEFAULT_COMBAT_MESSAGE_POSITION = 3;
  static constexpr const char *DEFAULT_COMBAT_MESSAGE_COLOR = "#FFFFFF";
  static constexpr int DEFAULT_MINING_TIMEOUT_SECONDS = 30;
  static constexpr bool DEFAULT_COMBAT_EVENT_BORDER_HIGHLIGHT = false;
  static constexpr int DEFAULT_COMBAT_BORDER_STYLE =
      static_cast<int>(BorderStyle::Dashed);
  static inline QStringList DEFAULT_COMBAT_MESSAGE_EVENT_TYPES() {
    return QStringList{"fleet_invite", "follow_warp",    "regroup",
                       "compression",  "mining_started", "mining_stopped"};
  }

private:
  Config();
  ~Config();

  std::unique_ptr<QSettings> m_settings;

  mutable bool m_cachedHighlightActive;
  mutable bool m_cachedHideActiveThumbnail;
  mutable QColor m_cachedHighlightColor;
  mutable int m_cachedHighlightBorderWidth;
  mutable BorderStyle m_cachedActiveBorderStyle;

  mutable int m_cachedThumbnailWidth;
  mutable int m_cachedThumbnailHeight;
  mutable int m_cachedThumbnailOpacity;

  mutable bool m_cachedShowNotLoggedIn;
  mutable int m_cachedNotLoggedInStackMode;
  mutable QPoint m_cachedNotLoggedInReferencePosition;
  mutable bool m_cachedShowNotLoggedInOverlay;
  mutable bool m_cachedShowNonEVEOverlay;

  mutable QStringList m_cachedProcessNames;

  mutable bool m_cachedAlwaysOnTop;
  mutable bool m_cachedMinimizeInactive;
  mutable int m_cachedMinimizeDelay;
  mutable QStringList m_cachedNeverMinimizeCharacters;
  mutable QStringList m_cachedHiddenCharacters;
  mutable bool m_cachedSaveClientLocation;

  mutable bool m_cachedRememberPositions;
  mutable bool m_cachedPreserveLogoutPositions;
  mutable bool m_cachedEnableSnapping;
  mutable int m_cachedSnapDistance;
  mutable bool m_cachedLockPositions;

  mutable bool m_cachedWildcardHotkeys;
  mutable bool m_cachedHotkeysOnlyWhenEVEFocused;

  mutable bool m_cachedShowCharacterName;
  mutable QColor m_cachedCharacterNameColor;
  mutable int m_cachedCharacterNamePosition;
  mutable QFont m_cachedCharacterNameFont;
  mutable bool m_cachedShowSystemName;
  mutable QColor m_cachedSystemNameColor;
  mutable int m_cachedSystemNamePosition;
  mutable QFont m_cachedSystemNameFont;
  mutable bool m_cachedShowOverlayBackground;
  mutable QColor m_cachedOverlayBackgroundColor;
  mutable int m_cachedOverlayBackgroundOpacity;
  mutable QFont m_cachedOverlayFont;

  mutable bool m_cachedEnableChatLogMonitoring;
  mutable QString m_cachedChatLogDirectory;
  mutable bool m_cachedEnableGameLogMonitoring;
  mutable QString m_cachedGameLogDirectory;

  mutable bool m_cachedShowCombatMessages;
  mutable int m_cachedCombatMessagePosition;
  mutable QFont m_cachedCombatMessageFont;
  mutable QMap<QString, QColor> m_cachedCombatEventColors;
  mutable QMap<QString, int> m_cachedCombatEventDurations;
  mutable QMap<QString, bool> m_cachedCombatEventBorderHighlights;
  mutable QMap<QString, BorderStyle> m_cachedCombatBorderStyles;
  mutable QStringList m_cachedEnabledCombatEventTypes;
  mutable int m_cachedMiningTimeoutSeconds;

  mutable QHash<QString, QColor> m_cachedCharacterBorderColors;
  mutable QHash<QString, QPoint> m_cachedThumbnailPositions;
  mutable QHash<QString, QSize> m_cachedThumbnailSizes;
  mutable QHash<QString, QString> m_cachedCustomThumbnailNames;
  mutable QHash<QString, QRect> m_cachedClientWindowRects;

  bool m_configDialogOpen = false;

  QString m_currentProfileName;
  std::unique_ptr<QSettings> m_globalSettings;

  void loadCacheFromSettings();

  QString getProfilesDirectory() const;
  QString getProfileFilePath(const QString &profileName) const;
  QString getGlobalSettingsPath() const;
  void ensureProfilesDirectoryExists() const;
  void migrateToProfileSystem();
  void migrateLegacyCombatKeys();
  void initializeDefaultProfile();
  void loadGlobalSettings();
  void saveGlobalSettings();

  static constexpr const char *KEY_CONFIG_VERSION = "config/version";

  static constexpr const char *KEY_UI_HIGHLIGHT_ACTIVE =
      "ui/highlightActiveWindow";
  static constexpr const char *KEY_UI_HIGHLIGHT_COLOR = "ui/highlightColor";
  static constexpr const char *KEY_UI_HIGHLIGHT_BORDER_WIDTH =
      "ui/highlightBorderWidth";
  static constexpr const char *KEY_UI_ACTIVE_BORDER_STYLE =
      "ui/activeBorderStyle";

  static constexpr const char *KEY_THUMBNAIL_WIDTH = "thumbnail/width";
  static constexpr const char *KEY_THUMBNAIL_HEIGHT = "thumbnail/height";
  static constexpr const char *KEY_UI_HIDE_ACTIVE_THUMBNAIL =
      "ui/hideActiveClientThumbnail";
  static constexpr const char *KEY_THUMBNAIL_OPACITY = "thumbnail/opacity";
  static constexpr const char *KEY_THUMBNAIL_PROCESS_NAMES =
      "thumbnail/processNames";
  static constexpr const char *KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN =
      "thumbnail/showNotLoggedInClients";
  static constexpr const char *KEY_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE =
      "thumbnail/notLoggedInStackMode";
  static constexpr const char *KEY_THUMBNAIL_NOT_LOGGED_IN_REF_POSITION =
      "thumbnail/notLoggedInReferencePosition";
  static constexpr const char *KEY_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY =
      "thumbnail/showNotLoggedInOverlay";
  static constexpr const char *KEY_THUMBNAIL_SHOW_NON_EVE_OVERLAY =
      "thumbnail/showNonEVEOverlay";

  static constexpr const char *KEY_WINDOW_ALWAYS_ON_TOP = "window/alwaysOnTop";
  static constexpr const char *KEY_WINDOW_MINIMIZE_INACTIVE =
      "window/minimizeInactiveClients";
  static constexpr const char *KEY_WINDOW_MINIMIZE_DELAY =
      "window/minimizeDelay";
  static constexpr const char *KEY_WINDOW_NEVER_MINIMIZE_CHARACTERS =
      "window/neverMinimizeCharacters";
  static constexpr const char *KEY_THUMBNAIL_HIDDEN_CHARACTERS =
      "thumbnail/hiddenCharacters";
  static constexpr const char *KEY_WINDOW_SAVE_CLIENT_LOCATION =
      "window/saveClientLocation";

  static constexpr const char *KEY_POSITION_REMEMBER =
      "position/rememberPositions";
  static constexpr const char *KEY_POSITION_PRESERVE_LOGOUT =
      "position/preserveLogoutPositions";
  static constexpr const char *KEY_POSITION_ENABLE_SNAPPING =
      "position/enableSnapping";
  static constexpr const char *KEY_POSITION_SNAP_DISTANCE =
      "position/snapDistance";
  static constexpr const char *KEY_POSITION_LOCK = "position/lockPositions";

  static constexpr const char *KEY_HOTKEY_WILDCARD = "hotkey/wildcardMode";
  static constexpr const char *KEY_HOTKEY_ONLY_WHEN_EVE_FOCUSED =
      "hotkey/onlyWhenEVEFocused";

  static constexpr const char *KEY_OVERLAY_SHOW_CHARACTER =
      "overlay/showCharacterName";
  static constexpr const char *KEY_OVERLAY_CHARACTER_COLOR =
      "overlay/characterNameColor";
  static constexpr const char *KEY_OVERLAY_CHARACTER_POSITION =
      "overlay/characterNamePosition";
  static constexpr const char *KEY_OVERLAY_CHARACTER_FONT =
      "overlay/characterNameFont";
  static constexpr const char *KEY_OVERLAY_SHOW_SYSTEM =
      "overlay/showSystemName";
  static constexpr const char *KEY_OVERLAY_SYSTEM_COLOR =
      "overlay/systemNameColor";
  static constexpr const char *KEY_OVERLAY_SYSTEM_POSITION =
      "overlay/systemNamePosition";
  static constexpr const char *KEY_OVERLAY_SYSTEM_FONT =
      "overlay/systemNameFont";
  static constexpr const char *KEY_OVERLAY_SHOW_BACKGROUND =
      "overlay/showBackground";
  static constexpr const char *KEY_OVERLAY_BACKGROUND_COLOR =
      "overlay/backgroundColor";
  static constexpr const char *KEY_OVERLAY_BACKGROUND_OPACITY =
      "overlay/backgroundOpacity";
  static constexpr const char *KEY_OVERLAY_FONT = "overlay/font";

  static constexpr const char *KEY_CHATLOG_ENABLE_MONITORING =
      "chatlog/enableMonitoring";
  static constexpr const char *KEY_CHATLOG_DIRECTORY = "chatlog/directory";

  static constexpr const char *KEY_GAMELOG_ENABLE_MONITORING =
      "gamelog/enableMonitoring";
  static constexpr const char *KEY_GAMELOG_DIRECTORY = "gamelog/directory";

  static constexpr const char *KEY_COMBAT_ENABLED = "combatMessages/enabled";
  static constexpr const char *KEY_COMBAT_DURATION = "combatMessages/duration";
  static constexpr const char *KEY_COMBAT_POSITION = "combatMessages/position";
  static constexpr const char *KEY_COMBAT_COLOR = "combatMessages/color";
  static constexpr const char *KEY_COMBAT_FONT = "combatMessages/font";
  static constexpr const char *KEY_COMBAT_ENABLED_EVENT_TYPES =
      "combatMessages/enabledEventTypes";

  static inline QString combatEventColorKey(const QString &eventType) {
    return QString("combatMessages/eventColors/%1").arg(eventType);
  }
  static inline QString combatEventDurationKey(const QString &eventType) {
    return QString("combatMessages/eventDurations/%1").arg(eventType);
  }
  static inline QString
  combatEventBorderHighlightKey(const QString &eventType) {
    return QString("combatMessages/borderHighlights/%1").arg(eventType);
  }
  static inline QString combatBorderStyleKey(const QString &eventType) {
    return QString("combatMessages/borderStyles/%1").arg(eventType);
  }

  static inline QMap<QString, QString> DEFAULT_EVENT_COLORS() {
    return QMap<QString, QString>{
        {"fleet_invite", "#4A9EFF"},   {"follow_warp", "#FFD700"},
        {"regroup", "#FF8C42"},        {"compression", "#7FFF00"},
        {"mining_started", "#FFD700"}, {"mining_stopped", "#FF6B6B"}};
  }

  static constexpr const char *KEY_MINING_TIMEOUT_SECONDS =
      "miningMode/timeoutSeconds";
};

#endif
