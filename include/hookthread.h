#ifndef HOOKTHREAD_H
#define HOOKTHREAD_H

#include <windows.h>

class HookThread {
public:
  static HookThread &instance();

  void installMouseHook(HOOKPROC proc);
  void uninstallMouseHook();

  void installKeyboardHook(HOOKPROC proc);
  void uninstallKeyboardHook();

  void start();
  void stop();

private:
  HookThread();
  ~HookThread();

  static DWORD WINAPI threadProc(LPVOID param);

  HANDLE m_threadHandle;
  DWORD m_threadId;

  HHOOK m_mouseHook;
  HHOOK m_keyboardHook;

  int m_mouseHookRefCount;
  int m_keyboardHookRefCount;

  HANDLE m_installCompleteEvent;
};

#endif
