#include "hookthread.h"
#include <cassert>
#include <cstdio>

static const UINT MSG_INSTALL_MOUSE = WM_APP + 1;
static const UINT MSG_UNINSTALL_MOUSE = WM_APP + 2;
static const UINT MSG_INSTALL_KEYBOARD = WM_APP + 3;
static const UINT MSG_UNINSTALL_KEYBOARD = WM_APP + 4;

HookThread &HookThread::instance() {
  static HookThread inst;
  return inst;
}

HookThread::HookThread()
    : m_threadHandle(nullptr), m_threadId(0), m_mouseHook(nullptr),
      m_keyboardHook(nullptr) {}

HookThread::~HookThread() { stop(); }

void HookThread::start() {
  if (m_threadHandle)
    return;

  m_threadHandle = CreateThread(nullptr, 0, threadProc, this, 0, &m_threadId);
  if (!m_threadHandle) {
    fprintf(stderr, "HookThread: CreateThread failed: %lu\n", GetLastError());
  }

  int tries = 0;
  while (m_threadId == 0 && tries++ < 10) {
    Sleep(10);
  }
}

void HookThread::stop() {
  if (m_threadHandle) {
    PostThreadMessage(m_threadId, WM_QUIT, 0, 0);
    WaitForSingleObject(m_threadHandle, INFINITE);
    CloseHandle(m_threadHandle);
    m_threadHandle = nullptr;
    m_threadId = 0;
  }
}

void HookThread::installMouseHook(HOOKPROC proc) {
  start();
  if (!m_threadHandle)
    return;

  PostThreadMessage(m_threadId, MSG_INSTALL_MOUSE,
                    reinterpret_cast<WPARAM>(proc), 0);
}

void HookThread::uninstallMouseHook() {
  if (!m_threadHandle)
    return;

  PostThreadMessage(m_threadId, MSG_UNINSTALL_MOUSE, 0, 0);
}

void HookThread::installKeyboardHook(HOOKPROC proc) {
  start();
  if (!m_threadHandle)
    return;

  PostThreadMessage(m_threadId, MSG_INSTALL_KEYBOARD,
                    reinterpret_cast<WPARAM>(proc), 0);
}

void HookThread::uninstallKeyboardHook() {
  if (!m_threadHandle)
    return;

  PostThreadMessage(m_threadId, MSG_UNINSTALL_KEYBOARD, 0, 0);
}

DWORD WINAPI HookThread::threadProc(LPVOID param) {
  HookThread *self = reinterpret_cast<HookThread *>(param);

  MSG msg;
  PeekMessage(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

  while (GetMessage(&msg, nullptr, 0, 0) > 0) {
    if (msg.message == MSG_INSTALL_MOUSE) {
      if (!self->m_mouseHook) {
        HOOKPROC proc = reinterpret_cast<HOOKPROC>(msg.wParam);
        self->m_mouseHook =
            SetWindowsHookEx(WH_MOUSE_LL, proc, GetModuleHandle(nullptr), 0);
        if (!self->m_mouseHook) {
          fprintf(stderr,
                  "HookThread: SetWindowsHookEx(WH_MOUSE_LL) failed: %lu\n",
                  GetLastError());
        }
      }
    } else if (msg.message == MSG_UNINSTALL_MOUSE) {
      if (self->m_mouseHook) {
        UnhookWindowsHookEx(self->m_mouseHook);
        self->m_mouseHook = nullptr;
      }
    } else if (msg.message == MSG_INSTALL_KEYBOARD) {
      if (!self->m_keyboardHook) {
        HOOKPROC proc = reinterpret_cast<HOOKPROC>(msg.wParam);
        self->m_keyboardHook =
            SetWindowsHookEx(WH_KEYBOARD_LL, proc, GetModuleHandle(nullptr), 0);
        if (!self->m_keyboardHook) {
          fprintf(stderr,
                  "HookThread: SetWindowsHookEx(WH_KEYBOARD_LL) failed: %lu\n",
                  GetLastError());
        }
      }
    } else if (msg.message == MSG_UNINSTALL_KEYBOARD) {
      if (self->m_keyboardHook) {
        UnhookWindowsHookEx(self->m_keyboardHook);
        self->m_keyboardHook = nullptr;
      }
    }

    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  if (self->m_mouseHook) {
    UnhookWindowsHookEx(self->m_mouseHook);
    self->m_mouseHook = nullptr;
  }

  if (self->m_keyboardHook) {
    UnhookWindowsHookEx(self->m_keyboardHook);
    self->m_keyboardHook = nullptr;
  }

  return 0;
}
