#include "uiohookmanager.h"
#include "hotkeymanager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <Windows.h>

QPointer<UIohookManager> UIohookManager::s_instance;

UIohookManager::UIohookManager(QObject *parent)
    : QObject(parent), m_running(false), m_hotkeyManager(nullptr) {
  s_instance = this;
}

UIohookManager::~UIohookManager() {
  stop();
  s_instance.clear();
}

bool UIohookManager::start() {
  if (m_running) {
    return true;
  }

  // Set the dispatch procedure for libuiohook (API changed in v1.3)
  hook_set_dispatch_proc(&UIohookManager::dispatchProc, nullptr);

  // Run the hook in a separate thread (hook_run is blocking)
  // Note: libuiohook internally manages the thread, so we just need to
  // call hook_run() which will return only when hook_stop() is called
  std::thread hookThread([]() {
    int status = hook_run();
    if (status != UIOHOOK_SUCCESS) {
      qWarning() << "libuiohook hook_run failed with status:" << status;
    }
  });
  hookThread.detach();

  // Give the hook a moment to initialize
  QThread::msleep(50);

  m_running = true;
  return true;
}

void UIohookManager::stop() {
  if (!m_running) {
    return;
  }

  // Stop the hook (this will cause hook_run to return)
  int status = hook_stop();

  if (status != UIOHOOK_SUCCESS) {
    qWarning() << "Failed to stop libuiohook. Error code:" << status;
  }

  m_running = false;
}

void UIohookManager::dispatchProc(uiohook_event *const event, void *user_data) {
  // user_data is not used in this implementation
  (void)user_data;

  if (!s_instance) {
    return;
  }

  // Only handle mouse button release events
  if (event->type == EVENT_MOUSE_RELEASED) {
    int button = convertMouseButton(event->data.mouse.button);

    if (button != 0) {
      bool ctrl = isCtrlPressed();
      bool alt = isAltPressed();
      bool shift = isShiftPressed();

      // If we have a HotkeyManager, notify it directly
      if (s_instance->m_hotkeyManager) {
        s_instance->m_hotkeyManager->checkMouseButtonBindings(button, ctrl, alt,
                                                              shift);
      }

      // Also emit signal for Qt-based connections
      emit s_instance->mouseButtonReleased(button, ctrl, alt, shift);
    }
  }
}

int UIohookManager::convertMouseButton(uint16_t button) {
  // Convert libuiohook button codes to Windows VK codes
  switch (button) {
  case MOUSE_BUTTON3: // Middle button
    return VK_MBUTTON;
  case MOUSE_BUTTON4: // X1 button
    return VK_XBUTTON1;
  case MOUSE_BUTTON5: // X2 button
    return VK_XBUTTON2;
  default:
    return 0; // We only handle middle and extra buttons
  }
}

bool UIohookManager::isCtrlPressed() {
  return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
}

bool UIohookManager::isAltPressed() {
  return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
}

bool UIohookManager::isShiftPressed() {
  return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
}
