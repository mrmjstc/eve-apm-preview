#ifndef OVERLAYINFO_H
#define OVERLAYINFO_H

#include <QColor>
#include <QFont>
#include <QHash>
#include <QRect>
#include <QString>

enum class OverlayPosition {
  TopLeft,
  TopCenter,
  TopRight,
  CenterLeft,
  Center,
  CenterRight,
  BottomLeft,
  BottomCenter,
  BottomRight
};

struct OverlayElement {
  QString text;
  QColor color;
  QFont font;
  OverlayPosition position;
  bool enabled;
  mutable QString cachedTruncatedText;
  mutable int cachedMaxWidth = -1;

  OverlayElement(const QString &text = "", const QColor &color = Qt::white,
                 OverlayPosition pos = OverlayPosition::TopLeft,
                 bool enabled = true, const QFont &customFont = QFont())
      : text(text), color(color), font(customFont), position(pos),
        enabled(enabled) {
    if (font.family().isEmpty()) {
      font.setFamily("Segoe UI");
      font.setPointSize(10);
    }
    font.setBold(true);
  }
};

class OverlayInfo {
public:
  static QString extractCharacterName(const QString &windowTitle);
  static QString extractSystemName(const QString &windowTitle);

  static QRect calculateTextRect(const QRect &thumbnailRect,
                                 OverlayPosition position, const QString &text,
                                 const QFont &font);

  static QString truncateText(const QString &text, const QFont &font,
                              int maxWidth);

  static QColor generateUniqueColor(const QString &systemName);

  static void clearCache();

private:
  static QHash<QString, QString> s_characterNameCache;
};

#endif
