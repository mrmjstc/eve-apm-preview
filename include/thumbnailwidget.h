#ifndef THUMBNAILWIDGET_H
#define THUMBNAILWIDGET_H

#include "overlayinfo.h"
#include <QLabel>
#include <QPixmap>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <dwmapi.h>
#include <windows.h>

class OverlayWidget;

class ThumbnailWidget : public QWidget {
  Q_OBJECT

public:
  explicit ThumbnailWidget(quintptr windowId, const QString &title,
                           QWidget *parent = nullptr);
  ~ThumbnailWidget();

  void setTitle(const QString &title);
  void setActive(bool active);
  void updateOverlays();
  quintptr getWindowId() const { return m_windowId; }

  void closeImmediately();

  void setCharacterName(const QString &characterName);
  QString getCharacterName() const { return m_characterName; }

  void setSystemName(const QString &systemName);
  QString getSystemName() const { return m_systemName; }

  void setCombatMessage(const QString &message,
                        const QString &eventType = QString());
  QString getCombatMessage() const { return m_combatMessage; }
  bool hasCombatEvent() const { return !m_combatMessage.isEmpty(); }
  QString getCombatEventType() const { return m_combatEventType; }

  void forceUpdate();
  void updateWindowFlags(bool alwaysOnTop);
  void forceOverlayRender();
  void hideOverlay();
  void showOverlay();

  void setOtherThumbnails(const QVector<ThumbnailWidget *> &others) {
    m_otherThumbnails = others;
  }

signals:
  void clicked(quintptr windowId);
  void positionChanged(quintptr windowId, QPoint position);
  void groupDragStarted(quintptr windowId);
  void groupDragMoved(quintptr windowId, QPoint delta);
  void groupDragEnded(quintptr windowId);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void moveEvent(QMoveEvent *event) override;
  bool nativeEvent(const QByteArray &eventType, void *message,
                   qintptr *result) override;

private:
  quintptr m_windowId;
  QString m_title;
  QString m_characterName;
  QString m_systemName;
  QString m_combatMessage;
  QString m_combatEventType;
  QPoint m_dragPosition;
  bool m_isDragging = false;
  bool m_isGroupDragging = false;
  QPoint m_groupDragStartPos;
  bool m_isActive = false;
  QVector<OverlayElement> m_overlays;
  QVector<ThumbnailWidget *> m_otherThumbnails;

  HTHUMBNAIL m_dwmThumbnail = nullptr;
  QTimer *m_updateTimer = nullptr;
  QTimer *m_combatMessageTimer = nullptr;

  OverlayWidget *m_overlayWidget = nullptr;

  QString m_cachedTitle;

  void setupDwmThumbnail();
  void cleanupDwmThumbnail();
  void updateDwmThumbnail();
  void updateOverlayWidget();
  QPoint snapPosition(const QPoint &pos,
                      const QVector<ThumbnailWidget *> &otherThumbnails);
};

class OverlayWidget : public QWidget {
  Q_OBJECT

public:
  explicit OverlayWidget(QWidget *parent = nullptr);

  void setOverlays(const QVector<OverlayElement> &overlays);
  void setActiveState(bool active);
  void setCharacterName(const QString &characterName);
  void setSystemName(const QString &systemName);
  void setCombatEventState(bool hasCombatEvent, const QString &eventType);
  void updateWindowFlags(bool alwaysOnTop);
  void invalidateCache();

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QVector<OverlayElement> m_overlays;
  bool m_isActive = false;
  QString m_characterName;
  QString m_systemName;
  bool m_hasCombatEvent = false;
  QString m_combatEventType;

  QPixmap m_overlayCache;
  bool m_overlayDirty = true;
  QSize m_lastOverlaySize;

  QTimer *m_borderAnimationTimer = nullptr;
  qreal m_dashOffset = 0.0;

  void drawOverlays(QPainter &painter);
  void renderOverlaysToCache();
};

#endif
