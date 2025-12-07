#include "hotkeycapture.h"
#include "stylesheet.h"
#include <QApplication>
#include <QStyle>
#include <Windows.h>

HotkeyCapture *HotkeyCapture::s_activeInstance = nullptr;
HHOOK HotkeyCapture::s_keyboardHook = nullptr;

HotkeyCapture::HotkeyCapture(QWidget *parent)
    : QLineEdit(parent), m_capturing(false), m_hasConflict(false) {
  setReadOnly(true);
  setPlaceholderText("Click to set hotkey...");
  setAlignment(Qt::AlignCenter);
  setCursor(Qt::PointingHandCursor);

  setStyleSheet(StyleSheet::getHotkeyCaptureStyleSheet());
}

HotkeyCapture::~HotkeyCapture() {
  uninstallKeyboardHook();
  QApplication::instance()->removeNativeEventFilter(this);
}

void HotkeyCapture::setHotkey(int keyCode, bool ctrl, bool alt, bool shift) {
  m_hotkeys.clear();
  if (keyCode != 0) {
    m_hotkeys.append(HotkeyCombination(keyCode, ctrl, alt, shift));
  }
  updateDisplay();
}

void HotkeyCapture::clearHotkey() {
  m_hotkeys.clear();
  updateDisplay();
  emit hotkeyChanged();
}

void HotkeyCapture::setHotkeys(const QVector<HotkeyCombination> &hotkeys) {
  m_hotkeys = hotkeys;
  updateDisplay();
}

void HotkeyCapture::addHotkey(int keyCode, bool ctrl, bool alt, bool shift) {
  addHotkey(HotkeyCombination(keyCode, ctrl, alt, shift));
}

void HotkeyCapture::addHotkey(const HotkeyCombination &hotkey) {
  if (!hotkey.isValid()) {
    return;
  }

  for (const HotkeyCombination &existing : m_hotkeys) {
    if (existing == hotkey) {
      return;
    }
  }

  m_hotkeys.append(hotkey);
  updateDisplay();
  emit hotkeyChanged();
}

void HotkeyCapture::removeHotkeyAt(int index) {
  if (index >= 0 && index < m_hotkeys.size()) {
    m_hotkeys.removeAt(index);
    updateDisplay();
    emit hotkeyChanged();
  }
}

void HotkeyCapture::keyPressEvent(QKeyEvent *event) {
  if (event->isAutoRepeat()) {
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_Escape) {
    if (m_capturing) {
      m_capturing = false;
      setText(m_savedText);
      uninstallKeyboardHook();
      clearFocus();
    }
    event->accept();
    return;
  }

  int key = event->key();
  if (key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Shift ||
      key == Qt::Key_Meta) {
    event->accept();
    return;
  }

  bool ctrl = event->modifiers() & Qt::ControlModifier;
  bool alt = event->modifiers() & Qt::AltModifier;
  bool shift = event->modifiers() & Qt::ShiftModifier;

  int keyCode = 0;

  if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
    keyCode = VK_F1 + (key - Qt::Key_F1);
  } else if (key >= Qt::Key_F13 && key <= Qt::Key_F24) {
    keyCode = VK_F13 + (key - Qt::Key_F13);
  } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    keyCode = '0' + (key - Qt::Key_0);
  } else if (key >= Qt::Key_A && key <= Qt::Key_Z) {
    keyCode = 'A' + (key - Qt::Key_A);
  } else {
    switch (key) {
    case Qt::Key_Insert:
      keyCode = VK_INSERT;
      break;
    case Qt::Key_Delete:
      keyCode = VK_DELETE;
      break;
    case Qt::Key_Home:
      keyCode = VK_HOME;
      break;
    case Qt::Key_End:
      keyCode = VK_END;
      break;
    case Qt::Key_PageUp:
      keyCode = VK_PRIOR;
      break;
    case Qt::Key_PageDown:
      keyCode = VK_NEXT;
      break;
    case Qt::Key_Pause:
      keyCode = VK_PAUSE;
      break;
    case Qt::Key_ScrollLock:
      keyCode = VK_SCROLL;
      break;
    case Qt::Key_Space:
      keyCode = VK_SPACE;
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      keyCode = VK_RETURN;
      break;
    case Qt::Key_Tab:
      keyCode = VK_TAB;
      break;
    case Qt::Key_Backspace:
      keyCode = VK_BACK;
      break;
    case Qt::Key_Left:
      keyCode = VK_LEFT;
      break;
    case Qt::Key_Right:
      keyCode = VK_RIGHT;
      break;
    case Qt::Key_Up:
      keyCode = VK_UP;
      break;
    case Qt::Key_Down:
      keyCode = VK_DOWN;
      break;
    default:
      event->accept();
      return;
    }
  }

  event->accept();
  addHotkey(keyCode, ctrl, alt, shift);
}

void HotkeyCapture::focusInEvent(QFocusEvent *event) {
  QLineEdit::focusInEvent(event);
  if (m_capturing) {
    installKeyboardHook();
  }
  selectAll();
}

void HotkeyCapture::focusOutEvent(QFocusEvent *event) {
  QLineEdit::focusOutEvent(event);
  uninstallKeyboardHook();

  if (m_capturing) {
    m_capturing = false;
    setText(m_savedText);
  }
}

void HotkeyCapture::mousePressEvent(QMouseEvent *event) {
  QLineEdit::mousePressEvent(event);

  if (!m_capturing) {
    m_capturing = true;
    m_savedText = text();
    setText("Press a key...");
    installKeyboardHook();
  }
}

bool HotkeyCapture::event(QEvent *e) {
  if (e->type() == QEvent::KeyPress) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(e);

    if (keyEvent->nativeVirtualKey() != 0) {
      int vkCode = keyEvent->nativeVirtualKey();

      if (vkCode == VK_CONTROL || vkCode == VK_MENU || vkCode == VK_SHIFT ||
          vkCode == VK_LWIN || vkCode == VK_RWIN) {
        return QLineEdit::event(e);
      }

      bool ctrl = keyEvent->modifiers() & Qt::ControlModifier;
      bool alt = keyEvent->modifiers() & Qt::AltModifier;
      bool shift = keyEvent->modifiers() & Qt::ShiftModifier;

      addHotkey(vkCode, ctrl, alt, shift);

      e->accept();
      return true;
    }
  }

  return QLineEdit::event(e);
}

bool HotkeyCapture::nativeEventFilter(const QByteArray &, void *message,
                                      qintptr *) {
  return false;
}

void HotkeyCapture::updateDisplay() {
  if (m_hotkeys.isEmpty()) {
    setText("");
    return;
  }

  QStringList hotkeyTexts;
  for (const HotkeyCombination &hk : m_hotkeys) {
    hotkeyTexts.append(formatHotkey(hk));
  }

  setText(hotkeyTexts.join(", "));
}

QString HotkeyCapture::formatHotkey(const HotkeyCombination &hk) const {
  QString text;
  if (hk.ctrl)
    text += "Ctrl+";
  if (hk.alt)
    text += "Alt+";
  if (hk.shift)
    text += "Shift+";
  text += keyCodeToString(hk.keyCode);
  return text;
}

QString HotkeyCapture::keyCodeToString(int keyCode) const {
  if (keyCode >= VK_F1 && keyCode <= VK_F12) {
    return QString("F%1").arg(keyCode - VK_F1 + 1);
  }
  if (keyCode >= VK_F13 && keyCode <= VK_F24) {
    return QString("F%1").arg(keyCode - VK_F13 + 13);
  }

  if (keyCode >= VK_NUMPAD0 && keyCode <= VK_NUMPAD9) {
    return QString("Numpad %1").arg(keyCode - VK_NUMPAD0);
  }

  if (keyCode >= '0' && keyCode <= '9') {
    return QString(QChar(keyCode));
  }

  if (keyCode >= 'A' && keyCode <= 'Z') {
    return QString(QChar(keyCode));
  }

  switch (keyCode) {
  case VK_INSERT:
    return "Insert";
  case VK_DELETE:
    return "Delete";
  case VK_HOME:
    return "Home";
  case VK_END:
    return "End";
  case VK_PRIOR:
    return "Page Up";
  case VK_NEXT:
    return "Page Down";
  case VK_PAUSE:
    return "Pause";
  case VK_SCROLL:
    return "Scroll Lock";
  case VK_SPACE:
    return "Space";
  case VK_RETURN:
    return "Enter";
  case VK_ESCAPE:
    return "Escape";
  case VK_TAB:
    return "Tab";
  case VK_BACK:
    return "Backspace";
  case VK_LEFT:
    return "Left";
  case VK_RIGHT:
    return "Right";
  case VK_UP:
    return "Up";
  case VK_DOWN:
    return "Down";
  case VK_MULTIPLY:
    return "Numpad *";
  case VK_ADD:
    return "Numpad +";
  case VK_SEPARATOR:
    return "Numpad Separator";
  case VK_SUBTRACT:
    return "Numpad -";
  case VK_DECIMAL:
    return "Numpad .";
  case VK_DIVIDE:
    return "Numpad /";
  default:
    return QString("Key %1").arg(keyCode);
  }
}

void HotkeyCapture::installKeyboardHook() {
  if (s_keyboardHook == nullptr) {
    s_activeInstance = this;
    s_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                      GetModuleHandle(nullptr), 0);
  }
}

void HotkeyCapture::uninstallKeyboardHook() {
  if (s_keyboardHook != nullptr) {
    UnhookWindowsHookEx(s_keyboardHook);
    s_keyboardHook = nullptr;
    s_activeInstance = nullptr;
  }
}

LRESULT CALLBACK HotkeyCapture::LowLevelKeyboardProc(int nCode, WPARAM wParam,
                                                     LPARAM lParam) {
  if (nCode == HC_ACTION && s_activeInstance != nullptr) {
    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
      KBDLLHOOKSTRUCT *pKeyboard = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
      int vkCode = pKeyboard->vkCode;

      if (vkCode == VK_ESCAPE) {
        QMetaObject::invokeMethod(
            s_activeInstance,
            [instance = s_activeInstance]() {
              if (instance->m_capturing) {
                instance->m_capturing = false;
                instance->setText(instance->m_savedText);
                instance->uninstallKeyboardHook();
                instance->clearFocus();
              }
            },
            Qt::QueuedConnection);

        return 1;
      }
    }

    if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
      KBDLLHOOKSTRUCT *pKeyboard = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
      int vkCode = pKeyboard->vkCode;

      if (vkCode != VK_CONTROL && vkCode != VK_MENU && vkCode != VK_SHIFT &&
          vkCode != VK_LWIN && vkCode != VK_RWIN) {

        bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        bool alt = GetKeyState(VK_MENU) & 0x8000;
        bool shift = GetKeyState(VK_SHIFT) & 0x8000;

        QMetaObject::invokeMethod(
            s_activeInstance,
            [instance = s_activeInstance, vkCode, ctrl, alt, shift]() {
              instance->m_capturing = false;
              instance->uninstallKeyboardHook();

              instance->addHotkey(vkCode, ctrl, alt, shift);
            },
            Qt::QueuedConnection);

        return 1;
      }
    }
  }

  return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
}

void HotkeyCapture::setHasConflict(bool hasConflict) {
  qDebug() << "HotkeyCapture::setHasConflict() - this:" << this
           << "hasConflict:" << hasConflict;

  if (m_hasConflict == hasConflict) {
    qDebug() << "  No change needed";
    return;
  }

  m_hasConflict = hasConflict;

  qDebug() << "  Updating border style";

  // Use property to add visual indicator while preserving base style
  setProperty("hasConflict", hasConflict);

  // Force style refresh
  style()->unpolish(this);
  style()->polish(this);

  qDebug() << (hasConflict ? "  Applied RED border"
                           : "  Applied NORMAL border");
}