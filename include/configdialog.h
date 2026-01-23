#ifndef CONFIGDIALOG_H
#define CONFIGDIALOG_H

#include "settingbinding.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QSoundEffect>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVariantMap>
#include <memory>

class HotkeyCapture;
class QVBoxLayout;
class ThumbnailWidget;
class QNetworkAccessManager;
class QScrollArea;

class ConfigDialog : public QDialog {
  Q_OBJECT

public:
  explicit ConfigDialog(QWidget *parent = nullptr);
  ~ConfigDialog();

public slots:
  void onExternalProfileSwitch(const QString &profileName);

signals:
  void settingsApplied();
  void saveClientLocationsRequested();

private slots:
  void onCategoryChanged(int index);
  void onApplyClicked();
  void onOkClicked();
  void onCancelClicked();
  void onColorButtonClicked();
  void onAddCharacterHotkey();
  void onPopulateFromOpenWindows();
  void onAddCycleGroup();
  void onEditCycleGroupCharacters();
  void onAddNeverMinimizeCharacter();
  void onPopulateNeverMinimize();
  void onAddNeverCloseCharacter();
  void onPopulateNeverClose();
  void onAddHiddenCharacter();
  void onPopulateHiddenCharacters();
  void onGlobalSearchChanged(const QString &text);
  void onResetAppearanceDefaults();
  void onResetHotkeysDefaults();
  void onResetBehaviorDefaults();
  void onResetNonEVEDefaults();
  void onResetCombatMessagesDefaults();
  void onAspectRatio16_9();
  void onAspectRatio21_9();
  void onAspectRatio4_3();
  void onAddCharacterColor();
  void onPopulateCharacterColors();
  void onAssignUniqueColors();
  void onCharacterColorButtonClicked();
  void onCustomSystemColors();
  void onAddThumbnailSize();
  void onPopulateThumbnailSizes();
  void onRemoveThumbnailSize();
  void onResetThumbnailSizesToDefault();
  void onAddCustomName();
  void onPopulateCustomNames();
  void onBrowseLegacySettings();
  void onCopyAllLegacySettings();
  void onImportEVEXAsProfile();
  void onAddProcessName();
  void onPopulateProcessNames();
  void onAddProcessThumbnailSize();
  void onPopulateProcessThumbnailSizes();
  void onResetProcessThumbnailSizesToDefault();
  void onBrowseChatLogDirectory();
  void onBrowseGameLogDirectory();
  void onSetNotLoggedInPosition();
  void onSetClientLocations();
  void onCheckForUpdates();
  void onDownloadUpdate();
  void onBugReportClicked();

  void onProfileChanged(int index);
  void onNewProfile();
  void onCloneProfile();
  void onRenameProfile();
  void onDeleteProfile();
  void onTestOverlays();
  void onHotkeyChanged();
  void validateAllHotkeys();

private:
  void setupUI();
  void createCategoryList();
  void createAppearancePage();
  void createHotkeysPage();
  void createBehaviorPage();
  void createNonEVEThumbnailsPage();
  void createPerformancePage();
  void createDataSourcesPage();
  void createLegacySettingsPage();
  void createAboutPage();

  void loadSettings();
  void saveSettings();
  void setupBindings();

  QWidget *createColorButton(const QColor &color);
  void updateColorButton(QPushButton *button, const QColor &color);
  void performGlobalSearch(const QString &searchText);
  void tagWidget(QWidget *widget, const QStringList &keywords);
  QWidget *createThumbnailSizeFormRow(const QString &characterName = "",
                                      int width = 0, int height = 0);
  void updateThumbnailSizesScrollHeight();
  QWidget *createProcessThumbnailSizeFormRow(const QString &processName = "",
                                             int width = 0, int height = 0);
  void updateProcessThumbnailSizesScrollHeight();
  QWidget *createCustomNameFormRow(const QString &characterName = "",
                                   const QString &customName = "");
  void updateCustomNamesScrollHeight();
  QWidget *createCharacterHotkeyFormRow(const QString &characterName = "",
                                        int vkCode = 0, int modifiers = 0);
  void updateCharacterHotkeysScrollHeight();
  QWidget *createCycleGroupFormRow(const QString &groupName = "",
                                   int backwardKey = 0, int backwardMods = 0,
                                   int forwardKey = 0, int forwardMods = 0,
                                   const QString &characters = "",
                                   bool includeNotLoggedIn = false,
                                   bool noLoop = false);
  void updateCycleGroupsScrollHeight();
  QWidget *createCharacterColorFormRow(const QString &characterName = "",
                                       const QColor &color = QColor("#00FFFF"));
  void updateCharacterColorsScrollHeight();
  QWidget *createNeverMinimizeFormRow(const QString &characterName = "");
  void updateNeverMinimizeScrollHeight();
  QWidget *createNeverCloseFormRow(const QString &characterName = "");
  void updateNeverCloseScrollHeight();
  QWidget *createHiddenCharactersFormRow(const QString &characterName = "");
  void updateHiddenCharactersScrollHeight();
  QWidget *createProcessNamesFormRow(const QString &processName = "");
  void updateProcessNamesScrollHeight();

  void parseLegacySettingsFile(const QString &filePath);
  void parseEVEXPreviewFile(const QVariantMap &rootMap);
  void displayEVEXProfile(const QString &profileName, QWidget *container);
  void displayLegacySettings();
  void displayLegacySettingsInternal(QLayout *targetLayout);
  QWidget *createLegacyCategoryWidget(const QString &categoryName,
                                      const QVariantMap &settings);
  void copyLegacySettings(const QString &category, const QVariantMap &settings);
  void showFeedback(QWidget *nearWidget, const QString &message);

  void updateTableVisibility(QTableWidget *table);

  void createProfileToolbar();
  void updateProfileDropdown();
  void switchProfile(const QString &profileName);
  bool confirmProfileSwitch();
  int compareVersions(const QString &version1, const QString &version2);

  struct HotkeyConflict {
    QString existingName;
    QString conflictingName;
    HotkeyBinding binding;
  };

  QVector<HotkeyConflict> checkHotkeyConflicts();
  void updateHotkeyConflictVisuals();
  void clearHotkeyConflictVisuals();
  QString getHotkeyDescription(HotkeyCapture *capture, const QString &baseName);
  void showConflictDialog(const QVector<HotkeyConflict> &conflicts);
  void setConflictBorder(HotkeyCapture *capture, bool hasConflict);

  QSet<HotkeyBinding> m_conflictingHotkeys;

  QListWidget *m_categoryList;
  QStackedWidget *m_stackedWidget;
  QPushButton *m_okButton;
  QPushButton *m_cancelButton;
  QPushButton *m_applyButton;
  QPushButton *m_testOverlaysButton;
  QPushButton *m_bugReportButton;
  QLineEdit *m_globalSearchBox;

  ThumbnailWidget *m_testThumbnail;
  ThumbnailWidget *m_notLoggedInReferenceThumbnail;

  QComboBox *m_profileCombo;
  QPushButton *m_newProfileButton;
  QPushButton *m_cloneProfileButton;
  QPushButton *m_renameProfileButton;
  QPushButton *m_deleteProfileButton;
  QPushButton *m_setHotkeyButton;
  HotkeyCapture *m_profileHotkeyCapture;
  QPushButton *m_clearProfileHotkeyButton;
  bool m_skipProfileSwitchConfirmation;

  QCheckBox *m_alwaysOnTopCheck;
  QComboBox *m_switchModeCombo;
  QLabel *m_switchModeLabel;
  QComboBox *m_dragButtonCombo;
  QLabel *m_dragButtonLabel;
  QCheckBox *m_rememberPositionsCheck;
  QCheckBox *m_preserveLogoutPositionsCheck;
  QCheckBox *m_enableSnappingCheck;
  QSpinBox *m_snapDistanceSpin;
  QLabel *m_snapDistanceLabel;
  QCheckBox *m_lockPositionsCheck;

  QSpinBox *m_thumbnailWidthSpin;
  QSpinBox *m_thumbnailHeightSpin;
  QPushButton *m_aspectRatio16_9Button;
  QPushButton *m_aspectRatio21_9Button;
  QPushButton *m_aspectRatio4_3Button;
  QSpinBox *m_opacitySpin;
  QCheckBox *m_showNotLoggedInClientsCheck;
  QLabel *m_notLoggedInPositionLabel;
  QPushButton *m_setNotLoggedInPositionButton;
  QLabel *m_notLoggedInStackModeLabel;
  QComboBox *m_notLoggedInStackModeCombo;
  QCheckBox *m_showNotLoggedInOverlayCheck;

  QCheckBox *m_showNonEVEOverlayCheck;

  QScrollArea *m_processNamesScrollArea;
  QWidget *m_processNamesContainer;
  QVBoxLayout *m_processNamesLayout;
  QPushButton *m_addProcessNameButton;
  QPushButton *m_populateProcessNamesButton;

  QScrollArea *m_processThumbnailSizesScrollArea;
  QWidget *m_processThumbnailSizesContainer;
  QVBoxLayout *m_processThumbnailSizesLayout;
  QPushButton *m_addProcessThumbnailSizeButton;
  QPushButton *m_populateProcessThumbnailSizesButton;
  QPushButton *m_resetProcessThumbnailSizesButton;

  QCheckBox *m_minimizeInactiveCheck;
  QSpinBox *m_minimizeDelaySpin;
  QLabel *m_minimizeDelayLabel;
  QLabel *m_neverMinimizeLabel;
  QLabel *m_neverMinimizeInfoLabel;
  QScrollArea *m_neverMinimizeScrollArea;
  QWidget *m_neverMinimizeContainer;
  QVBoxLayout *m_neverMinimizeLayout;
  QPushButton *m_addNeverMinimizeButton;
  QPushButton *m_populateNeverMinimizeButton;
  QLabel *m_neverCloseLabel;
  QLabel *m_neverCloseInfoLabel;
  QScrollArea *m_neverCloseScrollArea;
  QWidget *m_neverCloseContainer;
  QVBoxLayout *m_neverCloseLayout;
  QPushButton *m_addNeverCloseButton;
  QPushButton *m_populateNeverCloseButton;
  QScrollArea *m_hiddenCharactersScrollArea;
  QWidget *m_hiddenCharactersContainer;
  QVBoxLayout *m_hiddenCharactersLayout;
  QPushButton *m_addHiddenCharacterButton;
  QPushButton *m_populateHiddenCharactersButton;
  QCheckBox *m_saveClientLocationCheck;
  QPushButton *m_setClientLocationsButton;
  QLabel *m_setClientLocationsLabel;
  QCheckBox *m_highlightActiveCheck;
  QCheckBox *m_hideActiveClientThumbnailCheck;
  QCheckBox *m_hideThumbnailsWhenEVENotFocusedCheck;
  QPushButton *m_highlightColorButton;
  QLabel *m_highlightColorLabel;
  QSpinBox *m_highlightBorderWidthSpin;
  QLabel *m_highlightBorderWidthLabel;
  QComboBox *m_activeBorderStyleCombo;
  QLabel *m_activeBorderStyleLabel;
  QColor m_highlightColor;
  QCheckBox *m_showInactiveBordersCheck;
  QPushButton *m_inactiveBorderColorButton;
  QLabel *m_inactiveBorderColorLabel;
  QSpinBox *m_inactiveBorderWidthSpin;
  QLabel *m_inactiveBorderWidthLabel;
  QComboBox *m_inactiveBorderStyleCombo;
  QLabel *m_inactiveBorderStyleLabel;
  QColor m_inactiveBorderColor;
  QScrollArea *m_characterColorsScrollArea;
  QWidget *m_characterColorsContainer;
  QVBoxLayout *m_characterColorsLayout;
  QPushButton *m_addCharacterColorButton;
  QPushButton *m_populateCharacterColorsButton;
  QPushButton *m_assignUniqueColorsButton;

  QScrollArea *m_thumbnailSizesScrollArea;
  QWidget *m_thumbnailSizesContainer;
  QVBoxLayout *m_thumbnailSizesLayout;
  QPushButton *m_addThumbnailSizeButton;
  QPushButton *m_populateThumbnailSizesButton;
  QPushButton *m_resetThumbnailSizesButton;

  QScrollArea *m_customNamesScrollArea;
  QWidget *m_customNamesContainer;
  QVBoxLayout *m_customNamesLayout;
  QPushButton *m_addCustomNameButton;
  QPushButton *m_populateCustomNamesButton;

  QCheckBox *m_showCharacterNameCheck;
  QPushButton *m_characterNameColorButton;
  QLabel *m_characterNameColorLabel;
  QComboBox *m_characterNamePositionCombo;
  QLabel *m_characterNamePositionLabel;
  QPushButton *m_characterNameFontButton;
  QLabel *m_characterNameFontLabel;
  QLabel *m_characterNameOffsetXLabel;
  QSlider *m_characterNameOffsetXSlider;
  QLabel *m_characterNameOffsetXValue;
  QLabel *m_characterNameOffsetYLabel;
  QSlider *m_characterNameOffsetYSlider;
  QLabel *m_characterNameOffsetYValue;

  QCheckBox *m_showSystemNameCheck;
  QCheckBox *m_uniqueSystemColorsCheck;
  QPushButton *m_systemNameColorButton;
  QLabel *m_systemNameColorLabel;
  QComboBox *m_systemNamePositionCombo;
  QLabel *m_systemNamePositionLabel;
  QPushButton *m_systemNameFontButton;
  QLabel *m_systemNameFontLabel;
  QLabel *m_customSystemColorsLabel;
  QPushButton *m_customSystemColorsButton;
  QLabel *m_systemNameOffsetXLabel;
  QSlider *m_systemNameOffsetXSlider;
  QLabel *m_systemNameOffsetXValue;
  QLabel *m_systemNameOffsetYLabel;
  QSlider *m_systemNameOffsetYSlider;
  QLabel *m_systemNameOffsetYValue;

  QCheckBox *m_showBackgroundCheck;
  QPushButton *m_backgroundColorButton;
  QLabel *m_backgroundColorLabel;
  QSpinBox *m_backgroundOpacitySpin;
  QLabel *m_backgroundOpacityLabel;
  QColor m_characterNameColor;
  QColor m_systemNameColor;
  QColor m_backgroundColor;

  QPushButton *m_browseLegacyButton;
  QPushButton *m_copyAllLegacyButton;
  QPushButton *m_importEVEXButton;
  QLabel *m_legacyFilePathLabel;
  QWidget *m_legacySettingsContainer;
  QVBoxLayout *m_legacySettingsLayout;
  QString m_legacyFilePath;

  QCheckBox *m_enableChatLogMonitoringCheck;
  QLineEdit *m_chatLogDirectoryEdit;
  QPushButton *m_chatLogBrowseButton;
  QLabel *m_chatLogDirectoryLabel;
  QCheckBox *m_enableGameLogMonitoringCheck;
  QLineEdit *m_gameLogDirectoryEdit;
  QPushButton *m_gameLogBrowseButton;
  QLabel *m_gameLogDirectoryLabel;

  QCheckBox *m_showCombatMessagesCheck;
  QComboBox *m_combatMessagePositionCombo;
  QLabel *m_combatMessagePositionLabel;
  QPushButton *m_combatMessageFontButton;
  QLabel *m_combatMessageFontLabel;
  QLabel *m_combatMessageOffsetXLabel;
  QSlider *m_combatMessageOffsetXSlider;
  QLabel *m_combatMessageOffsetXValue;
  QLabel *m_combatMessageOffsetYLabel;
  QSlider *m_combatMessageOffsetYSlider;
  QLabel *m_combatMessageOffsetYValue;
  QCheckBox *m_combatEventFleetInviteCheck;
  QCheckBox *m_combatEventFollowWarpCheck;
  QCheckBox *m_combatEventRegroupCheck;
  QCheckBox *m_combatEventCompressionCheck;
  QCheckBox *m_combatEventDecloakCheck;
  QCheckBox *m_combatEventCrystalBrokeCheck;
  QCheckBox *m_combatEventConvoRequestCheck;

  QCheckBox *m_combatEventMiningStopCheck;
  QSpinBox *m_miningTimeoutSpin;

  QMap<QString, QPushButton *> m_eventColorButtons;
  QMap<QString, QSpinBox *> m_eventDurationSpins;
  QMap<QString, QCheckBox *> m_eventBorderCheckBoxes;
  QMap<QString, QComboBox *> m_eventBorderStyleCombos;
  QMap<QString, QLabel *> m_eventDurationLabels;
  QMap<QString, QLabel *> m_eventColorLabels;
  QMap<QString, QLabel *> m_eventBorderStyleLabels;
  QMap<QString, QCheckBox *> m_eventSuppressFocusedCheckBoxes;
  QMap<QString, QCheckBox *> m_eventSoundCheckBoxes;
  QMap<QString, QLabel *> m_eventSoundFileLabels;
  QMap<QString, QPushButton *> m_eventSoundFileButtons;
  QMap<QString, QPushButton *> m_eventSoundPlayButtons;
  QMap<QString, QLabel *> m_eventSoundVolumeLabels;
  QMap<QString, QSlider *> m_eventSoundVolumeSliders;
  QMap<QString, QLabel *> m_eventSoundVolumeValueLabels;
  QLabel *m_miningTimeoutLabel;

  QVariantMap m_legacySettings;
  QVariantMap m_evexProfiles;
  QVariantMap m_evexGlobalSettings;
  QString m_currentEVEXProfileName;

  QScrollArea *m_characterHotkeysScrollArea;
  QWidget *m_characterHotkeysContainer;
  QVBoxLayout *m_characterHotkeysLayout;
  QPushButton *m_addCharacterButton;
  QPushButton *m_populateCharactersButton;
  QScrollArea *m_cycleGroupsScrollArea;
  QWidget *m_cycleGroupsContainer;
  QVBoxLayout *m_cycleGroupsLayout;
  QPushButton *m_addGroupButton;
  HotkeyCapture *m_suspendHotkeyCapture;
  HotkeyCapture *m_notLoggedInForwardCapture;
  HotkeyCapture *m_notLoggedInBackwardCapture;
  HotkeyCapture *m_nonEVEForwardCapture;
  HotkeyCapture *m_nonEVEBackwardCapture;
  HotkeyCapture *m_closeAllClientsCapture;
  HotkeyCapture *m_minimizeAllClientsCapture;
  HotkeyCapture *m_toggleThumbnailsVisibilityCapture;
  HotkeyCapture *m_cycleProfileForwardCapture;
  HotkeyCapture *m_cycleProfileBackwardCapture;
  QCheckBox *m_wildcardHotkeysCheck;
  QCheckBox *m_hotkeysOnlyWhenEVEFocusedCheck;

  QLabel *m_updateStatusLabel;
  QPushButton *m_checkUpdateButton;
  QPushButton *m_downloadUpdateButton;
  QNetworkAccessManager *m_networkManager;
  QString m_latestReleaseUrl;

  std::unique_ptr<QSoundEffect> m_testSoundEffect;

  SettingBindingManager m_bindingManager;
};

#endif
