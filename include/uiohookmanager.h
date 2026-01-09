#ifndef UIOHOOKMANAGER_H
#define UIOHOOKMANAGER_H

#include <QObject>
#include <QPointer>
#include <uiohook.h>

/// Forward declaration
class HotkeyManager;

/**
 * @brief Wrapper for libuiohook to integrate with Qt signals/slots
 *
 * This class manages the libuiohook library lifecycle and converts
 * native mouse button events into Qt signals. It runs the hook in
 * a separate thread managed by libuiohook.
 *
 * libuiohook is a cross-platform library that provides low-level
 * keyboard and mouse hook functionality. This implementation uses
 * it specifically for mouse button detection (middle button and
 * extra buttons X1/X2) to support hotkey bindings.
 *
 * The library automatically manages its own threading, so no separate
 * thread management is needed. Events are dispatched through the
 * dispatchProc callback which runs in the libuiohook thread.
 */
class UIohookManager : public QObject {
  Q_OBJECT

public:
  explicit UIohookManager(QObject *parent = nullptr);
  ~UIohookManager();

  static UIohookManager *instance() { return s_instance.data(); }

  /// Start the input hook
  bool start();

  /// Stop the input hook
  void stop();

  /// Check if the hook is currently running
  bool isRunning() const { return m_running; }

  /// Set the HotkeyManager that will receive mouse button notifications
  void setHotkeyManager(HotkeyManager *manager) { m_hotkeyManager = manager; }

signals:
  /// Emitted when a mouse button is released
  void mouseButtonReleased(int button, bool ctrl, bool alt, bool shift);

private:
  static QPointer<UIohookManager> s_instance;

  bool m_running;
  HotkeyManager *m_hotkeyManager;

  /// libuiohook dispatcher callback (called from hook thread)
  /// Note: Signature matches dispatcher_t from libuiohook v1.3+
  static void dispatchProc(uiohook_event *const event, void *user_data);

  /// Helper to convert libuiohook button codes to Windows VK codes
  static int convertMouseButton(uint16_t button);

  /// Helper to check if modifier keys are pressed
  static bool isCtrlPressed();
  static bool isAltPressed();
  static bool isShiftPressed();
};

#endif // UIOHOOKMANAGER_H
