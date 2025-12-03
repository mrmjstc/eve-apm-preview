#ifndef SETTINGBINDING_H
#define SETTINGBINDING_H

#include <QObject>
#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QColor>
#include <QMap>
#include <QVector>
#include <functional>
#include <memory>

class Config;
class HotkeyCapture;
#include "hotkeymanager.h"

class SettingBindingBase
{
public:
    virtual ~SettingBindingBase() = default;
    virtual void loadFromConfig() = 0;
    virtual void saveToConfig() = 0;
    virtual void reset() = 0;
    virtual bool hasChanged() const = 0;
    virtual QWidget* widget() const = 0;
};

template<typename WidgetType, typename ValueType>
class SettingBinding : public SettingBindingBase
{
public:
    using Getter = std::function<ValueType()>;
    using Setter = std::function<void(ValueType)>;
    using WidgetGetter = std::function<ValueType(WidgetType*)>;
    using WidgetSetter = std::function<void(WidgetType*, ValueType)>;
    using Converter = std::function<ValueType(ValueType)>;
    
    SettingBinding(WidgetType* widget,
                   Getter configGetter,
                   Setter configSetter,
                   ValueType defaultValue,
                   WidgetGetter widgetGetter,
                   WidgetSetter widgetSetter,
                   Converter toWidget = nullptr,
                   Converter toConfig = nullptr)
        : m_widget(widget)
        , m_configGetter(configGetter)
        , m_configSetter(configSetter)
        , m_defaultValue(defaultValue)
        , m_widgetGetter(widgetGetter)
        , m_widgetSetter(widgetSetter)
        , m_toWidget(toWidget)
        , m_toConfig(toConfig)
        , m_initialValue(defaultValue)
    {
    }
    
    void loadFromConfig() override
    {
        ValueType value = m_configGetter();
        if (m_toWidget) {
            value = m_toWidget(value);
        }
        m_widgetSetter(m_widget, value);
        m_initialValue = value;
    }
    
    void saveToConfig() override
    {
        ValueType value = m_widgetGetter(m_widget);
        if (m_toConfig) {
            value = m_toConfig(value);
        }
        m_configSetter(value);
    }
    
    void reset() override
    {
        ValueType value = m_defaultValue;
        if (m_toWidget) {
            value = m_toWidget(value);
        }
        m_widgetSetter(m_widget, value);
    }
    
    bool hasChanged() const override
    {
        ValueType current = m_widgetGetter(m_widget);
        return current != m_initialValue;
    }
    
    QWidget* widget() const override
    {
        return m_widget;
    }
    
private:
    WidgetType* m_widget;
    Getter m_configGetter;
    Setter m_configSetter;
    ValueType m_defaultValue;
    WidgetGetter m_widgetGetter;
    WidgetSetter m_widgetSetter;
    Converter m_toWidget;
    Converter m_toConfig;
    ValueType m_initialValue;
};

class ColorButtonBinding : public SettingBindingBase
{
public:
    using Getter = std::function<QColor()>;
    using Setter = std::function<void(QColor)>;
    
    ColorButtonBinding(QPushButton* button,
                      Getter configGetter,
                      Setter configSetter,
                      QColor defaultValue,
                      std::function<void(QPushButton*, const QColor&)> updateButtonFunc);
    
    void loadFromConfig() override;
    void saveToConfig() override;
    void reset() override;
    bool hasChanged() const override;
    QWidget* widget() const override;
    
    QColor getCurrentColor() const { return m_currentColor; }
    void setCurrentColor(const QColor& color);
    
private:
    QPushButton* m_button;
    Getter m_configGetter;
    Setter m_configSetter;
    QColor m_defaultValue;
    QColor m_currentColor;
    QColor m_initialColor;
    std::function<void(QPushButton*, const QColor&)> m_updateButtonFunc;
};

class StringListTableBinding : public SettingBindingBase
{
public:
    using Getter = std::function<QStringList()>;
    using Setter = std::function<void(const QStringList&)>;
    
    StringListTableBinding(QTableWidget* table,
                          int column,
                          Getter configGetter,
                          Setter configSetter,
                          QStringList defaultValue);
    
    void loadFromConfig() override;
    void saveToConfig() override;
    void reset() override;
    bool hasChanged() const override;
    QWidget* widget() const override;
    
    QStringList getCurrentList() const;
    
private:
    QTableWidget* m_table;
    int m_column;
    Getter m_configGetter;
    Setter m_configSetter;
    QStringList m_defaultValue;
    QStringList m_initialValue;
};

class CharacterHotkeyTableBinding : public SettingBindingBase
{
public:
    using Getter = std::function<QHash<QString, HotkeyBinding>()>;
    using Setter = std::function<void(const QHash<QString, HotkeyBinding>&)>;
    
    CharacterHotkeyTableBinding(QTableWidget* table,
                               Getter configGetter,
                               Setter configSetter);
    
    void loadFromConfig() override;
    void saveToConfig() override;
    void reset() override;
    bool hasChanged() const override;
    QWidget* widget() const override;
    
private:
    QTableWidget* m_table;
    Getter m_configGetter;
    Setter m_configSetter;
    QHash<QString, HotkeyBinding> m_initialValue;
    QHash<QString, QVector<HotkeyBinding>> m_initialMultiHotkeys;
};

class CharacterColorTableBinding : public SettingBindingBase
{
public:
    using ColorUpdateFunc = std::function<void(QPushButton*, const QColor&)>;
    using ButtonConnector = std::function<void(QPushButton*)>;
    
    CharacterColorTableBinding(QTableWidget* table,
                              ColorUpdateFunc colorUpdateFunc,
                              ButtonConnector buttonConnector);
    
    void loadFromConfig() override;
    void saveToConfig() override;
    void reset() override;
    bool hasChanged() const override;
    QWidget* widget() const override;
    
private:
    QTableWidget* m_table;
    ColorUpdateFunc m_colorUpdateFunc;
    ButtonConnector m_buttonConnector;
    QHash<QString, QColor> m_initialValue;
};

class HotkeyCaptureBinding : public SettingBindingBase
{
public:
    using Getter = std::function<HotkeyBinding()>;
    using Setter = std::function<void(const HotkeyBinding&)>;
    
    HotkeyCaptureBinding(HotkeyCapture* widget,
                        Getter configGetter,
                        Setter configSetter,
                        HotkeyBinding defaultValue);
    
    void loadFromConfig() override;
    void saveToConfig() override;
    void reset() override;
    bool hasChanged() const override;
    QWidget* widget() const override;
    
private:
    HotkeyCapture* m_widget;
    Getter m_configGetter;
    Setter m_configSetter;
    HotkeyBinding m_defaultValue;
    HotkeyBinding m_initialValue;
};

class CycleGroupTableBinding : public SettingBindingBase
{
public:
    using Getter = std::function<QHash<QString, CycleGroup>()>;
    using Setter = std::function<void(const QHash<QString, CycleGroup>&)>;
    using ButtonConnector = std::function<void(QPushButton*)>;
    
    CycleGroupTableBinding(QTableWidget* table,
                          Getter configGetter,
                          Setter configSetter,
                          ButtonConnector buttonConnector);
    
    void loadFromConfig() override;
    void saveToConfig() override;
    void reset() override;
    bool hasChanged() const override;
    QWidget* widget() const override;
    
private:
    QTableWidget* m_table;
    Getter m_configGetter;
    Setter m_configSetter;
    ButtonConnector m_buttonConnector;
    QHash<QString, CycleGroup> m_initialValue;
};

class FontBinding : public SettingBindingBase
{
public:
    using Getter = std::function<QFont()>;
    using Setter = std::function<void(const QFont&)>;
    
    FontBinding(QComboBox* fontCombo,
               QSpinBox* sizeSpinBox,
               Getter configGetter,
               Setter configSetter,
               QFont defaultValue);
    
    void loadFromConfig() override;
    void saveToConfig() override;
    void reset() override;
    bool hasChanged() const override;
    QWidget* widget() const override;
    
private:
    QComboBox* m_fontCombo;
    QSpinBox* m_sizeSpinBox;
    Getter m_configGetter;
    Setter m_configSetter;
    QFont m_defaultValue;
    QFont m_initialValue;
};

class SettingBindingManager
{
public:
    SettingBindingManager() = default;
    ~SettingBindingManager() = default;
    
    void addBinding(std::unique_ptr<SettingBindingBase> binding);
    void loadAll();
    void saveAll();
    void resetAll();
    bool hasAnyChanges() const;
    
    SettingBindingBase* findBinding(QWidget* widget) const;
    
    void clear();
    
private:
    std::vector<std::unique_ptr<SettingBindingBase>> m_bindings;
};

namespace BindingHelpers {

    inline std::unique_ptr<SettingBindingBase> bindSpinBox(
        QSpinBox* widget,
        std::function<int()> getter,
        std::function<void(int)> setter,
        int defaultValue)
    {
        return std::make_unique<SettingBinding<QSpinBox, int>>(
            widget,
            getter,
            setter,
            defaultValue,
            [](QSpinBox* w) { return w->value(); },
            [](QSpinBox* w, int v) { w->setValue(v); }
        );
    }
    
    inline std::unique_ptr<SettingBindingBase> bindCheckBox(
        QCheckBox* widget,
        std::function<bool()> getter,
        std::function<void(bool)> setter,
        bool defaultValue)
    {
        return std::make_unique<SettingBinding<QCheckBox, bool>>(
            widget,
            getter,
            setter,
            defaultValue,
            [](QCheckBox* w) { return w->isChecked(); },
            [](QCheckBox* w, bool v) { w->setChecked(v); }
        );
    }
    
    inline std::unique_ptr<SettingBindingBase> bindComboBox(
        QComboBox* widget,
        std::function<int()> getter,
        std::function<void(int)> setter,
        int defaultValue)
    {
        return std::make_unique<SettingBinding<QComboBox, int>>(
            widget,
            getter,
            setter,
            defaultValue,
            [](QComboBox* w) { return w->currentIndex(); },
            [](QComboBox* w, int v) { w->setCurrentIndex(v); }
        );
    }
    
    inline std::unique_ptr<ColorButtonBinding> bindColorButton(
        QPushButton* button,
        std::function<QColor()> getter,
        std::function<void(QColor)> setter,
        QColor defaultValue,
        std::function<void(QPushButton*, const QColor&)> updateFunc)
    {
        return std::make_unique<ColorButtonBinding>(
            button,
            getter,
            setter,
            defaultValue,
            updateFunc
        );
    }
    
    inline std::unique_ptr<StringListTableBinding> bindStringListTable(
        QTableWidget* table,
        int column,
        std::function<QStringList()> getter,
        std::function<void(const QStringList&)> setter,
        QStringList defaultValue = QStringList())
    {
        return std::make_unique<StringListTableBinding>(
            table,
            column,
            getter,
            setter,
            defaultValue
        );
    }
    
    inline std::unique_ptr<CharacterHotkeyTableBinding> bindCharacterHotkeyTable(
        QTableWidget* table,
        std::function<QHash<QString, HotkeyBinding>()> getter,
        std::function<void(const QHash<QString, HotkeyBinding>&)> setter)
    {
        return std::make_unique<CharacterHotkeyTableBinding>(
            table,
            getter,
            setter
        );
    }
    
    inline std::unique_ptr<HotkeyCaptureBinding> bindHotkeyCapture(
        HotkeyCapture* widget,
        std::function<HotkeyBinding()> getter,
        std::function<void(const HotkeyBinding&)> setter,
        HotkeyBinding defaultValue = HotkeyBinding())
    {
        return std::make_unique<HotkeyCaptureBinding>(
            widget,
            getter,
            setter,
            defaultValue
        );
    }
    
    inline std::unique_ptr<CycleGroupTableBinding> bindCycleGroupTable(
        QTableWidget* table,
        std::function<QHash<QString, CycleGroup>()> getter,
        std::function<void(const QHash<QString, CycleGroup>&)> setter,
        std::function<void(QPushButton*)> buttonConnector)
    {
        return std::make_unique<CycleGroupTableBinding>(
            table,
            getter,
            setter,
            buttonConnector
        );
    }
    
    inline std::unique_ptr<CharacterColorTableBinding> bindCharacterColorTable(
        QTableWidget* table,
        std::function<void(QPushButton*, const QColor&)> colorUpdateFunc,
        std::function<void(QPushButton*)> buttonConnector)
    {
        return std::make_unique<CharacterColorTableBinding>(
            table,
            colorUpdateFunc,
            buttonConnector
        );
    }
    
    inline std::unique_ptr<FontBinding> bindFont(
        QComboBox* fontCombo,
        QSpinBox* sizeSpinBox,
        std::function<QFont()> getter,
        std::function<void(const QFont&)> setter,
        QFont defaultValue)
    {
        return std::make_unique<FontBinding>(
            fontCombo,
            sizeSpinBox,
            getter,
            setter,
            defaultValue
        );
    }

} 

#endif 
