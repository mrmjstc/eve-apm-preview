#include "stylesheet.h"

QString StyleSheet::colorBackground() { return "#1e1e1e"; }
QString StyleSheet::colorBackgroundLight() { return "#2b2b2b"; }
QString StyleSheet::colorBackgroundDark() { return "#353535"; }
QString StyleSheet::colorSection() { return "#252525"; }
QString StyleSheet::colorBorder() { return "#555555"; }
QString StyleSheet::colorAccent() { return "#fdcc12"; }
QString StyleSheet::colorTextPrimary() { return "#ffffff"; }
QString StyleSheet::colorTextSecondary() { return "#cccccc"; }
QString StyleSheet::colorTextInfo() { return "#888888"; }

QString StyleSheet::getDialogStyleSheet() {
  return QString(
             "QDialog {"
             "   background-color: %1;"
             "}"
             "QLabel {"
             "   color: %2;"
             "   font-size: 13px;"
             "}"
             "QCheckBox {"
             "   color: %2;"
             "   font-size: 13px;"
             "   spacing: 8px;"
             "}"
             "QCheckBox::indicator {"
             "   width: 18px;"
             "   height: 18px;"
             "   border: 2px solid %3;"
             "   border-radius: 3px;"
             "   background-color: %4;"
             "}"
             "QCheckBox::indicator:checked {"
             "   background-color: %5;"
             "   border-color: %5;"
             "}"
             "QSpinBox, QDoubleSpinBox, QComboBox {"
             "   background-color: %4;"
             "   color: %6;"
             "   border: 1px solid %3;"
             "   padding: 6px;"
             "   border-radius: 4px;"
             "   font-size: 13px;"
             "}"
             "QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {"
             "   border: 1px solid %5;"
             "}"
             "QSpinBox::up-button, QDoubleSpinBox::up-button {"
             "   background-color: #404040;"
             "   border: none;"
             "   border-left: 1px solid %3;"
             "   border-top-right-radius: 4px;"
             "   width: 16px;"
             "}"
             "QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover {"
             "   background-color: %5;"
             "}"
             "QSpinBox::down-button, QDoubleSpinBox::down-button {"
             "   background-color: #404040;"
             "   border: none;"
             "   border-left: 1px solid %3;"
             "   border-bottom-right-radius: 4px;"
             "   width: 16px;"
             "}"
             "QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {"
             "   background-color: %5;"
             "}"
             "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
             "   image: none;"
             "   border: 2px solid %2;"
             "   border-bottom: none;"
             "   border-left: none;"
             "   width: 4px;"
             "   height: 4px;"
             "   margin: 0px 6px 2px 0px;"
             "}"
             "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
             "   image: none;"
             "   border: 2px solid %2;"
             "   border-top: none;"
             "   border-right: none;"
             "   width: 4px;"
             "   height: 4px;"
             "   margin: 2px 6px 0px 0px;"
             "}"
             "QComboBox::drop-down {"
             "   border: none;"
             "   width: 20px;"
             "}"
             "QComboBox QAbstractItemView {"
             "   background-color: %4;"
             "   color: %6;"
             "   selection-background-color: %5;"
             "   border: 1px solid %3;"
             "}"
             "QGroupBox {"
             "   color: %6;"
             "   font-size: 14px;"
             "   font-weight: bold;"
             "   border: 1px solid #404040;"
             "   border-radius: 6px;"
             "   margin-top: 12px;"
             "   padding-top: 16px;"
             "}"
             "QGroupBox::title {"
             "   subcontrol-origin: margin;"
             "   subcontrol-position: top left;"
             "   padding: 0 8px;"
             "   left: 10px;"
             "}"
             "QScrollBar:vertical {"
             "   background-color: %4;"
             "   width: 12px;"
             "   border: none;"
             "   border-radius: 6px;"
             "   margin: 0px;"
             "}"
             "QScrollBar::handle:vertical {"
             "   background-color: %3;"
             "   border-radius: 6px;"
             "   min-height: 30px;"
             "}"
             "QScrollBar::handle:vertical:hover {"
             "   background-color: %5;"
             "}"
             "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
             "   height: 0px;"
             "   border: none;"
             "}"
             "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
             "   background: none;"
             "}"
             "QScrollBar:horizontal {"
             "   background-color: %4;"
             "   height: 12px;"
             "   border: none;"
             "   border-radius: 6px;"
             "   margin: 0px;"
             "}"
             "QScrollBar::handle:horizontal {"
             "   background-color: %3;"
             "   border-radius: 6px;"
             "   min-width: 30px;"
             "}"
             "QScrollBar::handle:horizontal:hover {"
             "   background-color: %5;"
             "}"
             "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal "
             "{"
             "   width: 0px;"
             "   border: none;"
             "}"
             "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal "
             "{"
             "   background: none;"
             "}")
      .arg(colorBackground())
      .arg(colorTextSecondary())
      .arg(colorBorder())
      .arg(colorBackgroundLight())
      .arg(colorAccent())
      .arg(colorTextPrimary());
}

QString StyleSheet::getCategoryListStyleSheet() {
  return QString("QListWidget {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   border: none;"
                 "   font-size: 13px;"
                 "   padding: 10px 0px;"
                 "   outline: none;"
                 "}"
                 "QListWidget::item {"
                 "   padding: 12px 20px;"
                 "   border-left: 3px solid transparent;"
                 "   outline: none;"
                 "}"
                 "QListWidget::item:selected {"
                 "   background-color: #3d3d3d;"
                 "   border-left: 3px solid %3;"
                 "   outline: none;"
                 "}"
                 "QListWidget::item:hover {"
                 "   background-color: %4;"
                 "}"
                 "QListWidget::item:focus {"
                 "   outline: none;"
                 "}")
      .arg(colorBackgroundLight())
      .arg(colorTextPrimary())
      .arg(colorAccent())
      .arg(colorBackgroundDark());
}

QString StyleSheet::getSearchBoxStyleSheet() {
  return QString("QLineEdit {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   border: 1px solid %3;"
                 "   padding: 6px 10px;"
                 "   border-radius: 4px;"
                 "   font-size: 12px;"
                 "   margin: 10px 10px 10px 10px;"
                 "}"
                 "QLineEdit:focus {"
                 "   border: 1px solid %4;"
                 "}")
      .arg(colorBackgroundLight())
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent());
}

QString StyleSheet::getButtonStyleSheet() {
  return QString("QPushButton {"
                 "   background-color: #404040;"
                 "   color: %1;"
                 "   border: 1px solid %2;"
                 "   padding: 8px 24px;"
                 "   border-radius: 4px;"
                 "   font-size: 13px;"
                 "   min-width: 80px;"
                 "}"
                 "QPushButton:hover {"
                 "   background-color: #4a4a4a;"
                 "   border: 1px solid %3;"
                 "}"
                 "QPushButton:pressed {"
                 "   background-color: %4;"
                 "}"
                 "QPushButton:disabled {"
                 "   background-color: #1a1a1a;"
                 "   color: #666666;"
                 "   border: 1px solid #333333;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent())
      .arg(colorBackgroundDark());
}

QString StyleSheet::getHotkeyButtonStyleSheet() {
  return QString("QPushButton {"
                 "   background-color: #404040;"
                 "   color: %1;"
                 "   border: 1px solid %2;"
                 "   padding: 6px 16px;"
                 "   border-radius: 4px;"
                 "   font-size: 12px;"
                 "   min-width: 60px;"
                 "}"
                 "QPushButton:hover {"
                 "   background-color: #4a4a4a;"
                 "   border: 1px solid %3;"
                 "}"
                 "QPushButton:pressed {"
                 "   background-color: %4;"
                 "}"
                 "QPushButton:disabled {"
                 "   background-color: #2b2b2b;"
                 "   color: #666666;"
                 "   border: 1px solid #3a3a3a;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent())
      .arg(colorBackgroundDark());
}

QString StyleSheet::getScrollAreaStyleSheet() {
  return "QScrollArea { background-color: transparent; border: none; }";
}

QString StyleSheet::getSectionStyleSheet() {
  return QString("QWidget { background-color: %1; border-radius: 6px; }")
      .arg(colorSection());
}

QString StyleSheet::getSectionHeaderStyleSheet() {
  return QString("font-size: 15px; "
                 "font-weight: bold; "
                 "color: %1; "
                 "border-left: 3px solid %2; "
                 "padding-left: 8px;")
      .arg(colorTextPrimary())
      .arg(colorAccent());
}

QString StyleSheet::getTitleLabelStyleSheet() {
  return QString("font-size: 20px; "
                 "font-weight: bold; "
                 "color: %1; "
                 "margin-bottom: 10px;")
      .arg(colorTextPrimary());
}

QString StyleSheet::getLabelStyleSheet() {
  return QString("QLabel {"
                 "   color: %1;"
                 "   padding-left: 4px;"
                 "}"
                 "QLabel:disabled {"
                 "   color: #666666;"
                 "}")
      .arg(colorTextPrimary());
}

QString StyleSheet::getInfoLabelStyleSheet() {
  return QString("color: %1; "
                 "font-size: 11px; "
                 "font-style: italic;")
      .arg(colorTextInfo());
}

QString StyleSheet::getCheckBoxStyleSheet() {
  return QString("QCheckBox {"
                 "   color: %1;"
                 "   font-size: 13px;"
                 "}"
                 "QCheckBox:disabled {"
                 "   color: #666666;"
                 "}"
                 "QCheckBox::indicator {"
                 "   width: 18px;"
                 "   height: 18px;"
                 "   border: 2px solid %2;"
                 "   border-radius: 3px;"
                 "   background-color: %3;"
                 "}"
                 "QCheckBox::indicator:disabled {"
                 "   background-color: #1a1a1a;"
                 "   border-color: #333333;"
                 "}"
                 "QCheckBox::indicator:checked {"
                 "   background-color: %4;"
                 "   border-color: %4;"
                 "}"
                 "QCheckBox::indicator:checked:disabled {"
                 "   background-color: #555555;"
                 "   border-color: #555555;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorBackgroundLight())
      .arg(colorAccent());
}

QString StyleSheet::getSpinBoxStyleSheet() {
  return QString(
             "QSpinBox, QDoubleSpinBox {"
             "   background-color: %1;"
             "   color: %2;"
             "   border: 1px solid %3;"
             "   padding: 5px 6px;"
             "   border-radius: 4px;"
             "   font-size: 12px;"
             "}"
             "QSpinBox:focus, QDoubleSpinBox:focus {"
             "   border: 1px solid %4;"
             "}"
             "QSpinBox:disabled, QDoubleSpinBox:disabled {"
             "   background-color: #1a1a1a;"
             "   color: #666666;"
             "   border: 1px solid #333333;"
             "}"
             "QSpinBox::up-button, QDoubleSpinBox::up-button {"
             "   background-color: #404040;"
             "   border: none;"
             "   border-left: 1px solid %3;"
             "   border-top-right-radius: 4px;"
             "   width: 16px;"
             "}"
             "QSpinBox::up-button:disabled, QDoubleSpinBox::up-button:disabled "
             "{"
             "   background-color: #1a1a1a;"
             "   border-left: 1px solid #333333;"
             "}"
             "QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover {"
             "   background-color: %4;"
             "}"
             "QSpinBox::down-button, QDoubleSpinBox::down-button {"
             "   background-color: #404040;"
             "   border: none;"
             "   border-left: 1px solid %3;"
             "   border-bottom-right-radius: 4px;"
             "   width: 16px;"
             "}"
             "QSpinBox::down-button:disabled, "
             "QDoubleSpinBox::down-button:disabled {"
             "   background-color: #1a1a1a;"
             "   border-left: 1px solid #333333;"
             "}"
             "QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {"
             "   background-color: %4;"
             "}"
             "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
             "   image: none;"
             "   border: 2px solid %2;"
             "   border-bottom: none;"
             "   border-left: none;"
             "   width: 4px;"
             "   height: 4px;"
             "   margin: 0px 6px 2px 0px;"
             "}"
             "QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled {"
             "   border-color: #666666;"
             "}"
             "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
             "   image: none;"
             "   border: 2px solid %2;"
             "   border-top: none;"
             "   border-right: none;"
             "   width: 4px;"
             "   height: 4px;"
             "   margin: 2px 6px 0px 0px;"
             "}"
             "QSpinBox::down-arrow:disabled, "
             "QDoubleSpinBox::down-arrow:disabled {"
             "   border-color: #666666;"
             "}")
      .arg(colorBackgroundLight())
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent());
}

QString StyleSheet::getComboBoxStyleSheet() {
  return QString("QComboBox {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   border: 1px solid %3;"
                 "   padding: 6px;"
                 "   border-radius: 4px;"
                 "   font-size: 12px;"
                 "}"
                 "QComboBox:focus {"
                 "   border: 1px solid %4;"
                 "}"
                 "QComboBox:disabled {"
                 "   background-color: #1a1a1a;"
                 "   color: #666666;"
                 "   border: 1px solid #333333;"
                 "}"
                 "QComboBox::drop-down {"
                 "   subcontrol-origin: padding;"
                 "   subcontrol-position: top right;"
                 "   width: 25px;"
                 "   border-left: 1px solid %3;"
                 "   background-color: %1;"
                 "   border-top-right-radius: 4px;"
                 "   border-bottom-right-radius: 4px;"
                 "}"
                 "QComboBox::drop-down:disabled {"
                 "   background-color: #1a1a1a;"
                 "   border-left: 1px solid #333333;"
                 "}"
                 "QComboBox::down-arrow {"
                 "   image: none;"
                 "   border-left: 4px solid transparent;"
                 "   border-right: 4px solid transparent;"
                 "   border-top: 6px solid %2;"
                 "   width: 0px;"
                 "   height: 0px;"
                 "   margin-right: 5px;"
                 "}"
                 "QComboBox::down-arrow:disabled {"
                 "   border-top: 6px solid #666666;"
                 "}"
                 "QComboBox::down-arrow:hover {"
                 "   border-top: 6px solid %4;"
                 "}"
                 "QComboBox QAbstractItemView {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   selection-background-color: %4;"
                 "   border: 1px solid %3;"
                 "   min-width: 150px;"
                 "   max-width: 400px;"
                 "}")
      .arg(colorBackgroundLight())
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent());
}

QString StyleSheet::getTableStyleSheet() {
  return QString("QHeaderView::section {"
                 "   font-weight: bold;"
                 "   border: 1px solid %1;"
                 "   border-top: none;"
                 "   border-left: none;"
                 "}"
                 "QTableWidget {"
                 "   outline: none;"
                 "   gridline-color: %1;"
                 "}"
                 "QTableWidget::item:focus {"
                 "   outline: none;"
                 "   border: none;"
                 "}")
      .arg(colorBorder());
}

QString StyleSheet::getHotkeyCaptureStyleSheet() {
  return QString("QLineEdit {"
                 "   background-color: transparent;"
                 "   color: %1;"
                 "   border: none;"
                 "   padding: 2px 4px;"
                 "   font-size: 12px;"
                 "}"
                 "QLineEdit:focus {"
                 "   background-color: %2;"
                 "}"
                 "QLineEdit:hover {"
                 "   background-color: %2;"
                 "}"
                 "QLineEdit[hasConflict=\"true\"] {"
                 "   border: 1px solid #e74c3c !important;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBackgroundDark());
}

QString StyleSheet::getHotkeyCaptureStandaloneStyleSheet() {
  return QString("QLineEdit {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   border: 1px solid %3;"
                 "   padding: 6px;"
                 "   border-radius: 4px;"
                 "   font-size: 13px;"
                 "}"
                 "QLineEdit:focus {"
                 "   border: 1px solid %4;"
                 "}"
                 "QLineEdit:hover {"
                 "   background-color: %5;"
                 "}"
                 "QLineEdit[hasConflict=\"true\"] {"
                 "   border: 1px solid #e74c3c !important;"
                 "}")
      .arg(colorBackgroundLight())
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent())
      .arg(colorBackgroundDark());
}

QString StyleSheet::getRightPanelStyleSheet() {
  return QString("background-color: %1;").arg(colorBackground());
}

QString StyleSheet::getStackedWidgetStyleSheet() {
  return "QStackedWidget { background-color: transparent; }";
}

QString StyleSheet::getAspectRatioButtonStyleSheet() {
  return QString("QPushButton {"
                 "   background-color: #404040;"
                 "   color: %1;"
                 "   border: 1px solid %2;"
                 "   border-radius: 4px;"
                 "   font-size: 11px;"
                 "   padding: 2px;"
                 "}"
                 "QPushButton:hover {"
                 "   background-color: %3;"
                 "   border-color: %3;"
                 "}"
                 "QPushButton:pressed {"
                 "   background-color: #6844dd;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent());
}

QString StyleSheet::getSpinBoxWithDisabledStyleSheet() {
  return QString("QSpinBox {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   border: 1px solid %3;"
                 "}"
                 "QSpinBox:disabled {"
                 "   background-color: #1a1a1a;"
                 "   color: #666666;"
                 "   border: 1px solid #333333;"
                 "}")
      .arg(colorBackgroundLight())
      .arg(colorTextPrimary())
      .arg(colorBorder());
}

QString StyleSheet::getComboBoxWithDisabledStyleSheet() {
  return QString("QComboBox {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   border: 1px solid %3;"
                 "   padding: 6px;"
                 "   border-radius: 4px;"
                 "   font-size: 12px;"
                 "}"
                 "QComboBox:focus {"
                 "   border: 1px solid %4;"
                 "}"
                 "QComboBox:disabled {"
                 "   background-color: #1a1a1a;"
                 "   color: #666666;"
                 "   border: 1px solid #333333;"
                 "}"
                 "QComboBox::drop-down {"
                 "   subcontrol-origin: padding;"
                 "   subcontrol-position: top right;"
                 "   width: 25px;"
                 "   border-left: 1px solid %3;"
                 "   background-color: %1;"
                 "   border-top-right-radius: 4px;"
                 "   border-bottom-right-radius: 4px;"
                 "}"
                 "QComboBox::drop-down:disabled {"
                 "   background-color: #1a1a1a;"
                 "   border-left: 1px solid #333333;"
                 "}"
                 "QComboBox::down-arrow {"
                 "   image: none;"
                 "   border-left: 4px solid transparent;"
                 "   border-right: 4px solid transparent;"
                 "   border-top: 6px solid %2;"
                 "   width: 0px;"
                 "   height: 0px;"
                 "   margin-right: 5px;"
                 "}"
                 "QComboBox::down-arrow:disabled {"
                 "   border-top: 6px solid #666666;"
                 "}"
                 "QComboBox::down-arrow:hover {"
                 "   border-top: 6px solid %4;"
                 "}"
                 "QComboBox QAbstractItemView {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   selection-background-color: %4;"
                 "   border: 1px solid %3;"
                 "   min-width: 150px;"
                 "   max-width: 400px;"
                 "}")
      .arg(colorBackgroundLight())
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent());
}

QString StyleSheet::getResetButtonStyleSheet() {
  return QString("QPushButton {"
                 "   background-color: #404040;"
                 "   color: %1;"
                 "   border: 1px solid %2;"
                 "   padding: 8px 24px;"
                 "   border-radius: 4px;"
                 "   font-size: 12px;"
                 "}"
                 "QPushButton:hover {"
                 "   background-color: #d9534f;"
                 "   border: 1px solid #d43f3a;"
                 "}"
                 "QPushButton:pressed {"
                 "   background-color: #c9302c;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBorder());
}

QString StyleSheet::getTableCellEditorStyleSheet() {
  return QString("QLineEdit {"
                 "   background-color: transparent;"
                 "   color: %1;"
                 "   border: none;"
                 "   padding: 2px 4px;"
                 "   font-size: 12px;"
                 "}"
                 "QLineEdit:focus {"
                 "   background-color: %2;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBackgroundDark());
}

QString StyleSheet::getMessageBoxStyleSheet() {
  return QString("QMessageBox {"
                 "   background-color: %1;"
                 "}"
                 "QLabel {"
                 "   color: %2;"
                 "}"
                 "QPushButton {"
                 "   background-color: #404040;"
                 "   color: %3;"
                 "   border: 1px solid %4;"
                 "   padding: 6px 16px;"
                 "   border-radius: 4px;"
                 "}"
                 "QPushButton:hover {"
                 "   background-color: #4a4a4a;"
                 "   border: 1px solid %5;"
                 "}")
      .arg(colorBackground())
      .arg(colorTextSecondary())
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent());
}

QString StyleSheet::getAboutTitleStyleSheet() {
  return QString("font-size: 28px; font-weight: bold; color: %1; "
                 "margin-bottom: 5px;")
      .arg(colorAccent());
}

QString StyleSheet::getVersionLabelStyleSheet() {
  return QString("font-size: 14px; color: %1;").arg(colorTextSecondary());
}

QString StyleSheet::getDescriptionLabelStyleSheet() {
  return QString("font-size: 13px; color: %1; line-height: 1.5;")
      .arg(colorTextSecondary());
}

QString StyleSheet::getFeatureLabelStyleSheet() {
  return QString("color: %1; font-size: 12px; padding: 2px 0px;")
      .arg(colorTextSecondary());
}

QString StyleSheet::getTechLabelStyleSheet() {
  return QString("color: %1; font-size: 12px;").arg(colorTextSecondary());
}

QString StyleSheet::getCopyrightLabelStyleSheet() {
  return QString("font-size: 11px; color: %1;").arg(colorTextInfo());
}

QString StyleSheet::getIndentedCheckBoxStyleSheet() {
  return QString("color: %1; font-size: 13px; margin-left: 24px;")
      .arg(colorTextPrimary());
}

QString StyleSheet::getSubLabelStyleSheet() {
  return QString("color: %1; font-size: 13px; margin-top: 10px;")
      .arg(colorTextSecondary());
}

QString StyleSheet::getDialogInfoLabelStyleSheet() {
  return QString("color: %1; font-size: 13px; margin-bottom: 8px;")
      .arg(colorTextSecondary());
}

QString StyleSheet::getSubsectionHeaderStyleSheet() {
  return QString("font-size: 16px; font-weight: bold; color: %1; "
                 "border-left: 3px solid %2; padding-left: 8px;")
      .arg(colorTextPrimary())
      .arg(colorAccent());
}

QString StyleSheet::getNeverMinimizeTableStyleSheet() { return QString(""); }

QString StyleSheet::getSecondaryButtonStyleSheet() {
  return QString("QPushButton {"
                 "   background-color: #404040;"
                 "   color: %1;"
                 "   border: 1px solid %2;"
                 "   padding: 7px 16px;"
                 "   border-radius: 4px;"
                 "   font-size: 12px;"
                 "}"
                 "QPushButton:hover {"
                 "   background-color: #4a4a4a;"
                 "   border: 1px solid %3;"
                 "}"
                 "QPushButton:pressed {"
                 "   background-color: %4;"
                 "}"
                 "QPushButton:disabled {"
                 "   background-color: #1a1a1a;"
                 "   color: #666666;"
                 "   border: 1px solid #333333;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent())
      .arg(colorBackgroundDark());
}

QString StyleSheet::getDialogListStyleSheet() {
  return QString("QListWidget {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   border: 1px solid %3;"
                 "   border-radius: 4px;"
                 "   padding: 4px;"
                 "   outline: none;"
                 "}"
                 "QListWidget::item {"
                 "   padding: 8px;"
                 "   border-radius: 3px;"
                 "   outline: none;"
                 "}"
                 "QListWidget::item:selected {"
                 "   background-color: #505050;"
                 "   color: %2;"
                 "   outline: none;"
                 "}"
                 "QListWidget::item:hover {"
                 "   background-color: #404040;"
                 "}"
                 "QListWidget::item:focus {"
                 "   outline: none;"
                 "}")
      .arg(colorSection())
      .arg(colorTextPrimary())
      .arg(colorBorder());
}

QString StyleSheet::getDialogLineEditStyleSheet() {
  return QString("QLineEdit {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   border: 1px solid %3;"
                 "   border-radius: 4px;"
                 "   padding: 6px;"
                 "   font-size: 13px;"
                 "   selection-background-color: %4;"
                 "   selection-color: #000000;"
                 "}"
                 "QLineEdit:focus {"
                 "   border: 1px solid %4;"
                 "   outline: none;"
                 "}"
                 "QLineEdit:disabled {"
                 "   background-color: #1a1a1a;"
                 "   color: #666666;"
                 "   border: 1px solid #333333;"
                 "}")
      .arg(colorBackgroundLight())
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent());
}

QString StyleSheet::getDialogCheckBoxStyleSheet() {
  return QString("QCheckBox {"
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
                 "PHN2ZyB3aWR0aD0iMTIiIGhlaWdodD0iMTAiIHhtbG5zPSJodHRwOi8vd3d3L"
                 "nczLm9yZy8yMDAwL3N2ZyI+"
                 "PHBhdGggZD0iTTEgNUw0IDhMMTEgMSIgc3Ryb2tlPSIjZmZmZmZmIiBzdHJva"
                 "2Utd2lkdGg9IjIiIGZpbGw9Im5vbmUiLz48L3N2Zz4=);"
                 "}"
                 "QCheckBox::indicator:checked:hover {"
                 "   background-color: %2;"
                 "   border: 2px solid %2;"
                 "}"
                 "QCheckBox::indicator:checked:focus {"
                 "   background-color: %2;"
                 "   border: 2px solid %2;"
                 "}")
      .arg(colorBorder())
      .arg(colorAccent());
}

QString StyleSheet::getDialogButtonStyleSheet() {
  return QString("QPushButton {"
                 "   background-color: #404040;"
                 "   color: %1;"
                 "   border: 1px solid %2;"
                 "   padding: 8px 24px;"
                 "   border-radius: 4px;"
                 "   font-size: 13px;"
                 "}"
                 "QPushButton:hover {"
                 "   background-color: #4a4a4a;"
                 "   border: 1px solid %3;"
                 "}"
                 "QPushButton:pressed {"
                 "   background-color: %4;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent())
      .arg(colorBackgroundDark());
}

QString StyleSheet::getTableCellButtonStyleSheet() {
  return QString("QPushButton {"
                 "   background-color: #404040;"
                 "   color: %1;"
                 "   border: 1px solid %2;"
                 "   padding: 0px 12px;"
                 "   border-radius: 4px;"
                 "   font-size: 12px;"
                 "   text-align: center;"
                 "}"
                 "QPushButton:hover {"
                 "   background-color: #4a4a4a;"
                 "   border: 1px solid %3;"
                 "}"
                 "QPushButton:pressed {"
                 "   background-color: %4;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent())
      .arg(colorBackgroundDark());
}

QString StyleSheet::getDialogStyleSheetForWidget() {
  return QString("QDialog {"
                 "   background-color: %1;"
                 "}"
                 "QLabel {"
                 "   color: %2;"
                 "   font-size: 13px;"
                 "}")
      .arg(colorBackground())
      .arg(colorTextSecondary());
}

QString StyleSheet::getColorButtonStyleSheet(const QString &backgroundColor,
                                             const QString &textColor) {
  return QString("QPushButton {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   border: 1px solid %3;"
                 "   border-radius: 4px;"
                 "   font-size: 12px;"
                 "   font-weight: bold;"
                 "   font-family: 'Consolas', 'Courier New', monospace;"
                 "}"
                 "QPushButton:hover {"
                 "   border: 1px solid %4;"
                 "}"
                 "QPushButton:pressed {"
                 "   border: 1px solid %5;"
                 "}"
                 "QPushButton:disabled {"
                 "   background-color: #1a1a1a;"
                 "   color: #666666;"
                 "   border: 1px solid #333333;"
                 "}")
      .arg(backgroundColor)
      .arg(textColor)
      .arg(colorBorder())
      .arg(colorAccent())
      .arg(colorTextPrimary());
}

QString StyleSheet::getProfileToolbarStyleSheet() {
  return QString("QWidget {"
                 "   background-color: %1;"
                 "   border-bottom: 1px solid %2;"
                 "}")
      .arg(colorSection())
      .arg("#3c3c3c");
}

QString StyleSheet::getProfileLabelStyleSheet() {
  return QString("QLabel {"
                 "   color: %1;"
                 "   font-weight: normal;"
                 "   background: transparent;"
                 "   border: none;"
                 "}")
      .arg(colorTextSecondary());
}

QString StyleSheet::getProfileComboBoxStyleSheet() {
  return QString("QComboBox {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   border: 1px solid %3;"
                 "   padding: 6px;"
                 "   border-radius: 4px;"
                 "   font-size: 13px;"
                 "   min-width: 150px;"
                 "}"
                 "QComboBox:focus {"
                 "   border: 1px solid %4;"
                 "}"
                 "QComboBox::drop-down {"
                 "   border: none;"
                 "   width: 20px;"
                 "}"
                 "QComboBox QAbstractItemView {"
                 "   background-color: %1;"
                 "   color: %2;"
                 "   selection-background-color: %4;"
                 "   border: 1px solid %3;"
                 "}")
      .arg(colorBackgroundLight())
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent());
}

QString StyleSheet::getProfileButtonStyleSheet() {
  return QString("QPushButton {"
                 "   background-color: #404040;"
                 "   color: %1;"
                 "   border: 1px solid %2;"
                 "   border-radius: 4px;"
                 "   padding: 6px 14px;"
                 "   font-size: 9pt;"
                 "   min-width: 60px;"
                 "}"
                 "QPushButton:hover {"
                 "   background-color: #4a4a4a;"
                 "   border: 1px solid %3;"
                 "}"
                 "QPushButton:pressed {"
                 "   background-color: %4;"
                 "}"
                 "QPushButton:disabled {"
                 "   background-color: #404040;"
                 "   color: %5;"
                 "   border-color: #333333;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorAccent())
      .arg(colorBackgroundDark())
      .arg(colorTextInfo());
}

QString StyleSheet::getProfileDeleteButtonStyleSheet() {
  return QString("QPushButton {"
                 "   background-color: #404040;"
                 "   color: %1;"
                 "   border: 1px solid %2;"
                 "   border-radius: 4px;"
                 "   padding: 6px 14px;"
                 "   font-size: 9pt;"
                 "   min-width: 60px;"
                 "}"
                 "QPushButton:hover {"
                 "   background-color: #4a4a4a;"
                 "   border: 1px solid #c42b1c;"
                 "}"
                 "QPushButton:pressed {"
                 "   background-color: %3;"
                 "}"
                 "QPushButton:disabled {"
                 "   background-color: #404040;"
                 "   color: %4;"
                 "   border-color: #333333;"
                 "}")
      .arg(colorTextPrimary())
      .arg(colorBorder())
      .arg(colorBackgroundDark())
      .arg(colorTextInfo());
}

QString StyleSheet::getProfileSeparatorStyleSheet() {
  return QString("QFrame {"
                 "   background-color: #3c3c3c;"
                 "   max-width: 1px;"
                 "}");
}
