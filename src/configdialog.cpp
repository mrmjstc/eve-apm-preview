#include "configdialog.h"
#include "config.h"
#include "hotkeycapture.h"
#include "hotkeymanager.h"
#include "stylesheet.h"
#include "thumbnailwidget.h"
#include "version.h"
#include "windowcapture.h"
#include <Psapi.h>
#include <QColorDialog>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDialog>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSslError>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <Windows.h>
#include <algorithm>

ConfigDialog::ConfigDialog(QWidget *parent)
    : QDialog(parent), m_skipProfileSwitchConfirmation(false),
      m_testThumbnail(nullptr), m_notLoggedInReferenceThumbnail(nullptr),
      m_networkManager(nullptr) {
  setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

  Config::instance().setConfigDialogOpen(true);

  setupUI();
  setupBindings();
  loadSettings();

  updateProfileDropdown();

  if (m_profileHotkeyCapture) {
    QString currentProfile = Config::instance().getCurrentProfileName();
    QString hotkey = Config::instance().getProfileHotkey(currentProfile);
    if (!hotkey.isEmpty()) {
      QMap<QString, QPair<int, int>> allHotkeys =
          Config::instance().getAllProfileHotkeys();
      if (allHotkeys.contains(currentProfile)) {
        QPair<int, int> keyData = allHotkeys[currentProfile];
        int modifiers = keyData.second;
        m_profileHotkeyCapture->setHotkey(
            keyData.first, (modifiers & Qt::ControlModifier) != 0,
            (modifiers & Qt::AltModifier) != 0,
            (modifiers & Qt::ShiftModifier) != 0);
      }
    }
  }

  setWindowTitle("Settings");
  resize(1050, 800);
}

ConfigDialog::~ConfigDialog() {
  if (m_testThumbnail) {
    delete m_testThumbnail;
    m_testThumbnail = nullptr;
  }

  if (m_notLoggedInReferenceThumbnail) {
    delete m_notLoggedInReferenceThumbnail;
    m_notLoggedInReferenceThumbnail = nullptr;
  }

  Config::instance().setConfigDialogOpen(false);
}

void ConfigDialog::setupUI() {
  QVBoxLayout *mainVertLayout = new QVBoxLayout(this);
  mainVertLayout->setSpacing(0);
  mainVertLayout->setContentsMargins(0, 0, 0, 0);

  QHBoxLayout *contentLayout = new QHBoxLayout();
  contentLayout->setSpacing(0);
  contentLayout->setContentsMargins(0, 0, 0, 0);

  m_categoryList = new QListWidget();
  m_categoryList->setMaximumWidth(200);
  m_categoryList->setFrameShape(QFrame::NoFrame);
  m_categoryList->setStyleSheet(StyleSheet::getCategoryListStyleSheet());

  createCategoryList();
  connect(m_categoryList, &QListWidget::currentRowChanged, this,
          &ConfigDialog::onCategoryChanged);

  m_globalSearchBox = new QLineEdit();
  m_globalSearchBox->setPlaceholderText("Search...");
  m_globalSearchBox->setMaximumWidth(200);
  m_globalSearchBox->setStyleSheet(StyleSheet::getSearchBoxStyleSheet());
  connect(m_globalSearchBox, &QLineEdit::textChanged, this,
          &ConfigDialog::onGlobalSearchChanged);

  QVBoxLayout *sidebarLayout = new QVBoxLayout();
  sidebarLayout->setContentsMargins(0, 0, 0, 0);
  sidebarLayout->setSpacing(0);
  sidebarLayout->addWidget(m_categoryList);
  sidebarLayout->addWidget(m_globalSearchBox);

  QWidget *sidebarWidget = new QWidget();
  sidebarWidget->setLayout(sidebarLayout);
  sidebarWidget->setMaximumWidth(200);

  QWidget *rightPanel = new QWidget();
  rightPanel->setStyleSheet(StyleSheet::getRightPanelStyleSheet());
  QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(10, 15, 8, 10);

  m_stackedWidget = new QStackedWidget();
  m_stackedWidget->setStyleSheet(StyleSheet::getStackedWidgetStyleSheet());

  createAppearancePage();
  createHotkeysPage();
  createBehaviorPage();
  createDataSourcesPage();
  createLegacySettingsPage();
  createAboutPage();

  rightLayout->addWidget(m_stackedWidget);

  QHBoxLayout *buttonLayout = new QHBoxLayout();

  m_testOverlaysButton = new QPushButton("Test Thumbnail");
  m_testOverlaysButton->setStyleSheet(StyleSheet::getButtonStyleSheet());
  m_testOverlaysButton->setAutoDefault(false);
  connect(m_testOverlaysButton, &QPushButton::clicked, this,
          &ConfigDialog::onTestOverlays);

  buttonLayout->addWidget(m_testOverlaysButton);
  buttonLayout->addStretch();

  m_okButton = new QPushButton("OK");
  m_cancelButton = new QPushButton("Cancel");
  m_applyButton = new QPushButton("Apply");

  QString buttonStyle = StyleSheet::getButtonStyleSheet();

  m_okButton->setStyleSheet(buttonStyle);
  m_cancelButton->setStyleSheet(buttonStyle);
  m_applyButton->setStyleSheet(buttonStyle);

  m_applyButton->setDefault(true);
  m_okButton->setAutoDefault(false);
  m_cancelButton->setAutoDefault(false);

  connect(m_okButton, &QPushButton::clicked, this, &ConfigDialog::onOkClicked);
  connect(m_cancelButton, &QPushButton::clicked, this,
          &ConfigDialog::onCancelClicked);
  connect(m_applyButton, &QPushButton::clicked, this,
          &ConfigDialog::onApplyClicked);

  buttonLayout->addWidget(m_okButton);
  buttonLayout->addWidget(m_cancelButton);
  buttonLayout->addWidget(m_applyButton);
  buttonLayout->addSpacing(13);

  rightLayout->addLayout(buttonLayout);

  contentLayout->addWidget(sidebarWidget);
  contentLayout->addWidget(rightPanel, 1);

  createProfileToolbar();

  mainVertLayout->addLayout(contentLayout);

  setStyleSheet(StyleSheet::getDialogStyleSheet());
}

void ConfigDialog::createCategoryList() {
  m_categoryList->addItem("Appearance");
  m_categoryList->addItem("Hotkeys");
  m_categoryList->addItem("Behavior");
  m_categoryList->addItem("Data Sources");
  m_categoryList->addItem("Legacy Settings");
  m_categoryList->addItem("About");
  m_categoryList->setCurrentRow(0);
}

void ConfigDialog::createAppearancePage() {
  QWidget *page = new QWidget();
  QScrollArea *scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet(StyleSheet::getScrollAreaStyleSheet());
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  QWidget *scrollWidget = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(scrollWidget);
  layout->setSpacing(10);
  layout->setContentsMargins(0, 0, 5, 0);

  QWidget *sizeSection = new QWidget();
  sizeSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *sizeSectionLayout = new QVBoxLayout(sizeSection);
  sizeSectionLayout->setContentsMargins(16, 12, 16, 12);
  sizeSectionLayout->setSpacing(10);

  tagWidget(sizeSection, {"thumbnail", "size", "width", "height", "opacity",
                          "transparent", "dimension", "pixel"});

  QLabel *sizeHeader = new QLabel("Thumbnail Size");
  sizeHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  sizeSectionLayout->addWidget(sizeHeader);

  QLabel *sizeInfoLabel =
      new QLabel("Adjust the size and opacity of thumbnail windows.");
  sizeInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  sizeSectionLayout->addWidget(sizeInfoLabel);

  QGridLayout *sizeGrid = new QGridLayout();
  sizeGrid->setSpacing(10);
  sizeGrid->setColumnMinimumWidth(0, 120);
  sizeGrid->setColumnStretch(2, 1);

  QLabel *widthLabel = new QLabel("Width:");
  widthLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_thumbnailWidthSpin = new QSpinBox();
  m_thumbnailWidthSpin->setRange(50, 800);
  m_thumbnailWidthSpin->setSuffix(" px");
  m_thumbnailWidthSpin->setFixedWidth(150);

  QHBoxLayout *aspectRatioLayout = new QHBoxLayout();
  aspectRatioLayout->setSpacing(6);

  m_aspectRatio16_9Button = new QPushButton("16:9");
  m_aspectRatio16_9Button->setFixedSize(50, 26);
  m_aspectRatio16_9Button->setToolTip("Set aspect ratio to 16:9 (widescreen)");
  m_aspectRatio16_9Button->setStyleSheet(
      StyleSheet::getAspectRatioButtonStyleSheet());

  m_aspectRatio21_9Button = new QPushButton("21:9");
  m_aspectRatio21_9Button->setFixedSize(50, 26);
  m_aspectRatio21_9Button->setToolTip("Set aspect ratio to 21:9 (ultrawide)");
  m_aspectRatio21_9Button->setStyleSheet(m_aspectRatio16_9Button->styleSheet());

  m_aspectRatio4_3Button = new QPushButton("4:3");
  m_aspectRatio4_3Button->setFixedSize(50, 26);
  m_aspectRatio4_3Button->setToolTip("Set aspect ratio to 4:3 (classic)");
  m_aspectRatio4_3Button->setStyleSheet(m_aspectRatio16_9Button->styleSheet());

  aspectRatioLayout->addWidget(m_aspectRatio16_9Button);
  aspectRatioLayout->addWidget(m_aspectRatio21_9Button);
  aspectRatioLayout->addWidget(m_aspectRatio4_3Button);
  aspectRatioLayout->addStretch();

  connect(m_aspectRatio16_9Button, &QPushButton::clicked, this,
          &ConfigDialog::onAspectRatio16_9);
  connect(m_aspectRatio21_9Button, &QPushButton::clicked, this,
          &ConfigDialog::onAspectRatio21_9);
  connect(m_aspectRatio4_3Button, &QPushButton::clicked, this,
          &ConfigDialog::onAspectRatio4_3);

  QLabel *heightLabel = new QLabel("Height:");
  heightLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_thumbnailHeightSpin = new QSpinBox();
  m_thumbnailHeightSpin->setRange(50, 600);
  m_thumbnailHeightSpin->setSuffix(" px");
  m_thumbnailHeightSpin->setFixedWidth(150);

  QLabel *opacityLabel = new QLabel("Opacity:");
  opacityLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_opacitySpin = new QSpinBox();
  m_opacitySpin->setRange(10, 100);
  m_opacitySpin->setSingleStep(5);
  m_opacitySpin->setSuffix(" %");
  m_opacitySpin->setFixedWidth(150);

  sizeGrid->addWidget(widthLabel, 0, 0, Qt::AlignLeft);
  sizeGrid->addWidget(m_thumbnailWidthSpin, 0, 1);
  sizeGrid->addLayout(aspectRatioLayout, 0, 2);
  sizeGrid->addWidget(heightLabel, 1, 0, Qt::AlignLeft);
  sizeGrid->addWidget(m_thumbnailHeightSpin, 1, 1);
  sizeGrid->addWidget(opacityLabel, 2, 0, Qt::AlignLeft);
  sizeGrid->addWidget(m_opacitySpin, 2, 1);

  sizeSectionLayout->addLayout(sizeGrid);

  layout->addWidget(sizeSection);

  QWidget *thumbnailSizesSection = new QWidget();
  thumbnailSizesSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *thumbnailSizesSectionLayout =
      new QVBoxLayout(thumbnailSizesSection);
  thumbnailSizesSectionLayout->setContentsMargins(16, 12, 16, 12);
  thumbnailSizesSectionLayout->setSpacing(10);

  tagWidget(thumbnailSizesSection,
            {"thumbnail", "size", "custom", "individual", "per-character",
             "width", "height", "dimension"});

  QLabel *thumbnailSizesHeader = new QLabel("Per-Character Thumbnail Sizes");
  thumbnailSizesHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  thumbnailSizesSectionLayout->addWidget(thumbnailSizesHeader);

  QLabel *thumbnailSizesInfoLabel =
      new QLabel("Set custom thumbnail sizes for specific characters. "
                 "Leave empty to use the default size above.");
  thumbnailSizesInfoLabel->setWordWrap(true);
  thumbnailSizesInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  thumbnailSizesSectionLayout->addWidget(thumbnailSizesInfoLabel);

  m_thumbnailSizesTable = new QTableWidget(0, 4);
  m_thumbnailSizesTable->setHorizontalHeaderLabels(
      {"Character Name", "Width (px)", "Height (px)", ""});
  m_thumbnailSizesTable->horizontalHeader()->setStretchLastSection(false);
  m_thumbnailSizesTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  m_thumbnailSizesTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Fixed);
  m_thumbnailSizesTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::Fixed);
  m_thumbnailSizesTable->horizontalHeader()->setSectionResizeMode(
      3, QHeaderView::Fixed);
  m_thumbnailSizesTable->setColumnWidth(1, 100);
  m_thumbnailSizesTable->setColumnWidth(2, 100);
  m_thumbnailSizesTable->setColumnWidth(3, 40);
  m_thumbnailSizesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_thumbnailSizesTable->setMinimumHeight(150);
  m_thumbnailSizesTable->setMaximumHeight(250);
  m_thumbnailSizesTable->verticalHeader()->setDefaultSectionSize(44);
  m_thumbnailSizesTable->setFocusPolicy(Qt::NoFocus);
  m_thumbnailSizesTable->setStyleSheet(StyleSheet::getTableStyleSheet());
  thumbnailSizesSectionLayout->addWidget(m_thumbnailSizesTable);

  QHBoxLayout *thumbnailSizesButtonLayout = new QHBoxLayout();
  m_addThumbnailSizeButton = new QPushButton("Add Character");
  m_populateThumbnailSizesButton =
      new QPushButton("Populate from Open Clients");
  m_resetThumbnailSizesButton = new QPushButton("Reset All to Default");

  QString thumbnailSizesButtonStyle =
      StyleSheet::getSecondaryButtonStyleSheet();

  m_addThumbnailSizeButton->setStyleSheet(thumbnailSizesButtonStyle);
  m_populateThumbnailSizesButton->setStyleSheet(thumbnailSizesButtonStyle);
  m_resetThumbnailSizesButton->setStyleSheet(thumbnailSizesButtonStyle);

  connect(m_addThumbnailSizeButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddThumbnailSize);
  connect(m_populateThumbnailSizesButton, &QPushButton::clicked, this,
          &ConfigDialog::onPopulateThumbnailSizes);
  connect(m_resetThumbnailSizesButton, &QPushButton::clicked, this,
          &ConfigDialog::onResetThumbnailSizesToDefault);

  thumbnailSizesButtonLayout->addWidget(m_addThumbnailSizeButton);
  thumbnailSizesButtonLayout->addWidget(m_populateThumbnailSizesButton);
  thumbnailSizesButtonLayout->addWidget(m_resetThumbnailSizesButton);
  thumbnailSizesButtonLayout->addStretch();

  thumbnailSizesSectionLayout->addLayout(thumbnailSizesButtonLayout);

  layout->addWidget(thumbnailSizesSection);

  QWidget *highlightSection = new QWidget();
  highlightSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *highlightSectionLayout = new QVBoxLayout(highlightSection);
  highlightSectionLayout->setContentsMargins(16, 12, 16, 12);
  highlightSectionLayout->setSpacing(10);

  tagWidget(highlightSection, {"highlight", "active", "window", "border",
                               "color", "cyan", "frame", "outline"});

  QLabel *highlightHeader = new QLabel("Active Window Highlighting");
  highlightHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  highlightSectionLayout->addWidget(highlightHeader);

  QLabel *highlightInfoLabel = new QLabel(
      "Highlight the active EVE client window with a colored border.");
  highlightInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  highlightSectionLayout->addWidget(highlightInfoLabel);

  m_highlightActiveCheck = new QCheckBox("Highlight active window");
  m_highlightActiveCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  highlightSectionLayout->addWidget(m_highlightActiveCheck);

  QGridLayout *highlightGrid = new QGridLayout();
  highlightGrid->setSpacing(10);
  highlightGrid->setColumnMinimumWidth(0, 120);
  highlightGrid->setColumnStretch(2, 1);
  highlightGrid->setContentsMargins(24, 0, 0, 0);

  m_highlightColorLabel = new QLabel("Color:");
  m_highlightColorLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_highlightColorButton = new QPushButton();
  m_highlightColorButton->setFixedSize(150, 32);
  m_highlightColorButton->setCursor(Qt::PointingHandCursor);
  connect(m_highlightColorButton, &QPushButton::clicked, this,
          &ConfigDialog::onColorButtonClicked);

  m_highlightBorderWidthLabel = new QLabel("Border width:");
  m_highlightBorderWidthLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_highlightBorderWidthSpin = new QSpinBox();
  m_highlightBorderWidthSpin->setRange(1, 10);
  m_highlightBorderWidthSpin->setSuffix(" px");
  m_highlightBorderWidthSpin->setFixedWidth(150);
  m_highlightBorderWidthSpin->setStyleSheet(
      StyleSheet::getSpinBoxWithDisabledStyleSheet());

  highlightGrid->addWidget(m_highlightColorLabel, 0, 0, Qt::AlignLeft);
  highlightGrid->addWidget(m_highlightColorButton, 0, 1);
  highlightGrid->addWidget(m_highlightBorderWidthLabel, 1, 0, Qt::AlignLeft);
  highlightGrid->addWidget(m_highlightBorderWidthSpin, 1, 1);

  highlightSectionLayout->addLayout(highlightGrid);

  layout->addWidget(highlightSection);

  connect(m_highlightActiveCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_highlightColorLabel->setEnabled(checked);
            m_highlightColorButton->setEnabled(checked);
            m_highlightBorderWidthLabel->setEnabled(checked);
            m_highlightBorderWidthSpin->setEnabled(checked);
          });

  QWidget *thumbnailVisibilitySection = new QWidget();
  thumbnailVisibilitySection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *thumbnailVisibilitySectionLayout =
      new QVBoxLayout(thumbnailVisibilitySection);
  thumbnailVisibilitySectionLayout->setContentsMargins(16, 12, 16, 12);
  thumbnailVisibilitySectionLayout->setSpacing(10);

  tagWidget(thumbnailVisibilitySection,
            {"hide", "active", "thumbnail", "visibility", "focus", "focused",
             "client", "window", "always", "top", "preview"});

  QLabel *thumbnailVisibilityHeader = new QLabel("Thumbnail Visibility");
  thumbnailVisibilityHeader->setStyleSheet(
      StyleSheet::getSectionHeaderStyleSheet());
  thumbnailVisibilitySectionLayout->addWidget(thumbnailVisibilityHeader);

  QLabel *thumbnailVisibilityInfoLabel =
      new QLabel("Control the visibility and behavior of thumbnail windows.");
  thumbnailVisibilityInfoLabel->setStyleSheet(
      StyleSheet::getInfoLabelStyleSheet());
  thumbnailVisibilitySectionLayout->addWidget(thumbnailVisibilityInfoLabel);

  m_alwaysOnTopCheck = new QCheckBox("Always on top");
  m_alwaysOnTopCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  thumbnailVisibilitySectionLayout->addWidget(m_alwaysOnTopCheck);

  m_hideActiveClientThumbnailCheck =
      new QCheckBox("Hide active client thumbnail");
  m_hideActiveClientThumbnailCheck->setStyleSheet(
      StyleSheet::getCheckBoxStyleSheet());
  thumbnailVisibilitySectionLayout->addWidget(m_hideActiveClientThumbnailCheck);

  layout->addWidget(thumbnailVisibilitySection);

  QWidget *charColorsSection = new QWidget();
  charColorsSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *charColorsSectionLayout = new QVBoxLayout(charColorsSection);
  charColorsSectionLayout->setContentsMargins(16, 12, 16, 12);
  charColorsSectionLayout->setSpacing(10);

  tagWidget(charColorsSection, {"character", "highlight", "color", "custom",
                                "border", "individual", "per-character"});

  QLabel *charColorsHeader = new QLabel("Per-Character Highlight Colors");
  charColorsHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  charColorsSectionLayout->addWidget(charColorsHeader);

  QLabel *charColorsInfoTop =
      new QLabel("Override the default highlight color for specific "
                 "characters. When a character-specific color is set, it will "
                 "be used instead of the global highlight color above.");
  charColorsInfoTop->setWordWrap(true);
  charColorsInfoTop->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  charColorsSectionLayout->addWidget(charColorsInfoTop);

  m_characterColorsTable = new QTableWidget(0, 3);
  m_characterColorsTable->setHorizontalHeaderLabels(
      {"Character Name", "Highlight Color", ""});
  m_characterColorsTable->horizontalHeader()->setStretchLastSection(false);
  m_characterColorsTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  m_characterColorsTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Fixed);
  m_characterColorsTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::Fixed);
  m_characterColorsTable->setColumnWidth(1, 160);
  m_characterColorsTable->setColumnWidth(2, 40);
  m_characterColorsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_characterColorsTable->setMinimumHeight(150);
  m_characterColorsTable->setMaximumHeight(250);
  m_characterColorsTable->verticalHeader()->setDefaultSectionSize(40);
  m_characterColorsTable->setFocusPolicy(Qt::NoFocus);
  m_characterColorsTable->setStyleSheet(StyleSheet::getTableStyleSheet());
  charColorsSectionLayout->addWidget(m_characterColorsTable);

  QHBoxLayout *charColorsButtonLayout = new QHBoxLayout();
  m_addCharacterColorButton = new QPushButton("Add Character");
  m_populateCharacterColorsButton =
      new QPushButton("Populate from Open Clients");
  m_assignUniqueColorsButton = new QPushButton("Assign Unique Colors");

  QString charColorsButtonStyle = StyleSheet::getSecondaryButtonStyleSheet();

  m_addCharacterColorButton->setStyleSheet(charColorsButtonStyle);
  m_populateCharacterColorsButton->setStyleSheet(charColorsButtonStyle);
  m_assignUniqueColorsButton->setStyleSheet(charColorsButtonStyle);

  connect(m_addCharacterColorButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddCharacterColor);
  connect(m_populateCharacterColorsButton, &QPushButton::clicked, this,
          &ConfigDialog::onPopulateCharacterColors);
  connect(m_assignUniqueColorsButton, &QPushButton::clicked, this,
          &ConfigDialog::onAssignUniqueColors);

  charColorsButtonLayout->addWidget(m_addCharacterColorButton);
  charColorsButtonLayout->addWidget(m_populateCharacterColorsButton);
  charColorsButtonLayout->addWidget(m_assignUniqueColorsButton);
  charColorsButtonLayout->addStretch();

  charColorsSectionLayout->addLayout(charColorsButtonLayout);

  layout->addWidget(charColorsSection);

  QWidget *overlaysSection = new QWidget();
  overlaysSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *overlaysSectionLayout = new QVBoxLayout(overlaysSection);
  overlaysSectionLayout->setContentsMargins(16, 12, 16, 12);
  overlaysSectionLayout->setSpacing(16);

  tagWidget(overlaysSection,
            {"overlay", "character", "name", "system", "font", "background",
             "text", "position", "color", "opacity"});

  QLabel *overlaysHeader = new QLabel("Thumbnail Overlays");
  overlaysHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  overlaysSectionLayout->addWidget(overlaysHeader);

  QLabel *overlaysInfoTop =
      new QLabel("Configure text overlays displayed on thumbnail windows.");
  overlaysInfoTop->setWordWrap(true);
  overlaysInfoTop->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  overlaysSectionLayout->addWidget(overlaysInfoTop);

  m_showCharacterNameCheck = new QCheckBox("Show character name");
  m_showCharacterNameCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  overlaysSectionLayout->addWidget(m_showCharacterNameCheck);

  QGridLayout *charGrid = new QGridLayout();
  charGrid->setSpacing(10);
  charGrid->setColumnMinimumWidth(0, 120);
  charGrid->setColumnStretch(2, 1);
  charGrid->setContentsMargins(24, 0, 0, 0);

  m_characterNameColorLabel = new QLabel("Text color:");
  m_characterNameColorLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_characterNameColorButton = new QPushButton();
  m_characterNameColorButton->setFixedSize(150, 32);
  m_characterNameColorButton->setCursor(Qt::PointingHandCursor);
  connect(m_characterNameColorButton, &QPushButton::clicked, this,
          &ConfigDialog::onColorButtonClicked);

  m_characterNamePositionLabel = new QLabel("Position:");
  m_characterNamePositionLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_characterNamePositionCombo = new QComboBox();
  m_characterNamePositionCombo->addItems({"Top Left", "Top Center", "Top Right",
                                          "Bottom Left", "Bottom Center",
                                          "Bottom Right"});
  m_characterNamePositionCombo->setFixedWidth(150);
  m_characterNamePositionCombo->setStyleSheet(
      StyleSheet::getComboBoxWithDisabledStyleSheet());

  m_characterNameFontLabel = new QLabel("Font:");
  m_characterNameFontLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_characterNameFontButton = new QPushButton("Select Font...");
  m_characterNameFontButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  m_characterNameFontButton->setFixedWidth(120);
  connect(m_characterNameFontButton, &QPushButton::clicked, this, [this]() {
    bool ok;
    QFont font =
        QFontDialog::getFont(&ok, Config::instance().characterNameFont(), this,
                             "Select Character Name Font");
    if (ok) {
      Config::instance().setCharacterNameFont(font);
    }
  });

  charGrid->addWidget(m_characterNameColorLabel, 0, 0, Qt::AlignLeft);
  charGrid->addWidget(m_characterNameColorButton, 0, 1);
  charGrid->addWidget(m_characterNamePositionLabel, 1, 0, Qt::AlignLeft);
  charGrid->addWidget(m_characterNamePositionCombo, 1, 1);
  charGrid->addWidget(m_characterNameFontLabel, 2, 0, Qt::AlignLeft);
  charGrid->addWidget(m_characterNameFontButton, 2, 1);

  overlaysSectionLayout->addLayout(charGrid);

  connect(m_showCharacterNameCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_characterNameColorLabel->setEnabled(checked);
            m_characterNameColorButton->setEnabled(checked);
            m_characterNamePositionLabel->setEnabled(checked);
            m_characterNamePositionCombo->setEnabled(checked);
            m_characterNameFontLabel->setEnabled(checked);
            m_characterNameFontButton->setEnabled(checked);
          });

  m_showSystemNameCheck = new QCheckBox("Show system name");
  m_showSystemNameCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  overlaysSectionLayout->addWidget(m_showSystemNameCheck);

  QGridLayout *sysGrid = new QGridLayout();
  sysGrid->setSpacing(10);
  sysGrid->setColumnMinimumWidth(0, 120);
  sysGrid->setColumnStretch(2, 1);
  sysGrid->setContentsMargins(24, 0, 0, 0);

  m_systemNameColorLabel = new QLabel("Text color:");
  m_systemNameColorLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_systemNameColorButton = new QPushButton();
  m_systemNameColorButton->setFixedSize(150, 32);
  m_systemNameColorButton->setCursor(Qt::PointingHandCursor);
  connect(m_systemNameColorButton, &QPushButton::clicked, this,
          &ConfigDialog::onColorButtonClicked);

  m_systemNamePositionLabel = new QLabel("Position:");
  m_systemNamePositionLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_systemNamePositionCombo = new QComboBox();
  m_systemNamePositionCombo->addItems({"Top Left", "Top Center", "Top Right",
                                       "Bottom Left", "Bottom Center",
                                       "Bottom Right"});
  m_systemNamePositionCombo->setFixedWidth(150);
  m_systemNamePositionCombo->setStyleSheet(
      StyleSheet::getComboBoxWithDisabledStyleSheet());

  m_systemNameFontLabel = new QLabel("Font:");
  m_systemNameFontLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_systemNameFontButton = new QPushButton("Select Font...");
  m_systemNameFontButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  m_systemNameFontButton->setFixedWidth(120);
  connect(m_systemNameFontButton, &QPushButton::clicked, this, [this]() {
    bool ok;
    QFont font = QFontDialog::getFont(&ok, Config::instance().systemNameFont(),
                                      this, "Select System Name Font");
    if (ok) {
      Config::instance().setSystemNameFont(font);
    }
  });

  sysGrid->addWidget(m_systemNameColorLabel, 0, 0, Qt::AlignLeft);
  sysGrid->addWidget(m_systemNameColorButton, 0, 1);
  sysGrid->addWidget(m_systemNamePositionLabel, 1, 0, Qt::AlignLeft);
  sysGrid->addWidget(m_systemNamePositionCombo, 1, 1);
  sysGrid->addWidget(m_systemNameFontLabel, 2, 0, Qt::AlignLeft);
  sysGrid->addWidget(m_systemNameFontButton, 2, 1);

  overlaysSectionLayout->addLayout(sysGrid);

  connect(m_showSystemNameCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_systemNameColorLabel->setEnabled(checked);
            m_systemNameColorButton->setEnabled(checked);
            m_systemNamePositionLabel->setEnabled(checked);
            m_systemNamePositionCombo->setEnabled(checked);
            m_systemNameFontLabel->setEnabled(checked);
            m_systemNameFontButton->setEnabled(checked);
          });

  m_showBackgroundCheck = new QCheckBox("Show background");
  m_showBackgroundCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  overlaysSectionLayout->addWidget(m_showBackgroundCheck);

  QGridLayout *bgGrid = new QGridLayout();
  bgGrid->setSpacing(10);
  bgGrid->setColumnMinimumWidth(0, 120);
  bgGrid->setColumnStretch(2, 1);
  bgGrid->setContentsMargins(24, 0, 0, 0);

  m_backgroundColorLabel = new QLabel("Color:");
  m_backgroundColorLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_backgroundColorButton = new QPushButton();
  m_backgroundColorButton->setFixedSize(150, 32);
  m_backgroundColorButton->setCursor(Qt::PointingHandCursor);
  connect(m_backgroundColorButton, &QPushButton::clicked, this,
          &ConfigDialog::onColorButtonClicked);

  m_backgroundOpacityLabel = new QLabel("Opacity:");
  m_backgroundOpacityLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_backgroundOpacitySpin = new QSpinBox();
  m_backgroundOpacitySpin->setRange(0, 100);
  m_backgroundOpacitySpin->setSingleStep(5);
  m_backgroundOpacitySpin->setSuffix(" %");
  m_backgroundOpacitySpin->setFixedWidth(150);
  m_backgroundOpacitySpin->setStyleSheet(
      StyleSheet::getSpinBoxWithDisabledStyleSheet());

  bgGrid->addWidget(m_backgroundColorLabel, 0, 0, Qt::AlignLeft);
  bgGrid->addWidget(m_backgroundColorButton, 0, 1);
  bgGrid->addWidget(m_backgroundOpacityLabel, 1, 0, Qt::AlignLeft);
  bgGrid->addWidget(m_backgroundOpacitySpin, 1, 1);

  overlaysSectionLayout->addLayout(bgGrid);

  connect(m_showBackgroundCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_backgroundColorLabel->setEnabled(checked);
            m_backgroundColorButton->setEnabled(checked);
            m_backgroundOpacityLabel->setEnabled(checked);
            m_backgroundOpacitySpin->setEnabled(checked);
          });

  layout->addWidget(overlaysSection);

  QHBoxLayout *resetLayout = new QHBoxLayout();
  resetLayout->addStretch();
  QPushButton *resetButton = new QPushButton("Reset to Defaults");
  resetButton->setStyleSheet(StyleSheet::getResetButtonStyleSheet());
  connect(resetButton, &QPushButton::clicked, this,
          &ConfigDialog::onResetAppearanceDefaults);
  resetLayout->addWidget(resetButton);
  layout->addLayout(resetLayout);

  layout->addStretch();

  scrollArea->setWidget(scrollWidget);

  QVBoxLayout *pageLayout = new QVBoxLayout(page);
  pageLayout->setContentsMargins(0, 0, 0, 0);
  pageLayout->addWidget(scrollArea);

  m_stackedWidget->addWidget(page);
}

void ConfigDialog::createHotkeysPage() {
  QWidget *page = new QWidget();
  QScrollArea *scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet(StyleSheet::getScrollAreaStyleSheet());

  QWidget *scrollWidget = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(scrollWidget);
  layout->setSpacing(20);
  layout->setContentsMargins(0, 0, 5, 0);

  QString hotkeyButtonStyle = StyleSheet::getHotkeyButtonStyleSheet();

  QWidget *suspendSection = new QWidget();
  suspendSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *suspendSectionLayout = new QVBoxLayout(suspendSection);
  suspendSectionLayout->setContentsMargins(16, 12, 16, 12);
  suspendSectionLayout->setSpacing(10);

  tagWidget(suspendSection,
            {"suspend", "toggle", "disable", "hotkey", "pause", "temporary"});

  QLabel *suspendHeader = new QLabel("Suspend Hotkey");
  suspendHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  suspendSectionLayout->addWidget(suspendHeader);

  QLabel *suspendInfoLabel =
      new QLabel("Press this hotkey to temporarily disable all other hotkeys.");
  suspendInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  suspendSectionLayout->addWidget(suspendInfoLabel);

  QGridLayout *suspendGrid = new QGridLayout();
  suspendGrid->setSpacing(10);
  suspendGrid->setColumnMinimumWidth(0, 120);
  suspendGrid->setColumnStretch(2, 1);

  QLabel *suspendLabel = new QLabel("Toggle hotkeys:");
  suspendLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_suspendHotkeyCapture = new HotkeyCapture();
  m_suspendHotkeyCapture->setFixedWidth(150);

  m_suspendHotkeyCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  QPushButton *clearSuspendButton = new QPushButton("Clear");
  clearSuspendButton->setFixedWidth(60);
  clearSuspendButton->setStyleSheet(hotkeyButtonStyle);
  connect(clearSuspendButton, &QPushButton::clicked,
          [this]() { m_suspendHotkeyCapture->clearHotkey(); });

  suspendGrid->addWidget(suspendLabel, 0, 0, Qt::AlignLeft);
  suspendGrid->addWidget(m_suspendHotkeyCapture, 0, 1);
  suspendGrid->addWidget(clearSuspendButton, 0, 2, Qt::AlignLeft);

  suspendSectionLayout->addLayout(suspendGrid);

  layout->addWidget(suspendSection);

  QWidget *closeAllSection = new QWidget();
  closeAllSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *closeAllSectionLayout = new QVBoxLayout(closeAllSection);
  closeAllSectionLayout->setContentsMargins(16, 12, 16, 12);
  closeAllSectionLayout->setSpacing(10);

  tagWidget(closeAllSection, {"close", "all", "clients", "exit", "quit",
                              "shutdown", "hotkey", "keyboard", "shortcut"});

  QLabel *closeAllHeader = new QLabel("Close All Clients");
  closeAllHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  closeAllSectionLayout->addWidget(closeAllHeader);

  QLabel *closeAllInfoLabel =
      new QLabel("Hotkey to close all EVE client windows at once.");
  closeAllInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  closeAllSectionLayout->addWidget(closeAllInfoLabel);

  QGridLayout *closeAllGrid = new QGridLayout();
  closeAllGrid->setHorizontalSpacing(10);
  closeAllGrid->setVerticalSpacing(8);
  closeAllGrid->setColumnMinimumWidth(0, 120);
  closeAllGrid->setColumnStretch(2, 1);

  QLabel *closeAllLabel = new QLabel("Close all:");
  closeAllLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_closeAllClientsCapture = new HotkeyCapture();
  m_closeAllClientsCapture->setFixedWidth(150);
  m_closeAllClientsCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  QPushButton *clearCloseAllButton = new QPushButton("Clear");
  clearCloseAllButton->setFixedWidth(60);
  clearCloseAllButton->setStyleSheet(hotkeyButtonStyle);
  connect(clearCloseAllButton, &QPushButton::clicked,
          [this]() { m_closeAllClientsCapture->clearHotkey(); });

  closeAllGrid->addWidget(closeAllLabel, 0, 0, Qt::AlignLeft);
  closeAllGrid->addWidget(m_closeAllClientsCapture, 0, 1);
  closeAllGrid->addWidget(clearCloseAllButton, 0, 2, Qt::AlignLeft);

  closeAllSectionLayout->addLayout(closeAllGrid);

  layout->addWidget(closeAllSection);

  QWidget *charHotkeysSection = new QWidget();
  charHotkeysSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *charHotkeysSectionLayout = new QVBoxLayout(charHotkeysSection);
  charHotkeysSectionLayout->setContentsMargins(16, 12, 16, 12);
  charHotkeysSectionLayout->setSpacing(10);

  tagWidget(charHotkeysSection,
            {"character", "hotkey", "switch", "activate", "client", "keyboard",
             "shortcut", "f1", "f2", "f3"});

  QLabel *charHotkeysHeader = new QLabel("Character Hotkeys");
  charHotkeysHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  charHotkeysSectionLayout->addWidget(charHotkeysHeader);

  QLabel *charInfoLabel = new QLabel(
      "Assign hotkeys to instantly switch to specific character windows.");
  charInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  charHotkeysSectionLayout->addWidget(charInfoLabel);

  m_characterHotkeysTable = new QTableWidget(0, 3);
  m_characterHotkeysTable->setHorizontalHeaderLabels(
      {"Character Name", "Hotkey", ""});
  m_characterHotkeysTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  m_characterHotkeysTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Fixed);
  m_characterHotkeysTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::Fixed);
  m_characterHotkeysTable->setColumnWidth(1, 200);
  m_characterHotkeysTable->setColumnWidth(2, 40);
  m_characterHotkeysTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_characterHotkeysTable->setMinimumHeight(150);
  m_characterHotkeysTable->setMaximumHeight(250);
  m_characterHotkeysTable->verticalHeader()->setDefaultSectionSize(40);
  m_characterHotkeysTable->setFocusPolicy(Qt::NoFocus);
  m_characterHotkeysTable->setStyleSheet(StyleSheet::getTableStyleSheet());
  charHotkeysSectionLayout->addWidget(m_characterHotkeysTable);

  QHBoxLayout *charButtonLayout = new QHBoxLayout();
  m_addCharacterButton = new QPushButton("Add Character");
  m_populateCharactersButton = new QPushButton("Populate from Open Clients");

  m_addCharacterButton->setStyleSheet(hotkeyButtonStyle);
  m_populateCharactersButton->setStyleSheet(hotkeyButtonStyle);

  connect(m_addCharacterButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddCharacterHotkey);
  connect(m_populateCharactersButton, &QPushButton::clicked, this,
          &ConfigDialog::onPopulateFromOpenWindows);

  charButtonLayout->addWidget(m_addCharacterButton);
  charButtonLayout->addWidget(m_populateCharactersButton);
  charButtonLayout->addStretch();

  charHotkeysSectionLayout->addLayout(charButtonLayout);
  layout->addWidget(charHotkeysSection);

  QWidget *cycleGroupsSection = new QWidget();
  cycleGroupsSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *cycleGroupsSectionLayout = new QVBoxLayout(cycleGroupsSection);
  cycleGroupsSectionLayout->setContentsMargins(16, 12, 16, 12);
  cycleGroupsSectionLayout->setSpacing(10);

  tagWidget(cycleGroupsSection, {"cycle", "group", "forward", "backward",
                                 "rotate", "tab", "ctrl", "shift"});

  QLabel *cycleGroupsHeader = new QLabel("Group Hotkeys");
  cycleGroupsHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  cycleGroupsSectionLayout->addWidget(cycleGroupsHeader);

  QLabel *cycleInfoLabel =
      new QLabel("Create groups of characters to cycle through with forward "
                 "and backward hotkeys.");
  cycleInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  cycleGroupsSectionLayout->addWidget(cycleInfoLabel);

  m_cycleGroupsTable = new QTableWidget(0, 7);
  m_cycleGroupsTable->setHorizontalHeaderLabels(
      {"Group Name", "Characters", "Forward Key", "Backward Key",
       "Inc. Not Logged In", "Don't Loop", ""});
  m_cycleGroupsTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Interactive);
  m_cycleGroupsTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Stretch);
  m_cycleGroupsTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::Fixed);
  m_cycleGroupsTable->horizontalHeader()->setSectionResizeMode(
      3, QHeaderView::Fixed);
  m_cycleGroupsTable->horizontalHeader()->setSectionResizeMode(
      4, QHeaderView::Fixed);
  m_cycleGroupsTable->horizontalHeader()->setSectionResizeMode(
      5, QHeaderView::Fixed);
  m_cycleGroupsTable->horizontalHeader()->setSectionResizeMode(
      6, QHeaderView::Fixed);
  m_cycleGroupsTable->setColumnWidth(2, 140);
  m_cycleGroupsTable->setColumnWidth(3, 140);
  m_cycleGroupsTable->setColumnWidth(4, 120);
  m_cycleGroupsTable->setColumnWidth(5, 100);
  m_cycleGroupsTable->setColumnWidth(6, 40);
  m_cycleGroupsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_cycleGroupsTable->setMinimumHeight(150);
  m_cycleGroupsTable->setMaximumHeight(250);
  m_cycleGroupsTable->verticalHeader()->setDefaultSectionSize(40);
  m_cycleGroupsTable->setFocusPolicy(Qt::NoFocus);
  m_cycleGroupsTable->setStyleSheet(StyleSheet::getTableStyleSheet());
  cycleGroupsSectionLayout->addWidget(m_cycleGroupsTable);

  QHBoxLayout *groupButtonLayout = new QHBoxLayout();
  m_addGroupButton = new QPushButton("Add Group");

  m_addGroupButton->setStyleSheet(hotkeyButtonStyle);

  connect(m_addGroupButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddCycleGroup);

  groupButtonLayout->addWidget(m_addGroupButton);
  groupButtonLayout->addStretch();

  cycleGroupsSectionLayout->addLayout(groupButtonLayout);
  layout->addWidget(cycleGroupsSection);

  QWidget *notLoggedInSection = new QWidget();
  notLoggedInSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *notLoggedInSectionLayout = new QVBoxLayout(notLoggedInSection);
  notLoggedInSectionLayout->setContentsMargins(16, 12, 16, 12);
  notLoggedInSectionLayout->setSpacing(10);

  tagWidget(notLoggedInSection, {"not logged in", "login", "cycle",
                                 "not-logged-in", "forward", "backward"});

  QLabel *notLoggedInHeader = new QLabel("Not-Logged-In Cycle Hotkeys");
  notLoggedInHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  notLoggedInSectionLayout->addWidget(notLoggedInHeader);

  QLabel *notLoggedInInfoLabel =
      new QLabel("Dedicated hotkeys to cycle through EVE clients that are not "
                 "yet logged in.");
  notLoggedInInfoLabel->setWordWrap(true);
  notLoggedInInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  notLoggedInSectionLayout->addWidget(notLoggedInInfoLabel);

  QGridLayout *notLoggedInGrid = new QGridLayout();
  notLoggedInGrid->setSpacing(10);
  notLoggedInGrid->setColumnMinimumWidth(0, 120);
  notLoggedInGrid->setColumnStretch(3, 1);

  QLabel *forwardLabel = new QLabel("Cycle forward:");
  forwardLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_notLoggedInForwardCapture = new HotkeyCapture();
  m_notLoggedInForwardCapture->setMinimumWidth(200);

  m_notLoggedInForwardCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  QPushButton *clearNotLoggedInForwardButton = new QPushButton("Clear");
  clearNotLoggedInForwardButton->setFixedWidth(60);
  clearNotLoggedInForwardButton->setStyleSheet(hotkeyButtonStyle);
  connect(clearNotLoggedInForwardButton, &QPushButton::clicked,
          [this]() { m_notLoggedInForwardCapture->clearHotkey(); });

  QLabel *backwardLabel = new QLabel("Cycle backward:");
  backwardLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_notLoggedInBackwardCapture = new HotkeyCapture();
  m_notLoggedInBackwardCapture->setMinimumWidth(200);

  m_notLoggedInBackwardCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  QPushButton *clearNotLoggedInBackwardButton = new QPushButton("Clear");
  clearNotLoggedInBackwardButton->setFixedWidth(60);
  clearNotLoggedInBackwardButton->setStyleSheet(hotkeyButtonStyle);
  connect(clearNotLoggedInBackwardButton, &QPushButton::clicked,
          [this]() { m_notLoggedInBackwardCapture->clearHotkey(); });

  notLoggedInGrid->addWidget(forwardLabel, 0, 0, Qt::AlignLeft);
  notLoggedInGrid->addWidget(m_notLoggedInForwardCapture, 0, 1);
  notLoggedInGrid->addWidget(clearNotLoggedInForwardButton, 0, 2,
                             Qt::AlignLeft);
  notLoggedInGrid->addWidget(backwardLabel, 1, 0, Qt::AlignLeft);
  notLoggedInGrid->addWidget(m_notLoggedInBackwardCapture, 1, 1);
  notLoggedInGrid->addWidget(clearNotLoggedInBackwardButton, 1, 2,
                             Qt::AlignLeft);

  notLoggedInSectionLayout->addLayout(notLoggedInGrid);

  layout->addWidget(notLoggedInSection);

  QWidget *nonEVESection = new QWidget();
  nonEVESection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *nonEVESectionLayout = new QVBoxLayout(nonEVESection);
  nonEVESectionLayout->setContentsMargins(16, 12, 16, 12);
  nonEVESectionLayout->setSpacing(10);

  tagWidget(nonEVESection, {"non-eve", "non eve", "cycle", "other",
                            "applications", "forward", "backward"});

  QLabel *nonEVEHeader = new QLabel("Non-EVE Cycle Hotkeys");
  nonEVEHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  nonEVESectionLayout->addWidget(nonEVEHeader);

  QLabel *nonEVEInfoLabel =
      new QLabel("Dedicated hotkeys to cycle through non-EVE applications "
                 "(other programs added to Extra Previews).");
  nonEVEInfoLabel->setWordWrap(true);
  nonEVEInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  nonEVESectionLayout->addWidget(nonEVEInfoLabel);

  QGridLayout *nonEVEGrid = new QGridLayout();
  nonEVEGrid->setSpacing(10);
  nonEVEGrid->setColumnMinimumWidth(0, 120);
  nonEVEGrid->setColumnStretch(3, 1);

  QLabel *nonEVEForwardLabel = new QLabel("Cycle forward:");
  nonEVEForwardLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_nonEVEForwardCapture = new HotkeyCapture();
  m_nonEVEForwardCapture->setMinimumWidth(200);

  m_nonEVEForwardCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  QPushButton *clearNonEVEForwardButton = new QPushButton("Clear");
  clearNonEVEForwardButton->setFixedWidth(60);
  clearNonEVEForwardButton->setStyleSheet(hotkeyButtonStyle);
  connect(clearNonEVEForwardButton, &QPushButton::clicked,
          [this]() { m_nonEVEForwardCapture->clearHotkey(); });

  QLabel *nonEVEBackwardLabel = new QLabel("Cycle backward:");
  nonEVEBackwardLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_nonEVEBackwardCapture = new HotkeyCapture();
  m_nonEVEBackwardCapture->setMinimumWidth(200);

  m_nonEVEBackwardCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  QPushButton *clearNonEVEBackwardButton = new QPushButton("Clear");
  clearNonEVEBackwardButton->setFixedWidth(60);
  clearNonEVEBackwardButton->setStyleSheet(hotkeyButtonStyle);
  connect(clearNonEVEBackwardButton, &QPushButton::clicked,
          [this]() { m_nonEVEBackwardCapture->clearHotkey(); });

  nonEVEGrid->addWidget(nonEVEForwardLabel, 0, 0, Qt::AlignLeft);
  nonEVEGrid->addWidget(m_nonEVEForwardCapture, 0, 1);
  nonEVEGrid->addWidget(clearNonEVEForwardButton, 0, 2, Qt::AlignLeft);
  nonEVEGrid->addWidget(nonEVEBackwardLabel, 1, 0, Qt::AlignLeft);
  nonEVEGrid->addWidget(m_nonEVEBackwardCapture, 1, 1);
  nonEVEGrid->addWidget(clearNonEVEBackwardButton, 1, 2, Qt::AlignLeft);

  nonEVESectionLayout->addLayout(nonEVEGrid);

  layout->addWidget(nonEVESection);

  QWidget *wildcardSection = new QWidget();
  wildcardSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *wildcardSectionLayout = new QVBoxLayout(wildcardSection);
  wildcardSectionLayout->setContentsMargins(16, 12, 16, 12);
  wildcardSectionLayout->setSpacing(10);

  tagWidget(wildcardSection, {"wildcard", "hotkey", "modifier", "ctrl", "alt",
                              "shift", "extra", "additional"});

  QLabel *wildcardHeader = new QLabel("Wildcard Hotkeys");
  wildcardHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  wildcardSectionLayout->addWidget(wildcardHeader);

  QLabel *wildcardInfoLabel =
      new QLabel("When enabled, hotkeys will work even when additional "
                 "modifier keys are pressed. "
                 "For example, if a hotkey is set to F22, it will also trigger "
                 "when pressing Ctrl+F22, Alt+F22, etc.");
  wildcardInfoLabel->setWordWrap(true);
  wildcardInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  wildcardSectionLayout->addWidget(wildcardInfoLabel);

  m_wildcardHotkeysCheck = new QCheckBox("Enable wildcard hotkeys");
  m_wildcardHotkeysCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  wildcardSectionLayout->addWidget(m_wildcardHotkeysCheck);

  layout->addWidget(wildcardSection);

  QWidget *eveFocusSection = new QWidget();
  eveFocusSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *eveFocusSectionLayout = new QVBoxLayout(eveFocusSection);
  eveFocusSectionLayout->setContentsMargins(16, 12, 16, 12);
  eveFocusSectionLayout->setSpacing(10);

  tagWidget(eveFocusSection,
            {"eve", "focus", "hotkey", "active", "window", "client", "only"});

  QLabel *eveFocusHeader = new QLabel("EVE Client Focus");
  eveFocusHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  eveFocusSectionLayout->addWidget(eveFocusHeader);

  QLabel *eveFocusInfoLabel =
      new QLabel("When enabled, hotkeys will only work when an EVE client "
                 "window is focused. "
                 "This prevents accidental window switching when using other "
                 "applications.");
  eveFocusInfoLabel->setWordWrap(true);
  eveFocusInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  eveFocusSectionLayout->addWidget(eveFocusInfoLabel);

  m_hotkeysOnlyWhenEVEFocusedCheck =
      new QCheckBox("Only process hotkeys when EVE client is focused");
  m_hotkeysOnlyWhenEVEFocusedCheck->setStyleSheet(
      StyleSheet::getCheckBoxStyleSheet());
  eveFocusSectionLayout->addWidget(m_hotkeysOnlyWhenEVEFocusedCheck);

  layout->addWidget(eveFocusSection);

  QHBoxLayout *resetLayout = new QHBoxLayout();
  resetLayout->addStretch();
  QPushButton *resetButton = new QPushButton("Reset to Defaults");
  resetButton->setStyleSheet(StyleSheet::getResetButtonStyleSheet());
  connect(resetButton, &QPushButton::clicked, this,
          &ConfigDialog::onResetHotkeysDefaults);
  resetLayout->addWidget(resetButton);
  layout->addLayout(resetLayout);

  layout->addStretch();

  scrollArea->setWidget(scrollWidget);

  QVBoxLayout *pageLayout = new QVBoxLayout(page);
  pageLayout->setContentsMargins(0, 0, 0, 0);
  pageLayout->addWidget(scrollArea);

  m_stackedWidget->addWidget(page);
}

void ConfigDialog::createBehaviorPage() {
  QWidget *page = new QWidget();
  QScrollArea *scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet(StyleSheet::getScrollAreaStyleSheet());

  QWidget *scrollWidget = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(scrollWidget);
  layout->setSpacing(20);
  layout->setContentsMargins(0, 0, 5, 0);

  QWidget *windowSection = new QWidget();
  windowSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *windowSectionLayout = new QVBoxLayout(windowSection);
  windowSectionLayout->setContentsMargins(16, 12, 16, 12);
  windowSectionLayout->setSpacing(10);

  tagWidget(windowSection, {"window", "desktop", "minimize", "inactive",
                            "delay", "never", "management", "client", "eve",
                            "location", "position", "save", "restore", "move"});

  QLabel *windowHeader = new QLabel("EVE Client Management");
  windowHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  windowSectionLayout->addWidget(windowHeader);

  QLabel *windowInfoLabel = new QLabel(
      "Control how EVE client windows behave when switching between them.");
  windowInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  windowSectionLayout->addWidget(windowInfoLabel);

  m_saveClientLocationCheck =
      new QCheckBox("Save and restore client window locations");
  m_saveClientLocationCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  windowSectionLayout->addWidget(m_saveClientLocationCheck);

  QGridLayout *clientLocationGrid = new QGridLayout();
  clientLocationGrid->setSpacing(10);
  clientLocationGrid->setColumnMinimumWidth(0, 120);
  clientLocationGrid->setColumnStretch(2, 1);
  clientLocationGrid->setContentsMargins(24, 0, 0, 0);

  m_setClientLocationsLabel = new QLabel("Current Positions:");
  m_setClientLocationsLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());

  m_setClientLocationsButton = new QPushButton("Set Positions");
  m_setClientLocationsButton->setFixedSize(150, 32);
  m_setClientLocationsButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  m_setClientLocationsButton->setToolTip(
      "Save the current window positions of all open EVE clients");

  clientLocationGrid->addWidget(m_setClientLocationsLabel, 0, 0, Qt::AlignLeft);
  clientLocationGrid->addWidget(m_setClientLocationsButton, 0, 1);

  windowSectionLayout->addLayout(clientLocationGrid);

  connect(m_setClientLocationsButton, &QPushButton::clicked, this,
          &ConfigDialog::onSetClientLocations);

  connect(m_saveClientLocationCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_setClientLocationsLabel->setEnabled(checked);
            m_setClientLocationsButton->setEnabled(checked);
          });

  windowSectionLayout->addSpacing(10);

  m_minimizeInactiveCheck = new QCheckBox("Minimize inactive clients");
  m_minimizeInactiveCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());

  windowSectionLayout->addWidget(m_minimizeInactiveCheck);

  QGridLayout *minimizeGrid = new QGridLayout();
  minimizeGrid->setSpacing(10);
  minimizeGrid->setColumnMinimumWidth(0, 120);
  minimizeGrid->setColumnStretch(2, 1);
  minimizeGrid->setContentsMargins(24, 0, 0, 0);

  m_minimizeDelayLabel = new QLabel("Minimize delay:");
  m_minimizeDelayLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_minimizeDelaySpin = new QSpinBox();
  m_minimizeDelaySpin->setRange(0, 1000);
  m_minimizeDelaySpin->setSuffix(" ms");
  m_minimizeDelaySpin->setFixedWidth(150);
  m_minimizeDelaySpin->setStyleSheet(
      StyleSheet::getSpinBoxWithDisabledStyleSheet());

  minimizeGrid->addWidget(m_minimizeDelayLabel, 0, 0, Qt::AlignLeft);
  minimizeGrid->addWidget(m_minimizeDelaySpin, 0, 1);

  windowSectionLayout->addLayout(minimizeGrid);

  QLabel *neverMinimizeLabel = new QLabel("Never Minimize Characters:");
  neverMinimizeLabel->setStyleSheet(StyleSheet::getSubLabelStyleSheet());
  windowSectionLayout->addWidget(neverMinimizeLabel);

  m_neverMinimizeTable = new QTableWidget(0, 2);
  m_neverMinimizeTable->setHorizontalHeaderLabels({"Character Name", ""});
  m_neverMinimizeTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  m_neverMinimizeTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Fixed);
  m_neverMinimizeTable->setColumnWidth(1, 40);
  m_neverMinimizeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_neverMinimizeTable->setMinimumHeight(150);
  m_neverMinimizeTable->setMaximumHeight(250);
  m_neverMinimizeTable->verticalHeader()->setDefaultSectionSize(40);
  m_neverMinimizeTable->setFocusPolicy(Qt::NoFocus);
  m_neverMinimizeTable->setStyleSheet(StyleSheet::getTableStyleSheet());
  windowSectionLayout->addWidget(m_neverMinimizeTable);

  QHBoxLayout *neverMinimizeButtonLayout = new QHBoxLayout();
  m_addNeverMinimizeButton = new QPushButton("Add Character");
  m_populateNeverMinimizeButton = new QPushButton("Populate from Open Clients");

  QString neverMinimizeButtonStyle = StyleSheet::getSecondaryButtonStyleSheet();

  m_addNeverMinimizeButton->setStyleSheet(neverMinimizeButtonStyle);
  m_populateNeverMinimizeButton->setStyleSheet(neverMinimizeButtonStyle);

  connect(m_addNeverMinimizeButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddNeverMinimizeCharacter);
  connect(m_populateNeverMinimizeButton, &QPushButton::clicked, this,
          &ConfigDialog::onPopulateNeverMinimize);

  neverMinimizeButtonLayout->addWidget(m_addNeverMinimizeButton);
  neverMinimizeButtonLayout->addWidget(m_populateNeverMinimizeButton);
  neverMinimizeButtonLayout->addStretch();

  windowSectionLayout->addLayout(neverMinimizeButtonLayout);

  layout->addWidget(windowSection);

  connect(m_minimizeInactiveCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_minimizeDelayLabel->setEnabled(checked);
            m_minimizeDelaySpin->setEnabled(checked);
            m_neverMinimizeTable->setEnabled(checked);
            m_addNeverMinimizeButton->setEnabled(checked);
            m_populateNeverMinimizeButton->setEnabled(checked);
          });

  QWidget *positionSection = new QWidget();
  positionSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *positionSectionLayout = new QVBoxLayout(positionSection);
  positionSectionLayout->setContentsMargins(16, 12, 16, 12);
  positionSectionLayout->setSpacing(10);

  tagWidget(positionSection,
            {"position", "remember", "snap", "snapping", "distance", "lock",
             "locked", "placement", "arrange"});

  QLabel *positionHeader = new QLabel("Thumbnail Positioning");
  positionHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  positionSectionLayout->addWidget(positionHeader);

  QLabel *positionInfoLabel =
      new QLabel("Control thumbnail placement and snapping behavior for easier "
                 "organization.");
  positionInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  positionSectionLayout->addWidget(positionInfoLabel);

  m_rememberPositionsCheck = new QCheckBox("Remember thumbnail positions");
  m_rememberPositionsCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  m_preserveLogoutPositionsCheck =
      new QCheckBox("Preserve positions when logged out");
  m_preserveLogoutPositionsCheck->setStyleSheet(
      StyleSheet::getCheckBoxStyleSheet());
  m_lockPositionsCheck = new QCheckBox("Lock thumbnail positions");
  m_lockPositionsCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());

  positionSectionLayout->addWidget(m_rememberPositionsCheck);
  positionSectionLayout->addWidget(m_preserveLogoutPositionsCheck);

  m_enableSnappingCheck = new QCheckBox("Enable snapping");
  m_enableSnappingCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  positionSectionLayout->addWidget(m_enableSnappingCheck);

  QGridLayout *snapGrid = new QGridLayout();
  snapGrid->setSpacing(10);
  snapGrid->setColumnMinimumWidth(0, 120);
  snapGrid->setColumnStretch(2, 1);
  snapGrid->setContentsMargins(24, 0, 0, 0);

  m_snapDistanceLabel = new QLabel("Snapping distance:");
  m_snapDistanceLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_snapDistanceSpin = new QSpinBox();
  m_snapDistanceSpin->setRange(5, 100);
  m_snapDistanceSpin->setSuffix(" px");
  m_snapDistanceSpin->setFixedWidth(150);
  m_snapDistanceSpin->setStyleSheet(
      StyleSheet::getSpinBoxWithDisabledStyleSheet());

  snapGrid->addWidget(m_snapDistanceLabel, 0, 0, Qt::AlignLeft);
  snapGrid->addWidget(m_snapDistanceSpin, 0, 1);

  positionSectionLayout->addLayout(snapGrid);
  positionSectionLayout->addWidget(m_lockPositionsCheck);

  layout->addWidget(positionSection);

  connect(m_enableSnappingCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_snapDistanceLabel->setEnabled(checked);
            m_snapDistanceSpin->setEnabled(checked);
          });

  QWidget *clientFilterSection = new QWidget();
  clientFilterSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *clientFilterSectionLayout = new QVBoxLayout(clientFilterSection);
  clientFilterSectionLayout->setContentsMargins(16, 12, 16, 12);
  clientFilterSectionLayout->setSpacing(10);

  tagWidget(clientFilterSection,
            {"client", "filter", "visibility", "not logged in", "extra",
             "previews", "non-eve", "application"});

  QLabel *clientFilterHeader = new QLabel("Client Filtering & Visibility");
  clientFilterHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  clientFilterSectionLayout->addWidget(clientFilterHeader);

  QLabel *clientFilterInfoLabel =
      new QLabel("Control which windows are shown as thumbnails and how they "
                 "are displayed.");
  clientFilterInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  clientFilterSectionLayout->addWidget(clientFilterInfoLabel);

  QWidget *notLoggedInSection = new QWidget();
  notLoggedInSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *notLoggedInSectionLayout = new QVBoxLayout(notLoggedInSection);
  notLoggedInSectionLayout->setContentsMargins(16, 12, 16, 12);
  notLoggedInSectionLayout->setSpacing(10);

  tagWidget(notLoggedInSection,
            {"not logged in", "login", "position", "stack", "overlay"});

  QLabel *notLoggedInHeader = new QLabel("Not Logged In");
  notLoggedInHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  notLoggedInSectionLayout->addWidget(notLoggedInHeader);

  QLabel *notLoggedInInfoLabel = new QLabel(
      "Configure how EVE clients that are not yet logged in are displayed.");
  notLoggedInInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  notLoggedInSectionLayout->addWidget(notLoggedInInfoLabel);

  m_showNotLoggedInClientsCheck =
      new QCheckBox("Show not-logged-in client thumbnails");
  m_showNotLoggedInClientsCheck->setStyleSheet(
      StyleSheet::getCheckBoxStyleSheet());
  notLoggedInSectionLayout->addWidget(m_showNotLoggedInClientsCheck);

  QGridLayout *notLoggedInGrid = new QGridLayout();
  notLoggedInGrid->setSpacing(10);
  notLoggedInGrid->setColumnMinimumWidth(0, 120);
  notLoggedInGrid->setColumnStretch(2, 1);
  notLoggedInGrid->setContentsMargins(24, 0, 0, 0);

  m_notLoggedInPositionLabel = new QLabel("Position:");
  m_notLoggedInPositionLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());

  m_setNotLoggedInPositionButton = new QPushButton("Set Position");
  m_setNotLoggedInPositionButton->setToolTip(
      "Set custom position for not-logged-in client thumbnails");
  m_setNotLoggedInPositionButton->setStyleSheet(
      StyleSheet::getButtonStyleSheet());
  m_setNotLoggedInPositionButton->setFixedSize(150, 32);

  m_notLoggedInStackModeLabel = new QLabel("Stack mode:");
  m_notLoggedInStackModeLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_notLoggedInStackModeCombo = new QComboBox();
  m_notLoggedInStackModeCombo->addItem("Horizontal");
  m_notLoggedInStackModeCombo->addItem("Vertical");
  m_notLoggedInStackModeCombo->addItem("Overlapped");
  m_notLoggedInStackModeCombo->setFixedWidth(150);
  m_notLoggedInStackModeCombo->setStyleSheet(
      StyleSheet::getComboBoxWithDisabledStyleSheet());

  notLoggedInGrid->addWidget(m_notLoggedInPositionLabel, 0, 0, Qt::AlignLeft);
  notLoggedInGrid->addWidget(m_setNotLoggedInPositionButton, 0, 1);
  notLoggedInGrid->addWidget(m_notLoggedInStackModeLabel, 1, 0, Qt::AlignLeft);
  notLoggedInGrid->addWidget(m_notLoggedInStackModeCombo, 1, 1);

  notLoggedInSectionLayout->addLayout(notLoggedInGrid);

  connect(m_setNotLoggedInPositionButton, &QPushButton::clicked, this,
          &ConfigDialog::onSetNotLoggedInPosition);

  m_showNotLoggedInOverlayCheck =
      new QCheckBox("Show \"Not Logged In\" overlay text");
  m_showNotLoggedInOverlayCheck->setStyleSheet(
      StyleSheet::getCheckBoxStyleSheet());
  notLoggedInSectionLayout->addWidget(m_showNotLoggedInOverlayCheck);

  connect(m_showNotLoggedInClientsCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_notLoggedInPositionLabel->setEnabled(checked);
            m_setNotLoggedInPositionButton->setEnabled(checked);
            m_notLoggedInStackModeLabel->setEnabled(checked);
            m_notLoggedInStackModeCombo->setEnabled(checked);
            m_showNotLoggedInOverlayCheck->setEnabled(checked);
          });

  layout->addWidget(notLoggedInSection);

  m_showNonEVEOverlayCheck =
      new QCheckBox("Show overlay text on non-EVE thumbnails");
  m_showNonEVEOverlayCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  clientFilterSectionLayout->addWidget(m_showNonEVEOverlayCheck);

  QLabel *extraPreviewsSubHeader = new QLabel("Additional Applications:");
  extraPreviewsSubHeader->setStyleSheet(StyleSheet::getSubLabelStyleSheet());
  clientFilterSectionLayout->addWidget(extraPreviewsSubHeader);

  QLabel *extraPreviewsInfoLabel = new QLabel(
      "Add other executable names to create thumbnails for. EVE Online clients "
      "(exefile.exe) are always included. Case-insensitive.");
  extraPreviewsInfoLabel->setWordWrap(true);
  extraPreviewsInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  clientFilterSectionLayout->addWidget(extraPreviewsInfoLabel);

  m_processNamesTable = new QTableWidget(0, 2);
  m_processNamesTable->setObjectName("processNamesTable");
  m_processNamesTable->setHorizontalHeaderLabels(
      {"Additional Executable Names", ""});
  m_processNamesTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  m_processNamesTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Fixed);
  m_processNamesTable->setColumnWidth(1, 40);
  m_processNamesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_processNamesTable->setMinimumHeight(150);
  m_processNamesTable->setMaximumHeight(250);
  m_processNamesTable->verticalHeader()->setDefaultSectionSize(40);
  m_processNamesTable->setFocusPolicy(Qt::NoFocus);
  m_processNamesTable->setStyleSheet(StyleSheet::getTableStyleSheet());
  clientFilterSectionLayout->addWidget(m_processNamesTable);

  QHBoxLayout *processFilterButtonLayout = new QHBoxLayout();
  m_addProcessNameButton = new QPushButton("Add Process");
  m_populateProcessNamesButton = new QPushButton("Populate from Open Windows");

  QString processFilterButtonStyle = StyleSheet::getSecondaryButtonStyleSheet();

  m_addProcessNameButton->setStyleSheet(processFilterButtonStyle);
  m_populateProcessNamesButton->setStyleSheet(processFilterButtonStyle);

  processFilterButtonLayout->addWidget(m_addProcessNameButton);
  processFilterButtonLayout->addWidget(m_populateProcessNamesButton);
  processFilterButtonLayout->addStretch();

  clientFilterSectionLayout->addLayout(processFilterButtonLayout);

  connect(m_addProcessNameButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddProcessName);
  connect(m_populateProcessNamesButton, &QPushButton::clicked, this,
          &ConfigDialog::onPopulateProcessNames);

  layout->addWidget(clientFilterSection);

  QHBoxLayout *resetLayout = new QHBoxLayout();
  resetLayout->addStretch();
  QPushButton *resetButton = new QPushButton("Reset to Defaults");
  resetButton->setStyleSheet(StyleSheet::getResetButtonStyleSheet());
  connect(resetButton, &QPushButton::clicked, this,
          &ConfigDialog::onResetBehaviorDefaults);
  resetLayout->addWidget(resetButton);
  layout->addLayout(resetLayout);

  layout->addStretch();

  scrollArea->setWidget(scrollWidget);

  QVBoxLayout *pageLayout = new QVBoxLayout(page);
  pageLayout->setContentsMargins(0, 0, 0, 0);
  pageLayout->addWidget(scrollArea);

  m_stackedWidget->addWidget(page);
}

void ConfigDialog::createPerformancePage() {}

void ConfigDialog::createDataSourcesPage() {
  QWidget *page = new QWidget();
  QScrollArea *scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet(StyleSheet::getScrollAreaStyleSheet());
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  QWidget *scrollWidget = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(scrollWidget);
  layout->setSpacing(20);
  layout->setContentsMargins(0, 0, 5, 0);

  QWidget *logMonitoringSection = new QWidget();
  logMonitoringSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *logSectionLayout = new QVBoxLayout(logMonitoringSection);
  logSectionLayout->setContentsMargins(16, 12, 16, 12);
  logSectionLayout->setSpacing(10);

  tagWidget(logMonitoringSection,
            {"chat", "game", "log", "monitoring", "system", "character",
             "location", "directory", "path", "combat", "event"});

  QLabel *logHeader = new QLabel("Log Monitoring");
  logHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  logSectionLayout->addWidget(logHeader);

  QLabel *logInfoLabel =
      new QLabel("Monitor EVE Online chat and game logs to automatically "
                 "detect system locations and combat events. "
                 "Chat logs provide character location data, while game logs "
                 "contain fleet notifications and other events.");
  logInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  logInfoLabel->setWordWrap(true);
  logSectionLayout->addWidget(logInfoLabel);

  m_enableChatLogMonitoringCheck = new QCheckBox("Enable chat log monitoring");
  m_enableChatLogMonitoringCheck->setStyleSheet(
      StyleSheet::getCheckBoxStyleSheet());
  logSectionLayout->addWidget(m_enableChatLogMonitoringCheck);

  QHBoxLayout *chatDirLayout = new QHBoxLayout();
  chatDirLayout->setContentsMargins(24, 0, 0, 0);
  m_chatLogDirectoryLabel = new QLabel("Chat log directory:");
  m_chatLogDirectoryLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_chatLogDirectoryLabel->setFixedWidth(150);

  m_chatLogDirectoryEdit = new QLineEdit();
  m_chatLogDirectoryEdit->setStyleSheet(
      StyleSheet::getDialogLineEditStyleSheet());
  m_chatLogDirectoryEdit->setPlaceholderText(
      "Default: " + Config::instance().getDefaultChatLogDirectory());

  m_chatLogBrowseButton = new QPushButton("Browse...");
  m_chatLogBrowseButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  m_chatLogBrowseButton->setFixedWidth(90);
  connect(m_chatLogBrowseButton, &QPushButton::clicked, this,
          &ConfigDialog::onBrowseChatLogDirectory);

  chatDirLayout->addWidget(m_chatLogDirectoryLabel);
  chatDirLayout->addWidget(m_chatLogDirectoryEdit, 1);
  chatDirLayout->addWidget(m_chatLogBrowseButton);
  logSectionLayout->addLayout(chatDirLayout);

  connect(m_enableChatLogMonitoringCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_chatLogDirectoryLabel->setEnabled(checked);
            m_chatLogDirectoryEdit->setEnabled(checked);
            m_chatLogBrowseButton->setEnabled(checked);
          });

  m_enableGameLogMonitoringCheck = new QCheckBox("Enable game log monitoring");
  m_enableGameLogMonitoringCheck->setStyleSheet(
      StyleSheet::getCheckBoxStyleSheet());
  logSectionLayout->addWidget(m_enableGameLogMonitoringCheck);

  QHBoxLayout *gameDirLayout = new QHBoxLayout();
  gameDirLayout->setContentsMargins(24, 0, 0, 0);
  m_gameLogDirectoryLabel = new QLabel("Game log directory:");
  m_gameLogDirectoryLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_gameLogDirectoryLabel->setFixedWidth(150);

  m_gameLogDirectoryEdit = new QLineEdit();
  m_gameLogDirectoryEdit->setStyleSheet(
      StyleSheet::getDialogLineEditStyleSheet());
  m_gameLogDirectoryEdit->setPlaceholderText(
      "Default: " + Config::instance().getDefaultGameLogDirectory());

  m_gameLogBrowseButton = new QPushButton("Browse...");
  m_gameLogBrowseButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  m_gameLogBrowseButton->setFixedWidth(90);
  connect(m_gameLogBrowseButton, &QPushButton::clicked, this,
          &ConfigDialog::onBrowseGameLogDirectory);

  gameDirLayout->addWidget(m_gameLogDirectoryLabel);
  gameDirLayout->addWidget(m_gameLogDirectoryEdit, 1);
  gameDirLayout->addWidget(m_gameLogBrowseButton);
  logSectionLayout->addLayout(gameDirLayout);

  QHBoxLayout *debounceLayout = new QHBoxLayout();
  debounceLayout->setContentsMargins(24, 0, 0, 0);
  QLabel *debounceLabel = new QLabel("File change debounce:");
  debounceLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  debounceLabel->setFixedWidth(150);

  m_fileChangeDebounceSpin = new QSpinBox();
  m_fileChangeDebounceSpin->setRange(10, 5000);
  m_fileChangeDebounceSpin->setSingleStep(10);
  m_fileChangeDebounceSpin->setSuffix(" ms");
  m_fileChangeDebounceSpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());
  m_fileChangeDebounceSpin->setFixedWidth(120);

  debounceLayout->addWidget(debounceLabel);
  debounceLayout->addWidget(m_fileChangeDebounceSpin);
  debounceLayout->addStretch();
  logSectionLayout->addLayout(debounceLayout);

  connect(m_enableGameLogMonitoringCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_gameLogDirectoryLabel->setEnabled(checked);
            m_gameLogDirectoryEdit->setEnabled(checked);
            m_gameLogBrowseButton->setEnabled(checked);
            m_fileChangeDebounceSpin->setEnabled(checked);
          });

  layout->addWidget(logMonitoringSection);

  QWidget *combatSection = new QWidget();
  combatSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *combatSectionLayout = new QVBoxLayout(combatSection);
  combatSectionLayout->setContentsMargins(16, 12, 16, 12);
  combatSectionLayout->setSpacing(10);

  tagWidget(combatSection, {"combat", "event", "message", "notification",
                            "fleet", "warp", "regroup", "compression"});

  QLabel *combatHeader = new QLabel("Combat Event Messages");
  combatHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  combatSectionLayout->addWidget(combatHeader);

  QLabel *combatInfoLabel = new QLabel(
      "Display event notifications from game logs on thumbnail overlays. "
      "Messages include fleet invites, warp follows, regroups, and compression "
      "events.");
  combatInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  combatInfoLabel->setWordWrap(true);
  combatSectionLayout->addWidget(combatInfoLabel);

  m_showCombatMessagesCheck = new QCheckBox("Show combat event messages");
  m_showCombatMessagesCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  combatSectionLayout->addWidget(m_showCombatMessagesCheck);

  QHBoxLayout *positionLayout = new QHBoxLayout();
  positionLayout->setContentsMargins(24, 0, 0, 0);
  m_combatMessagePositionLabel = new QLabel("Message position:");
  m_combatMessagePositionLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_combatMessagePositionLabel->setFixedWidth(150);

  m_combatMessagePositionCombo = new QComboBox();
  m_combatMessagePositionCombo->setStyleSheet(
      StyleSheet::getComboBoxStyleSheet());
  m_combatMessagePositionCombo->addItem("Top Left", 0);
  m_combatMessagePositionCombo->addItem("Top Center", 1);
  m_combatMessagePositionCombo->addItem("Top Right", 2);
  m_combatMessagePositionCombo->addItem("Bottom Left", 3);
  m_combatMessagePositionCombo->addItem("Bottom Center", 4);
  m_combatMessagePositionCombo->addItem("Bottom Right", 5);
  m_combatMessagePositionCombo->setFixedWidth(150);

  positionLayout->addWidget(m_combatMessagePositionLabel);
  positionLayout->addWidget(m_combatMessagePositionCombo);
  positionLayout->addStretch();
  combatSectionLayout->addLayout(positionLayout);

  QHBoxLayout *fontLayout = new QHBoxLayout();
  fontLayout->setContentsMargins(24, 0, 0, 0);
  m_combatMessageFontLabel = new QLabel("Message font:");
  m_combatMessageFontLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_combatMessageFontLabel->setFixedWidth(150);

  m_combatMessageFontButton = new QPushButton("Select Font...");
  m_combatMessageFontButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  m_combatMessageFontButton->setFixedWidth(120);
  connect(m_combatMessageFontButton, &QPushButton::clicked, this, [this]() {
    bool ok;
    QFont font =
        QFontDialog::getFont(&ok, Config::instance().combatMessageFont(), this,
                             "Select Combat Message Font");
    if (ok) {
      Config::instance().setCombatMessageFont(font);
    }
  });

  fontLayout->addWidget(m_combatMessageFontLabel);
  fontLayout->addWidget(m_combatMessageFontButton);
  fontLayout->addStretch();
  combatSectionLayout->addLayout(fontLayout);

  auto createEventRow = [&](const QString &eventType, const QString &label,
                            QCheckBox *&checkbox) {
    QHBoxLayout *rowLayout = new QHBoxLayout();

    checkbox = new QCheckBox(label);
    checkbox->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
    checkbox->setFixedWidth(174);
    rowLayout->addWidget(checkbox);

    QPushButton *colorBtn = new QPushButton();
    colorBtn->setFixedSize(80, 30);
    colorBtn->setCursor(Qt::PointingHandCursor);
    updateColorButton(colorBtn, Qt::white);
    connect(
        colorBtn, &QPushButton::clicked, this, [this, eventType, colorBtn]() {
          QColor currentColor = Config::instance().combatEventColor(eventType);
          QColor color = QColorDialog::getColor(
              currentColor, this, QString("Select %1 Color").arg(eventType));
          if (color.isValid()) {
            updateColorButton(colorBtn, color);
            Config::instance().setCombatEventColor(eventType, color);
          }
        });
    m_eventColorButtons[eventType] = colorBtn;
    rowLayout->addWidget(colorBtn);

    rowLayout->addSpacing(20);

    QLabel *durationLabel = new QLabel("Duration:");
    durationLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
    m_eventDurationLabels[eventType] = durationLabel;
    rowLayout->addWidget(durationLabel);

    QSpinBox *durationSpin = new QSpinBox();
    durationSpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());
    durationSpin->setRange(1, 30);
    durationSpin->setSingleStep(1);
    durationSpin->setSuffix(" sec");
    durationSpin->setFixedWidth(120);
    connect(durationSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [eventType](int value) {
              Config::instance().setCombatEventDuration(eventType,
                                                        value * 1000);
            });
    m_eventDurationSpins[eventType] = durationSpin;
    rowLayout->addWidget(durationSpin);

    rowLayout->addSpacing(20);

    QCheckBox *borderCheck = new QCheckBox("Border");
    borderCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
    borderCheck->setToolTip(
        "Show colored dashed border when this event occurs");
    connect(borderCheck, &QCheckBox::toggled, this, [eventType](bool checked) {
      Config::instance().setCombatEventBorderHighlight(eventType, checked);
    });
    m_eventBorderCheckBoxes[eventType] = borderCheck;
    rowLayout->addWidget(borderCheck);

    rowLayout->addStretch();
    combatSectionLayout->addLayout(rowLayout);
  };

  createEventRow("fleet_invite", "Fleet invites",
                 m_combatEventFleetInviteCheck);
  createEventRow("follow_warp", "Following in warp",
                 m_combatEventFollowWarpCheck);
  createEventRow("regroup", "Regroup commands", m_combatEventRegroupCheck);
  createEventRow("compression", "Compression events",
                 m_combatEventCompressionCheck);
  createEventRow("mining_started", "Mining started",
                 m_combatEventMiningStartCheck);
  createEventRow("mining_stopped", "Mining stopped",
                 m_combatEventMiningStopCheck);

  auto connectEventCheckbox = [this](const QString &eventType,
                                     QCheckBox *checkbox) {
    connect(checkbox, &QCheckBox::toggled, this,
            [this, eventType](bool checked) {
              bool enable = checked && m_showCombatMessagesCheck->isChecked();

              if (m_eventColorButtons.contains(eventType)) {
                m_eventColorButtons[eventType]->setEnabled(enable);
              }
              if (m_eventDurationSpins.contains(eventType)) {
                m_eventDurationSpins[eventType]->setEnabled(enable);
              }
              if (m_eventDurationLabels.contains(eventType)) {
                m_eventDurationLabels[eventType]->setEnabled(enable);
              }
              if (m_eventBorderCheckBoxes.contains(eventType)) {
                m_eventBorderCheckBoxes[eventType]->setEnabled(enable);
              }
            });
  };

  connectEventCheckbox("fleet_invite", m_combatEventFleetInviteCheck);
  connectEventCheckbox("follow_warp", m_combatEventFollowWarpCheck);
  connectEventCheckbox("regroup", m_combatEventRegroupCheck);
  connectEventCheckbox("compression", m_combatEventCompressionCheck);
  connectEventCheckbox("mining_started", m_combatEventMiningStartCheck);
  connectEventCheckbox("mining_stopped", m_combatEventMiningStopCheck);

  QHBoxLayout *miningTimeoutLayout = new QHBoxLayout();
  miningTimeoutLayout->setContentsMargins(24, 0, 0, 0);
  m_miningTimeoutLabel = new QLabel("Mining timeout:");
  m_miningTimeoutLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_miningTimeoutLabel->setFixedWidth(150);

  m_miningTimeoutSpin = new QSpinBox();
  m_miningTimeoutSpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());
  m_miningTimeoutSpin->setRange(15, 120);
  m_miningTimeoutSpin->setSingleStep(5);
  m_miningTimeoutSpin->setSuffix(" sec");
  m_miningTimeoutSpin->setFixedWidth(120);

  miningTimeoutLayout->addWidget(m_miningTimeoutLabel);
  miningTimeoutLayout->addWidget(m_miningTimeoutSpin);
  miningTimeoutLayout->addStretch();
  combatSectionLayout->addLayout(miningTimeoutLayout);

  connect(m_combatEventMiningStopCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            bool enable = checked && m_showCombatMessagesCheck->isChecked();
            m_miningTimeoutSpin->setEnabled(enable);
            m_miningTimeoutLabel->setEnabled(enable);
          });

  connect(m_showCombatMessagesCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_combatMessagePositionCombo->setEnabled(checked);
            m_combatMessagePositionLabel->setEnabled(checked);
            m_combatMessageFontButton->setEnabled(checked);
            m_combatMessageFontLabel->setEnabled(checked);
            m_combatEventFleetInviteCheck->setEnabled(checked);
            m_combatEventFollowWarpCheck->setEnabled(checked);
            m_combatEventRegroupCheck->setEnabled(checked);
            m_combatEventCompressionCheck->setEnabled(checked);
            m_combatEventMiningStartCheck->setEnabled(checked);
            m_combatEventMiningStopCheck->setEnabled(checked);

            bool miningStopChecked = m_combatEventMiningStopCheck->isChecked();
            m_miningTimeoutSpin->setEnabled(checked && miningStopChecked);
            m_miningTimeoutLabel->setEnabled(checked && miningStopChecked);

            QMap<QString, QCheckBox *> eventCheckboxes = {
                {"fleet_invite", m_combatEventFleetInviteCheck},
                {"follow_warp", m_combatEventFollowWarpCheck},
                {"regroup", m_combatEventRegroupCheck},
                {"compression", m_combatEventCompressionCheck},
                {"mining_started", m_combatEventMiningStartCheck},
                {"mining_stopped", m_combatEventMiningStopCheck}};

            for (auto it = eventCheckboxes.constBegin();
                 it != eventCheckboxes.constEnd(); ++it) {
              QString eventType = it.key();
              bool eventEnabled = checked && it.value()->isChecked();

              if (m_eventColorButtons.contains(eventType)) {
                m_eventColorButtons[eventType]->setEnabled(eventEnabled);
              }
              if (m_eventDurationSpins.contains(eventType)) {
                m_eventDurationSpins[eventType]->setEnabled(eventEnabled);
              }
              if (m_eventDurationLabels.contains(eventType)) {
                m_eventDurationLabels[eventType]->setEnabled(eventEnabled);
              }
              if (m_eventBorderCheckBoxes.contains(eventType)) {
                m_eventBorderCheckBoxes[eventType]->setEnabled(eventEnabled);
              }
            }
          });

  layout->addWidget(combatSection);

  QHBoxLayout *resetLayout = new QHBoxLayout();
  resetLayout->addStretch();
  QPushButton *resetButton = new QPushButton("Reset to Defaults");
  resetButton->setStyleSheet(StyleSheet::getResetButtonStyleSheet());
  connect(resetButton, &QPushButton::clicked, this,
          &ConfigDialog::onResetCombatMessagesDefaults);
  resetLayout->addWidget(resetButton);
  layout->addLayout(resetLayout);

  layout->addStretch();

  scrollArea->setWidget(scrollWidget);
  m_stackedWidget->addWidget(scrollArea);
}

void ConfigDialog::createLegacySettingsPage() {
  QWidget *page = new QWidget();
  QScrollArea *scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet(StyleSheet::getScrollAreaStyleSheet());
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  QWidget *scrollWidget = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(scrollWidget);
  layout->setSpacing(20);
  layout->setContentsMargins(0, 0, 5, 0);

  QWidget *browseSection = new QWidget();
  browseSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *browseSectionLayout = new QVBoxLayout(browseSection);
  browseSectionLayout->setContentsMargins(16, 12, 16, 12);
  browseSectionLayout->setSpacing(10);

  QLabel *browseHeader = new QLabel("Legacy Settings File");
  browseHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  browseSectionLayout->addWidget(browseHeader);

  QLabel *infoLabel =
      new QLabel("Import settings from EVE-O/X Preview configuration file."
                 "Select your legacy settings file, then use the Copy buttons "
                 "to import settings into the current configuration.");
  infoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  infoLabel->setWordWrap(true);
  browseSectionLayout->addWidget(infoLabel);

  QHBoxLayout *browseLayout = new QHBoxLayout();
  m_browseLegacyButton = new QPushButton("Browse...");
  m_browseLegacyButton->setStyleSheet(StyleSheet::getButtonStyleSheet());
  m_browseLegacyButton->setFixedWidth(120);
  m_browseLegacyButton->setMaximumHeight(32);
  connect(m_browseLegacyButton, &QPushButton::clicked, this,
          &ConfigDialog::onBrowseLegacySettings);

  m_legacyFilePathLabel = new QLabel("No file selected");
  m_legacyFilePathLabel->setStyleSheet("color: #b0b0b0; font-size: 11pt;");
  m_legacyFilePathLabel->setWordWrap(true);

  m_copyAllLegacyButton = new QPushButton("Copy All");
  m_copyAllLegacyButton->setStyleSheet(StyleSheet::getButtonStyleSheet());
  m_copyAllLegacyButton->setFixedWidth(120);
  m_copyAllLegacyButton->setMaximumHeight(32);
  m_copyAllLegacyButton->setVisible(false);
  m_copyAllLegacyButton->setToolTip("Copy all profiles and settings (EVE-X) or "
                                    "all settings to current profile (EVE-O)");
  connect(m_copyAllLegacyButton, &QPushButton::clicked, this,
          &ConfigDialog::onCopyAllLegacySettings);

  m_importEVEXButton = new QPushButton("Copy Profile");
  m_importEVEXButton->setStyleSheet(StyleSheet::getButtonStyleSheet());
  m_importEVEXButton->setFixedWidth(200);
  m_importEVEXButton->setMaximumHeight(32);
  m_importEVEXButton->setVisible(false);
  m_importEVEXButton->setToolTip(
      "Copy the selected EVE-X profile into your current profile");
  connect(m_importEVEXButton, &QPushButton::clicked, this,
          &ConfigDialog::onImportEVEXAsProfile);

  browseLayout->addWidget(m_browseLegacyButton);
  browseLayout->addWidget(m_legacyFilePathLabel, 1);
  browseLayout->addWidget(m_copyAllLegacyButton);
  browseSectionLayout->addLayout(browseLayout);

  layout->addWidget(browseSection);

  m_legacySettingsContainer = new QWidget();
  m_legacySettingsLayout = new QVBoxLayout(m_legacySettingsContainer);
  m_legacySettingsLayout->setSpacing(15);
  m_legacySettingsLayout->setContentsMargins(0, 0, 0, 0);

  layout->addWidget(m_legacySettingsContainer);

  layout->addStretch();

  scrollArea->setWidget(scrollWidget);
  m_stackedWidget->addWidget(scrollArea);
}

void ConfigDialog::createAboutPage() {
  QWidget *page = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(page);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(20);

  QLabel *iconLabel = new QLabel();
  QPixmap icon(":/bee.png");
  if (!icon.isNull()) {
    iconLabel->setPixmap(
        icon.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);
  }

  QLabel *titleLabel = new QLabel("EVE-APM Preview");
  titleLabel->setStyleSheet(StyleSheet::getAboutTitleStyleSheet());
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  QLabel *versionLabel = new QLabel("Version " APP_VERSION);
  versionLabel->setStyleSheet(StyleSheet::getVersionLabelStyleSheet());
  versionLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(versionLabel);

  layout->addSpacing(15);

  QWidget *updateSection = new QWidget();
  updateSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *updateSectionLayout = new QVBoxLayout(updateSection);
  updateSectionLayout->setContentsMargins(16, 12, 16, 12);
  updateSectionLayout->setSpacing(8);

  QLabel *updateHeader = new QLabel("Update Check");
  updateHeader->setStyleSheet(StyleSheet::getSubsectionHeaderStyleSheet());
  updateSectionLayout->addWidget(updateHeader);

  QHBoxLayout *updateCheckLayout = new QHBoxLayout();

  m_updateStatusLabel = new QLabel(
      "Click 'Check for Updates' to see if a newer version is available.");
  m_updateStatusLabel->setStyleSheet(StyleSheet::getFeatureLabelStyleSheet());
  m_updateStatusLabel->setWordWrap(true);
  updateCheckLayout->addWidget(m_updateStatusLabel, 1);

  m_checkUpdateButton = new QPushButton("Check for Updates");
  m_checkUpdateButton->setStyleSheet(StyleSheet::getButtonStyleSheet());
  connect(m_checkUpdateButton, &QPushButton::clicked, this,
          &ConfigDialog::onCheckForUpdates);
  updateCheckLayout->addWidget(m_checkUpdateButton);
  m_checkUpdateButton->setFixedSize(160, 32);

  updateSectionLayout->addLayout(updateCheckLayout);

  m_downloadUpdateButton = new QPushButton("Download Latest Release");
  m_downloadUpdateButton->setStyleSheet(StyleSheet::getButtonStyleSheet());
  m_downloadUpdateButton->setVisible(false);
  connect(m_downloadUpdateButton, &QPushButton::clicked, this,
          &ConfigDialog::onDownloadUpdate);
  updateSectionLayout->addWidget(m_downloadUpdateButton);

  layout->addWidget(updateSection);

  layout->addSpacing(15);

  QWidget *thanksSection = new QWidget();
  thanksSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *thanksSectionLayout = new QVBoxLayout(thanksSection);
  thanksSectionLayout->setContentsMargins(16, 12, 16, 12);
  thanksSectionLayout->setSpacing(8);

  QLabel *thanksHeader = new QLabel("Thanks");
  thanksHeader->setStyleSheet(StyleSheet::getSubsectionHeaderStyleSheet());
  thanksSectionLayout->addWidget(thanksHeader);

  QGridLayout *thanksGridLayout = new QGridLayout();
  thanksGridLayout->setSpacing(8);

  QStringList thanksList = {
      "The Aggressor",  "Exie",       "Hyperion Iwaira",  "Zintage Enaka",
      "snipereagle1",   "degeva",     "Killer 641",       "Aulis",
      "Cyanide",        "Oebrun",     "Kondo Rio Sotken", "Zack Power",
      "Langanmyer Nor", "ham Norris", "Groot Brustir",    "The Llama"};

  int row = 0;
  int col = 0;
  for (const QString &name : thanksList) {
    QLabel *nameLabel = new QLabel("💙  " + name);
    nameLabel->setStyleSheet(StyleSheet::getFeatureLabelStyleSheet());
    thanksGridLayout->addWidget(nameLabel, row, col);

    col++;
    if (col >= 3) {
      col = 0;
      row++;
    }
  }

  thanksSectionLayout->addLayout(thanksGridLayout);

  layout->addWidget(thanksSection);

  layout->addStretch();

  QLabel *copyrightLabel = new QLabel(
      "© 2025 EVE-APM Preview\n"
      "Inspired by the original EVE-O Preview and EVE-X Preview tools.");
  copyrightLabel->setStyleSheet(StyleSheet::getCopyrightLabelStyleSheet());
  copyrightLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(copyrightLabel);

  m_stackedWidget->addWidget(page);
}

void ConfigDialog::setupBindings() {
  Config &config = Config::instance();

  m_bindingManager.clear();

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_alwaysOnTopCheck, [&config]() { return config.alwaysOnTop(); },
      [&config](bool value) { config.setAlwaysOnTop(value); }, true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_rememberPositionsCheck,
      [&config]() { return config.rememberPositions(); },
      [&config](bool value) { config.setRememberPositions(value); }, true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_preserveLogoutPositionsCheck,
      [&config]() { return config.preserveLogoutPositions(); },
      [&config](bool value) { config.setPreserveLogoutPositions(value); },
      false));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_enableSnappingCheck, [&config]() { return config.enableSnapping(); },
      [&config](bool value) { config.setEnableSnapping(value); }, true));

  m_bindingManager.addBinding(BindingHelpers::bindSpinBox(
      m_snapDistanceSpin, [&config]() { return config.snapDistance(); },
      [&config](int value) { config.setSnapDistance(value); }, 10));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_lockPositionsCheck,
      [&config]() { return config.lockThumbnailPositions(); },
      [&config](bool value) { config.setLockThumbnailPositions(value); },
      false));

  m_bindingManager.addBinding(BindingHelpers::bindSpinBox(
      m_thumbnailWidthSpin, [&config]() { return config.thumbnailWidth(); },
      [&config](int value) { config.setThumbnailWidth(value); }, 400));

  m_bindingManager.addBinding(BindingHelpers::bindSpinBox(
      m_thumbnailHeightSpin, [&config]() { return config.thumbnailHeight(); },
      [&config](int value) { config.setThumbnailHeight(value); }, 300));

  m_bindingManager.addBinding(BindingHelpers::bindSpinBox(
      m_opacitySpin, [&config]() { return config.thumbnailOpacity(); },
      [&config](int value) { config.setThumbnailOpacity(value); }, 95));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_showNotLoggedInClientsCheck,
      [&config]() { return config.showNotLoggedInClients(); },
      [&config](bool value) { config.setShowNotLoggedInClients(value); },
      true));

  m_bindingManager.addBinding(BindingHelpers::bindComboBox(
      m_notLoggedInStackModeCombo,
      [&config]() { return config.notLoggedInStackMode(); },
      [&config](int value) { config.setNotLoggedInStackMode(value); }, 0));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_showNotLoggedInOverlayCheck,
      [&config]() { return config.showNotLoggedInOverlay(); },
      [&config](bool value) { config.setShowNotLoggedInOverlay(value); },
      true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_showNonEVEOverlayCheck,
      [&config]() { return config.showNonEVEOverlay(); },
      [&config](bool value) { config.setShowNonEVEOverlay(value); }, true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_minimizeInactiveCheck,
      [&config]() { return config.minimizeInactiveClients(); },
      [&config](bool value) { config.setMinimizeInactiveClients(value); },
      false));

  m_bindingManager.addBinding(BindingHelpers::bindSpinBox(
      m_minimizeDelaySpin, [&config]() { return config.minimizeDelay(); },
      [&config](int value) { config.setMinimizeDelay(value); }, 500));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_saveClientLocationCheck,
      [&config]() { return config.saveClientLocation(); },
      [&config](bool value) { config.setSaveClientLocation(value); }, false));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_highlightActiveCheck,
      [&config]() { return config.highlightActiveWindow(); },
      [&config](bool value) { config.setHighlightActiveWindow(value); }, true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_hideActiveClientThumbnailCheck,
      [&config]() { return config.hideActiveClientThumbnail(); },
      [&config](bool value) { config.setHideActiveClientThumbnail(value); },
      false));

  auto highlightColorBinding = BindingHelpers::bindColorButton(
      m_highlightColorButton, [&config]() { return config.highlightColor(); },
      [&config](const QColor &color) { config.setHighlightColor(color); },
      QColor(255, 200, 0),
      [this](QPushButton *btn, const QColor &color) {
        m_highlightColor = color;
        updateColorButton(btn, color);
      });
  m_bindingManager.addBinding(std::move(highlightColorBinding));

  m_bindingManager.addBinding(BindingHelpers::bindSpinBox(
      m_highlightBorderWidthSpin,
      [&config]() { return config.highlightBorderWidth(); },
      [&config](int value) { config.setHighlightBorderWidth(value); }, 3));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_showCharacterNameCheck,
      [&config]() { return config.showCharacterName(); },
      [&config](bool value) { config.setShowCharacterName(value); }, true));

  auto charColorBinding = BindingHelpers::bindColorButton(
      m_characterNameColorButton,
      [&config]() { return config.characterNameColor(); },
      [&config](const QColor &color) { config.setCharacterNameColor(color); },
      QColor(Qt::white),
      [this](QPushButton *btn, const QColor &color) {
        m_characterNameColor = color;
        updateColorButton(btn, color);
      });
  m_bindingManager.addBinding(std::move(charColorBinding));

  m_bindingManager.addBinding(BindingHelpers::bindComboBox(
      m_characterNamePositionCombo,
      [&config]() { return config.characterNamePosition(); },
      [&config](int value) { config.setCharacterNamePosition(value); }, 0));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_showSystemNameCheck, [&config]() { return config.showSystemName(); },
      [&config](bool value) { config.setShowSystemName(value); }, true));

  auto sysColorBinding = BindingHelpers::bindColorButton(
      m_systemNameColorButton, [&config]() { return config.systemNameColor(); },
      [&config](const QColor &color) { config.setSystemNameColor(color); },
      QColor(Qt::white),
      [this](QPushButton *btn, const QColor &color) {
        m_systemNameColor = color;
        updateColorButton(btn, color);
      });
  m_bindingManager.addBinding(std::move(sysColorBinding));

  m_bindingManager.addBinding(BindingHelpers::bindComboBox(
      m_systemNamePositionCombo,
      [&config]() { return config.systemNamePosition(); },
      [&config](int value) { config.setSystemNamePosition(value); }, 0));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_showBackgroundCheck,
      [&config]() { return config.showOverlayBackground(); },
      [&config](bool value) { config.setShowOverlayBackground(value); }, true));

  auto bgColorBinding = BindingHelpers::bindColorButton(
      m_backgroundColorButton,
      [&config]() { return config.overlayBackgroundColor(); },
      [&config](const QColor &color) {
        config.setOverlayBackgroundColor(color);
      },
      QColor(0, 0, 0, 180),
      [this](QPushButton *btn, const QColor &color) {
        m_backgroundColor = color;
        updateColorButton(btn, color);
      });
  m_bindingManager.addBinding(std::move(bgColorBinding));

  m_bindingManager.addBinding(BindingHelpers::bindSpinBox(
      m_backgroundOpacitySpin,
      [&config]() { return config.overlayBackgroundOpacity(); },
      [&config](int value) { config.setOverlayBackgroundOpacity(value); }, 70));

  m_bindingManager.addBinding(BindingHelpers::bindStringListTable(
      m_neverMinimizeTable, 0,
      [&config]() { return config.neverMinimizeCharacters(); },
      [&config](const QStringList &list) {
        config.setNeverMinimizeCharacters(list);
      },
      QStringList()));

  m_bindingManager.addBinding(BindingHelpers::bindStringListTable(
      m_processNamesTable, 0, [&config]() { return config.processNames(); },
      [&config](const QStringList &list) { config.setProcessNames(list); },
      QStringList() << "exefile.exe"));

  m_bindingManager.addBinding(BindingHelpers::bindCharacterColorTable(
      m_characterColorsTable,
      [this](QPushButton *btn, const QColor &color) {
        updateColorButton(btn, color);
      },
      [this](QPushButton *button) {
        connect(button, &QPushButton::clicked, this,
                &ConfigDialog::onCharacterColorButtonClicked);
      }));

  HotkeyManager *hotkeyMgr = HotkeyManager::instance();
  if (hotkeyMgr) {
    m_bindingManager.addBinding(BindingHelpers::bindCharacterHotkeyTable(
        m_characterHotkeysTable,
        [hotkeyMgr]() { return hotkeyMgr->getAllCharacterHotkeys(); },
        [hotkeyMgr](const QHash<QString, HotkeyBinding> &hotkeys) {
          QHash<QString, HotkeyBinding> existing =
              hotkeyMgr->getAllCharacterHotkeys();
          for (auto it = existing.constBegin(); it != existing.constEnd();
               ++it) {
            hotkeyMgr->removeCharacterHotkey(it.key());
          }
          for (auto it = hotkeys.constBegin(); it != hotkeys.constEnd(); ++it) {
            hotkeyMgr->setCharacterHotkey(it.key(), it.value());
          }
        }));

    m_bindingManager.addBinding(BindingHelpers::bindCycleGroupTable(
        m_cycleGroupsTable,
        [hotkeyMgr]() { return hotkeyMgr->getAllCycleGroups(); },
        [hotkeyMgr](const QHash<QString, CycleGroup> &groups) {
          QHash<QString, CycleGroup> existing = hotkeyMgr->getAllCycleGroups();
          for (auto it = existing.constBegin(); it != existing.constEnd();
               ++it) {
            hotkeyMgr->removeCycleGroup(it.key());
          }
          for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            hotkeyMgr->createCycleGroup(it.value());
          }
        },
        [this](QPushButton *button) {
          connect(button, &QPushButton::clicked, this,
                  &ConfigDialog::onEditCycleGroupCharacters);
        }));

    m_bindingManager.addBinding(BindingHelpers::bindHotkeyCapture(
        m_suspendHotkeyCapture,
        [hotkeyMgr]() { return hotkeyMgr->getSuspendHotkey(); },
        [hotkeyMgr](const HotkeyBinding &hk) {
          hotkeyMgr->setSuspendHotkey(hk);
        },
        HotkeyBinding(0, false, false, false, false)));

    m_bindingManager.addBinding(BindingHelpers::bindHotkeyCapture(
        m_notLoggedInForwardCapture,
        [hotkeyMgr]() { return hotkeyMgr->getNotLoggedInForwardHotkey(); },
        [hotkeyMgr](const HotkeyBinding &hk) {
          hotkeyMgr->setNotLoggedInCycleHotkeys(
              hk, hotkeyMgr->getNotLoggedInBackwardHotkey());
        },
        HotkeyBinding(0, false, false, false, false)));

    m_bindingManager.addBinding(BindingHelpers::bindHotkeyCapture(
        m_notLoggedInBackwardCapture,
        [hotkeyMgr]() { return hotkeyMgr->getNotLoggedInBackwardHotkey(); },
        [hotkeyMgr](const HotkeyBinding &hk) {
          hotkeyMgr->setNotLoggedInCycleHotkeys(
              hotkeyMgr->getNotLoggedInForwardHotkey(), hk);
        },
        HotkeyBinding(0, false, false, false, false)));

    m_bindingManager.addBinding(BindingHelpers::bindHotkeyCapture(
        m_nonEVEForwardCapture,
        [hotkeyMgr]() { return hotkeyMgr->getNonEVEForwardHotkey(); },
        [hotkeyMgr](const HotkeyBinding &hk) {
          hotkeyMgr->setNonEVECycleHotkeys(
              hk, hotkeyMgr->getNonEVEBackwardHotkey());
        },
        HotkeyBinding(0, false, false, false, false)));

    m_bindingManager.addBinding(BindingHelpers::bindHotkeyCapture(
        m_nonEVEBackwardCapture,
        [hotkeyMgr]() { return hotkeyMgr->getNonEVEBackwardHotkey(); },
        [hotkeyMgr](const HotkeyBinding &hk) {
          hotkeyMgr->setNonEVECycleHotkeys(hotkeyMgr->getNonEVEForwardHotkey(),
                                           hk);
        },
        HotkeyBinding(0, false, false, false, false)));

    m_bindingManager.addBinding(BindingHelpers::bindHotkeyCapture(
        m_closeAllClientsCapture,
        [hotkeyMgr]() { return hotkeyMgr->getCloseAllClientsHotkey(); },
        [hotkeyMgr](const HotkeyBinding &hk) {
          hotkeyMgr->setCloseAllClientsHotkey(hk);
        },
        HotkeyBinding(0, false, false, false, false)));
  }

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_wildcardHotkeysCheck, [&config]() { return config.wildcardHotkeys(); },
      [&config, hotkeyMgr](bool value) {
        config.setWildcardHotkeys(value);
        if (hotkeyMgr) {
          hotkeyMgr->registerHotkeys();
        }
      },
      false));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_hotkeysOnlyWhenEVEFocusedCheck,
      [&config]() { return config.hotkeysOnlyWhenEVEFocused(); },
      [&config](bool value) { config.setHotkeysOnlyWhenEVEFocused(value); },
      false));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_enableChatLogMonitoringCheck,
      [&config]() { return config.enableChatLogMonitoring(); },
      [&config](bool value) { config.setEnableChatLogMonitoring(value); },
      false));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_enableGameLogMonitoringCheck,
      [&config]() { return config.enableGameLogMonitoring(); },
      [&config](bool value) { config.setEnableGameLogMonitoring(value); },
      false));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_showCombatMessagesCheck,
      [&config]() { return config.showCombatMessages(); },
      [&config](bool value) { config.setShowCombatMessages(value); }, true));

  m_bindingManager.addBinding(BindingHelpers::bindSpinBox(
      m_fileChangeDebounceSpin,
      [&config]() { return config.fileChangeDebounceMs(); },
      [&config](int value) { config.setFileChangeDebounceMs(value); }, 200));

  m_bindingManager.addBinding(BindingHelpers::bindComboBox(
      m_combatMessagePositionCombo,
      [&config]() { return config.combatMessagePosition(); },
      [&config](int value) { config.setCombatMessagePosition(value); }, 3));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_combatEventFleetInviteCheck,
      [&config]() { return config.isCombatEventTypeEnabled("fleet_invite"); },
      [&config](bool value) {
        QStringList types = config.enabledCombatEventTypes();
        if (value && !types.contains("fleet_invite")) {
          types << "fleet_invite";
          config.setEnabledCombatEventTypes(types);
        } else if (!value) {
          types.removeAll("fleet_invite");
          config.setEnabledCombatEventTypes(types);
        }
      },
      true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_combatEventFollowWarpCheck,
      [&config]() { return config.isCombatEventTypeEnabled("follow_warp"); },
      [&config](bool value) {
        QStringList types = config.enabledCombatEventTypes();
        if (value && !types.contains("follow_warp")) {
          types << "follow_warp";
          config.setEnabledCombatEventTypes(types);
        } else if (!value) {
          types.removeAll("follow_warp");
          config.setEnabledCombatEventTypes(types);
        }
      },
      true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_combatEventRegroupCheck,
      [&config]() { return config.isCombatEventTypeEnabled("regroup"); },
      [&config](bool value) {
        QStringList types = config.enabledCombatEventTypes();
        if (value && !types.contains("regroup")) {
          types << "regroup";
          config.setEnabledCombatEventTypes(types);
        } else if (!value) {
          types.removeAll("regroup");
          config.setEnabledCombatEventTypes(types);
        }
      },
      true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_combatEventCompressionCheck,
      [&config]() { return config.isCombatEventTypeEnabled("compression"); },
      [&config](bool value) {
        QStringList types = config.enabledCombatEventTypes();
        if (value && !types.contains("compression")) {
          types << "compression";
          config.setEnabledCombatEventTypes(types);
        } else if (!value) {
          types.removeAll("compression");
          config.setEnabledCombatEventTypes(types);
        }
      },
      true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_combatEventMiningStartCheck,
      [&config]() { return config.isCombatEventTypeEnabled("mining_started"); },
      [&config](bool value) {
        QStringList types = config.enabledCombatEventTypes();
        if (value && !types.contains("mining_started")) {
          types << "mining_started";
          config.setEnabledCombatEventTypes(types);
        } else if (!value) {
          types.removeAll("mining_started");
          config.setEnabledCombatEventTypes(types);
        }
      },
      true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_combatEventMiningStopCheck,
      [&config]() { return config.isCombatEventTypeEnabled("mining_stopped"); },
      [&config](bool value) {
        QStringList types = config.enabledCombatEventTypes();
        if (value && !types.contains("mining_stopped")) {
          types << "mining_stopped";
          config.setEnabledCombatEventTypes(types);
        } else if (!value) {
          types.removeAll("mining_stopped");
          config.setEnabledCombatEventTypes(types);
        }
      },
      true));

  m_bindingManager.addBinding(BindingHelpers::bindSpinBox(
      m_miningTimeoutSpin,
      [&config]() { return config.miningTimeoutSeconds(); },
      [&config](int value) { config.setMiningTimeoutSeconds(value); }, true));
}

void ConfigDialog::onCategoryChanged(int index) {
  m_stackedWidget->setCurrentIndex(index);
}

void ConfigDialog::loadSettings() {
  Config &config = Config::instance();

  m_bindingManager.loadAll();

  m_chatLogDirectoryEdit->setText(config.chatLogDirectory());

  m_gameLogDirectoryEdit->setText(config.gameLogDirectory());

  for (auto it = m_eventColorButtons.constBegin();
       it != m_eventColorButtons.constEnd(); ++it) {
    QString eventType = it.key();
    QPushButton *colorBtn = it.value();
    updateColorButton(colorBtn, config.combatEventColor(eventType));
  }

  for (auto it = m_eventDurationSpins.constBegin();
       it != m_eventDurationSpins.constEnd(); ++it) {
    QString eventType = it.key();
    QSpinBox *durationSpin = it.value();
    durationSpin->setValue(config.combatEventDuration(eventType) / 1000);
  }

  for (auto it = m_eventBorderCheckBoxes.constBegin();
       it != m_eventBorderCheckBoxes.constEnd(); ++it) {
    QString eventType = it.key();
    QCheckBox *borderCheck = it.value();
    borderCheck->setChecked(config.combatEventBorderHighlight(eventType));
  }

  m_snapDistanceLabel->setEnabled(config.enableSnapping());
  m_snapDistanceSpin->setEnabled(config.enableSnapping());
  m_minimizeDelayLabel->setEnabled(config.minimizeInactiveClients());
  m_minimizeDelaySpin->setEnabled(config.minimizeInactiveClients());
  m_highlightColorLabel->setEnabled(config.highlightActiveWindow());
  m_highlightColorButton->setEnabled(config.highlightActiveWindow());
  m_highlightBorderWidthLabel->setEnabled(config.highlightActiveWindow());
  m_highlightBorderWidthSpin->setEnabled(config.highlightActiveWindow());
  m_characterNameColorLabel->setEnabled(config.showCharacterName());
  m_characterNameColorButton->setEnabled(config.showCharacterName());
  m_characterNamePositionLabel->setEnabled(config.showCharacterName());
  m_characterNamePositionCombo->setEnabled(config.showCharacterName());
  m_characterNameFontLabel->setEnabled(config.showCharacterName());
  m_characterNameFontButton->setEnabled(config.showCharacterName());
  m_systemNameColorLabel->setEnabled(config.showSystemName());
  m_systemNameColorButton->setEnabled(config.showSystemName());
  m_systemNamePositionLabel->setEnabled(config.showSystemName());
  m_systemNamePositionCombo->setEnabled(config.showSystemName());
  m_systemNameFontLabel->setEnabled(config.showSystemName());
  m_systemNameFontButton->setEnabled(config.showSystemName());
  m_backgroundColorLabel->setEnabled(config.showOverlayBackground());
  m_backgroundOpacityLabel->setEnabled(config.showOverlayBackground());

  m_neverMinimizeTable->setEnabled(config.minimizeInactiveClients());
  m_addNeverMinimizeButton->setEnabled(config.minimizeInactiveClients());
  m_populateNeverMinimizeButton->setEnabled(config.minimizeInactiveClients());
  m_setClientLocationsLabel->setEnabled(config.saveClientLocation());
  m_setClientLocationsButton->setEnabled(config.saveClientLocation());

  bool notLoggedInEnabled = config.showNotLoggedInClients();
  m_notLoggedInPositionLabel->setEnabled(notLoggedInEnabled);
  m_setNotLoggedInPositionButton->setEnabled(notLoggedInEnabled);
  m_notLoggedInStackModeLabel->setEnabled(notLoggedInEnabled);
  m_notLoggedInStackModeCombo->setEnabled(notLoggedInEnabled);
  m_showNotLoggedInOverlayCheck->setEnabled(notLoggedInEnabled);

  bool chatLogEnabled = config.enableChatLogMonitoring();
  m_chatLogDirectoryLabel->setEnabled(chatLogEnabled);
  m_chatLogDirectoryEdit->setEnabled(chatLogEnabled);
  m_chatLogBrowseButton->setEnabled(chatLogEnabled);

  bool gameLogEnabled = config.enableGameLogMonitoring();
  m_gameLogDirectoryLabel->setEnabled(gameLogEnabled);
  m_gameLogDirectoryEdit->setEnabled(gameLogEnabled);
  m_gameLogBrowseButton->setEnabled(gameLogEnabled);
  m_fileChangeDebounceSpin->setEnabled(gameLogEnabled);

  bool combatMessagesEnabled = config.showCombatMessages();
  m_combatMessagePositionCombo->setEnabled(combatMessagesEnabled);
  m_combatMessagePositionLabel->setEnabled(combatMessagesEnabled);
  m_combatMessageFontButton->setEnabled(combatMessagesEnabled);
  m_combatMessageFontLabel->setEnabled(combatMessagesEnabled);
  m_combatEventFleetInviteCheck->setEnabled(combatMessagesEnabled);
  m_combatEventFollowWarpCheck->setEnabled(combatMessagesEnabled);
  m_combatEventRegroupCheck->setEnabled(combatMessagesEnabled);
  m_combatEventCompressionCheck->setEnabled(combatMessagesEnabled);
  m_combatEventMiningStartCheck->setEnabled(combatMessagesEnabled);
  m_combatEventMiningStopCheck->setEnabled(combatMessagesEnabled);

  bool miningStopChecked = m_combatEventMiningStopCheck->isChecked();
  m_miningTimeoutSpin->setEnabled(combatMessagesEnabled && miningStopChecked);
  m_miningTimeoutLabel->setEnabled(combatMessagesEnabled && miningStopChecked);

  QMap<QString, QCheckBox *> eventCheckboxes = {
      {"fleet_invite", m_combatEventFleetInviteCheck},
      {"follow_warp", m_combatEventFollowWarpCheck},
      {"regroup", m_combatEventRegroupCheck},
      {"compression", m_combatEventCompressionCheck},
      {"mining_started", m_combatEventMiningStartCheck},
      {"mining_stopped", m_combatEventMiningStopCheck}};

  for (auto it = eventCheckboxes.constBegin(); it != eventCheckboxes.constEnd();
       ++it) {
    QString eventType = it.key();
    bool eventEnabled = combatMessagesEnabled && it.value()->isChecked();

    if (m_eventColorButtons.contains(eventType)) {
      m_eventColorButtons[eventType]->setEnabled(eventEnabled);
    }
    if (m_eventDurationSpins.contains(eventType)) {
      m_eventDurationSpins[eventType]->setEnabled(eventEnabled);
    }
    if (m_eventDurationLabels.contains(eventType)) {
      m_eventDurationLabels[eventType]->setEnabled(eventEnabled);
    }
    if (m_eventBorderCheckBoxes.contains(eventType)) {
      m_eventBorderCheckBoxes[eventType]->setEnabled(eventEnabled);
    }
  }

  m_thumbnailSizesTable->setRowCount(0);
  QHash<QString, QSize> customSizes = config.getAllCustomThumbnailSizes();
  for (auto it = customSizes.constBegin(); it != customSizes.constEnd(); ++it) {
    int row = m_thumbnailSizesTable->rowCount();
    m_thumbnailSizesTable->insertRow(row);

    QLineEdit *nameEdit = new QLineEdit(it.key());
    nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
    m_thumbnailSizesTable->setCellWidget(row, 0, nameEdit);

    QSpinBox *widthSpin = new QSpinBox();
    widthSpin->setRange(50, 800);
    widthSpin->setSuffix(" px");
    widthSpin->setValue(it.value().width());
    widthSpin->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());

    QWidget *widthContainer = new QWidget();
    QHBoxLayout *widthLayout = new QHBoxLayout(widthContainer);
    widthLayout->setContentsMargins(3, 3, 3, 3);
    widthLayout->addWidget(widthSpin);
    m_thumbnailSizesTable->setCellWidget(row, 1, widthContainer);

    QSpinBox *heightSpin = new QSpinBox();
    heightSpin->setRange(50, 600);
    heightSpin->setSuffix(" px");
    heightSpin->setValue(it.value().height());
    heightSpin->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());

    QWidget *heightContainer = new QWidget();
    QHBoxLayout *heightLayout = new QHBoxLayout(heightContainer);
    heightLayout->setContentsMargins(3, 3, 3, 3);
    heightLayout->addWidget(heightSpin);
    m_thumbnailSizesTable->setCellWidget(row, 2, heightContainer);

    QWidget *deleteButtonContainer = new QWidget();
    QHBoxLayout *deleteButtonLayout = new QHBoxLayout(deleteButtonContainer);
    deleteButtonLayout->setContentsMargins(0, 0, 0, 0);

    QPushButton *deleteButton = new QPushButton("×");
    deleteButton->setFixedSize(24, 24);
    deleteButton->setStyleSheet("QPushButton {"
                                "    background-color: #3a3a3a;"
                                "    color: #ffffff;"
                                "    border: 1px solid #555555;"
                                "    border-radius: 4px;"
                                "    font-size: 16px;"
                                "    font-weight: bold;"
                                "    padding: 0px;"
                                "}"
                                "QPushButton:hover {"
                                "    background-color: #e74c3c;"
                                "    border: 1px solid #c0392b;"
                                "}"
                                "QPushButton:pressed {"
                                "    background-color: #c0392b;"
                                "}");

    connect(deleteButton, &QPushButton::clicked, this,
            [this, row]() { m_thumbnailSizesTable->removeRow(row); });

    deleteButtonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
    m_thumbnailSizesTable->setCellWidget(row, 3, deleteButtonContainer);
  }
}

void ConfigDialog::saveSettings() {
  m_bindingManager.saveAll();

  HotkeyManager::instance()->saveToConfig();

  Config::instance().setChatLogDirectory(
      m_chatLogDirectoryEdit->text().trimmed());

  Config::instance().setGameLogDirectory(
      m_gameLogDirectoryEdit->text().trimmed());

  qDebug() << "ConfigDialog::saveSettings() - enableGameLogMonitoring:"
           << Config::instance().enableGameLogMonitoring();
  qDebug() << "ConfigDialog::saveSettings() - checkbox state:"
           << m_enableGameLogMonitoringCheck->isChecked();

  Config &cfg = Config::instance();

  QHash<QString, QSize> existingSizes = cfg.getAllCustomThumbnailSizes();
  for (const QString &charName : existingSizes.keys()) {
    cfg.removeThumbnailSize(charName);
  }

  for (int row = 0; row < m_thumbnailSizesTable->rowCount(); ++row) {
    QLineEdit *nameEdit =
        qobject_cast<QLineEdit *>(m_thumbnailSizesTable->cellWidget(row, 0));

    QWidget *widthContainer = m_thumbnailSizesTable->cellWidget(row, 1);
    QSpinBox *widthSpin =
        widthContainer ? widthContainer->findChild<QSpinBox *>() : nullptr;

    QWidget *heightContainer = m_thumbnailSizesTable->cellWidget(row, 2);
    QSpinBox *heightSpin =
        heightContainer ? heightContainer->findChild<QSpinBox *>() : nullptr;

    if (!nameEdit || !widthSpin || !heightSpin) {
      continue;
    }

    QString charName = nameEdit->text().trimmed();
    if (charName.isEmpty()) {
      continue;
    }

    QSize size(widthSpin->value(), heightSpin->value());
    cfg.setThumbnailSize(charName, size);
  }

  Config::instance().save();
}

void ConfigDialog::onApplyClicked() {
  saveSettings();
  emit settingsApplied();

  if (m_testThumbnail) {
    onTestOverlays();
  }
}

void ConfigDialog::onOkClicked() {
  saveSettings();
  emit settingsApplied();
  accept();
}

void ConfigDialog::onCancelClicked() { reject(); }

void ConfigDialog::onTestOverlays() {
  if (!m_testThumbnail) {
    m_testThumbnail =
        new ThumbnailWidget(quintptr(0), "Test Window - Preview", nullptr);

    m_testThumbnail->setCharacterName("Test Character");
    m_testThumbnail->setSystemName("Jita");

    const Config &cfg = Config::instance();
    if (cfg.showCombatMessages()) {
      QStringList enabledEvents = cfg.enabledCombatEventTypes();
      if (!enabledEvents.isEmpty()) {
        QString eventType = enabledEvents.first();
        m_testThumbnail->setCombatMessage("Sample Combat Event", eventType);
      } else {
        m_testThumbnail->setCombatMessage("Sample Event", "mining_start");
      }
    }

    m_testThumbnail->resize(cfg.thumbnailWidth(), cfg.thumbnailHeight());

    QPoint dialogPos = pos();
    QPoint testPos = dialogPos + QPoint(width() + 20, 0);
    m_testThumbnail->move(testPos);

    m_testThumbnail->updateOverlays();
    m_testThumbnail->show();
    m_testThumbnail->raise();
  } else {
    const Config &cfg = Config::instance();

    if (cfg.showCombatMessages()) {
      QStringList enabledEvents = cfg.enabledCombatEventTypes();
      if (!enabledEvents.isEmpty() && !m_testThumbnail->hasCombatEvent()) {
        QString eventType = enabledEvents.first();
        m_testThumbnail->setCombatMessage("Sample Combat Event", eventType);
      }
    } else {
      m_testThumbnail->setCombatMessage("", "");
    }

    m_testThumbnail->resize(cfg.thumbnailWidth(), cfg.thumbnailHeight());

    m_testThumbnail->updateOverlays();
    m_testThumbnail->forceUpdate();
    m_testThumbnail->forceOverlayRender();

    m_testThumbnail->raise();
    m_testThumbnail->activateWindow();
  }
}

void ConfigDialog::onSetNotLoggedInPosition() {
  if (!m_notLoggedInReferenceThumbnail) {
    m_notLoggedInReferenceThumbnail = new ThumbnailWidget(
        quintptr(0), "Not Logged In - Reference Position", nullptr);

    m_notLoggedInReferenceThumbnail->setCharacterName("Not Logged In");
    m_notLoggedInReferenceThumbnail->setSystemName("");

    const Config &cfg = Config::instance();
    m_notLoggedInReferenceThumbnail->resize(cfg.thumbnailWidth(),
                                            cfg.thumbnailHeight());

    QPoint refPos = cfg.notLoggedInReferencePosition();
    m_notLoggedInReferenceThumbnail->move(refPos);

    connect(m_notLoggedInReferenceThumbnail, &ThumbnailWidget::positionChanged,
            this, [](quintptr, QPoint position) {
              Config::instance().setNotLoggedInReferencePosition(position);
              Config::instance().save();
            });

    m_notLoggedInReferenceThumbnail->updateOverlays();
    m_notLoggedInReferenceThumbnail->show();
    m_notLoggedInReferenceThumbnail->raise();
    m_notLoggedInReferenceThumbnail->activateWindow();
  } else {
    if (m_notLoggedInReferenceThumbnail->isVisible()) {
      m_notLoggedInReferenceThumbnail->hide();
    } else {
      const Config &cfg = Config::instance();
      m_notLoggedInReferenceThumbnail->resize(cfg.thumbnailWidth(),
                                              cfg.thumbnailHeight());
      m_notLoggedInReferenceThumbnail->updateOverlays();
      m_notLoggedInReferenceThumbnail->show();
      m_notLoggedInReferenceThumbnail->raise();
      m_notLoggedInReferenceThumbnail->activateWindow();
    }
  }
}

void ConfigDialog::onSetClientLocations() {
  emit saveClientLocationsRequested();

  QMessageBox::information(
      this, "Client Locations Saved",
      "The current window positions of all open EVE clients have been saved.");
}

void ConfigDialog::onColorButtonClicked() {
  QPushButton *button = qobject_cast<QPushButton *>(sender());
  if (!button)
    return;

  QColor currentColor;
  QColor *targetColor = nullptr;

  if (button == m_highlightColorButton) {
    currentColor = m_highlightColor;
    targetColor = &m_highlightColor;
  } else if (button == m_characterNameColorButton) {
    currentColor = m_characterNameColor;
    targetColor = &m_characterNameColor;
  } else if (button == m_systemNameColorButton) {
    currentColor = m_systemNameColor;
    targetColor = &m_systemNameColor;
  } else if (button == m_backgroundColorButton) {
    currentColor = m_backgroundColor;
    targetColor = &m_backgroundColor;
  }

  if (targetColor) {
    QColor color = QColorDialog::getColor(currentColor, this, "Choose Color");
    if (color.isValid()) {
      *targetColor = color;
      updateColorButton(button, color);

      SettingBindingBase *bindingBase = m_bindingManager.findBinding(button);
      if (bindingBase) {
        ColorButtonBinding *colorBinding =
            dynamic_cast<ColorButtonBinding *>(bindingBase);
        if (colorBinding) {
          colorBinding->setCurrentColor(color);
        }
      }
    }
  }
}

void ConfigDialog::updateColorButton(QPushButton *button, const QColor &color) {
  QString textColor = color.lightness() > 128 ? "#000000" : "#ffffff";
  button->setStyleSheet(
      StyleSheet::getColorButtonStyleSheet(color.name(), textColor));
  button->setText(color.name().toUpper());
}

void ConfigDialog::onAddCharacterHotkey() {
  int row = m_characterHotkeysTable->rowCount();
  m_characterHotkeysTable->insertRow(row);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setPlaceholderText("Enter character name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  m_characterHotkeysTable->setCellWidget(row, 0, nameEdit);

  QWidget *hotkeyWidget = new QWidget();
  QHBoxLayout *hotkeyLayout = new QHBoxLayout(hotkeyWidget);
  hotkeyLayout->setContentsMargins(0, 0, 4, 0);
  hotkeyLayout->setSpacing(4);

  HotkeyCapture *hotkeyCapture = new HotkeyCapture();
  QPushButton *clearButton = new QPushButton("×");
  clearButton->setFixedSize(24, 24);
  clearButton->setStyleSheet("QPushButton {"
                             "    background-color: #3a3a3a;"
                             "    color: #a0a0a0;"
                             "    border: 1px solid #555555;"
                             "    border-radius: 3px;"
                             "    font-size: 16px;"
                             "    font-weight: bold;"
                             "    padding: 0px;"
                             "}"
                             "QPushButton:hover {"
                             "    background-color: #4a4a4a;"
                             "    color: #ffffff;"
                             "    border: 1px solid #666666;"
                             "}"
                             "QPushButton:pressed {"
                             "    background-color: #2a2a2a;"
                             "}");
  clearButton->setToolTip("Clear hotkey");
  connect(clearButton, &QPushButton::clicked,
          [hotkeyCapture]() { hotkeyCapture->clearHotkey(); });

  hotkeyLayout->addWidget(hotkeyCapture, 1);
  hotkeyLayout->addWidget(clearButton, 0);

  m_characterHotkeysTable->setCellWidget(row, 1, hotkeyWidget);

  QWidget *deleteContainer = new QWidget();
  deleteContainer->setStyleSheet("QWidget { background-color: transparent; }");
  QHBoxLayout *deleteLayout = new QHBoxLayout(deleteContainer);
  deleteLayout->setContentsMargins(0, 0, 0, 0);
  deleteLayout->setAlignment(Qt::AlignCenter);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(24, 24);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #e74c3c;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 3px;"
                              "    font-size: 16px;"
                              "    font-weight: bold;"
                              "    padding: 0px;"
                              "}"
                              "QPushButton:hover {"
                              "    background-color: #e74c3c;"
                              "    color: #ffffff;"
                              "    border: 1px solid #e74c3c;"
                              "}"
                              "QPushButton:pressed {"
                              "    background-color: #c0392b;"
                              "}");
  deleteButton->setToolTip("Delete this character hotkey");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, [this, deleteButton]() {
    for (int i = 0; i < m_characterHotkeysTable->rowCount(); ++i) {
      QWidget *widget = m_characterHotkeysTable->cellWidget(i, 2);
      if (widget && widget->findChild<QPushButton *>() == deleteButton) {
        m_characterHotkeysTable->removeRow(i);
        break;
      }
    }
  });

  deleteLayout->addWidget(deleteButton);
  m_characterHotkeysTable->setCellWidget(row, 2, deleteContainer);

  m_characterHotkeysTable->scrollToBottom();
}

void ConfigDialog::onPopulateFromOpenWindows() {
  WindowCapture capture;
  QVector<WindowInfo> windows = capture.getEVEWindows();

  if (windows.isEmpty()) {
    QMessageBox::information(this, "No Windows Found",
                             "No EVE Online windows are currently open.");
    return;
  }

  std::sort(windows.begin(), windows.end(),
            [](const WindowInfo &a, const WindowInfo &b) {
              return a.creationTime < b.creationTime;
            });

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Populate Characters");
  msgBox.setText(QString("Found %1 open EVE Online window%2.")
                     .arg(windows.count())
                     .arg(windows.count() == 1 ? "" : "s"));
  msgBox.setInformativeText(
      "Do you want to clear existing entries or add to them?");

  QPushButton *clearButton =
      msgBox.addButton("Clear & Replace", QMessageBox::ActionRole);
  QPushButton *addButton =
      msgBox.addButton("Add to Existing", QMessageBox::ActionRole);
  QPushButton *cancelButton =
      msgBox.addButton("Cancel", QMessageBox::RejectRole);

  msgBox.setStyleSheet(StyleSheet::getMessageBoxStyleSheet());

  msgBox.exec();

  if (msgBox.clickedButton() == cancelButton) {
    return;
  }

  bool clearExisting = (msgBox.clickedButton() == clearButton);

  QSet<QString> existingCharacters;
  if (!clearExisting) {
    for (int row = 0; row < m_characterHotkeysTable->rowCount(); ++row) {
      QLineEdit *nameEdit = qobject_cast<QLineEdit *>(
          m_characterHotkeysTable->cellWidget(row, 0));
      if (nameEdit && !nameEdit->text().trimmed().isEmpty()) {
        existingCharacters.insert(nameEdit->text().trimmed());
      }
    }
  } else {
    m_characterHotkeysTable->setRowCount(0);
  }

  int addedCount = 0;
  for (const WindowInfo &window : windows) {
    QString characterName = window.title;
    if (characterName.startsWith("EVE - ")) {
      characterName = characterName.mid(6);
    } else {
      continue;
    }

    if (characterName.trimmed().isEmpty()) {
      continue;
    }

    if (!clearExisting && existingCharacters.contains(characterName)) {
      continue;
    }

    int row = m_characterHotkeysTable->rowCount();
    m_characterHotkeysTable->insertRow(row);

    QLineEdit *nameEdit = new QLineEdit();
    nameEdit->setText(characterName);
    nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
    m_characterHotkeysTable->setCellWidget(row, 0, nameEdit);

    QWidget *hotkeyWidget = new QWidget();
    QHBoxLayout *hotkeyLayout = new QHBoxLayout(hotkeyWidget);
    hotkeyLayout->setContentsMargins(0, 0, 0, 0);
    hotkeyLayout->setSpacing(4);

    HotkeyCapture *hotkeyCapture = new HotkeyCapture();
    QPushButton *clearButton = new QPushButton("×");
    clearButton->setFixedSize(24, 24);
    clearButton->setStyleSheet("QPushButton {"
                               "    background-color: #3a3a3a;"
                               "    color: #a0a0a0;"
                               "    border: 1px solid #555555;"
                               "    border-radius: 3px;"
                               "    font-size: 16px;"
                               "    font-weight: bold;"
                               "    padding: 0px;"
                               "}"
                               "QPushButton:hover {"
                               "    background-color: #4a4a4a;"
                               "    color: #ffffff;"
                               "    border: 1px solid #666666;"
                               "}"
                               "QPushButton:pressed {"
                               "    background-color: #2a2a2a;"
                               "}");
    clearButton->setToolTip("Clear hotkey");
    connect(clearButton, &QPushButton::clicked,
            [hotkeyCapture]() { hotkeyCapture->clearHotkey(); });

    hotkeyLayout->addWidget(hotkeyCapture, 1);
    hotkeyLayout->addWidget(clearButton, 0);
    m_characterHotkeysTable->setCellWidget(row, 1, hotkeyWidget);

    QWidget *deleteContainer = new QWidget();
    deleteContainer->setStyleSheet(
        "QWidget { background-color: transparent; }");
    QHBoxLayout *deleteLayout = new QHBoxLayout(deleteContainer);
    deleteLayout->setContentsMargins(0, 0, 0, 0);
    deleteLayout->setAlignment(Qt::AlignCenter);

    QPushButton *deleteButton = new QPushButton("×");
    deleteButton->setFixedSize(24, 24);
    deleteButton->setStyleSheet("QPushButton {"
                                "    background-color: #3a3a3a;"
                                "    color: #e74c3c;"
                                "    border: 1px solid #555555;"
                                "    border-radius: 3px;"
                                "    font-size: 16px;"
                                "    font-weight: bold;"
                                "    padding: 0px;"
                                "}"
                                "QPushButton:hover {"
                                "    background-color: #e74c3c;"
                                "    color: #ffffff;"
                                "    border: 1px solid #e74c3c;"
                                "}"
                                "QPushButton:pressed {"
                                "    background-color: #c0392b;"
                                "}");
    deleteButton->setToolTip("Delete this character hotkey");
    deleteButton->setCursor(Qt::PointingHandCursor);

    connect(deleteButton, &QPushButton::clicked, [this, deleteButton]() {
      for (int i = 0; i < m_characterHotkeysTable->rowCount(); ++i) {
        QWidget *widget = m_characterHotkeysTable->cellWidget(i, 2);
        if (widget && widget->findChild<QPushButton *>() == deleteButton) {
          m_characterHotkeysTable->removeRow(i);
          break;
        }
      }
    });

    deleteLayout->addWidget(deleteButton);
    m_characterHotkeysTable->setCellWidget(row, 2, deleteContainer);

    addedCount++;
  }

  if (addedCount > 0) {
    QMessageBox::information(
        this, "Characters Added",
        QString("Added %1 character%2 to the hotkey table.")
            .arg(addedCount)
            .arg(addedCount == 1 ? "" : "s"));
  } else if (!clearExisting) {
    QMessageBox::information(this, "No New Characters",
                             "All open characters are already in the table.");
  }
}

void ConfigDialog::onAddCycleGroup() {
  int row = m_cycleGroupsTable->rowCount();
  m_cycleGroupsTable->insertRow(row);

  QString cellStyle = "QLineEdit {"
                      "   background-color: transparent;"
                      "   color: #ffffff;"
                      "   border: none;"
                      "   padding: 2px 4px;"
                      "   font-size: 12px;"
                      "}"
                      "QLineEdit:focus {"
                      "   background-color: #353535;"
                      "}";

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(QString("Group %1").arg(row + 1));
  nameEdit->setStyleSheet(cellStyle);
  m_cycleGroupsTable->setCellWidget(row, 0, nameEdit);

  QPushButton *charactersButton = new QPushButton("(No characters)");
  charactersButton->setStyleSheet(StyleSheet::getTableCellButtonStyleSheet());
  charactersButton->setCursor(Qt::PointingHandCursor);
  charactersButton->setProperty("characterList", QStringList());
  connect(charactersButton, &QPushButton::clicked, this,
          &ConfigDialog::onEditCycleGroupCharacters);
  m_cycleGroupsTable->setCellWidget(row, 1, charactersButton);

  QWidget *forwardHotkeyWidget = new QWidget();
  QHBoxLayout *forwardLayout = new QHBoxLayout(forwardHotkeyWidget);
  forwardLayout->setContentsMargins(0, 0, 0, 0);
  forwardLayout->setSpacing(4);

  HotkeyCapture *forwardCapture = new HotkeyCapture();
  QPushButton *clearForwardButton = new QPushButton("×");
  clearForwardButton->setFixedSize(24, 24);
  clearForwardButton->setStyleSheet("QPushButton {"
                                    "    background-color: #3a3a3a;"
                                    "    color: #a0a0a0;"
                                    "    border: 1px solid #555555;"
                                    "    border-radius: 3px;"
                                    "    font-size: 16px;"
                                    "    font-weight: bold;"
                                    "    padding: 0px;"
                                    "}"
                                    "QPushButton:hover {"
                                    "    background-color: #4a4a4a;"
                                    "    color: #ffffff;"
                                    "    border: 1px solid #666666;"
                                    "}"
                                    "QPushButton:pressed {"
                                    "    background-color: #2a2a2a;"
                                    "}");
  clearForwardButton->setToolTip("Clear hotkey");
  connect(clearForwardButton, &QPushButton::clicked,
          [forwardCapture]() { forwardCapture->clearHotkey(); });

  forwardLayout->addWidget(forwardCapture, 1);
  forwardLayout->addWidget(clearForwardButton, 0);
  m_cycleGroupsTable->setCellWidget(row, 2, forwardHotkeyWidget);

  QWidget *backwardHotkeyWidget = new QWidget();
  QHBoxLayout *backwardLayout = new QHBoxLayout(backwardHotkeyWidget);
  backwardLayout->setContentsMargins(0, 0, 0, 0);
  backwardLayout->setSpacing(4);

  HotkeyCapture *backwardCapture = new HotkeyCapture();
  QPushButton *clearBackwardButton = new QPushButton("×");
  clearBackwardButton->setFixedSize(24, 24);
  clearBackwardButton->setStyleSheet("QPushButton {"
                                     "    background-color: #3a3a3a;"
                                     "    color: #a0a0a0;"
                                     "    border: 1px solid #555555;"
                                     "    border-radius: 3px;"
                                     "    font-size: 16px;"
                                     "    font-weight: bold;"
                                     "    padding: 0px;"
                                     "}"
                                     "QPushButton:hover {"
                                     "    background-color: #4a4a4a;"
                                     "    color: #ffffff;"
                                     "    border: 1px solid #666666;"
                                     "}"
                                     "QPushButton:pressed {"
                                     "    background-color: #2a2a2a;"
                                     "}");
  clearBackwardButton->setToolTip("Clear hotkey");
  connect(clearBackwardButton, &QPushButton::clicked,
          [backwardCapture]() { backwardCapture->clearHotkey(); });

  backwardLayout->addWidget(backwardCapture, 1);
  backwardLayout->addWidget(clearBackwardButton, 0);
  m_cycleGroupsTable->setCellWidget(row, 3, backwardHotkeyWidget);

  QWidget *checkboxContainer = new QWidget();
  checkboxContainer->setStyleSheet(
      "QWidget { background-color: transparent; }");
  QHBoxLayout *checkboxLayout = new QHBoxLayout(checkboxContainer);
  checkboxLayout->setContentsMargins(0, 0, 0, 0);
  checkboxLayout->setAlignment(Qt::AlignCenter);

  QCheckBox *includeNotLoggedInCheck = new QCheckBox();
  includeNotLoggedInCheck->setChecked(false);
  includeNotLoggedInCheck->setToolTip(
      "Include not-logged-in EVE clients in this cycle group");

  QString checkboxStyle =
      QString("QCheckBox {"
              "   spacing: 5px;"
              "   outline: none;"
              "}"
              "QCheckBox::indicator {"
              "   width: 18px;"
              "   height: 18px;"
              "   border: 2px solid %1;"
              "   border-radius: 4px;"
              "   background-color: #303030;"
              "}"
              "QCheckBox::indicator:hover {"
              "   border: 2px solid %2;"
              "}"
              "QCheckBox::indicator:focus {"
              "   border: 2px solid %2;"
              "}"
              "QCheckBox::indicator:checked {"
              "   background-color: %2;"
              "   border: 2px solid %2;"
              "   image: "
              "url(data:image/"
              "svg+xml;base64,"
              "PHN2ZyB3aWR0aD0iMTIiIGhlaWdodD0iMTAiIHhtbG5zPSJodHRwOi8vd3d3Lncz"
              "Lm9yZy8yMDAwL3N2ZyI+"
              "PHBhdGggZD0iTTEgNUw0IDhMMTEgMSIgc3Ryb2tlPSIjZmZmZmZmIiBzdHJva2Ut"
              "d2lkdGg9IjIiIGZpbGw9Im5vbmUiLz48L3N2Zz4=);"
              "}"
              "QCheckBox::indicator:checked:hover {"
              "   background-color: %2;"
              "   border: 2px solid %2;"
              "}"
              "QCheckBox::indicator:checked:focus {"
              "   background-color: %2;"
              "   border: 2px solid %2;"
              "}")
          .arg(StyleSheet::colorBorder())
          .arg(StyleSheet::colorAccent());

  includeNotLoggedInCheck->setStyleSheet(checkboxStyle);

  checkboxLayout->addWidget(includeNotLoggedInCheck);
  m_cycleGroupsTable->setCellWidget(row, 4, checkboxContainer);

  QWidget *noLoopContainer = new QWidget();
  noLoopContainer->setStyleSheet("QWidget { background-color: transparent; }");
  QHBoxLayout *noLoopLayout = new QHBoxLayout(noLoopContainer);
  noLoopLayout->setContentsMargins(0, 0, 0, 0);
  noLoopLayout->setAlignment(Qt::AlignCenter);

  QCheckBox *noLoopCheck = new QCheckBox();
  noLoopCheck->setChecked(false);
  noLoopCheck->setToolTip("Don't loop when reaching the end of the list");
  noLoopCheck->setStyleSheet(checkboxStyle);

  noLoopLayout->addWidget(noLoopCheck);
  m_cycleGroupsTable->setCellWidget(row, 5, noLoopContainer);

  QWidget *deleteContainer = new QWidget();
  deleteContainer->setStyleSheet("QWidget { background-color: transparent; }");
  QHBoxLayout *deleteLayout = new QHBoxLayout(deleteContainer);
  deleteLayout->setContentsMargins(0, 0, 0, 0);
  deleteLayout->setAlignment(Qt::AlignCenter);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(24, 24);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #e74c3c;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 3px;"
                              "    font-size: 16px;"
                              "    font-weight: bold;"
                              "    padding: 0px;"
                              "}"
                              "QPushButton:hover {"
                              "    background-color: #e74c3c;"
                              "    color: #ffffff;"
                              "    border: 1px solid #e74c3c;"
                              "}"
                              "QPushButton:pressed {"
                              "    background-color: #c0392b;"
                              "}");
  deleteButton->setToolTip("Delete this cycle group");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, [this, deleteButton]() {
    for (int i = 0; i < m_cycleGroupsTable->rowCount(); ++i) {
      QWidget *widget = m_cycleGroupsTable->cellWidget(i, 6);
      if (widget && widget->findChild<QPushButton *>() == deleteButton) {
        m_cycleGroupsTable->removeRow(i);
        break;
      }
    }
  });

  deleteLayout->addWidget(deleteButton);
  m_cycleGroupsTable->setCellWidget(row, 6, deleteContainer);

  m_cycleGroupsTable->scrollToBottom();
}

void ConfigDialog::onEditCycleGroupCharacters() {
  QPushButton *button = qobject_cast<QPushButton *>(sender());
  if (!button)
    return;

  QStringList currentList = button->property("characterList").toStringList();

  QDialog dialog(this);
  dialog.setWindowTitle("Edit Character List");
  dialog.resize(400, 500);

  QVBoxLayout *layout = new QVBoxLayout(&dialog);

  QLabel *infoLabel =
      new QLabel("Add characters to this cycle group. Drag to reorder:");
  infoLabel->setStyleSheet(StyleSheet::getDialogInfoLabelStyleSheet());
  layout->addWidget(infoLabel);

  QListWidget *characterList = new QListWidget();
  characterList->addItems(currentList);

  characterList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  characterList->setDragEnabled(true);
  characterList->setAcceptDrops(true);
  characterList->setDropIndicatorShown(true);
  characterList->setDragDropMode(QAbstractItemView::InternalMove);
  characterList->setDefaultDropAction(Qt::MoveAction);

  characterList->setStyleSheet(StyleSheet::getDialogListStyleSheet());
  layout->addWidget(characterList);

  QHBoxLayout *addLayout = new QHBoxLayout();
  QLineEdit *newCharEdit = new QLineEdit();
  newCharEdit->setPlaceholderText("Enter character name");
  newCharEdit->setStyleSheet(StyleSheet::getDialogLineEditStyleSheet());

  QString btnStyle = StyleSheet::getDialogButtonStyleSheet();

  QPushButton *addButton = new QPushButton("Add");
  addButton->setStyleSheet(btnStyle);

  addLayout->addWidget(newCharEdit);
  addLayout->addWidget(addButton);
  layout->addLayout(addLayout);

  QHBoxLayout *listButtonLayout = new QHBoxLayout();
  QPushButton *removeButton = new QPushButton("Remove Selected");
  QPushButton *moveUpButton = new QPushButton("Move Up");
  QPushButton *moveDownButton = new QPushButton("Move Down");
  QPushButton *populateButton = new QPushButton("Populate from Open Clients");

  removeButton->setStyleSheet(btnStyle);
  moveUpButton->setStyleSheet(btnStyle);
  moveDownButton->setStyleSheet(btnStyle);
  populateButton->setStyleSheet(btnStyle);

  listButtonLayout->addWidget(removeButton);
  listButtonLayout->addWidget(moveUpButton);
  listButtonLayout->addWidget(moveDownButton);
  listButtonLayout->addWidget(populateButton);
  listButtonLayout->addStretch();
  layout->addLayout(listButtonLayout);

  QHBoxLayout *dialogButtonLayout = new QHBoxLayout();
  dialogButtonLayout->addStretch();
  QPushButton *okButton = new QPushButton("OK");
  QPushButton *cancelButton = new QPushButton("Cancel");

  okButton->setStyleSheet(btnStyle);
  cancelButton->setStyleSheet(btnStyle);

  dialogButtonLayout->addWidget(okButton);
  dialogButtonLayout->addWidget(cancelButton);
  layout->addLayout(dialogButtonLayout);

  connect(addButton, &QPushButton::clicked, [=]() {
    QString charName = newCharEdit->text().trimmed();
    if (!charName.isEmpty()) {
      characterList->addItem(charName);
      newCharEdit->clear();
    }
  });

  connect(newCharEdit, &QLineEdit::returnPressed, [=]() {
    QString charName = newCharEdit->text().trimmed();
    if (!charName.isEmpty()) {
      characterList->addItem(charName);
      newCharEdit->clear();
    }
  });

  connect(removeButton, &QPushButton::clicked, [=]() {
    QListWidgetItem *item = characterList->currentItem();
    if (item) {
      delete item;
    }
  });

  connect(moveUpButton, &QPushButton::clicked, [=]() {
    int currentRow = characterList->currentRow();
    if (currentRow > 0) {
      QListWidgetItem *item = characterList->takeItem(currentRow);
      characterList->insertItem(currentRow - 1, item);
      characterList->setCurrentRow(currentRow - 1);
    }
  });

  connect(moveDownButton, &QPushButton::clicked, [=]() {
    int currentRow = characterList->currentRow();
    if (currentRow >= 0 && currentRow < characterList->count() - 1) {
      QListWidgetItem *item = characterList->takeItem(currentRow);
      characterList->insertItem(currentRow + 1, item);
      characterList->setCurrentRow(currentRow + 1);
    }
  });

  connect(populateButton, &QPushButton::clicked, [=]() {
    WindowCapture capture;
    QVector<WindowInfo> windows = capture.getEVEWindows();

    if (windows.isEmpty()) {
      QMessageBox::information(const_cast<ConfigDialog *>(this),
                               "No Windows Found",
                               "No EVE Online windows are currently open.");
      return;
    }

    std::sort(windows.begin(), windows.end(),
              [](const WindowInfo &a, const WindowInfo &b) {
                return a.creationTime < b.creationTime;
              });

    QSet<QString> existingCharacters;
    for (int i = 0; i < characterList->count(); ++i) {
      existingCharacters.insert(characterList->item(i)->text());
    }

    int addedCount = 0;
    for (const WindowInfo &window : windows) {
      QString characterName = window.title;
      if (characterName.startsWith("EVE - ")) {
        characterName = characterName.mid(6);
      }

      if (existingCharacters.contains(characterName)) {
        continue;
      }

      characterList->addItem(characterName);
      existingCharacters.insert(characterName);
      addedCount++;
    }

    if (addedCount > 0) {
      QMessageBox::information(const_cast<ConfigDialog *>(this),
                               "Characters Added",
                               QString("Added %1 character%2 to the list.")
                                   .arg(addedCount)
                                   .arg(addedCount == 1 ? "" : "s"));
    } else {
      QMessageBox::information(const_cast<ConfigDialog *>(this),
                               "No New Characters",
                               "All open characters are already in the list.");
    }
  });

  connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
  connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

  dialog.setStyleSheet(StyleSheet::getDialogStyleSheetForWidget());

  if (dialog.exec() == QDialog::Accepted) {
    QStringList newList;
    for (int i = 0; i < characterList->count(); ++i) {
      newList.append(characterList->item(i)->text());
    }

    button->setProperty("characterList", newList);

    if (newList.isEmpty()) {
      button->setText("(No characters)");
    } else if (newList.count() == 1) {
      button->setText(newList.first());
    } else {
      button->setText(QString("%1 characters").arg(newList.count()));
    }
  }
}

void ConfigDialog::onAddNeverMinimizeCharacter() {
  int row = m_neverMinimizeTable->rowCount();
  m_neverMinimizeTable->insertRow(row);

  QTableWidgetItem *nameItem = new QTableWidgetItem("");
  nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);
  m_neverMinimizeTable->setItem(row, 0, nameItem);

  QWidget *buttonContainer = new QWidget();
  QHBoxLayout *buttonLayout = new QHBoxLayout(buttonContainer);
  buttonLayout->setContentsMargins(0, 0, 0, 0);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(24, 24);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 16px;"
                              "    font-weight: bold;"
                              "    padding: 0px;"
                              "}"
                              "QPushButton:hover {"
                              "    background-color: #e74c3c;"
                              "    border: 1px solid #c0392b;"
                              "}"
                              "QPushButton:pressed {"
                              "    background-color: #c0392b;"
                              "}");

  connect(deleteButton, &QPushButton::clicked, this,
          [this, row]() { m_neverMinimizeTable->removeRow(row); });

  buttonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
  m_neverMinimizeTable->setCellWidget(row, 1, buttonContainer);

  m_neverMinimizeTable->editItem(nameItem);

  m_neverMinimizeTable->scrollToBottom();
}

void ConfigDialog::onPopulateNeverMinimize() {
  WindowCapture capture;
  QVector<WindowInfo> windows = capture.getEVEWindows();

  if (windows.isEmpty()) {
    QMessageBox::information(this, "No Windows Found",
                             "No EVE Online windows are currently open.");
    return;
  }

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Populate Never Minimize List");
  msgBox.setText(QString("Found %1 open EVE Online window%2.")
                     .arg(windows.count())
                     .arg(windows.count() == 1 ? "" : "s"));
  msgBox.setInformativeText(
      "Do you want to clear existing entries or add to them?");

  QPushButton *clearButton =
      msgBox.addButton("Clear & Replace", QMessageBox::ActionRole);
  QPushButton *addButton =
      msgBox.addButton("Add to Existing", QMessageBox::ActionRole);
  QPushButton *cancelButton =
      msgBox.addButton("Cancel", QMessageBox::RejectRole);

  msgBox.setStyleSheet(StyleSheet::getMessageBoxStyleSheet());

  msgBox.exec();

  if (msgBox.clickedButton() == cancelButton) {
    return;
  }

  bool clearExisting = (msgBox.clickedButton() == clearButton);

  QSet<QString> existingCharacters;
  if (!clearExisting) {
    for (int row = 0; row < m_neverMinimizeTable->rowCount(); ++row) {
      QTableWidgetItem *item = m_neverMinimizeTable->item(row, 0);
      if (item) {
        QString charName = item->text().trimmed();
        if (!charName.isEmpty()) {
          existingCharacters.insert(charName);
        }
      }
    }
  } else {
    m_neverMinimizeTable->setRowCount(0);
  }

  int addedCount = 0;
  for (const WindowInfo &window : windows) {
    QString characterName = window.title;
    if (characterName.startsWith("EVE - ")) {
      characterName = characterName.mid(6);
    } else {
      continue;
    }

    if (characterName.trimmed().isEmpty()) {
      continue;
    }

    if (!clearExisting && existingCharacters.contains(characterName)) {
      continue;
    }

    int row = m_neverMinimizeTable->rowCount();
    m_neverMinimizeTable->insertRow(row);

    QTableWidgetItem *nameItem = new QTableWidgetItem(characterName);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    m_neverMinimizeTable->setItem(row, 0, nameItem);

    QWidget *buttonContainer = new QWidget();
    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(0, 0, 0, 0);

    QPushButton *deleteButton = new QPushButton("×");
    deleteButton->setFixedSize(24, 24);
    deleteButton->setStyleSheet("QPushButton {"
                                "    background-color: #3a3a3a;"
                                "    color: #ffffff;"
                                "    border: 1px solid #555555;"
                                "    border-radius: 4px;"
                                "    font-size: 16px;"
                                "    font-weight: bold;"
                                "    padding: 0px;"
                                "}"
                                "QPushButton:hover {"
                                "    background-color: #e74c3c;"
                                "    border: 1px solid #c0392b;"
                                "}"
                                "QPushButton:pressed {"
                                "    background-color: #c0392b;"
                                "}");

    connect(deleteButton, &QPushButton::clicked, this,
            [this, row]() { m_neverMinimizeTable->removeRow(row); });

    buttonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
    m_neverMinimizeTable->setCellWidget(row, 1, buttonContainer);

    addedCount++;
  }

  if (addedCount > 0) {
    QMessageBox::information(
        this, "Characters Added",
        QString("Added %1 character%2 to the never minimize list.")
            .arg(addedCount)
            .arg(addedCount == 1 ? "" : "s"));
  } else {
    QMessageBox::information(this, "No New Characters",
                             "All open characters are already in the list.");
  }
}

void ConfigDialog::onAddCharacterColor() {
  int row = m_characterColorsTable->rowCount();
  m_characterColorsTable->insertRow(row);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setPlaceholderText("Enter character name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  m_characterColorsTable->setCellWidget(row, 0, nameEdit);

  QPushButton *colorButton = new QPushButton();
  colorButton->setFixedSize(150, 28);
  colorButton->setCursor(Qt::PointingHandCursor);
  colorButton->setProperty("color", QColor("#00FFFF"));
  updateColorButton(colorButton, QColor("#00FFFF"));
  connect(colorButton, &QPushButton::clicked, this,
          &ConfigDialog::onCharacterColorButtonClicked);

  QWidget *buttonContainer = new QWidget();
  QHBoxLayout *buttonLayout = new QHBoxLayout(buttonContainer);
  buttonLayout->setContentsMargins(3, 3, 3, 3);
  buttonLayout->addWidget(colorButton);
  buttonLayout->setAlignment(Qt::AlignCenter);

  m_characterColorsTable->setCellWidget(row, 1, buttonContainer);

  QWidget *deleteButtonContainer = new QWidget();
  QHBoxLayout *deleteButtonLayout = new QHBoxLayout(deleteButtonContainer);
  deleteButtonLayout->setContentsMargins(0, 0, 0, 0);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(24, 24);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 16px;"
                              "    font-weight: bold;"
                              "    padding: 0px;"
                              "}"
                              "QPushButton:hover {"
                              "    background-color: #e74c3c;"
                              "    border: 1px solid #c0392b;"
                              "}"
                              "QPushButton:pressed {"
                              "    background-color: #c0392b;"
                              "}");

  connect(deleteButton, &QPushButton::clicked, this,
          [this, row]() { m_characterColorsTable->removeRow(row); });

  deleteButtonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
  m_characterColorsTable->setCellWidget(row, 2, deleteButtonContainer);

  m_characterColorsTable->scrollToBottom();
}

void ConfigDialog::onPopulateCharacterColors() {
  WindowCapture capture;
  QVector<WindowInfo> windows = capture.getEVEWindows();

  if (windows.isEmpty()) {
    QMessageBox::information(this, "No Windows Found",
                             "No EVE Online windows are currently open.");
    return;
  }

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Populate Character Colors");
  msgBox.setText(QString("Found %1 open EVE Online window%2.")
                     .arg(windows.count())
                     .arg(windows.count() == 1 ? "" : "s"));
  msgBox.setInformativeText(
      "Do you want to clear existing entries or add to them?");

  QPushButton *clearButton =
      msgBox.addButton("Clear & Replace", QMessageBox::ActionRole);
  QPushButton *addButton =
      msgBox.addButton("Add to Existing", QMessageBox::ActionRole);
  QPushButton *cancelButton =
      msgBox.addButton("Cancel", QMessageBox::RejectRole);

  msgBox.setStyleSheet(StyleSheet::getMessageBoxStyleSheet());

  msgBox.exec();

  if (msgBox.clickedButton() == cancelButton) {
    return;
  }

  bool clearExisting = (msgBox.clickedButton() == clearButton);

  QSet<QString> existingCharacters;
  if (!clearExisting) {
    for (int row = 0; row < m_characterColorsTable->rowCount(); ++row) {
      QLineEdit *nameEdit =
          qobject_cast<QLineEdit *>(m_characterColorsTable->cellWidget(row, 0));
      if (nameEdit && !nameEdit->text().trimmed().isEmpty()) {
        existingCharacters.insert(nameEdit->text().trimmed());
      }
    }
  } else {
    m_characterColorsTable->setRowCount(0);
  }

  int addedCount = 0;
  for (const WindowInfo &window : windows) {
    QString characterName = window.title;
    if (characterName.startsWith("EVE - ")) {
      characterName = characterName.mid(6);
    }

    if (characterName == "EVE" || characterName.trimmed().isEmpty()) {
      continue;
    }

    if (!clearExisting && existingCharacters.contains(characterName)) {
      continue;
    }

    int row = m_characterColorsTable->rowCount();
    m_characterColorsTable->insertRow(row);

    QLineEdit *nameEdit = new QLineEdit(characterName);
    nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
    m_characterColorsTable->setCellWidget(row, 0, nameEdit);

    Config &config = Config::instance();
    QColor characterColor = config.getCharacterBorderColor(characterName);
    if (!characterColor.isValid()) {
      characterColor = QColor("#00FFFF");
    }

    QPushButton *colorButton = new QPushButton();
    colorButton->setFixedSize(150, 28);
    colorButton->setCursor(Qt::PointingHandCursor);
    colorButton->setProperty("color", characterColor);
    updateColorButton(colorButton, characterColor);
    connect(colorButton, &QPushButton::clicked, this,
            &ConfigDialog::onCharacterColorButtonClicked);

    QWidget *buttonContainer = new QWidget();
    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(3, 3, 3, 3);
    buttonLayout->addWidget(colorButton);
    buttonLayout->setAlignment(Qt::AlignCenter);

    m_characterColorsTable->setCellWidget(row, 1, buttonContainer);

    QWidget *deleteButtonContainer = new QWidget();
    QHBoxLayout *deleteButtonLayout = new QHBoxLayout(deleteButtonContainer);
    deleteButtonLayout->setContentsMargins(0, 0, 0, 0);

    QPushButton *deleteButton = new QPushButton("×");
    deleteButton->setFixedSize(24, 24);
    deleteButton->setStyleSheet("QPushButton {"
                                "    background-color: #3a3a3a;"
                                "    color: #ffffff;"
                                "    border: 1px solid #555555;"
                                "    border-radius: 4px;"
                                "    font-size: 16px;"
                                "    font-weight: bold;"
                                "    padding: 0px;"
                                "}"
                                "QPushButton:hover {"
                                "    background-color: #e74c3c;"
                                "    border: 1px solid #c0392b;"
                                "}"
                                "QPushButton:pressed {"
                                "    background-color: #c0392b;"
                                "}");

    connect(deleteButton, &QPushButton::clicked, this,
            [this, row]() { m_characterColorsTable->removeRow(row); });

    deleteButtonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
    m_characterColorsTable->setCellWidget(row, 2, deleteButtonContainer);

    addedCount++;
  }

  if (addedCount > 0) {
    QMessageBox::information(
        this, "Characters Added",
        QString("Added %1 character%2 to the color customization list.")
            .arg(addedCount)
            .arg(addedCount == 1 ? "" : "s"));
  } else {
    QMessageBox::information(this, "No New Characters",
                             "All open characters are already in the list.");
  }
}

void ConfigDialog::onCharacterColorButtonClicked() {
  QPushButton *button = qobject_cast<QPushButton *>(sender());
  if (!button)
    return;

  QColor currentColor = button->property("color").value<QColor>();

  QColor newColor = QColorDialog::getColor(currentColor, this,
                                           "Select Character Highlight Color");

  if (newColor.isValid()) {
    button->setProperty("color", newColor);
    updateColorButton(button, newColor);
  }
}

void ConfigDialog::onAssignUniqueColors() {
  int rowCount = m_characterColorsTable->rowCount();

  if (rowCount == 0) {
    QMessageBox::information(
        this, "No Characters",
        "There are no characters in the table. Add characters first using 'Add "
        "Character' or 'Populate from Open Clients'.");
    return;
  }

  QStringList colorPalette = {
      "#FF5733", "#A23E48", "#33FF57", "#F79F1F", "#3357FF", "#129C95",
      "#FF33A1", "#4C5B72", "#33FFF3", "#E8D42A", "#A133FF", "#5E2C00",
      "#57FF33", "#C38D9E", "#FFD133", "#1E4D2B", "#33A1FF", "#8F45A4",
      "#FF3357", "#2D005E", "#7A33FF", "#C4DFE6", "#FF7A33", "#5D5C61",
      "#33FF7A", "#F5B994", "#7A33FF", "#4B0002", "#337AFF", "#808000",
      "#FF337A", "#D8A47F", "#FF33D1", "#0A0A0A", "#D1FF33", "#FFFFFF",
      "#33D1FF", "#8B4513", "#D133FF", "#2F4F4F"};

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Assign Unique Colors");
  msgBox.setText(
      QString(
          "This will assign unique colors to all %1 character%2 in the table.")
          .arg(rowCount)
          .arg(rowCount == 1 ? "" : "s"));
  msgBox.setInformativeText("Colors will be assigned from a predefined "
                            "palette. Do you want to continue?");
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
  msgBox.setDefaultButton(QMessageBox::Yes);
  msgBox.setStyleSheet(StyleSheet::getMessageBoxStyleSheet());

  if (msgBox.exec() != QMessageBox::Yes) {
    return;
  }

  for (int row = 0; row < rowCount; ++row) {
    QColor assignedColor(colorPalette[row % colorPalette.size()]);

    QWidget *buttonContainer = m_characterColorsTable->cellWidget(row, 1);
    if (buttonContainer) {
      QPushButton *colorButton = buttonContainer->findChild<QPushButton *>();
      if (colorButton) {
        colorButton->setProperty("color", assignedColor);
        updateColorButton(colorButton, assignedColor);
      }
    }
  }

  QMessageBox::information(
      this, "Colors Assigned",
      QString("Unique colors have been assigned to %1 character%2.")
          .arg(rowCount)
          .arg(rowCount == 1 ? "" : "s"));
}

void ConfigDialog::onAddThumbnailSize() {
  int row = m_thumbnailSizesTable->rowCount();
  m_thumbnailSizesTable->insertRow(row);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setPlaceholderText("Enter character name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  m_thumbnailSizesTable->setCellWidget(row, 0, nameEdit);

  QSpinBox *widthSpin = new QSpinBox();
  widthSpin->setRange(50, 800);
  widthSpin->setSuffix(" px");
  widthSpin->setValue(Config::instance().thumbnailWidth());
  widthSpin->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());

  QWidget *widthContainer = new QWidget();
  QHBoxLayout *widthLayout = new QHBoxLayout(widthContainer);
  widthLayout->setContentsMargins(3, 3, 3, 3);
  widthLayout->addWidget(widthSpin);
  m_thumbnailSizesTable->setCellWidget(row, 1, widthContainer);

  QSpinBox *heightSpin = new QSpinBox();
  heightSpin->setRange(50, 600);
  heightSpin->setSuffix(" px");
  heightSpin->setValue(Config::instance().thumbnailHeight());
  heightSpin->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());

  QWidget *heightContainer = new QWidget();
  QHBoxLayout *heightLayout = new QHBoxLayout(heightContainer);
  heightLayout->setContentsMargins(3, 3, 3, 3);
  heightLayout->addWidget(heightSpin);
  m_thumbnailSizesTable->setCellWidget(row, 2, heightContainer);

  QWidget *deleteButtonContainer = new QWidget();
  QHBoxLayout *deleteButtonLayout = new QHBoxLayout(deleteButtonContainer);
  deleteButtonLayout->setContentsMargins(0, 0, 0, 0);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(24, 24);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 16px;"
                              "    font-weight: bold;"
                              "    padding: 0px;"
                              "}"
                              "QPushButton:hover {"
                              "    background-color: #e74c3c;"
                              "    border: 1px solid #c0392b;"
                              "}"
                              "QPushButton:pressed {"
                              "    background-color: #c0392b;"
                              "}");

  connect(deleteButton, &QPushButton::clicked, this, [this, deleteButton]() {
    for (int r = 0; r < m_thumbnailSizesTable->rowCount(); ++r) {
      QWidget *container = m_thumbnailSizesTable->cellWidget(r, 3);
      if (container && container->findChild<QPushButton *>() == deleteButton) {
        m_thumbnailSizesTable->removeRow(r);
        break;
      }
    }
  });

  deleteButtonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
  m_thumbnailSizesTable->setCellWidget(row, 3, deleteButtonContainer);

  m_thumbnailSizesTable->scrollToBottom();
}

void ConfigDialog::onPopulateThumbnailSizes() {
  WindowCapture capture;
  QVector<WindowInfo> windows = capture.getEVEWindows();

  if (windows.isEmpty()) {
    QMessageBox::information(this, "No Windows Found",
                             "No EVE Online windows are currently open.");
    return;
  }

  QStringList characterNames;
  for (const auto &window : windows) {
    QString characterName = window.title;
    if (characterName.startsWith("EVE - ")) {
      characterName = characterName.mid(6);
    }

    if (characterName == "EVE" || characterName.trimmed().isEmpty()) {
      continue;
    }

    if (!characterNames.contains(characterName)) {
      characterNames.append(characterName);
    }
  }

  if (characterNames.isEmpty()) {
    QMessageBox::information(this, "No Characters Found",
                             "No logged-in EVE characters detected.");
    return;
  }

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Populate Thumbnail Sizes");
  msgBox.setText(QString("Found %1 logged-in character%2.")
                     .arg(characterNames.count())
                     .arg(characterNames.count() == 1 ? "" : "s"));
  msgBox.setInformativeText(
      "Do you want to clear existing entries or add to them?");

  QPushButton *clearButton =
      msgBox.addButton("Clear & Replace", QMessageBox::ActionRole);
  QPushButton *addButton =
      msgBox.addButton("Add to Existing", QMessageBox::ActionRole);
  QPushButton *cancelButton =
      msgBox.addButton("Cancel", QMessageBox::RejectRole);

  msgBox.setStyleSheet(StyleSheet::getMessageBoxStyleSheet());
  msgBox.exec();

  if (msgBox.clickedButton() == cancelButton) {
    return;
  }

  bool clearExisting = (msgBox.clickedButton() == clearButton);

  QSet<QString> existingCharacters;
  if (!clearExisting) {
    for (int row = 0; row < m_thumbnailSizesTable->rowCount(); ++row) {
      QLineEdit *nameEdit =
          qobject_cast<QLineEdit *>(m_thumbnailSizesTable->cellWidget(row, 0));
      if (nameEdit) {
        existingCharacters.insert(nameEdit->text().trimmed());
      }
    }
  } else {
    m_thumbnailSizesTable->setRowCount(0);
  }

  Config &cfg = Config::instance();
  int addedCount = 0;

  for (const QString &characterName : characterNames) {
    if (!clearExisting && existingCharacters.contains(characterName)) {
      continue;
    }

    int row = m_thumbnailSizesTable->rowCount();
    m_thumbnailSizesTable->insertRow(row);

    QLineEdit *nameEdit = new QLineEdit(characterName);
    nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
    m_thumbnailSizesTable->setCellWidget(row, 0, nameEdit);

    int width, height;
    if (cfg.hasCustomThumbnailSize(characterName)) {
      QSize customSize = cfg.getThumbnailSize(characterName);
      width = customSize.width();
      height = customSize.height();
    } else {
      width = cfg.thumbnailWidth();
      height = cfg.thumbnailHeight();
    }

    QSpinBox *widthSpin = new QSpinBox();
    widthSpin->setRange(50, 800);
    widthSpin->setSuffix(" px");
    widthSpin->setValue(width);
    widthSpin->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());

    QWidget *widthContainer = new QWidget();
    QHBoxLayout *widthLayout = new QHBoxLayout(widthContainer);
    widthLayout->setContentsMargins(3, 3, 3, 3);
    widthLayout->addWidget(widthSpin);
    m_thumbnailSizesTable->setCellWidget(row, 1, widthContainer);

    QSpinBox *heightSpin = new QSpinBox();
    heightSpin->setRange(50, 600);
    heightSpin->setSuffix(" px");
    heightSpin->setValue(height);
    heightSpin->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());

    QWidget *heightContainer = new QWidget();
    QHBoxLayout *heightLayout = new QHBoxLayout(heightContainer);
    heightLayout->setContentsMargins(3, 3, 3, 3);
    heightLayout->addWidget(heightSpin);
    m_thumbnailSizesTable->setCellWidget(row, 2, heightContainer);

    QWidget *deleteButtonContainer = new QWidget();
    QHBoxLayout *deleteButtonLayout = new QHBoxLayout(deleteButtonContainer);
    deleteButtonLayout->setContentsMargins(0, 0, 0, 0);

    QPushButton *deleteButton = new QPushButton("×");
    deleteButton->setFixedSize(24, 24);
    deleteButton->setStyleSheet("QPushButton {"
                                "    background-color: #3a3a3a;"
                                "    color: #ffffff;"
                                "    border: 1px solid #555555;"
                                "    border-radius: 4px;"
                                "    font-size: 16px;"
                                "    font-weight: bold;"
                                "    padding: 0px;"
                                "}"
                                "QPushButton:hover {"
                                "    background-color: #e74c3c;"
                                "    border: 1px solid #c0392b;"
                                "}"
                                "QPushButton:pressed {"
                                "    background-color: #c0392b;"
                                "}");

    connect(deleteButton, &QPushButton::clicked, this, [this, deleteButton]() {
      for (int r = 0; r < m_thumbnailSizesTable->rowCount(); ++r) {
        QWidget *container = m_thumbnailSizesTable->cellWidget(r, 3);
        if (container &&
            container->findChild<QPushButton *>() == deleteButton) {
          m_thumbnailSizesTable->removeRow(r);
          break;
        }
      }
    });

    deleteButtonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
    m_thumbnailSizesTable->setCellWidget(row, 3, deleteButtonContainer);

    addedCount++;
  }

  QString resultMsg = clearExisting ? QString("Replaced with %1 character%2.")
                                          .arg(addedCount)
                                          .arg(addedCount == 1 ? "" : "s")
                                    : QString("Added %1 new character%2.")
                                          .arg(addedCount)
                                          .arg(addedCount == 1 ? "" : "s");

  QMessageBox::information(this, "Populate Complete", resultMsg);
}

void ConfigDialog::onRemoveThumbnailSize() {
  int currentRow = m_thumbnailSizesTable->currentRow();
  if (currentRow >= 0) {
    m_thumbnailSizesTable->removeRow(currentRow);
  }
}

void ConfigDialog::onResetThumbnailSizesToDefault() {
  if (m_thumbnailSizesTable->rowCount() == 0) {
    return;
  }

  QMessageBox::StandardButton reply = QMessageBox::question(
      this, "Reset All Sizes",
      "Are you sure you want to remove all custom thumbnail sizes?\n"
      "All characters will revert to the default size.",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    m_thumbnailSizesTable->setRowCount(0);
  }
}

void ConfigDialog::onAddProcessName() {
  int row = m_processNamesTable->rowCount();
  m_processNamesTable->insertRow(row);

  QTableWidgetItem *processItem = new QTableWidgetItem("");
  processItem->setFlags(processItem->flags() | Qt::ItemIsEditable);
  m_processNamesTable->setItem(row, 0, processItem);

  QWidget *buttonContainer = new QWidget();
  QHBoxLayout *buttonLayout = new QHBoxLayout(buttonContainer);
  buttonLayout->setContentsMargins(0, 0, 0, 0);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(24, 24);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 16px;"
                              "    font-weight: bold;"
                              "    padding: 0px;"
                              "}"
                              "QPushButton:hover {"
                              "    background-color: #e74c3c;"
                              "    border: 1px solid #c0392b;"
                              "}"
                              "QPushButton:pressed {"
                              "    background-color: #c0392b;"
                              "}");

  connect(deleteButton, &QPushButton::clicked, this,
          [this, row]() { m_processNamesTable->removeRow(row); });

  buttonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
  m_processNamesTable->setCellWidget(row, 1, buttonContainer);

  m_processNamesTable->editItem(processItem);
}

void ConfigDialog::onPopulateProcessNames() {
  QMap<QString, QString> processToTitle;

  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *processMap = reinterpret_cast<QMap<QString, QString> *>(lParam);

        if (!IsWindowVisible(hwnd)) {
          return TRUE;
        }

        wchar_t title[256];
        int length =
            GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));
        if (length == 0) {
          return TRUE;
        }
        QString titleStr = QString::fromWCharArray(title, length);
        if (titleStr.isEmpty() || titleStr.contains("EVEAPMPreview")) {
          return TRUE;
        }

        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);

        QString processName;
        HANDLE hProcess = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
        if (hProcess) {
          wchar_t processNameBuffer[MAX_PATH];
          if (GetModuleBaseNameW(hProcess, NULL, processNameBuffer, MAX_PATH)) {
            processName = QString::fromWCharArray(processNameBuffer);
          }
          CloseHandle(hProcess);
        }

        if (!processName.isEmpty() && !processMap->contains(processName)) {
          processMap->insert(processName, titleStr);
        }

        return TRUE;
      },
      reinterpret_cast<LPARAM>(&processToTitle));

  if (processToTitle.isEmpty()) {
    QMessageBox::information(
        this, "No Processes Found",
        "No visible windows with process information are currently open.");
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle("Select Process Names");
  dialog.resize(700, 500);

  QVBoxLayout *layout = new QVBoxLayout(&dialog);

  QLabel *infoLabel = new QLabel(
      QString("Found %1 unique process%2. Select the ones you want to monitor:")
          .arg(processToTitle.count())
          .arg(processToTitle.count() == 1 ? "" : "es"));
  infoLabel->setWordWrap(true);
  layout->addWidget(infoLabel);

  QTableWidget *tableWidget = new QTableWidget();
  tableWidget->setColumnCount(2);
  tableWidget->setHorizontalHeaderLabels(
      {"Process Name", "Example Window Title"});
  tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableWidget->setSelectionMode(QAbstractItemView::MultiSelection);
  tableWidget->horizontalHeader()->setStretchLastSection(true);
  tableWidget->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);

  for (auto it = processToTitle.constBegin(); it != processToTitle.constEnd();
       ++it) {
    int row = tableWidget->rowCount();
    tableWidget->insertRow(row);

    QTableWidgetItem *processItem = new QTableWidgetItem(it.key());
    QTableWidgetItem *titleItem = new QTableWidgetItem(it.value());

    tableWidget->setItem(row, 0, processItem);
    tableWidget->setItem(row, 1, titleItem);
  }

  layout->addWidget(tableWidget);

  QHBoxLayout *buttonLayout = new QHBoxLayout();
  QPushButton *selectAllButton = new QPushButton("Select All");
  QPushButton *clearSelectionButton = new QPushButton("Clear Selection");
  QPushButton *okButton = new QPushButton("OK");
  QPushButton *cancelButton = new QPushButton("Cancel");

  buttonLayout->addWidget(selectAllButton);
  buttonLayout->addWidget(clearSelectionButton);
  buttonLayout->addStretch();
  buttonLayout->addWidget(okButton);
  buttonLayout->addWidget(cancelButton);
  layout->addLayout(buttonLayout);

  connect(selectAllButton, &QPushButton::clicked, tableWidget,
          &QTableWidget::selectAll);
  connect(clearSelectionButton, &QPushButton::clicked, tableWidget,
          &QTableWidget::clearSelection);
  connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
  connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

  dialog.setStyleSheet(StyleSheet::getDialogStyleSheetForWidget());
  tableWidget->setStyleSheet(StyleSheet::getTableStyleSheet());

  if (dialog.exec() == QDialog::Accepted) {
    QList<QTableWidgetItem *> selectedItems = tableWidget->selectedItems();
    if (selectedItems.isEmpty()) {
      return;
    }

    QSet<QString> existingProcesses;
    for (int row = 0; row < m_processNamesTable->rowCount(); ++row) {
      QTableWidgetItem *item = m_processNamesTable->item(row, 0);
      if (item) {
        QString processName = item->text().trimmed();
        if (!processName.isEmpty()) {
          existingProcesses.insert(processName.toLower());
        }
      }
    }

    QSet<QString> selectedProcesses;
    for (QTableWidgetItem *item : selectedItems) {
      if (item->column() == 0) {
        QString processName = item->text().trimmed();
        if (!processName.isEmpty() &&
            processName.compare("exefile.exe", Qt::CaseInsensitive) != 0) {
          selectedProcesses.insert(processName);
        }
      }
    }

    int addedCount = 0;
    for (const QString &processName : selectedProcesses) {
      if (!existingProcesses.contains(processName.toLower())) {
        int row = m_processNamesTable->rowCount();
        m_processNamesTable->insertRow(row);

        QTableWidgetItem *processItem = new QTableWidgetItem(processName);
        processItem->setFlags(processItem->flags() | Qt::ItemIsEditable);
        m_processNamesTable->setItem(row, 0, processItem);

        QWidget *buttonContainer = new QWidget();
        QHBoxLayout *buttonLayout = new QHBoxLayout(buttonContainer);
        buttonLayout->setContentsMargins(0, 0, 0, 0);

        QPushButton *deleteButton = new QPushButton("×");
        deleteButton->setFixedSize(24, 24);
        deleteButton->setStyleSheet("QPushButton {"
                                    "    background-color: #3a3a3a;"
                                    "    color: #ffffff;"
                                    "    border: 1px solid #555555;"
                                    "    border-radius: 4px;"
                                    "    font-size: 16px;"
                                    "    font-weight: bold;"
                                    "    padding: 0px;"
                                    "}"
                                    "QPushButton:hover {"
                                    "    background-color: #e74c3c;"
                                    "    border: 1px solid #c0392b;"
                                    "}"
                                    "QPushButton:pressed {"
                                    "    background-color: #c0392b;"
                                    "}");

        connect(deleteButton, &QPushButton::clicked, this,
                [this, row]() { m_processNamesTable->removeRow(row); });

        buttonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
        m_processNamesTable->setCellWidget(row, 1, buttonContainer);

        existingProcesses.insert(processName.toLower());
        addedCount++;
      }
    }

    if (addedCount > 0) {
      QMessageBox::information(
          this, "Processes Added",
          QString("Added %1 process name%2 to the Extra Previews list.")
              .arg(addedCount)
              .arg(addedCount == 1 ? "" : "s"));
    } else {
      QMessageBox::information(
          this, "No New Processes",
          "All selected processes are already in the list.");
    }
  }
}

void ConfigDialog::tagWidget(QWidget *widget, const QStringList &keywords) {
  widget->setProperty("searchKeywords", keywords);
}

void ConfigDialog::onGlobalSearchChanged(const QString &text) {
  performGlobalSearch(text);
}

void ConfigDialog::performGlobalSearch(const QString &searchText) {
  QString lowerSearch = searchText.trimmed().toLower();

  for (int pageIndex = 0; pageIndex < m_stackedWidget->count(); ++pageIndex) {
    QWidget *page = m_stackedWidget->widget(pageIndex);
    if (!page)
      continue;

    QList<QWidget *> sections = page->findChildren<QWidget *>();

    bool pageHasVisibleSection = false;

    for (QWidget *section : sections) {
      if (!section->property("searchKeywords").isValid()) {
        continue;
      }

      bool matchesSearch = false;

      if (lowerSearch.isEmpty()) {
        matchesSearch = true;
      } else {
        QStringList keywords =
            section->property("searchKeywords").toStringList();
        for (const QString &keyword : keywords) {
          if (keyword.toLower().contains(lowerSearch) ||
              lowerSearch.contains(keyword.toLower())) {
            matchesSearch = true;
            break;
          }
        }
      }

      section->setVisible(matchesSearch);

      if (matchesSearch) {
        pageHasVisibleSection = true;
      }
    }

    if (pageIndex < m_categoryList->count()) {
      if (lowerSearch.isEmpty()) {
        m_categoryList->item(pageIndex)->setHidden(false);
      } else {
        m_categoryList->item(pageIndex)->setHidden(!pageHasVisibleSection);

        if (pageHasVisibleSection &&
            m_stackedWidget->currentIndex() != pageIndex) {
          QWidget *currentPage = m_stackedWidget->currentWidget();
          QList<QWidget *> currentSections =
              currentPage->findChildren<QWidget *>();
          bool currentHasVisible = false;
          for (QWidget *sec : currentSections) {
            if (sec->property("searchKeywords").isValid() && sec->isVisible()) {
              currentHasVisible = true;
              break;
            }
          }

          if (!currentHasVisible) {
            m_categoryList->setCurrentRow(pageIndex);
            m_stackedWidget->setCurrentIndex(pageIndex);
          }
        }
      }
    }
  }
}

void ConfigDialog::onResetAppearanceDefaults() {
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Reset Appearance Settings");
  msgBox.setText("Are you sure you want to reset all appearance settings to "
                 "their default values?");
  msgBox.setInformativeText("This will reset thumbnail size, opacity, "
                            "highlighting, and overlay settings.");
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox.setDefaultButton(QMessageBox::No);
  msgBox.setStyleSheet(StyleSheet::getMessageBoxStyleSheet());

  if (msgBox.exec() == QMessageBox::Yes) {
    m_thumbnailWidthSpin->setValue(Config::DEFAULT_THUMBNAIL_WIDTH);
    m_thumbnailHeightSpin->setValue(Config::DEFAULT_THUMBNAIL_HEIGHT);
    m_opacitySpin->setValue(Config::DEFAULT_THUMBNAIL_OPACITY);

    m_highlightActiveCheck->setChecked(Config::DEFAULT_UI_HIGHLIGHT_ACTIVE);
    m_highlightColor = QColor(Config::DEFAULT_UI_HIGHLIGHT_COLOR);
    updateColorButton(m_highlightColorButton, m_highlightColor);
    m_highlightBorderWidthSpin->setValue(
        Config::DEFAULT_UI_HIGHLIGHT_BORDER_WIDTH);

    m_showCharacterNameCheck->setChecked(
        Config::DEFAULT_OVERLAY_SHOW_CHARACTER);
    m_characterNameColor = QColor(Config::DEFAULT_OVERLAY_CHARACTER_COLOR);
    updateColorButton(m_characterNameColorButton, m_characterNameColor);
    m_characterNamePositionCombo->setCurrentIndex(
        Config::DEFAULT_OVERLAY_CHARACTER_POSITION);
    Config::instance().setCharacterNameFont(
        QFont(Config::DEFAULT_OVERLAY_FONT_FAMILY,
              Config::DEFAULT_OVERLAY_FONT_SIZE));

    m_showSystemNameCheck->setChecked(Config::DEFAULT_OVERLAY_SHOW_SYSTEM);
    m_systemNameColor = QColor(Config::DEFAULT_OVERLAY_SYSTEM_COLOR);
    updateColorButton(m_systemNameColorButton, m_systemNameColor);
    m_systemNamePositionCombo->setCurrentIndex(
        Config::DEFAULT_OVERLAY_SYSTEM_POSITION);
    Config::instance().setSystemNameFont(
        QFont(Config::DEFAULT_OVERLAY_FONT_FAMILY,
              Config::DEFAULT_OVERLAY_FONT_SIZE));

    m_showBackgroundCheck->setChecked(Config::DEFAULT_OVERLAY_SHOW_BACKGROUND);
    m_backgroundColor = QColor(Config::DEFAULT_OVERLAY_BACKGROUND_COLOR);
    updateColorButton(m_backgroundColorButton, m_backgroundColor);
    m_backgroundOpacitySpin->setValue(
        Config::DEFAULT_OVERLAY_BACKGROUND_OPACITY);

    QMessageBox::information(
        this, "Reset Complete",
        "Appearance settings have been reset to defaults.\n\n"
        "Click Apply or OK to save the changes.");
  }
}

void ConfigDialog::onResetHotkeysDefaults() {
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Reset Hotkey Settings");
  msgBox.setText("Are you sure you want to reset all hotkey settings to their "
                 "default values?");
  msgBox.setInformativeText(
      "This will clear all character hotkeys and cycle groups.");
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox.setDefaultButton(QMessageBox::No);
  msgBox.setStyleSheet(StyleSheet::getMessageBoxStyleSheet());

  if (msgBox.exec() == QMessageBox::Yes) {
    m_suspendHotkeyCapture->setHotkey(0, false, false, false);
    m_closeAllClientsCapture->setHotkey(0, false, false, false);

    m_characterHotkeysTable->setRowCount(0);

    m_cycleGroupsTable->setRowCount(0);

    QMessageBox::information(this, "Reset Complete",
                             "Hotkey settings have been reset to defaults.\n\n"
                             "Click Apply or OK to save the changes.");
  }
}

void ConfigDialog::onResetBehaviorDefaults() {
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Reset Behavior Settings");
  msgBox.setText("Are you sure you want to reset all behavior settings to "
                 "their default values?");
  msgBox.setInformativeText(
      "This will reset window management and positioning settings.");
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox.setDefaultButton(QMessageBox::No);
  msgBox.setStyleSheet(StyleSheet::getMessageBoxStyleSheet());

  if (msgBox.exec() == QMessageBox::Yes) {
    m_alwaysOnTopCheck->setChecked(Config::DEFAULT_WINDOW_ALWAYS_ON_TOP);
    m_minimizeInactiveCheck->setChecked(
        Config::DEFAULT_WINDOW_MINIMIZE_INACTIVE);
    m_minimizeDelaySpin->setValue(Config::DEFAULT_WINDOW_MINIMIZE_DELAY);
    m_saveClientLocationCheck->setChecked(
        Config::DEFAULT_WINDOW_SAVE_CLIENT_LOCATION);

    m_neverMinimizeTable->setRowCount(0);

    m_rememberPositionsCheck->setChecked(Config::DEFAULT_POSITION_REMEMBER);
    m_enableSnappingCheck->setChecked(Config::DEFAULT_POSITION_ENABLE_SNAPPING);
    m_snapDistanceSpin->setValue(Config::DEFAULT_POSITION_SNAP_DISTANCE);
    m_lockPositionsCheck->setChecked(Config::DEFAULT_POSITION_LOCK);

    QMessageBox::information(
        this, "Reset Complete",
        "Behavior settings have been reset to defaults.\n\n"
        "Click Apply or OK to save the changes.");
  }
}

void ConfigDialog::onResetCombatMessagesDefaults() {
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Reset Combat Event Messages Settings");
  msgBox.setText("Are you sure you want to reset all combat event messages "
                 "settings to their default values?");
  msgBox.setInformativeText("This will reset position, font, event types, "
                            "colors, durations, and mining timeout.");
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox.setDefaultButton(QMessageBox::No);
  msgBox.setStyleSheet(StyleSheet::getMessageBoxStyleSheet());

  if (msgBox.exec() == QMessageBox::Yes) {
    m_showCombatMessagesCheck->setChecked(
        Config::DEFAULT_COMBAT_MESSAGES_ENABLED);

    m_combatMessagePositionCombo->setCurrentIndex(
        Config::DEFAULT_COMBAT_MESSAGE_POSITION);

    Config::instance().setCombatMessageFont(
        QFont(Config::DEFAULT_OVERLAY_FONT_FAMILY,
              Config::DEFAULT_OVERLAY_FONT_SIZE));

    QStringList defaultEvents = Config::DEFAULT_COMBAT_MESSAGE_EVENT_TYPES();
    for (auto it = m_eventColorButtons.constBegin();
         it != m_eventColorButtons.constEnd(); ++it) {
      QString eventType = it.key();

      QCheckBox *checkbox = nullptr;
      if (eventType == "fleet_invite")
        checkbox = m_combatEventFleetInviteCheck;
      else if (eventType == "follow_warp")
        checkbox = m_combatEventFollowWarpCheck;
      else if (eventType == "regroup")
        checkbox = m_combatEventRegroupCheck;
      else if (eventType == "compression")
        checkbox = m_combatEventCompressionCheck;
      else if (eventType == "mining_started")
        checkbox = m_combatEventMiningStartCheck;
      else if (eventType == "mining_stopped")
        checkbox = m_combatEventMiningStopCheck;

      if (checkbox) {
        checkbox->setChecked(defaultEvents.contains(eventType));
      }

      QColor defaultColor(Config::DEFAULT_COMBAT_MESSAGE_COLOR);
      updateColorButton(it.value(), defaultColor);

      if (m_eventDurationSpins.contains(eventType)) {
        m_eventDurationSpins[eventType]->setValue(
            Config::DEFAULT_COMBAT_MESSAGE_DURATION / 1000);
      }

      if (m_eventBorderCheckBoxes.contains(eventType)) {
        m_eventBorderCheckBoxes[eventType]->setChecked(
            Config::DEFAULT_COMBAT_EVENT_BORDER_HIGHLIGHT);
      }
    }

    m_miningTimeoutSpin->setValue(Config::DEFAULT_MINING_TIMEOUT_SECONDS);

    QMessageBox::information(
        this, "Reset Complete",
        "Combat event messages settings have been reset to defaults.\n\n"
        "Click Apply or OK to save the changes.");
  }
}

void ConfigDialog::onAspectRatio16_9() {
  int width = m_thumbnailWidthSpin->value();
  int height = static_cast<int>(width * 9.0 / 16.0);
  m_thumbnailHeightSpin->setValue(height);
}

void ConfigDialog::onAspectRatio21_9() {
  int width = m_thumbnailWidthSpin->value();
  int height = static_cast<int>(width * 9.0 / 21.0);
  m_thumbnailHeightSpin->setValue(height);
}

void ConfigDialog::onAspectRatio4_3() {
  int width = m_thumbnailWidthSpin->value();
  int height = static_cast<int>(width * 3.0 / 4.0);
  m_thumbnailHeightSpin->setValue(height);
}

void ConfigDialog::onBrowseLegacySettings() {
  QString fileName = QFileDialog::getOpenFileName(
      this, "Select Legacy Settings File", QString(),
      "JSON Files (*.json);;All Files (*.*)");

  if (!fileName.isEmpty()) {
    parseLegacySettingsFile(fileName);
  }
}

void ConfigDialog::onCopyAllLegacySettings() {
  if (!m_evexProfiles.isEmpty()) {
    QStringList profileNames = m_evexProfiles.keys();
    int importedCount = 0;
    QStringList importedProfileNames;

    for (const QString &profileName : profileNames) {
      m_currentEVEXProfileName = profileName;

      QVariantMap profile = m_evexProfiles[profileName].toMap();
      m_legacySettings.clear();

      if (profile.contains("Thumbnail Settings")) {
        QVariantMap thumbSettings = profile["Thumbnail Settings"].toMap();
        if (thumbSettings.contains("ShowThumbnailsAlwaysOnTop")) {
          m_legacySettings["ShowThumbnailsAlwaysOnTop"] =
              thumbSettings["ShowThumbnailsAlwaysOnTop"];
        }
        if (thumbSettings.contains("ShowClientHighlightBorder")) {
          m_legacySettings["EnableActiveClientHighlight"] =
              thumbSettings["ShowClientHighlightBorder"];
        }
        if (thumbSettings.contains("ClientHighligtColor")) {
          m_legacySettings["ActiveClientHighlightColor"] =
              thumbSettings["ClientHighligtColor"];
        }
        if (thumbSettings.contains("ClientHighligtBorderthickness")) {
          m_legacySettings["ActiveClientHighlightThickness"] =
              thumbSettings["ClientHighligtBorderthickness"];
        }
        if (thumbSettings.contains("ShowThumbnailTextOverlay")) {
          m_legacySettings["ShowThumbnailOverlays"] =
              thumbSettings["ShowThumbnailTextOverlay"];
        }
        if (thumbSettings.contains("ThumbnailTextColor")) {
          m_legacySettings["OverlayLabelColor"] =
              thumbSettings["ThumbnailTextColor"];
        }
        if (thumbSettings.contains("ThumbnailOpacity")) {
          int opacity = thumbSettings["ThumbnailOpacity"].toInt();
          m_legacySettings["ThumbnailsOpacity"] = opacity / 100.0;
        }
        if (thumbSettings.contains("HideThumbnailsOnLostFocus")) {
          m_legacySettings["HideThumbnailsOnLostFocus"] =
              thumbSettings["HideThumbnailsOnLostFocus"];
        }
      }

      if (profile.contains("Client Settings")) {
        QVariantMap clientSettings = profile["Client Settings"].toMap();
        if (clientSettings.contains("MinimizeInactiveClients")) {
          m_legacySettings["MinimizeInactiveClients"] =
              clientSettings["MinimizeInactiveClients"];
        }
      }

      if (profile.contains("Thumbnail Positions")) {
        QVariantMap positions = profile["Thumbnail Positions"].toMap();
        QVariantMap flatLayout;
        for (auto it = positions.constBegin(); it != positions.constEnd();
             ++it) {
          QString charName = it.key();
          QVariantMap pos = it.value().toMap();
          if (pos.contains("x") && pos.contains("y")) {
            int x = pos["x"].toInt();
            int y = pos["y"].toInt();
            if (x >= 0 && y >= 0) {
              flatLayout[charName] = QString("%1, %2").arg(x).arg(y);
            }
          }
        }
        if (!flatLayout.isEmpty()) {
          m_legacySettings["FlatLayout"] = flatLayout;
        }
      }

      if (profile.contains("Hotkey Groups")) {
        QVariantMap hotkeyGroups = profile["Hotkey Groups"].toMap();
        int groupIndex = 1;
        for (auto it = hotkeyGroups.constBegin(); it != hotkeyGroups.constEnd();
             ++it) {
          if (groupIndex > 5)
            break;
          QString groupName = it.key();
          QVariantMap group = it.value().toMap();
          if (group.contains("ForwardsHotkey")) {
            QString hotkey = group["ForwardsHotkey"].toString();
            if (!hotkey.isEmpty()) {
              m_legacySettings[QString("CycleGroup%1ForwardHotkeys")
                                   .arg(groupIndex)] = QVariantList() << hotkey;
            }
          }
          if (group.contains("BackwardsHotkey")) {
            QString hotkey = group["BackwardsHotkey"].toString();
            if (!hotkey.isEmpty()) {
              m_legacySettings[QString("CycleGroup%1BackwardHotkeys")
                                   .arg(groupIndex)] = QVariantList() << hotkey;
            }
          }
          if (group.contains("Characters")) {
            QVariantList characters = group["Characters"].toList();
            QVariantMap clientsOrder;
            for (int i = 0; i < characters.size(); ++i) {
              QString charName = characters[i].toString();
              clientsOrder[charName] = i + 1;
            }
            if (!clientsOrder.isEmpty()) {
              m_legacySettings[QString("CycleGroup%1ClientsOrder")
                                   .arg(groupIndex)] = clientsOrder;
            }
          }
          groupIndex++;
        }
      }

      if (m_evexGlobalSettings.contains("ThumbnailSnap")) {
        m_legacySettings["EnableThumbnailSnap"] =
            m_evexGlobalSettings["ThumbnailSnap"];
      }
      if (m_evexGlobalSettings.contains("ThumbnailSnap_Distance")) {
        int distance = m_evexGlobalSettings["ThumbnailSnap_Distance"].toInt();
        m_legacySettings["ThumbnailSnapToGridSizeX"] = distance;
        m_legacySettings["ThumbnailSnapToGridSizeY"] = distance;
      }

      QString sanitizedName = profileName;
      sanitizedName.replace('/', '_');
      sanitizedName.replace('\\', '_');
      sanitizedName.replace('.', '_');

      if (Config::instance().createProfile(sanitizedName, true)) {
        QString previousProfile = Config::instance().getCurrentProfileName();

        if (Config::instance().loadProfile(sanitizedName)) {
          QStringList categories = {
              "Thumbnail Settings",  "Window Behavior",
              "Overlay Settings",    "Highlight Settings",
              "Position & Snapping", "Hotkeys & Cycle Groups"};

          for (const QString &category : categories) {
            QVariantMap categorySettings;

            if (category == "Thumbnail Settings") {
              if (m_legacySettings.contains("ThumbnailSize")) {
                categorySettings["ThumbnailSize"] =
                    m_legacySettings["ThumbnailSize"];
              }
              if (m_legacySettings.contains("ThumbnailsOpacity")) {
                categorySettings["ThumbnailsOpacity"] =
                    m_legacySettings["ThumbnailsOpacity"];
              }
              if (m_legacySettings.contains("ThumbnailRefreshPeriod")) {
                categorySettings["ThumbnailRefreshPeriod"] =
                    m_legacySettings["ThumbnailRefreshPeriod"];
              }
            } else if (category == "Window Behavior") {
              if (m_legacySettings.contains("ShowThumbnailsAlwaysOnTop")) {
                categorySettings["ShowThumbnailsAlwaysOnTop"] =
                    m_legacySettings["ShowThumbnailsAlwaysOnTop"];
              }
              if (m_legacySettings.contains("MinimizeInactiveClients")) {
                categorySettings["MinimizeInactiveClients"] =
                    m_legacySettings["MinimizeInactiveClients"];
              }
              if (m_legacySettings.contains("HideActiveClientThumbnail")) {
                categorySettings["HideActiveClientThumbnail"] =
                    m_legacySettings["HideActiveClientThumbnail"];
              }
              if (m_legacySettings.contains("HideLoginClientThumbnail")) {
                categorySettings["HideLoginClientThumbnail"] =
                    m_legacySettings["HideLoginClientThumbnail"];
              }
            } else if (category == "Overlay Settings") {
              if (m_legacySettings.contains("ShowThumbnailOverlays")) {
                categorySettings["ShowThumbnailOverlays"] =
                    m_legacySettings["ShowThumbnailOverlays"];
              }
              if (m_legacySettings.contains("OverlayLabelColor")) {
                categorySettings["OverlayLabelColor"] =
                    m_legacySettings["OverlayLabelColor"];
              }
              if (m_legacySettings.contains("OverlayLabelAnchor")) {
                categorySettings["OverlayLabelAnchor"] =
                    m_legacySettings["OverlayLabelAnchor"];
              }
            } else if (category == "Highlight Settings") {
              if (m_legacySettings.contains("EnableActiveClientHighlight")) {
                categorySettings["EnableActiveClientHighlight"] =
                    m_legacySettings["EnableActiveClientHighlight"];
              }
              if (m_legacySettings.contains("ActiveClientHighlightColor")) {
                categorySettings["ActiveClientHighlightColor"] =
                    m_legacySettings["ActiveClientHighlightColor"];
              }
              if (m_legacySettings.contains("ActiveClientHighlightThickness")) {
                categorySettings["ActiveClientHighlightThickness"] =
                    m_legacySettings["ActiveClientHighlightThickness"];
              }
            } else if (category == "Position & Snapping") {
              if (m_legacySettings.contains("EnableThumbnailSnap")) {
                categorySettings["EnableThumbnailSnap"] =
                    m_legacySettings["EnableThumbnailSnap"];
              }
              if (m_legacySettings.contains("LockThumbnailLocation")) {
                categorySettings["LockThumbnailLocation"] =
                    m_legacySettings["LockThumbnailLocation"];
              }
              if (m_legacySettings.contains("ThumbnailSnapToGridSizeX")) {
                categorySettings["ThumbnailSnapToGridSizeX"] =
                    m_legacySettings["ThumbnailSnapToGridSizeX"];
              }
              if (m_legacySettings.contains("ThumbnailSnapToGridSizeY")) {
                categorySettings["ThumbnailSnapToGridSizeY"] =
                    m_legacySettings["ThumbnailSnapToGridSizeY"];
              }
              if (m_legacySettings.contains("FlatLayout")) {
                categorySettings["FlatLayout"] = m_legacySettings["FlatLayout"];
              }
            } else if (category == "Hotkeys & Cycle Groups") {
              for (int i = 1; i <= 5; ++i) {
                QString forwardKey =
                    QString("CycleGroup%1ForwardHotkeys").arg(i);
                QString backwardKey =
                    QString("CycleGroup%1BackwardHotkeys").arg(i);
                QString clientsKey = QString("CycleGroup%1ClientsOrder").arg(i);
                if (m_legacySettings.contains(forwardKey)) {
                  categorySettings[forwardKey] = m_legacySettings[forwardKey];
                }
                if (m_legacySettings.contains(backwardKey)) {
                  categorySettings[backwardKey] = m_legacySettings[backwardKey];
                }
                if (m_legacySettings.contains(clientsKey)) {
                  categorySettings[clientsKey] = m_legacySettings[clientsKey];
                }
              }
              if (m_legacySettings.contains("ClientHotkey")) {
                categorySettings["ClientHotkey"] =
                    m_legacySettings["ClientHotkey"];
              }
            }

            if (!categorySettings.isEmpty()) {
              copyLegacySettings(category, categorySettings);
            }
          }

          saveSettings();

          Config::instance().loadProfile(previousProfile);
          importedCount++;
          importedProfileNames << sanitizedName;
        }
      }
    }

    updateProfileDropdown();

    m_copyAllLegacyButton->setText("All Imported!");
    m_copyAllLegacyButton->setEnabled(false);

    QMessageBox::information(
        this, "Success",
        QString("Successfully imported %1 EVE-X profile(s):\n\n%2")
            .arg(importedCount)
            .arg(importedProfileNames.join("\n")));
    return;
  }

  if (m_legacySettings.isEmpty()) {
    return;
  }

  QStringList categoriesToCopy;

  QVariantMap thumbnailSettings;
  if (m_legacySettings.contains("ThumbnailSize") ||
      m_legacySettings.contains("ThumbnailsOpacity") ||
      m_legacySettings.contains("ThumbnailRefreshPeriod")) {
    categoriesToCopy << "Thumbnail Settings";
  }

  QVariantMap behaviorSettings;
  if (m_legacySettings.contains("ShowThumbnailsAlwaysOnTop") ||
      m_legacySettings.contains("MinimizeInactiveClients") ||
      m_legacySettings.contains("HideActiveClientThumbnail") ||
      m_legacySettings.contains("HideLoginClientThumbnail")) {
    categoriesToCopy << "Window Behavior";
  }

  QVariantMap overlaySettings;
  if (m_legacySettings.contains("ShowThumbnailOverlays") ||
      m_legacySettings.contains("OverlayLabelColor") ||
      m_legacySettings.contains("OverlayLabelAnchor")) {
    categoriesToCopy << "Overlay Settings";
  }

  QVariantMap highlightSettings;
  if (m_legacySettings.contains("EnableActiveClientHighlight") ||
      m_legacySettings.contains("ActiveClientHighlightColor") ||
      m_legacySettings.contains("ActiveClientHighlightThickness")) {
    categoriesToCopy << "Highlight Settings";
  }

  QVariantMap positionSettings;
  if (m_legacySettings.contains("EnableThumbnailSnap") ||
      m_legacySettings.contains("LockThumbnailLocation") ||
      m_legacySettings.contains("ThumbnailSnapToGridSizeX") ||
      m_legacySettings.contains("FlatLayout")) {
    categoriesToCopy << "Position & Snapping";
  }

  bool hasHotkeys = false;
  for (int i = 1; i <= 5; ++i) {
    if (m_legacySettings.contains(
            QString("CycleGroup%1ForwardHotkeys").arg(i)) ||
        m_legacySettings.contains(
            QString("CycleGroup%1BackwardHotkeys").arg(i)) ||
        m_legacySettings.contains(QString("CycleGroup%1ClientsOrder").arg(i))) {
      hasHotkeys = true;
      break;
    }
  }
  if (hasHotkeys || m_legacySettings.contains("ClientHotkey")) {
    categoriesToCopy << "Hotkeys & Cycle Groups";
  }

  if (categoriesToCopy.isEmpty()) {
    QMessageBox::information(this, "No Settings",
                             "No legacy settings found to copy.");
    return;
  }

  QMessageBox::StandardButton reply = QMessageBox::question(
      this, "Copy All Settings",
      QString("This will copy %1 categor%2 of settings to your current "
              "profile:\n\n%3\n\nContinue?")
          .arg(categoriesToCopy.size())
          .arg(categoriesToCopy.size() == 1 ? "y" : "ies")
          .arg(categoriesToCopy.join("\n")),
      QMessageBox::Yes | QMessageBox::No);

  if (reply != QMessageBox::Yes) {
    return;
  }

  int copiedCount = 0;
  for (const QString &category : categoriesToCopy) {
    QVariantMap categorySettings;

    if (category == "Thumbnail Settings") {
      if (m_legacySettings.contains("ThumbnailSize")) {
        categorySettings["ThumbnailSize"] = m_legacySettings["ThumbnailSize"];
      }
      if (m_legacySettings.contains("ThumbnailsOpacity")) {
        categorySettings["ThumbnailsOpacity"] =
            m_legacySettings["ThumbnailsOpacity"];
      }
      if (m_legacySettings.contains("ThumbnailRefreshPeriod")) {
        categorySettings["ThumbnailRefreshPeriod"] =
            m_legacySettings["ThumbnailRefreshPeriod"];
      }
    } else if (category == "Window Behavior") {
      if (m_legacySettings.contains("ShowThumbnailsAlwaysOnTop")) {
        categorySettings["ShowThumbnailsAlwaysOnTop"] =
            m_legacySettings["ShowThumbnailsAlwaysOnTop"];
      }
      if (m_legacySettings.contains("MinimizeInactiveClients")) {
        categorySettings["MinimizeInactiveClients"] =
            m_legacySettings["MinimizeInactiveClients"];
      }
      if (m_legacySettings.contains("HideActiveClientThumbnail")) {
        categorySettings["HideActiveClientThumbnail"] =
            m_legacySettings["HideActiveClientThumbnail"];
      }
      if (m_legacySettings.contains("HideLoginClientThumbnail")) {
        categorySettings["HideLoginClientThumbnail"] =
            m_legacySettings["HideLoginClientThumbnail"];
      }
    } else if (category == "Overlay Settings") {
      if (m_legacySettings.contains("ShowThumbnailOverlays")) {
        categorySettings["ShowThumbnailOverlays"] =
            m_legacySettings["ShowThumbnailOverlays"];
      }
      if (m_legacySettings.contains("OverlayLabelColor")) {
        categorySettings["OverlayLabelColor"] =
            m_legacySettings["OverlayLabelColor"];
      }
      if (m_legacySettings.contains("OverlayLabelAnchor")) {
        categorySettings["OverlayLabelAnchor"] =
            m_legacySettings["OverlayLabelAnchor"];
      }
    } else if (category == "Highlight Settings") {
      if (m_legacySettings.contains("EnableActiveClientHighlight")) {
        categorySettings["EnableActiveClientHighlight"] =
            m_legacySettings["EnableActiveClientHighlight"];
      }
      if (m_legacySettings.contains("ActiveClientHighlightColor")) {
        categorySettings["ActiveClientHighlightColor"] =
            m_legacySettings["ActiveClientHighlightColor"];
      }
      if (m_legacySettings.contains("ActiveClientHighlightThickness")) {
        categorySettings["ActiveClientHighlightThickness"] =
            m_legacySettings["ActiveClientHighlightThickness"];
      }
    } else if (category == "Position & Snapping") {
      if (m_legacySettings.contains("EnableThumbnailSnap")) {
        categorySettings["EnableThumbnailSnap"] =
            m_legacySettings["EnableThumbnailSnap"];
      }
      if (m_legacySettings.contains("LockThumbnailLocation")) {
        categorySettings["LockThumbnailLocation"] =
            m_legacySettings["LockThumbnailLocation"];
      }
      if (m_legacySettings.contains("ThumbnailSnapToGridSizeX")) {
        categorySettings["ThumbnailSnapToGridSizeX"] =
            m_legacySettings["ThumbnailSnapToGridSizeX"];
      }
      if (m_legacySettings.contains("ThumbnailSnapToGridSizeY")) {
        categorySettings["ThumbnailSnapToGridSizeY"] =
            m_legacySettings["ThumbnailSnapToGridSizeY"];
      }
      if (m_legacySettings.contains("FlatLayout")) {
        categorySettings["FlatLayout"] = m_legacySettings["FlatLayout"];
      }
    } else if (category == "Hotkeys & Cycle Groups") {
      for (int i = 1; i <= 5; ++i) {
        QString forwardKey = QString("CycleGroup%1ForwardHotkeys").arg(i);
        QString backwardKey = QString("CycleGroup%1BackwardHotkeys").arg(i);
        QString clientsKey = QString("CycleGroup%1ClientsOrder").arg(i);

        if (m_legacySettings.contains(forwardKey)) {
          categorySettings[forwardKey] = m_legacySettings[forwardKey];
        }
        if (m_legacySettings.contains(backwardKey)) {
          categorySettings[backwardKey] = m_legacySettings[backwardKey];
        }
        if (m_legacySettings.contains(clientsKey)) {
          categorySettings[clientsKey] = m_legacySettings[clientsKey];
        }
      }
      if (m_legacySettings.contains("ClientHotkey")) {
        categorySettings["ClientHotkey"] = m_legacySettings["ClientHotkey"];
      }
    }

    if (!categorySettings.isEmpty()) {
      copyLegacySettings(category, categorySettings);
      copiedCount++;
    }
  }

  m_copyAllLegacyButton->setText("All Copied!");
  m_copyAllLegacyButton->setEnabled(false);

  QMessageBox::information(
      this, "Success",
      QString("Successfully copied %1 categor%2 of settings.")
          .arg(copiedCount)
          .arg(copiedCount == 1 ? "y" : "ies"));
}

void ConfigDialog::onImportEVEXAsProfile() {
  if (m_evexProfiles.isEmpty() || m_currentEVEXProfileName.isEmpty()) {
    QMessageBox::warning(this, "Error", "No EVE-X profile selected.");
    return;
  }

  QTimer::singleShot(0, this, [this]() {
    bool ok;
    QString suggestedName = m_currentEVEXProfileName;

    suggestedName.replace('/', '_');
    suggestedName.replace('\\', '_');
    suggestedName.replace('.', '_');

    QString profileName = QInputDialog::getText(
        this, "Import EVE-X Profile",
        QString("Enter name for the new profile:\n(importing from: %1)")
            .arg(m_currentEVEXProfileName),
        QLineEdit::Normal, suggestedName, &ok);

    if (!ok || profileName.isEmpty()) {
      return;
    }

    if (profileName.contains('/') || profileName.contains('\\') ||
        profileName.contains('.')) {
      QMessageBox::warning(this, "Invalid Name",
                           "Profile name cannot contain slashes or dots.");
      return;
    }

    if (Config::instance().profileExists(profileName)) {
      QMessageBox::warning(
          this, "Profile Exists",
          QString("Profile \"%1\" already exists.").arg(profileName));
      return;
    }

    bool success = Config::instance().createProfile(profileName, true);

    if (!success) {
      QMessageBox::critical(
          this, "Import Failed",
          QString("Failed to create profile: %1").arg(profileName));
      return;
    }

    QString previousProfile = Config::instance().getCurrentProfileName();
    if (!Config::instance().loadProfile(profileName)) {
      QMessageBox::critical(this, "Import Failed",
                            "Failed to load the newly created profile.");
      Config::instance().deleteProfile(profileName);
      return;
    }

    QStringList categoriesToImport = {
        "Thumbnail Settings", "Window Behavior",     "Overlay Settings",
        "Highlight Settings", "Position & Snapping", "Hotkeys & Cycle Groups"};

    int importedCount = 0;
    for (const QString &category : categoriesToImport) {
      QVariantMap categorySettings;

      if (category == "Thumbnail Settings") {
        if (m_legacySettings.contains("ThumbnailSize")) {
          categorySettings["ThumbnailSize"] = m_legacySettings["ThumbnailSize"];
        }
        if (m_legacySettings.contains("ThumbnailsOpacity")) {
          categorySettings["ThumbnailsOpacity"] =
              m_legacySettings["ThumbnailsOpacity"];
        }
        if (m_legacySettings.contains("ThumbnailRefreshPeriod")) {
          categorySettings["ThumbnailRefreshPeriod"] =
              m_legacySettings["ThumbnailRefreshPeriod"];
        }
      } else if (category == "Window Behavior") {
        if (m_legacySettings.contains("ShowThumbnailsAlwaysOnTop")) {
          categorySettings["ShowThumbnailsAlwaysOnTop"] =
              m_legacySettings["ShowThumbnailsAlwaysOnTop"];
        }
        if (m_legacySettings.contains("MinimizeInactiveClients")) {
          categorySettings["MinimizeInactiveClients"] =
              m_legacySettings["MinimizeInactiveClients"];
        }
        if (m_legacySettings.contains("HideActiveClientThumbnail")) {
          categorySettings["HideActiveClientThumbnail"] =
              m_legacySettings["HideActiveClientThumbnail"];
        }
        if (m_legacySettings.contains("HideLoginClientThumbnail")) {
          categorySettings["HideLoginClientThumbnail"] =
              m_legacySettings["HideLoginClientThumbnail"];
        }
      } else if (category == "Overlay Settings") {
        if (m_legacySettings.contains("ShowThumbnailOverlays")) {
          categorySettings["ShowThumbnailOverlays"] =
              m_legacySettings["ShowThumbnailOverlays"];
        }
        if (m_legacySettings.contains("OverlayLabelColor")) {
          categorySettings["OverlayLabelColor"] =
              m_legacySettings["OverlayLabelColor"];
        }
        if (m_legacySettings.contains("OverlayLabelAnchor")) {
          categorySettings["OverlayLabelAnchor"] =
              m_legacySettings["OverlayLabelAnchor"];
        }
      } else if (category == "Highlight Settings") {
        if (m_legacySettings.contains("EnableActiveClientHighlight")) {
          categorySettings["EnableActiveClientHighlight"] =
              m_legacySettings["EnableActiveClientHighlight"];
        }
        if (m_legacySettings.contains("ActiveClientHighlightColor")) {
          categorySettings["ActiveClientHighlightColor"] =
              m_legacySettings["ActiveClientHighlightColor"];
        }
        if (m_legacySettings.contains("ActiveClientHighlightThickness")) {
          categorySettings["ActiveClientHighlightThickness"] =
              m_legacySettings["ActiveClientHighlightThickness"];
        }
      } else if (category == "Position & Snapping") {
        if (m_legacySettings.contains("EnableThumbnailSnap")) {
          categorySettings["EnableThumbnailSnap"] =
              m_legacySettings["EnableThumbnailSnap"];
        }
        if (m_legacySettings.contains("LockThumbnailLocation")) {
          categorySettings["LockThumbnailLocation"] =
              m_legacySettings["LockThumbnailLocation"];
        }
        if (m_legacySettings.contains("ThumbnailSnapToGridSizeX")) {
          categorySettings["ThumbnailSnapToGridSizeX"] =
              m_legacySettings["ThumbnailSnapToGridSizeX"];
        }
        if (m_legacySettings.contains("ThumbnailSnapToGridSizeY")) {
          categorySettings["ThumbnailSnapToGridSizeY"] =
              m_legacySettings["ThumbnailSnapToGridSizeY"];
        }
        if (m_legacySettings.contains("FlatLayout")) {
          categorySettings["FlatLayout"] = m_legacySettings["FlatLayout"];
        }
      } else if (category == "Hotkeys & Cycle Groups") {
        for (int i = 1; i <= 5; ++i) {
          QString forwardKey = QString("CycleGroup%1ForwardHotkeys").arg(i);
          QString backwardKey = QString("CycleGroup%1BackwardHotkeys").arg(i);
          QString clientsKey = QString("CycleGroup%1ClientsOrder").arg(i);

          if (m_legacySettings.contains(forwardKey)) {
            categorySettings[forwardKey] = m_legacySettings[forwardKey];
          }
          if (m_legacySettings.contains(backwardKey)) {
            categorySettings[backwardKey] = m_legacySettings[backwardKey];
          }
          if (m_legacySettings.contains(clientsKey)) {
            categorySettings[clientsKey] = m_legacySettings[clientsKey];
          }
        }
        if (m_legacySettings.contains("ClientHotkey")) {
          categorySettings["ClientHotkey"] = m_legacySettings["ClientHotkey"];
        }
      }

      if (!categorySettings.isEmpty()) {
        copyLegacySettings(category, categorySettings);
        importedCount++;
      }
    }

    saveSettings();

    Config::instance().loadProfile(previousProfile);

    updateProfileDropdown();

    m_importEVEXButton->setText("Imported!");
    m_importEVEXButton->setEnabled(false);

    QMessageBox::StandardButton switchNow = QMessageBox::question(
        this, "Import Complete",
        QString("Successfully imported EVE-X profile as \"%1\".\n\n"
                "Switch to it now?")
            .arg(profileName),
        QMessageBox::Yes | QMessageBox::No);

    if (switchNow == QMessageBox::Yes) {
      int index = m_profileCombo->findText(profileName);
      if (index >= 0) {
        m_skipProfileSwitchConfirmation = true;
        m_profileCombo->setCurrentIndex(index);
        m_skipProfileSwitchConfirmation = false;
      }
    } else {
      loadSettings();
    }
  });
}

void ConfigDialog::onBrowseChatLogDirectory() {
  QString currentPath = m_chatLogDirectoryEdit->text().trimmed();
  if (currentPath.isEmpty()) {
    currentPath = Config::instance().getDefaultChatLogDirectory();
  }

  QString dirPath = QFileDialog::getExistingDirectory(
      this, "Select Chat Log Directory", currentPath,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (!dirPath.isEmpty()) {
    m_chatLogDirectoryEdit->setText(dirPath);
  }
}

void ConfigDialog::onBrowseGameLogDirectory() {
  QString currentPath = m_gameLogDirectoryEdit->text().trimmed();
  if (currentPath.isEmpty()) {
    currentPath = Config::instance().getDefaultGameLogDirectory();
  }

  QString dirPath = QFileDialog::getExistingDirectory(
      this, "Select Game Log Directory", currentPath,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (!dirPath.isEmpty()) {
    m_gameLogDirectoryEdit->setText(dirPath);
  }
}

void ConfigDialog::parseLegacySettingsFile(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, "Error",
                         QString("Could not open file: %1").arg(filePath));
    return;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    QMessageBox::warning(
        this, "Parse Error",
        QString("Failed to parse JSON: %1").arg(parseError.errorString()));
    return;
  }

  if (!doc.isObject()) {
    QMessageBox::warning(this, "Invalid Format",
                         "The file does not contain a valid JSON object.");
    return;
  }

  m_legacyFilePath = filePath;
  QVariantMap rootMap = doc.object().toVariantMap();

  if (rootMap.contains("_Profiles") && rootMap.contains("global_Settings")) {
    parseEVEXPreviewFile(rootMap);
  } else {
    m_legacySettings = rootMap;
    QFileInfo fileInfo(filePath);
    m_legacyFilePathLabel->setText(fileInfo.fileName() + " (EVE-O-Preview)");
    m_legacyFilePathLabel->setStyleSheet(
        "color: #ffffff; font-size: 11pt; font-weight: bold;");
    displayLegacySettings();

    m_copyAllLegacyButton->setVisible(true);
    m_copyAllLegacyButton->setEnabled(true);
    m_copyAllLegacyButton->setText("Copy All");
  }
}

void ConfigDialog::parseEVEXPreviewFile(const QVariantMap &rootMap) {
  QVariantMap profiles = rootMap["_Profiles"].toMap();
  m_evexGlobalSettings = rootMap["global_Settings"].toMap();
  m_evexProfiles = profiles;

  if (profiles.isEmpty()) {
    QMessageBox::warning(this, "Error",
                         "No profiles found in EVE-X-Preview file.");
    return;
  }

  QLayoutItem *item;
  while ((item = m_legacySettingsLayout->takeAt(0)) != nullptr) {
    delete item->widget();
    delete item;
  }

  QWidget *headerSection = new QWidget();
  headerSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *headerLayout = new QVBoxLayout(headerSection);
  headerLayout->setContentsMargins(16, 12, 16, 12);
  headerLayout->setSpacing(8);

  QLabel *headerLabel = new QLabel(
      QString("Found %1 EVE-X-Preview Profile(s)").arg(profiles.size()));
  headerLabel->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  headerLayout->addWidget(headerLabel);

  QLabel *infoLabel =
      new QLabel("Select a profile to view its settings. You can then copy "
                 "individual settings categories to your current profile.");
  infoLabel->setStyleSheet("color: #b0b0b0; font-size: 10pt;");
  infoLabel->setWordWrap(true);
  headerLayout->addWidget(infoLabel);

  QStringList profileNames = profiles.keys();
  profileNames.sort();

  QWidget *selectorWidget = new QWidget();
  selectorWidget->setStyleSheet("background-color: transparent;");
  QHBoxLayout *selectorLayout = new QHBoxLayout(selectorWidget);
  selectorLayout->setContentsMargins(0, 8, 0, 0);

  QLabel *selectLabel = new QLabel("Select Profile:");
  selectLabel->setStyleSheet(
      "color: #b0b0b0; font-size: 10pt; font-weight: bold;");
  selectorLayout->addWidget(selectLabel);

  QComboBox *profileCombo = new QComboBox();
  profileCombo->addItems(profileNames);
  profileCombo->setStyleSheet("QComboBox {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 3px;"
                              "    padding: 5px 10px;"
                              "    min-width: 200px;"
                              "    font-size: 10pt;"
                              "}"
                              "QComboBox:hover {"
                              "    border: 1px solid #fdcc12;"
                              "}"
                              "QComboBox::drop-down {"
                              "    border: none;"
                              "    width: 0px;"
                              "}"
                              "QComboBox QAbstractItemView {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    selection-background-color: #fdcc12;"
                              "    selection-color: #1e1e1e;"
                              "    border: 1px solid #555555;"
                              "}");
  selectorLayout->addWidget(profileCombo);

  selectorLayout->addStretch();

  m_importEVEXButton->setVisible(true);
  m_importEVEXButton->setEnabled(true);
  m_importEVEXButton->setText("Copy Profile");
  selectorLayout->addWidget(m_importEVEXButton);

  headerLayout->addWidget(selectorWidget);

  m_legacySettingsLayout->addWidget(headerSection);

  QWidget *settingsContainer = new QWidget();
  settingsContainer->setObjectName("evexSettingsContainer");
  QVBoxLayout *settingsLayout = new QVBoxLayout(settingsContainer);
  settingsLayout->setContentsMargins(0, 0, 0, 0);
  settingsLayout->setSpacing(10);
  m_legacySettingsLayout->addWidget(settingsContainer);

  connect(profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this, profileCombo, settingsContainer]() {
            QString selectedProfile = profileCombo->currentText();
            if (!selectedProfile.isEmpty()) {
              displayEVEXProfile(selectedProfile, settingsContainer);
              m_importEVEXButton->setText("Copy Profile");
              m_importEVEXButton->setEnabled(true);
            }
          });

  if (!profileNames.isEmpty()) {
    displayEVEXProfile(profileNames.first(), settingsContainer);
  }

  m_legacySettingsLayout->addStretch();

  QFileInfo fileInfo(m_legacyFilePath);
  m_legacyFilePathLabel->setText(fileInfo.fileName() + " (EVE-X-Preview)");
  m_legacyFilePathLabel->setStyleSheet(
      "color: #ffffff; font-size: 11pt; font-weight: bold;");

  m_copyAllLegacyButton->setVisible(true);
  m_copyAllLegacyButton->setEnabled(true);
  m_copyAllLegacyButton->setText("Copy All");
}

void ConfigDialog::displayEVEXProfile(const QString &profileName,
                                      QWidget *container) {
  if (!m_evexProfiles.contains(profileName)) {
    return;
  }

  m_currentEVEXProfileName = profileName;

  QLayout *layout = container->layout();
  if (layout) {
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
      delete item->widget();
      delete item;
    }
  }

  QVariantMap profile = m_evexProfiles[profileName].toMap();

  m_legacySettings.clear();

  if (profile.contains("Thumbnail Settings")) {
    QVariantMap thumbSettings = profile["Thumbnail Settings"].toMap();

    if (thumbSettings.contains("ShowThumbnailsAlwaysOnTop")) {
      m_legacySettings["ShowThumbnailsAlwaysOnTop"] =
          thumbSettings["ShowThumbnailsAlwaysOnTop"];
    }

    if (thumbSettings.contains("ShowClientHighlightBorder")) {
      m_legacySettings["EnableActiveClientHighlight"] =
          thumbSettings["ShowClientHighlightBorder"];
    }

    if (thumbSettings.contains("ClientHighligtColor")) {
      m_legacySettings["ActiveClientHighlightColor"] =
          thumbSettings["ClientHighligtColor"];
    }

    if (thumbSettings.contains("ClientHighligtBorderthickness")) {
      m_legacySettings["ActiveClientHighlightThickness"] =
          thumbSettings["ClientHighligtBorderthickness"];
    }

    if (thumbSettings.contains("ShowThumbnailTextOverlay")) {
      m_legacySettings["ShowThumbnailOverlays"] =
          thumbSettings["ShowThumbnailTextOverlay"];
    }

    if (thumbSettings.contains("ThumbnailTextColor")) {
      m_legacySettings["OverlayLabelColor"] =
          thumbSettings["ThumbnailTextColor"];
    }

    if (thumbSettings.contains("ThumbnailOpacity")) {
      int opacity = thumbSettings["ThumbnailOpacity"].toInt();
      m_legacySettings["ThumbnailsOpacity"] = opacity / 100.0;
    }

    if (thumbSettings.contains("HideThumbnailsOnLostFocus")) {
      m_legacySettings["HideThumbnailsOnLostFocus"] =
          thumbSettings["HideThumbnailsOnLostFocus"];
    }
  }

  if (profile.contains("Client Settings")) {
    QVariantMap clientSettings = profile["Client Settings"].toMap();

    if (clientSettings.contains("MinimizeInactiveClients")) {
      m_legacySettings["MinimizeInactiveClients"] =
          clientSettings["MinimizeInactiveClients"];
    }
  }

  if (profile.contains("Thumbnail Positions")) {
    QVariantMap positions = profile["Thumbnail Positions"].toMap();
    QVariantMap flatLayout;

    for (auto it = positions.constBegin(); it != positions.constEnd(); ++it) {
      QString charName = it.key();
      QVariantMap pos = it.value().toMap();

      if (pos.contains("x") && pos.contains("y")) {
        int x = pos["x"].toInt();
        int y = pos["y"].toInt();

        if (x >= 0 && y >= 0) {
          flatLayout[charName] = QString("%1, %2").arg(x).arg(y);
        }
      }
    }

    if (!flatLayout.isEmpty()) {
      m_legacySettings["FlatLayout"] = flatLayout;
    }
  }

  if (profile.contains("Hotkey Groups")) {
    QVariantMap hotkeyGroups = profile["Hotkey Groups"].toMap();
    int groupIndex = 1;

    for (auto it = hotkeyGroups.constBegin(); it != hotkeyGroups.constEnd();
         ++it) {
      if (groupIndex > 5)
        break;

      QString groupName = it.key();
      QVariantMap group = it.value().toMap();

      if (group.contains("ForwardsHotkey")) {
        QString hotkey = group["ForwardsHotkey"].toString();
        if (!hotkey.isEmpty()) {
          m_legacySettings[QString("CycleGroup%1ForwardHotkeys")
                               .arg(groupIndex)] = QVariantList() << hotkey;
        }
      }

      if (group.contains("BackwardsHotkey")) {
        QString hotkey = group["BackwardsHotkey"].toString();
        if (!hotkey.isEmpty()) {
          m_legacySettings[QString("CycleGroup%1BackwardHotkeys")
                               .arg(groupIndex)] = QVariantList() << hotkey;
        }
      }

      if (group.contains("Characters")) {
        QVariantList characters = group["Characters"].toList();
        QVariantMap clientsOrder;

        for (int i = 0; i < characters.size(); ++i) {
          QString charName = characters[i].toString();
          clientsOrder[charName] = i + 1;
        }

        if (!clientsOrder.isEmpty()) {
          m_legacySettings[QString("CycleGroup%1ClientsOrder")
                               .arg(groupIndex)] = clientsOrder;
        }
      }

      groupIndex++;
    }
  }

  if (m_evexGlobalSettings.contains("ThumbnailSnap")) {
    m_legacySettings["EnableThumbnailSnap"] =
        m_evexGlobalSettings["ThumbnailSnap"];
  }

  if (m_evexGlobalSettings.contains("ThumbnailSnap_Distance")) {
    int distance = m_evexGlobalSettings["ThumbnailSnap_Distance"].toInt();
    m_legacySettings["ThumbnailSnapToGridSizeX"] = distance;
    m_legacySettings["ThumbnailSnapToGridSizeY"] = distance;
  }

  QFileInfo fileInfo(m_legacyFilePath);
  m_legacyFilePathLabel->setText(
      QString("%1 (EVE-X-Preview: %2)").arg(fileInfo.fileName(), profileName));
  m_legacyFilePathLabel->setStyleSheet(
      "color: #ffffff; font-size: 11pt; font-weight: bold;");

  QLayout *containerLayout = container->layout();
  if (containerLayout) {
    QLayoutItem *item;
    while ((item = containerLayout->takeAt(0)) != nullptr) {
      delete item->widget();
      delete item;
    }
  }

  displayLegacySettingsInternal(containerLayout);
}

void ConfigDialog::displayLegacySettingsInternal(QLayout *targetLayout) {
  if (m_legacySettings.isEmpty()) {
    return;
  }

  QVariantMap thumbnailSettings;
  if (m_legacySettings.contains("ThumbnailSize")) {
    thumbnailSettings["ThumbnailSize"] = m_legacySettings["ThumbnailSize"];
  }
  if (m_legacySettings.contains("ThumbnailsOpacity")) {
    thumbnailSettings["ThumbnailsOpacity"] =
        m_legacySettings["ThumbnailsOpacity"];
  }
  if (m_legacySettings.contains("ThumbnailRefreshPeriod")) {
    thumbnailSettings["ThumbnailRefreshPeriod"] =
        m_legacySettings["ThumbnailRefreshPeriod"];
  }
  if (!thumbnailSettings.isEmpty()) {
    targetLayout->addWidget(
        createLegacyCategoryWidget("Thumbnail Settings", thumbnailSettings));
  }

  QVariantMap behaviorSettings;
  if (m_legacySettings.contains("ShowThumbnailsAlwaysOnTop")) {
    behaviorSettings["ShowThumbnailsAlwaysOnTop"] =
        m_legacySettings["ShowThumbnailsAlwaysOnTop"];
  }
  if (m_legacySettings.contains("MinimizeInactiveClients")) {
    behaviorSettings["MinimizeInactiveClients"] =
        m_legacySettings["MinimizeInactiveClients"];
  }
  if (m_legacySettings.contains("HideActiveClientThumbnail")) {
    behaviorSettings["HideActiveClientThumbnail"] =
        m_legacySettings["HideActiveClientThumbnail"];
  }
  if (m_legacySettings.contains("HideLoginClientThumbnail")) {
    behaviorSettings["HideLoginClientThumbnail"] =
        m_legacySettings["HideLoginClientThumbnail"];
  }
  if (!behaviorSettings.isEmpty()) {
    targetLayout->addWidget(
        createLegacyCategoryWidget("Window Behavior", behaviorSettings));
  }

  QVariantMap overlaySettings;
  if (m_legacySettings.contains("ShowThumbnailOverlays")) {
    overlaySettings["ShowThumbnailOverlays"] =
        m_legacySettings["ShowThumbnailOverlays"];
  }
  if (m_legacySettings.contains("OverlayLabelColor")) {
    overlaySettings["OverlayLabelColor"] =
        m_legacySettings["OverlayLabelColor"];
  }
  if (m_legacySettings.contains("OverlayLabelAnchor")) {
    overlaySettings["OverlayLabelAnchor"] =
        m_legacySettings["OverlayLabelAnchor"];
  }
  if (!overlaySettings.isEmpty()) {
    targetLayout->addWidget(
        createLegacyCategoryWidget("Overlay Settings", overlaySettings));
  }

  QVariantMap highlightSettings;
  if (m_legacySettings.contains("EnableActiveClientHighlight")) {
    highlightSettings["EnableActiveClientHighlight"] =
        m_legacySettings["EnableActiveClientHighlight"];
  }
  if (m_legacySettings.contains("ActiveClientHighlightColor")) {
    highlightSettings["ActiveClientHighlightColor"] =
        m_legacySettings["ActiveClientHighlightColor"];
  }
  if (m_legacySettings.contains("ActiveClientHighlightThickness")) {
    highlightSettings["ActiveClientHighlightThickness"] =
        m_legacySettings["ActiveClientHighlightThickness"];
  }
  if (!highlightSettings.isEmpty()) {
    targetLayout->addWidget(
        createLegacyCategoryWidget("Highlight Settings", highlightSettings));
  }

  QVariantMap positionSettings;
  if (m_legacySettings.contains("EnableThumbnailSnap")) {
    positionSettings["EnableThumbnailSnap"] =
        m_legacySettings["EnableThumbnailSnap"];
  }
  if (m_legacySettings.contains("LockThumbnailLocation")) {
    positionSettings["LockThumbnailLocation"] =
        m_legacySettings["LockThumbnailLocation"];
  }
  if (m_legacySettings.contains("ThumbnailSnapToGridSizeX") &&
      m_legacySettings.contains("ThumbnailSnapToGridSizeY")) {
    positionSettings["ThumbnailSnapToGridSizeX"] =
        m_legacySettings["ThumbnailSnapToGridSizeX"];
    positionSettings["ThumbnailSnapToGridSizeY"] =
        m_legacySettings["ThumbnailSnapToGridSizeY"];
  }
  if (m_legacySettings.contains("FlatLayout")) {
    QVariantMap layout = m_legacySettings["FlatLayout"].toMap();
    if (!layout.isEmpty()) {
      positionSettings["FlatLayout"] = m_legacySettings["FlatLayout"];
    }
  }
  if (!positionSettings.isEmpty()) {
    targetLayout->addWidget(
        createLegacyCategoryWidget("Position & Snapping", positionSettings));
  }

  QVariantMap hotkeySettings;
  for (int i = 1; i <= 5; ++i) {
    QString forwardKey = QString("CycleGroup%1ForwardHotkeys").arg(i);
    QString backwardKey = QString("CycleGroup%1BackwardHotkeys").arg(i);
    QString clientsKey = QString("CycleGroup%1ClientsOrder").arg(i);

    if (m_legacySettings.contains(forwardKey)) {
      hotkeySettings[forwardKey] = m_legacySettings[forwardKey];
    }
    if (m_legacySettings.contains(backwardKey)) {
      hotkeySettings[backwardKey] = m_legacySettings[backwardKey];
    }
    if (m_legacySettings.contains(clientsKey)) {
      hotkeySettings[clientsKey] = m_legacySettings[clientsKey];
    }
  }
  if (m_legacySettings.contains("ClientHotkey")) {
    hotkeySettings["ClientHotkey"] = m_legacySettings["ClientHotkey"];
  }
  if (!hotkeySettings.isEmpty()) {
    targetLayout->addWidget(
        createLegacyCategoryWidget("Hotkeys & Cycle Groups", hotkeySettings));
  }
}

void ConfigDialog::displayLegacySettings() {
  QLayoutItem *item;
  while ((item = m_legacySettingsLayout->takeAt(0)) != nullptr) {
    delete item->widget();
    delete item;
  }

  displayLegacySettingsInternal(m_legacySettingsLayout);
}

QWidget *ConfigDialog::createLegacyCategoryWidget(const QString &categoryName,
                                                  const QVariantMap &settings) {
  QWidget *categoryWidget = new QWidget();
  categoryWidget->setStyleSheet(StyleSheet::getSectionStyleSheet());

  QVBoxLayout *categoryLayout = new QVBoxLayout(categoryWidget);
  categoryLayout->setContentsMargins(16, 12, 16, 12);
  categoryLayout->setSpacing(10);

  QLabel *categoryLabel = new QLabel(categoryName);
  categoryLabel->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  categoryLayout->addWidget(categoryLabel);

  QMap<QString, QString> friendlyNames;

  friendlyNames["ThumbnailSize"] = "Thumbnail Size";
  friendlyNames["ThumbnailsOpacity"] = "Thumbnail Opacity";

  friendlyNames["ShowThumbnailsAlwaysOnTop"] = "Always On Top";
  friendlyNames["MinimizeInactiveClients"] = "Minimize Inactive Clients";
  friendlyNames["HideActiveClientThumbnail"] = "Hide Active Client Thumbnail";
  friendlyNames["HideLoginClientThumbnail"] = "Show Not-Logged-In Clients";

  friendlyNames["ShowThumbnailOverlays"] = "Show Character Name";
  friendlyNames["OverlayLabelColor"] = "Character Name Color";
  friendlyNames["OverlayLabelAnchor"] = "Character Name Position";

  friendlyNames["EnableActiveClientHighlight"] = "Highlight Active Window";
  friendlyNames["ActiveClientHighlightColor"] = "Highlight Color";
  friendlyNames["ActiveClientHighlightThickness"] = "Highlight Border Width";

  friendlyNames["EnableThumbnailSnap"] = "Enable Snapping";
  friendlyNames["LockThumbnailLocation"] = "Lock Thumbnail Positions";
  friendlyNames["ThumbnailSnapToGridSizeX"] = "Snap Grid Size X";
  friendlyNames["ThumbnailSnapToGridSizeY"] = "Snap Grid Size Y";
  friendlyNames["FlatLayout"] = "Character Positions";

  QGridLayout *settingsGrid = new QGridLayout();
  settingsGrid->setSpacing(8);
  settingsGrid->setColumnStretch(1, 1);

  int row = 0;

  if (categoryName == "Hotkeys & Cycle Groups") {
    for (int i = 1; i <= 5; ++i) {
      QString clientsKey = QString("CycleGroup%1ClientsOrder").arg(i);
      if (!settings.contains(clientsKey)) {
        continue;
      }

      QVariantMap clientsOrder = settings[clientsKey].toMap();
      if (clientsOrder.isEmpty() ||
          (clientsOrder.size() == 1 &&
           clientsOrder.firstKey().contains("cycle group"))) {
        continue;
      }

      QString forwardKey = QString("CycleGroup%1ForwardHotkeys").arg(i);
      QString backwardKey = QString("CycleGroup%1BackwardHotkeys").arg(i);

      QString forwardHotkey = "(not set)";
      QString backwardHotkey = "(not set)";

      if (settings.contains(forwardKey)) {
        QVariantList forwardList = settings[forwardKey].toList();
        if (!forwardList.isEmpty() && !forwardList[0].toString().isEmpty()) {
          forwardHotkey = forwardList[0].toString();
        }
      }

      if (settings.contains(backwardKey)) {
        QVariantList backwardList = settings[backwardKey].toList();
        if (!backwardList.isEmpty() && !backwardList[0].toString().isEmpty()) {
          backwardHotkey = backwardList[0].toString();
        }
      }

      QMap<int, QString> orderedCharacters;
      for (auto it = clientsOrder.constBegin(); it != clientsOrder.constEnd();
           ++it) {
        QString characterName = it.key();
        if (characterName.startsWith("EVE - ")) {
          characterName = characterName.mid(6);
        }
        int order = it.value().toInt();
        orderedCharacters[order] = characterName;
      }

      QStringList charNames;
      for (auto it = orderedCharacters.constBegin();
           it != orderedCharacters.constEnd(); ++it) {
        charNames.append(it.value());
      }

      QLabel *groupLabel = new QLabel(QString("Cycle Group %1:").arg(i));
      groupLabel->setStyleSheet(
          "color: #ffffff; font-size: 10pt; font-weight: bold;");

      QString valueStr = QString("Forward: %1, Backward: %2")
                             .arg(forwardHotkey)
                             .arg(backwardHotkey);

      QLabel *valueLabel = new QLabel(valueStr);
      valueLabel->setStyleSheet("color: #ffffff; font-size: 10pt;");
      valueLabel->setWordWrap(true);

      settingsGrid->addWidget(groupLabel, row, 0, Qt::AlignTop);
      settingsGrid->addWidget(valueLabel, row, 1);
      row++;

      QLabel *charsLabel =
          new QLabel(QString("Characters (%1):").arg(charNames.size()));
      charsLabel->setStyleSheet(
          "color: #ffffff; font-size: 10pt; font-weight: bold;");

      QLabel *charsValueLabel = new QLabel(charNames.join(", "));
      charsValueLabel->setStyleSheet("color: #ffffff; font-size: 10pt;");
      charsValueLabel->setWordWrap(true);

      settingsGrid->addWidget(charsLabel, row, 0, Qt::AlignTop);
      settingsGrid->addWidget(charsValueLabel, row, 1);
      row++;
    }

    if (settings.contains("ClientHotkey")) {
      QVariantMap clientHotkeys = settings["ClientHotkey"].toMap();
      if (!clientHotkeys.isEmpty()) {
        QLabel *keyLabel = new QLabel("Character Hotkeys:");
        keyLabel->setStyleSheet(
            "color: #ffffff; font-size: 10pt; font-weight: bold;");

        QStringList hotkeysList;
        for (auto it = clientHotkeys.constBegin();
             it != clientHotkeys.constEnd(); ++it) {
          QString characterName = it.key();
          if (characterName.startsWith("EVE - ")) {
            characterName = characterName.mid(6);
          }

          QVariantList hotkeyList = it.value().toList();
          QString hotkey = "(not set)";
          if (!hotkeyList.isEmpty() && !hotkeyList[0].toString().isEmpty()) {
            hotkey = hotkeyList[0].toString();
          }

          hotkeysList.append(QString("%1: %2").arg(hotkey).arg(characterName));
        }

        QString valueStr = hotkeysList.join(", ");
        QLabel *valueLabel = new QLabel(valueStr);
        valueLabel->setStyleSheet("color: #ffffff; font-size: 10pt;");
        valueLabel->setWordWrap(true);

        settingsGrid->addWidget(keyLabel, row, 0, Qt::AlignTop);
        settingsGrid->addWidget(valueLabel, row, 1);
        row++;
      }
    }
  } else {
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
      QString legacyKey = it.key();
      QString displayName = friendlyNames.value(legacyKey, legacyKey);

      QLabel *keyLabel = new QLabel(displayName + ":");
      keyLabel->setStyleSheet(
          "color: #ffffff; font-size: 10pt; font-weight: bold;");

      QString valueStr;
      QVariant value = it.value();

      if (value.type() == QVariant::Map || value.type() == QVariant::Hash) {
        QVariantMap map = value.toMap();
        if (legacyKey == "FlatLayout") {
          int eveCount = 0;
          for (auto mapIt = map.constBegin(); mapIt != map.constEnd();
               ++mapIt) {
            if (mapIt.key().startsWith("EVE - ")) {
              eveCount++;
            }
          }
          valueStr = QString("%1 character position%2")
                         .arg(eveCount)
                         .arg(eveCount != 1 ? "s" : "");
        } else {
          valueStr = QString("{%1 items}").arg(map.size());
        }
      } else if (value.type() == QVariant::List) {
        QVariantList list = value.toList();
        if (list.isEmpty() ||
            (list.size() == 1 && list[0].toString().isEmpty())) {
          valueStr = "(not set)";
        } else {
          QStringList strList;
          for (const QVariant &item : list) {
            strList.append(item.toString());
          }
          valueStr = strList.join(", ");
        }
      } else {
        if (legacyKey == "ThumbnailsOpacity") {
          double opacity = value.toDouble();
          valueStr = QString("%1%").arg(static_cast<int>(opacity * 100));
        } else if (legacyKey == "HideLoginClientThumbnail") {
          bool hide = value.toBool();
          valueStr = hide ? "No (Hidden)" : "Yes (Shown)";
        } else if (legacyKey == "OverlayLabelAnchor") {
          int anchor = value.toInt();
          QStringList positions = {"Top Left",      "Top Center",
                                   "Top Right",     "Bottom Left",
                                   "Bottom Center", "Bottom Right"};
          if (anchor >= 0 && anchor < positions.size()) {
            valueStr = positions[anchor];
          } else {
            valueStr = value.toString();
          }
        } else {
          valueStr = value.toString();
        }
      }

      QLabel *valueLabel = new QLabel(valueStr);
      valueLabel->setStyleSheet("color: #ffffff; font-size: 10pt;");
      valueLabel->setWordWrap(true);

      settingsGrid->addWidget(keyLabel, row, 0, Qt::AlignTop);
      settingsGrid->addWidget(valueLabel, row, 1);
      row++;
    }
  }

  categoryLayout->addLayout(settingsGrid);

  QPushButton *copyButton = new QPushButton("Copy");
  copyButton->setStyleSheet(StyleSheet::getButtonStyleSheet());
  copyButton->setFixedWidth(90);
  copyButton->setProperty("category", categoryName);
  connect(copyButton, &QPushButton::clicked, this,
          [this, categoryName, settings, copyButton]() {
            copyLegacySettings(categoryName, settings);

            copyButton->setText("Copied!");
            copyButton->setEnabled(false);
          });
  categoryLayout->addWidget(copyButton, 0, Qt::AlignRight);

  return categoryWidget;
}

void ConfigDialog::copyLegacySettings(const QString &category,
                                      const QVariantMap &settings) {
  Config &config = Config::instance();

  if (category == "Thumbnail Settings") {
    if (settings.contains("ThumbnailSize")) {
      QString sizeStr = settings["ThumbnailSize"].toString();
      QStringList parts = sizeStr.split(',');
      if (parts.size() == 2) {
        bool ok1, ok2;
        int width = parts[0].trimmed().toInt(&ok1);
        int height = parts[1].trimmed().toInt(&ok2);
        if (ok1 && ok2) {
          m_thumbnailWidthSpin->setValue(width);
          m_thumbnailHeightSpin->setValue(height);
        }
      }
    }

    if (settings.contains("ThumbnailsOpacity")) {
      double opacity = settings["ThumbnailsOpacity"].toDouble();
      m_opacitySpin->setValue(static_cast<int>(opacity * 100));
    }
  } else if (category == "Window Behavior") {
    if (settings.contains("ShowThumbnailsAlwaysOnTop")) {
      m_alwaysOnTopCheck->setChecked(
          settings["ShowThumbnailsAlwaysOnTop"].toBool());
    }

    if (settings.contains("MinimizeInactiveClients")) {
      m_minimizeInactiveCheck->setChecked(
          settings["MinimizeInactiveClients"].toBool());
    }

    if (settings.contains("HideActiveClientThumbnail")) {
      m_hideActiveClientThumbnailCheck->setChecked(
          settings["HideActiveClientThumbnail"].toBool());
    }

    if (settings.contains("HideLoginClientThumbnail")) {
      bool hideLogin = settings["HideLoginClientThumbnail"].toBool();
      m_showNotLoggedInClientsCheck->setChecked(!hideLogin);
    }
  } else if (category == "Overlay Settings") {
    if (settings.contains("ShowThumbnailOverlays")) {
      m_showCharacterNameCheck->setChecked(
          settings["ShowThumbnailOverlays"].toBool());
    }

    if (settings.contains("OverlayLabelColor")) {
      QString colorStr = settings["OverlayLabelColor"].toString();
      QStringList parts = colorStr.split(',');
      if (parts.size() == 3) {
        bool ok1, ok2, ok3;
        int r = parts[0].trimmed().toInt(&ok1);
        int g = parts[1].trimmed().toInt(&ok2);
        int b = parts[2].trimmed().toInt(&ok3);
        if (ok1 && ok2 && ok3) {
          m_characterNameColor = QColor(r, g, b);
          updateColorButton(m_characterNameColorButton, m_characterNameColor);
        }
      }
    }

    if (settings.contains("OverlayLabelAnchor")) {
      int anchor = settings["OverlayLabelAnchor"].toInt();
      if (anchor >= 0 && anchor < m_characterNamePositionCombo->count()) {
        m_characterNamePositionCombo->setCurrentIndex(anchor);
      }
    }
  } else if (category == "Highlight Settings") {
    if (settings.contains("EnableActiveClientHighlight")) {
      m_highlightActiveCheck->setChecked(
          settings["EnableActiveClientHighlight"].toBool());
    }

    if (settings.contains("ActiveClientHighlightColor")) {
      QString colorName = settings["ActiveClientHighlightColor"].toString();
      QColor color(colorName);
      if (color.isValid()) {
        m_highlightColor = color;
        updateColorButton(m_highlightColorButton, m_highlightColor);
      }
    }

    if (settings.contains("ActiveClientHighlightThickness")) {
      m_highlightBorderWidthSpin->setValue(
          settings["ActiveClientHighlightThickness"].toInt());
    }
  } else if (category == "Position & Snapping") {
    if (settings.contains("EnableThumbnailSnap")) {
      m_enableSnappingCheck->setChecked(
          settings["EnableThumbnailSnap"].toBool());
    }

    if (settings.contains("LockThumbnailLocation")) {
      m_lockPositionsCheck->setChecked(
          settings["LockThumbnailLocation"].toBool());
    }

    if (settings.contains("ThumbnailSnapToGridSizeX") &&
        settings.contains("ThumbnailSnapToGridSizeY")) {
      int x = settings["ThumbnailSnapToGridSizeX"].toInt();
      int y = settings["ThumbnailSnapToGridSizeY"].toInt();
      m_snapDistanceSpin->setValue((x + y) / 2);
    }

    if (settings.contains("FlatLayout")) {
      QVariantMap layout = settings["FlatLayout"].toMap();
      for (auto it = layout.constBegin(); it != layout.constEnd(); ++it) {
        QString characterName = it.key();
        if (characterName.startsWith("EVE - ")) {
          characterName = characterName.mid(6);
        }

        QString posStr = it.value().toString();
        QStringList parts = posStr.split(',');
        if (parts.size() == 2) {
          bool ok1, ok2;
          int x = parts[0].trimmed().toInt(&ok1);
          int y = parts[1].trimmed().toInt(&ok2);
          if (ok1 && ok2) {
            config.setThumbnailPosition(characterName, QPoint(x, y));
          }
        }
      }
      m_rememberPositionsCheck->setChecked(true);
    }
  } else if (category == "Hotkeys & Cycle Groups") {
    m_cycleGroupsTable->setRowCount(0);

    auto legacyKeyToVirtualKey = [](const QString &keyName) -> int {
      QString key = keyName.trimmed().toUpper();

      if (key.startsWith("*")) {
        key = key.mid(1).trimmed();
      }

      if (key.startsWith("F")) {
        bool ok;
        int num = key.mid(1).toInt(&ok);
        if (ok && num >= 1 && num <= 12) {
          return VK_F1 + (num - 1);
        }
        if (ok && num >= 13 && num <= 24) {
          return VK_F13 + (num - 13);
        }
      }

      if (key.length() == 1 && key[0] >= '0' && key[0] <= '9') {
        return key[0].unicode();
      }

      if (key.length() == 1 && key[0] >= 'A' && key[0] <= 'Z') {
        return key[0].unicode();
      }

      if (key == "INSERT")
        return VK_INSERT;
      if (key == "DELETE")
        return VK_DELETE;
      if (key == "HOME")
        return VK_HOME;
      if (key == "END")
        return VK_END;
      if (key == "PAGEUP" || key == "PAGE UP")
        return VK_PRIOR;
      if (key == "PAGEDOWN" || key == "PAGE DOWN")
        return VK_NEXT;
      if (key == "PAUSE")
        return VK_PAUSE;
      if (key == "SCROLLLOCK" || key == "SCROLL LOCK")
        return VK_SCROLL;
      if (key == "SPACE")
        return VK_SPACE;
      if (key == "ENTER" || key == "RETURN")
        return VK_RETURN;
      if (key == "ESCAPE" || key == "ESC")
        return VK_ESCAPE;
      if (key == "TAB")
        return VK_TAB;
      if (key == "BACKSPACE")
        return VK_BACK;
      if (key == "LEFT")
        return VK_LEFT;
      if (key == "RIGHT")
        return VK_RIGHT;
      if (key == "UP")
        return VK_UP;
      if (key == "DOWN")
        return VK_DOWN;

      return 0;
    };

    for (int i = 1; i <= 5; ++i) {
      QString forwardKey = QString("CycleGroup%1ForwardHotkeys").arg(i);
      QString backwardKey = QString("CycleGroup%1BackwardHotkeys").arg(i);
      QString clientsKey = QString("CycleGroup%1ClientsOrder").arg(i);

      if (!settings.contains(clientsKey)) {
        continue;
      }

      QVariantMap clientsOrder = settings[clientsKey].toMap();

      if (clientsOrder.isEmpty() ||
          (clientsOrder.size() == 1 &&
           clientsOrder.firstKey().contains("cycle group"))) {
        continue;
      }

      QString forwardHotkey;
      QString backwardHotkey;

      if (settings.contains(forwardKey)) {
        QVariantList forwardList = settings[forwardKey].toList();
        if (!forwardList.isEmpty()) {
          forwardHotkey = forwardList[0].toString();
        }
      }

      if (settings.contains(backwardKey)) {
        QVariantList backwardList = settings[backwardKey].toList();
        if (!backwardList.isEmpty()) {
          backwardHotkey = backwardList[0].toString();
        }
      }

      QMap<int, QString> orderedCharacters;
      for (auto it = clientsOrder.constBegin(); it != clientsOrder.constEnd();
           ++it) {
        QString characterName = it.key();
        if (characterName.startsWith("EVE - ")) {
          characterName = characterName.mid(6);
        }
        int order = it.value().toInt();
        orderedCharacters[order] = characterName;
      }

      QStringList characterList;
      for (auto it = orderedCharacters.constBegin();
           it != orderedCharacters.constEnd(); ++it) {
        characterList.append(it.value());
      }

      int row = m_cycleGroupsTable->rowCount();
      m_cycleGroupsTable->insertRow(row);

      QString cellStyle = "QLineEdit {"
                          "   background-color: transparent;"
                          "   color: #ffffff;"
                          "   border: none;"
                          "   padding: 2px 4px;"
                          "   font-size: 12px;"
                          "}"
                          "QLineEdit:focus {"
                          "   background-color: #353535;"
                          "}";

      QLineEdit *nameEdit = new QLineEdit();
      nameEdit->setText(QString("Cycle Group %1").arg(i));
      nameEdit->setStyleSheet(cellStyle);
      m_cycleGroupsTable->setCellWidget(row, 0, nameEdit);

      QPushButton *charactersButton =
          new QPushButton(QString("(%1 characters)").arg(characterList.size()));
      charactersButton->setStyleSheet(
          StyleSheet::getTableCellButtonStyleSheet());
      charactersButton->setCursor(Qt::PointingHandCursor);
      charactersButton->setProperty("characterList", characterList);
      charactersButton->setToolTip(characterList.join(", "));
      connect(charactersButton, &QPushButton::clicked, this,
              &ConfigDialog::onEditCycleGroupCharacters);
      m_cycleGroupsTable->setCellWidget(row, 1, charactersButton);

      QWidget *forwardHotkeyWidget = new QWidget();
      QHBoxLayout *forwardLayout = new QHBoxLayout(forwardHotkeyWidget);
      forwardLayout->setContentsMargins(0, 0, 0, 0);
      forwardLayout->setSpacing(4);

      HotkeyCapture *forwardCapture = new HotkeyCapture();
      if (!forwardHotkey.isEmpty() && forwardHotkey != "") {
        int vkCode = legacyKeyToVirtualKey(forwardHotkey);
        if (vkCode != 0) {
          forwardCapture->setHotkey(vkCode, false, false, false);
        }
      }

      QPushButton *clearForwardButton = new QPushButton("×");
      clearForwardButton->setFixedSize(24, 24);
      clearForwardButton->setStyleSheet("QPushButton {"
                                        "    background-color: #3a3a3a;"
                                        "    color: #a0a0a0;"
                                        "    border: 1px solid #555555;"
                                        "    border-radius: 3px;"
                                        "    font-size: 16px;"
                                        "    font-weight: bold;"
                                        "    padding: 0px;"
                                        "}"
                                        "QPushButton:hover {"
                                        "    background-color: #4a4a4a;"
                                        "    color: #ffffff;"
                                        "    border: 1px solid #666666;"
                                        "}"
                                        "QPushButton:pressed {"
                                        "    background-color: #2a2a2a;"
                                        "}");
      clearForwardButton->setToolTip("Clear hotkey");
      connect(clearForwardButton, &QPushButton::clicked,
              [forwardCapture]() { forwardCapture->clearHotkey(); });

      forwardLayout->addWidget(forwardCapture, 1);
      forwardLayout->addWidget(clearForwardButton, 0);
      m_cycleGroupsTable->setCellWidget(row, 2, forwardHotkeyWidget);

      QWidget *backwardHotkeyWidget = new QWidget();
      QHBoxLayout *backwardLayout = new QHBoxLayout(backwardHotkeyWidget);
      backwardLayout->setContentsMargins(0, 0, 0, 0);
      backwardLayout->setSpacing(4);

      HotkeyCapture *backwardCapture = new HotkeyCapture();
      if (!backwardHotkey.isEmpty() && backwardHotkey != "") {
        int vkCode = legacyKeyToVirtualKey(backwardHotkey);
        if (vkCode != 0) {
          backwardCapture->setHotkey(vkCode, false, false, false);
        }
      }

      QPushButton *clearBackwardButton = new QPushButton("×");
      clearBackwardButton->setFixedSize(24, 24);
      clearBackwardButton->setStyleSheet("QPushButton {"
                                         "    background-color: #3a3a3a;"
                                         "    color: #a0a0a0;"
                                         "    border: 1px solid #555555;"
                                         "    border-radius: 3px;"
                                         "    font-size: 16px;"
                                         "    font-weight: bold;"
                                         "    padding: 0px;"
                                         "}"
                                         "QPushButton:hover {"
                                         "    background-color: #4a4a4a;"
                                         "    color: #ffffff;"
                                         "    border: 1px solid #666666;"
                                         "}"
                                         "QPushButton:pressed {"
                                         "    background-color: #2a2a2a;"
                                         "}");
      clearBackwardButton->setToolTip("Clear hotkey");
      connect(clearBackwardButton, &QPushButton::clicked,
              [backwardCapture]() { backwardCapture->clearHotkey(); });

      backwardLayout->addWidget(backwardCapture, 1);
      backwardLayout->addWidget(clearBackwardButton, 0);
      m_cycleGroupsTable->setCellWidget(row, 3, backwardHotkeyWidget);

      QWidget *checkboxContainer = new QWidget();
      checkboxContainer->setStyleSheet(
          "QWidget { background-color: transparent; }");
      QHBoxLayout *checkboxLayout = new QHBoxLayout(checkboxContainer);
      checkboxLayout->setContentsMargins(0, 0, 0, 0);
      checkboxLayout->setAlignment(Qt::AlignCenter);

      QCheckBox *includeNotLoggedInCheck = new QCheckBox();
      includeNotLoggedInCheck->setChecked(false);
      includeNotLoggedInCheck->setToolTip(
          "Include not-logged-in EVE clients in this cycle group");
      includeNotLoggedInCheck->setStyleSheet(
          StyleSheet::getDialogCheckBoxStyleSheet());

      checkboxLayout->addWidget(includeNotLoggedInCheck);
      m_cycleGroupsTable->setCellWidget(row, 4, checkboxContainer);

      QWidget *noLoopContainer = new QWidget();
      noLoopContainer->setStyleSheet(
          "QWidget { background-color: transparent; }");
      QHBoxLayout *noLoopLayout = new QHBoxLayout(noLoopContainer);
      noLoopLayout->setContentsMargins(0, 0, 0, 0);
      noLoopLayout->setAlignment(Qt::AlignCenter);

      QCheckBox *noLoopCheck = new QCheckBox();
      noLoopCheck->setChecked(false);
      noLoopCheck->setToolTip(
          "Do not loop back to the first character when reaching the end");
      noLoopCheck->setStyleSheet(StyleSheet::getDialogCheckBoxStyleSheet());

      noLoopLayout->addWidget(noLoopCheck);
      m_cycleGroupsTable->setCellWidget(row, 5, noLoopContainer);
    }

    bool hasWildcard = false;
    for (int i = 1; i <= 5; ++i) {
      QString forwardKey = QString("CycleGroup%1ForwardHotkeys").arg(i);
      QString backwardKey = QString("CycleGroup%1BackwardHotkeys").arg(i);

      if (settings.contains(forwardKey)) {
        QVariantList forwardList = settings[forwardKey].toList();
        if (!forwardList.isEmpty()) {
          QString hotkey = forwardList[0].toString();
          if (hotkey.trimmed().startsWith("*")) {
            hasWildcard = true;
            break;
          }
        }
      }

      if (settings.contains(backwardKey)) {
        QVariantList backwardList = settings[backwardKey].toList();
        if (!backwardList.isEmpty()) {
          QString hotkey = backwardList[0].toString();
          if (hotkey.trimmed().startsWith("*")) {
            hasWildcard = true;
            break;
          }
        }
      }
    }

    if (hasWildcard) {
      m_wildcardHotkeysCheck->setChecked(true);
    }

    if (settings.contains("ClientHotkey")) {
      QVariantMap clientHotkeys = settings["ClientHotkey"].toMap();

      for (auto it = clientHotkeys.constBegin(); it != clientHotkeys.constEnd();
           ++it) {
        QString characterName = it.key();
        if (characterName.startsWith("EVE - ")) {
          characterName = characterName.mid(6);
        }

        QString hotkeyStr = it.value().toString();
        if (!hotkeyStr.isEmpty()) {
          int vkCode = legacyKeyToVirtualKey(hotkeyStr);
          if (vkCode != 0) {
            int row = m_characterHotkeysTable->rowCount();
            m_characterHotkeysTable->insertRow(row);

            QString cellStyle = "QLineEdit {"
                                "   background-color: transparent;"
                                "   color: #ffffff;"
                                "   border: none;"
                                "   padding: 2px 4px;"
                                "   font-size: 12px;"
                                "}"
                                "QLineEdit:focus {"
                                "   background-color: #353535;"
                                "}";

            QLineEdit *nameEdit = new QLineEdit();
            nameEdit->setText(characterName);
            nameEdit->setStyleSheet(cellStyle);
            m_characterHotkeysTable->setCellWidget(row, 0, nameEdit);

            QWidget *hotkeyWidget = new QWidget();
            QHBoxLayout *hotkeyLayout = new QHBoxLayout(hotkeyWidget);
            hotkeyLayout->setContentsMargins(0, 0, 4, 0);
            hotkeyLayout->setSpacing(4);

            HotkeyCapture *hotkeyCapture = new HotkeyCapture();
            hotkeyCapture->setHotkey(vkCode, false, false, false);

            QPushButton *clearButton = new QPushButton("×");
            clearButton->setFixedSize(24, 24);
            clearButton->setStyleSheet("QPushButton {"
                                       "    background-color: #3a3a3a;"
                                       "    color: #a0a0a0;"
                                       "    border: 1px solid #555555;"
                                       "    border-radius: 3px;"
                                       "    font-size: 16px;"
                                       "    font-weight: bold;"
                                       "    padding: 0px;"
                                       "}"
                                       "QPushButton:hover {"
                                       "    background-color: #4a4a4a;"
                                       "    color: #ffffff;"
                                       "    border: 1px solid #666666;"
                                       "}"
                                       "QPushButton:pressed {"
                                       "    background-color: #2a2a2a;"
                                       "}");
            clearButton->setToolTip("Clear hotkey");
            connect(clearButton, &QPushButton::clicked,
                    [hotkeyCapture]() { hotkeyCapture->clearHotkey(); });

            hotkeyLayout->addWidget(hotkeyCapture, 1);
            hotkeyLayout->addWidget(clearButton, 0);
            m_characterHotkeysTable->setCellWidget(row, 1, hotkeyWidget);

            QWidget *deleteContainer = new QWidget();
            deleteContainer->setStyleSheet(
                "QWidget { background-color: transparent; }");
            QHBoxLayout *deleteLayout = new QHBoxLayout(deleteContainer);
            deleteLayout->setContentsMargins(0, 0, 0, 0);
            deleteLayout->setAlignment(Qt::AlignCenter);

            QPushButton *deleteButton = new QPushButton("×");
            deleteButton->setFixedSize(24, 24);
            deleteButton->setStyleSheet("QPushButton {"
                                        "    background-color: #3a3a3a;"
                                        "    color: #e74c3c;"
                                        "    border: 1px solid #555555;"
                                        "    border-radius: 3px;"
                                        "    font-size: 16px;"
                                        "    font-weight: bold;"
                                        "    padding: 0px;"
                                        "}"
                                        "QPushButton:hover {"
                                        "    background-color: #e74c3c;"
                                        "    color: #ffffff;"
                                        "    border: 1px solid #e74c3c;"
                                        "}"
                                        "QPushButton:pressed {"
                                        "    background-color: #c0392b;"
                                        "}");
            deleteButton->setToolTip("Delete this character hotkey");
            deleteButton->setCursor(Qt::PointingHandCursor);

            connect(
                deleteButton, &QPushButton::clicked, [this, deleteButton]() {
                  for (int i = 0; i < m_characterHotkeysTable->rowCount();
                       ++i) {
                    QWidget *widget = m_characterHotkeysTable->cellWidget(i, 2);
                    if (widget &&
                        widget->findChild<QPushButton *>() == deleteButton) {
                      m_characterHotkeysTable->removeRow(i);
                      break;
                    }
                  }
                });

            deleteLayout->addWidget(deleteButton);
            m_characterHotkeysTable->setCellWidget(row, 2, deleteContainer);
          }
        }
      }
    }
  }
}

void ConfigDialog::showFeedback(QWidget *nearWidget, const QString &message) {
  QLabel *feedbackLabel = new QLabel(message, nearWidget->parentWidget());
  feedbackLabel->setStyleSheet("QLabel {"
                               "    background-color: #28a745;"
                               "    color: white;"
                               "    padding: 6px 12px;"
                               "    border-radius: 4px;"
                               "    font-weight: bold;"
                               "    font-size: 10pt;"
                               "}");
  feedbackLabel->adjustSize();

  QPoint buttonPos = nearWidget->mapTo(nearWidget->window(), QPoint(0, 0));
  feedbackLabel->move(buttonPos.x() + nearWidget->width() + 10, buttonPos.y());
  feedbackLabel->show();
  feedbackLabel->raise();

  QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(feedbackLabel);
  feedbackLabel->setGraphicsEffect(effect);

  QPropertyAnimation *animation = new QPropertyAnimation(effect, "opacity");
  animation->setDuration(2000);
  animation->setStartValue(1.0);
  animation->setEndValue(0.0);
  animation->setEasingCurve(QEasingCurve::InOutQuad);

  connect(animation, &QPropertyAnimation::finished, feedbackLabel,
          &QLabel::deleteLater);

  animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ConfigDialog::createProfileToolbar() {
  QWidget *toolbarWidget = new QWidget();
  toolbarWidget->setStyleSheet(StyleSheet::getProfileToolbarStyleSheet());

  QHBoxLayout *toolbarLayout = new QHBoxLayout(toolbarWidget);
  toolbarLayout->setContentsMargins(15, 8, 15, 8);
  toolbarLayout->setSpacing(10);

  QLabel *profileLabel = new QLabel("Profile:");
  profileLabel->setStyleSheet(StyleSheet::getProfileLabelStyleSheet());
  toolbarLayout->addWidget(profileLabel);

  m_profileCombo = new QComboBox();
  m_profileCombo->setMinimumWidth(220);
  m_profileCombo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_profileCombo->setStyleSheet(StyleSheet::getProfileComboBoxStyleSheet());

  connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ConfigDialog::onProfileChanged);

  toolbarLayout->addWidget(m_profileCombo);

  QFrame *separator = new QFrame();
  separator->setFrameShape(QFrame::VLine);
  separator->setFrameShadow(QFrame::Sunken);
  separator->setStyleSheet(StyleSheet::getProfileSeparatorStyleSheet());
  toolbarLayout->addWidget(separator);

  m_newProfileButton = new QPushButton("New");
  m_newProfileButton->setStyleSheet(StyleSheet::getProfileButtonStyleSheet());
  m_newProfileButton->setToolTip("Create a new profile");
  m_newProfileButton->setCursor(Qt::PointingHandCursor);
  m_newProfileButton->setAutoDefault(false);
  connect(m_newProfileButton, &QPushButton::clicked, this,
          &ConfigDialog::onNewProfile);
  toolbarLayout->addWidget(m_newProfileButton);

  m_cloneProfileButton = new QPushButton("Clone");
  m_cloneProfileButton->setStyleSheet(StyleSheet::getProfileButtonStyleSheet());
  m_cloneProfileButton->setToolTip("Clone the current profile");
  m_cloneProfileButton->setCursor(Qt::PointingHandCursor);
  m_cloneProfileButton->setAutoDefault(false);
  connect(m_cloneProfileButton, &QPushButton::clicked, this,
          &ConfigDialog::onCloneProfile);
  toolbarLayout->addWidget(m_cloneProfileButton);

  m_renameProfileButton = new QPushButton("Rename");
  m_renameProfileButton->setStyleSheet(
      StyleSheet::getProfileButtonStyleSheet());
  m_renameProfileButton->setToolTip("Rename the current profile");
  m_renameProfileButton->setCursor(Qt::PointingHandCursor);
  m_renameProfileButton->setAutoDefault(false);
  connect(m_renameProfileButton, &QPushButton::clicked, this,
          &ConfigDialog::onRenameProfile);
  toolbarLayout->addWidget(m_renameProfileButton);

  m_deleteProfileButton = new QPushButton("Delete");
  m_deleteProfileButton->setStyleSheet(
      StyleSheet::getProfileDeleteButtonStyleSheet());
  m_deleteProfileButton->setToolTip("Delete the current profile");
  m_deleteProfileButton->setCursor(Qt::PointingHandCursor);
  m_deleteProfileButton->setAutoDefault(false);
  connect(m_deleteProfileButton, &QPushButton::clicked, this,
          &ConfigDialog::onDeleteProfile);
  toolbarLayout->addWidget(m_deleteProfileButton);

  QFrame *separator2 = new QFrame();
  separator2->setFrameShape(QFrame::VLine);
  separator2->setFrameShadow(QFrame::Sunken);
  separator2->setStyleSheet(StyleSheet::getProfileSeparatorStyleSheet());
  toolbarLayout->addWidget(separator2);

  QLabel *hotkeyLabel = new QLabel("Hotkey:");
  hotkeyLabel->setStyleSheet(StyleSheet::getProfileLabelStyleSheet());
  toolbarLayout->addWidget(hotkeyLabel);

  m_profileHotkeyCapture = new HotkeyCapture();
  m_profileHotkeyCapture->setFixedWidth(150);
  m_profileHotkeyCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());
  connect(
      m_profileHotkeyCapture, &HotkeyCapture::hotkeyChanged, this, [this]() {
        QString currentProfile = Config::instance().getCurrentProfileName();
        int key = m_profileHotkeyCapture->getKeyCode();

        if (key == 0) {
          Config::instance().clearProfileHotkey(currentProfile);
          return;
        }

        int modifiers = 0;
        if (m_profileHotkeyCapture->getCtrl())
          modifiers |= Qt::ControlModifier;
        if (m_profileHotkeyCapture->getAlt())
          modifiers |= Qt::AltModifier;
        if (m_profileHotkeyCapture->getShift())
          modifiers |= Qt::ShiftModifier;

        HotkeyBinding binding;
        binding.keyCode = key;
        binding.ctrl = (modifiers & Qt::ControlModifier) != 0;
        binding.alt = (modifiers & Qt::AltModifier) != 0;
        binding.shift = (modifiers & Qt::ShiftModifier) != 0;
        binding.enabled = true;

        QString conflict = HotkeyManager::instance()->findHotkeyConflict(
            binding, currentProfile);
        if (!conflict.isEmpty()) {
          QMessageBox::warning(
              this, "Hotkey Conflict",
              QString(
                  "This hotkey is already assigned to:\n\n%1\n\nPlease choose "
                  "a different hotkey or remove the existing assignment first.")
                  .arg(conflict));
          m_profileHotkeyCapture->clearHotkey();
          return;
        }

        Config::instance().setProfileHotkey(currentProfile, key, modifiers);
      });
  toolbarLayout->addWidget(m_profileHotkeyCapture);

  m_clearProfileHotkeyButton = new QPushButton("Clear");
  m_clearProfileHotkeyButton->setStyleSheet(
      StyleSheet::getProfileButtonStyleSheet());
  m_clearProfileHotkeyButton->setToolTip("Clear the hotkey for this profile");
  m_clearProfileHotkeyButton->setCursor(Qt::PointingHandCursor);
  m_clearProfileHotkeyButton->setFixedWidth(60);
  connect(m_clearProfileHotkeyButton, &QPushButton::clicked, this, [this]() {
    QString currentProfile = Config::instance().getCurrentProfileName();
    Config::instance().clearProfileHotkey(currentProfile);
    m_profileHotkeyCapture->clearHotkey();
  });
  toolbarLayout->addWidget(m_clearProfileHotkeyButton);

  toolbarLayout->addStretch();

  QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout *>(layout());
  if (mainLayout) {
    mainLayout->insertWidget(0, toolbarWidget);
  } else {
    qWarning() << "Failed to get main layout for profile toolbar";
  }
}

void ConfigDialog::updateProfileDropdown() {
  if (!m_profileCombo) {
    qWarning() << "Profile combo box not initialized";
    return;
  }

  m_profileCombo->blockSignals(true);

  m_profileCombo->clear();

  QStringList profiles = Config::instance().listProfiles();
  QString currentProfile = Config::instance().getCurrentProfileName();

  qDebug() << "Updating profile dropdown. Profiles:" << profiles
           << "Current:" << currentProfile;

  if (profiles.isEmpty()) {
    qWarning() << "No profiles found. This shouldn't happen.";
    m_profileCombo->addItem("default");
    m_profileCombo->setCurrentIndex(0);
    m_profileCombo->blockSignals(false);
    return;
  }

  for (const QString &profile : profiles) {
    m_profileCombo->addItem(profile);
  }

  int currentIndex = m_profileCombo->findText(currentProfile);
  if (currentIndex >= 0) {
    m_profileCombo->setCurrentIndex(currentIndex);
  } else {
    qWarning() << "Current profile" << currentProfile << "not found in list";
    if (m_profileCombo->count() > 0) {
      m_profileCombo->setCurrentIndex(0);
    }
  }

  bool isDefault = (currentProfile == "default");
  bool hasMultipleProfiles = (profiles.count() > 1);

  m_renameProfileButton->setEnabled(!isDefault);
  m_deleteProfileButton->setEnabled(!isDefault && hasMultipleProfiles);

  m_profileCombo->blockSignals(false);
}

void ConfigDialog::onProfileChanged(int index) {
  if (index < 0) {
    return;
  }

  QString newProfileName = m_profileCombo->itemText(index);
  QString currentProfileName = Config::instance().getCurrentProfileName();

  if (newProfileName == currentProfileName) {
    return;
  }

  if (!confirmProfileSwitch()) {
    m_profileCombo->blockSignals(true);
    int currentIndex = m_profileCombo->findText(currentProfileName);
    if (currentIndex >= 0) {
      m_profileCombo->setCurrentIndex(currentIndex);
    }
    m_profileCombo->blockSignals(false);
    return;
  }

  switchProfile(newProfileName);
}

bool ConfigDialog::confirmProfileSwitch() {
  Config &config = Config::instance();
  QSettings globalSettings(
      config.configFilePath().replace(QRegularExpression("/profiles/.*\\.ini$"),
                                      "/settings.global.ini"),
      QSettings::IniFormat);

  bool skipConfirmation =
      globalSettings
          .value(Config::KEY_UI_SKIP_PROFILE_SWITCH_CONFIRMATION,
                 Config::DEFAULT_UI_SKIP_PROFILE_SWITCH_CONFIRMATION)
          .toBool();

  if (skipConfirmation || m_skipProfileSwitchConfirmation) {
    return true;
  }

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Switch Profile?");
  msgBox.setText(QString("Switch from \"%1\" to \"%2\"?")
                     .arg(Config::instance().getCurrentProfileName())
                     .arg(m_profileCombo->currentText()));
  msgBox.setInformativeText("Current settings will be saved automatically.");
  msgBox.setIcon(QMessageBox::Question);
  msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
  msgBox.setDefaultButton(QMessageBox::Ok);

  QCheckBox *dontAskCheckbox = new QCheckBox("Don't ask again");
  msgBox.setCheckBox(dontAskCheckbox);

  int result = msgBox.exec();

  if (dontAskCheckbox->isChecked()) {
    globalSettings.setValue(Config::KEY_UI_SKIP_PROFILE_SWITCH_CONFIRMATION,
                            true);
    globalSettings.sync();
  }

  return (result == QMessageBox::Ok);
}

void ConfigDialog::switchProfile(const QString &profileName) {
  QString currentProfileName = Config::instance().getCurrentProfileName();

  saveSettings();
  Config::instance().save();
  HotkeyManager::instance()->saveToConfig();

  if (Config::instance().loadProfile(profileName)) {
    HotkeyManager::instance()->loadFromConfig();

    loadSettings();

    updateProfileDropdown();

    if (m_profileHotkeyCapture) {
      QString hotkey = Config::instance().getProfileHotkey(profileName);
      if (hotkey.isEmpty()) {
        m_profileHotkeyCapture->clearHotkey();
      } else {
        QMap<QString, QPair<int, int>> allHotkeys =
            Config::instance().getAllProfileHotkeys();
        if (allHotkeys.contains(profileName)) {
          QPair<int, int> keyData = allHotkeys[profileName];
          int modifiers = keyData.second;
          m_profileHotkeyCapture->setHotkey(
              keyData.first, (modifiers & Qt::ControlModifier) != 0,
              (modifiers & Qt::AltModifier) != 0,
              (modifiers & Qt::ShiftModifier) != 0);
        }
      }
    }

    emit settingsApplied();

    qDebug() << "Switched to profile:" << profileName;
  } else {
    QMessageBox::warning(
        this, "Profile Switch Failed",
        QString("Failed to switch to profile: %1").arg(profileName));
  }
}

void ConfigDialog::onExternalProfileSwitch(const QString &profileName) {
  qDebug() << "ConfigDialog: External profile switch to" << profileName;

  if (Config::instance().getCurrentProfileName() == profileName) {
    loadSettings();

    updateProfileDropdown();

    if (m_profileHotkeyCapture) {
      QString hotkey = Config::instance().getProfileHotkey(profileName);
      if (hotkey.isEmpty()) {
        m_profileHotkeyCapture->clearHotkey();
      } else {
        QMap<QString, QPair<int, int>> allHotkeys =
            Config::instance().getAllProfileHotkeys();
        if (allHotkeys.contains(profileName)) {
          QPair<int, int> keyData = allHotkeys[profileName];
          int modifiers = keyData.second;
          m_profileHotkeyCapture->setHotkey(
              keyData.first, (modifiers & Qt::ControlModifier) != 0,
              (modifiers & Qt::AltModifier) != 0,
              (modifiers & Qt::ShiftModifier) != 0);
        }
      }
    }
  }
}

void ConfigDialog::onNewProfile() {
  QTimer::singleShot(0, this, [this]() {
    bool ok;
    QString profileName = QInputDialog::getText(
        this, "New Profile", "Enter profile name:", QLineEdit::Normal, "", &ok);

    if (!ok || profileName.isEmpty()) {
      return;
    }

    if (profileName.contains('/') || profileName.contains('\\') ||
        profileName.contains('.')) {
      QMessageBox::warning(this, "Invalid Name",
                           "Profile name cannot contain slashes or dots.");
      return;
    }

    if (Config::instance().profileExists(profileName)) {
      QMessageBox::warning(
          this, "Profile Exists",
          QString("Profile \"%1\" already exists.").arg(profileName));
      return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Profile Source",
        QString("Clone from current profile \"%1\"?\n\n"
                "Choose Yes to copy current settings,\n"
                "or No to use default settings.")
            .arg(Config::instance().getCurrentProfileName()),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (reply == QMessageBox::Cancel) {
      return;
    }

    bool success;
    if (reply == QMessageBox::Yes) {
      success = Config::instance().cloneProfile(
          Config::instance().getCurrentProfileName(), profileName);
    } else {
      success = Config::instance().createProfile(profileName, true);
    }

    if (success) {
      updateProfileDropdown();

      QMessageBox::StandardButton switchNow = QMessageBox::question(
          this, "Switch Profile?",
          QString("Profile \"%1\" created successfully.\n\n"
                  "Switch to it now?")
              .arg(profileName),
          QMessageBox::Yes | QMessageBox::No);

      if (switchNow == QMessageBox::Yes) {
        int index = m_profileCombo->findText(profileName);
        if (index >= 0) {
          m_skipProfileSwitchConfirmation = true;
          m_profileCombo->setCurrentIndex(index);
          m_skipProfileSwitchConfirmation = false;
        }
      }
    } else {
      QMessageBox::critical(
          this, "Creation Failed",
          QString("Failed to create profile: %1").arg(profileName));
    }
  });
}

void ConfigDialog::onCloneProfile() {
  QTimer::singleShot(0, this, [this]() {
    QString currentProfile = Config::instance().getCurrentProfileName();

    QString defaultName = currentProfile + " (Copy)";
    int counter = 2;
    while (Config::instance().profileExists(defaultName)) {
      defaultName = QString("%1 (Copy %2)").arg(currentProfile).arg(counter++);
    }

    bool ok;
    QString cloneName = QInputDialog::getText(
        this, "Clone Profile", QString("Clone \"%1\" as:").arg(currentProfile),
        QLineEdit::Normal, defaultName, &ok);

    if (!ok || cloneName.isEmpty()) {
      return;
    }

    if (cloneName.contains('/') || cloneName.contains('\\') ||
        cloneName.contains('.')) {
      QMessageBox::warning(this, "Invalid Name",
                           "Profile name cannot contain slashes or dots.");
      return;
    }

    if (Config::instance().profileExists(cloneName)) {
      QMessageBox::warning(
          this, "Profile Exists",
          QString("Profile \"%1\" already exists.").arg(cloneName));
      return;
    }

    if (Config::instance().cloneProfile(currentProfile, cloneName)) {
      updateProfileDropdown();

      QMessageBox::information(
          this, "Profile Cloned",
          QString("Profile \"%1\" cloned successfully as \"%2\".")
              .arg(currentProfile)
              .arg(cloneName));

      QMessageBox::StandardButton switchNow =
          QMessageBox::question(this, "Switch Profile?",
                                QString("Switch to \"%1\" now?").arg(cloneName),
                                QMessageBox::Yes | QMessageBox::No);

      if (switchNow == QMessageBox::Yes) {
        int index = m_profileCombo->findText(cloneName);
        if (index >= 0) {
          m_skipProfileSwitchConfirmation = true;
          m_profileCombo->setCurrentIndex(index);
          m_skipProfileSwitchConfirmation = false;
        }
      }
    } else {
      QMessageBox::critical(
          this, "Clone Failed",
          QString("Failed to clone profile: %1").arg(currentProfile));
    }
  });
}

void ConfigDialog::onRenameProfile() {
  QTimer::singleShot(0, this, [this]() {
    QString currentProfile = Config::instance().getCurrentProfileName();

    if (currentProfile == "default") {
      QMessageBox::information(this, "Cannot Rename",
                               "The default profile cannot be renamed.");
      return;
    }

    bool ok;
    QString newName =
        QInputDialog::getText(this, "Rename Profile",
                              QString("Rename \"%1\" to:").arg(currentProfile),
                              QLineEdit::Normal, currentProfile, &ok);

    if (!ok || newName.isEmpty() || newName == currentProfile) {
      return;
    }

    if (newName.contains('/') || newName.contains('\\') ||
        newName.contains('.')) {
      QMessageBox::warning(this, "Invalid Name",
                           "Profile name cannot contain slashes or dots.");
      return;
    }

    if (Config::instance().profileExists(newName)) {
      QMessageBox::warning(
          this, "Profile Exists",
          QString("Profile \"%1\" already exists.").arg(newName));
      return;
    }

    if (Config::instance().renameProfile(currentProfile, newName)) {
      updateProfileDropdown();

      QMessageBox::information(this, "Profile Renamed",
                               QString("Profile renamed from \"%1\" to \"%2\".")
                                   .arg(currentProfile)
                                   .arg(newName));
    } else {
      QMessageBox::critical(
          this, "Rename Failed",
          QString("Failed to rename profile: %1").arg(currentProfile));
    }
  });
}

void ConfigDialog::onDeleteProfile() {
  QTimer::singleShot(0, this, [this]() {
    QString currentProfile = Config::instance().getCurrentProfileName();

    if (currentProfile == "default") {
      QMessageBox::information(this, "Cannot Delete",
                               "The default profile cannot be deleted.");
      return;
    }

    QStringList profiles = Config::instance().listProfiles();
    if (profiles.count() <= 1) {
      QMessageBox::information(this, "Cannot Delete",
                               "Cannot delete the last remaining profile.");
      return;
    }

    QMessageBox::StandardButton reply = QMessageBox::warning(
        this, "Delete Profile?",
        QString("Are you sure you want to delete profile \"%1\"?\n\n"
                "This action cannot be undone.\n\n"
                "The app will switch to the \"default\" profile.")
            .arg(currentProfile),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes) {
      return;
    }

    if (Config::instance().deleteProfile(currentProfile)) {
      updateProfileDropdown();

      loadSettings();

      emit settingsApplied();

      QMessageBox::information(
          this, "Profile Deleted",
          QString("Profile \"%1\" has been deleted.\n\n"
                  "Switched to profile: %2")
              .arg(currentProfile)
              .arg(Config::instance().getCurrentProfileName()));
    } else {
      QMessageBox::critical(
          this, "Delete Failed",
          QString("Failed to delete profile: %1").arg(currentProfile));
    }
  });
}

void ConfigDialog::onCopyLegacyCategory(const QString &category) {}

void ConfigDialog::onCheckForUpdates() {
  if (!m_networkManager) {
    m_networkManager = new QNetworkAccessManager(this);
  }

  if (!QSslSocket::supportsSsl()) {
    m_updateStatusLabel->setText(
        QString("❌ SSL not available. OpenSSL libraries required."));
    QMessageBox::warning(
        this, "SSL Not Available",
        QString(
            "OpenSSL libraries are not available.\n\n"
            "Qt requires OpenSSL %1.%2.x libraries to make HTTPS requests.\n\n"
            "Please install OpenSSL and ensure the DLLs are in your PATH or "
            "application directory.")
            .arg(QSslSocket::sslLibraryVersionNumber() >> 28)
            .arg((QSslSocket::sslLibraryVersionNumber() >> 20) & 0xff));
    return;
  }

  m_checkUpdateButton->setEnabled(false);
  m_updateStatusLabel->setText("🔄 Checking for updates...");
  m_downloadUpdateButton->setVisible(false);

  QUrl url(
      "https://api.github.com/repos/mrmjstc/eve-apm-preview/releases/latest");
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, "EVE-APM-Preview");

  QNetworkReply *reply = m_networkManager->get(request);

  connect(reply,
          QOverload<const QList<QSslError> &>::of(&QNetworkReply::sslErrors),
          this, [this, reply](const QList<QSslError> &errors) {
            QString errorMsg = "SSL Errors:\n";
            for (const QSslError &error : errors) {
              errorMsg += error.errorString() + "\n";
            }
            qWarning() << errorMsg;
          });

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    m_checkUpdateButton->setEnabled(true);

    if (reply->error() != QNetworkReply::NoError) {
      QString errorMsg = reply->errorString();
      if (reply->error() == QNetworkReply::SslHandshakeFailedError) {
        errorMsg =
            "TLS initialization failed. OpenSSL libraries may be missing.";
      }
      m_updateStatusLabel->setText(QString("❌ Error: %1").arg(errorMsg));
      return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isObject()) {
      m_updateStatusLabel->setText("❌ Invalid response from GitHub API");
      return;
    }

    QJsonObject obj = doc.object();
    QString latestVersion = obj["tag_name"].toString();
    QString releaseUrl = obj["html_url"].toString();

    if (latestVersion.isEmpty()) {
      m_updateStatusLabel->setText("❌ Could not determine latest version");
      return;
    }

    QString cleanLatestVersion = latestVersion;
    if (cleanLatestVersion.startsWith('v')) {
      cleanLatestVersion = cleanLatestVersion.mid(1);
    }

    QString currentVersion = APP_VERSION;

    if (compareVersions(currentVersion, cleanLatestVersion) < 0) {
      m_updateStatusLabel->setText(
          QString("🎉 New version available: %1 (you have %2)")
              .arg(latestVersion)
              .arg(currentVersion));
      m_downloadUpdateButton->setVisible(true);
      m_latestReleaseUrl =
          "https://github.com/mrmjstc/eve-apm-preview/releases";
    } else {
      m_updateStatusLabel->setText(
          QString("✅ You have the latest version (%1)").arg(currentVersion));
      m_downloadUpdateButton->setVisible(false);
    }
  });
}

void ConfigDialog::onDownloadUpdate() {
  if (!m_latestReleaseUrl.isEmpty()) {
    QDesktopServices::openUrl(QUrl(m_latestReleaseUrl));
  }
}

int ConfigDialog::compareVersions(const QString &version1,
                                  const QString &version2) {
  QStringList v1Parts = version1.split('.');
  QStringList v2Parts = version2.split('.');

  int maxLength = qMax(v1Parts.length(), v2Parts.length());

  for (int i = 0; i < maxLength; ++i) {
    int v1 = (i < v1Parts.length()) ? v1Parts[i].toInt() : 0;
    int v2 = (i < v2Parts.length()) ? v2Parts[i].toInt() : 0;

    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return 1;
  }

  return 0;
}
