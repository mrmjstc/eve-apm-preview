#include "configdialog.h"
#include "config.h"
#include "hotkeycapture.h"
#include "hotkeymanager.h"
#include "stylesheet.h"
#include "systemcolorsdialog.h"
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
#include <QScrollBar>
#include <QSslError>
#include <QSslSocket>
#include <QTabWidget>
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
    QVector<HotkeyBinding> hotkeys =
        Config::instance().getProfileHotkeys(currentProfile);

    if (hotkeys.isEmpty()) {
      m_profileHotkeyCapture->clearHotkey();
    } else {
      QVector<HotkeyCombination> combinations;
      for (const HotkeyBinding &binding : hotkeys) {
        HotkeyCombination combo;
        combo.keyCode = binding.keyCode;
        combo.ctrl = binding.ctrl;
        combo.alt = binding.alt;
        combo.shift = binding.shift;
        combinations.append(combo);
      }
      m_profileHotkeyCapture->setHotkeys(combinations);
    }
  }

  setWindowTitle("Settings");
  resize(1050, 800);

  QTimer::singleShot(0, this, &ConfigDialog::validateAllHotkeys);
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
  rightLayout->setContentsMargins(10, 10, 10, 10);

  m_stackedWidget = new QStackedWidget();
  m_stackedWidget->setStyleSheet(StyleSheet::getStackedWidgetStyleSheet());

  createAppearancePage();
  createHotkeysPage();
  createBehaviorPage();
  createNonEVEThumbnailsPage();
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

  m_bugReportButton = new QPushButton("Report Bug");
  m_bugReportButton->setStyleSheet(StyleSheet::getButtonStyleSheet());
  m_bugReportButton->setAutoDefault(false);
  connect(m_bugReportButton, &QPushButton::clicked, this,
          &ConfigDialog::onBugReportClicked);

  buttonLayout->addWidget(m_testOverlaysButton);
  buttonLayout->addWidget(m_bugReportButton);
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
  m_categoryList->addItem("Non-EVE Thumbnails");
  m_categoryList->addItem("Logs");
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
  m_thumbnailWidthSpin->setRange(50, 2000);
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
  m_thumbnailHeightSpin->setRange(50, 2000);
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

  m_thumbnailSizesScrollArea = new QScrollArea();
  m_thumbnailSizesScrollArea->setWidgetResizable(true);
  m_thumbnailSizesScrollArea->setFrameShape(QFrame::NoFrame);
  m_thumbnailSizesScrollArea->setHorizontalScrollBarPolicy(
      Qt::ScrollBarAlwaysOff);
  m_thumbnailSizesScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_thumbnailSizesScrollArea->setSizePolicy(QSizePolicy::Preferred,
                                            QSizePolicy::Preferred);
  m_thumbnailSizesScrollArea->setMinimumHeight(10);
  m_thumbnailSizesScrollArea->setMaximumHeight(240);
  m_thumbnailSizesScrollArea->setFixedHeight(10);
  m_thumbnailSizesScrollArea->setStyleSheet(
      "QScrollArea { background-color: transparent; border: none; }");

  m_thumbnailSizesContainer = new QWidget();
  m_thumbnailSizesContainer->setStyleSheet(
      "QWidget { background-color: transparent; }");
  m_thumbnailSizesLayout = new QVBoxLayout(m_thumbnailSizesContainer);
  m_thumbnailSizesLayout->setContentsMargins(0, 0, 0, 0);
  m_thumbnailSizesLayout->setSpacing(8);
  m_thumbnailSizesLayout->addStretch();

  m_thumbnailSizesScrollArea->setWidget(m_thumbnailSizesContainer);
  thumbnailSizesSectionLayout->addWidget(m_thumbnailSizesScrollArea);

  thumbnailSizesSectionLayout->addSpacing(-8);

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

  QWidget *customNamesSection = new QWidget();
  customNamesSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *customNamesSectionLayout = new QVBoxLayout(customNamesSection);
  customNamesSectionLayout->setContentsMargins(16, 12, 16, 12);
  customNamesSectionLayout->setSpacing(10);

  tagWidget(customNamesSection,
            {"custom", "name", "label", "character", "thumbnail", "display"});

  QLabel *customNamesHeader = new QLabel("Custom Thumbnail Names");
  customNamesHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  customNamesSectionLayout->addWidget(customNamesHeader);

  QLabel *customNamesInfoLabel =
      new QLabel("Set custom display names for character thumbnails. "
                 "These names will appear instead of the character name.");
  customNamesInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  customNamesSectionLayout->addWidget(customNamesInfoLabel);

  m_customNamesScrollArea = new QScrollArea();
  m_customNamesScrollArea->setWidgetResizable(true);
  m_customNamesScrollArea->setFrameShape(QFrame::NoFrame);
  m_customNamesScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_customNamesScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_customNamesScrollArea->setSizePolicy(QSizePolicy::Preferred,
                                         QSizePolicy::Expanding);
  m_customNamesScrollArea->setMinimumHeight(10);
  m_customNamesScrollArea->setMaximumHeight(240);
  m_customNamesScrollArea->setFixedHeight(10);
  m_customNamesScrollArea->setStyleSheet(
      "QScrollArea { background-color: transparent; border: none; }");

  m_customNamesContainer = new QWidget();
  m_customNamesContainer->setStyleSheet(
      "QWidget { background-color: transparent; }");
  m_customNamesLayout = new QVBoxLayout(m_customNamesContainer);
  m_customNamesLayout->setContentsMargins(0, 0, 0, 0);
  m_customNamesLayout->setSpacing(8);
  m_customNamesLayout->addStretch();

  m_customNamesScrollArea->setWidget(m_customNamesContainer);
  customNamesSectionLayout->addWidget(m_customNamesScrollArea);

  customNamesSectionLayout->addSpacing(-8);

  QHBoxLayout *customNamesButtonLayout = new QHBoxLayout();
  m_addCustomNameButton = new QPushButton("Add Character");
  m_populateCustomNamesButton = new QPushButton("Populate from Open Clients");

  QString customNamesButtonStyle = StyleSheet::getSecondaryButtonStyleSheet();

  m_addCustomNameButton->setStyleSheet(customNamesButtonStyle);
  m_populateCustomNamesButton->setStyleSheet(customNamesButtonStyle);

  connect(m_addCustomNameButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddCustomName);
  connect(m_populateCustomNamesButton, &QPushButton::clicked, this,
          &ConfigDialog::onPopulateCustomNames);

  customNamesButtonLayout->addWidget(m_addCustomNameButton);
  customNamesButtonLayout->addWidget(m_populateCustomNamesButton);
  customNamesButtonLayout->addStretch();

  customNamesSectionLayout->addLayout(customNamesButtonLayout);

  layout->addWidget(customNamesSection);

  // Window Highlighting Section
  QWidget *highlightSection = new QWidget();
  highlightSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *highlightSectionLayout = new QVBoxLayout(highlightSection);
  highlightSectionLayout->setContentsMargins(16, 12, 16, 12);
  highlightSectionLayout->setSpacing(10);

  tagWidget(highlightSection, {"highlight", "active", "inactive", "window",
                               "border", "color", "cyan", "frame", "outline"});

  QLabel *highlightHeader = new QLabel("Window Highlighting");
  highlightHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  highlightSectionLayout->addWidget(highlightHeader);

  QLabel *highlightInfoLabel = new QLabel(
      "Display colored borders on EVE client thumbnails. Configure separate "
      "border styles for active and inactive windows, or use per-character "
      "colors below.");
  highlightInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  highlightInfoLabel->setWordWrap(true);
  highlightSectionLayout->addWidget(highlightInfoLabel);

  // Active border settings
  m_highlightActiveCheck = new QCheckBox("Show border on active window");
  m_highlightActiveCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  highlightSectionLayout->addWidget(m_highlightActiveCheck);

  QGridLayout *activeGrid = new QGridLayout();
  activeGrid->setSpacing(10);
  activeGrid->setColumnMinimumWidth(0, 120);
  activeGrid->setColumnStretch(2, 1);
  activeGrid->setContentsMargins(24, 0, 0, 0);

  m_highlightColorLabel = new QLabel("Active color:");
  m_highlightColorLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_highlightColorButton = new QPushButton();
  m_highlightColorButton->setFixedSize(150, 32);
  m_highlightColorButton->setCursor(Qt::PointingHandCursor);
  connect(m_highlightColorButton, &QPushButton::clicked, this,
          &ConfigDialog::onColorButtonClicked);

  m_highlightBorderWidthLabel = new QLabel("Active width:");
  m_highlightBorderWidthLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_highlightBorderWidthSpin = new QSpinBox();
  m_highlightBorderWidthSpin->setRange(1, 10);
  m_highlightBorderWidthSpin->setSuffix(" px");
  m_highlightBorderWidthSpin->setFixedWidth(150);
  m_highlightBorderWidthSpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());

  m_activeBorderStyleLabel = new QLabel("Active style:");
  m_activeBorderStyleLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_activeBorderStyleCombo = new QComboBox();
  m_activeBorderStyleCombo->addItem("Solid",
                                    static_cast<int>(BorderStyle::Solid));
  m_activeBorderStyleCombo->addItem("Dashed",
                                    static_cast<int>(BorderStyle::Dashed));
  m_activeBorderStyleCombo->addItem("Dotted",
                                    static_cast<int>(BorderStyle::Dotted));
  m_activeBorderStyleCombo->addItem("Dash-Dot",
                                    static_cast<int>(BorderStyle::DashDot));
  m_activeBorderStyleCombo->addItem("Faded Edges",
                                    static_cast<int>(BorderStyle::FadedEdges));
  m_activeBorderStyleCombo->addItem(
      "Corner Accents", static_cast<int>(BorderStyle::CornerAccents));
  m_activeBorderStyleCombo->addItem(
      "Rounded Corners", static_cast<int>(BorderStyle::RoundedCorners));
  m_activeBorderStyleCombo->addItem("Neon",
                                    static_cast<int>(BorderStyle::Neon));
  m_activeBorderStyleCombo->addItem("Shimmer",
                                    static_cast<int>(BorderStyle::Shimmer));
  m_activeBorderStyleCombo->addItem("Thick/Thin",
                                    static_cast<int>(BorderStyle::ThickThin));
  m_activeBorderStyleCombo->addItem("Electric Arc",
                                    static_cast<int>(BorderStyle::ElectricArc));
  m_activeBorderStyleCombo->addItem("Rainbow",
                                    static_cast<int>(BorderStyle::Rainbow));
  m_activeBorderStyleCombo->addItem(
      "Breathing Glow", static_cast<int>(BorderStyle::BreathingGlow));
  m_activeBorderStyleCombo->addItem("Double Glow",
                                    static_cast<int>(BorderStyle::DoubleGlow));
  m_activeBorderStyleCombo->addItem("Zigzag",
                                    static_cast<int>(BorderStyle::Zigzag));
  m_activeBorderStyleCombo->setFixedWidth(150);
  m_activeBorderStyleCombo->setStyleSheet(StyleSheet::getComboBoxStyleSheet());

  activeGrid->addWidget(m_highlightColorLabel, 0, 0, Qt::AlignLeft);
  activeGrid->addWidget(m_highlightColorButton, 0, 1);
  activeGrid->addWidget(m_highlightBorderWidthLabel, 1, 0, Qt::AlignLeft);
  activeGrid->addWidget(m_highlightBorderWidthSpin, 1, 1);
  activeGrid->addWidget(m_activeBorderStyleLabel, 2, 0, Qt::AlignLeft);
  activeGrid->addWidget(m_activeBorderStyleCombo, 2, 1);

  highlightSectionLayout->addLayout(activeGrid);

  // Inactive border settings
  m_showInactiveBordersCheck = new QCheckBox("Show border on inactive windows");
  m_showInactiveBordersCheck->setStyleSheet(
      StyleSheet::getCheckBoxStyleSheet());
  highlightSectionLayout->addWidget(m_showInactiveBordersCheck);

  QGridLayout *inactiveGrid = new QGridLayout();
  inactiveGrid->setSpacing(10);
  inactiveGrid->setColumnMinimumWidth(0, 120);
  inactiveGrid->setColumnStretch(2, 1);
  inactiveGrid->setContentsMargins(24, 0, 0, 0);

  m_inactiveBorderColorLabel = new QLabel("Inactive color:");
  m_inactiveBorderColorLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_inactiveBorderColorButton = new QPushButton();
  m_inactiveBorderColorButton->setFixedSize(150, 32);
  m_inactiveBorderColorButton->setCursor(Qt::PointingHandCursor);
  connect(m_inactiveBorderColorButton, &QPushButton::clicked, this,
          &ConfigDialog::onColorButtonClicked);

  m_inactiveBorderWidthLabel = new QLabel("Inactive width:");
  m_inactiveBorderWidthLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_inactiveBorderWidthSpin = new QSpinBox();
  m_inactiveBorderWidthSpin->setFixedWidth(150);
  m_inactiveBorderWidthSpin->setMinimum(1);
  m_inactiveBorderWidthSpin->setMaximum(20);
  m_inactiveBorderWidthSpin->setSuffix(" px");
  m_inactiveBorderWidthSpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());

  m_inactiveBorderStyleLabel = new QLabel("Inactive style:");
  m_inactiveBorderStyleLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_inactiveBorderStyleCombo = new QComboBox();
  m_inactiveBorderStyleCombo->addItem("Solid",
                                      static_cast<int>(BorderStyle::Solid));
  m_inactiveBorderStyleCombo->addItem("Dashed",
                                      static_cast<int>(BorderStyle::Dashed));
  m_inactiveBorderStyleCombo->addItem("Dotted",
                                      static_cast<int>(BorderStyle::Dotted));
  m_inactiveBorderStyleCombo->addItem("Dash-Dot",
                                      static_cast<int>(BorderStyle::DashDot));
  m_inactiveBorderStyleCombo->addItem(
      "Faded Edges", static_cast<int>(BorderStyle::FadedEdges));
  m_inactiveBorderStyleCombo->addItem(
      "Corner Accents", static_cast<int>(BorderStyle::CornerAccents));
  m_inactiveBorderStyleCombo->addItem(
      "Rounded Corners", static_cast<int>(BorderStyle::RoundedCorners));
  m_inactiveBorderStyleCombo->addItem("Neon",
                                      static_cast<int>(BorderStyle::Neon));
  m_inactiveBorderStyleCombo->addItem("Shimmer",
                                      static_cast<int>(BorderStyle::Shimmer));
  m_inactiveBorderStyleCombo->addItem("Thick/Thin",
                                      static_cast<int>(BorderStyle::ThickThin));
  m_inactiveBorderStyleCombo->addItem(
      "Electric Arc", static_cast<int>(BorderStyle::ElectricArc));
  m_inactiveBorderStyleCombo->addItem("Rainbow",
                                      static_cast<int>(BorderStyle::Rainbow));
  m_inactiveBorderStyleCombo->addItem(
      "Breathing Glow", static_cast<int>(BorderStyle::BreathingGlow));
  m_inactiveBorderStyleCombo->addItem(
      "Double Glow", static_cast<int>(BorderStyle::DoubleGlow));
  m_inactiveBorderStyleCombo->addItem("Zigzag",
                                      static_cast<int>(BorderStyle::Zigzag));
  m_inactiveBorderStyleCombo->setFixedWidth(150);
  m_inactiveBorderStyleCombo->setStyleSheet(
      StyleSheet::getComboBoxStyleSheet());

  inactiveGrid->addWidget(m_inactiveBorderColorLabel, 0, 0, Qt::AlignLeft);
  inactiveGrid->addWidget(m_inactiveBorderColorButton, 0, 1);
  inactiveGrid->addWidget(m_inactiveBorderWidthLabel, 1, 0, Qt::AlignLeft);
  inactiveGrid->addWidget(m_inactiveBorderWidthSpin, 1, 1);
  inactiveGrid->addWidget(m_inactiveBorderStyleLabel, 2, 0, Qt::AlignLeft);
  inactiveGrid->addWidget(m_inactiveBorderStyleCombo, 2, 1);

  highlightSectionLayout->addLayout(inactiveGrid);

  layout->addWidget(highlightSection);

  connect(m_highlightActiveCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_highlightColorLabel->setEnabled(checked);
            m_highlightColorButton->setEnabled(checked);
            m_highlightBorderWidthLabel->setEnabled(checked);
            m_highlightBorderWidthSpin->setEnabled(checked);
            m_activeBorderStyleLabel->setEnabled(checked);
            m_activeBorderStyleCombo->setEnabled(checked);
          });

  connect(m_showInactiveBordersCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_inactiveBorderColorLabel->setEnabled(checked);
            m_inactiveBorderColorButton->setEnabled(checked);
            m_inactiveBorderWidthLabel->setEnabled(checked);
            m_inactiveBorderWidthSpin->setEnabled(checked);
            m_inactiveBorderStyleLabel->setEnabled(checked);
            m_inactiveBorderStyleCombo->setEnabled(checked);
          });

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
      new QLabel("Override the default highlight colors for specific "
                 "characters. Set both active and inactive border colors for "
                 "each character. "
                 "Per-character colors take priority over global settings.");
  charColorsInfoTop->setWordWrap(true);
  charColorsInfoTop->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  charColorsSectionLayout->addWidget(charColorsInfoTop);

  // Add header row for column labels
  QWidget *headerRow = new QWidget();
  QHBoxLayout *headerLayout = new QHBoxLayout(headerRow);
  headerLayout->setContentsMargins(12, 0, 12, 0);
  headerLayout->setSpacing(8);

  QLabel *nameHeader = new QLabel("Character Name");
  nameHeader->setStyleSheet("QLabel { color: #aaaaaa; font-weight: bold; }");
  headerLayout->addWidget(nameHeader, 1);

  QLabel *activeHeader = new QLabel("  Active");
  activeHeader->setStyleSheet("QLabel { color: #aaaaaa; font-weight: bold; }");
  activeHeader->setFixedWidth(80);
  activeHeader->setAlignment(Qt::AlignLeft);
  headerLayout->addWidget(activeHeader);

  QLabel *inactiveHeader = new QLabel("Inactive");
  inactiveHeader->setStyleSheet(
      "QLabel { color: #aaaaaa; font-weight: bold; }");
  inactiveHeader->setFixedWidth(80);
  inactiveHeader->setAlignment(Qt::AlignLeft);
  headerLayout->addWidget(inactiveHeader);

  // Spacer for delete button column
  QLabel *deleteHeader = new QLabel("");
  deleteHeader->setFixedWidth(32);
  headerLayout->addWidget(deleteHeader);

  charColorsSectionLayout->addWidget(headerRow);

  m_characterColorsScrollArea = new QScrollArea();
  m_characterColorsScrollArea->setWidgetResizable(true);
  m_characterColorsScrollArea->setHorizontalScrollBarPolicy(
      Qt::ScrollBarAlwaysOff);
  m_characterColorsScrollArea->setVerticalScrollBarPolicy(
      Qt::ScrollBarAsNeeded);
  m_characterColorsScrollArea->setStyleSheet(
      "QScrollArea { border: none; background-color: transparent; }");

  m_characterColorsContainer = new QWidget();
  m_characterColorsLayout = new QVBoxLayout(m_characterColorsContainer);
  m_characterColorsLayout->setContentsMargins(4, 4, 4, 4);
  m_characterColorsLayout->setSpacing(6);
  m_characterColorsLayout->addStretch();

  m_characterColorsScrollArea->setWidget(m_characterColorsContainer);
  charColorsSectionLayout->addWidget(m_characterColorsScrollArea);

  updateCharacterColorsScrollHeight();

  charColorsSectionLayout->addSpacing(-8);

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

  m_hideThumbnailsWhenEVENotFocusedCheck =
      new QCheckBox("Hide all thumbnails when EVE is not focused");
  m_hideThumbnailsWhenEVENotFocusedCheck->setStyleSheet(
      StyleSheet::getCheckBoxStyleSheet());
  thumbnailVisibilitySectionLayout->addWidget(
      m_hideThumbnailsWhenEVENotFocusedCheck);

  QLabel *hiddenCharactersLabel = new QLabel("Hidden Characters");
  hiddenCharactersLabel->setStyleSheet(
      StyleSheet::getSectionSubHeaderStyleSheet() + " margin-top: 10px;");
  thumbnailVisibilitySectionLayout->addWidget(hiddenCharactersLabel);

  m_hiddenCharactersScrollArea = new QScrollArea();
  m_hiddenCharactersScrollArea->setWidgetResizable(true);
  m_hiddenCharactersScrollArea->setHorizontalScrollBarPolicy(
      Qt::ScrollBarAlwaysOff);
  m_hiddenCharactersScrollArea->setVerticalScrollBarPolicy(
      Qt::ScrollBarAsNeeded);
  m_hiddenCharactersScrollArea->setStyleSheet(
      "QScrollArea { background-color: transparent; border: none; }");

  m_hiddenCharactersContainer = new QWidget();
  m_hiddenCharactersLayout = new QVBoxLayout(m_hiddenCharactersContainer);
  m_hiddenCharactersLayout->setContentsMargins(4, 4, 4, 4);
  m_hiddenCharactersLayout->setSpacing(6);
  m_hiddenCharactersLayout->addStretch();

  m_hiddenCharactersScrollArea->setWidget(m_hiddenCharactersContainer);
  thumbnailVisibilitySectionLayout->addWidget(m_hiddenCharactersScrollArea);

  updateHiddenCharactersScrollHeight();

  thumbnailVisibilitySectionLayout->addSpacing(-8);

  QHBoxLayout *hiddenCharactersButtonLayout = new QHBoxLayout();
  m_addHiddenCharacterButton = new QPushButton("Add Character");
  m_populateHiddenCharactersButton =
      new QPushButton("Populate from Open Clients");

  QString hiddenCharactersButtonStyle =
      StyleSheet::getSecondaryButtonStyleSheet();

  m_addHiddenCharacterButton->setStyleSheet(hiddenCharactersButtonStyle);
  m_populateHiddenCharactersButton->setStyleSheet(hiddenCharactersButtonStyle);

  connect(m_addHiddenCharacterButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddHiddenCharacter);
  connect(m_populateHiddenCharactersButton, &QPushButton::clicked, this,
          &ConfigDialog::onPopulateHiddenCharacters);

  hiddenCharactersButtonLayout->addWidget(m_addHiddenCharacterButton);
  hiddenCharactersButtonLayout->addWidget(m_populateHiddenCharactersButton);
  hiddenCharactersButtonLayout->addStretch();

  thumbnailVisibilitySectionLayout->addLayout(hiddenCharactersButtonLayout);

  layout->addWidget(thumbnailVisibilitySection);

  QWidget *overlaysSection = new QWidget();
  overlaysSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *overlaysSectionLayout = new QVBoxLayout(overlaysSection);
  overlaysSectionLayout->setContentsMargins(16, 12, 16, 12);
  overlaysSectionLayout->setSpacing(10);

  tagWidget(overlaysSection,
            {"overlay", "character", "name", "system", "font", "background",
             "text", "position", "color", "opacity"});

  QLabel *overlaysHeader = new QLabel("Thumbnail Overlays");
  overlaysHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  overlaysSectionLayout->addWidget(overlaysHeader);

  QLabel *overlaysInfoTop =
      new QLabel("Configure text overlays displayed on thumbnail windows. "
                 "System names require Logs > Chat log and Game log "
                 "monitoring enabled.");
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
  m_characterNamePositionCombo->addItems(
      {"Top Left", "Top Center", "Top Right", "Center Left", "Center",
       "Center Right", "Bottom Left", "Bottom Center", "Bottom Right"});
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

  // X Offset slider
  m_characterNameOffsetXLabel = new QLabel("X Offset:");
  m_characterNameOffsetXLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_characterNameOffsetXSlider = new QSlider(Qt::Horizontal);
  m_characterNameOffsetXSlider->setRange(-20, 20);
  m_characterNameOffsetXSlider->setValue(0);
  m_characterNameOffsetXSlider->setTickPosition(QSlider::TicksBelow);
  m_characterNameOffsetXSlider->setTickInterval(5);
  m_characterNameOffsetXSlider->setFixedWidth(150);
  m_characterNameOffsetXValue = new QLabel("0");
  m_characterNameOffsetXValue->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_characterNameOffsetXValue->setFixedWidth(30);
  m_characterNameOffsetXValue->setAlignment(Qt::AlignCenter);
  QHBoxLayout *charOffsetXLayout = new QHBoxLayout();
  charOffsetXLayout->addWidget(m_characterNameOffsetXSlider);
  charOffsetXLayout->addWidget(m_characterNameOffsetXValue);
  charOffsetXLayout->setContentsMargins(0, 0, 0, 0);
  charGrid->addWidget(m_characterNameOffsetXLabel, 3, 0, Qt::AlignLeft);
  charGrid->addLayout(charOffsetXLayout, 3, 1);

  // Y Offset slider
  m_characterNameOffsetYLabel = new QLabel("Y Offset:");
  m_characterNameOffsetYLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_characterNameOffsetYSlider = new QSlider(Qt::Horizontal);
  m_characterNameOffsetYSlider->setRange(-20, 20);
  m_characterNameOffsetYSlider->setValue(0);
  m_characterNameOffsetYSlider->setTickPosition(QSlider::TicksBelow);
  m_characterNameOffsetYSlider->setTickInterval(5);
  m_characterNameOffsetYSlider->setFixedWidth(150);
  m_characterNameOffsetYValue = new QLabel("0");
  m_characterNameOffsetYValue->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_characterNameOffsetYValue->setFixedWidth(30);
  m_characterNameOffsetYValue->setAlignment(Qt::AlignCenter);
  QHBoxLayout *charOffsetYLayout = new QHBoxLayout();
  charOffsetYLayout->addWidget(m_characterNameOffsetYSlider);
  charOffsetYLayout->addWidget(m_characterNameOffsetYValue);
  charOffsetYLayout->setContentsMargins(0, 0, 0, 0);
  charGrid->addWidget(m_characterNameOffsetYLabel, 4, 0, Qt::AlignLeft);
  charGrid->addLayout(charOffsetYLayout, 4, 1);

  connect(m_characterNameOffsetXSlider, &QSlider::valueChanged, this,
          [this](int value) {
            m_characterNameOffsetXValue->setText(QString::number(value));
          });
  connect(m_characterNameOffsetYSlider, &QSlider::valueChanged, this,
          [this](int value) {
            m_characterNameOffsetYValue->setText(QString::number(value));
          });

  overlaysSectionLayout->addLayout(charGrid);

  connect(m_showCharacterNameCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_characterNameColorLabel->setEnabled(checked);
            m_characterNameColorButton->setEnabled(checked);
            m_characterNamePositionLabel->setEnabled(checked);
            m_characterNamePositionCombo->setEnabled(checked);
            m_characterNameFontLabel->setEnabled(checked);
            m_characterNameFontButton->setEnabled(checked);
            m_characterNameOffsetXLabel->setEnabled(checked);
            m_characterNameOffsetXSlider->setEnabled(checked);
            m_characterNameOffsetXValue->setEnabled(checked);
            m_characterNameOffsetYLabel->setEnabled(checked);
            m_characterNameOffsetYSlider->setEnabled(checked);
            m_characterNameOffsetYValue->setEnabled(checked);
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

  m_uniqueSystemColorsCheck = new QCheckBox("Use unique colors");
  m_uniqueSystemColorsCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());

  m_systemNamePositionLabel = new QLabel("Position:");
  m_systemNamePositionLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_systemNamePositionCombo = new QComboBox();
  m_systemNamePositionCombo->addItems(
      {"Top Left", "Top Center", "Top Right", "Center Left", "Center",
       "Center Right", "Bottom Left", "Bottom Center", "Bottom Right"});
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

  m_customSystemColorsLabel = new QLabel("Custom colors:");
  m_customSystemColorsLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_customSystemColorsButton = new QPushButton("Assign Systems");
  m_customSystemColorsButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  m_customSystemColorsButton->setFixedWidth(120);
  connect(m_customSystemColorsButton, &QPushButton::clicked, this,
          &ConfigDialog::onCustomSystemColors);

  sysGrid->addWidget(m_systemNameColorLabel, 0, 0, Qt::AlignLeft);
  sysGrid->addWidget(m_systemNameColorButton, 0, 1);
  sysGrid->addWidget(m_uniqueSystemColorsCheck, 0, 2, Qt::AlignLeft);
  sysGrid->addWidget(m_customSystemColorsLabel, 1, 0, Qt::AlignLeft);
  sysGrid->addWidget(m_customSystemColorsButton, 1, 1);
  sysGrid->addWidget(m_systemNamePositionLabel, 2, 0, Qt::AlignLeft);
  sysGrid->addWidget(m_systemNamePositionCombo, 2, 1);
  sysGrid->addWidget(m_systemNameFontLabel, 3, 0, Qt::AlignLeft);
  sysGrid->addWidget(m_systemNameFontButton, 3, 1);

  // X Offset slider
  m_systemNameOffsetXLabel = new QLabel("X Offset:");
  m_systemNameOffsetXLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_systemNameOffsetXSlider = new QSlider(Qt::Horizontal);
  m_systemNameOffsetXSlider->setRange(-20, 20);
  m_systemNameOffsetXSlider->setValue(0);
  m_systemNameOffsetXSlider->setTickPosition(QSlider::TicksBelow);
  m_systemNameOffsetXSlider->setTickInterval(5);
  m_systemNameOffsetXSlider->setFixedWidth(150);
  m_systemNameOffsetXValue = new QLabel("0");
  m_systemNameOffsetXValue->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_systemNameOffsetXValue->setFixedWidth(30);
  m_systemNameOffsetXValue->setAlignment(Qt::AlignCenter);
  QHBoxLayout *sysOffsetXLayout = new QHBoxLayout();
  sysOffsetXLayout->addWidget(m_systemNameOffsetXSlider);
  sysOffsetXLayout->addWidget(m_systemNameOffsetXValue);
  sysOffsetXLayout->setContentsMargins(0, 0, 0, 0);
  sysGrid->addWidget(m_systemNameOffsetXLabel, 4, 0, Qt::AlignLeft);
  sysGrid->addLayout(sysOffsetXLayout, 4, 1);

  // Y Offset slider
  m_systemNameOffsetYLabel = new QLabel("Y Offset:");
  m_systemNameOffsetYLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_systemNameOffsetYSlider = new QSlider(Qt::Horizontal);
  m_systemNameOffsetYSlider->setRange(-20, 20);
  m_systemNameOffsetYSlider->setValue(0);
  m_systemNameOffsetYSlider->setTickPosition(QSlider::TicksBelow);
  m_systemNameOffsetYSlider->setTickInterval(5);
  m_systemNameOffsetYSlider->setFixedWidth(150);
  m_systemNameOffsetYValue = new QLabel("0");
  m_systemNameOffsetYValue->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_systemNameOffsetYValue->setFixedWidth(30);
  m_systemNameOffsetYValue->setAlignment(Qt::AlignCenter);
  QHBoxLayout *sysOffsetYLayout = new QHBoxLayout();
  sysOffsetYLayout->addWidget(m_systemNameOffsetYSlider);
  sysOffsetYLayout->addWidget(m_systemNameOffsetYValue);
  sysOffsetYLayout->setContentsMargins(0, 0, 0, 0);
  sysGrid->addWidget(m_systemNameOffsetYLabel, 5, 0, Qt::AlignLeft);
  sysGrid->addLayout(sysOffsetYLayout, 5, 1);

  connect(m_systemNameOffsetXSlider, &QSlider::valueChanged, this,
          [this](int value) {
            m_systemNameOffsetXValue->setText(QString::number(value));
          });
  connect(m_systemNameOffsetYSlider, &QSlider::valueChanged, this,
          [this](int value) {
            m_systemNameOffsetYValue->setText(QString::number(value));
          });

  overlaysSectionLayout->addLayout(sysGrid);

  connect(m_showSystemNameCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_uniqueSystemColorsCheck->setEnabled(checked);
            m_systemNameColorLabel->setEnabled(
                checked && !m_uniqueSystemColorsCheck->isChecked());
            m_systemNameColorButton->setEnabled(
                checked && !m_uniqueSystemColorsCheck->isChecked());
            m_systemNamePositionLabel->setEnabled(checked);
            m_systemNamePositionCombo->setEnabled(checked);
            m_systemNameFontLabel->setEnabled(checked);
            m_systemNameFontButton->setEnabled(checked);
            m_customSystemColorsLabel->setEnabled(checked);
            m_customSystemColorsButton->setEnabled(checked);
            m_systemNameOffsetXLabel->setEnabled(checked);
            m_systemNameOffsetXSlider->setEnabled(checked);
            m_systemNameOffsetXValue->setEnabled(checked);
            m_systemNameOffsetYLabel->setEnabled(checked);
            m_systemNameOffsetYSlider->setEnabled(checked);
            m_systemNameOffsetYValue->setEnabled(checked);
          });

  connect(m_uniqueSystemColorsCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_systemNameColorLabel->setEnabled(
                !checked && m_showSystemNameCheck->isChecked());
            m_systemNameColorButton->setEnabled(
                !checked && m_showSystemNameCheck->isChecked());
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
  m_backgroundOpacitySpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());

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
  layout->setSpacing(10);
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

  connect(m_suspendHotkeyCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);

  QPushButton *clearSuspendButton = new QPushButton("Clear");
  clearSuspendButton->setFixedWidth(60);
  clearSuspendButton->setStyleSheet(StyleSheet::getSecondaryButtonStyleSheet());
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

  connect(m_closeAllClientsCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);

  QPushButton *clearCloseAllButton = new QPushButton("Clear");
  clearCloseAllButton->setFixedWidth(60);
  clearCloseAllButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  connect(clearCloseAllButton, &QPushButton::clicked,
          [this]() { m_closeAllClientsCapture->clearHotkey(); });

  closeAllGrid->addWidget(closeAllLabel, 0, 0, Qt::AlignLeft);
  closeAllGrid->addWidget(m_closeAllClientsCapture, 0, 1);
  closeAllGrid->addWidget(clearCloseAllButton, 0, 2, Qt::AlignLeft);

  closeAllSectionLayout->addLayout(closeAllGrid);

  closeAllSectionLayout->addSpacing(10);

  m_neverCloseLabel = new QLabel("Never Close Characters");
  m_neverCloseLabel->setStyleSheet(StyleSheet::getSectionSubHeaderStyleSheet() +
                                   " margin-top: 10px;");
  closeAllSectionLayout->addWidget(m_neverCloseLabel);

  m_neverCloseInfoLabel = new QLabel(
      "Specify character names that should be excluded from the Close All "
      "Clients hotkey. These clients will not be closed when the hotkey is "
      "activated.");
  m_neverCloseInfoLabel->setWordWrap(true);
  m_neverCloseInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  closeAllSectionLayout->addWidget(m_neverCloseInfoLabel);

  m_neverCloseScrollArea = new QScrollArea();
  m_neverCloseScrollArea->setWidgetResizable(true);
  m_neverCloseScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_neverCloseScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_neverCloseScrollArea->setStyleSheet(
      "QScrollArea { background-color: transparent; border: none; }");

  m_neverCloseContainer = new QWidget();
  m_neverCloseLayout = new QVBoxLayout(m_neverCloseContainer);
  m_neverCloseLayout->setContentsMargins(4, 4, 4, 4);
  m_neverCloseLayout->setSpacing(6);
  m_neverCloseLayout->addStretch();

  m_neverCloseScrollArea->setWidget(m_neverCloseContainer);
  closeAllSectionLayout->addWidget(m_neverCloseScrollArea);

  updateNeverCloseScrollHeight();

  closeAllSectionLayout->addSpacing(-8);

  QHBoxLayout *neverCloseButtonLayout = new QHBoxLayout();
  m_addNeverCloseButton = new QPushButton("Add Character");
  m_populateNeverCloseButton = new QPushButton("Populate from Open Clients");

  QString neverCloseButtonStyle = StyleSheet::getSecondaryButtonStyleSheet();

  m_addNeverCloseButton->setStyleSheet(neverCloseButtonStyle);
  m_populateNeverCloseButton->setStyleSheet(neverCloseButtonStyle);

  connect(m_addNeverCloseButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddNeverCloseCharacter);
  connect(m_populateNeverCloseButton, &QPushButton::clicked, this,
          &ConfigDialog::onPopulateNeverClose);

  neverCloseButtonLayout->addWidget(m_addNeverCloseButton);
  neverCloseButtonLayout->addWidget(m_populateNeverCloseButton);
  neverCloseButtonLayout->addStretch();

  closeAllSectionLayout->addLayout(neverCloseButtonLayout);

  layout->addWidget(closeAllSection);

  QWidget *minimizeAllSection = new QWidget();
  minimizeAllSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *minimizeAllSectionLayout = new QVBoxLayout(minimizeAllSection);
  minimizeAllSectionLayout->setContentsMargins(16, 12, 16, 12);
  minimizeAllSectionLayout->setSpacing(10);

  tagWidget(minimizeAllSection,
            {"minimize", "all", "clients", "hotkey", "global"});

  QLabel *minimizeAllTitle = new QLabel("Minimize All Clients");
  minimizeAllTitle->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  minimizeAllSectionLayout->addWidget(minimizeAllTitle);

  QLabel *minimizeAllDesc =
      new QLabel("Set a hotkey to minimize all EVE Online clients.");
  minimizeAllDesc->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  minimizeAllDesc->setWordWrap(true);
  minimizeAllSectionLayout->addWidget(minimizeAllDesc);

  QGridLayout *minimizeAllGrid = new QGridLayout();
  minimizeAllGrid->setHorizontalSpacing(10);
  minimizeAllGrid->setVerticalSpacing(8);
  minimizeAllGrid->setColumnMinimumWidth(0, 120);
  minimizeAllGrid->setColumnStretch(2, 1);

  QLabel *minimizeAllLabel = new QLabel("Minimize all:");
  minimizeAllLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_minimizeAllClientsCapture = new HotkeyCapture();
  m_minimizeAllClientsCapture->setFixedWidth(150);
  m_minimizeAllClientsCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  connect(m_minimizeAllClientsCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);

  QPushButton *clearMinimizeAllButton = new QPushButton("Clear");
  clearMinimizeAllButton->setFixedWidth(60);
  clearMinimizeAllButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  connect(clearMinimizeAllButton, &QPushButton::clicked,
          [this]() { m_minimizeAllClientsCapture->clearHotkey(); });

  minimizeAllGrid->addWidget(minimizeAllLabel, 0, 0, Qt::AlignLeft);
  minimizeAllGrid->addWidget(m_minimizeAllClientsCapture, 0, 1);
  minimizeAllGrid->addWidget(clearMinimizeAllButton, 0, 2, Qt::AlignLeft);

  minimizeAllSectionLayout->addLayout(minimizeAllGrid);

  layout->addWidget(minimizeAllSection);

  QWidget *toggleThumbnailsSection = new QWidget();
  toggleThumbnailsSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *toggleThumbnailsSectionLayout =
      new QVBoxLayout(toggleThumbnailsSection);
  toggleThumbnailsSectionLayout->setContentsMargins(16, 12, 16, 12);
  toggleThumbnailsSectionLayout->setSpacing(10);

  tagWidget(toggleThumbnailsSection,
            {"toggle", "show", "hide", "thumbnails", "visibility", "hotkey"});

  QLabel *toggleThumbnailsHeader = new QLabel("Toggle Thumbnails Visibility");
  toggleThumbnailsHeader->setStyleSheet(
      StyleSheet::getSectionHeaderStyleSheet());
  toggleThumbnailsSectionLayout->addWidget(toggleThumbnailsHeader);

  QLabel *toggleThumbnailsInfoLabel = new QLabel(
      "Hotkey to manually show/hide all thumbnails (overrides all other "
      "visibility settings).");
  toggleThumbnailsInfoLabel->setStyleSheet(
      StyleSheet::getInfoLabelStyleSheet());
  toggleThumbnailsSectionLayout->addWidget(toggleThumbnailsInfoLabel);

  QGridLayout *toggleThumbnailsGrid = new QGridLayout();
  toggleThumbnailsGrid->setHorizontalSpacing(10);
  toggleThumbnailsGrid->setVerticalSpacing(8);
  toggleThumbnailsGrid->setColumnMinimumWidth(0, 120);
  toggleThumbnailsGrid->setColumnStretch(2, 1);

  QLabel *toggleThumbnailsLabel = new QLabel("Toggle visibility:");
  toggleThumbnailsLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_toggleThumbnailsVisibilityCapture = new HotkeyCapture();
  m_toggleThumbnailsVisibilityCapture->setFixedWidth(150);
  m_toggleThumbnailsVisibilityCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  connect(m_toggleThumbnailsVisibilityCapture, &HotkeyCapture::hotkeyChanged,
          this, &ConfigDialog::onHotkeyChanged);

  QPushButton *clearToggleThumbnailsButton = new QPushButton("Clear");
  clearToggleThumbnailsButton->setFixedWidth(60);
  clearToggleThumbnailsButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  connect(clearToggleThumbnailsButton, &QPushButton::clicked,
          [this]() { m_toggleThumbnailsVisibilityCapture->clearHotkey(); });

  toggleThumbnailsGrid->addWidget(toggleThumbnailsLabel, 0, 0, Qt::AlignLeft);
  toggleThumbnailsGrid->addWidget(m_toggleThumbnailsVisibilityCapture, 0, 1);
  toggleThumbnailsGrid->addWidget(clearToggleThumbnailsButton, 0, 2,
                                  Qt::AlignLeft);

  toggleThumbnailsSectionLayout->addLayout(toggleThumbnailsGrid);

  layout->addWidget(toggleThumbnailsSection);

  // Cycle Profile Hotkeys Section
  QWidget *cycleProfileSection = new QWidget();
  cycleProfileSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *cycleProfileSectionLayout = new QVBoxLayout(cycleProfileSection);
  cycleProfileSectionLayout->setContentsMargins(16, 12, 16, 12);
  cycleProfileSectionLayout->setSpacing(10);

  tagWidget(cycleProfileSection,
            {"cycle", "profile", "switch", "hotkey", "forward", "backward"});

  QLabel *cycleProfileHeader = new QLabel("Cycle Profiles");
  cycleProfileHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  cycleProfileSectionLayout->addWidget(cycleProfileHeader);

  QLabel *cycleProfileInfoLabel = new QLabel(
      "Hotkeys to cycle forward or backward through all available profiles. "
      "These hotkeys are global and work regardless of which profile is "
      "active.");
  cycleProfileInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  cycleProfileSectionLayout->addWidget(cycleProfileInfoLabel);

  QGridLayout *cycleProfileGrid = new QGridLayout();
  cycleProfileGrid->setHorizontalSpacing(10);
  cycleProfileGrid->setVerticalSpacing(8);
  cycleProfileGrid->setColumnMinimumWidth(0, 120);
  cycleProfileGrid->setColumnStretch(2, 1);

  QLabel *cycleForwardLabel = new QLabel("Cycle forward:");
  cycleForwardLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_cycleProfileForwardCapture = new HotkeyCapture();
  m_cycleProfileForwardCapture->setFixedWidth(150);
  m_cycleProfileForwardCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  connect(m_cycleProfileForwardCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);

  QPushButton *clearCycleForwardButton = new QPushButton("Clear");
  clearCycleForwardButton->setFixedWidth(60);
  clearCycleForwardButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  connect(clearCycleForwardButton, &QPushButton::clicked,
          [this]() { m_cycleProfileForwardCapture->clearHotkey(); });

  cycleProfileGrid->addWidget(cycleForwardLabel, 0, 0, Qt::AlignLeft);
  cycleProfileGrid->addWidget(m_cycleProfileForwardCapture, 0, 1);
  cycleProfileGrid->addWidget(clearCycleForwardButton, 0, 2, Qt::AlignLeft);

  QLabel *cycleBackwardLabel = new QLabel("Cycle backward:");
  cycleBackwardLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_cycleProfileBackwardCapture = new HotkeyCapture();
  m_cycleProfileBackwardCapture->setFixedWidth(150);
  m_cycleProfileBackwardCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  connect(m_cycleProfileBackwardCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);

  QPushButton *clearCycleBackwardButton = new QPushButton("Clear");
  clearCycleBackwardButton->setFixedWidth(60);
  clearCycleBackwardButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  connect(clearCycleBackwardButton, &QPushButton::clicked,
          [this]() { m_cycleProfileBackwardCapture->clearHotkey(); });

  cycleProfileGrid->addWidget(cycleBackwardLabel, 1, 0, Qt::AlignLeft);
  cycleProfileGrid->addWidget(m_cycleProfileBackwardCapture, 1, 1);
  cycleProfileGrid->addWidget(clearCycleBackwardButton, 1, 2, Qt::AlignLeft);

  cycleProfileSectionLayout->addLayout(cycleProfileGrid);

  layout->addWidget(cycleProfileSection);

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
      "Assign hotkeys to instantly switch to specific character "
      "windows. Multiple characters can share the same hotkey, but will "
      "cycle in window opening order.");
  charInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  charHotkeysSectionLayout->addWidget(charInfoLabel);

  m_characterHotkeysScrollArea = new QScrollArea();
  m_characterHotkeysScrollArea->setWidgetResizable(true);
  m_characterHotkeysScrollArea->setFrameShape(QFrame::NoFrame);
  m_characterHotkeysScrollArea->setHorizontalScrollBarPolicy(
      Qt::ScrollBarAlwaysOff);
  m_characterHotkeysScrollArea->setVerticalScrollBarPolicy(
      Qt::ScrollBarAsNeeded);
  m_characterHotkeysScrollArea->setSizePolicy(QSizePolicy::Preferred,
                                              QSizePolicy::Preferred);
  m_characterHotkeysScrollArea->setMinimumHeight(10);
  m_characterHotkeysScrollArea->setMaximumHeight(240);
  m_characterHotkeysScrollArea->setFixedHeight(10);
  m_characterHotkeysScrollArea->setStyleSheet(
      "QScrollArea { background-color: transparent; border: none; }");

  m_characterHotkeysContainer = new QWidget();
  m_characterHotkeysContainer->setStyleSheet(
      "QWidget { background-color: transparent; }");
  m_characterHotkeysLayout = new QVBoxLayout(m_characterHotkeysContainer);
  m_characterHotkeysLayout->setContentsMargins(0, 0, 0, 0);
  m_characterHotkeysLayout->setSpacing(8);
  m_characterHotkeysLayout->addStretch();

  m_characterHotkeysScrollArea->setWidget(m_characterHotkeysContainer);
  charHotkeysSectionLayout->addWidget(m_characterHotkeysScrollArea);

  charHotkeysSectionLayout->addSpacing(-8);

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

  m_cycleGroupsScrollArea = new QScrollArea();
  m_cycleGroupsScrollArea->setWidgetResizable(true);
  m_cycleGroupsScrollArea->setFrameShape(QFrame::NoFrame);
  m_cycleGroupsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_cycleGroupsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_cycleGroupsScrollArea->setStyleSheet(
      "QScrollArea { background-color: transparent; border: none; }");

  m_cycleGroupsContainer = new QWidget();
  m_cycleGroupsContainer->setStyleSheet(
      "QWidget { background-color: transparent; }");

  m_cycleGroupsLayout = new QVBoxLayout(m_cycleGroupsContainer);
  m_cycleGroupsLayout->setContentsMargins(0, 0, 0, 0);
  m_cycleGroupsLayout->setSpacing(6);
  m_cycleGroupsLayout->addStretch();

  m_cycleGroupsScrollArea->setWidget(m_cycleGroupsContainer);
  m_cycleGroupsScrollArea->setFixedHeight(10);

  cycleGroupsSectionLayout->addWidget(m_cycleGroupsScrollArea);

  cycleGroupsSectionLayout->addSpacing(-8);

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

  connect(m_notLoggedInForwardCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);

  QPushButton *clearNotLoggedInForwardButton = new QPushButton("Clear");
  clearNotLoggedInForwardButton->setFixedWidth(60);
  clearNotLoggedInForwardButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  connect(clearNotLoggedInForwardButton, &QPushButton::clicked,
          [this]() { m_notLoggedInForwardCapture->clearHotkey(); });

  QLabel *backwardLabel = new QLabel("Cycle backward:");
  backwardLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_notLoggedInBackwardCapture = new HotkeyCapture();
  m_notLoggedInBackwardCapture->setMinimumWidth(200);

  m_notLoggedInBackwardCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  connect(m_notLoggedInBackwardCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);

  QPushButton *clearNotLoggedInBackwardButton = new QPushButton("Clear");
  clearNotLoggedInBackwardButton->setFixedWidth(60);
  clearNotLoggedInBackwardButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
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

  connect(m_nonEVEForwardCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);

  QPushButton *clearNonEVEForwardButton = new QPushButton("Clear");
  clearNonEVEForwardButton->setFixedWidth(60);
  clearNonEVEForwardButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
  connect(clearNonEVEForwardButton, &QPushButton::clicked,
          [this]() { m_nonEVEForwardCapture->clearHotkey(); });

  QLabel *nonEVEBackwardLabel = new QLabel("Cycle backward:");
  nonEVEBackwardLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_nonEVEBackwardCapture = new HotkeyCapture();
  m_nonEVEBackwardCapture->setMinimumWidth(200);

  m_nonEVEBackwardCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  connect(m_nonEVEBackwardCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);

  QPushButton *clearNonEVEBackwardButton = new QPushButton("Clear");
  clearNonEVEBackwardButton->setFixedWidth(60);
  clearNonEVEBackwardButton->setStyleSheet(
      StyleSheet::getSecondaryButtonStyleSheet());
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
  layout->setSpacing(10);
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
  m_minimizeDelaySpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());

  minimizeGrid->addWidget(m_minimizeDelayLabel, 0, 0, Qt::AlignLeft);
  minimizeGrid->addWidget(m_minimizeDelaySpin, 0, 1);

  windowSectionLayout->addLayout(minimizeGrid);

  m_neverMinimizeLabel = new QLabel("Never Minimize Characters");
  m_neverMinimizeLabel->setStyleSheet(
      StyleSheet::getSectionSubHeaderStyleSheet() + " margin-top: 10px;");
  windowSectionLayout->addWidget(m_neverMinimizeLabel);

  m_neverMinimizeInfoLabel = new QLabel(
      "Specify character names that should never be minimized when inactive. "
      "These clients will remain visible even when 'Minimize inactive clients' "
      "is enabled.");
  m_neverMinimizeInfoLabel->setWordWrap(true);
  m_neverMinimizeInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  windowSectionLayout->addWidget(m_neverMinimizeInfoLabel);

  m_neverMinimizeScrollArea = new QScrollArea();
  m_neverMinimizeScrollArea->setWidgetResizable(true);
  m_neverMinimizeScrollArea->setHorizontalScrollBarPolicy(
      Qt::ScrollBarAlwaysOff);
  m_neverMinimizeScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_neverMinimizeScrollArea->setStyleSheet(
      "QScrollArea { background-color: transparent; border: none; }");

  m_neverMinimizeContainer = new QWidget();
  m_neverMinimizeLayout = new QVBoxLayout(m_neverMinimizeContainer);
  m_neverMinimizeLayout->setContentsMargins(4, 4, 4, 4);
  m_neverMinimizeLayout->setSpacing(6);
  m_neverMinimizeLayout->addStretch();

  m_neverMinimizeScrollArea->setWidget(m_neverMinimizeContainer);
  windowSectionLayout->addWidget(m_neverMinimizeScrollArea);

  updateNeverMinimizeScrollHeight();

  windowSectionLayout->addSpacing(-8);

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
            m_neverMinimizeLabel->setEnabled(checked);
            m_neverMinimizeInfoLabel->setEnabled(checked);
            m_neverMinimizeScrollArea->setEnabled(checked);
            m_addNeverMinimizeButton->setEnabled(checked);
            m_populateNeverMinimizeButton->setEnabled(checked);
          });

  emit m_minimizeInactiveCheck->toggled(m_minimizeInactiveCheck->isChecked());

  // Thumbnail Interactions Section
  QWidget *interactionsSection = new QWidget();
  interactionsSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *interactionsSectionLayout = new QVBoxLayout(interactionsSection);
  interactionsSectionLayout->setContentsMargins(16, 12, 16, 12);
  interactionsSectionLayout->setSpacing(10);

  tagWidget(interactionsSection,
            {"thumbnail", "interaction", "click", "mouse", "switch", "behavior",
             "down", "up", "press", "release"});

  QLabel *interactionsHeader = new QLabel("Thumbnail Interactions");
  interactionsHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  interactionsSectionLayout->addWidget(interactionsHeader);

  QLabel *interactionsInfoLabel = new QLabel(
      "Configure how clicking on thumbnails behaves when switching windows.");
  interactionsInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  interactionsSectionLayout->addWidget(interactionsInfoLabel);

  QGridLayout *interactionsGrid = new QGridLayout();
  interactionsGrid->setSpacing(10);
  interactionsGrid->setColumnMinimumWidth(0, 120);
  interactionsGrid->setColumnStretch(2, 1);

  m_switchModeLabel = new QLabel("Switch trigger:");
  m_switchModeLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_switchModeCombo = new QComboBox();
  m_switchModeCombo->addItem("Mouse Up (Release)");
  m_switchModeCombo->addItem("Mouse Down (Press)");
  m_switchModeCombo->setFixedWidth(200);
  m_switchModeCombo->setStyleSheet(
      StyleSheet::getComboBoxWithDisabledStyleSheet());
  m_switchModeCombo->setToolTip("Mouse Up: Switch windows when you release the "
                                "mouse button (traditional).\n"
                                "Mouse Down: Switch windows immediately when "
                                "you press the mouse button (faster).");

  interactionsGrid->addWidget(m_switchModeLabel, 0, 0, Qt::AlignLeft);
  interactionsGrid->addWidget(m_switchModeCombo, 0, 1);

  m_dragButtonLabel = new QLabel("Drag button:");
  m_dragButtonLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_dragButtonCombo = new QComboBox();
  m_dragButtonCombo->addItem("Left Click");
  m_dragButtonCombo->addItem("Right Click");
  m_dragButtonCombo->setFixedWidth(200);
  m_dragButtonCombo->setStyleSheet(
      StyleSheet::getComboBoxWithDisabledStyleSheet());
  m_dragButtonCombo->setToolTip(
      "Left Click: Drag thumbnails with left mouse button.\n"
      "Right Click: Drag thumbnails with right mouse button (traditional).");

  interactionsGrid->addWidget(m_dragButtonLabel, 1, 0, Qt::AlignLeft);
  interactionsGrid->addWidget(m_dragButtonCombo, 1, 1);

  interactionsSectionLayout->addLayout(interactionsGrid);

  layout->addWidget(interactionsSection);

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
  m_snapDistanceSpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());

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

  // Not Logged In EVE Clients Section
  QWidget *notLoggedInSection = new QWidget();
  notLoggedInSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *notLoggedInSectionLayout = new QVBoxLayout(notLoggedInSection);
  notLoggedInSectionLayout->setContentsMargins(16, 12, 16, 12);
  notLoggedInSectionLayout->setSpacing(10);

  tagWidget(notLoggedInSection, {"not logged in", "login", "position", "stack",
                                 "overlay", "eve", "client"});

  QLabel *notLoggedInHeader = new QLabel("Not Logged In EVE Clients");
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

void ConfigDialog::createNonEVEThumbnailsPage() {
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

  // Additional Applications Section
  QWidget *additionalAppsSection = new QWidget();
  additionalAppsSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *additionalAppsSectionLayout =
      new QVBoxLayout(additionalAppsSection);
  additionalAppsSectionLayout->setContentsMargins(16, 12, 16, 12);
  additionalAppsSectionLayout->setSpacing(10);

  tagWidget(additionalAppsSection,
            {"additional", "applications", "process", "non-eve", "extra",
             "previews", "overlay", "executable", "thumbnail"});

  QLabel *additionalAppsHeader = new QLabel("Additional Applications");
  additionalAppsHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  additionalAppsSectionLayout->addWidget(additionalAppsHeader);

  QLabel *additionalAppsInfoLabel = new QLabel(
      "Add other executable names to create thumbnails for. EVE Online clients "
      "(exefile.exe) are always included. Case-insensitive.");
  additionalAppsInfoLabel->setWordWrap(true);
  additionalAppsInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  additionalAppsSectionLayout->addWidget(additionalAppsInfoLabel);

  m_showNonEVEOverlayCheck =
      new QCheckBox("Show overlay text on non-EVE thumbnails");
  m_showNonEVEOverlayCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  additionalAppsSectionLayout->addWidget(m_showNonEVEOverlayCheck);

  additionalAppsSectionLayout->addSpacing(5);

  QLabel *processListLabel = new QLabel("Process Names");
  processListLabel->setStyleSheet(StyleSheet::getSectionSubHeaderStyleSheet());
  additionalAppsSectionLayout->addWidget(processListLabel);

  m_processNamesScrollArea = new QScrollArea();
  m_processNamesScrollArea->setWidgetResizable(true);
  m_processNamesScrollArea->setHorizontalScrollBarPolicy(
      Qt::ScrollBarAlwaysOff);
  m_processNamesScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_processNamesScrollArea->setStyleSheet(
      "QScrollArea { background-color: transparent; border: none; }");

  m_processNamesContainer = new QWidget();
  m_processNamesLayout = new QVBoxLayout(m_processNamesContainer);
  m_processNamesLayout->setContentsMargins(4, 4, 4, 4);
  m_processNamesLayout->setSpacing(6);
  m_processNamesLayout->addStretch();

  m_processNamesScrollArea->setWidget(m_processNamesContainer);
  additionalAppsSectionLayout->addWidget(m_processNamesScrollArea);

  updateProcessNamesScrollHeight();

  additionalAppsSectionLayout->addSpacing(-8);

  QHBoxLayout *processFilterButtonLayout = new QHBoxLayout();
  m_addProcessNameButton = new QPushButton("Add Process");
  m_populateProcessNamesButton = new QPushButton("Populate from Open Windows");

  QString processFilterButtonStyle = StyleSheet::getSecondaryButtonStyleSheet();

  m_addProcessNameButton->setStyleSheet(processFilterButtonStyle);
  m_populateProcessNamesButton->setStyleSheet(processFilterButtonStyle);

  processFilterButtonLayout->addWidget(m_addProcessNameButton);
  processFilterButtonLayout->addWidget(m_populateProcessNamesButton);
  processFilterButtonLayout->addStretch();

  additionalAppsSectionLayout->addLayout(processFilterButtonLayout);

  connect(m_addProcessNameButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddProcessName);
  connect(m_populateProcessNamesButton, &QPushButton::clicked, this,
          &ConfigDialog::onPopulateProcessNames);

  layout->addWidget(additionalAppsSection);

  // Per-Process Thumbnail Sizes Section
  QWidget *processThumbnailSizesSection = new QWidget();
  processThumbnailSizesSection->setStyleSheet(
      StyleSheet::getSectionStyleSheet());
  QVBoxLayout *processThumbnailSizesSectionLayout =
      new QVBoxLayout(processThumbnailSizesSection);
  processThumbnailSizesSectionLayout->setContentsMargins(16, 12, 16, 12);
  processThumbnailSizesSectionLayout->setSpacing(10);

  tagWidget(processThumbnailSizesSection,
            {"thumbnail", "size", "custom", "process", "per-process", "width",
             "height", "dimension", "non-eve"});

  QLabel *processThumbnailSizesHeader =
      new QLabel("Per-Process Thumbnail Sizes");
  processThumbnailSizesHeader->setStyleSheet(
      StyleSheet::getSectionHeaderStyleSheet());
  processThumbnailSizesSectionLayout->addWidget(processThumbnailSizesHeader);

  QLabel *processThumbnailSizesInfoLabel = new QLabel(
      "Set custom thumbnail sizes for specific non-EVE applications. "
      "Leave empty to use the default size from the Appearance tab.");
  processThumbnailSizesInfoLabel->setWordWrap(true);
  processThumbnailSizesInfoLabel->setStyleSheet(
      StyleSheet::getInfoLabelStyleSheet());
  processThumbnailSizesSectionLayout->addWidget(processThumbnailSizesInfoLabel);

  m_processThumbnailSizesScrollArea = new QScrollArea();
  m_processThumbnailSizesScrollArea->setWidgetResizable(true);
  m_processThumbnailSizesScrollArea->setFrameShape(QFrame::NoFrame);
  m_processThumbnailSizesScrollArea->setHorizontalScrollBarPolicy(
      Qt::ScrollBarAlwaysOff);
  m_processThumbnailSizesScrollArea->setVerticalScrollBarPolicy(
      Qt::ScrollBarAsNeeded);
  m_processThumbnailSizesScrollArea->setSizePolicy(QSizePolicy::Preferred,
                                                   QSizePolicy::Preferred);
  m_processThumbnailSizesScrollArea->setMinimumHeight(10);
  m_processThumbnailSizesScrollArea->setMaximumHeight(240);
  m_processThumbnailSizesScrollArea->setFixedHeight(10);
  m_processThumbnailSizesScrollArea->setStyleSheet(
      "QScrollArea { background-color: transparent; border: none; }");

  m_processThumbnailSizesContainer = new QWidget();
  m_processThumbnailSizesContainer->setStyleSheet(
      "QWidget { background-color: transparent; }");
  m_processThumbnailSizesLayout =
      new QVBoxLayout(m_processThumbnailSizesContainer);
  m_processThumbnailSizesLayout->setContentsMargins(0, 0, 0, 0);
  m_processThumbnailSizesLayout->setSpacing(8);
  m_processThumbnailSizesLayout->addStretch();

  m_processThumbnailSizesScrollArea->setWidget(
      m_processThumbnailSizesContainer);
  processThumbnailSizesSectionLayout->addWidget(
      m_processThumbnailSizesScrollArea);

  processThumbnailSizesSectionLayout->addSpacing(-8);

  QHBoxLayout *processThumbnailSizesButtonLayout = new QHBoxLayout();
  m_addProcessThumbnailSizeButton = new QPushButton("Add Process");
  m_populateProcessThumbnailSizesButton =
      new QPushButton("Populate from Open Windows");
  m_resetProcessThumbnailSizesButton = new QPushButton("Reset All to Default");

  QString processThumbnailSizesButtonStyle =
      StyleSheet::getSecondaryButtonStyleSheet();
  m_addProcessThumbnailSizeButton->setStyleSheet(
      processThumbnailSizesButtonStyle);
  m_populateProcessThumbnailSizesButton->setStyleSheet(
      processThumbnailSizesButtonStyle);
  m_resetProcessThumbnailSizesButton->setStyleSheet(
      processThumbnailSizesButtonStyle);

  connect(m_addProcessThumbnailSizeButton, &QPushButton::clicked, this,
          &ConfigDialog::onAddProcessThumbnailSize);
  connect(m_populateProcessThumbnailSizesButton, &QPushButton::clicked, this,
          &ConfigDialog::onPopulateProcessThumbnailSizes);
  connect(m_resetProcessThumbnailSizesButton, &QPushButton::clicked, this,
          &ConfigDialog::onResetProcessThumbnailSizesToDefault);

  processThumbnailSizesButtonLayout->addWidget(m_addProcessThumbnailSizeButton);
  processThumbnailSizesButtonLayout->addWidget(
      m_populateProcessThumbnailSizesButton);
  processThumbnailSizesButtonLayout->addWidget(
      m_resetProcessThumbnailSizesButton);
  processThumbnailSizesButtonLayout->addStretch();

  processThumbnailSizesSectionLayout->addLayout(
      processThumbnailSizesButtonLayout);

  layout->addWidget(processThumbnailSizesSection);

  QHBoxLayout *resetLayout = new QHBoxLayout();
  resetLayout->addStretch();
  QPushButton *resetButton = new QPushButton("Reset to Defaults");
  resetButton->setStyleSheet(StyleSheet::getResetButtonStyleSheet());
  connect(resetButton, &QPushButton::clicked, this,
          &ConfigDialog::onResetNonEVEDefaults);
  resetLayout->addWidget(resetButton);
  layout->addLayout(resetLayout);

  layout->addStretch();

  scrollArea->setWidget(scrollWidget);

  QVBoxLayout *pageLayout = new QVBoxLayout(page);
  pageLayout->setContentsMargins(0, 0, 0, 0);
  pageLayout->addWidget(scrollArea);

  m_stackedWidget->addWidget(page);
}

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
  layout->setSpacing(10);
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
                 "contain fleet notifications and other events. "
                 "Log paths support environment variables (e.g., "
                 "<tt>%USERPROFILE%\\Documents\\EVE\\logs\\Chatlogs</tt>) for "
                 "portable configurations across different PCs.");
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

  connect(m_enableGameLogMonitoringCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            m_gameLogDirectoryLabel->setEnabled(checked);
            m_gameLogDirectoryEdit->setEnabled(checked);
            m_gameLogBrowseButton->setEnabled(checked);
          });

  layout->addWidget(logMonitoringSection);

  // Combat Log Events Section with Tabs
  QWidget *combatSection = new QWidget();
  combatSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *combatSectionLayout = new QVBoxLayout(combatSection);
  combatSectionLayout->setContentsMargins(16, 12, 16, 12);
  combatSectionLayout->setSpacing(10);

  tagWidget(combatSection, {"combat", "event", "message", "notification",
                            "fleet", "warp", "regroup", "compression"});

  QLabel *combatHeader = new QLabel("Combat Log Events");
  combatHeader->setStyleSheet(StyleSheet::getSectionHeaderStyleSheet());
  combatSectionLayout->addWidget(combatHeader);

  QLabel *combatInfoLabel = new QLabel(
      "Display event notifications from game logs on thumbnail overlays. "
      "Messages include fleet invites, warp follows, regroups, compression, "
      "and mining crystal events.");
  combatInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  combatInfoLabel->setWordWrap(true);
  combatSectionLayout->addWidget(combatInfoLabel);

  // Master enable checkbox
  m_showCombatMessagesCheck = new QCheckBox("Show combat log event messages");
  m_showCombatMessagesCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  combatSectionLayout->addWidget(m_showCombatMessagesCheck);

  // Global settings
  QHBoxLayout *positionLayout = new QHBoxLayout();
  positionLayout->setContentsMargins(0, 0, 0, 0);
  m_combatMessagePositionLabel = new QLabel("Message position:");
  m_combatMessagePositionLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_combatMessagePositionLabel->setFixedWidth(150);

  m_combatMessagePositionCombo = new QComboBox();
  m_combatMessagePositionCombo->setStyleSheet(
      StyleSheet::getComboBoxStyleSheet());
  m_combatMessagePositionCombo->addItem("Top Left", 0);
  m_combatMessagePositionCombo->addItem("Top Center", 1);
  m_combatMessagePositionCombo->addItem("Top Right", 2);
  m_combatMessagePositionCombo->addItem("Center Left", 3);
  m_combatMessagePositionCombo->addItem("Center", 4);
  m_combatMessagePositionCombo->addItem("Center Right", 5);
  m_combatMessagePositionCombo->addItem("Bottom Left", 6);
  m_combatMessagePositionCombo->addItem("Bottom Center", 7);
  m_combatMessagePositionCombo->addItem("Bottom Right", 8);
  m_combatMessagePositionCombo->setFixedWidth(150);

  positionLayout->addWidget(m_combatMessagePositionLabel);
  positionLayout->addWidget(m_combatMessagePositionCombo);
  positionLayout->addStretch();
  combatSectionLayout->addLayout(positionLayout);

  QHBoxLayout *fontLayout = new QHBoxLayout();
  fontLayout->setContentsMargins(0, 0, 0, 0);
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

  // X Offset slider
  QHBoxLayout *offsetXLayout = new QHBoxLayout();
  offsetXLayout->setContentsMargins(0, 0, 0, 0);
  m_combatMessageOffsetXLabel = new QLabel("X Offset:");
  m_combatMessageOffsetXLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_combatMessageOffsetXLabel->setFixedWidth(150);
  m_combatMessageOffsetXSlider = new QSlider(Qt::Horizontal);
  m_combatMessageOffsetXSlider->setRange(-20, 20);
  m_combatMessageOffsetXSlider->setValue(0);
  m_combatMessageOffsetXSlider->setTickPosition(QSlider::TicksBelow);
  m_combatMessageOffsetXSlider->setTickInterval(5);
  m_combatMessageOffsetXSlider->setFixedWidth(150);
  m_combatMessageOffsetXValue = new QLabel("0");
  m_combatMessageOffsetXValue->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_combatMessageOffsetXValue->setFixedWidth(30);
  m_combatMessageOffsetXValue->setAlignment(Qt::AlignCenter);
  offsetXLayout->addWidget(m_combatMessageOffsetXLabel);
  offsetXLayout->addWidget(m_combatMessageOffsetXSlider);
  offsetXLayout->addWidget(m_combatMessageOffsetXValue);
  offsetXLayout->addStretch();
  combatSectionLayout->addLayout(offsetXLayout);

  // Y Offset slider
  QHBoxLayout *offsetYLayout = new QHBoxLayout();
  offsetYLayout->setContentsMargins(0, 0, 0, 0);
  m_combatMessageOffsetYLabel = new QLabel("Y Offset:");
  m_combatMessageOffsetYLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_combatMessageOffsetYLabel->setFixedWidth(150);
  m_combatMessageOffsetYSlider = new QSlider(Qt::Horizontal);
  m_combatMessageOffsetYSlider->setRange(-20, 20);
  m_combatMessageOffsetYSlider->setValue(0);
  m_combatMessageOffsetYSlider->setTickPosition(QSlider::TicksBelow);
  m_combatMessageOffsetYSlider->setTickInterval(5);
  m_combatMessageOffsetYSlider->setFixedWidth(150);
  m_combatMessageOffsetYValue = new QLabel("0");
  m_combatMessageOffsetYValue->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_combatMessageOffsetYValue->setFixedWidth(30);
  m_combatMessageOffsetYValue->setAlignment(Qt::AlignCenter);
  offsetYLayout->addWidget(m_combatMessageOffsetYLabel);
  offsetYLayout->addWidget(m_combatMessageOffsetYSlider);
  offsetYLayout->addWidget(m_combatMessageOffsetYValue);
  offsetYLayout->addStretch();
  combatSectionLayout->addLayout(offsetYLayout);

  connect(m_combatMessageOffsetXSlider, &QSlider::valueChanged, this,
          [this](int value) {
            m_combatMessageOffsetXValue->setText(QString::number(value));
          });
  connect(m_combatMessageOffsetYSlider, &QSlider::valueChanged, this,
          [this](int value) {
            m_combatMessageOffsetYValue->setText(QString::number(value));
          });

  // Create tab widget for individual event settings
  QTabWidget *combatTabs = new QTabWidget();
  combatTabs->setStyleSheet(StyleSheet::getTabWidgetStyleSheet());

  // Helper to create event tab
  auto createEventTab = [&](const QString &eventType, const QString &label,
                            QCheckBox *&checkbox) {
    QWidget *tab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    checkbox = new QCheckBox("Enable this event type");
    checkbox->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
    layout->addWidget(checkbox);

    QCheckBox *suppressCheck = new QCheckBox("Suppress for focused window");
    suppressCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
    suppressCheck->setToolTip("Do not show this event notification for the "
                              "currently focused/active window");
    connect(
        suppressCheck, &QCheckBox::toggled, this, [eventType](bool checked) {
          Config::instance().setCombatEventSuppressFocused(eventType, checked);
        });
    m_eventSuppressFocusedCheckBoxes[eventType] = suppressCheck;
    layout->addWidget(suppressCheck);

    layout->addSpacing(8);

    // Color setting
    QHBoxLayout *colorLayout = new QHBoxLayout();
    QLabel *colorLabel = new QLabel("Message color:");
    colorLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
    colorLabel->setFixedWidth(120);
    m_eventColorLabels[eventType] = colorLabel;

    QPushButton *colorBtn = new QPushButton();
    colorBtn->setFixedSize(150, 32);
    colorBtn->setCursor(Qt::PointingHandCursor);
    updateColorButton(colorBtn, Qt::white);
    connect(
        colorBtn, &QPushButton::clicked, this, [this, eventType, colorBtn]() {
          QColor currentColor = Config::instance().combatEventColor(eventType);
          QColor color = QColorDialog::getColor(currentColor, this,
                                                QString("Select Color"));
          if (color.isValid()) {
            updateColorButton(colorBtn, color);
            Config::instance().setCombatEventColor(eventType, color);
          }
        });
    m_eventColorButtons[eventType] = colorBtn;
    colorLayout->addWidget(colorLabel);
    colorLayout->addWidget(colorBtn);
    colorLayout->addStretch();
    layout->addLayout(colorLayout);

    // Duration setting
    QHBoxLayout *durationLayout = new QHBoxLayout();
    QLabel *durationLabel = new QLabel("Display duration:");
    durationLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
    durationLabel->setFixedWidth(120);
    m_eventDurationLabels[eventType] = durationLabel;

    QSpinBox *durationSpin = new QSpinBox();
    durationSpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());
    durationSpin->setRange(1, 30);
    durationSpin->setSingleStep(1);
    durationSpin->setSuffix(" sec");
    durationSpin->setFixedWidth(100);
    connect(durationSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [eventType](int value) {
              Config::instance().setCombatEventDuration(eventType,
                                                        value * 1000);
            });
    m_eventDurationSpins[eventType] = durationSpin;
    durationLayout->addWidget(durationLabel);
    durationLayout->addWidget(durationSpin);
    durationLayout->addStretch();
    layout->addLayout(durationLayout);

    layout->addSpacing(12);

    // Border settings group
    QCheckBox *borderCheck = new QCheckBox("Show colored border");
    borderCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
    borderCheck->setToolTip("Show colored border when this event occurs");
    connect(borderCheck, &QCheckBox::toggled, this,
            [this, eventType](bool checked) {
              Config::instance().setCombatEventBorderHighlight(eventType,
                                                               checked);
              if (m_eventBorderStyleLabels.contains(eventType)) {
                m_eventBorderStyleLabels[eventType]->setEnabled(checked);
              }
              if (m_eventBorderStyleCombos.contains(eventType)) {
                m_eventBorderStyleCombos[eventType]->setEnabled(checked);
              }
            });
    m_eventBorderCheckBoxes[eventType] = borderCheck;
    layout->addWidget(borderCheck);

    QHBoxLayout *borderStyleLayout = new QHBoxLayout();
    borderStyleLayout->setContentsMargins(24, 0, 0, 0);
    QLabel *styleLabel = new QLabel("Border style:");
    styleLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
    styleLabel->setFixedWidth(96);
    m_eventBorderStyleLabels[eventType] = styleLabel;

    QComboBox *styleCombo = new QComboBox();
    styleCombo->setStyleSheet(StyleSheet::getComboBoxStyleSheet());
    styleCombo->addItem("Solid", static_cast<int>(BorderStyle::Solid));
    styleCombo->addItem("Dashed", static_cast<int>(BorderStyle::Dashed));
    styleCombo->addItem("Dotted", static_cast<int>(BorderStyle::Dotted));
    styleCombo->addItem("Dash-Dot", static_cast<int>(BorderStyle::DashDot));
    styleCombo->addItem("Faded", static_cast<int>(BorderStyle::FadedEdges));
    styleCombo->addItem("Corners",
                        static_cast<int>(BorderStyle::CornerAccents));
    styleCombo->addItem("Rounded",
                        static_cast<int>(BorderStyle::RoundedCorners));
    styleCombo->addItem("Neon", static_cast<int>(BorderStyle::Neon));
    styleCombo->addItem("Shimmer", static_cast<int>(BorderStyle::Shimmer));
    styleCombo->addItem("Thick/Thin", static_cast<int>(BorderStyle::ThickThin));
    styleCombo->addItem("Electric Arc",
                        static_cast<int>(BorderStyle::ElectricArc));
    styleCombo->addItem("Rainbow", static_cast<int>(BorderStyle::Rainbow));
    styleCombo->addItem("Breathing Glow",
                        static_cast<int>(BorderStyle::BreathingGlow));
    styleCombo->addItem("Double Glow",
                        static_cast<int>(BorderStyle::DoubleGlow));
    styleCombo->addItem("Zigzag", static_cast<int>(BorderStyle::Zigzag));
    styleCombo->setFixedWidth(150);
    styleCombo->setToolTip("Border style for this event");
    connect(styleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, eventType, styleCombo](int index) {
              BorderStyle style =
                  static_cast<BorderStyle>(styleCombo->itemData(index).toInt());
              Config::instance().setCombatBorderStyle(eventType, style);
            });
    m_eventBorderStyleCombos[eventType] = styleCombo;
    borderStyleLayout->addWidget(styleLabel);
    borderStyleLayout->addWidget(styleCombo);
    borderStyleLayout->addStretch();
    layout->addLayout(borderStyleLayout);

    layout->addSpacing(12);

    // Sound notification settings
    QCheckBox *soundCheck = new QCheckBox("Play sound notification");
    soundCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
    soundCheck->setToolTip("Play a sound when this event occurs");
    connect(soundCheck, &QCheckBox::toggled, this,
            [this, eventType](bool checked) {
              Config::instance().setCombatEventSoundEnabled(eventType, checked);
              if (m_eventSoundFileLabels.contains(eventType)) {
                m_eventSoundFileLabels[eventType]->setEnabled(checked);
              }
              if (m_eventSoundFileButtons.contains(eventType)) {
                m_eventSoundFileButtons[eventType]->setEnabled(checked);
              }
              if (m_eventSoundVolumeLabels.contains(eventType)) {
                m_eventSoundVolumeLabels[eventType]->setEnabled(checked);
              }
              if (m_eventSoundVolumeSliders.contains(eventType)) {
                m_eventSoundVolumeSliders[eventType]->setEnabled(checked);
              }
              if (m_eventSoundVolumeValueLabels.contains(eventType)) {
                m_eventSoundVolumeValueLabels[eventType]->setEnabled(checked);
              }
              if (m_eventSoundPlayButtons.contains(eventType)) {
                m_eventSoundPlayButtons[eventType]->setEnabled(checked);
              }
            });
    m_eventSoundCheckBoxes[eventType] = soundCheck;
    layout->addWidget(soundCheck);

    QHBoxLayout *soundFileLayout = new QHBoxLayout();
    soundFileLayout->setContentsMargins(24, 0, 0, 0);
    QLabel *soundFileLabel = new QLabel("Sound file:");
    soundFileLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
    soundFileLabel->setFixedWidth(96);
    m_eventSoundFileLabels[eventType] = soundFileLabel;

    QPushButton *soundFileBtn = new QPushButton("Browse...");
    soundFileBtn->setStyleSheet(StyleSheet::getButtonStyleSheet());
    soundFileBtn->setFixedWidth(150);
    soundFileBtn->setCursor(Qt::PointingHandCursor);
    soundFileBtn->setToolTip("Select a sound file (.wav recommended)");
    connect(soundFileBtn, &QPushButton::clicked, this,
            [this, eventType, soundFileBtn]() {
              QString currentFile =
                  Config::instance().combatEventSoundFile(eventType);
              QString fileName = QFileDialog::getOpenFileName(
                  this, "Select Sound File", currentFile,
                  "Sound Files (*.wav *.mp3 *.ogg);;All Files (*)");
              if (!fileName.isEmpty()) {
                Config::instance().setCombatEventSoundFile(eventType, fileName);
                QFileInfo fileInfo(fileName);
                soundFileBtn->setText(fileInfo.fileName());
              }
            });
    m_eventSoundFileButtons[eventType] = soundFileBtn;

    QPushButton *playBtn = new QPushButton("▶ Test");
    playBtn->setStyleSheet(StyleSheet::getButtonStyleSheet());
    playBtn->setFixedWidth(80);
    playBtn->setCursor(Qt::PointingHandCursor);
    playBtn->setToolTip("Play the selected sound file");
    connect(playBtn, &QPushButton::clicked, this, [this, eventType]() {
      QString soundFile = Config::instance().combatEventSoundFile(eventType);
      if (!soundFile.isEmpty() && QFile::exists(soundFile)) {
        if (!m_testSoundEffect) {
          m_testSoundEffect = std::make_unique<QSoundEffect>();
        }
        m_testSoundEffect->setSource(QUrl::fromLocalFile(soundFile));
        int volume = Config::instance().combatEventSoundVolume(eventType);
        m_testSoundEffect->setVolume(volume / 100.0);
        m_testSoundEffect->play();
      }
    });
    m_eventSoundPlayButtons[eventType] = playBtn;

    soundFileLayout->addWidget(soundFileLabel);
    soundFileLayout->addWidget(soundFileBtn);
    soundFileLayout->addWidget(playBtn);
    soundFileLayout->addStretch();
    layout->addLayout(soundFileLayout);

    QHBoxLayout *volumeLayout = new QHBoxLayout();
    volumeLayout->setContentsMargins(24, 0, 0, 0);
    QLabel *volumeLabel = new QLabel("Volume:");
    volumeLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
    volumeLabel->setFixedWidth(96);
    m_eventSoundVolumeLabels[eventType] = volumeLabel;

    QSlider *volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(Config::DEFAULT_COMBAT_SOUND_VOLUME);
    volumeSlider->setFixedWidth(120);
    m_eventSoundVolumeSliders[eventType] = volumeSlider;

    QLabel *volumeValueLabel = new QLabel("70%");
    volumeValueLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
    volumeValueLabel->setFixedWidth(40);
    m_eventSoundVolumeValueLabels[eventType] = volumeValueLabel;

    connect(volumeSlider, &QSlider::valueChanged, this,
            [this, eventType, volumeValueLabel](int value) {
              Config::instance().setCombatEventSoundVolume(eventType, value);
              volumeValueLabel->setText(QString("%1%").arg(value));
            });

    volumeLayout->addWidget(volumeLabel);
    volumeLayout->addWidget(volumeSlider);
    volumeLayout->addWidget(volumeValueLabel);
    volumeLayout->addStretch();
    layout->addLayout(volumeLayout);

    layout->addStretch();
    return tab;
  };

  // Create individual tabs for each event
  combatTabs->addTab(createEventTab("fleet_invite", "Fleet Invites",
                                    m_combatEventFleetInviteCheck),
                     "Fleet Invites");
  combatTabs->addTab(createEventTab("follow_warp", "Following in Warp",
                                    m_combatEventFollowWarpCheck),
                     "Warp Follow");
  combatTabs->addTab(
      createEventTab("regroup", "Regroup Commands", m_combatEventRegroupCheck),
      "Regroup");
  combatTabs->addTab(createEventTab("compression", "Compression Events",
                                    m_combatEventCompressionCheck),
                     "Compression");
  combatTabs->addTab(
      createEventTab("decloak", "Decloak Events", m_combatEventDecloakCheck),
      "Decloak");
  combatTabs->addTab(createEventTab("convo_request", "Convo Request",
                                    m_combatEventConvoRequestCheck),
                     "Convo Request");
  combatTabs->addTab(createEventTab("crystal_broke", "Mining Crystal Broke",
                                    m_combatEventCrystalBrokeCheck),
                     "Crystal Broke");

  // Mining stopped tab with special timeout setting
  QWidget *miningStopTab = new QWidget();
  QVBoxLayout *miningStopLayout = new QVBoxLayout(miningStopTab);
  miningStopLayout->setContentsMargins(16, 16, 16, 16);
  miningStopLayout->setSpacing(12);

  m_combatEventMiningStopCheck = new QCheckBox("Enable this event type");
  m_combatEventMiningStopCheck->setStyleSheet(
      StyleSheet::getCheckBoxStyleSheet());
  miningStopLayout->addWidget(m_combatEventMiningStopCheck);

  QCheckBox *miningSuppressCheck = new QCheckBox("Suppress for focused window");
  miningSuppressCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  miningSuppressCheck->setToolTip(
      "Do not show this event notification for the currently focused/active "
      "window");
  connect(miningSuppressCheck, &QCheckBox::toggled, this, [](bool checked) {
    Config::instance().setCombatEventSuppressFocused("mining_stopped", checked);
  });
  m_eventSuppressFocusedCheckBoxes["mining_stopped"] = miningSuppressCheck;
  miningStopLayout->addWidget(miningSuppressCheck);

  miningStopLayout->addSpacing(8);

  QHBoxLayout *miningColorLayout = new QHBoxLayout();
  QLabel *miningColorLabel = new QLabel("Message color:");
  miningColorLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  miningColorLabel->setFixedWidth(120);
  m_eventColorLabels["mining_stopped"] = miningColorLabel;

  QPushButton *miningColorBtn = new QPushButton();
  miningColorBtn->setFixedSize(150, 32);
  miningColorBtn->setCursor(Qt::PointingHandCursor);
  updateColorButton(miningColorBtn, Qt::white);
  connect(miningColorBtn, &QPushButton::clicked, this,
          [this, miningColorBtn]() {
            QColor currentColor =
                Config::instance().combatEventColor("mining_stopped");
            QColor color =
                QColorDialog::getColor(currentColor, this, "Select Color");
            if (color.isValid()) {
              updateColorButton(miningColorBtn, color);
              Config::instance().setCombatEventColor("mining_stopped", color);
            }
          });
  m_eventColorButtons["mining_stopped"] = miningColorBtn;
  miningColorLayout->addWidget(miningColorLabel);
  miningColorLayout->addWidget(miningColorBtn);
  miningColorLayout->addStretch();
  miningStopLayout->addLayout(miningColorLayout);

  QHBoxLayout *miningDurationLayout = new QHBoxLayout();
  QLabel *miningDurationLabel = new QLabel("Display duration:");
  miningDurationLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  miningDurationLabel->setFixedWidth(120);
  m_eventDurationLabels["mining_stopped"] = miningDurationLabel;

  QSpinBox *miningDurationSpin = new QSpinBox();
  miningDurationSpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());
  miningDurationSpin->setRange(1, 30);
  miningDurationSpin->setSingleStep(1);
  miningDurationSpin->setSuffix(" sec");
  miningDurationSpin->setFixedWidth(100);
  connect(miningDurationSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [](int value) {
            Config::instance().setCombatEventDuration("mining_stopped",
                                                      value * 1000);
          });
  m_eventDurationSpins["mining_stopped"] = miningDurationSpin;
  miningDurationLayout->addWidget(miningDurationLabel);
  miningDurationLayout->addWidget(miningDurationSpin);
  miningDurationLayout->addStretch();
  miningStopLayout->addLayout(miningDurationLayout);

  // Mining-specific timeout
  QHBoxLayout *miningTimeoutLayout = new QHBoxLayout();
  m_miningTimeoutLabel = new QLabel("Mining timeout:");
  m_miningTimeoutLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  m_miningTimeoutLabel->setFixedWidth(120);
  m_miningTimeoutLabel->setToolTip(
      "Time to wait before showing mining stopped event");

  m_miningTimeoutSpin = new QSpinBox();
  m_miningTimeoutSpin->setStyleSheet(StyleSheet::getSpinBoxStyleSheet());
  m_miningTimeoutSpin->setRange(15, 120);
  m_miningTimeoutSpin->setSingleStep(5);
  m_miningTimeoutSpin->setSuffix(" sec");
  m_miningTimeoutSpin->setFixedWidth(100);

  miningTimeoutLayout->addWidget(m_miningTimeoutLabel);
  miningTimeoutLayout->addWidget(m_miningTimeoutSpin);
  miningTimeoutLayout->addStretch();
  miningStopLayout->addLayout(miningTimeoutLayout);

  miningStopLayout->addSpacing(12);

  QCheckBox *miningBorderCheck = new QCheckBox("Show colored border");
  miningBorderCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  connect(miningBorderCheck, &QCheckBox::toggled, this, [this](bool checked) {
    Config::instance().setCombatEventBorderHighlight("mining_stopped", checked);
    if (m_eventBorderStyleLabels.contains("mining_stopped")) {
      m_eventBorderStyleLabels["mining_stopped"]->setEnabled(checked);
    }
    if (m_eventBorderStyleCombos.contains("mining_stopped")) {
      m_eventBorderStyleCombos["mining_stopped"]->setEnabled(checked);
    }
  });
  m_eventBorderCheckBoxes["mining_stopped"] = miningBorderCheck;
  miningStopLayout->addWidget(miningBorderCheck);

  QHBoxLayout *miningBorderStyleLayout = new QHBoxLayout();
  miningBorderStyleLayout->setContentsMargins(24, 0, 0, 0);
  QLabel *miningStyleLabel = new QLabel("Border style:");
  miningStyleLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  miningStyleLabel->setFixedWidth(96);
  m_eventBorderStyleLabels["mining_stopped"] = miningStyleLabel;

  QComboBox *miningStyleCombo = new QComboBox();
  miningStyleCombo->setStyleSheet(StyleSheet::getComboBoxStyleSheet());
  miningStyleCombo->addItem("Solid", static_cast<int>(BorderStyle::Solid));
  miningStyleCombo->addItem("Dashed", static_cast<int>(BorderStyle::Dashed));
  miningStyleCombo->addItem("Dotted", static_cast<int>(BorderStyle::Dotted));
  miningStyleCombo->addItem("Dash-Dot", static_cast<int>(BorderStyle::DashDot));
  miningStyleCombo->addItem("Faded", static_cast<int>(BorderStyle::FadedEdges));
  miningStyleCombo->addItem("Corners",
                            static_cast<int>(BorderStyle::CornerAccents));
  miningStyleCombo->addItem("Rounded",
                            static_cast<int>(BorderStyle::RoundedCorners));
  miningStyleCombo->addItem("Neon", static_cast<int>(BorderStyle::Neon));
  miningStyleCombo->addItem("Shimmer", static_cast<int>(BorderStyle::Shimmer));
  miningStyleCombo->addItem("Thick/Thin",
                            static_cast<int>(BorderStyle::ThickThin));
  miningStyleCombo->addItem("Electric Arc",
                            static_cast<int>(BorderStyle::ElectricArc));
  miningStyleCombo->addItem("Rainbow", static_cast<int>(BorderStyle::Rainbow));
  miningStyleCombo->addItem("Breathing Glow",
                            static_cast<int>(BorderStyle::BreathingGlow));
  miningStyleCombo->addItem("Double Glow",
                            static_cast<int>(BorderStyle::DoubleGlow));
  miningStyleCombo->addItem("Zigzag", static_cast<int>(BorderStyle::Zigzag));
  miningStyleCombo->setFixedWidth(150);
  connect(miningStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this, miningStyleCombo](int index) {
            BorderStyle style = static_cast<BorderStyle>(
                miningStyleCombo->itemData(index).toInt());
            Config::instance().setCombatBorderStyle("mining_stopped", style);
          });
  m_eventBorderStyleCombos["mining_stopped"] = miningStyleCombo;
  miningBorderStyleLayout->addWidget(miningStyleLabel);
  miningBorderStyleLayout->addWidget(miningStyleCombo);
  miningBorderStyleLayout->addStretch();
  miningStopLayout->addLayout(miningBorderStyleLayout);

  miningStopLayout->addSpacing(12);

  // Sound notification settings for mining stopped
  QCheckBox *miningSoundCheck = new QCheckBox("Play sound notification");
  miningSoundCheck->setStyleSheet(StyleSheet::getCheckBoxStyleSheet());
  miningSoundCheck->setToolTip("Play a sound when this event occurs");
  connect(miningSoundCheck, &QCheckBox::toggled, this, [this](bool checked) {
    Config::instance().setCombatEventSoundEnabled("mining_stopped", checked);
    if (m_eventSoundFileLabels.contains("mining_stopped")) {
      m_eventSoundFileLabels["mining_stopped"]->setEnabled(checked);
    }
    if (m_eventSoundFileButtons.contains("mining_stopped")) {
      m_eventSoundFileButtons["mining_stopped"]->setEnabled(checked);
    }
    if (m_eventSoundVolumeLabels.contains("mining_stopped")) {
      m_eventSoundVolumeLabels["mining_stopped"]->setEnabled(checked);
    }
    if (m_eventSoundVolumeSliders.contains("mining_stopped")) {
      m_eventSoundVolumeSliders["mining_stopped"]->setEnabled(checked);
    }
    if (m_eventSoundVolumeValueLabels.contains("mining_stopped")) {
      m_eventSoundVolumeValueLabels["mining_stopped"]->setEnabled(checked);
    }
    if (m_eventSoundPlayButtons.contains("mining_stopped")) {
      m_eventSoundPlayButtons["mining_stopped"]->setEnabled(checked);
    }
  });
  m_eventSoundCheckBoxes["mining_stopped"] = miningSoundCheck;
  miningStopLayout->addWidget(miningSoundCheck);

  QHBoxLayout *miningSoundFileLayout = new QHBoxLayout();
  miningSoundFileLayout->setContentsMargins(24, 0, 0, 0);
  QLabel *miningSoundFileLabel = new QLabel("Sound file:");
  miningSoundFileLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  miningSoundFileLabel->setFixedWidth(96);
  m_eventSoundFileLabels["mining_stopped"] = miningSoundFileLabel;

  QPushButton *miningSoundFileBtn = new QPushButton("Browse...");
  miningSoundFileBtn->setStyleSheet(StyleSheet::getButtonStyleSheet());
  miningSoundFileBtn->setFixedWidth(150);
  miningSoundFileBtn->setCursor(Qt::PointingHandCursor);
  miningSoundFileBtn->setToolTip("Select a sound file (.wav recommended)");
  connect(miningSoundFileBtn, &QPushButton::clicked, this,
          [this, miningSoundFileBtn]() {
            QString currentFile =
                Config::instance().combatEventSoundFile("mining_stopped");
            QString fileName = QFileDialog::getOpenFileName(
                this, "Select Sound File", currentFile,
                "Sound Files (*.wav *.mp3 *.ogg);;All Files (*)");
            if (!fileName.isEmpty()) {
              Config::instance().setCombatEventSoundFile("mining_stopped",
                                                         fileName);
              QFileInfo fileInfo(fileName);
              miningSoundFileBtn->setText(fileInfo.fileName());
            }
          });
  m_eventSoundFileButtons["mining_stopped"] = miningSoundFileBtn;

  QPushButton *miningSoundPlayBtn = new QPushButton("▶ Test");
  miningSoundPlayBtn->setStyleSheet(StyleSheet::getButtonStyleSheet());
  miningSoundPlayBtn->setFixedWidth(80);
  miningSoundPlayBtn->setCursor(Qt::PointingHandCursor);
  miningSoundPlayBtn->setToolTip("Play the selected sound file");
  connect(miningSoundPlayBtn, &QPushButton::clicked, this, [this]() {
    QString soundFile =
        Config::instance().combatEventSoundFile("mining_stopped");
    if (!soundFile.isEmpty() && QFile::exists(soundFile)) {
      if (!m_testSoundEffect) {
        m_testSoundEffect = std::make_unique<QSoundEffect>();
      }
      m_testSoundEffect->setSource(QUrl::fromLocalFile(soundFile));
      int volume = Config::instance().combatEventSoundVolume("mining_stopped");
      m_testSoundEffect->setVolume(volume / 100.0);
      m_testSoundEffect->play();
    }
  });
  m_eventSoundPlayButtons["mining_stopped"] = miningSoundPlayBtn;

  miningSoundFileLayout->addWidget(miningSoundFileLabel);
  miningSoundFileLayout->addWidget(miningSoundFileBtn);
  miningSoundFileLayout->addWidget(miningSoundPlayBtn);
  miningSoundFileLayout->addStretch();
  miningStopLayout->addLayout(miningSoundFileLayout);

  QHBoxLayout *miningSoundVolumeLayout = new QHBoxLayout();
  miningSoundVolumeLayout->setContentsMargins(24, 0, 0, 0);
  QLabel *miningSoundVolumeLabel = new QLabel("Volume:");
  miningSoundVolumeLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  miningSoundVolumeLabel->setFixedWidth(96);
  m_eventSoundVolumeLabels["mining_stopped"] = miningSoundVolumeLabel;

  QSlider *miningSoundVolumeSlider = new QSlider(Qt::Horizontal);
  miningSoundVolumeSlider->setRange(0, 100);
  miningSoundVolumeSlider->setValue(Config::DEFAULT_COMBAT_SOUND_VOLUME);
  miningSoundVolumeSlider->setFixedWidth(120);
  m_eventSoundVolumeSliders["mining_stopped"] = miningSoundVolumeSlider;

  QLabel *miningSoundVolumeValueLabel = new QLabel("70%");
  miningSoundVolumeValueLabel->setStyleSheet(StyleSheet::getLabelStyleSheet());
  miningSoundVolumeValueLabel->setFixedWidth(40);
  m_eventSoundVolumeValueLabels["mining_stopped"] = miningSoundVolumeValueLabel;

  connect(miningSoundVolumeSlider, &QSlider::valueChanged, this,
          [this, miningSoundVolumeValueLabel](int value) {
            Config::instance().setCombatEventSoundVolume("mining_stopped",
                                                         value);
            miningSoundVolumeValueLabel->setText(QString("%1%").arg(value));
          });

  miningSoundVolumeLayout->addWidget(miningSoundVolumeLabel);
  miningSoundVolumeLayout->addWidget(miningSoundVolumeSlider);
  miningSoundVolumeLayout->addWidget(miningSoundVolumeValueLabel);
  miningSoundVolumeLayout->addStretch();
  miningStopLayout->addLayout(miningSoundVolumeLayout);

  miningStopLayout->addStretch();

  combatTabs->addTab(miningStopTab, "Mining Stopped");

  combatSectionLayout->addWidget(combatTabs);

  combatSectionLayout->addWidget(combatTabs);

  // Connect event checkboxes
  auto connectEventCheckbox = [this](const QString &eventType,
                                     QCheckBox *checkbox) {
    connect(
        checkbox, &QCheckBox::toggled, this, [this, eventType](bool checked) {
          bool enable = checked && m_showCombatMessagesCheck->isChecked();

          if (m_eventSuppressFocusedCheckBoxes.contains(eventType)) {
            m_eventSuppressFocusedCheckBoxes[eventType]->setEnabled(enable);
          }
          if (m_eventColorLabels.contains(eventType)) {
            m_eventColorLabels[eventType]->setEnabled(enable);
          }
          if (m_eventColorButtons.contains(eventType)) {
            m_eventColorButtons[eventType]->setEnabled(enable);
          }
          if (m_eventDurationLabels.contains(eventType)) {
            m_eventDurationLabels[eventType]->setEnabled(enable);
          }
          if (m_eventDurationSpins.contains(eventType)) {
            m_eventDurationSpins[eventType]->setEnabled(enable);
          }
          if (m_eventBorderCheckBoxes.contains(eventType)) {
            m_eventBorderCheckBoxes[eventType]->setEnabled(enable);
          }
          if (m_eventBorderStyleLabels.contains(eventType)) {
            bool borderEnabled =
                enable && m_eventBorderCheckBoxes[eventType]->isChecked();
            m_eventBorderStyleLabels[eventType]->setEnabled(borderEnabled);
          }
          if (m_eventBorderStyleCombos.contains(eventType)) {
            bool borderEnabled =
                enable && m_eventBorderCheckBoxes[eventType]->isChecked();
            m_eventBorderStyleCombos[eventType]->setEnabled(borderEnabled);
          }
          if (m_eventSoundCheckBoxes.contains(eventType)) {
            m_eventSoundCheckBoxes[eventType]->setEnabled(enable);
          }
          if (m_eventSoundFileLabels.contains(eventType)) {
            bool soundEnabled =
                enable && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundFileLabels[eventType]->setEnabled(soundEnabled);
          }
          if (m_eventSoundFileButtons.contains(eventType)) {
            bool soundEnabled =
                enable && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundFileButtons[eventType]->setEnabled(soundEnabled);
          }
          if (m_eventSoundPlayButtons.contains(eventType)) {
            bool soundEnabled =
                enable && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundPlayButtons[eventType]->setEnabled(soundEnabled);
          }
          if (m_eventSoundVolumeLabels.contains(eventType)) {
            bool soundEnabled =
                enable && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundVolumeLabels[eventType]->setEnabled(soundEnabled);
          }
          if (m_eventSoundVolumeSliders.contains(eventType)) {
            bool soundEnabled =
                enable && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundVolumeSliders[eventType]->setEnabled(soundEnabled);
          }
          if (m_eventSoundVolumeValueLabels.contains(eventType)) {
            bool soundEnabled =
                enable && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundVolumeValueLabels[eventType]->setEnabled(soundEnabled);
          }
        });
  };

  connectEventCheckbox("fleet_invite", m_combatEventFleetInviteCheck);
  connectEventCheckbox("follow_warp", m_combatEventFollowWarpCheck);
  connectEventCheckbox("regroup", m_combatEventRegroupCheck);
  connectEventCheckbox("convo_request", m_combatEventConvoRequestCheck);
  connectEventCheckbox("compression", m_combatEventCompressionCheck);
  connectEventCheckbox("decloak", m_combatEventDecloakCheck);
  connectEventCheckbox("crystal_broke", m_combatEventCrystalBrokeCheck);
  connectEventCheckbox("mining_stopped", m_combatEventMiningStopCheck);

  connect(m_combatEventMiningStopCheck, &QCheckBox::toggled, this,
          [this](bool checked) {
            bool enable = checked && m_showCombatMessagesCheck->isChecked();
            m_miningTimeoutSpin->setEnabled(enable);
            m_miningTimeoutLabel->setEnabled(enable);
          });

  connect(
      m_showCombatMessagesCheck, &QCheckBox::toggled, this,
      [this](bool checked) {
        m_combatMessagePositionCombo->setEnabled(checked);
        m_combatMessagePositionLabel->setEnabled(checked);
        m_combatMessageFontButton->setEnabled(checked);
        m_combatMessageFontLabel->setEnabled(checked);
        m_combatMessageOffsetXLabel->setEnabled(checked);
        m_combatMessageOffsetXSlider->setEnabled(checked);
        m_combatMessageOffsetXValue->setEnabled(checked);
        m_combatMessageOffsetYLabel->setEnabled(checked);
        m_combatMessageOffsetYSlider->setEnabled(checked);
        m_combatMessageOffsetYValue->setEnabled(checked);
        m_combatEventFleetInviteCheck->setEnabled(checked);
        m_combatEventFollowWarpCheck->setEnabled(checked);
        m_combatEventRegroupCheck->setEnabled(checked);
        m_combatEventCompressionCheck->setEnabled(checked);
        m_combatEventDecloakCheck->setEnabled(checked);
        m_combatEventCrystalBrokeCheck->setEnabled(checked);
        m_combatEventConvoRequestCheck->setEnabled(checked);
        m_combatEventMiningStopCheck->setEnabled(checked);

        bool miningStopChecked = m_combatEventMiningStopCheck->isChecked();
        m_miningTimeoutSpin->setEnabled(checked && miningStopChecked);
        m_miningTimeoutLabel->setEnabled(checked && miningStopChecked);

        QMap<QString, QCheckBox *> eventCheckboxes = {
            {"fleet_invite", m_combatEventFleetInviteCheck},
            {"follow_warp", m_combatEventFollowWarpCheck},
            {"regroup", m_combatEventRegroupCheck},
            {"compression", m_combatEventCompressionCheck},
            {"decloak", m_combatEventDecloakCheck},
            {"crystal_broke", m_combatEventCrystalBrokeCheck},
            {"convo_request", m_combatEventConvoRequestCheck},
            {"mining_stopped", m_combatEventMiningStopCheck}};

        for (auto it = eventCheckboxes.constBegin();
             it != eventCheckboxes.constEnd(); ++it) {
          QString eventType = it.key();
          bool eventEnabled = checked && it.value()->isChecked();

          if (m_eventSuppressFocusedCheckBoxes.contains(eventType)) {
            m_eventSuppressFocusedCheckBoxes[eventType]->setEnabled(
                eventEnabled);
          }
          if (m_eventColorLabels.contains(eventType)) {
            m_eventColorLabels[eventType]->setEnabled(eventEnabled);
          }
          if (m_eventColorButtons.contains(eventType)) {
            m_eventColorButtons[eventType]->setEnabled(eventEnabled);
          }
          if (m_eventDurationLabels.contains(eventType)) {
            m_eventDurationLabels[eventType]->setEnabled(eventEnabled);
          }
          if (m_eventDurationSpins.contains(eventType)) {
            m_eventDurationSpins[eventType]->setEnabled(eventEnabled);
          }
          if (m_eventBorderCheckBoxes.contains(eventType)) {
            m_eventBorderCheckBoxes[eventType]->setEnabled(eventEnabled);
          }
          if (m_eventBorderStyleLabels.contains(eventType)) {
            bool borderEnabled =
                eventEnabled && m_eventBorderCheckBoxes[eventType]->isChecked();
            m_eventBorderStyleLabels[eventType]->setEnabled(borderEnabled);
          }
          if (m_eventBorderStyleCombos.contains(eventType)) {
            bool borderEnabled =
                eventEnabled && m_eventBorderCheckBoxes[eventType]->isChecked();
            m_eventBorderStyleCombos[eventType]->setEnabled(borderEnabled);
          }
          if (m_eventSoundCheckBoxes.contains(eventType)) {
            m_eventSoundCheckBoxes[eventType]->setEnabled(eventEnabled);
          }
          if (m_eventSoundFileLabels.contains(eventType)) {
            bool soundEnabled =
                eventEnabled && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundFileLabels[eventType]->setEnabled(soundEnabled);
          }
          if (m_eventSoundFileButtons.contains(eventType)) {
            bool soundEnabled =
                eventEnabled && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundFileButtons[eventType]->setEnabled(soundEnabled);
          }
          if (m_eventSoundPlayButtons.contains(eventType)) {
            bool soundEnabled =
                eventEnabled && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundPlayButtons[eventType]->setEnabled(soundEnabled);
          }
          if (m_eventSoundVolumeLabels.contains(eventType)) {
            bool soundEnabled =
                eventEnabled && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundVolumeLabels[eventType]->setEnabled(soundEnabled);
          }
          if (m_eventSoundVolumeSliders.contains(eventType)) {
            bool soundEnabled =
                eventEnabled && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundVolumeSliders[eventType]->setEnabled(soundEnabled);
          }
          if (m_eventSoundVolumeValueLabels.contains(eventType)) {
            bool soundEnabled =
                eventEnabled && m_eventSoundCheckBoxes[eventType]->isChecked();
            m_eventSoundVolumeValueLabels[eventType]->setEnabled(soundEnabled);
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
  layout->setSpacing(10);
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
      new QLabel("Import settings from EVE-O/X Preview configuration file. "
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
  m_legacySettingsLayout->setSpacing(10);
  m_legacySettingsLayout->setContentsMargins(0, 0, 0, 0);

  layout->addWidget(m_legacySettingsContainer);

  layout->addStretch();

  scrollArea->setWidget(scrollWidget);
  m_stackedWidget->addWidget(scrollArea);
}

void ConfigDialog::createAboutPage() {
  QWidget *page = new QWidget();
  QScrollArea *scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet(StyleSheet::getScrollAreaStyleSheet());

  QWidget *scrollWidget = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(scrollWidget);
  layout->setContentsMargins(0, 0, 5, 0);
  layout->setSpacing(10);

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

  layout->addSpacing(10);

  QWidget *aboutSection = new QWidget();
  aboutSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *aboutSectionLayout = new QVBoxLayout(aboutSection);
  aboutSectionLayout->setContentsMargins(16, 12, 16, 12);
  aboutSectionLayout->setSpacing(10);

  QLabel *aboutHeader = new QLabel("About");
  aboutHeader->setStyleSheet(StyleSheet::getSubsectionHeaderStyleSheet());
  aboutSectionLayout->addWidget(aboutHeader);

  QHBoxLayout *aboutLayout = new QHBoxLayout();

  QLabel *aboutInfoLabel = new QLabel(
      "EVE-APM Preview is developed and maintained by Mr Majestic, as a"
      " modern alternative to the original EVE-O Preview and EVE-X Preview "
      "tools.");
  aboutInfoLabel->setStyleSheet(StyleSheet::getFeatureLabelStyleSheet());
  aboutInfoLabel->setWordWrap(true);
  aboutLayout->addWidget(aboutInfoLabel, 1);

  QPushButton *githubButton = new QPushButton("GitHub Repository");
  githubButton->setStyleSheet(StyleSheet::getButtonStyleSheet());
  githubButton->setCursor(Qt::PointingHandCursor);
  githubButton->setFixedSize(160, 32);
  connect(githubButton, &QPushButton::clicked, []() {
    QDesktopServices::openUrl(
        QUrl("https://github.com/mrmjstc/eve-apm-preview"));
  });
  aboutLayout->addWidget(githubButton);

  aboutSectionLayout->addLayout(aboutLayout);

  layout->addWidget(aboutSection);

  QWidget *updateSection = new QWidget();
  updateSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *updateSectionLayout = new QVBoxLayout(updateSection);
  updateSectionLayout->setContentsMargins(16, 12, 16, 12);
  updateSectionLayout->setSpacing(10);

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

  QWidget *thanksSection = new QWidget();
  thanksSection->setStyleSheet(StyleSheet::getSectionStyleSheet());
  QVBoxLayout *thanksSectionLayout = new QVBoxLayout(thanksSection);
  thanksSectionLayout->setContentsMargins(16, 12, 16, 12);
  thanksSectionLayout->setSpacing(10);

  QLabel *thanksHeader = new QLabel("Thanks");
  thanksHeader->setStyleSheet(StyleSheet::getSubsectionHeaderStyleSheet());
  thanksSectionLayout->addWidget(thanksHeader);

  QLabel *thanksInfoLabel = new QLabel(
      "Thank you to everyone that submitted a bug or feature request,"
      " even if I forgot to you to the list.");
  thanksInfoLabel->setStyleSheet(StyleSheet::getInfoLabelStyleSheet());
  thanksInfoLabel->setWordWrap(true);
  thanksSectionLayout->addWidget(thanksInfoLabel);

  QGridLayout *thanksGridLayout = new QGridLayout();
  thanksGridLayout->setSpacing(10);

  QStringList thanksList = {
      "The Aggressor",    "Exie",         "Hyperion Iwaira",  "Zintage Enaka",
      "snipereagle1",     "degeva",       "Killer 641",       "Aulis",
      "Cyanide",          "Oebrun",       "Kondo Rio Sotken", "Zack Power",
      "Langanmyer Nor",   "ham Norris",   "Groot Brustir",    "The Llama",
      "Rhazien Shardani", "Anthony Mong", "LazyBong"};

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

  scrollArea->setWidget(scrollWidget);

  QVBoxLayout *pageLayout = new QVBoxLayout(page);
  pageLayout->setContentsMargins(0, 0, 0, 0);
  pageLayout->addWidget(scrollArea);

  m_stackedWidget->addWidget(page);
}

void ConfigDialog::setupBindings() {
  Config &config = Config::instance();

  m_bindingManager.clear();

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_alwaysOnTopCheck, [&config]() { return config.alwaysOnTop(); },
      [&config](bool value) { config.setAlwaysOnTop(value); }, true));

  m_bindingManager.addBinding(BindingHelpers::bindComboBox(
      m_switchModeCombo,
      [&config]() { return config.switchOnMouseDown() ? 1 : 0; },
      [&config](int value) { config.setSwitchOnMouseDown(value == 1); }, 0));

  m_bindingManager.addBinding(BindingHelpers::bindComboBox(
      m_dragButtonCombo,
      [&config]() { return config.useDragWithRightClick() ? 1 : 0; },
      [&config](int value) { config.setUseDragWithRightClick(value == 1); },
      1));

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

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_hideThumbnailsWhenEVENotFocusedCheck,
      [&config]() { return config.hideThumbnailsWhenEVENotFocused(); },
      [&config](bool value) {
        config.setHideThumbnailsWhenEVENotFocused(value);
      },
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

  m_bindingManager.addBinding(BindingHelpers::bindComboBox(
      m_activeBorderStyleCombo,
      [&config]() { return static_cast<int>(config.activeBorderStyle()); },
      [&config](int value) {
        config.setActiveBorderStyle(static_cast<BorderStyle>(value));
      },
      static_cast<int>(BorderStyle::Solid)));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_showInactiveBordersCheck,
      [&config]() { return config.showInactiveBorders(); },
      [&config](bool value) { config.setShowInactiveBorders(value); }, false));

  auto inactiveBorderColorBinding = BindingHelpers::bindColorButton(
      m_inactiveBorderColorButton,
      [&config]() { return config.inactiveBorderColor(); },
      [&config](const QColor &color) { config.setInactiveBorderColor(color); },
      QColor(128, 128, 128),
      [this](QPushButton *btn, const QColor &color) {
        m_inactiveBorderColor = color;
        updateColorButton(btn, color);
      });
  m_bindingManager.addBinding(std::move(inactiveBorderColorBinding));

  m_bindingManager.addBinding(BindingHelpers::bindSpinBox(
      m_inactiveBorderWidthSpin,
      [&config]() { return config.inactiveBorderWidth(); },
      [&config](int value) { config.setInactiveBorderWidth(value); }, 2));

  m_bindingManager.addBinding(BindingHelpers::bindComboBox(
      m_inactiveBorderStyleCombo,
      [&config]() { return static_cast<int>(config.inactiveBorderStyle()); },
      [&config](int value) {
        config.setInactiveBorderStyle(static_cast<BorderStyle>(value));
      },
      static_cast<int>(BorderStyle::Solid)));

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

  m_bindingManager.addBinding(BindingHelpers::bindSlider(
      m_characterNameOffsetXSlider,
      [&config]() { return config.characterNameOffsetX(); },
      [&config](int value) { config.setCharacterNameOffsetX(value); }, 0));

  m_bindingManager.addBinding(BindingHelpers::bindSlider(
      m_characterNameOffsetYSlider,
      [&config]() { return config.characterNameOffsetY(); },
      [&config](int value) { config.setCharacterNameOffsetY(value); }, 0));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_showSystemNameCheck, [&config]() { return config.showSystemName(); },
      [&config](bool value) { config.setShowSystemName(value); }, true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_uniqueSystemColorsCheck,
      [&config]() { return config.useUniqueSystemNameColors(); },
      [&config](bool value) { config.setUseUniqueSystemNameColors(value); },
      false));

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

  m_bindingManager.addBinding(BindingHelpers::bindSlider(
      m_systemNameOffsetXSlider,
      [&config]() { return config.systemNameOffsetX(); },
      [&config](int value) { config.setSystemNameOffsetX(value); }, 0));

  m_bindingManager.addBinding(BindingHelpers::bindSlider(
      m_systemNameOffsetYSlider,
      [&config]() { return config.systemNameOffsetY(); },
      [&config](int value) { config.setSystemNameOffsetY(value); }, 0));

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

  HotkeyManager *hotkeyMgr = HotkeyManager::instance();
  if (hotkeyMgr) {
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

  m_bindingManager.addBinding(BindingHelpers::bindComboBox(
      m_combatMessagePositionCombo,
      [&config]() { return config.combatMessagePosition(); },
      [&config](int value) { config.setCombatMessagePosition(value); }, 6));

  m_bindingManager.addBinding(BindingHelpers::bindSlider(
      m_combatMessageOffsetXSlider,
      [&config]() { return config.combatMessageOffsetX(); },
      [&config](int value) { config.setCombatMessageOffsetX(value); }, 0));

  m_bindingManager.addBinding(BindingHelpers::bindSlider(
      m_combatMessageOffsetYSlider,
      [&config]() { return config.combatMessageOffsetY(); },
      [&config](int value) { config.setCombatMessageOffsetY(value); }, 0));

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
      m_combatEventDecloakCheck,
      [&config]() { return config.isCombatEventTypeEnabled("decloak"); },
      [&config](bool value) {
        QStringList types = config.enabledCombatEventTypes();
        if (value && !types.contains("decloak")) {
          types << "decloak";
          config.setEnabledCombatEventTypes(types);
        } else if (!value) {
          types.removeAll("decloak");
          config.setEnabledCombatEventTypes(types);
        }
      },
      true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_combatEventCrystalBrokeCheck,
      [&config]() { return config.isCombatEventTypeEnabled("crystal_broke"); },
      [&config](bool value) {
        QStringList types = config.enabledCombatEventTypes();
        if (value && !types.contains("crystal_broke")) {
          types << "crystal_broke";
          config.setEnabledCombatEventTypes(types);
        } else if (!value) {
          types.removeAll("crystal_broke");
          config.setEnabledCombatEventTypes(types);
        }
      },
      true));

  m_bindingManager.addBinding(BindingHelpers::bindCheckBox(
      m_combatEventConvoRequestCheck,
      [&config]() { return config.isCombatEventTypeEnabled("convo_request"); },
      [&config](bool value) {
        QStringList types = config.enabledCombatEventTypes();
        if (value && !types.contains("convo_request")) {
          types << "convo_request";
          config.setEnabledCombatEventTypes(types);
        } else if (!value) {
          types.removeAll("convo_request");
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

  HotkeyManager *hotkeyMgr = HotkeyManager::instance();
  if (hotkeyMgr) {
    QVector<HotkeyBinding> suspendBindings = hotkeyMgr->getSuspendHotkeys();
    if (!suspendBindings.isEmpty()) {
      QVector<HotkeyCombination> suspendCombos;
      for (const HotkeyBinding &binding : suspendBindings) {
        suspendCombos.append(HotkeyCombination(
            binding.keyCode, binding.getModifiers() & MOD_CONTROL,
            binding.getModifiers() & MOD_ALT,
            binding.getModifiers() & MOD_SHIFT));
      }
      m_suspendHotkeyCapture->setHotkeys(suspendCombos);
    } else {
      m_suspendHotkeyCapture->clearHotkey();
    }

    QVector<HotkeyBinding> closeAllBindings =
        hotkeyMgr->getCloseAllClientsHotkeys();
    if (!closeAllBindings.isEmpty()) {
      QVector<HotkeyCombination> closeAllCombos;
      for (const HotkeyBinding &binding : closeAllBindings) {
        closeAllCombos.append(HotkeyCombination(
            binding.keyCode, binding.getModifiers() & MOD_CONTROL,
            binding.getModifiers() & MOD_ALT,
            binding.getModifiers() & MOD_SHIFT));
      }
      m_closeAllClientsCapture->setHotkeys(closeAllCombos);
    } else {
      m_closeAllClientsCapture->clearHotkey();
    }

    QVector<HotkeyBinding> minimizeAllBindings =
        hotkeyMgr->getMinimizeAllClientsHotkeys();
    if (!minimizeAllBindings.isEmpty()) {
      QVector<HotkeyCombination> minimizeAllCombos;
      for (const HotkeyBinding &binding : minimizeAllBindings) {
        minimizeAllCombos.append(HotkeyCombination(
            binding.keyCode, binding.getModifiers() & MOD_CONTROL,
            binding.getModifiers() & MOD_ALT,
            binding.getModifiers() & MOD_SHIFT));
      }
      m_minimizeAllClientsCapture->setHotkeys(minimizeAllCombos);
    } else {
      m_minimizeAllClientsCapture->clearHotkey();
    }

    QVector<HotkeyBinding> toggleThumbnailsBindings =
        hotkeyMgr->getToggleThumbnailsVisibilityHotkeys();
    if (!toggleThumbnailsBindings.isEmpty()) {
      QVector<HotkeyCombination> toggleThumbnailsCombos;
      for (const HotkeyBinding &binding : toggleThumbnailsBindings) {
        toggleThumbnailsCombos.append(HotkeyCombination(
            binding.keyCode, binding.getModifiers() & MOD_CONTROL,
            binding.getModifiers() & MOD_ALT,
            binding.getModifiers() & MOD_SHIFT));
      }
      m_toggleThumbnailsVisibilityCapture->setHotkeys(toggleThumbnailsCombos);
    } else {
      m_toggleThumbnailsVisibilityCapture->clearHotkey();
    }

    QVector<HotkeyBinding> cycleProfileFwdBindings =
        hotkeyMgr->getCycleProfileForwardHotkeys();
    if (!cycleProfileFwdBindings.isEmpty()) {
      QVector<HotkeyCombination> cycleProfileFwdCombos;
      for (const HotkeyBinding &binding : cycleProfileFwdBindings) {
        cycleProfileFwdCombos.append(HotkeyCombination(
            binding.keyCode, binding.getModifiers() & MOD_CONTROL,
            binding.getModifiers() & MOD_ALT,
            binding.getModifiers() & MOD_SHIFT));
      }
      m_cycleProfileForwardCapture->setHotkeys(cycleProfileFwdCombos);
    } else {
      m_cycleProfileForwardCapture->clearHotkey();
    }

    QVector<HotkeyBinding> cycleProfileBwdBindings =
        hotkeyMgr->getCycleProfileBackwardHotkeys();
    if (!cycleProfileBwdBindings.isEmpty()) {
      QVector<HotkeyCombination> cycleProfileBwdCombos;
      for (const HotkeyBinding &binding : cycleProfileBwdBindings) {
        cycleProfileBwdCombos.append(HotkeyCombination(
            binding.keyCode, binding.getModifiers() & MOD_CONTROL,
            binding.getModifiers() & MOD_ALT,
            binding.getModifiers() & MOD_SHIFT));
      }
      m_cycleProfileBackwardCapture->setHotkeys(cycleProfileBwdCombos);
    } else {
      m_cycleProfileBackwardCapture->clearHotkey();
    }

    QVector<HotkeyBinding> notLoggedInFwdBindings =
        hotkeyMgr->getNotLoggedInForwardHotkeys();
    if (!notLoggedInFwdBindings.isEmpty()) {
      QVector<HotkeyCombination> notLoggedInFwdCombos;
      for (const HotkeyBinding &binding : notLoggedInFwdBindings) {
        notLoggedInFwdCombos.append(HotkeyCombination(
            binding.keyCode, binding.getModifiers() & MOD_CONTROL,
            binding.getModifiers() & MOD_ALT,
            binding.getModifiers() & MOD_SHIFT));
      }
      m_notLoggedInForwardCapture->setHotkeys(notLoggedInFwdCombos);
    } else {
      m_notLoggedInForwardCapture->clearHotkey();
    }

    QVector<HotkeyBinding> notLoggedInBwdBindings =
        hotkeyMgr->getNotLoggedInBackwardHotkeys();
    if (!notLoggedInBwdBindings.isEmpty()) {
      QVector<HotkeyCombination> notLoggedInBwdCombos;
      for (const HotkeyBinding &binding : notLoggedInBwdBindings) {
        notLoggedInBwdCombos.append(HotkeyCombination(
            binding.keyCode, binding.getModifiers() & MOD_CONTROL,
            binding.getModifiers() & MOD_ALT,
            binding.getModifiers() & MOD_SHIFT));
      }
      m_notLoggedInBackwardCapture->setHotkeys(notLoggedInBwdCombos);
    } else {
      m_notLoggedInBackwardCapture->clearHotkey();
    }

    QVector<HotkeyBinding> nonEVEFwdBindings =
        hotkeyMgr->getNonEVEForwardHotkeys();
    if (!nonEVEFwdBindings.isEmpty()) {
      QVector<HotkeyCombination> nonEVEFwdCombos;
      for (const HotkeyBinding &binding : nonEVEFwdBindings) {
        nonEVEFwdCombos.append(HotkeyCombination(
            binding.keyCode, binding.getModifiers() & MOD_CONTROL,
            binding.getModifiers() & MOD_ALT,
            binding.getModifiers() & MOD_SHIFT));
      }
      m_nonEVEForwardCapture->setHotkeys(nonEVEFwdCombos);
    } else {
      m_nonEVEForwardCapture->clearHotkey();
    }

    QVector<HotkeyBinding> nonEVEBwdBindings =
        hotkeyMgr->getNonEVEBackwardHotkeys();
    if (!nonEVEBwdBindings.isEmpty()) {
      QVector<HotkeyCombination> nonEVEBwdCombos;
      for (const HotkeyBinding &binding : nonEVEBwdBindings) {
        nonEVEBwdCombos.append(HotkeyCombination(
            binding.keyCode, binding.getModifiers() & MOD_CONTROL,
            binding.getModifiers() & MOD_ALT,
            binding.getModifiers() & MOD_SHIFT));
      }
      m_nonEVEBackwardCapture->setHotkeys(nonEVEBwdCombos);
    } else {
      m_nonEVEBackwardCapture->clearHotkey();
    }
  }

  m_chatLogDirectoryEdit->setText(config.chatLogDirectoryRaw());

  m_gameLogDirectoryEdit->setText(config.gameLogDirectoryRaw());

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

  for (auto it = m_eventSuppressFocusedCheckBoxes.constBegin();
       it != m_eventSuppressFocusedCheckBoxes.constEnd(); ++it) {
    QString eventType = it.key();
    QCheckBox *suppressCheck = it.value();
    suppressCheck->setChecked(config.combatEventSuppressFocused(eventType));
  }

  for (auto it = m_eventBorderStyleCombos.constBegin();
       it != m_eventBorderStyleCombos.constEnd(); ++it) {
    QString eventType = it.key();
    QComboBox *styleCombo = it.value();
    BorderStyle style = config.combatBorderStyle(eventType);
    int index = styleCombo->findData(static_cast<int>(style));
    if (index >= 0) {
      styleCombo->setCurrentIndex(index);
    }
    if (m_eventBorderCheckBoxes.contains(eventType)) {
      styleCombo->setEnabled(m_eventBorderCheckBoxes[eventType]->isChecked());
    }
  }

  // Load sound settings
  for (auto it = m_eventSoundCheckBoxes.constBegin();
       it != m_eventSoundCheckBoxes.constEnd(); ++it) {
    QString eventType = it.key();
    QCheckBox *soundCheck = it.value();
    bool soundEnabled = config.combatEventSoundEnabled(eventType);
    soundCheck->setChecked(soundEnabled);

    // Update button text if sound file is set
    if (m_eventSoundFileButtons.contains(eventType)) {
      QString soundFile = config.combatEventSoundFile(eventType);
      if (!soundFile.isEmpty()) {
        QFileInfo fileInfo(soundFile);
        m_eventSoundFileButtons[eventType]->setText(fileInfo.fileName());
      }
      m_eventSoundFileButtons[eventType]->setEnabled(soundEnabled);
    }

    // Update volume slider and label
    if (m_eventSoundVolumeSliders.contains(eventType)) {
      int volume = config.combatEventSoundVolume(eventType);
      m_eventSoundVolumeSliders[eventType]->setValue(volume);
      m_eventSoundVolumeSliders[eventType]->setEnabled(soundEnabled);
    }
    if (m_eventSoundVolumeValueLabels.contains(eventType)) {
      int volume = config.combatEventSoundVolume(eventType);
      m_eventSoundVolumeValueLabels[eventType]->setText(
          QString("%1%").arg(volume));
      m_eventSoundVolumeValueLabels[eventType]->setEnabled(soundEnabled);
    }
    if (m_eventSoundFileLabels.contains(eventType)) {
      m_eventSoundFileLabels[eventType]->setEnabled(soundEnabled);
    }
    if (m_eventSoundVolumeLabels.contains(eventType)) {
      m_eventSoundVolumeLabels[eventType]->setEnabled(soundEnabled);
    }
    if (m_eventSoundPlayButtons.contains(eventType)) {
      m_eventSoundPlayButtons[eventType]->setEnabled(soundEnabled);
    }
  }

  BorderStyle activeStyle = config.activeBorderStyle();
  int activeIndex =
      m_activeBorderStyleCombo->findData(static_cast<int>(activeStyle));
  if (activeIndex >= 0) {
    m_activeBorderStyleCombo->setCurrentIndex(activeIndex);
  }

  m_snapDistanceLabel->setEnabled(config.enableSnapping());
  m_snapDistanceSpin->setEnabled(config.enableSnapping());
  m_minimizeDelayLabel->setEnabled(config.minimizeInactiveClients());
  m_minimizeDelaySpin->setEnabled(config.minimizeInactiveClients());
  m_neverMinimizeLabel->setStyleSheet(
      StyleSheet::getSectionSubHeaderStyleSheet() + " margin-top: 10px;");
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
  m_uniqueSystemColorsCheck->setEnabled(config.showSystemName());
  m_systemNameColorLabel->setEnabled(config.showSystemName() &&
                                     !config.useUniqueSystemNameColors());
  m_systemNameColorButton->setEnabled(config.showSystemName() &&
                                      !config.useUniqueSystemNameColors());
  m_systemNamePositionLabel->setEnabled(config.showSystemName());
  m_systemNamePositionCombo->setEnabled(config.showSystemName());
  m_systemNameFontLabel->setEnabled(config.showSystemName());
  m_systemNameFontButton->setEnabled(config.showSystemName());
  m_backgroundColorLabel->setEnabled(config.showOverlayBackground());
  m_backgroundOpacityLabel->setEnabled(config.showOverlayBackground());

  m_neverMinimizeLabel->setEnabled(config.minimizeInactiveClients());
  m_neverMinimizeInfoLabel->setEnabled(config.minimizeInactiveClients());
  m_neverMinimizeScrollArea->setEnabled(config.minimizeInactiveClients());
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

  bool combatMessagesEnabled = config.showCombatMessages();
  m_combatMessagePositionCombo->setEnabled(combatMessagesEnabled);
  m_combatMessagePositionLabel->setEnabled(combatMessagesEnabled);
  m_combatMessageFontButton->setEnabled(combatMessagesEnabled);
  m_combatMessageFontLabel->setEnabled(combatMessagesEnabled);
  m_combatEventFleetInviteCheck->setEnabled(combatMessagesEnabled);
  m_combatEventFollowWarpCheck->setEnabled(combatMessagesEnabled);
  m_combatEventRegroupCheck->setEnabled(combatMessagesEnabled);
  m_combatEventCompressionCheck->setEnabled(combatMessagesEnabled);
  m_combatEventConvoRequestCheck->setEnabled(combatMessagesEnabled);
  m_combatEventMiningStopCheck->setEnabled(combatMessagesEnabled);

  bool miningStopChecked = m_combatEventMiningStopCheck->isChecked();
  m_miningTimeoutSpin->setEnabled(combatMessagesEnabled && miningStopChecked);
  m_miningTimeoutLabel->setEnabled(combatMessagesEnabled && miningStopChecked);

  QMap<QString, QCheckBox *> eventCheckboxes = {
      {"fleet_invite", m_combatEventFleetInviteCheck},
      {"follow_warp", m_combatEventFollowWarpCheck},
      {"regroup", m_combatEventRegroupCheck},
      {"compression", m_combatEventCompressionCheck},
      {"convo_request", m_combatEventConvoRequestCheck},
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

  while (m_thumbnailSizesLayout->count() > 1) {
    QLayoutItem *item = m_thumbnailSizesLayout->takeAt(0);
    if (item->widget()) {
      QWidget *widget = item->widget();
      widget->setParent(nullptr);
      delete widget;
    }
    delete item;
  }

  QHash<QString, QSize> customSizes = config.getAllCustomThumbnailSizes();
  for (auto it = customSizes.constBegin(); it != customSizes.constEnd(); ++it) {
    QWidget *formRow = createThumbnailSizeFormRow(it.key(), it.value().width(),
                                                  it.value().height());
    int count = m_thumbnailSizesLayout->count();
    m_thumbnailSizesLayout->insertWidget(count - 1, formRow);
  }

  if (customSizes.isEmpty()) {
    QWidget *formRow = createThumbnailSizeFormRow();
    int count = m_thumbnailSizesLayout->count();
    m_thumbnailSizesLayout->insertWidget(count - 1, formRow);
  }

  updateThumbnailSizesScrollHeight();

  while (m_processThumbnailSizesLayout->count() > 1) {
    QLayoutItem *item = m_processThumbnailSizesLayout->takeAt(0);
    if (item->widget()) {
      QWidget *widget = item->widget();
      widget->setParent(nullptr);
      delete widget;
    }
    delete item;
  }

  QHash<QString, QSize> customProcessSizes =
      config.getAllCustomProcessThumbnailSizes();
  for (auto it = customProcessSizes.constBegin();
       it != customProcessSizes.constEnd(); ++it) {
    QWidget *formRow = createProcessThumbnailSizeFormRow(
        it.key(), it.value().width(), it.value().height());
    int count = m_processThumbnailSizesLayout->count();
    m_processThumbnailSizesLayout->insertWidget(count - 1, formRow);
  }

  if (customProcessSizes.isEmpty()) {
    QWidget *formRow = createProcessThumbnailSizeFormRow();
    int count = m_processThumbnailSizesLayout->count();
    m_processThumbnailSizesLayout->insertWidget(count - 1, formRow);
  }

  updateProcessThumbnailSizesScrollHeight();

  while (m_customNamesLayout->count() > 1) {
    QLayoutItem *item = m_customNamesLayout->takeAt(0);
    if (item->widget()) {
      QWidget *widget = item->widget();
      widget->setParent(nullptr);
      delete widget;
    }
    delete item;
  }

  QHash<QString, QString> customNames = config.getAllCustomThumbnailNames();
  for (auto it = customNames.constBegin(); it != customNames.constEnd(); ++it) {
    QWidget *formRow = createCustomNameFormRow(it.key(), it.value());
    int count = m_customNamesLayout->count();
    m_customNamesLayout->insertWidget(count - 1, formRow);
  }

  if (customNames.isEmpty()) {
    QWidget *formRow = createCustomNameFormRow();
    int count = m_customNamesLayout->count();
    m_customNamesLayout->insertWidget(count - 1, formRow);
  }

  updateCustomNamesScrollHeight();

  while (m_characterHotkeysLayout->count() > 1) {
    QLayoutItem *item = m_characterHotkeysLayout->takeAt(0);
    if (item->widget()) {
      QWidget *widget = item->widget();
      widget->setParent(nullptr);
      delete widget;
    }
    delete item;
  }

  if (hotkeyMgr) {
    QHash<QString, QVector<HotkeyBinding>> characterMultiHotkeys =
        hotkeyMgr->getAllCharacterMultiHotkeys();
    for (auto it = characterMultiHotkeys.constBegin();
         it != characterMultiHotkeys.constEnd(); ++it) {
      const QVector<HotkeyBinding> &bindings = it.value();

      QWidget *formRow;
      if (!bindings.isEmpty()) {
        formRow =
            createCharacterHotkeyFormRow(it.key(), bindings.first().keyCode,
                                         bindings.first().getModifiers());

        if (bindings.size() > 1) {
          HotkeyCapture *capture = formRow->findChild<HotkeyCapture *>();
          if (capture) {
            QVector<HotkeyCombination> combos;
            for (const HotkeyBinding &binding : bindings) {
              combos.append(HotkeyCombination(
                  binding.keyCode, binding.getModifiers() & MOD_CONTROL,
                  binding.getModifiers() & MOD_ALT,
                  binding.getModifiers() & MOD_SHIFT));
            }
            capture->setHotkeys(combos);
          }
        }
      } else {
        formRow = createCharacterHotkeyFormRow(it.key());
      }

      int count = m_characterHotkeysLayout->count();
      m_characterHotkeysLayout->insertWidget(count - 1, formRow);
    }

    if (characterMultiHotkeys.isEmpty()) {
      QWidget *formRow = createCharacterHotkeyFormRow();
      int count = m_characterHotkeysLayout->count();
      m_characterHotkeysLayout->insertWidget(count - 1, formRow);
    }

    updateCharacterHotkeysScrollHeight();
  }

  while (m_cycleGroupsLayout->count() > 1) {
    QLayoutItem *item = m_cycleGroupsLayout->takeAt(0);
    if (item->widget()) {
      QWidget *widget = item->widget();
      widget->setParent(nullptr);
      delete widget;
    }
    delete item;
  }

  if (hotkeyMgr) {
    QHash<QString, CycleGroup> cycleGroups = hotkeyMgr->getAllCycleGroups();
    for (auto it = cycleGroups.constBegin(); it != cycleGroups.constEnd();
         ++it) {
      const CycleGroup &group = it.value();
      QString characters = group.characterNames.join(", ");

      int backwardKey = group.backwardBindings.isEmpty()
                            ? 0
                            : group.backwardBindings.first().keyCode;
      int backwardMods = group.backwardBindings.isEmpty()
                             ? 0
                             : group.backwardBindings.first().getModifiers();
      int forwardKey = group.forwardBindings.isEmpty()
                           ? 0
                           : group.forwardBindings.first().keyCode;
      int forwardMods = group.forwardBindings.isEmpty()
                            ? 0
                            : group.forwardBindings.first().getModifiers();

      QWidget *formRow = createCycleGroupFormRow(
          group.groupName, backwardKey, backwardMods, forwardKey, forwardMods,
          characters, group.includeNotLoggedIn, group.noLoop);

      QList<HotkeyCapture *> captures =
          formRow->findChildren<HotkeyCapture *>();
      if (captures.size() >= 2) {
        HotkeyCapture *backwardCapture = captures[0];
        HotkeyCapture *forwardCapture = captures[1];

        if (group.backwardBindings.size() > 1) {
          QVector<HotkeyCombination> backwardCombos;
          for (const HotkeyBinding &binding : group.backwardBindings) {
            backwardCombos.append(HotkeyCombination(
                binding.keyCode, binding.getModifiers() & MOD_CONTROL,
                binding.getModifiers() & MOD_ALT,
                binding.getModifiers() & MOD_SHIFT));
          }
          backwardCapture->setHotkeys(backwardCombos);
        }

        if (group.forwardBindings.size() > 1) {
          QVector<HotkeyCombination> forwardCombos;
          for (const HotkeyBinding &binding : group.forwardBindings) {
            forwardCombos.append(HotkeyCombination(
                binding.keyCode, binding.getModifiers() & MOD_CONTROL,
                binding.getModifiers() & MOD_ALT,
                binding.getModifiers() & MOD_SHIFT));
          }
          forwardCapture->setHotkeys(forwardCombos);
        }
      }

      int count = m_cycleGroupsLayout->count();
      m_cycleGroupsLayout->insertWidget(count - 1, formRow);
    }

    if (cycleGroups.isEmpty()) {
      QWidget *formRow = createCycleGroupFormRow("Group 1");
      int count = m_cycleGroupsLayout->count();
      m_cycleGroupsLayout->insertWidget(count - 1, formRow);
    }

    updateCycleGroupsScrollHeight();
  }

  while (m_characterColorsLayout->count() > 1) {
    QLayoutItem *item = m_characterColorsLayout->takeAt(0);
    if (item->widget()) {
      QWidget *widget = item->widget();
      widget->setParent(nullptr);
      delete widget;
    }
    delete item;
  }

  QHash<QString, QColor> characterColors = config.getAllCharacterBorderColors();
  for (auto it = characterColors.constBegin(); it != characterColors.constEnd();
       ++it) {
    QString charName = it.key();
    QColor color = it.value();

    QWidget *formRow = createCharacterColorFormRow(charName, color);
    int count = m_characterColorsLayout->count();
    m_characterColorsLayout->insertWidget(count - 1, formRow);
  }

  if (characterColors.isEmpty()) {
    QWidget *formRow = createCharacterColorFormRow();
    int count = m_characterColorsLayout->count();
    m_characterColorsLayout->insertWidget(count - 1, formRow);
  }

  updateCharacterColorsScrollHeight();

  while (m_neverMinimizeLayout->count() > 1) {
    QLayoutItem *item = m_neverMinimizeLayout->takeAt(0);
    if (item->widget()) {
      QWidget *widget = item->widget();
      widget->setParent(nullptr);
      delete widget;
    }
    delete item;
  }

  QStringList neverMinimize = config.neverMinimizeCharacters();
  for (const QString &charName : neverMinimize) {
    QWidget *formRow = createNeverMinimizeFormRow(charName);
    int count = m_neverMinimizeLayout->count();
    m_neverMinimizeLayout->insertWidget(count - 1, formRow);
  }

  if (neverMinimize.isEmpty()) {
    QWidget *formRow = createNeverMinimizeFormRow();
    int count = m_neverMinimizeLayout->count();
    m_neverMinimizeLayout->insertWidget(count - 1, formRow);
  }

  updateNeverMinimizeScrollHeight();

  while (m_neverCloseLayout->count() > 1) {
    QLayoutItem *item = m_neverCloseLayout->takeAt(0);
    if (item->widget()) {
      QWidget *widget = item->widget();
      widget->setParent(nullptr);
      delete widget;
    }
    delete item;
  }

  QStringList neverClose = config.neverCloseCharacters();
  for (const QString &charName : neverClose) {
    QWidget *formRow = createNeverCloseFormRow(charName);
    int count = m_neverCloseLayout->count();
    m_neverCloseLayout->insertWidget(count - 1, formRow);
  }

  if (neverClose.isEmpty()) {
    QWidget *formRow = createNeverCloseFormRow();
    int count = m_neverCloseLayout->count();
    m_neverCloseLayout->insertWidget(count - 1, formRow);
  }

  updateNeverCloseScrollHeight();

  while (m_hiddenCharactersLayout->count() > 1) {
    QLayoutItem *item = m_hiddenCharactersLayout->takeAt(0);
    if (item->widget()) {
      QWidget *widget = item->widget();
      widget->setParent(nullptr);
      delete widget;
    }
    delete item;
  }

  QStringList hiddenChars = config.hiddenCharacters();
  for (const QString &charName : hiddenChars) {
    QWidget *formRow = createHiddenCharactersFormRow(charName);
    int count = m_hiddenCharactersLayout->count();
    m_hiddenCharactersLayout->insertWidget(count - 1, formRow);
  }

  if (hiddenChars.isEmpty()) {
    QWidget *formRow = createHiddenCharactersFormRow();
    int count = m_hiddenCharactersLayout->count();
    m_hiddenCharactersLayout->insertWidget(count - 1, formRow);
  }

  updateHiddenCharactersScrollHeight();

  while (m_processNamesLayout->count() > 1) {
    QLayoutItem *item = m_processNamesLayout->takeAt(0);
    if (item->widget()) {
      QWidget *widget = item->widget();
      widget->setParent(nullptr);
      delete widget;
    }
    delete item;
  }

  QStringList processNames = config.processNames();
  QStringList displayProcessNames;
  for (const QString &processName : processNames) {
    if (processName.compare("exefile.exe", Qt::CaseInsensitive) != 0) {
      displayProcessNames.append(processName);
    }
  }

  for (const QString &processName : displayProcessNames) {
    QWidget *formRow = createProcessNamesFormRow(processName);
    int count = m_processNamesLayout->count();
    m_processNamesLayout->insertWidget(count - 1, formRow);
  }

  if (displayProcessNames.isEmpty()) {
    QWidget *formRow = createProcessNamesFormRow();
    int count = m_processNamesLayout->count();
    m_processNamesLayout->insertWidget(count - 1, formRow);
  }

  updateProcessNamesScrollHeight();

  // Set initial enable/disable states based on loaded config
  bool showInactiveBorders = config.showInactiveBorders();
  m_inactiveBorderColorLabel->setEnabled(showInactiveBorders);
  m_inactiveBorderColorButton->setEnabled(showInactiveBorders);
  m_inactiveBorderWidthLabel->setEnabled(showInactiveBorders);
  m_inactiveBorderWidthSpin->setEnabled(showInactiveBorders);
  m_inactiveBorderStyleLabel->setEnabled(showInactiveBorders);
  m_inactiveBorderStyleCombo->setEnabled(showInactiveBorders);

  bool highlightActive = config.highlightActiveWindow();
  m_highlightColorLabel->setEnabled(highlightActive);
  m_highlightColorButton->setEnabled(highlightActive);
  m_highlightBorderWidthLabel->setEnabled(highlightActive);
  m_highlightBorderWidthSpin->setEnabled(highlightActive);
  m_activeBorderStyleLabel->setEnabled(highlightActive);
  m_activeBorderStyleCombo->setEnabled(highlightActive);
}

void ConfigDialog::saveSettings() {
  m_bindingManager.saveAll();

  HotkeyManager *hotkeyMgr = HotkeyManager::instance();
  if (hotkeyMgr) {
    QVector<HotkeyBinding> suspendBindings;
    QVector<HotkeyCombination> suspendCombos =
        m_suspendHotkeyCapture->getHotkeys();
    for (const HotkeyCombination &combo : suspendCombos) {
      suspendBindings.append(
          HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
    }
    hotkeyMgr->setSuspendHotkeys(suspendBindings);

    QVector<HotkeyBinding> closeAllBindings;
    QVector<HotkeyCombination> closeAllCombos =
        m_closeAllClientsCapture->getHotkeys();
    for (const HotkeyCombination &combo : closeAllCombos) {
      closeAllBindings.append(
          HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
    }
    hotkeyMgr->setCloseAllClientsHotkeys(closeAllBindings);

    QVector<HotkeyBinding> minimizeAllBindings;
    QVector<HotkeyCombination> minimizeAllCombos =
        m_minimizeAllClientsCapture->getHotkeys();
    for (const HotkeyCombination &combo : minimizeAllCombos) {
      minimizeAllBindings.append(
          HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
    }
    hotkeyMgr->setMinimizeAllClientsHotkeys(minimizeAllBindings);

    QVector<HotkeyBinding> toggleThumbnailsBindings;
    QVector<HotkeyCombination> toggleThumbnailsCombos =
        m_toggleThumbnailsVisibilityCapture->getHotkeys();
    for (const HotkeyCombination &combo : toggleThumbnailsCombos) {
      toggleThumbnailsBindings.append(
          HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
    }
    hotkeyMgr->setToggleThumbnailsVisibilityHotkeys(toggleThumbnailsBindings);

    QVector<HotkeyBinding> cycleProfileFwdBindings;
    QVector<HotkeyCombination> cycleProfileFwdCombos =
        m_cycleProfileForwardCapture->getHotkeys();
    for (const HotkeyCombination &combo : cycleProfileFwdCombos) {
      cycleProfileFwdBindings.append(
          HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
    }

    QVector<HotkeyBinding> cycleProfileBwdBindings;
    QVector<HotkeyCombination> cycleProfileBwdCombos =
        m_cycleProfileBackwardCapture->getHotkeys();
    for (const HotkeyCombination &combo : cycleProfileBwdCombos) {
      cycleProfileBwdBindings.append(
          HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
    }
    hotkeyMgr->setCycleProfileHotkeys(cycleProfileFwdBindings,
                                      cycleProfileBwdBindings);

    QVector<HotkeyBinding> notLoggedInFwdBindings;
    QVector<HotkeyCombination> notLoggedInFwdCombos =
        m_notLoggedInForwardCapture->getHotkeys();
    for (const HotkeyCombination &combo : notLoggedInFwdCombos) {
      notLoggedInFwdBindings.append(
          HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
    }

    QVector<HotkeyBinding> notLoggedInBwdBindings;
    QVector<HotkeyCombination> notLoggedInBwdCombos =
        m_notLoggedInBackwardCapture->getHotkeys();
    for (const HotkeyCombination &combo : notLoggedInBwdCombos) {
      notLoggedInBwdBindings.append(
          HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
    }
    hotkeyMgr->setNotLoggedInCycleHotkeys(notLoggedInFwdBindings,
                                          notLoggedInBwdBindings);

    QVector<HotkeyBinding> nonEVEFwdBindings;
    QVector<HotkeyCombination> nonEVEFwdCombos =
        m_nonEVEForwardCapture->getHotkeys();
    for (const HotkeyCombination &combo : nonEVEFwdCombos) {
      nonEVEFwdBindings.append(
          HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
    }

    QVector<HotkeyBinding> nonEVEBwdBindings;
    QVector<HotkeyCombination> nonEVEBwdCombos =
        m_nonEVEBackwardCapture->getHotkeys();
    for (const HotkeyCombination &combo : nonEVEBwdCombos) {
      nonEVEBwdBindings.append(
          HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
    }
    hotkeyMgr->setNonEVECycleHotkeys(nonEVEFwdBindings, nonEVEBwdBindings);
  }

  HotkeyManager::instance()->saveToConfig();

  Config::instance().setChatLogDirectory(
      m_chatLogDirectoryEdit->text().trimmed());

  Config::instance().setGameLogDirectory(
      m_gameLogDirectoryEdit->text().trimmed());

  Config &cfg = Config::instance();

  QHash<QString, QSize> existingSizes = cfg.getAllCustomThumbnailSizes();
  for (const QString &charName : existingSizes.keys()) {
    cfg.removeThumbnailSize(charName);
  }

  for (int i = 0; i < m_thumbnailSizesLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_thumbnailSizesLayout->itemAt(i)->widget());
    if (!rowWidget) {
      continue;
    }

    QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
    QList<QSpinBox *> spinBoxes = rowWidget->findChildren<QSpinBox *>();

    if (!nameEdit || spinBoxes.size() < 2) {
      continue;
    }

    QString charName = nameEdit->text().trimmed();
    if (charName.isEmpty()) {
      continue;
    }

    QSpinBox *widthSpin = spinBoxes[0];
    QSpinBox *heightSpin = spinBoxes[1];

    QSize size(widthSpin->value(), heightSpin->value());
    cfg.setThumbnailSize(charName, size);
  }

  QHash<QString, QSize> existingProcessSizes =
      cfg.getAllCustomProcessThumbnailSizes();
  for (const QString &processName : existingProcessSizes.keys()) {
    cfg.removeProcessThumbnailSize(processName);
  }

  for (int i = 0; i < m_processThumbnailSizesLayout->count() - 1; ++i) {
    QWidget *rowWidget = qobject_cast<QWidget *>(
        m_processThumbnailSizesLayout->itemAt(i)->widget());
    if (!rowWidget) {
      continue;
    }

    QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
    QList<QSpinBox *> spinBoxes = rowWidget->findChildren<QSpinBox *>();

    if (!nameEdit || spinBoxes.size() < 2) {
      continue;
    }

    QString processName = nameEdit->text().trimmed();
    if (processName.isEmpty()) {
      continue;
    }

    int width = spinBoxes[0]->value();
    int height = spinBoxes[1]->value();

    QSize size(width, height);
    cfg.setProcessThumbnailSize(processName, size);
  }

  QHash<QString, QString> existingCustomNames =
      cfg.getAllCustomThumbnailNames();
  for (const QString &charName : existingCustomNames.keys()) {
    cfg.removeCustomThumbnailName(charName);
  }

  for (int i = 0; i < m_customNamesLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_customNamesLayout->itemAt(i)->widget());
    if (!rowWidget) {
      continue;
    }

    QList<QLineEdit *> lineEdits = rowWidget->findChildren<QLineEdit *>();
    if (lineEdits.size() < 2) {
      continue;
    }

    QString charName = lineEdits[0]->text().trimmed();
    QString customName = lineEdits[1]->text().trimmed();

    if (!charName.isEmpty() && !customName.isEmpty()) {
      cfg.setCustomThumbnailName(charName, customName);
    }
  }

  if (hotkeyMgr) {
    QHash<QString, HotkeyBinding> existing =
        hotkeyMgr->getAllCharacterHotkeys();
    for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
      hotkeyMgr->removeCharacterHotkey(it.key());
    }

    for (int i = 0; i < m_characterHotkeysLayout->count() - 1; ++i) {
      QWidget *rowWidget = qobject_cast<QWidget *>(
          m_characterHotkeysLayout->itemAt(i)->widget());
      if (!rowWidget) {
        continue;
      }

      QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
      HotkeyCapture *hotkeyCapture = rowWidget->findChild<HotkeyCapture *>();

      if (!nameEdit || !hotkeyCapture) {
        continue;
      }

      QString charName = nameEdit->text().trimmed();
      if (charName.isEmpty()) {
        continue;
      }

      QVector<HotkeyCombination> hotkeyCombos = hotkeyCapture->getHotkeys();
      if (!hotkeyCombos.isEmpty()) {
        QVector<HotkeyBinding> bindings;
        for (const HotkeyCombination &combo : hotkeyCombos) {
          bindings.append(
              HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
        }
        hotkeyMgr->setCharacterHotkeys(charName, bindings);
      }
    }

    QHash<QString, CycleGroup> existingGroups = hotkeyMgr->getAllCycleGroups();
    for (auto it = existingGroups.constBegin(); it != existingGroups.constEnd();
         ++it) {
      hotkeyMgr->removeCycleGroup(it.key());
    }

    for (int i = 0; i < m_cycleGroupsLayout->count() - 1; ++i) {
      QWidget *rowWidget =
          qobject_cast<QWidget *>(m_cycleGroupsLayout->itemAt(i)->widget());
      if (!rowWidget) {
        continue;
      }

      QList<QLineEdit *> lineEdits = rowWidget->findChildren<QLineEdit *>();
      if (lineEdits.isEmpty()) {
        continue;
      }

      QLineEdit *nameEdit = lineEdits[0];

      QList<QPushButton *> buttons = rowWidget->findChildren<QPushButton *>();
      QPushButton *charactersButton = nullptr;
      for (QPushButton *btn : buttons) {
        if (btn->property("characterList").isValid()) {
          charactersButton = btn;
          break;
        }
      }

      QList<HotkeyCapture *> hotkeyCaptures =
          rowWidget->findChildren<HotkeyCapture *>();
      if (hotkeyCaptures.size() < 2) {
        continue;
      }

      HotkeyCapture *backwardCapture = hotkeyCaptures[0];
      HotkeyCapture *forwardCapture = hotkeyCaptures[1];

      QList<QCheckBox *> checkBoxes = rowWidget->findChildren<QCheckBox *>();
      if (checkBoxes.size() < 2) {
        continue;
      }

      QCheckBox *includeNotLoggedInCheck = checkBoxes[0];
      QCheckBox *noLoopCheck = checkBoxes[1];

      QString groupName = nameEdit->text().trimmed();
      if (groupName.isEmpty()) {
        continue;
      }

      CycleGroup group;
      group.groupName = groupName;

      if (charactersButton) {
        QStringList charList =
            charactersButton->property("characterList").toStringList();
        for (QString &charName : charList) {
          charName = charName.trimmed();
          if (!charName.isEmpty()) {
            group.characterNames.append(charName);
          }
        }
      }

      group.includeNotLoggedIn = includeNotLoggedInCheck->isChecked();
      group.noLoop = noLoopCheck->isChecked();

      QVector<HotkeyCombination> backwardCombos = backwardCapture->getHotkeys();
      for (const HotkeyCombination &combo : backwardCombos) {
        group.backwardBindings.append(
            HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
      }
      if (!backwardCombos.isEmpty()) {
        const HotkeyCombination &first = backwardCombos.first();
        group.backwardBinding =
            HotkeyBinding(first.keyCode, first.ctrl, first.alt, first.shift);
      }

      QVector<HotkeyCombination> forwardCombos = forwardCapture->getHotkeys();
      for (const HotkeyCombination &combo : forwardCombos) {
        group.forwardBindings.append(
            HotkeyBinding(combo.keyCode, combo.ctrl, combo.alt, combo.shift));
      }
      if (!forwardCombos.isEmpty()) {
        const HotkeyCombination &first = forwardCombos.first();
        group.forwardBinding =
            HotkeyBinding(first.keyCode, first.ctrl, first.alt, first.shift);
      }

      hotkeyMgr->createCycleGroup(group);
    }

    hotkeyMgr->saveToConfig();
  }
  Config &config = Config::instance();

  QHash<QString, QColor> existingColors = config.getAllCharacterBorderColors();
  for (auto it = existingColors.constBegin(); it != existingColors.constEnd();
       ++it) {
    config.removeCharacterBorderColor(it.key());
  }

  for (int i = 0; i < m_characterColorsLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_characterColorsLayout->itemAt(i)->widget());
    if (!rowWidget)
      continue;

    QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
    if (!nameEdit)
      continue;

    QString charName = nameEdit->text().trimmed();
    if (charName.isEmpty())
      continue;

    // Find active and inactive color buttons
    QPushButton *activeColorButton = nullptr;
    QPushButton *inactiveColorButton = nullptr;
    QList<QPushButton *> buttons = rowWidget->findChildren<QPushButton *>();
    for (QPushButton *btn : buttons) {
      if (btn->property("color").isValid()) {
        QString colorType = btn->property("colorType").toString();
        if (colorType == "active") {
          activeColorButton = btn;
        } else if (colorType == "inactive") {
          inactiveColorButton = btn;
        }
      }
    }

    if (activeColorButton) {
      QColor color = activeColorButton->property("color").value<QColor>();
      config.setCharacterBorderColor(charName, color);
    }

    if (inactiveColorButton) {
      QColor inactiveColor =
          inactiveColorButton->property("color").value<QColor>();
      if (inactiveColor.isValid()) {
        config.setCharacterInactiveBorderColor(charName, inactiveColor);
      } else {
        config.removeCharacterInactiveBorderColor(charName);
      }
    }
  }

  QStringList neverMinimize;
  for (int i = 0; i < m_neverMinimizeLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_neverMinimizeLayout->itemAt(i)->widget());
    if (!rowWidget)
      continue;

    QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
    if (!nameEdit)
      continue;

    QString charName = nameEdit->text().trimmed();
    if (!charName.isEmpty()) {
      neverMinimize.append(charName);
    }
  }
  config.setNeverMinimizeCharacters(neverMinimize);

  QStringList neverClose;
  for (int i = 0; i < m_neverCloseLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_neverCloseLayout->itemAt(i)->widget());
    if (!rowWidget)
      continue;

    QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
    if (!nameEdit)
      continue;

    QString charName = nameEdit->text().trimmed();
    if (!charName.isEmpty()) {
      neverClose.append(charName);
    }
  }
  config.setNeverCloseCharacters(neverClose);

  QStringList hiddenChars;
  for (int i = 0; i < m_hiddenCharactersLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_hiddenCharactersLayout->itemAt(i)->widget());
    if (!rowWidget)
      continue;

    QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
    if (!nameEdit)
      continue;

    QString charName = nameEdit->text().trimmed();
    if (!charName.isEmpty()) {
      hiddenChars.append(charName);
    }
  }
  config.setHiddenCharacters(hiddenChars);

  QStringList processNames;
  processNames.append("exefile.exe");

  for (int i = 0; i < m_processNamesLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_processNamesLayout->itemAt(i)->widget());
    if (!rowWidget)
      continue;

    QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
    if (!nameEdit)
      continue;

    QString processName = nameEdit->text().trimmed();
    if (!processName.isEmpty() &&
        processName.compare("exefile.exe", Qt::CaseInsensitive) != 0) {
      processNames.append(processName);
    }
  }
  config.setProcessNames(processNames);

  Config::instance().save();
}

void ConfigDialog::onApplyClicked() {
  QVector<HotkeyConflict> conflicts = checkHotkeyConflicts();
  if (!conflicts.isEmpty()) {
    showConflictDialog(conflicts);
    return;
  }

  saveSettings();
  emit settingsApplied();

  if (m_testThumbnail) {
    onTestOverlays();
  }
}

void ConfigDialog::onOkClicked() {
  QVector<HotkeyConflict> conflicts = checkHotkeyConflicts();
  if (!conflicts.isEmpty()) {
    showConflictDialog(conflicts);
    return;
  }

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
        // Add first event
        QString eventType = enabledEvents.first();
        m_testThumbnail->setCombatMessage("Sample Combat Event", eventType);

        // Add second event if there's another enabled type, or use a different
        // one
        if (enabledEvents.size() > 1) {
          m_testThumbnail->setCombatMessage("Second Combat Event",
                                            enabledEvents.at(1));
        } else if (enabledEvents.size() == 1) {
          // Use a different default event type for variety
          QString secondType =
              (eventType == "fleet_invite") ? "follow_warp" : "fleet_invite";
          m_testThumbnail->setCombatMessage("Fleet Warp Command", secondType);
        }
      } else {
        // Use event types that have borders enabled by default
        m_testThumbnail->setCombatMessage("Fleet Invite", "fleet_invite");
        m_testThumbnail->setCombatMessage("Follow Warp", "follow_warp");
      }

      // For testing: Log the active event types to verify they're registered
      QStringList activeTypes = m_testThumbnail->getActiveCombatEventTypes();
      qDebug() << "Test thumbnail active event types:" << activeTypes;
      for (const QString &type : activeTypes) {
        qDebug() << "  Event type:" << type
                 << "Border enabled:" << cfg.combatEventBorderHighlight(type)
                 << "Color:" << cfg.combatEventColor(type);
      }
    }

    m_testThumbnail->resize(cfg.thumbnailWidth(), cfg.thumbnailHeight());

    QPoint dialogPos = pos();
    QPoint testPos = dialogPos + QPoint(width() + 20, 0);
    m_testThumbnail->move(testPos);

    m_testThumbnail->updateOverlays();
    m_testThumbnail->show();
    m_testThumbnail->raise();
  } else if (m_testThumbnail->isVisible()) {
    m_testThumbnail->hide();
    delete m_testThumbnail;
    m_testThumbnail = nullptr;
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

    m_testThumbnail->show();
    m_testThumbnail->raise();
    m_testThumbnail->activateWindow();
  }
}

void ConfigDialog::onBugReportClicked() {
  QDesktopServices::openUrl(
      QUrl("https://github.com/mrmjstc/eve-apm-preview/issues/new"));
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
  } else if (button == m_inactiveBorderColorButton) {
    currentColor = m_inactiveBorderColor;
    targetColor = &m_inactiveBorderColor;
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

QWidget *ConfigDialog::createThumbnailSizeFormRow(const QString &characterName,
                                                  int width, int height) {
  QWidget *rowWidget = new QWidget();
  rowWidget->setStyleSheet(
      "QWidget { background-color: #2a2a2a; border: 1px solid #3a3a3a; "
      "border-radius: 4px; padding: 4px; }");

  QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(8, 4, 8, 4);
  rowLayout->setSpacing(8);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(characterName);
  nameEdit->setPlaceholderText("Character Name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  nameEdit->setMinimumWidth(150);
  rowLayout->addWidget(nameEdit, 1);

  QLabel *widthLabel = new QLabel("Width:");
  widthLabel->setStyleSheet("QLabel { color: #ffffff; background-color: "
                            "transparent; border: none; }");
  rowLayout->addWidget(widthLabel);

  QSpinBox *widthSpin = new QSpinBox();
  widthSpin->setRange(50, 2000);
  widthSpin->setSuffix(" px");
  widthSpin->setValue(width > 0 ? width : Config::instance().thumbnailWidth());
  widthSpin->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  widthSpin->setFixedWidth(100);
  rowLayout->addWidget(widthSpin);

  QLabel *heightLabel = new QLabel("Height:");
  heightLabel->setStyleSheet("QLabel { color: #ffffff; background-color: "
                             "transparent; border: none; }");
  rowLayout->addWidget(heightLabel);

  QSpinBox *heightSpin = new QSpinBox();
  heightSpin->setRange(50, 2000);
  heightSpin->setSuffix(" px");
  heightSpin->setValue(height > 0 ? height
                                  : Config::instance().thumbnailHeight());
  heightSpin->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  heightSpin->setFixedWidth(100);
  rowLayout->addWidget(heightSpin);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(32, 32);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 18px;"
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
  deleteButton->setToolTip("Remove this character size override");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, this, [this, rowWidget]() {
    m_thumbnailSizesLayout->removeWidget(rowWidget);
    rowWidget->deleteLater();
    QTimer::singleShot(0, this,
                       &ConfigDialog::updateThumbnailSizesScrollHeight);
  });

  rowLayout->addWidget(deleteButton);

  return rowWidget;
}

void ConfigDialog::updateThumbnailSizesScrollHeight() {
  int rowCount = m_thumbnailSizesLayout->count() - 1;

  if (rowCount <= 0) {
    m_thumbnailSizesScrollArea->setFixedHeight(10);
  } else {
    int calculatedHeight = (rowCount * 48) + 10;

    int finalHeight = qMin(240, qMax(50, calculatedHeight));
    m_thumbnailSizesScrollArea->setFixedHeight(finalHeight);
  }
}

QWidget *
ConfigDialog::createProcessThumbnailSizeFormRow(const QString &processName,
                                                int width, int height) {
  QWidget *rowWidget = new QWidget();
  rowWidget->setStyleSheet(
      "QWidget { background-color: #2a2a2a; border: 1px solid #3a3a3a; "
      "border-radius: 4px; padding: 4px; }");

  QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(8, 4, 8, 4);
  rowLayout->setSpacing(8);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(processName);
  nameEdit->setPlaceholderText("Process Name (e.g., notepad.exe)");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  nameEdit->setMinimumWidth(150);
  rowLayout->addWidget(nameEdit, 1);

  QLabel *widthLabel = new QLabel("Width:");
  widthLabel->setStyleSheet("QLabel { color: #ffffff; background-color: "
                            "transparent; border: none; }");
  rowLayout->addWidget(widthLabel);

  QSpinBox *widthSpin = new QSpinBox();
  widthSpin->setRange(50, 2000);
  widthSpin->setSuffix(" px");
  widthSpin->setValue(width > 0 ? width : Config::instance().thumbnailWidth());
  widthSpin->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  widthSpin->setFixedWidth(100);
  rowLayout->addWidget(widthSpin);

  QLabel *heightLabel = new QLabel("Height:");
  heightLabel->setStyleSheet("QLabel { color: #ffffff; background-color: "
                             "transparent; border: none; }");
  rowLayout->addWidget(heightLabel);

  QSpinBox *heightSpin = new QSpinBox();
  heightSpin->setRange(50, 2000);
  heightSpin->setSuffix(" px");
  heightSpin->setValue(height > 0 ? height
                                  : Config::instance().thumbnailHeight());
  heightSpin->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  heightSpin->setFixedWidth(100);
  rowLayout->addWidget(heightSpin);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(32, 32);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 18px;"
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
  deleteButton->setToolTip("Remove this process size override");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, this, [this, rowWidget]() {
    m_processThumbnailSizesLayout->removeWidget(rowWidget);
    rowWidget->deleteLater();
    QTimer::singleShot(0, this,
                       &ConfigDialog::updateProcessThumbnailSizesScrollHeight);
  });

  rowLayout->addWidget(deleteButton);

  return rowWidget;
}

void ConfigDialog::updateProcessThumbnailSizesScrollHeight() {
  int rowCount = m_processThumbnailSizesLayout->count() - 1;

  if (rowCount <= 0) {
    m_processThumbnailSizesScrollArea->setFixedHeight(10);
  } else {
    int calculatedHeight = (rowCount * 48) + 10;

    int finalHeight = qMin(240, qMax(50, calculatedHeight));
    m_processThumbnailSizesScrollArea->setFixedHeight(finalHeight);
  }
}

QWidget *ConfigDialog::createCustomNameFormRow(const QString &characterName,
                                               const QString &customName) {
  QWidget *rowWidget = new QWidget();
  rowWidget->setStyleSheet(
      "QWidget { background-color: #2a2a2a; border: 1px solid #3a3a3a; "
      "border-radius: 4px; padding: 4px; }");

  QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(8, 4, 8, 4);
  rowLayout->setSpacing(8);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(characterName);
  nameEdit->setPlaceholderText("Character Name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  nameEdit->setMinimumWidth(150);
  rowLayout->addWidget(nameEdit, 1);

  QLabel *arrowLabel = new QLabel("→");
  arrowLabel->setStyleSheet("QLabel { color: #888888; background-color: "
                            "transparent; border: none; font-size: 16px; }");
  rowLayout->addWidget(arrowLabel);

  QLineEdit *customNameEdit = new QLineEdit();
  customNameEdit->setText(customName);
  customNameEdit->setPlaceholderText("Custom Display Name");
  customNameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  customNameEdit->setMinimumWidth(150);
  rowLayout->addWidget(customNameEdit, 1);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(32, 32);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 18px;"
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
  deleteButton->setToolTip("Remove this custom name");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, this, [this, rowWidget]() {
    m_customNamesLayout->removeWidget(rowWidget);
    rowWidget->deleteLater();
    QTimer::singleShot(0, this, &ConfigDialog::updateCustomNamesScrollHeight);
  });

  rowLayout->addWidget(deleteButton);

  return rowWidget;
}

void ConfigDialog::updateCustomNamesScrollHeight() {
  int rowCount = m_customNamesLayout->count() - 1;

  if (rowCount <= 0) {
    m_customNamesScrollArea->setFixedHeight(10);
  } else {
    int calculatedHeight = (rowCount * 48) + 10;

    int finalHeight = qMin(240, qMax(50, calculatedHeight));
    m_customNamesScrollArea->setFixedHeight(finalHeight);
  }
}

QWidget *
ConfigDialog::createCharacterHotkeyFormRow(const QString &characterName,
                                           int vkCode, int modifiers) {
  QWidget *rowWidget = new QWidget();
  rowWidget->setStyleSheet(
      "QWidget { background-color: #2a2a2a; border: 1px solid #3a3a3a; "
      "border-radius: 4px; padding: 4px; }");

  QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(8, 4, 8, 4);
  rowLayout->setSpacing(8);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(characterName);
  nameEdit->setPlaceholderText("Character Name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  nameEdit->setMinimumWidth(150);
  rowLayout->addWidget(nameEdit, 1);

  QLabel *hotkeyLabel = new QLabel("Hotkey:");
  hotkeyLabel->setStyleSheet("QLabel { color: #ffffff; background-color: "
                             "transparent; border: none; }");
  rowLayout->addWidget(hotkeyLabel);

  HotkeyCapture *hotkeyCapture = new HotkeyCapture();
  hotkeyCapture->setFixedWidth(180);
  hotkeyCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());

  if (vkCode > 0) {
    hotkeyCapture->setHotkey(vkCode, (modifiers & MOD_CONTROL) != 0,
                             (modifiers & MOD_ALT) != 0,
                             (modifiers & MOD_SHIFT) != 0);
  }

  connect(hotkeyCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);

  rowLayout->addWidget(hotkeyCapture);

  QPushButton *clearButton = new QPushButton("Clear");
  clearButton->setFixedHeight(28);
  clearButton->setStyleSheet("QPushButton {"
                             "    background-color: #3a3a3a;"
                             "    color: #a0a0a0;"
                             "    border: 1px solid #555555;"
                             "    border-radius: 3px;"
                             "    font-size: 11px;"
                             "    font-weight: bold;"
                             "    padding: 4px 8px;"
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
  clearButton->setCursor(Qt::PointingHandCursor);
  connect(clearButton, &QPushButton::clicked,
          [hotkeyCapture]() { hotkeyCapture->clearHotkey(); });

  rowLayout->addWidget(clearButton);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(32, 32);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 18px;"
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
  deleteButton->setToolTip("Remove this character hotkey");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, this, [this, rowWidget]() {
    m_characterHotkeysLayout->removeWidget(rowWidget);
    rowWidget->deleteLater();
    QTimer::singleShot(0, this,
                       &ConfigDialog::updateCharacterHotkeysScrollHeight);
    onHotkeyChanged();
  });

  rowLayout->addWidget(deleteButton);

  return rowWidget;
}

void ConfigDialog::updateCharacterHotkeysScrollHeight() {
  int rowCount = m_characterHotkeysLayout->count() - 1;

  if (rowCount <= 0) {
    m_characterHotkeysScrollArea->setFixedHeight(10);
  } else {
    int calculatedHeight = (rowCount * 48) + 10;
    int finalHeight = qMin(240, qMax(50, calculatedHeight));
    m_characterHotkeysScrollArea->setFixedHeight(finalHeight);
  }
}

void ConfigDialog::onAddCharacterHotkey() {
  QWidget *formRow = createCharacterHotkeyFormRow();

  int count = m_characterHotkeysLayout->count();
  m_characterHotkeysLayout->insertWidget(count - 1, formRow);

  m_characterHotkeysContainer->updateGeometry();
  m_characterHotkeysLayout->activate();

  QLineEdit *nameEdit = formRow->findChild<QLineEdit *>();
  if (nameEdit) {
    nameEdit->setFocus();
    nameEdit->selectAll();
  }

  updateCharacterHotkeysScrollHeight();

  QTimer::singleShot(10, this, [this, formRow]() {
    m_characterHotkeysScrollArea->ensureWidgetVisible(formRow, 10, 10);
    QScrollBar *scrollBar = m_characterHotkeysScrollArea->verticalScrollBar();
    if (scrollBar) {
      scrollBar->setValue(scrollBar->maximum());
    }
  });
}

QWidget *ConfigDialog::createCycleGroupFormRow(
    const QString &groupName, int backwardKey, int backwardMods, int forwardKey,
    int forwardMods, const QString &characters, bool includeNotLoggedIn,
    bool noLoop) {

  QWidget *rowWidget = new QWidget();
  rowWidget->setStyleSheet("QWidget#cycleGroupRow { background-color: #2a2a2a; "
                           "border: 1px solid #3a3a3a; "
                           "border-radius: 4px; padding: 8px; }");
  rowWidget->setObjectName("cycleGroupRow");

  QHBoxLayout *topLayout = new QHBoxLayout(rowWidget);
  topLayout->setContentsMargins(8, 8, 8, 8);
  topLayout->setSpacing(8);

  QVBoxLayout *contentLayout = new QVBoxLayout();
  contentLayout->setSpacing(4);

  QHBoxLayout *firstRowLayout = new QHBoxLayout();
  firstRowLayout->setSpacing(8);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(groupName);
  nameEdit->setPlaceholderText("Group Name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  nameEdit->setMinimumWidth(80);
  nameEdit->setMaximumWidth(100);
  firstRowLayout->addWidget(nameEdit);

  QPushButton *charactersButton = new QPushButton();
  QStringList charList;
  if (!characters.isEmpty()) {
    charList = characters.split(',');
    for (QString &ch : charList) {
      ch = ch.trimmed();
    }
    charactersButton->setText(QString("(%1 characters)").arg(charList.size()));
    charactersButton->setToolTip(charList.join(", "));
  } else {
    charactersButton->setText("(No characters)");
  }
  charactersButton->setStyleSheet(StyleSheet::getTableCellButtonStyleSheet());
  charactersButton->setFixedHeight(32);
  charactersButton->setCursor(Qt::PointingHandCursor);
  charactersButton->setProperty("characterList", charList);
  connect(charactersButton, &QPushButton::clicked, this,
          &ConfigDialog::onEditCycleGroupCharacters);
  firstRowLayout->addWidget(charactersButton, 1);

  QLabel *backwardLabel = new QLabel("Back:");
  backwardLabel->setStyleSheet(
      "QLabel { color: #ffffff; background-color: transparent; border: none; "
      "}");
  firstRowLayout->addWidget(backwardLabel);

  HotkeyCapture *backwardCapture = new HotkeyCapture();
  backwardCapture->setFixedWidth(140);
  backwardCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());
  if (backwardKey > 0) {
    backwardCapture->setHotkey(backwardKey, (backwardMods & MOD_CONTROL) != 0,
                               (backwardMods & MOD_ALT) != 0,
                               (backwardMods & MOD_SHIFT) != 0);
  }
  connect(backwardCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);
  firstRowLayout->addWidget(backwardCapture);

  QPushButton *clearBackwardButton = new QPushButton("Clear");
  clearBackwardButton->setFixedHeight(28);
  clearBackwardButton->setStyleSheet("QPushButton {"
                                     "    background-color: #3a3a3a;"
                                     "    color: #ffffff;"
                                     "    border: 1px solid #555555;"
                                     "    border-radius: 3px;"
                                     "    font-size: 11px;"
                                     "    padding: 4px 8px;"
                                     "}"
                                     "QPushButton:hover {"
                                     "    background-color: #4a4a4a;"
                                     "    border: 1px solid #666666;"
                                     "}"
                                     "QPushButton:pressed {"
                                     "    background-color: #2a2a2a;"
                                     "}");
  clearBackwardButton->setToolTip("Clear backward hotkey");
  clearBackwardButton->setCursor(Qt::PointingHandCursor);
  connect(clearBackwardButton, &QPushButton::clicked,
          [backwardCapture]() { backwardCapture->clearHotkey(); });
  firstRowLayout->addWidget(clearBackwardButton);

  QLabel *forwardLabel = new QLabel("Fwd:");
  forwardLabel->setStyleSheet(
      "QLabel { color: #ffffff; background-color: transparent; border: none; "
      "}");
  firstRowLayout->addWidget(forwardLabel);

  HotkeyCapture *forwardCapture = new HotkeyCapture();
  forwardCapture->setFixedWidth(140);
  forwardCapture->setStyleSheet(
      StyleSheet::getHotkeyCaptureStandaloneStyleSheet());
  if (forwardKey > 0) {
    forwardCapture->setHotkey(forwardKey, (forwardMods & MOD_CONTROL) != 0,
                              (forwardMods & MOD_ALT) != 0,
                              (forwardMods & MOD_SHIFT) != 0);
  }
  connect(forwardCapture, &HotkeyCapture::hotkeyChanged, this,
          &ConfigDialog::onHotkeyChanged);
  firstRowLayout->addWidget(forwardCapture);

  QPushButton *clearForwardButton = new QPushButton("Clear");
  clearForwardButton->setFixedHeight(28);
  clearForwardButton->setStyleSheet("QPushButton {"
                                    "    background-color: #3a3a3a;"
                                    "    color: #ffffff;"
                                    "    border: 1px solid #555555;"
                                    "    border-radius: 3px;"
                                    "    font-size: 11px;"
                                    "    padding: 4px 8px;"
                                    "}"
                                    "QPushButton:hover {"
                                    "    background-color: #4a4a4a;"
                                    "    border: 1px solid #666666;"
                                    "}"
                                    "QPushButton:pressed {"
                                    "    background-color: #2a2a2a;"
                                    "}");
  clearForwardButton->setToolTip("Clear forward hotkey");
  clearForwardButton->setCursor(Qt::PointingHandCursor);
  connect(clearForwardButton, &QPushButton::clicked,
          [forwardCapture]() { forwardCapture->clearHotkey(); });
  firstRowLayout->addWidget(clearForwardButton);

  contentLayout->addLayout(firstRowLayout);

  QHBoxLayout *secondRowLayout = new QHBoxLayout();
  secondRowLayout->setSpacing(16);
  secondRowLayout->setContentsMargins(4, 0, 4, 0);

  QCheckBox *includeNotLoggedInCheck =
      new QCheckBox("Include Not-Logged-In Clients");
  includeNotLoggedInCheck->setChecked(includeNotLoggedIn);
  includeNotLoggedInCheck->setStyleSheet(
      StyleSheet::getDialogCheckBoxStyleSheet());
  includeNotLoggedInCheck->setToolTip(
      "Include not-logged-in EVE clients in this cycle group");
  secondRowLayout->addWidget(includeNotLoggedInCheck);

  QCheckBox *noLoopCheck = new QCheckBox("Don't Loop Back to Start");
  noLoopCheck->setChecked(noLoop);
  noLoopCheck->setStyleSheet(StyleSheet::getDialogCheckBoxStyleSheet());
  noLoopCheck->setToolTip(
      "Do not loop back to the first character when reaching the end");
  secondRowLayout->addWidget(noLoopCheck);

  secondRowLayout->addStretch();

  contentLayout->addLayout(secondRowLayout);

  topLayout->addLayout(contentLayout);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(32, 32);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 18px;"
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
  deleteButton->setToolTip("Remove this cycle group");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, this, [this, rowWidget]() {
    m_cycleGroupsLayout->removeWidget(rowWidget);
    rowWidget->deleteLater();
    QTimer::singleShot(0, this, &ConfigDialog::updateCycleGroupsScrollHeight);
    onHotkeyChanged();
  });

  topLayout->addWidget(deleteButton, 0, Qt::AlignVCenter);

  return rowWidget;
}

QWidget *ConfigDialog::createCharacterColorFormRow(const QString &characterName,
                                                   const QColor &color) {
  Config &cfg = Config::instance();
  QColor inactiveColor = cfg.getCharacterInactiveBorderColor(characterName);

  QWidget *rowWidget = new QWidget();
  rowWidget->setStyleSheet(
      "QWidget { background-color: #2a2a2a; border: 1px solid #3a3a3a; "
      "border-radius: 4px; padding: 4px; }");

  QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(8, 4, 8, 4);
  rowLayout->setSpacing(8);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(characterName);
  nameEdit->setPlaceholderText("Character Name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  rowLayout->addWidget(nameEdit, 1);

  // Active color button
  QPushButton *colorButton = new QPushButton("Active");
  colorButton->setFixedSize(80, 32);
  colorButton->setCursor(Qt::PointingHandCursor);
  colorButton->setProperty("color", color);
  colorButton->setProperty("colorType", "active");
  updateColorButton(colorButton, color);
  connect(colorButton, &QPushButton::clicked, this,
          &ConfigDialog::onCharacterColorButtonClicked);
  rowLayout->addWidget(colorButton);

  // Inactive color button
  QPushButton *inactiveColorButton = new QPushButton("Inactive");
  inactiveColorButton->setFixedSize(80, 32);
  inactiveColorButton->setCursor(Qt::PointingHandCursor);
  inactiveColorButton->setProperty("color", inactiveColor);
  inactiveColorButton->setProperty("colorType", "inactive");
  updateColorButton(inactiveColorButton, inactiveColor.isValid()
                                             ? inactiveColor
                                             : QColor("#808080"));
  connect(inactiveColorButton, &QPushButton::clicked, this,
          &ConfigDialog::onCharacterColorButtonClicked);
  rowLayout->addWidget(inactiveColorButton);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(32, 32);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 18px;"
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
  deleteButton->setToolTip("Remove this character color");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, this, [this, rowWidget]() {
    m_characterColorsLayout->removeWidget(rowWidget);
    rowWidget->deleteLater();
    QTimer::singleShot(0, this,
                       &ConfigDialog::updateCharacterColorsScrollHeight);
  });

  rowLayout->addWidget(deleteButton);

  return rowWidget;
}

void ConfigDialog::updateCharacterColorsScrollHeight() {
  if (m_characterColorsScrollArea && m_characterColorsLayout) {
    int rowCount = m_characterColorsLayout->count() - 1;

    if (rowCount <= 0) {
      m_characterColorsScrollArea->setFixedHeight(10);
    } else {
      int calculatedHeight = (rowCount * 48) + 10;
      int finalHeight = qMin(202, qMax(50, calculatedHeight));
      m_characterColorsScrollArea->setFixedHeight(finalHeight);
    }
  }
}

QWidget *
ConfigDialog::createNeverMinimizeFormRow(const QString &characterName) {
  QWidget *rowWidget = new QWidget();
  rowWidget->setStyleSheet(
      "QWidget { background-color: #2a2a2a; border: 1px solid #3a3a3a; "
      "border-radius: 4px; padding: 4px; }");

  QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(8, 4, 8, 4);
  rowLayout->setSpacing(8);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(characterName);
  nameEdit->setPlaceholderText("Character Name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  rowLayout->addWidget(nameEdit, 1);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(32, 32);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 18px;"
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
  deleteButton->setToolTip("Remove this character");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, this, [this, rowWidget]() {
    m_neverMinimizeLayout->removeWidget(rowWidget);
    rowWidget->deleteLater();
    QTimer::singleShot(0, this, &ConfigDialog::updateNeverMinimizeScrollHeight);
  });

  rowLayout->addWidget(deleteButton);

  return rowWidget;
}

void ConfigDialog::updateNeverMinimizeScrollHeight() {
  if (m_neverMinimizeScrollArea && m_neverMinimizeLayout) {
    int rowCount = m_neverMinimizeLayout->count() - 1;

    if (rowCount <= 0) {
      m_neverMinimizeScrollArea->setFixedHeight(10);
    } else {
      int calculatedHeight = (rowCount * 48) + 10;
      int finalHeight = qMin(202, qMax(50, calculatedHeight));
      m_neverMinimizeScrollArea->setFixedHeight(finalHeight);
    }
  }
}

QWidget *ConfigDialog::createNeverCloseFormRow(const QString &characterName) {
  QWidget *rowWidget = new QWidget();
  rowWidget->setStyleSheet(
      "QWidget { background-color: #2a2a2a; border: 1px solid #3a3a3a; "
      "border-radius: 4px; padding: 4px; }");

  QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(8, 4, 8, 4);
  rowLayout->setSpacing(8);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(characterName);
  nameEdit->setPlaceholderText("Character Name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  rowLayout->addWidget(nameEdit, 1);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(32, 32);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 18px;"
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
  deleteButton->setToolTip("Remove this character");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, this, [this, rowWidget]() {
    m_neverCloseLayout->removeWidget(rowWidget);
    rowWidget->deleteLater();
    QTimer::singleShot(0, this, &ConfigDialog::updateNeverCloseScrollHeight);
  });

  rowLayout->addWidget(deleteButton);

  return rowWidget;
}

void ConfigDialog::updateNeverCloseScrollHeight() {
  if (m_neverCloseScrollArea && m_neverCloseLayout) {
    int rowCount = m_neverCloseLayout->count() - 1;

    if (rowCount <= 0) {
      m_neverCloseScrollArea->setFixedHeight(10);
    } else {
      int calculatedHeight = (rowCount * 48) + 10;
      int finalHeight = qMin(202, qMax(50, calculatedHeight));
      m_neverCloseScrollArea->setFixedHeight(finalHeight);
    }
  }
}

QWidget *
ConfigDialog::createHiddenCharactersFormRow(const QString &characterName) {
  QWidget *rowWidget = new QWidget();
  rowWidget->setStyleSheet(
      "QWidget { background-color: #2a2a2a; border: 1px solid #3a3a3a; "
      "border-radius: 4px; padding: 4px; }");

  QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(8, 4, 8, 4);
  rowLayout->setSpacing(8);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(characterName);
  nameEdit->setPlaceholderText("Character Name");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  rowLayout->addWidget(nameEdit, 1);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(32, 32);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 18px;"
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
  deleteButton->setToolTip("Remove this character");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, this, [this, rowWidget]() {
    m_hiddenCharactersLayout->removeWidget(rowWidget);
    rowWidget->deleteLater();
    QTimer::singleShot(0, this,
                       &ConfigDialog::updateHiddenCharactersScrollHeight);
  });

  rowLayout->addWidget(deleteButton);

  return rowWidget;
}

void ConfigDialog::updateHiddenCharactersScrollHeight() {
  if (m_hiddenCharactersScrollArea && m_hiddenCharactersLayout) {
    int rowCount = m_hiddenCharactersLayout->count() - 1;

    if (rowCount <= 0) {
      m_hiddenCharactersScrollArea->setFixedHeight(10);
    } else {
      int calculatedHeight = (rowCount * 48) + 10;
      int finalHeight = qMin(202, qMax(50, calculatedHeight));
      m_hiddenCharactersScrollArea->setFixedHeight(finalHeight);
    }
  }
}

QWidget *ConfigDialog::createProcessNamesFormRow(const QString &processName) {
  QWidget *rowWidget = new QWidget();
  rowWidget->setStyleSheet(
      "QWidget { background-color: #2a2a2a; border: 1px solid #3a3a3a; "
      "border-radius: 4px; padding: 4px; }");

  QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(8, 4, 8, 4);
  rowLayout->setSpacing(8);

  QLineEdit *nameEdit = new QLineEdit();
  nameEdit->setText(processName);
  nameEdit->setPlaceholderText("Process Name (e.g., example.exe)");
  nameEdit->setStyleSheet(StyleSheet::getTableCellEditorStyleSheet());
  rowLayout->addWidget(nameEdit, 1);

  QPushButton *deleteButton = new QPushButton("×");
  deleteButton->setFixedSize(32, 32);
  deleteButton->setStyleSheet("QPushButton {"
                              "    background-color: #3a3a3a;"
                              "    color: #ffffff;"
                              "    border: 1px solid #555555;"
                              "    border-radius: 4px;"
                              "    font-size: 18px;"
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
  deleteButton->setToolTip("Remove this process");
  deleteButton->setCursor(Qt::PointingHandCursor);

  connect(deleteButton, &QPushButton::clicked, this, [this, rowWidget]() {
    m_processNamesLayout->removeWidget(rowWidget);
    rowWidget->deleteLater();
    QTimer::singleShot(0, this, &ConfigDialog::updateProcessNamesScrollHeight);
  });

  rowLayout->addWidget(deleteButton);

  return rowWidget;
}

void ConfigDialog::updateProcessNamesScrollHeight() {
  if (m_processNamesScrollArea && m_processNamesLayout) {
    int rowCount = m_processNamesLayout->count() - 1;

    if (rowCount <= 0) {
      m_processNamesScrollArea->setFixedHeight(10);
    } else {
      int calculatedHeight = (rowCount * 48) + 10;
      int finalHeight = qMin(202, qMax(50, calculatedHeight));
      m_processNamesScrollArea->setFixedHeight(finalHeight);
    }
  }
}

void ConfigDialog::updateCycleGroupsScrollHeight() {
  if (m_cycleGroupsScrollArea && m_cycleGroupsLayout) {
    int rowCount = m_cycleGroupsLayout->count() - 1;

    if (rowCount <= 0) {
      m_cycleGroupsScrollArea->setFixedHeight(10);
    } else {
      int calculatedHeight = (rowCount * 85) + 10;
      int finalHeight = qMin(180, qMax(50, calculatedHeight));
      m_cycleGroupsScrollArea->setFixedHeight(finalHeight);
    }
  }
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
    for (int i = 0; i < m_characterHotkeysLayout->count() - 1; ++i) {
      QWidget *rowWidget = qobject_cast<QWidget *>(
          m_characterHotkeysLayout->itemAt(i)->widget());
      if (rowWidget) {
        QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
        if (nameEdit && !nameEdit->text().trimmed().isEmpty()) {
          existingCharacters.insert(nameEdit->text().trimmed());
        }
      }
    }
  } else {
    while (m_characterHotkeysLayout->count() > 1) {
      QLayoutItem *item = m_characterHotkeysLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
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

    QWidget *formRow = createCharacterHotkeyFormRow(characterName);
    int count = m_characterHotkeysLayout->count();
    m_characterHotkeysLayout->insertWidget(count - 1, formRow);

    addedCount++;
  }

  updateCharacterHotkeysScrollHeight();

  if (addedCount > 0) {
    onHotkeyChanged();
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
  int count = m_cycleGroupsLayout->count() - 1;
  QString defaultName = QString("Group %1").arg(count + 1);

  QWidget *formRow = createCycleGroupFormRow(defaultName);

  int layoutCount = m_cycleGroupsLayout->count();
  m_cycleGroupsLayout->insertWidget(layoutCount - 1, formRow);

  m_cycleGroupsContainer->updateGeometry();
  m_cycleGroupsLayout->activate();

  QLineEdit *nameEdit = formRow->findChild<QLineEdit *>();
  if (nameEdit) {
    nameEdit->setFocus();
    nameEdit->selectAll();
  }

  updateCycleGroupsScrollHeight();

  QTimer::singleShot(10, this, [this, formRow]() {
    m_cycleGroupsScrollArea->ensureWidgetVisible(formRow, 10, 10);
    QScrollBar *scrollBar = m_cycleGroupsScrollArea->verticalScrollBar();
    if (scrollBar) {
      scrollBar->setValue(scrollBar->maximum());
    }
  });
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
      button->setToolTip("");
    } else {
      button->setText(QString("(%1 characters)").arg(newList.count()));
      button->setToolTip(newList.join(", "));
    }
  }
}

void ConfigDialog::onAddNeverMinimizeCharacter() {
  QWidget *formRow = createNeverMinimizeFormRow();

  int count = m_neverMinimizeLayout->count();
  m_neverMinimizeLayout->insertWidget(count - 1, formRow);

  m_neverMinimizeContainer->updateGeometry();
  m_neverMinimizeLayout->activate();

  QLineEdit *nameEdit = formRow->findChild<QLineEdit *>();
  if (nameEdit) {
    nameEdit->setFocus();
  }

  updateNeverMinimizeScrollHeight();

  QTimer::singleShot(0, [this]() {
    m_neverMinimizeScrollArea->verticalScrollBar()->setValue(
        m_neverMinimizeScrollArea->verticalScrollBar()->maximum());
  });
}

void ConfigDialog::onAddNeverCloseCharacter() {
  QWidget *formRow = createNeverCloseFormRow();

  int count = m_neverCloseLayout->count();
  m_neverCloseLayout->insertWidget(count - 1, formRow);

  m_neverCloseContainer->updateGeometry();
  m_neverCloseLayout->activate();

  QLineEdit *nameEdit = formRow->findChild<QLineEdit *>();
  if (nameEdit) {
    nameEdit->setFocus();
  }

  updateNeverCloseScrollHeight();

  QTimer::singleShot(0, [this]() {
    m_neverCloseScrollArea->verticalScrollBar()->setValue(
        m_neverCloseScrollArea->verticalScrollBar()->maximum());
  });
}

void ConfigDialog::onPopulateNeverClose() {
  WindowCapture capture;
  QVector<WindowInfo> windows = capture.getEVEWindows();

  if (windows.isEmpty()) {
    QMessageBox::information(this, "No Windows Found",
                             "No EVE Online windows are currently open.");
    return;
  }

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Populate Never Close List");
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
    for (int i = 0; i < m_neverCloseLayout->count() - 1; ++i) {
      QWidget *rowWidget =
          qobject_cast<QWidget *>(m_neverCloseLayout->itemAt(i)->widget());
      if (!rowWidget)
        continue;

      QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
      if (!nameEdit)
        continue;

      QString charName = nameEdit->text().trimmed();
      if (!charName.isEmpty()) {
        existingCharacters.insert(charName.toLower());
      }
    }
  } else {
    while (m_neverCloseLayout->count() > 1) {
      QLayoutItem *item = m_neverCloseLayout->takeAt(0);
      if (item->widget()) {
        QWidget *widget = item->widget();
        widget->setParent(nullptr);
        delete widget;
      }
      delete item;
    }
  }

  int addedCount = 0;
  for (const WindowInfo &window : windows) {
    QString charName = OverlayInfo::extractCharacterName(window.title);
    if (charName.isEmpty())
      continue;

    if (existingCharacters.contains(charName.toLower()))
      continue;

    QWidget *formRow = createNeverCloseFormRow(charName);
    int count = m_neverCloseLayout->count();
    m_neverCloseLayout->insertWidget(count - 1, formRow);
    existingCharacters.insert(charName.toLower());
    addedCount++;
  }

  if (addedCount == 0 && !clearExisting) {
    QMessageBox::information(
        this, "No New Characters",
        "All open characters are already in the Never Close list.");
  } else {
    m_neverCloseContainer->updateGeometry();
    m_neverCloseLayout->activate();
    updateNeverCloseScrollHeight();

    QTimer::singleShot(0, [this]() {
      m_neverCloseScrollArea->verticalScrollBar()->setValue(
          m_neverCloseScrollArea->verticalScrollBar()->maximum());
    });
  }
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
    for (int i = 0; i < m_neverMinimizeLayout->count() - 1; ++i) {
      QWidget *rowWidget =
          qobject_cast<QWidget *>(m_neverMinimizeLayout->itemAt(i)->widget());
      if (!rowWidget)
        continue;

      QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
      if (nameEdit && !nameEdit->text().trimmed().isEmpty()) {
        existingCharacters.insert(nameEdit->text().trimmed());
      }
    }
  } else {
    while (m_neverMinimizeLayout->count() > 1) {
      QLayoutItem *item = m_neverMinimizeLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
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

    QWidget *formRow = createNeverMinimizeFormRow(characterName);
    int count = m_neverMinimizeLayout->count();
    m_neverMinimizeLayout->insertWidget(count - 1, formRow);

    addedCount++;
  }

  updateNeverMinimizeScrollHeight();

  if (addedCount > 0) {
    QMessageBox::information(
        this, "Characters Added",
        QString("Added %1 character%2 to the never minimize list.")
            .arg(addedCount)
            .arg(addedCount == 1 ? "" : "s"));

    QTimer::singleShot(0, [this]() {
      m_neverMinimizeScrollArea->verticalScrollBar()->setValue(
          m_neverMinimizeScrollArea->verticalScrollBar()->maximum());
    });
  } else {
    QMessageBox::information(this, "No New Characters",
                             "All open characters are already in the list.");
  }
}

void ConfigDialog::onAddHiddenCharacter() {
  QWidget *formRow = createHiddenCharactersFormRow();

  int count = m_hiddenCharactersLayout->count();
  m_hiddenCharactersLayout->insertWidget(count - 1, formRow);

  updateHiddenCharactersScrollHeight();

  QLineEdit *nameEdit = formRow->findChild<QLineEdit *>();
  if (nameEdit) {
    nameEdit->setFocus();
  }

  QTimer::singleShot(100, this, [this]() {
    m_hiddenCharactersScrollArea->verticalScrollBar()->setValue(
        m_hiddenCharactersScrollArea->verticalScrollBar()->maximum());
  });
}

void ConfigDialog::onPopulateHiddenCharacters() {
  WindowCapture capture;
  QVector<WindowInfo> windows = capture.getEVEWindows();

  if (windows.isEmpty()) {
    QMessageBox::information(this, "No Windows Found",
                             "No EVE Online windows are currently open.");
    return;
  }

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Populate Hidden Characters List");
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
    for (int i = 0; i < m_hiddenCharactersLayout->count() - 1; ++i) {
      QWidget *rowWidget = qobject_cast<QWidget *>(
          m_hiddenCharactersLayout->itemAt(i)->widget());
      if (!rowWidget)
        continue;

      QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
      if (nameEdit) {
        QString charName = nameEdit->text().trimmed();
        if (!charName.isEmpty()) {
          existingCharacters.insert(charName);
        }
      }
    }
  } else {
    while (m_hiddenCharactersLayout->count() > 1) {
      QLayoutItem *item = m_hiddenCharactersLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
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

    QWidget *formRow = createHiddenCharactersFormRow(characterName);
    int count = m_hiddenCharactersLayout->count();
    m_hiddenCharactersLayout->insertWidget(count - 1, formRow);

    addedCount++;
  }

  updateHiddenCharactersScrollHeight();

  if (addedCount > 0) {
    QMessageBox::information(
        this, "Characters Added",
        QString("Added %1 character%2 to the hidden characters list.")
            .arg(addedCount)
            .arg(addedCount == 1 ? "" : "s"));
  } else {
    QMessageBox::information(this, "No New Characters",
                             "All open characters are already in the list.");
  }
}

void ConfigDialog::onAddCharacterColor() {
  QWidget *formRow = createCharacterColorFormRow();

  int count = m_characterColorsLayout->count();
  m_characterColorsLayout->insertWidget(count - 1, formRow);

  m_characterColorsContainer->updateGeometry();
  m_characterColorsLayout->activate();

  QLineEdit *nameEdit = formRow->findChild<QLineEdit *>();
  if (nameEdit) {
    nameEdit->setFocus();
  }

  updateCharacterColorsScrollHeight();

  QTimer::singleShot(0, [this]() {
    m_characterColorsScrollArea->verticalScrollBar()->setValue(
        m_characterColorsScrollArea->verticalScrollBar()->maximum());
  });
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
    for (int i = 0; i < m_characterColorsLayout->count() - 1; ++i) {
      QWidget *rowWidget =
          qobject_cast<QWidget *>(m_characterColorsLayout->itemAt(i)->widget());
      if (!rowWidget)
        continue;

      QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
      if (nameEdit && !nameEdit->text().trimmed().isEmpty()) {
        existingCharacters.insert(nameEdit->text().trimmed());
      }
    }
  } else {
    while (m_characterColorsLayout->count() > 1) {
      QLayoutItem *item = m_characterColorsLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
  }

  Config &config = Config::instance();
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

    QColor characterColor = config.getCharacterBorderColor(characterName);
    if (!characterColor.isValid()) {
      characterColor = QColor("#00FFFF");
    }

    QWidget *formRow =
        createCharacterColorFormRow(characterName, characterColor);
    int count = m_characterColorsLayout->count();
    m_characterColorsLayout->insertWidget(count - 1, formRow);

    addedCount++;
  }

  updateCharacterColorsScrollHeight();

  if (addedCount > 0) {
    QMessageBox::information(
        this, "Characters Added",
        QString("Added %1 character%2 to the color customization list.")
            .arg(addedCount)
            .arg(addedCount == 1 ? "" : "s"));

    QTimer::singleShot(0, [this]() {
      m_characterColorsScrollArea->verticalScrollBar()->setValue(
          m_characterColorsScrollArea->verticalScrollBar()->maximum());
    });
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
  int rowCount = m_characterColorsLayout->count() - 1;

  if (rowCount <= 0) {
    QMessageBox::information(
        this, "No Characters",
        "There are no characters in the list. Add characters first using 'Add "
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
          "This will assign unique colors to all %1 character%2 in the list.")
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

  for (int i = 0; i < rowCount; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_characterColorsLayout->itemAt(i)->widget());
    if (!rowWidget)
      continue;

    QColor assignedColor(colorPalette[i % colorPalette.size()]);

    QPushButton *colorButton = nullptr;
    QList<QPushButton *> buttons = rowWidget->findChildren<QPushButton *>();
    for (QPushButton *btn : buttons) {
      if (btn->property("color").isValid()) {
        colorButton = btn;
        break;
      }
    }

    if (colorButton) {
      colorButton->setProperty("color", assignedColor);
      updateColorButton(colorButton, assignedColor);
    }
  }

  QMessageBox::information(
      this, "Colors Assigned",
      QString("Unique colors have been assigned to %1 character%2.")
          .arg(rowCount)
          .arg(rowCount == 1 ? "" : "s"));
}

void ConfigDialog::onCustomSystemColors() {
  SystemColorsDialog *dialog = new SystemColorsDialog(this);
  dialog->loadSystemColors();

  if (dialog->exec() == QDialog::Accepted) {
    emit settingsApplied();
  }

  dialog->deleteLater();
}

void ConfigDialog::onAddThumbnailSize() {
  QWidget *formRow = createThumbnailSizeFormRow();

  int count = m_thumbnailSizesLayout->count();
  m_thumbnailSizesLayout->insertWidget(count - 1, formRow);

  m_thumbnailSizesContainer->updateGeometry();
  m_thumbnailSizesLayout->activate();

  QLineEdit *nameEdit = formRow->findChild<QLineEdit *>();
  if (nameEdit) {
    nameEdit->setFocus();
    nameEdit->selectAll();
  }

  updateThumbnailSizesScrollHeight();

  QTimer::singleShot(10, this, [this, formRow]() {
    m_thumbnailSizesScrollArea->ensureWidgetVisible(formRow, 10, 10);

    QScrollBar *scrollBar = m_thumbnailSizesScrollArea->verticalScrollBar();
    if (scrollBar) {
      scrollBar->setValue(scrollBar->maximum());
    }
  });
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
    for (int i = 0; i < m_thumbnailSizesLayout->count() - 1; ++i) {
      QWidget *rowWidget =
          qobject_cast<QWidget *>(m_thumbnailSizesLayout->itemAt(i)->widget());
      if (rowWidget) {
        QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
        if (nameEdit) {
          existingCharacters.insert(nameEdit->text().trimmed());
        }
      }
    }
  } else {
    while (m_thumbnailSizesLayout->count() > 1) {
      QLayoutItem *item = m_thumbnailSizesLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
  }

  Config &cfg = Config::instance();
  int addedCount = 0;

  for (const QString &characterName : characterNames) {
    if (!clearExisting && existingCharacters.contains(characterName)) {
      continue;
    }

    int width, height;
    if (cfg.hasCustomThumbnailSize(characterName)) {
      QSize customSize = cfg.getThumbnailSize(characterName);
      width = customSize.width();
      height = customSize.height();
    } else {
      width = cfg.thumbnailWidth();
      height = cfg.thumbnailHeight();
    }

    QWidget *formRow = createThumbnailSizeFormRow(characterName, width, height);
    int count = m_thumbnailSizesLayout->count();
    m_thumbnailSizesLayout->insertWidget(count - 1, formRow);

    addedCount++;
  }

  updateThumbnailSizesScrollHeight();

  QString resultMsg = clearExisting ? QString("Replaced with %1 character%2.")
                                          .arg(addedCount)
                                          .arg(addedCount == 1 ? "" : "s")
                                    : QString("Added %1 new character%2.")
                                          .arg(addedCount)
                                          .arg(addedCount == 1 ? "" : "s");

  QMessageBox::information(this, "Populate Complete", resultMsg);
}

void ConfigDialog::onRemoveThumbnailSize() {}

void ConfigDialog::onResetThumbnailSizesToDefault() {
  int rowCount = m_thumbnailSizesLayout->count() - 1;

  if (rowCount == 0) {
    return;
  }

  QMessageBox::StandardButton reply = QMessageBox::question(
      this, "Reset All Sizes",
      "Are you sure you want to remove all custom thumbnail sizes?\n"
      "All characters will revert to the default size.",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    while (m_thumbnailSizesLayout->count() > 1) {
      QLayoutItem *item = m_thumbnailSizesLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }

    updateThumbnailSizesScrollHeight();
  }
}

void ConfigDialog::onAddProcessThumbnailSize() {
  QWidget *formRow = createProcessThumbnailSizeFormRow();

  int count = m_processThumbnailSizesLayout->count();
  m_processThumbnailSizesLayout->insertWidget(count - 1, formRow);

  m_processThumbnailSizesContainer->updateGeometry();
  m_processThumbnailSizesLayout->activate();

  QLineEdit *nameEdit = formRow->findChild<QLineEdit *>();
  if (nameEdit) {
    nameEdit->setFocus();
    nameEdit->selectAll();
  }

  updateProcessThumbnailSizesScrollHeight();

  QTimer::singleShot(10, this, [this, formRow]() {
    m_processThumbnailSizesScrollArea->ensureWidgetVisible(formRow, 10, 10);

    QScrollBar *scrollBar =
        m_processThumbnailSizesScrollArea->verticalScrollBar();
    if (scrollBar) {
      scrollBar->setValue(scrollBar->maximum());
    }
  });
}

void ConfigDialog::onPopulateProcessThumbnailSizes() {
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
        if (processId == 0) {
          return TRUE;
        }

        HANDLE processHandle =
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (!processHandle) {
          return TRUE;
        }

        wchar_t processPath[MAX_PATH];
        DWORD pathSize = MAX_PATH;
        if (QueryFullProcessImageNameW(processHandle, 0, processPath,
                                       &pathSize)) {
          QString fullPath = QString::fromWCharArray(processPath, pathSize);
          QString processName =
              fullPath.mid(fullPath.lastIndexOf('\\') + 1).toLower();

          if (processName.compare("exefile.exe", Qt::CaseInsensitive) != 0) {
            (*processMap)[processName] = titleStr;
          }
        }

        CloseHandle(processHandle);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&processToTitle));

  if (processToTitle.isEmpty()) {
    QMessageBox::information(this, "No Windows Found",
                             "No non-EVE windows are currently open.");
    return;
  }

  QStringList processNames = processToTitle.keys();
  std::sort(processNames.begin(), processNames.end());

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Populate Process Thumbnail Sizes");
  msgBox.setText(QString("Found %1 non-EVE application%2.")
                     .arg(processNames.count())
                     .arg(processNames.count() == 1 ? "" : "s"));
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

  QSet<QString> existingProcesses;
  if (!clearExisting) {
    for (int i = 0; i < m_processThumbnailSizesLayout->count() - 1; ++i) {
      QWidget *rowWidget = qobject_cast<QWidget *>(
          m_processThumbnailSizesLayout->itemAt(i)->widget());
      if (rowWidget) {
        QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
        if (nameEdit) {
          existingProcesses.insert(nameEdit->text().trimmed().toLower());
        }
      }
    }
  } else {
    while (m_processThumbnailSizesLayout->count() > 1) {
      QLayoutItem *item = m_processThumbnailSizesLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
  }

  Config &cfg = Config::instance();
  int addedCount = 0;

  for (const QString &processName : processNames) {
    if (!clearExisting && existingProcesses.contains(processName.toLower())) {
      continue;
    }

    int width, height;
    if (cfg.hasCustomProcessThumbnailSize(processName)) {
      QSize customSize = cfg.getProcessThumbnailSize(processName);
      width = customSize.width();
      height = customSize.height();
    } else {
      width = cfg.thumbnailWidth();
      height = cfg.thumbnailHeight();
    }

    QWidget *formRow =
        createProcessThumbnailSizeFormRow(processName, width, height);
    int count = m_processThumbnailSizesLayout->count();
    m_processThumbnailSizesLayout->insertWidget(count - 1, formRow);

    addedCount++;
  }

  updateProcessThumbnailSizesScrollHeight();

  QString resultMsg = clearExisting ? QString("Replaced with %1 process%2.")
                                          .arg(addedCount)
                                          .arg(addedCount == 1 ? "" : "es")
                                    : QString("Added %1 new process%2.")
                                          .arg(addedCount)
                                          .arg(addedCount == 1 ? "" : "es");

  QMessageBox::information(this, "Populate Complete", resultMsg);
}

void ConfigDialog::onResetProcessThumbnailSizesToDefault() {
  int rowCount = m_processThumbnailSizesLayout->count() - 1;

  if (rowCount == 0) {
    return;
  }

  QMessageBox::StandardButton reply = QMessageBox::question(
      this, "Reset All Sizes",
      "Are you sure you want to remove all custom process thumbnail sizes?\n"
      "All non-EVE applications will revert to the default size.",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    while (m_processThumbnailSizesLayout->count() > 1) {
      QLayoutItem *item = m_processThumbnailSizesLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }

    updateProcessThumbnailSizesScrollHeight();
  }
}

void ConfigDialog::onAddCustomName() {
  QWidget *formRow = createCustomNameFormRow();

  int count = m_customNamesLayout->count();
  m_customNamesLayout->insertWidget(count - 1, formRow);

  m_customNamesContainer->updateGeometry();
  m_customNamesLayout->activate();

  QList<QLineEdit *> lineEdits = formRow->findChildren<QLineEdit *>();
  if (!lineEdits.isEmpty()) {
    lineEdits[0]->setFocus();
    lineEdits[0]->selectAll();
  }

  updateCustomNamesScrollHeight();

  QTimer::singleShot(10, this, [this, formRow]() {
    m_customNamesScrollArea->ensureWidgetVisible(formRow, 10, 10);

    QScrollBar *scrollBar = m_customNamesScrollArea->verticalScrollBar();
    if (scrollBar) {
      scrollBar->setValue(scrollBar->maximum());
    }
  });
}

void ConfigDialog::onPopulateCustomNames() {
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
  msgBox.setWindowTitle("Populate Custom Names");
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
    for (int i = 0; i < m_customNamesLayout->count() - 1; ++i) {
      QWidget *rowWidget =
          qobject_cast<QWidget *>(m_customNamesLayout->itemAt(i)->widget());
      if (rowWidget) {
        QList<QLineEdit *> lineEdits = rowWidget->findChildren<QLineEdit *>();
        if (!lineEdits.isEmpty()) {
          existingCharacters.insert(lineEdits[0]->text().trimmed());
        }
      }
    }
  } else {
    while (m_customNamesLayout->count() > 1) {
      QLayoutItem *item = m_customNamesLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
  }

  Config &cfg = Config::instance();
  int addedCount = 0;

  for (const QString &characterName : characterNames) {
    if (!clearExisting && existingCharacters.contains(characterName)) {
      continue;
    }

    QString customName = cfg.getCustomThumbnailName(characterName);

    QWidget *formRow = createCustomNameFormRow(characterName, customName);
    int count = m_customNamesLayout->count();
    m_customNamesLayout->insertWidget(count - 1, formRow);

    addedCount++;
  }

  updateCustomNamesScrollHeight();

  QString resultMsg = clearExisting ? QString("Replaced with %1 character%2.")
                                          .arg(addedCount)
                                          .arg(addedCount == 1 ? "" : "s")
                                    : QString("Added %1 new character%2.")
                                          .arg(addedCount)
                                          .arg(addedCount == 1 ? "" : "s");

  QMessageBox::information(this, "Populate Complete", resultMsg);
}

void ConfigDialog::onAddProcessName() {
  QWidget *formRow = createProcessNamesFormRow();

  int count = m_processNamesLayout->count();
  m_processNamesLayout->insertWidget(count - 1, formRow);

  updateProcessNamesScrollHeight();

  QLineEdit *nameEdit = formRow->findChild<QLineEdit *>();
  if (nameEdit) {
    nameEdit->setFocus();
  }

  QTimer::singleShot(100, this, [this]() {
    m_processNamesScrollArea->verticalScrollBar()->setValue(
        m_processNamesScrollArea->verticalScrollBar()->maximum());
  });
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
  tableWidget->setFocusPolicy(Qt::NoFocus);
  tableWidget->setStyleSheet(StyleSheet::getTableStyleSheet());

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

  if (dialog.exec() == QDialog::Accepted) {
    QList<QTableWidgetItem *> selectedItems = tableWidget->selectedItems();
    if (selectedItems.isEmpty()) {
      return;
    }

    QSet<QString> existingProcesses;
    for (int i = 0; i < m_processNamesLayout->count() - 1; ++i) {
      QWidget *rowWidget =
          qobject_cast<QWidget *>(m_processNamesLayout->itemAt(i)->widget());
      if (!rowWidget)
        continue;

      QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
      if (nameEdit) {
        QString processName = nameEdit->text().trimmed();
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
        QWidget *formRow = createProcessNamesFormRow(processName);
        int count = m_processNamesLayout->count();
        m_processNamesLayout->insertWidget(count - 1, formRow);

        existingProcesses.insert(processName.toLower());
        addedCount++;
      }
    }

    updateProcessNamesScrollHeight();

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
    m_activeBorderStyleCombo->setCurrentIndex(
        m_activeBorderStyleCombo->findData(
            Config::DEFAULT_ACTIVE_BORDER_STYLE));

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
    m_uniqueSystemColorsCheck->setChecked(
        Config::DEFAULT_OVERLAY_UNIQUE_SYSTEM_COLORS);
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

    while (m_characterHotkeysLayout->count() > 1) {
      QLayoutItem *item = m_characterHotkeysLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
    updateCharacterHotkeysScrollHeight();

    while (m_cycleGroupsLayout->count() > 1) {
      QLayoutItem *item = m_cycleGroupsLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
    updateCycleGroupsScrollHeight();

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

    while (m_neverMinimizeLayout->count() > 1) {
      QLayoutItem *item = m_neverMinimizeLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
    updateNeverMinimizeScrollHeight();

    m_rememberPositionsCheck->setChecked(Config::DEFAULT_POSITION_REMEMBER);
    m_enableSnappingCheck->setChecked(Config::DEFAULT_POSITION_ENABLE_SNAPPING);
    m_snapDistanceSpin->setValue(Config::DEFAULT_POSITION_SNAP_DISTANCE);
    m_lockPositionsCheck->setChecked(Config::DEFAULT_POSITION_LOCK);

    m_showNotLoggedInClientsCheck->setChecked(
        Config::DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN);
    m_notLoggedInStackModeCombo->setCurrentIndex(
        Config::DEFAULT_THUMBNAIL_NOT_LOGGED_IN_STACK_MODE);
    m_showNotLoggedInOverlayCheck->setChecked(
        Config::DEFAULT_THUMBNAIL_SHOW_NOT_LOGGED_IN_OVERLAY);

    QMessageBox::information(
        this, "Reset Complete",
        "Behavior settings have been reset to defaults.\n\n"
        "Click Apply or OK to save the changes.");
  }
}

void ConfigDialog::onResetNonEVEDefaults() {
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Reset Non-EVE Thumbnail Settings");
  msgBox.setText(
      "Are you sure you want to reset all non-EVE thumbnail settings to "
      "their default values?");
  msgBox.setInformativeText("This will reset additional application settings.");
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox.setDefaultButton(QMessageBox::No);
  msgBox.setStyleSheet(StyleSheet::getMessageBoxStyleSheet());

  if (msgBox.exec() == QMessageBox::Yes) {
    m_showNonEVEOverlayCheck->setChecked(
        Config::DEFAULT_THUMBNAIL_SHOW_NON_EVE_OVERLAY);

    while (m_processNamesLayout->count() > 1) {
      QLayoutItem *item = m_processNamesLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
    updateProcessNamesScrollHeight();

    QMessageBox::information(
        this, "Reset Complete",
        "Non-EVE thumbnail settings have been reset to defaults.\n\n"
        "Click Apply or OK to save the changes.");
  }
}

void ConfigDialog::onResetCombatMessagesDefaults() {
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Reset Combat Log Events Settings");
  msgBox.setText("Are you sure you want to reset all combat log events "
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
      else if (eventType == "convo_request")
        checkbox = m_combatEventConvoRequestCheck;
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

      if (m_eventSoundCheckBoxes.contains(eventType)) {
        m_eventSoundCheckBoxes[eventType]->setChecked(
            Config::DEFAULT_COMBAT_SOUND_ENABLED);
      }

      if (m_eventSoundFileButtons.contains(eventType)) {
        m_eventSoundFileButtons[eventType]->setText("Browse...");
        Config::instance().setCombatEventSoundFile(eventType, QString());
      }

      if (m_eventSoundVolumeSliders.contains(eventType)) {
        m_eventSoundVolumeSliders[eventType]->setValue(
            Config::DEFAULT_COMBAT_SOUND_VOLUME);
      }

      if (m_eventSoundVolumeValueLabels.contains(eventType)) {
        m_eventSoundVolumeValueLabels[eventType]->setText(
            QString("%1%").arg(Config::DEFAULT_COMBAT_SOUND_VOLUME));
      }
    }

    m_miningTimeoutSpin->setValue(Config::DEFAULT_MINING_TIMEOUT_SECONDS);

    QMessageBox::information(
        this, "Reset Complete",
        "Combat log events settings have been reset to defaults.\n\n"
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
            if (x != -1 && y != -1) {
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
  } else {
    // Expand environment variables for the file dialog to work correctly
    currentPath = Config::instance().chatLogDirectory();
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
  } else {
    // Expand environment variables for the file dialog to work correctly
    currentPath = Config::instance().gameLogDirectory();
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

        if (x != -1 && y != -1) {
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
    while (m_cycleGroupsLayout->count() > 1) {
      QLayoutItem *item = m_cycleGroupsLayout->takeAt(0);
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }

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

      int forwardVkCode = 0;
      int backwardVkCode = 0;

      if (!forwardHotkey.isEmpty()) {
        forwardVkCode = legacyKeyToVirtualKey(forwardHotkey);
      }

      if (!backwardHotkey.isEmpty()) {
        backwardVkCode = legacyKeyToVirtualKey(backwardHotkey);
      }

      QString characters = characterList.join(", ");
      QWidget *formRow = createCycleGroupFormRow(
          QString("Cycle Group %1").arg(i), backwardVkCode, 0, forwardVkCode, 0,
          characters, false, false);

      int count = m_cycleGroupsLayout->count();
      m_cycleGroupsLayout->insertWidget(count - 1, formRow);
    }

    updateCycleGroupsScrollHeight();

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
            QWidget *formRow =
                createCharacterHotkeyFormRow(characterName, vkCode, 0);
            int count = m_characterHotkeysLayout->count();
            m_characterHotkeysLayout->insertWidget(count - 1, formRow);
          }
        }
      }

      updateCharacterHotkeysScrollHeight();
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

void ConfigDialog::updateTableVisibility(QTableWidget *table) {
  if (!table)
    return;

  bool isEmpty = (table->rowCount() == 0);
  table->setVisible(!isEmpty);
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

        QVector<HotkeyCombination> combinations =
            m_profileHotkeyCapture->getHotkeys();

        if (combinations.isEmpty()) {
          Config::instance().clearProfileHotkey(currentProfile);
          HotkeyManager::instance()->setProfileHotkeys(
              currentProfile, QVector<HotkeyBinding>());
          onHotkeyChanged();
          return;
        }

        QVector<HotkeyBinding> bindings;
        for (const HotkeyCombination &combo : combinations) {
          bindings.append(HotkeyBinding{combo.keyCode, combo.ctrl, combo.alt,
                                        combo.shift, true});
        }

        Config::instance().setProfileHotkeys(currentProfile, bindings);
        HotkeyManager::instance()->setProfileHotkeys(currentProfile, bindings);

        onHotkeyChanged();
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
  }
}

void ConfigDialog::updateProfileDropdown() {
  if (!m_profileCombo) {
    return;
  }

  m_profileCombo->blockSignals(true);

  m_profileCombo->clear();

  QStringList profiles = Config::instance().listProfiles();
  QString currentProfile = Config::instance().getCurrentProfileName();

  if (profiles.isEmpty()) {
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
                                      "/profiles/settings.global.ini"),
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

  if (Config::instance().loadProfile(profileName)) {
    HotkeyManager::instance()->loadFromConfig();

    loadSettings();

    updateProfileDropdown();

    if (m_profileHotkeyCapture) {
      QVector<HotkeyBinding> hotkeys =
          Config::instance().getProfileHotkeys(profileName);

      if (hotkeys.isEmpty()) {
        m_profileHotkeyCapture->clearHotkey();
      } else {
        QVector<HotkeyCombination> combinations;
        for (const HotkeyBinding &binding : hotkeys) {
          HotkeyCombination combo;
          combo.keyCode = binding.keyCode;
          combo.ctrl = binding.ctrl;
          combo.alt = binding.alt;
          combo.shift = binding.shift;
          combinations.append(combo);
        }
        m_profileHotkeyCapture->setHotkeys(combinations);
      }
    }

    emit settingsApplied();
  } else {
    QMessageBox::warning(
        this, "Profile Switch Failed",
        QString("Failed to switch to profile: %1").arg(profileName));
  }
}

void ConfigDialog::onExternalProfileSwitch(const QString &profileName) {
  if (Config::instance().getCurrentProfileName() == profileName) {
    loadSettings();

    updateProfileDropdown();

    if (m_profileHotkeyCapture) {
      QVector<HotkeyBinding> hotkeys =
          Config::instance().getProfileHotkeys(profileName);

      if (hotkeys.isEmpty()) {
        m_profileHotkeyCapture->clearHotkey();
      } else {
        QVector<HotkeyCombination> combinations;
        for (const HotkeyBinding &binding : hotkeys) {
          HotkeyCombination combo;
          combo.keyCode = binding.keyCode;
          combo.ctrl = binding.ctrl;
          combo.alt = binding.alt;
          combo.shift = binding.shift;
          combinations.append(combo);
        }
        m_profileHotkeyCapture->setHotkeys(combinations);
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
          this, [this, reply](const QList<QSslError> &) {});

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

QVector<ConfigDialog::HotkeyConflict> ConfigDialog::checkHotkeyConflicts() {
  QVector<HotkeyConflict> conflicts;

  QHash<HotkeyBinding, QStringList> hotkeyMap;

  auto addHotkeysFromCapture = [&hotkeyMap](HotkeyCapture *capture,
                                            const QString &description) {
    if (!capture) {
      return;
    }

    QVector<HotkeyCombination> hotkeys = capture->getHotkeys();
    for (const HotkeyCombination &hk : hotkeys) {
      if (hk.keyCode != 0) {
        HotkeyBinding binding(hk.keyCode, hk.ctrl, hk.alt, hk.shift, true);
        hotkeyMap[binding].append(description);
      }
    }
  };

  QStringList characterHotkeys;
  int charFormRowCount = m_characterHotkeysLayout->count() - 1;
  for (int i = 0; i < charFormRowCount; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_characterHotkeysLayout->itemAt(i)->widget());

    if (!rowWidget) {
      continue;
    }

    QLineEdit *nameEdit = rowWidget->findChild<QLineEdit *>();
    HotkeyCapture *hotkeyCapture = rowWidget->findChild<HotkeyCapture *>();

    if (nameEdit && hotkeyCapture) {
      QString charName = nameEdit->text().trimmed();
      QString desc = charName.isEmpty()
                         ? QString("Character: (unnamed row %1)").arg(i + 1)
                         : QString("Character: %1").arg(charName);

      if (!charName.isEmpty()) {
        characterHotkeys.append(desc);
      }
      addHotkeysFromCapture(hotkeyCapture, desc);
    }
  }

  addHotkeysFromCapture(m_suspendHotkeyCapture, "Suspend Hotkeys");

  int cycleFormRowCount = m_cycleGroupsLayout->count() - 1;
  for (int i = 0; i < cycleFormRowCount; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_cycleGroupsLayout->itemAt(i)->widget());

    if (!rowWidget) {
      continue;
    }

    QList<QLineEdit *> lineEdits = rowWidget->findChildren<QLineEdit *>();
    QList<HotkeyCapture *> hotkeyCaptures =
        rowWidget->findChildren<HotkeyCapture *>();

    if (!lineEdits.isEmpty() && hotkeyCaptures.size() >= 2) {
      QLineEdit *nameEdit = lineEdits[0];
      HotkeyCapture *backwardCapture = hotkeyCaptures[0];
      HotkeyCapture *forwardCapture = hotkeyCaptures[1];

      QString groupName = nameEdit->text().trimmed();
      QString backwardDesc =
          groupName.isEmpty()
              ? QString("Cycle Group (unnamed row %1): Cycle Backward")
                    .arg(i + 1)
              : QString("Group '%1': Cycle Backward").arg(groupName);
      QString forwardDesc =
          groupName.isEmpty()
              ? QString("Cycle Group (unnamed row %1): Cycle Forward")
                    .arg(i + 1)
              : QString("Group '%1': Cycle Forward").arg(groupName);

      addHotkeysFromCapture(backwardCapture, backwardDesc);
      addHotkeysFromCapture(forwardCapture, forwardDesc);
    }
  }

  addHotkeysFromCapture(m_notLoggedInForwardCapture,
                        "Not-Logged-In: Cycle Forward");
  addHotkeysFromCapture(m_notLoggedInBackwardCapture,
                        "Not-Logged-In: Cycle Backward");

  addHotkeysFromCapture(m_nonEVEForwardCapture, "Non-EVE: Cycle Forward");
  addHotkeysFromCapture(m_nonEVEBackwardCapture, "Non-EVE: Cycle Backward");

  addHotkeysFromCapture(m_closeAllClientsCapture, "Close All Clients");

  if (m_profileHotkeyCapture &&
      !m_profileHotkeyCapture->getHotkeys().isEmpty()) {
    QString profileName =
        m_profileCombo ? m_profileCombo->currentText() : "Current Profile";
    addHotkeysFromCapture(m_profileHotkeyCapture,
                          QString("Profile Switch: %1").arg(profileName));
  }

  for (auto it = hotkeyMap.constBegin(); it != hotkeyMap.constEnd(); ++it) {
    const HotkeyBinding &binding = it.key();
    const QStringList &descriptions = it.value();

    if (descriptions.size() > 1) {
      bool allCharacterHotkeys = true;
      for (const QString &desc : descriptions) {
        if (!desc.startsWith("Character: ")) {
          allCharacterHotkeys = false;
          break;
        }
      }

      if (!allCharacterHotkeys) {
        for (int i = 0; i < descriptions.size(); ++i) {
          for (int j = i + 1; j < descriptions.size(); ++j) {
            HotkeyConflict conflict;
            conflict.existingName = descriptions[i];
            conflict.conflictingName = descriptions[j];
            conflict.binding = binding;
            conflicts.append(conflict);
          }
        }
      }
    }
  }

  return conflicts;
}
void ConfigDialog::updateHotkeyConflictVisuals() {
  clearHotkeyConflictVisuals();

  QVector<HotkeyConflict> conflicts = checkHotkeyConflicts();

  m_conflictingHotkeys.clear();
  for (const HotkeyConflict &conflict : conflicts) {
    m_conflictingHotkeys.insert(conflict.binding);
  }

  auto markIfConflicting = [this](HotkeyCapture *capture) {
    if (!capture) {
      return;
    }

    QVector<HotkeyCombination> hotkeys = capture->getHotkeys();

    bool hasValidHotkey = false;
    for (const HotkeyCombination &hk : hotkeys) {
      if (hk.keyCode != 0) {
        hasValidHotkey = true;
        break;
      }
    }

    if (!hasValidHotkey) {
      capture->setHasConflict(false);
      return;
    }

    bool hasConflict = false;
    for (const HotkeyCombination &hk : hotkeys) {
      if (hk.keyCode != 0) {
        HotkeyBinding binding(hk.keyCode, hk.ctrl, hk.alt, hk.shift, true);
        if (m_conflictingHotkeys.contains(binding)) {
          hasConflict = true;
          break;
        }
      }
    }

    capture->setHasConflict(hasConflict);
  };

  for (int i = 0; i < m_characterHotkeysLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_characterHotkeysLayout->itemAt(i)->widget());
    HotkeyCapture *capture =
        rowWidget ? rowWidget->findChild<HotkeyCapture *>() : nullptr;
    markIfConflicting(capture);
  }

  for (int i = 0; i < m_cycleGroupsLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_cycleGroupsLayout->itemAt(i)->widget());
    if (!rowWidget) {
      continue;
    }

    QList<HotkeyCapture *> hotkeyCaptures =
        rowWidget->findChildren<HotkeyCapture *>();
    if (hotkeyCaptures.size() >= 2) {
      markIfConflicting(hotkeyCaptures[0]);
      markIfConflicting(hotkeyCaptures[1]);
    }
  }

  markIfConflicting(m_suspendHotkeyCapture);
  markIfConflicting(m_notLoggedInForwardCapture);
  markIfConflicting(m_notLoggedInBackwardCapture);
  markIfConflicting(m_nonEVEForwardCapture);
  markIfConflicting(m_nonEVEBackwardCapture);
  markIfConflicting(m_closeAllClientsCapture);
  markIfConflicting(m_profileHotkeyCapture);

  bool hasConflicts = !conflicts.isEmpty();
  if (m_okButton) {
    m_okButton->setEnabled(!hasConflicts);
  }
  if (m_applyButton) {
    m_applyButton->setEnabled(!hasConflicts);
  }
}

void ConfigDialog::clearHotkeyConflictVisuals() {
  auto clearConflict = [](HotkeyCapture *capture) {
    if (capture) {
      capture->setHasConflict(false);
    }
  };

  for (int i = 0; i < m_characterHotkeysLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_characterHotkeysLayout->itemAt(i)->widget());
    HotkeyCapture *capture =
        rowWidget ? rowWidget->findChild<HotkeyCapture *>() : nullptr;
    clearConflict(capture);
  }

  for (int i = 0; i < m_cycleGroupsLayout->count() - 1; ++i) {
    QWidget *rowWidget =
        qobject_cast<QWidget *>(m_cycleGroupsLayout->itemAt(i)->widget());
    if (!rowWidget) {
      continue;
    }

    QList<HotkeyCapture *> hotkeyCaptures =
        rowWidget->findChildren<HotkeyCapture *>();
    if (hotkeyCaptures.size() >= 2) {
      clearConflict(hotkeyCaptures[0]);
      clearConflict(hotkeyCaptures[1]);
    }
  }

  clearConflict(m_suspendHotkeyCapture);
  clearConflict(m_notLoggedInForwardCapture);
  clearConflict(m_notLoggedInBackwardCapture);
  clearConflict(m_nonEVEForwardCapture);
  clearConflict(m_nonEVEBackwardCapture);
  clearConflict(m_closeAllClientsCapture);
  clearConflict(m_profileHotkeyCapture);

  m_conflictingHotkeys.clear();
}

void ConfigDialog::showConflictDialog(
    const QVector<HotkeyConflict> &conflicts) {
  if (conflicts.isEmpty()) {
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle("Hotkey Conflicts Detected");
  dialog.setMinimumWidth(500);

  QVBoxLayout *layout = new QVBoxLayout(&dialog);

  QLabel *headerLabel = new QLabel(
      QString("<b>Cannot save settings: %1 hotkey conflict(s) detected.</b>")
          .arg(conflicts.size()));
  headerLabel->setWordWrap(true);
  layout->addWidget(headerLabel);

  QLabel *infoLabel =
      new QLabel("The following hotkeys are assigned to multiple functions. "
                 "Character hotkeys can share the same key with each other, "
                 "but cannot conflict with any other hotkey type.");
  infoLabel->setWordWrap(true);
  layout->addWidget(infoLabel);

  layout->addSpacing(10);

  QTableWidget *conflictTable = new QTableWidget(conflicts.size(), 3);
  conflictTable->setHorizontalHeaderLabels(
      {"Hotkey", "Function 1", "Function 2"});
  conflictTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  conflictTable->horizontalHeader()->setSectionResizeMode(1,
                                                          QHeaderView::Stretch);
  conflictTable->horizontalHeader()->setSectionResizeMode(2,
                                                          QHeaderView::Stretch);
  conflictTable->verticalHeader()->setVisible(false);
  conflictTable->setSelectionMode(QAbstractItemView::NoSelection);
  conflictTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  conflictTable->setMaximumHeight(300);
  conflictTable->setFocusPolicy(Qt::NoFocus);
  conflictTable->setStyleSheet(StyleSheet::getTableStyleSheet());

  for (int i = 0; i < conflicts.size(); ++i) {
    const HotkeyConflict &conflict = conflicts[i];

    QString hotkeyStr = conflict.binding.toString();
    QTableWidgetItem *hotkeyItem = new QTableWidgetItem(hotkeyStr);
    hotkeyItem->setForeground(QBrush(QColor("#e74c3c")));
    hotkeyItem->setFont(QFont("Consolas", 9, QFont::Bold));
    conflictTable->setItem(i, 0, hotkeyItem);

    QTableWidgetItem *func1Item = new QTableWidgetItem(conflict.existingName);
    conflictTable->setItem(i, 1, func1Item);

    QTableWidgetItem *func2Item =
        new QTableWidgetItem(conflict.conflictingName);
    conflictTable->setItem(i, 2, func2Item);
  }

  layout->addWidget(conflictTable);

  layout->addSpacing(10);

  QLabel *instructionLabel = new QLabel(
      "<i>Please change the conflicting hotkeys (marked with red borders) "
      "and try again.</i>");
  instructionLabel->setWordWrap(true);
  layout->addWidget(instructionLabel);

  layout->addSpacing(10);

  QPushButton *okButton = new QPushButton("OK");
  okButton->setDefault(true);
  connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);

  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  buttonLayout->addWidget(okButton);
  layout->addLayout(buttonLayout);

  dialog.exec();
}

void ConfigDialog::onHotkeyChanged() { validateAllHotkeys(); }

void ConfigDialog::validateAllHotkeys() { updateHotkeyConflictVisuals(); }
