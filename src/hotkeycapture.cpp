#include "hotkeycapture.h"
#include "stylesheet.h"
#include <Windows.h>
#include <QApplication>

HotkeyCapture* HotkeyCapture::s_activeInstance = nullptr;
HHOOK HotkeyCapture::s_keyboardHook = nullptr;

HotkeyCapture::HotkeyCapture(QWidget *parent)
    : QLineEdit(parent)
    , m_keyCode(0)
    , m_ctrl(false)
    , m_alt(false)
    , m_shift(false)
    , m_capturing(false)
{
    setReadOnly(true);
    setPlaceholderText("Click to set hotkey...");
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::PointingHandCursor);
    
    setStyleSheet(StyleSheet::getHotkeyCaptureStyleSheet());
}

HotkeyCapture::~HotkeyCapture()
{
    uninstallKeyboardHook();
    QApplication::instance()->removeNativeEventFilter(this);
}

void HotkeyCapture::setHotkey(int keyCode, bool ctrl, bool alt, bool shift)
{
    m_keyCode = keyCode;
    m_ctrl = ctrl;
    m_alt = alt;
    m_shift = shift;
    updateDisplay();
}

void HotkeyCapture::clearHotkey()
{
    m_keyCode = 0;
    m_ctrl = false;
    m_alt = false;
    m_shift = false;
    updateDisplay();
    emit hotkeyChanged();
}

void HotkeyCapture::keyPressEvent(QKeyEvent *event)
{
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
    if (key == Qt::Key_Control || key == Qt::Key_Alt || 
        key == Qt::Key_Shift || key == Qt::Key_Meta) {
        event->accept();
        return;
    }
    
    m_ctrl = event->modifiers() & Qt::ControlModifier;
    m_alt = event->modifiers() & Qt::AltModifier;
    m_shift = event->modifiers() & Qt::ShiftModifier;
    
    m_keyCode = 0;
    
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        m_keyCode = VK_F1 + (key - Qt::Key_F1);
    }
    else if (key >= Qt::Key_F13 && key <= Qt::Key_F24) {
        m_keyCode = VK_F13 + (key - Qt::Key_F13);
    }
    else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        m_keyCode = '0' + (key - Qt::Key_0);
    }
    else if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        m_keyCode = 'A' + (key - Qt::Key_A);
    }
    else {
        switch (key) {
            case Qt::Key_Insert: m_keyCode = VK_INSERT; break;
            case Qt::Key_Delete: m_keyCode = VK_DELETE; break;
            case Qt::Key_Home: m_keyCode = VK_HOME; break;
            case Qt::Key_End: m_keyCode = VK_END; break;
            case Qt::Key_PageUp: m_keyCode = VK_PRIOR; break;
            case Qt::Key_PageDown: m_keyCode = VK_NEXT; break;
            case Qt::Key_Pause: m_keyCode = VK_PAUSE; break;
            case Qt::Key_ScrollLock: m_keyCode = VK_SCROLL; break;
            case Qt::Key_Space: m_keyCode = VK_SPACE; break;
            case Qt::Key_Return:
            case Qt::Key_Enter: m_keyCode = VK_RETURN; break;
            case Qt::Key_Tab: m_keyCode = VK_TAB; break;
            case Qt::Key_Backspace: m_keyCode = VK_BACK; break;
            case Qt::Key_Left: m_keyCode = VK_LEFT; break;
            case Qt::Key_Right: m_keyCode = VK_RIGHT; break;
            case Qt::Key_Up: m_keyCode = VK_UP; break;
            case Qt::Key_Down: m_keyCode = VK_DOWN; break;
            default:
                event->accept();
                return;
        }
    }
    
    event->accept();
    updateDisplay();
    emit hotkeyChanged();
}

void HotkeyCapture::focusInEvent(QFocusEvent *event)
{
    QLineEdit::focusInEvent(event);
    if (m_capturing) {
        installKeyboardHook();
    }
    selectAll();
}

void HotkeyCapture::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
    uninstallKeyboardHook();
    
    if (m_capturing) {
        m_capturing = false;
        setText(m_savedText);
    }
}

void HotkeyCapture::mousePressEvent(QMouseEvent *event)
{
    QLineEdit::mousePressEvent(event);
    
    if (!m_capturing) {
        m_capturing = true;
        m_savedText = text();
        setText("Press a key...");
        installKeyboardHook();
    }
}

bool HotkeyCapture::event(QEvent *e)
{
    if (e->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(e);
        
        if (keyEvent->nativeVirtualKey() != 0) {
            int vkCode = keyEvent->nativeVirtualKey();
            
            if (vkCode == VK_CONTROL || vkCode == VK_MENU || 
                vkCode == VK_SHIFT || vkCode == VK_LWIN || vkCode == VK_RWIN) {
                return QLineEdit::event(e);
            }
            
            m_ctrl = keyEvent->modifiers() & Qt::ControlModifier;
            m_alt = keyEvent->modifiers() & Qt::AltModifier;
            m_shift = keyEvent->modifiers() & Qt::ShiftModifier;
            
            m_keyCode = vkCode;
            
            updateDisplay();
            emit hotkeyChanged();
            
            e->accept();
            return true;
        }
    }
    
    return QLineEdit::event(e);
}

bool HotkeyCapture::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
}

void HotkeyCapture::updateDisplay()
{
    if (m_keyCode == 0) {
        setText("");
        return;
    }
    
    QString text;
    if (m_ctrl) text += "Ctrl+";
    if (m_alt) text += "Alt+";
    if (m_shift) text += "Shift+";
    text += keyCodeToString(m_keyCode);
    
    setText(text);
}

QString HotkeyCapture::keyCodeToString(int keyCode) const
{
    if (keyCode >= VK_F1 && keyCode <= VK_F12) {
        return QString("F%1").arg(keyCode - VK_F1 + 1);
    }
    if (keyCode >= VK_F13 && keyCode <= VK_F24) {
        return QString("F%1").arg(keyCode - VK_F13 + 13);
    }
    
    // Handle numpad keys
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
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case VK_PAUSE: return "Pause";
        case VK_SCROLL: return "Scroll Lock";
        case VK_SPACE: return "Space";
        case VK_RETURN: return "Enter";
        case VK_ESCAPE: return "Escape";
        case VK_TAB: return "Tab";
        case VK_BACK: return "Backspace";
        case VK_LEFT: return "Left";
        case VK_RIGHT: return "Right";
        case VK_UP: return "Up";
        case VK_DOWN: return "Down";
        case VK_MULTIPLY: return "Numpad *";
        case VK_ADD: return "Numpad +";
        case VK_SEPARATOR: return "Numpad Separator";
        case VK_SUBTRACT: return "Numpad -";
        case VK_DECIMAL: return "Numpad .";
        case VK_DIVIDE: return "Numpad /";
        default: return QString("Key %1").arg(keyCode);
    }
}

void HotkeyCapture::installKeyboardHook()
{
    if (s_keyboardHook == nullptr) {
        s_activeInstance = this;
        s_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
    }
}

void HotkeyCapture::uninstallKeyboardHook()
{
    if (s_keyboardHook != nullptr) {
        UnhookWindowsHookEx(s_keyboardHook);
        s_keyboardHook = nullptr;
        s_activeInstance = nullptr;
    }
}

LRESULT CALLBACK HotkeyCapture::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_activeInstance != nullptr) {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            KBDLLHOOKSTRUCT* pKeyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            int vkCode = pKeyboard->vkCode;
            
            if (vkCode == VK_ESCAPE) {
                QMetaObject::invokeMethod(s_activeInstance, [instance = s_activeInstance]() {
                    if (instance->m_capturing) {
                        instance->m_capturing = false;
                        instance->setText(instance->m_savedText);
                        instance->uninstallKeyboardHook();
                        instance->clearFocus();
                    }
                }, Qt::QueuedConnection);
                
                return 1;
            }
            
            if (vkCode != VK_CONTROL && vkCode != VK_MENU && 
                vkCode != VK_SHIFT && vkCode != VK_LWIN && vkCode != VK_RWIN) {
                
                s_activeInstance->m_ctrl = GetKeyState(VK_CONTROL) & 0x8000;
                s_activeInstance->m_alt = GetKeyState(VK_MENU) & 0x8000;
                s_activeInstance->m_shift = GetKeyState(VK_SHIFT) & 0x8000;
                
                s_activeInstance->m_keyCode = vkCode;
                
                QMetaObject::invokeMethod(s_activeInstance, [instance = s_activeInstance]() {
                    instance->m_capturing = false;
                    instance->uninstallKeyboardHook();
                    
                    instance->updateDisplay();
                    emit instance->hotkeyChanged();
                }, Qt::QueuedConnection);
                
                return 1;
            }
        }
    }
    
    return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
}
