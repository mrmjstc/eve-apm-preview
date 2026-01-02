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

  // Hash the system name for deterministic color generation
  uint hash = qHash(systemName);

  // Use golden ratio conjugate for better distribution across hue spectrum
  // This spreads colors more evenly than simple modulo
  const double goldenRatioConjugate = 0.618033988749895;
  double h = fmod(static_cast<double>(hash) * goldenRatioConjugate, 1.0);
  int hue = static_cast<int>(h * 360.0); // 0-360 degrees

  // Use different parts of hash for saturation and value
  int saturation =
      200 + ((hash >> 8) % 36); // 200-255 (very saturated, vibrant)
  int value =
      210 + ((hash >> 16) % 26); // 210-255 (very bright, no dark colors)

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

  int textWidth = metrics.horizontalAdvance(displayText) + 2;
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
