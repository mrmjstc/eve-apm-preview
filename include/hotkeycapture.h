#ifndef HOTKEYCAPTURE_H
#define HOTKEYCAPTURE_H

#include <QLineEdit>
#include <QKeyEvent>
#include <QAbstractNativeEventFilter>
#include <QVector>
#include <Windows.h>

struct HotkeyCombination {
    int keyCode;
    bool ctrl;
    bool alt;
    bool shift;
    
    HotkeyCombination(int k = 0, bool c = false, bool a = false, bool s = false)
        : keyCode(k), ctrl(c), alt(a), shift(s) {}
    
    bool isValid() const { return keyCode != 0; }
    bool operator==(const HotkeyCombination& other) const {
        return keyCode == other.keyCode && ctrl == other.ctrl && 
               alt == other.alt && shift == other.shift;
    }
};

class HotkeyCapture : public QLineEdit, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit HotkeyCapture(QWidget *parent = nullptr);
    ~HotkeyCapture();
    
    void setHotkey(int keyCode, bool ctrl, bool alt, bool shift);
    void clearHotkey();
    int getKeyCode() const { return m_hotkeys.isEmpty() ? 0 : m_hotkeys.first().keyCode; }
    bool getCtrl() const { return m_hotkeys.isEmpty() ? false : m_hotkeys.first().ctrl; }
    bool getAlt() const { return m_hotkeys.isEmpty() ? false : m_hotkeys.first().alt; }
    bool getShift() const { return m_hotkeys.isEmpty() ? false : m_hotkeys.first().shift; }
    
    void setHotkeys(const QVector<HotkeyCombination>& hotkeys);
    void addHotkey(int keyCode, bool ctrl, bool alt, bool shift);
    void addHotkey(const HotkeyCombination& hotkey);
    QVector<HotkeyCombination> getHotkeys() const { return m_hotkeys; }
    void removeHotkeyAt(int index);
    bool hasMultipleHotkeys() const { return m_hotkeys.size() > 1; }
    
signals:
    void hotkeyChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void updateDisplay();
    QString keyCodeToString(int keyCode) const;
    QString formatHotkey(const HotkeyCombination& hk) const;
    void installKeyboardHook();
    void uninstallKeyboardHook();
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    
    QVector<HotkeyCombination> m_hotkeys;
    bool m_capturing;
    QString m_savedText;
    
    static HotkeyCapture* s_activeInstance;
    static HHOOK s_keyboardHook;
};

#endif 
