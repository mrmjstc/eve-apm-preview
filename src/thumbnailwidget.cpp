#include "thumbnailwidget.h"
#include "config.h"
#include <QApplication>
#include <QDebug>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QScreen>
#include <QtMath>
#include <cmath>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

ThumbnailWidget::ThumbnailWidget(quintptr windowId, const QString &title,
                                 QWidget *parent)
    : QWidget(parent), m_windowId(windowId), m_title(title) {
  setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
  setAttribute(Qt::WA_TranslucentBackground, false);
  setAttribute(Qt::WA_NoSystemBackground, false);

  setCursor(Qt::PointingHandCursor);
  setProperty("windowTitle", title);

  m_overlayWidget = new OverlayWidget(this);
  m_overlayWidget->setGeometry(rect());
  m_overlayWidget->hide();

  updateOverlays();

  m_updateTimer = new QTimer(this);
  connect(m_updateTimer, &QTimer::timeout, this,
          &ThumbnailWidget::updateDwmThumbnail);

  m_updateTimer->start(60000);
}

ThumbnailWidget::~ThumbnailWidget() { cleanupDwmThumbnail(); }

void ThumbnailWidget::setTitle(const QString &title) {
  if (m_title == title) {
    return;
  }

  m_title = title;
  setProperty("windowTitle", title);
  updateOverlays();
}

void ThumbnailWidget::setCharacterName(const QString &characterName) {
  if (m_characterName == characterName) {
    return;
  }

  m_characterName = characterName;
  updateOverlays();
  if (m_overlayWidget) {
    QString displayName =
        m_customName.isEmpty() ? m_characterName : m_customName;
    m_overlayWidget->setCharacterName(displayName);
  }
}

void ThumbnailWidget::setCustomName(const QString &customName) {
  if (m_customName == customName) {
    return;
  }

  m_customName = customName;
  updateOverlays();
  if (m_overlayWidget) {
    QString displayName =
        m_customName.isEmpty() ? m_characterName : m_customName;
    m_overlayWidget->setCharacterName(displayName);
  }
}

void ThumbnailWidget::setSystemName(const QString &systemName) {
  if (m_systemName == systemName) {
    return;
  }

  m_systemName = systemName;

  const Config &cfg = Config::instance();

  QColor customColor = cfg.getSystemNameColor(m_systemName);
  if (customColor.isValid()) {
    m_cachedSystemColor = customColor;
  } else if (cfg.useUniqueSystemNameColors()) {
    m_cachedSystemColor = OverlayInfo::generateUniqueColor(m_systemName);
  } else {
    m_cachedSystemColor = cfg.systemNameColor();
  }

  updateOverlays();
  if (m_overlayWidget) {
    m_overlayWidget->setSystemName(systemName);
  }
}

void ThumbnailWidget::refreshSystemColor() {
  const Config &cfg = Config::instance();
  if (!m_systemName.isEmpty()) {
    QColor customColor = cfg.getSystemNameColor(m_systemName);
    if (customColor.isValid()) {
      m_cachedSystemColor = customColor;
    } else if (cfg.useUniqueSystemNameColors()) {
      m_cachedSystemColor = OverlayInfo::generateUniqueColor(m_systemName);
    } else {
      m_cachedSystemColor = cfg.systemNameColor();
    }
    updateOverlays();
  }
}

void ThumbnailWidget::setCombatMessage(const QString &message,
                                       const QString &eventType) {
  if (message.isEmpty()) {
    return;
  }

  // Check if this exact event is already active
  for (const auto &event : m_combatEvents) {
    if (event.message == message && event.eventType == eventType) {
      return; // Don't add duplicates
    }
  }

  // Create a new timer for this event
  QTimer *timer = new QTimer(this);
  timer->setSingleShot(true);

  // Add the event to the list
  m_combatEvents.append(CombatEvent(message, eventType, timer));

  // Connect the timer to remove this specific event when it expires
  connect(timer, &QTimer::timeout, this, [this, timer]() {
    // Find and remove the event associated with this timer
    for (int i = 0; i < m_combatEvents.size(); ++i) {
      if (m_combatEvents[i].timer == timer) {
        m_combatEvents[i].timer->deleteLater();
        m_combatEvents.removeAt(i);
        break;
      }
    }

    updateOverlays();

    // Update the overlay widget with current active event types
    if (m_overlayWidget) {
      m_overlayWidget->setCombatEventTypes(getActiveCombatEventTypes());
    }
  });

  // Start the timer with the event's configured duration
  const Config &cfg = Config::instance();
  int duration = cfg.combatEventDuration(eventType);
  timer->start(duration);

  updateOverlays();

  // Update the overlay widget with current active event types
  if (m_overlayWidget) {
    m_overlayWidget->setCombatEventTypes(getActiveCombatEventTypes());
  }
}

void ThumbnailWidget::setActive(bool active) {
  m_isActive = active;
  if (m_overlayWidget) {
    m_overlayWidget->setActiveState(active);
  }
}

void ThumbnailWidget::closeImmediately() {
  if (m_overlayWidget) {
    m_overlayWidget->setUpdatesEnabled(false);
    m_overlayWidget->close();
    m_overlayWidget->hide();
  }

  if (m_updateTimer) {
    m_updateTimer->stop();
  }

  // Stop and clean up all combat event timers
  for (auto &event : m_combatEvents) {
    if (event.timer) {
      event.timer->stop();
      event.timer->deleteLater();
    }
  }
  m_combatEvents.clear();

  setUpdatesEnabled(false);
  close();
}

void ThumbnailWidget::updateOverlays() {
  const Config &cfg = Config::instance();
  m_overlays.clear();

  if (cfg.showCharacterName()) {
    QString displayName =
        m_customName.isEmpty() ? m_characterName : m_customName;
    if (!displayName.isEmpty()) {
      OverlayPosition pos =
          static_cast<OverlayPosition>(cfg.characterNamePosition());
      QFont characterFont = cfg.characterNameFont();
      characterFont.setBold(true);
      OverlayElement charElement(
          displayName, cfg.characterNameColor(), pos, true, characterFont,
          cfg.characterNameOffsetX(), cfg.characterNameOffsetY());
      m_overlays.append(charElement);
    }
  }

  if (cfg.showSystemName()) {
    if (!m_systemName.isEmpty()) {
      OverlayPosition pos =
          static_cast<OverlayPosition>(cfg.systemNamePosition());
      QFont systemFont = cfg.systemNameFont();
      systemFont.setBold(true);

      OverlayElement sysElement(m_systemName, m_cachedSystemColor, pos, true,
                                systemFont, cfg.systemNameOffsetX(),
                                cfg.systemNameOffsetY());
      m_overlays.append(sysElement);
    }
  }

  // Add all active combat events
  if (cfg.showCombatMessages()) {
    OverlayPosition pos =
        static_cast<OverlayPosition>(cfg.combatMessagePosition());
    QFont combatFont = cfg.combatMessageFont();

    for (const auto &event : m_combatEvents) {
      QColor messageColor = cfg.combatEventColor(event.eventType);
      OverlayElement combatElement(event.message, messageColor, pos, true,
                                   combatFont, cfg.combatMessageOffsetX(),
                                   cfg.combatMessageOffsetY());
      m_overlays.append(combatElement);
    }
  }

  updateOverlayWidget();
}

void ThumbnailWidget::updateOverlayWidget() {
  if (m_overlayWidget) {
    m_overlayWidget->setOverlays(m_overlays);
    m_overlayWidget->update();
  }
}

void ThumbnailWidget::forceOverlayRender() {
  if (m_overlayWidget) {
    m_overlayWidget->invalidateCache();
    m_overlayWidget->update();
  }
}

QPoint ThumbnailWidget::snapPosition(
    const QPoint &pos, const QVector<ThumbnailWidget *> &otherThumbnails) {
  const Config &cfg = Config::instance();
  if (!cfg.enableSnapping()) {
    return pos;
  }

  int snapDist = cfg.snapDistance();
  QPoint snappedPos = pos;

  QScreen *screen = QApplication::screenAt(pos);
  if (!screen) {
    screen = QApplication::primaryScreen();
  }
  QRect screenGeom = screen->availableGeometry();

  int closestXDist = snapDist;
  int closestYDist = snapDist;
  int snappedX = pos.x();
  int snappedY = pos.y();

  QRect thisRect(pos.x(), pos.y(), width(), height());

  int distToLeft = qAbs(pos.x() - screenGeom.left());
  int distToRight = qAbs(pos.x() + width() - screenGeom.right());

  if (distToLeft < closestXDist) {
    closestXDist = distToLeft;
    snappedX = screenGeom.left();
  }
  if (distToRight < closestXDist) {
    closestXDist = distToRight;
    snappedX = screenGeom.right() - width();
  }

  int distToTop = qAbs(pos.y() - screenGeom.top());
  int distToBottom = qAbs(pos.y() + height() - screenGeom.bottom());

  if (distToTop < closestYDist) {
    closestYDist = distToTop;
    snappedY = screenGeom.top();
  }
  if (distToBottom < closestYDist) {
    closestYDist = distToBottom;
    snappedY = screenGeom.bottom() - height();
  }

  for (const ThumbnailWidget *other : otherThumbnails) {
    if (other == this || !other->isVisible()) {
      continue;
    }

    QRect otherRect = other->geometry();

    QRect expandedThisRect =
        thisRect.adjusted(-snapDist, -snapDist, snapDist, snapDist);
    if (!expandedThisRect.intersects(otherRect)) {
      continue;
    }

    bool verticalOverlap = !(thisRect.bottom() < otherRect.top() - snapDist ||
                             thisRect.top() > otherRect.bottom() + snapDist);

    if (verticalOverlap) {
      int targetX = otherRect.right() + 1;
      int distLeftToRight = qAbs(thisRect.left() - targetX);
      if (distLeftToRight <= closestXDist) {
        closestXDist = distLeftToRight;
        snappedX = targetX;
      }

      targetX = otherRect.left() - width();
      int distRightToLeft = qAbs(thisRect.left() - targetX);
      if (distRightToLeft <= closestXDist) {
        closestXDist = distRightToLeft;
        snappedX = targetX;
      }
    }

    bool horizontalOverlap = !(thisRect.right() < otherRect.left() - snapDist ||
                               thisRect.left() > otherRect.right() + snapDist);

    if (horizontalOverlap) {
      int targetY = otherRect.bottom() + 1;
      int distTopToBottom = qAbs(thisRect.top() - targetY);
      if (distTopToBottom <= closestYDist) {
        closestYDist = distTopToBottom;
        snappedY = targetY;
      }

      targetY = otherRect.top() - height();
      int distBottomToTop = qAbs(thisRect.top() - targetY);
      if (distBottomToTop <= closestYDist) {
        closestYDist = distBottomToTop;
        snappedY = targetY;
      }
    }

    QRect potentialRect(snappedX, pos.y(), width(), height());
    bool isSideBySide =
        (potentialRect.right() + 1 == otherRect.left() ||
         potentialRect.left() == otherRect.right() + 1 ||
         qAbs(potentialRect.right() - (otherRect.left() - 1)) <= 1 ||
         qAbs(potentialRect.left() - (otherRect.right() + 1)) <= 1);

    if (isSideBySide) {
      int distTopToTop = qAbs(thisRect.top() - otherRect.top());
      if (distTopToTop <= closestYDist) {
        closestYDist = distTopToTop;
        snappedY = otherRect.top();
      }

      int distBottomToBottom = qAbs(thisRect.bottom() - otherRect.bottom());
      if (distBottomToBottom <= closestYDist) {
        closestYDist = distBottomToBottom;
        snappedY = otherRect.bottom() - height() + 1;
      }
    }

    potentialRect = QRect(pos.x(), snappedY, width(), height());
    bool isStackedVertically =
        (potentialRect.bottom() + 1 == otherRect.top() ||
         potentialRect.top() == otherRect.bottom() + 1 ||
         qAbs(potentialRect.bottom() - (otherRect.top() - 1)) <= 1 ||
         qAbs(potentialRect.top() - (otherRect.bottom() + 1)) <= 1);

    if (isStackedVertically) {
      int distLeftToLeft = qAbs(thisRect.left() - otherRect.left());
      if (distLeftToLeft <= closestXDist) {
        closestXDist = distLeftToLeft;
        snappedX = otherRect.left();
      }

      int distRightToRight = qAbs(thisRect.right() - otherRect.right());
      if (distRightToRight <= closestXDist) {
        closestXDist = distRightToRight;
        snappedX = otherRect.right() - width() + 1;
      }
    }
  }

  snappedPos.setX(snappedX);
  snappedPos.setY(snappedY);

  return snappedPos;
}

void ThumbnailWidget::paintEvent(QPaintEvent *) {
  if (m_windowId == 0) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QLinearGradient gradient(0, 0, 0, height());
    gradient.setColorAt(0, QColor(30, 35, 40));
    gradient.setColorAt(1, QColor(20, 25, 30));
    painter.fillRect(rect(), gradient);

    painter.setPen(QColor(255, 255, 255, 30));
    QFont font = painter.font();
    font.setPointSize(24);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(rect(), Qt::AlignCenter, "PREVIEW");
  }
}

void ThumbnailWidget::mousePressEvent(QMouseEvent *event) {
  const Config &cfg = Config::instance();
  if (cfg.lockThumbnailPositions() && !cfg.isConfigDialogOpen()) {
    if (event->button() == Qt::LeftButton) {
      m_isDragging = false;
      // Check if we should switch on mouse down
      if (cfg.switchOnMouseDown()) {
        emit clicked(m_windowId);
      }
    }
    QWidget::mousePressEvent(event);
    return;
  }

  if (event->button() == Qt::LeftButton) {
    if (event->modifiers() & Qt::ShiftModifier) {
      m_dragPosition =
          event->globalPosition().toPoint() - frameGeometry().topLeft();
      m_isGroupDragging = true;
      m_isDragging = true;
      m_groupDragStartPos = pos();
      setCursor(Qt::ClosedHandCursor);

      if (m_updateTimer->isActive()) {
        m_updateTimer->stop();
      }

      if (m_overlayWidget) {
        m_overlayWidget->pauseAnimations();
      }

      emit groupDragStarted(m_windowId);
      event->accept();
      return;
    }

    // Left-click behavior depends on drag button setting
    if (!cfg.useDragWithRightClick()) {
      // Left-click is for dragging (and also clicking if no drag occurs)
      m_dragPosition =
          event->globalPosition().toPoint() - frameGeometry().topLeft();
      m_isDragging = false;
      // If switch on mouse down is enabled, emit clicked immediately
      if (cfg.switchOnMouseDown()) {
        emit clicked(m_windowId);
      }
    } else {
      // Left-click is for switching only
      m_isDragging = false;
      // If switch on mouse down is enabled, emit clicked immediately
      if (cfg.switchOnMouseDown()) {
        emit clicked(m_windowId);
      }
      // Don't set m_dragPosition for clicking
    }
  } else if (event->button() == Qt::RightButton) {
    // Right-click behavior depends on drag button setting
    if (cfg.useDragWithRightClick()) {
      // Right-click is for dragging (traditional) - only set position, not
      // dragging flag yet
      m_dragPosition =
          event->globalPosition().toPoint() - frameGeometry().topLeft();
      m_isDragging = false;
    } else {
      // Right-click is for switching (when left-click is drag button)
      if (cfg.switchOnMouseDown()) {
        emit clicked(m_windowId);
      }
    }
  }
  QWidget::mousePressEvent(event);
}

void ThumbnailWidget::mouseMoveEvent(QMouseEvent *event) {
  const Config &cfg = Config::instance();
  if (cfg.lockThumbnailPositions() && !cfg.isConfigDialogOpen()) {
    QWidget::mouseMoveEvent(event);
    return;
  }

  // Handle group dragging (Shift+drag, always left-click)
  if ((event->buttons() & Qt::LeftButton) && m_isGroupDragging &&
      m_dragPosition != QPoint()) {
    QPoint newPos = event->globalPosition().toPoint() - m_dragPosition;
    newPos = snapPosition(newPos, m_otherThumbnails);
    QPoint delta = newPos - m_groupDragStartPos;

    if (newPos != pos()) {
      move(newPos);
      emit groupDragMoved(m_windowId, delta);
    }
    event->accept();
    return;
  }

  // Handle normal dragging based on configured button
  bool isDraggingWithLeftClick = !cfg.useDragWithRightClick() &&
                                 (event->buttons() & Qt::LeftButton) &&
                                 m_dragPosition != QPoint();
  bool isDraggingWithRightClick = cfg.useDragWithRightClick() &&
                                  (event->buttons() & Qt::RightButton) &&
                                  m_dragPosition != QPoint();

  if (isDraggingWithLeftClick) {
    QPoint delta = event->globalPosition().toPoint() -
                   frameGeometry().topLeft() - m_dragPosition;
    if (!m_isDragging && (qAbs(delta.x()) > 5 || qAbs(delta.y()) > 5)) {
      m_isDragging = true;
      setCursor(Qt::ClosedHandCursor);

      if (m_updateTimer->isActive()) {
        m_updateTimer->stop();
      }

      if (m_overlayWidget) {
        m_overlayWidget->hide();
        m_overlayWidget->pauseAnimations();
      }
    }

    if (m_isDragging) {
      QPoint newPos = event->globalPosition().toPoint() - m_dragPosition;
      newPos = snapPosition(newPos, m_otherThumbnails);

      if (newPos != pos()) {
        move(newPos);

        if (m_overlayWidget) {
          m_overlayWidget->hide();
        }
      }
      event->accept();
      return;
    }
  } else if (isDraggingWithRightClick) {
    QPoint delta = event->globalPosition().toPoint() -
                   frameGeometry().topLeft() - m_dragPosition;
    if (!m_isDragging && (qAbs(delta.x()) > 5 || qAbs(delta.y()) > 5)) {
      m_isDragging = true;
      setCursor(Qt::ClosedHandCursor);

      if (m_updateTimer->isActive()) {
        m_updateTimer->stop();
      }

      if (m_overlayWidget) {
        m_overlayWidget->hide();
        m_overlayWidget->pauseAnimations();
      }
    }

    if (m_isDragging) {
      QPoint newPos = event->globalPosition().toPoint() - m_dragPosition;
      newPos = snapPosition(newPos, m_otherThumbnails);

      if (newPos != pos()) {
        move(newPos);

        if (m_overlayWidget) {
          m_overlayWidget->hide();
        }
      }
      event->accept();
      return;
    }
  }

  QWidget::mouseMoveEvent(event);
}

void ThumbnailWidget::mouseReleaseEvent(QMouseEvent *event) {
  const Config &cfg = Config::instance();

  if (event->button() == Qt::LeftButton) {
    if (!m_isDragging) {
      // Left-click for switching (works for both drag button modes)
      if (!cfg.switchOnMouseDown()) {
        emit clicked(m_windowId);
      }
      // Reset state even for non-drag clicks
      m_dragPosition = QPoint();
      setCursor(Qt::PointingHandCursor);
    } else {
      // Ending a drag operation (left-click drag or group drag)
      if (m_isGroupDragging) {
        emit groupDragEnded(m_windowId);
        m_isGroupDragging = false;
      }

      emit positionChanged(m_windowId, pos());

      if (!m_updateTimer->isActive()) {
        m_updateTimer->start(60000);
        updateDwmThumbnail();
      }

      if (m_overlayWidget) {
        m_overlayWidget->move(pos());
        m_overlayWidget->show();
        m_overlayWidget->raise();
        m_overlayWidget->resumeAnimations();
      }

      m_isDragging = false;
      m_dragPosition = QPoint();
      setCursor(Qt::PointingHandCursor);
    }
  } else if (event->button() == Qt::RightButton) {
    // Right-click behavior depends on drag setting
    if (cfg.useDragWithRightClick() && m_isDragging) {
      // Right-click drag end (when right-click is drag button)
      emit positionChanged(m_windowId, pos());

      if (!m_updateTimer->isActive()) {
        m_updateTimer->start(60000);
        updateDwmThumbnail();
      }

      if (m_overlayWidget) {
        m_overlayWidget->move(pos());
        m_overlayWidget->show();
        m_overlayWidget->raise();
        m_overlayWidget->resumeAnimations();
      }

      m_isDragging = false;
      m_dragPosition = QPoint();
      setCursor(Qt::PointingHandCursor);
    } else if (!cfg.useDragWithRightClick() && !cfg.switchOnMouseDown()) {
      // Right-click for switching (when left-click is drag button)
      emit clicked(m_windowId);
    }
  }
  QWidget::mouseReleaseEvent(event);
}

void ThumbnailWidget::enterEvent(QEnterEvent *event) {
  QWidget::enterEvent(event);
}

void ThumbnailWidget::leaveEvent(QEvent *event) { QWidget::leaveEvent(event); }

void ThumbnailWidget::hideOverlay() {
  if (m_overlayWidget) {
    m_overlayWidget->hide();
  }
}

void ThumbnailWidget::showOverlay() {
  if (m_overlayWidget) {
    m_overlayWidget->move(pos());
    m_overlayWidget->show();
    m_overlayWidget->raise();
  }
}

void ThumbnailWidget::forceUpdate() { updateDwmThumbnail(); }

void ThumbnailWidget::updateWindowFlags(bool alwaysOnTop) {
  Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
  if (alwaysOnTop) {
    flags |= Qt::WindowStaysOnTopHint;
  }
  setWindowFlags(flags);

  if (m_overlayWidget) {
    m_overlayWidget->updateWindowFlags(alwaysOnTop);
  }
}

void ThumbnailWidget::setupDwmThumbnail() {
  if (m_dwmThumbnail) {
    return;
  }

  if (m_windowId == 0) {
    return;
  }

  HWND sourceWindow = reinterpret_cast<HWND>(m_windowId);
  HWND destWindow = reinterpret_cast<HWND>(winId());

  HRESULT hr = DwmRegisterThumbnail(destWindow, sourceWindow, &m_dwmThumbnail);
  if (SUCCEEDED(hr)) {
    updateDwmThumbnail();
  } else {
    qDebug() << "Failed to register DWM thumbnail:" << hr;
  }
}

void ThumbnailWidget::cleanupDwmThumbnail() {
  if (m_dwmThumbnail) {
    DwmUnregisterThumbnail(m_dwmThumbnail);
    m_dwmThumbnail = nullptr;
  }
}

void ThumbnailWidget::updateDwmThumbnail() {
  if (!m_dwmThumbnail) {
    return;
  }

  SIZE sourceSize;
  HRESULT hr = DwmQueryThumbnailSourceSize(m_dwmThumbnail, &sourceSize);
  if (FAILED(hr) || sourceSize.cx <= 0 || sourceSize.cy <= 0) {
    return;
  }

  qreal dpr = devicePixelRatio();

  int physicalWidth = static_cast<int>(width() * dpr);
  int physicalHeight = static_cast<int>(height() * dpr);

  DWM_THUMBNAIL_PROPERTIES props = {};
  props.dwFlags = DWM_TNP_RECTSOURCE | DWM_TNP_RECTDESTINATION |
                  DWM_TNP_VISIBLE | DWM_TNP_OPACITY |
                  DWM_TNP_SOURCECLIENTAREAONLY;
  props.fVisible = TRUE;
  props.opacity = 255;
  props.fSourceClientAreaOnly = TRUE;

  props.rcSource.left = 0;
  props.rcSource.top = 0;
  props.rcSource.right = sourceSize.cx;
  props.rcSource.bottom = sourceSize.cy;

  props.rcDestination.left = 0;
  props.rcDestination.top = 0;
  props.rcDestination.right = physicalWidth;
  props.rcDestination.bottom = physicalHeight;

  DwmUpdateThumbnailProperties(m_dwmThumbnail, &props);
}

void ThumbnailWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);

  if (m_overlayWidget) {
    m_overlayWidget->resize(size());
  }
  updateDwmThumbnail();
}

void ThumbnailWidget::moveEvent(QMoveEvent *event) {
  QWidget::moveEvent(event);

  if (m_overlayWidget && !m_isDragging) {
    m_overlayWidget->move(pos());
  }
}

bool ThumbnailWidget::nativeEvent(const QByteArray &eventType, void *message,
                                  qintptr *result) {
  Q_UNUSED(eventType);
  Q_UNUSED(result);

  MSG *msg = static_cast<MSG *>(message);

  if (msg->message == WM_DPICHANGED) {
    updateDwmThumbnail();

    if (m_overlayWidget) {
      m_overlayWidget->invalidateCache();
    }
  }

  return false;
}

void ThumbnailWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  setupDwmThumbnail();
  if (m_overlayWidget) {
    m_overlayWidget->setGeometry(geometry());
    m_overlayWidget->show();
    m_overlayWidget->raise();
  }
}

void ThumbnailWidget::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  if (m_overlayWidget) {
    m_overlayWidget->hide();
  }
}

QString ThumbnailWidget::getCombatMessage() const {
  if (m_combatEvents.isEmpty()) {
    return QString();
  }
  // Return the most recent message
  return m_combatEvents.last().message;
}

QString ThumbnailWidget::getCombatEventType() const {
  if (m_combatEvents.isEmpty()) {
    return QString();
  }
  // Return the most recent event type
  return m_combatEvents.last().eventType;
}

QStringList ThumbnailWidget::getActiveCombatEventTypes() const {
  QStringList types;
  for (const auto &event : m_combatEvents) {
    if (!types.contains(event.eventType)) {
      types.append(event.eventType);
    }
  }
  return types;
}

OverlayWidget::OverlayWidget(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint) {
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setWindowFlags(windowFlags() | Qt::WindowTransparentForInput);
  setAutoFillBackground(false);

  m_borderAnimationTimer = new QTimer(this);
  m_borderAnimationTimer->setInterval(16);
  connect(m_borderAnimationTimer, &QTimer::timeout, this, [this]() {
    m_animationPhase += 0.5;
    if (m_animationPhase >= 100.0) {
      m_animationPhase = 0.0;
    }
    update();
  });
}

void OverlayWidget::setOverlays(const QVector<OverlayElement> &overlays) {
  bool changed = (m_overlays.size() != overlays.size());
  if (!changed) {
    for (int i = 0; i < overlays.size(); ++i) {
      const OverlayElement &a = m_overlays.at(i);
      const OverlayElement &b = overlays.at(i);
      if (a.text != b.text || a.color != b.color || a.font != b.font ||
          a.position != b.position || a.enabled != b.enabled ||
          a.offsetX != b.offsetX || a.offsetY != b.offsetY) {
        changed = true;
        break;
      }
    }
  }

  if (!changed)
    return;

  m_overlays = overlays;
  m_overlayDirty = true;
  qDebug()
      << "OverlayWidget::setOverlays - overlays changed, marking dirty (count="
      << m_overlays.size() << ")";
  update();
}

void OverlayWidget::setActiveState(bool active) {
  if (m_isActive == active) {
    return;
  }
  m_isActive = active;

  if (active) {
    const Config &cfg = Config::instance();
    BorderStyle style = cfg.activeBorderStyle();
    bool needsAnimation =
        (style == BorderStyle::Dashed || style == BorderStyle::Neon ||
         style == BorderStyle::Shimmer || style == BorderStyle::ElectricArc ||
         style == BorderStyle::Rainbow || style == BorderStyle::BreathingGlow);
    if (needsAnimation && !m_borderAnimationTimer->isActive()) {
      m_animationPhase = 0.0;
      m_borderAnimationTimer->start();
    }
  } else {
    // Check if inactive border needs animation
    const Config &cfg = Config::instance();
    QColor inactiveCharacterColor =
        cfg.getCharacterInactiveBorderColor(m_characterName);
    bool hasCustomInactiveColor = inactiveCharacterColor.isValid();
    bool showGlobalInactiveBorder = cfg.showInactiveBorders();

    bool needsInactiveAnimation = false;
    if (hasCustomInactiveColor || showGlobalInactiveBorder) {
      BorderStyle inactiveStyle = cfg.inactiveBorderStyle();
      needsInactiveAnimation = (inactiveStyle == BorderStyle::Dashed ||
                                inactiveStyle == BorderStyle::Neon ||
                                inactiveStyle == BorderStyle::Shimmer ||
                                inactiveStyle == BorderStyle::ElectricArc ||
                                inactiveStyle == BorderStyle::Rainbow ||
                                inactiveStyle == BorderStyle::BreathingGlow);
    }

    if (!needsInactiveAnimation && m_combatEventTypes.isEmpty()) {
      m_borderAnimationTimer->stop();
    } else if (needsInactiveAnimation && !m_borderAnimationTimer->isActive()) {
      m_animationPhase = 0.0;
      m_borderAnimationTimer->start();
    }
  }

  update();
}

void OverlayWidget::setCharacterName(const QString &characterName) {
  if (m_characterName == characterName) {
    return;
  }
  m_characterName = characterName;
  update();
}

void OverlayWidget::setSystemName(const QString &systemName) {
  if (m_systemName == systemName) {
    return;
  }
  m_systemName = systemName;
  update();
}

void OverlayWidget::setCombatEventTypes(const QStringList &eventTypes) {
  if (m_combatEventTypes == eventTypes) {
    return;
  }
  m_combatEventTypes = eventTypes;

  const Config &cfg = Config::instance();
  bool needsAnimation = false;

  // Check if any event type has border highlighting enabled
  for (const QString &eventType : m_combatEventTypes) {
    if (cfg.combatEventBorderHighlight(eventType)) {
      needsAnimation = true;
      break;
    }
  }

  if (needsAnimation) {
    m_animationPhase = 0.0;
    m_borderAnimationTimer->start();
  } else if (!m_isActive) {
    m_borderAnimationTimer->stop();
  }

  update();
}

void OverlayWidget::updateWindowFlags(bool alwaysOnTop) {
  Qt::WindowFlags flags =
      Qt::Tool | Qt::FramelessWindowHint | Qt::WindowTransparentForInput;
  if (alwaysOnTop) {
    flags |= Qt::WindowStaysOnTopHint;
  }
  setWindowFlags(flags);
  if (isVisible()) {
    show();
  }
}

void OverlayWidget::invalidateCache() {
  m_overlayDirty = true;
  update();
}

void OverlayWidget::pauseAnimations() {
  if (!m_animationsPaused && m_borderAnimationTimer->isActive()) {
    m_borderAnimationTimer->stop();
    m_animationsPaused = true;
  }
}

void OverlayWidget::resumeAnimations() {
  if (m_animationsPaused) {
    m_animationsPaused = false;

    const Config &cfg = Config::instance();

    bool needsAnimation = false;

    // Check if any active combat event has border highlighting
    for (const QString &eventType : m_combatEventTypes) {
      if (cfg.combatEventBorderHighlight(eventType)) {
        needsAnimation = true;
        break;
      }
    }

    if (!needsAnimation && m_isActive) {
      BorderStyle style = cfg.activeBorderStyle();
      needsAnimation =
          (style == BorderStyle::Dashed || style == BorderStyle::Neon ||
           style == BorderStyle::Shimmer || style == BorderStyle::ElectricArc ||
           style == BorderStyle::Rainbow ||
           style == BorderStyle::BreathingGlow);
    }

    // Check if inactive border needs animation
    if (!needsAnimation && !m_isActive) {
      QColor inactiveCharacterColor =
          cfg.getCharacterInactiveBorderColor(m_characterName);
      bool hasCustomInactiveColor = inactiveCharacterColor.isValid();
      bool showGlobalInactiveBorder = cfg.showInactiveBorders();

      if (hasCustomInactiveColor || showGlobalInactiveBorder) {
        BorderStyle inactiveStyle = cfg.inactiveBorderStyle();
        needsAnimation = (inactiveStyle == BorderStyle::Dashed ||
                          inactiveStyle == BorderStyle::Neon ||
                          inactiveStyle == BorderStyle::Shimmer ||
                          inactiveStyle == BorderStyle::ElectricArc ||
                          inactiveStyle == BorderStyle::Rainbow ||
                          inactiveStyle == BorderStyle::BreathingGlow);
      }
    }

    if (needsAnimation) {
      m_borderAnimationTimer->start();
    }
  }
}

void OverlayWidget::paintEvent(QPaintEvent *) {

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  drawOverlays(painter);

  const Config &cfg = Config::instance();

  bool highlightEnabled = cfg.highlightActiveWindow();
  bool configDialogOpen = cfg.isConfigDialogOpen();
  bool shouldDrawActiveBorder =
      (highlightEnabled && m_isActive) || configDialogOpen;

  qreal halfWidth = cfg.highlightBorderWidth() / 2.0;
  int borderWidth = cfg.highlightBorderWidth();
  qreal currentOffset = 0.0;

  // Draw active border on the outside (when active)
  if (shouldDrawActiveBorder) {
    QRectF borderRect(halfWidth + currentOffset, halfWidth + currentOffset,
                      width() - 2 * (halfWidth + currentOffset),
                      height() - 2 * (halfWidth + currentOffset));

    QColor borderColor = cfg.getCharacterBorderColor(m_characterName);
    if (!borderColor.isValid()) {
      borderColor = cfg.highlightColor();
    }

    BorderStyle style = cfg.activeBorderStyle();
    drawBorderWithStyle(painter, borderRect, borderColor, borderWidth, style);

    // Move offset inward for next border
    currentOffset += borderWidth;
  } else if (!m_isActive) {
    // Draw inactive border (when not active and global setting is enabled OR
    // per-character color is set)
    QColor inactiveCharacterColor =
        cfg.getCharacterInactiveBorderColor(m_characterName);
    bool hasCustomInactiveColor = inactiveCharacterColor.isValid();
    bool showGlobalInactiveBorder = cfg.showInactiveBorders();

    if (hasCustomInactiveColor || showGlobalInactiveBorder) {
      int inactiveBorderWidth = cfg.inactiveBorderWidth();
      qreal inactiveHalfWidth = inactiveBorderWidth / 2.0;

      QRectF borderRect(inactiveHalfWidth + currentOffset,
                        inactiveHalfWidth + currentOffset,
                        width() - 2 * (inactiveHalfWidth + currentOffset),
                        height() - 2 * (inactiveHalfWidth + currentOffset));

      QColor borderColor = hasCustomInactiveColor ? inactiveCharacterColor
                                                  : cfg.inactiveBorderColor();

      BorderStyle style = cfg.inactiveBorderStyle();
      drawBorderWithStyle(painter, borderRect, borderColor, inactiveBorderWidth,
                          style);

      // Move offset inward for next border
      currentOffset += inactiveBorderWidth;
      borderWidth = inactiveBorderWidth;
      halfWidth = inactiveHalfWidth;
    }
  }

  // Draw all combat event borders nested inside
  for (const QString &eventType : m_combatEventTypes) {
    if (cfg.combatEventBorderHighlight(eventType)) {
      QRectF borderRect(halfWidth + currentOffset, halfWidth + currentOffset,
                        width() - 2 * (halfWidth + currentOffset),
                        height() - 2 * (halfWidth + currentOffset));

      QColor borderColor = cfg.combatEventColor(eventType);
      BorderStyle style = cfg.combatBorderStyle(eventType);

      drawBorderWithStyle(painter, borderRect, borderColor, borderWidth, style);

      // Move offset inward for next border
      currentOffset += borderWidth;
    }
  }
}

void OverlayWidget::renderOverlaysToCache() {
  m_overlayCache = QPixmap(size());
  m_overlayCache.fill(Qt::transparent);

  QPainter cachePainter(&m_overlayCache);
  cachePainter.setRenderHint(QPainter::Antialiasing);

  int positionOffsets[9] = {0};

  const Config &cfg = Config::instance();
  const bool showBg = cfg.showOverlayBackground();

  for (auto &overlay : m_overlays) {
    if (!overlay.enabled)
      continue;

    cachePainter.setFont(overlay.font);

    QFontMetrics metrics(overlay.font);
    QRect textRect = OverlayInfo::calculateTextRect(
        rect(), overlay.position, overlay.text, overlay.font, overlay.offsetX,
        overlay.offsetY);

    int padding = 5;
    int maxAvailableWidth = rect().width() - (2 * padding);

    QString displayText;
    if (overlay.cachedMaxWidth == maxAvailableWidth &&
        !overlay.cachedTruncatedText.isEmpty()) {
      displayText = overlay.cachedTruncatedText;
    } else {
      displayText = OverlayInfo::truncateText(overlay.text, overlay.font,
                                              maxAvailableWidth);
      overlay.cachedTruncatedText = displayText;
      overlay.cachedMaxWidth = maxAvailableWidth;
    }

    int posIdx = static_cast<int>(overlay.position);
    int offset = positionOffsets[posIdx];
    if (offset > 0) {
      switch (overlay.position) {
      case OverlayPosition::TopLeft:
      case OverlayPosition::TopCenter:
      case OverlayPosition::TopRight:
        textRect.moveTop(textRect.top() + offset);
        break;
      case OverlayPosition::CenterLeft:
      case OverlayPosition::Center:
      case OverlayPosition::CenterRight:
        // For center positions, stack downward
        textRect.moveTop(textRect.top() + offset);
        break;
      case OverlayPosition::BottomLeft:
      case OverlayPosition::BottomCenter:
      case OverlayPosition::BottomRight:
        textRect.moveTop(textRect.top() - offset);
        break;
      }
    }

    positionOffsets[posIdx] = offset + metrics.height() + (showBg ? 6 : 2);

    if (showBg) {
      QColor bgColor = cfg.overlayBackgroundColor();
      bgColor.setAlpha(cfg.overlayBackgroundOpacity() * 255 / 100);
      cachePainter.fillRect(textRect.adjusted(-3, -2, 3, 2), bgColor);
    }

    cachePainter.setPen(overlay.color);
    cachePainter.drawText(textRect, Qt::AlignCenter | Qt::AlignVCenter,
                          displayText);
  }

  m_lastOverlaySize = size();
}

void OverlayWidget::drawOverlays(QPainter &painter) {
  if (m_overlayDirty || m_lastOverlaySize != size()) {
    renderOverlaysToCache();
    m_overlayDirty = false;
  }

  if (!m_overlayCache.isNull()) {
    painter.drawPixmap(0, 0, m_overlayCache);
  }
}

void OverlayWidget::drawBorderWithStyle(QPainter &painter, const QRectF &rect,
                                        const QColor &color, int width,
                                        BorderStyle style) {
  switch (style) {
  case BorderStyle::Solid:
    drawSolidBorder(painter, rect, color, width);
    break;
  case BorderStyle::Dashed:
    drawDashedBorder(painter, rect, color, width);
    break;
  case BorderStyle::Dotted:
    drawDottedBorder(painter, rect, color, width);
    break;
  case BorderStyle::DashDot:
    drawDashDotBorder(painter, rect, color, width);
    break;
  case BorderStyle::FadedEdges:
    drawFadedEdgesBorder(painter, rect, color, width);
    break;
  case BorderStyle::CornerAccents:
    drawCornerAccentsBorder(painter, rect, color, width);
    break;
  case BorderStyle::RoundedCorners:
    drawRoundedCornersBorder(painter, rect, color, width);
    break;
  case BorderStyle::Neon:
    drawNeonBorder(painter, rect, color, width);
    break;
  case BorderStyle::Shimmer:
    drawShimmerBorder(painter, rect, color, width);
    break;
  case BorderStyle::ThickThin:
    drawThickThinBorder(painter, rect, color, width);
    break;
  case BorderStyle::ElectricArc:
    drawElectricArcBorder(painter, rect, color, width);
    break;
  case BorderStyle::Rainbow:
    drawRainbowBorder(painter, rect, color, width);
    break;
  case BorderStyle::BreathingGlow:
    drawBreathingGlowBorder(painter, rect, color, width);
    break;
  case BorderStyle::DoubleGlow:
    drawDoubleGlowBorder(painter, rect, color, width);
    break;
  case BorderStyle::Zigzag:
    drawZigzagBorder(painter, rect, color, width);
    break;
  }
}

void OverlayWidget::drawSolidBorder(QPainter &painter, const QRectF &rect,
                                    const QColor &color, int width) {
  QPen pen(color, width);
  pen.setJoinStyle(Qt::MiterJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.drawRect(rect);
}

void OverlayWidget::drawDashedBorder(QPainter &painter, const QRectF &rect,
                                     const QColor &color, int width) {
  QPen pen(color, width);
  pen.setStyle(Qt::DashLine);
  pen.setDashOffset(m_animationPhase);
  pen.setJoinStyle(Qt::MiterJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.drawRect(rect);
}

void OverlayWidget::drawDottedBorder(QPainter &painter, const QRectF &rect,
                                     const QColor &color, int width) {
  QPen pen(color, width);
  pen.setStyle(Qt::DotLine);
  pen.setJoinStyle(Qt::MiterJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.drawRect(rect);
}

void OverlayWidget::drawDashDotBorder(QPainter &painter, const QRectF &rect,
                                      const QColor &color, int width) {
  QPen pen(color, width);
  pen.setStyle(Qt::DashDotLine);
  pen.setJoinStyle(Qt::MiterJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.drawRect(rect);
}

void OverlayWidget::drawFadedEdgesBorder(QPainter &painter, const QRectF &rect,
                                         const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setBrush(Qt::NoBrush);

  qreal fadeLength = qMin(rect.width(), rect.height()) * 0.25;

  QLinearGradient topGradient(rect.topLeft(),
                              QPointF(rect.left() + fadeLength, rect.top()));
  QColor transparent = color;
  transparent.setAlpha(0);
  topGradient.setColorAt(0, transparent);
  topGradient.setColorAt(1, color);

  QPen topPen(QBrush(topGradient), width);
  painter.setPen(topPen);
  painter.drawLine(rect.topLeft(),
                   QPointF(rect.left() + fadeLength, rect.top()));

  QPen solidPen(color, width);
  painter.setPen(solidPen);
  painter.drawLine(QPointF(rect.left() + fadeLength, rect.top()),
                   QPointF(rect.right() - fadeLength, rect.top()));

  QLinearGradient rightFade(QPointF(rect.right() - fadeLength, rect.top()),
                            rect.topRight());
  rightFade.setColorAt(0, color);
  rightFade.setColorAt(1, transparent);
  QPen rightPen(QBrush(rightFade), width);
  painter.setPen(rightPen);
  painter.drawLine(QPointF(rect.right() - fadeLength, rect.top()),
                   rect.topRight());

  painter.setPen(solidPen);
  painter.drawLine(rect.topRight(), rect.bottomRight());
  painter.drawLine(rect.bottomRight(), rect.bottomLeft());
  painter.drawLine(rect.bottomLeft(), rect.topLeft());
}

void OverlayWidget::drawCornerAccentsBorder(QPainter &painter,
                                            const QRectF &rect,
                                            const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, false);

  QPen pen(color, width);
  pen.setCapStyle(Qt::SquareCap);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  qreal accentLength = qMin(rect.width(), rect.height()) * 0.15;

  painter.drawLine(rect.topLeft(),
                   QPointF(rect.left() + accentLength, rect.top()));
  painter.drawLine(rect.topLeft(),
                   QPointF(rect.left(), rect.top() + accentLength));

  painter.drawLine(rect.topRight(),
                   QPointF(rect.right() - accentLength, rect.top()));
  painter.drawLine(rect.topRight(),
                   QPointF(rect.right(), rect.top() + accentLength));

  painter.drawLine(rect.bottomRight(),
                   QPointF(rect.right() - accentLength, rect.bottom()));
  painter.drawLine(rect.bottomRight(),
                   QPointF(rect.right(), rect.bottom() - accentLength));

  painter.drawLine(rect.bottomLeft(),
                   QPointF(rect.left() + accentLength, rect.bottom()));
  painter.drawLine(rect.bottomLeft(),
                   QPointF(rect.left(), rect.bottom() - accentLength));
}

void OverlayWidget::drawRoundedCornersBorder(QPainter &painter,
                                             const QRectF &rect,
                                             const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, true);

  QPen pen(color, width);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  qreal radius = qMin(rect.width(), rect.height()) * 0.1;
  painter.drawRoundedRect(rect, radius, radius);
}

void OverlayWidget::drawNeonBorder(QPainter &painter, const QRectF &rect,
                                   const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);

  QColor neonColor = color;
  int hue = (color.hue() + static_cast<int>(m_animationPhase * 3.6)) % 360;
  neonColor.setHsv(hue, 255, 255);

  for (int i = 4; i >= 0; --i) {
    qreal glowWidth = width + i * 2.0;
    QColor glowColor = neonColor;
    glowColor.setAlpha(100 - i * 15);

    QPen pen(glowColor, glowWidth);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawRect(rect);
  }

  QPen corePen(neonColor.lighter(150), width);
  corePen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(corePen);
  painter.drawRect(rect);
}

void OverlayWidget::drawShimmerBorder(QPainter &painter, const QRectF &rect,
                                      const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setBrush(Qt::NoBrush);

  QPen basePen(color, width);
  basePen.setJoinStyle(Qt::MiterJoin);
  painter.setPen(basePen);
  painter.drawRect(rect);

  painter.setRenderHint(QPainter::Antialiasing, true);
  qreal perimeter = (rect.width() + rect.height()) * 2.0;

  for (int i = 0; i < 8; ++i) {
    qreal sparklePos =
        fmod(m_animationPhase * 3.0 + i * (perimeter / 8.0), perimeter);
    qreal sparkleIntensity =
        qAbs(qSin((m_animationPhase + i * 12.5) * 0.062831853));

    QPointF sparklePoint;
    if (sparklePos < rect.width()) {
      sparklePoint = QPointF(rect.left() + sparklePos, rect.top());
    } else if (sparklePos < rect.width() + rect.height()) {
      sparklePoint =
          QPointF(rect.right(), rect.top() + (sparklePos - rect.width()));
    } else if (sparklePos < rect.width() * 2 + rect.height()) {
      sparklePoint =
          QPointF(rect.right() - (sparklePos - rect.width() - rect.height()),
                  rect.bottom());
    } else {
      sparklePoint =
          QPointF(rect.left(), rect.bottom() - (sparklePos - rect.width() * 2 -
                                                rect.height()));
    }

    QColor sparkleColor = color.lighter(150);
    sparkleColor.setAlpha(static_cast<int>(200 * sparkleIntensity));

    QRadialGradient sparkle(sparklePoint, width * 2);
    sparkle.setColorAt(0, sparkleColor);
    QColor transparent = sparkleColor;
    transparent.setAlpha(0);
    sparkle.setColorAt(1, transparent);

    painter.setBrush(QBrush(sparkle));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(sparklePoint, width * 1.5, width * 1.5);
  }
}

void OverlayWidget::drawThickThinBorder(QPainter &painter, const QRectF &rect,
                                        const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setBrush(Qt::NoBrush);

  QPen pen(color, width);
  pen.setJoinStyle(Qt::MiterJoin);
  painter.setPen(pen);

  qreal segmentLength = 15.0;
  int thickWidth = width * 2;
  int thinWidth = width;

  auto drawSegmentedEdge = [&](const QPointF &start, const QPointF &end) {
    qreal dx = end.x() - start.x();
    qreal dy = end.y() - start.y();
    qreal length = qSqrt(dx * dx + dy * dy);
    qreal ux = dx / length;
    qreal uy = dy / length;

    qreal pos = 0;
    bool thick = true;
    while (pos < length) {
      qreal segLen = qMin(segmentLength, length - pos);
      QPointF p1(start.x() + ux * pos, start.y() + uy * pos);
      QPointF p2(start.x() + ux * (pos + segLen),
                 start.y() + uy * (pos + segLen));

      pen.setWidth(thick ? thickWidth : thinWidth);
      painter.setPen(pen);
      painter.drawLine(p1, p2);

      pos += segLen;
      thick = !thick;
    }
  };

  drawSegmentedEdge(rect.topLeft(), rect.topRight());
  drawSegmentedEdge(rect.topRight(), rect.bottomRight());
  drawSegmentedEdge(rect.bottomRight(), rect.bottomLeft());
  drawSegmentedEdge(rect.bottomLeft(), rect.topLeft());
}

void OverlayWidget::drawElectricArcBorder(QPainter &painter, const QRectF &rect,
                                          const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);

  QPainterPath path;
  qreal segmentLength = 8.0;
  qreal jitter = 2.0;

  auto createJaggedEdge = [&](const QPointF &start, const QPointF &end) {
    qreal dx = end.x() - start.x();
    qreal dy = end.y() - start.y();
    qreal length = qSqrt(dx * dx + dy * dy);
    qreal ux = dx / length;
    qreal uy = dy / length;
    qreal nx = -uy;
    qreal ny = ux;

    qreal pos = 0;
    path.moveTo(start);

    qreal animOffset = m_animationPhase * 0.062831853;

    while (pos < length) {
      pos += segmentLength;
      if (pos > length)
        pos = length;

      qreal jitterAmount =
          jitter * qSin((pos / segmentLength) * 3.14159 + animOffset * 10.0);
      QPointF point(start.x() + ux * pos + nx * jitterAmount,
                    start.y() + uy * pos + ny * jitterAmount);
      path.lineTo(point);
    }
  };

  createJaggedEdge(rect.topLeft(), rect.topRight());
  createJaggedEdge(rect.topRight(), rect.bottomRight());
  createJaggedEdge(rect.bottomRight(), rect.bottomLeft());
  createJaggedEdge(rect.bottomLeft(), rect.topLeft());

  QColor glowColor = color.lighter(150);
  for (int i = 2; i >= 0; --i) {
    QColor layerColor = glowColor;
    layerColor.setAlpha(80 - i * 20);
    QPen pen(layerColor, width + i * 1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawPath(path);
  }
}

void OverlayWidget::drawRainbowBorder(QPainter &painter, const QRectF &rect,
                                      const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setBrush(Qt::NoBrush);

  qreal perimeter = (rect.width() + rect.height()) * 2.0;
  qreal segmentLength = 5.0;
  int hueOffset = static_cast<int>(m_animationPhase * 3.6) % 360;

  auto drawColorfulEdge = [&](const QPointF &start, const QPointF &end,
                              qreal startPos) {
    qreal dx = end.x() - start.x();
    qreal dy = end.y() - start.y();
    qreal length = qSqrt(dx * dx + dy * dy);
    qreal ux = dx / length;
    qreal uy = dy / length;

    qreal pos = 0;
    while (pos < length) {
      qreal segLen = qMin(segmentLength, length - pos);

      int hue =
          (hueOffset + static_cast<int>((startPos + pos) / perimeter * 360)) %
          360;
      QColor segmentColor;
      segmentColor.setHsv(hue, 255, 255);

      QPen pen(segmentColor, width);
      pen.setCapStyle(Qt::FlatCap);
      painter.setPen(pen);

      QPointF p1(start.x() + ux * pos, start.y() + uy * pos);
      QPointF p2(start.x() + ux * (pos + segLen),
                 start.y() + uy * (pos + segLen));
      painter.drawLine(p1, p2);

      pos += segLen;
    }
  };

  qreal topLen = rect.width();
  qreal rightLen = rect.height();
  qreal bottomLen = rect.width();

  drawColorfulEdge(rect.topLeft(), rect.topRight(), 0);
  drawColorfulEdge(rect.topRight(), rect.bottomRight(), topLen);
  drawColorfulEdge(rect.bottomRight(), rect.bottomLeft(), topLen + rightLen);
  drawColorfulEdge(rect.bottomLeft(), rect.topLeft(),
                   topLen + rightLen + bottomLen);
}

void OverlayWidget::drawBreathingGlowBorder(QPainter &painter,
                                            const QRectF &rect,
                                            const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);

  qreal breath = (qSin(m_animationPhase * 0.062831853) + 1.0) * 0.5;

  int glowLayers = 6;
  qreal maxSpread = width * 3 * breath;

  for (int i = glowLayers; i >= 0; --i) {
    qreal layerProgress = static_cast<qreal>(i) / glowLayers;
    qreal spread = maxSpread * layerProgress;

    QColor glowColor = color;
    int alpha = static_cast<int>(120 * (1.0 - layerProgress) * breath);
    glowColor.setAlpha(alpha);

    QPen pen(glowColor, width + spread);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawRect(rect);
  }

  QPen corePen(color, width);
  corePen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(corePen);
  painter.drawRect(rect);
}

void OverlayWidget::drawDoubleGlowBorder(QPainter &painter, const QRectF &rect,
                                         const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);

  for (int i = 3; i >= 0; --i) {
    QColor innerGlow = color.lighter(130);
    innerGlow.setAlpha(100 - i * 20);
    QPen pen(innerGlow, width + i * 1.0);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawRect(rect);
  }

  for (int i = 4; i >= 0; --i) {
    QColor outerGlow = color;
    outerGlow.setAlpha(80 - i * 15);
    QPen pen(outerGlow, width + i * 2.0 + 4);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawRect(rect);
  }

  QPen corePen(color.lighter(120), width);
  corePen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(corePen);
  painter.drawRect(rect);
}

void OverlayWidget::drawZigzagBorder(QPainter &painter, const QRectF &rect,
                                     const QColor &color, int width) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);

  QPen pen(color, width);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);

  qreal zigWidth = 8.0;
  qreal zigHeight = 4.0;

  auto drawZigzagEdge = [&](const QPointF &start, const QPointF &end) {
    qreal dx = end.x() - start.x();
    qreal dy = end.y() - start.y();
    qreal length = qSqrt(dx * dx + dy * dy);
    qreal ux = dx / length;
    qreal uy = dy / length;
    qreal nx = -uy;
    qreal ny = ux;

    QPainterPath path;
    path.moveTo(start);

    qreal pos = 0;
    bool up = true;
    while (pos < length) {
      pos += zigWidth;
      if (pos > length)
        pos = length;

      qreal offset = up ? zigHeight : -zigHeight;
      QPointF point(start.x() + ux * pos + nx * offset,
                    start.y() + uy * pos + ny * offset);
      path.lineTo(point);
      up = !up;
    }

    painter.drawPath(path);
  };

  drawZigzagEdge(rect.topLeft(), rect.topRight());
  drawZigzagEdge(rect.topRight(), rect.bottomRight());
  drawZigzagEdge(rect.bottomRight(), rect.bottomLeft());
  drawZigzagEdge(rect.bottomLeft(), rect.topLeft());
}
