#include "overlayinfo.h"
#include <QDebug>
#include <QFontMetrics>
#include <cmath>

QHash<QString, QString> OverlayInfo::s_characterNameCache;

QString OverlayInfo::extractCharacterName(const QString &windowTitle) {
  auto it = s_characterNameCache.constFind(windowTitle);
  if (it != s_characterNameCache.constEnd()) {
    return it.value();
  }

  QString characterName;
  static const QString prefix = QStringLiteral("EVE - ");
  if (windowTitle.startsWith(prefix)) {
    characterName = windowTitle.mid(prefix.length());
  }

  if (s_characterNameCache.size() > 100) {
    s_characterNameCache.clear();
  }

  s_characterNameCache.insert(windowTitle, characterName);
  return characterName;
}

QString OverlayInfo::extractSystemName(const QString &windowTitle) {
  return QString();
}

QColor OverlayInfo::generateUniqueColor(const QString &systemName) {
  if (systemName.isEmpty()) {
    return Qt::white;
  }

  uint hash = qHash(systemName);

  const double goldenRatioConjugate = 0.618033988749895;
  double h = fmod(static_cast<double>(hash) * goldenRatioConjugate, 1.0);
  int hue = static_cast<int>(h * 360.0);

  int saturation = 200 + ((hash >> 8) % 36);
  int value = 210 + ((hash >> 16) % 26);

  QColor color;
  color.setHsv(hue, saturation, value);

  qDebug() << "generateUniqueColor for" << systemName << "- Hash:" << hash
           << "HSV(" << hue << "," << saturation << "," << value << ")"
           << "RGB:" << color.name();

  return color;
}

QString OverlayInfo::truncateText(const QString &text, const QFont &font,
                                  int maxWidth) {
  QFontMetrics metrics(font);

  if (metrics.horizontalAdvance(text) <= maxWidth) {
    return text;
  }

  QString truncated = text;
  while (!truncated.isEmpty() &&
         metrics.horizontalAdvance(truncated) > maxWidth) {
    truncated.chop(1);
  }

  return truncated;
}

QRect OverlayInfo::calculateTextRect(const QRect &thumbnailRect,
                                     OverlayPosition position,
                                     const QString &text, const QFont &font) {
  QFontMetrics metrics(font);

  int padding = 5;
  int maxAvailableWidth = thumbnailRect.width() - (2 * padding);

  QString displayText = truncateText(text, font, maxAvailableWidth);

  int textWidth = metrics.horizontalAdvance(displayText);
  int textHeight = metrics.height();

  int x = padding;
  int y = padding;

  switch (position) {
  case OverlayPosition::TopLeft:
    x = padding;
    y = padding + textHeight;
    break;
  case OverlayPosition::TopCenter:
    x = (thumbnailRect.width() - textWidth) / 2;
    y = padding + textHeight;
    break;
  case OverlayPosition::TopRight:
    x = thumbnailRect.width() - textWidth - padding;
    y = padding + textHeight;
    break;
  case OverlayPosition::CenterLeft:
    x = padding;
    y = (thumbnailRect.height() + textHeight) / 2;
    break;
  case OverlayPosition::Center:
    x = (thumbnailRect.width() - textWidth) / 2;
    y = (thumbnailRect.height() + textHeight) / 2;
    break;
  case OverlayPosition::CenterRight:
    x = thumbnailRect.width() - textWidth - padding;
    y = (thumbnailRect.height() + textHeight) / 2;
    break;
  case OverlayPosition::BottomLeft:
    x = padding;
    y = thumbnailRect.height() - padding;
    break;
  case OverlayPosition::BottomCenter:
    x = (thumbnailRect.width() - textWidth) / 2;
    y = thumbnailRect.height() - padding;
    break;
  case OverlayPosition::BottomRight:
    x = thumbnailRect.width() - textWidth - padding;
    y = thumbnailRect.height() - padding;
    break;
  }

  return QRect(x, y - textHeight, textWidth, textHeight);
}

void OverlayInfo::clearCache() { s_characterNameCache.clear(); }
