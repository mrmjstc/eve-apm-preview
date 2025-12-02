#ifndef CONFIGDIALOG_H
#define CONFIGDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QMap>
#include <QVariantMap>
#include "settingbinding.h"

class HotkeyCapture;
class QVBoxLayout;
class ThumbnailWidget;

class ConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConfigDialog(QWidget *parent = nullptr);
    ~ConfigDialog();

public slots:
    void onExternalProfileSwitch(const QString& profileName);

signals:
    void settingsApplied();

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
    void onGlobalSearchChanged(const QString& text);
    void onResetAppearanceDefaults();
    void onResetHotkeysDefaults();
    void onResetBehaviorDefaults();
    void onResetPerformanceDefaults();
    void onResetCombatMessagesDefaults();
    void onAspectRatio16_9();
    void onAspectRatio21_9();
    void onAspectRatio4_3();
    void onAddCharacterColor();
    void onPopulateCharacterColors();
    void onAssignUniqueColors();
    void onCharacterColorButtonClicked();
    void onBrowseLegacySettings();
    void onCopyAllLegacySettings();
    void onCopyLegacyCategory(const QString& category);
    void onAddProcessName();
    void onPopulateProcessNames();
    void onBrowseChatLogDirectory();
    void onBrowseGameLogDirectory();
    void onSetNotLoggedInPosition();
    
    void onProfileChanged(int index);
    void onNewProfile();
    void onCloneProfile();
    void onRenameProfile();
    void onDeleteProfile();
    void onTestOverlays();

private:
    void setupUI();
    void createCategoryList();
    void createAppearancePage();
    void createHotkeysPage();
    void createBehaviorPage();
    void createPerformancePage();
    void createDataSourcesPage();
    void createLegacySettingsPage();
    void createAboutPage();
    
    void loadSettings();
    void saveSettings();
    void setupBindings();
    
    QWidget* createColorButton(const QColor& color);
    void updateColorButton(QPushButton* button, const QColor& color);
    void performGlobalSearch(const QString& searchText);
    void tagWidget(QWidget* widget, const QStringList& keywords);
    
    void parseLegacySettingsFile(const QString& filePath);
    void parseEVEXPreviewFile(const QVariantMap& rootMap);
    void displayEVEXProfile(const QString& profileName, QWidget* container);
    void displayLegacySettings();
    void displayLegacySettingsInternal(QLayout* targetLayout);
    QWidget* createLegacyCategoryWidget(const QString& categoryName, const QVariantMap& settings);
    void copyLegacySettings(const QString& category, const QVariantMap& settings);
    void showFeedback(QWidget* nearWidget, const QString& message);
    
    void createProfileToolbar();
    void updateProfileDropdown();
    void switchProfile(const QString& profileName);
    bool confirmProfileSwitch();
    
    QListWidget *m_categoryList;
    QStackedWidget *m_stackedWidget;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    QPushButton *m_applyButton;
    QPushButton *m_testOverlaysButton;
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
    QCheckBox *m_rememberPositionsCheck;
    QCheckBox *m_preserveLogoutPositionsCheck;
    QCheckBox *m_enableSnappingCheck;
    QSpinBox *m_snapDistanceSpin;
    QCheckBox *m_lockPositionsCheck;
    
    QSpinBox *m_thumbnailWidthSpin;
    QSpinBox *m_thumbnailHeightSpin;
    QPushButton *m_aspectRatio16_9Button;
    QPushButton *m_aspectRatio21_9Button;
    QPushButton *m_aspectRatio4_3Button;
    QSpinBox *m_refreshIntervalSpin;
    QSpinBox *m_opacitySpin;
    QCheckBox *m_showNotLoggedInClientsCheck;
    QComboBox *m_notLoggedInStackModeCombo;
    QCheckBox *m_showNotLoggedInOverlayCheck;
    
    QCheckBox *m_showNonEVEOverlayCheck;
    
    QTableWidget *m_processNamesTable;
    QPushButton *m_addProcessNameButton;
    QPushButton *m_populateProcessNamesButton;
    
    QCheckBox *m_minimizeInactiveCheck;
    QSpinBox *m_minimizeDelaySpin;
    QTableWidget *m_neverMinimizeTable;
    QPushButton *m_addNeverMinimizeButton;
    QPushButton *m_populateNeverMinimizeButton;
    QCheckBox *m_highlightActiveCheck;
    QCheckBox *m_hideActiveClientThumbnailCheck;
    QPushButton *m_highlightColorButton;
    QSpinBox *m_highlightBorderWidthSpin;
    QColor m_highlightColor;
    QTableWidget *m_characterColorsTable;
    QPushButton *m_addCharacterColorButton;
    QPushButton *m_populateCharacterColorsButton;
    QPushButton *m_assignUniqueColorsButton;
    
    QCheckBox *m_showCharacterNameCheck;
    QPushButton *m_characterNameColorButton;
    QComboBox *m_characterNamePositionCombo;
    QPushButton *m_characterNameFontButton;
    
    QCheckBox *m_showSystemNameCheck;
    QPushButton *m_systemNameColorButton;
    QComboBox *m_systemNamePositionCombo;
    QPushButton *m_systemNameFontButton;
    
    QCheckBox *m_showBackgroundCheck;
    QPushButton *m_backgroundColorButton;
    QSpinBox *m_backgroundOpacitySpin;
    QColor m_characterNameColor;
    QColor m_systemNameColor;
    QColor m_backgroundColor;
    
    QPushButton *m_browseLegacyButton;
    QPushButton *m_copyAllLegacyButton;
    QLabel *m_legacyFilePathLabel;
    QWidget *m_legacySettingsContainer;
    QVBoxLayout *m_legacySettingsLayout;
    QString m_legacyFilePath;
    
    QCheckBox *m_enableChatLogMonitoringCheck;
    QLineEdit *m_chatLogDirectoryEdit;
    QPushButton *m_chatLogBrowseButton;
    QSpinBox *m_fileChangeDebounceSpin;
    QCheckBox *m_enableGameLogMonitoringCheck;
    QLineEdit *m_gameLogDirectoryEdit;
    QPushButton *m_gameLogBrowseButton;
    
    QCheckBox *m_showCombatMessagesCheck;
    QComboBox *m_combatMessagePositionCombo;
    QPushButton *m_combatMessageFontButton;
    QCheckBox *m_combatEventFleetInviteCheck;
    QCheckBox *m_combatEventFollowWarpCheck;
    QCheckBox *m_combatEventRegroupCheck;
    QCheckBox *m_combatEventCompressionCheck;
    QCheckBox *m_combatEventMiningStartCheck;
    QCheckBox *m_combatEventMiningStopCheck;
    QSpinBox *m_miningTimeoutSpin;
    
    QMap<QString, QPushButton*> m_eventColorButtons;
    QMap<QString, QSpinBox*> m_eventDurationSpins;
    QMap<QString, QCheckBox*> m_eventBorderCheckBoxes;
    
    QVariantMap m_legacySettings;
    QVariantMap m_evexProfiles;
    QVariantMap m_evexGlobalSettings;
    
    QTableWidget *m_characterHotkeysTable;
    QPushButton *m_addCharacterButton;
    QPushButton *m_populateCharactersButton;
    QTableWidget *m_cycleGroupsTable;
    QPushButton *m_addGroupButton;
    HotkeyCapture *m_suspendHotkeyCapture;
    HotkeyCapture *m_notLoggedInForwardCapture;
    HotkeyCapture *m_notLoggedInBackwardCapture;
    HotkeyCapture *m_nonEVEForwardCapture;
    HotkeyCapture *m_nonEVEBackwardCapture;
    HotkeyCapture *m_closeAllClientsCapture;
    QCheckBox *m_wildcardHotkeysCheck;
    QCheckBox *m_hotkeysOnlyWhenEVEFocusedCheck;
    
    SettingBindingManager m_bindingManager;
};

#endif 
