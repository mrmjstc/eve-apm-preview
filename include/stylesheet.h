#ifndef STYLESHEET_H
#define STYLESHEET_H

#include <QString>

class StyleSheet {
public:
  static QString getDialogStyleSheet();

  static QString getCategoryListStyleSheet();
  static QString getSearchBoxStyleSheet();
  static QString getButtonStyleSheet();
  static QString getHotkeyButtonStyleSheet();
  static QString getScrollAreaStyleSheet();
  static QString getSectionStyleSheet();
  static QString getSectionHeaderStyleSheet();
  static QString getSectionSubHeaderStyleSheet();
  static QString getTitleLabelStyleSheet();
  static QString getLabelStyleSheet();
  static QString getInfoLabelStyleSheet();
  static QString getCheckBoxStyleSheet();
  static QString getSpinBoxStyleSheet();
  static QString getSpinBoxWithDisabledStyleSheet();
  static QString getComboBoxStyleSheet();
  static QString getComboBoxWithDisabledStyleSheet();
  static QString getTableStyleSheet();
  static QString getHotkeyCaptureStyleSheet();
  static QString getHotkeyCaptureStandaloneStyleSheet();
  static QString getRightPanelStyleSheet();
  static QString getStackedWidgetStyleSheet();
  static QString getAspectRatioButtonStyleSheet();
  static QString getResetButtonStyleSheet();
  static QString getTableCellEditorStyleSheet();
  static QString getMessageBoxStyleSheet();
  static QString getAboutTitleStyleSheet();
  static QString getVersionLabelStyleSheet();
  static QString getDescriptionLabelStyleSheet();
  static QString getFeatureLabelStyleSheet();
  static QString getTechLabelStyleSheet();
  static QString getCopyrightLabelStyleSheet();
  static QString getIndentedCheckBoxStyleSheet();

  static QString getProfileToolbarStyleSheet();
  static QString getProfileLabelStyleSheet();
  static QString getProfileComboBoxStyleSheet();
  static QString getProfileButtonStyleSheet();
  static QString getProfileDeleteButtonStyleSheet();
  static QString getProfileSeparatorStyleSheet();
  static QString getSubLabelStyleSheet();
  static QString getDialogInfoLabelStyleSheet();
  static QString getSubsectionHeaderStyleSheet();
  static QString getNeverMinimizeTableStyleSheet();
  static QString getSecondaryButtonStyleSheet();
  static QString getDialogListStyleSheet();
  static QString getDialogLineEditStyleSheet();
  static QString getDialogCheckBoxStyleSheet();
  static QString getDialogButtonStyleSheet();
  static QString getTableCellButtonStyleSheet();
  static QString getDialogStyleSheetForWidget();
  static QString getColorButtonStyleSheet(const QString &backgroundColor,
                                          const QString &textColor);
  static QString getTabWidgetStyleSheet();

  static QString colorBackground();
  static QString colorBackgroundLight();
  static QString colorBackgroundDark();
  static QString colorSection();
  static QString colorBorder();
  static QString colorAccent();
  static QString colorAccentSecondary();
  static QString colorTextPrimary();
  static QString colorTextSecondary();
  static QString colorTextInfo();

private:
  StyleSheet() = delete;
};

#endif
