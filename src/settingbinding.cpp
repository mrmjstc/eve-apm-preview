#include "settingbinding.h"
#include "config.h"
#include "hotkeycapture.h"
#include "hotkeymanager.h"
#include "stylesheet.h"
#include <QPalette>
#include <QTableWidgetItem>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLineEdit>

ColorButtonBinding::ColorButtonBinding(QPushButton* button,
                                     Getter configGetter,
                                     Setter configSetter,
                                     QColor defaultValue,
                                     std::function<void(QPushButton*, const QColor&)> updateButtonFunc)
    : m_button(button)
    , m_configGetter(configGetter)
    , m_configSetter(configSetter)
    , m_defaultValue(defaultValue)
    , m_currentColor(defaultValue)
    , m_initialColor(defaultValue)
    , m_updateButtonFunc(updateButtonFunc)
{
}

void ColorButtonBinding::loadFromConfig()
{
    m_currentColor = m_configGetter();
    m_initialColor = m_currentColor;
    m_updateButtonFunc(m_button, m_currentColor);
}

void ColorButtonBinding::saveToConfig()
{
    m_configSetter(m_currentColor);
}

void ColorButtonBinding::reset()
{
    m_currentColor = m_defaultValue;
    m_updateButtonFunc(m_button, m_currentColor);
}

bool ColorButtonBinding::hasChanged() const
{
    return m_currentColor != m_initialColor;
}

QWidget* ColorButtonBinding::widget() const
{
    return m_button;
}

void ColorButtonBinding::setCurrentColor(const QColor& color)
{
    m_currentColor = color;
    m_updateButtonFunc(m_button, m_currentColor);
}

StringListTableBinding::StringListTableBinding(QTableWidget* table,
                                             int column,
                                             Getter configGetter,
                                             Setter configSetter,
                                             QStringList defaultValue)
    : m_table(table)
    , m_column(column)
    , m_configGetter(configGetter)
    , m_configSetter(configSetter)
    , m_defaultValue(defaultValue)
    , m_initialValue(defaultValue)
{
}

void StringListTableBinding::loadFromConfig()
{
    QStringList items = m_configGetter();
    m_initialValue = items;
    
    m_table->setRowCount(0);
    
    bool isProcessNamesTable = m_table->objectName() == "processNamesTable";
    bool isNeverMinimizeTable = (m_table->columnCount() == 2 && m_column == 0 && !isProcessNamesTable);
    
    for (const QString& item : items) {
        if (isProcessNamesTable && item.compare("exefile.exe", Qt::CaseInsensitive) == 0) {
            continue;
        }
        
        int row = m_table->rowCount();
        m_table->insertRow(row);
        
        QTableWidgetItem* tableItem = new QTableWidgetItem(item);
        tableItem->setFlags(tableItem->flags() | Qt::ItemIsEditable); 
        m_table->setItem(row, m_column, tableItem);
        
        if (isNeverMinimizeTable || isProcessNamesTable) {
            QWidget* buttonContainer = new QWidget();
            QHBoxLayout* buttonLayout = new QHBoxLayout(buttonContainer);
            buttonLayout->setContentsMargins(0, 0, 0, 0);
            
            QPushButton* deleteButton = new QPushButton("×");
            deleteButton->setFixedSize(24, 24);
            deleteButton->setStyleSheet(
                "QPushButton {"
                "    background-color: #3a3a3a;"
                "    color: #ffffff;"
                "    border: 1px solid #555555;"
                "    border-radius: 4px;"
                "    font-size: 16px;"
                "    font-weight: bold;"
                "    padding: 0px;"
                "}"
                "QPushButton:hover {"
                "    background-color: #e74c3c;"
                "    border: 1px solid #c0392b;"
                "}"
                "QPushButton:pressed {"
                "    background-color: #c0392b;"
                "}"
            );
            
            QObject::connect(deleteButton, &QPushButton::clicked, [this, row]() {
                m_table->removeRow(row);
            });
            
            buttonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
            m_table->setCellWidget(row, 1, buttonContainer);
        }
    }
}

void StringListTableBinding::saveToConfig()
{
    QStringList items = getCurrentList();
    
    bool isProcessNamesTable = m_table->objectName() == "processNamesTable";
    if (isProcessNamesTable) {
        bool hasExeFile = false;
        for (const QString& item : items) {
            if (item.compare("exefile.exe", Qt::CaseInsensitive) == 0) {
                hasExeFile = true;
                break;
            }
        }
        
        if (!hasExeFile) {
            items.prepend("exefile.exe");
        }
    }
    
    m_configSetter(items);
}

void StringListTableBinding::reset()
{
    m_table->setRowCount(0);
    
    bool isProcessNamesTable = m_table->objectName() == "processNamesTable";
    
    for (const QString& item : m_defaultValue) {
        if (isProcessNamesTable && item.compare("exefile.exe", Qt::CaseInsensitive) == 0) {
            continue;
        }
        
        int row = m_table->rowCount();
        m_table->insertRow(row);
        
        QTableWidgetItem* tableItem = new QTableWidgetItem(item);
        tableItem->setFlags(tableItem->flags() | Qt::ItemIsEditable); 
        m_table->setItem(row, m_column, tableItem);
    }
}

bool StringListTableBinding::hasChanged() const
{
    return getCurrentList() != m_initialValue;
}

QWidget* StringListTableBinding::widget() const
{
    return m_table;
}

QStringList StringListTableBinding::getCurrentList() const
{
    QStringList items;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem* item = m_table->item(row, m_column);
        if (item) {
            items.append(item->text());
        }
    }
    return items;
}

CharacterHotkeyTableBinding::CharacterHotkeyTableBinding(QTableWidget* table,
                                                       Getter configGetter,
                                                       Setter configSetter)
    : m_table(table)
    , m_configGetter(configGetter)
    , m_configSetter(configSetter)
{
}

void CharacterHotkeyTableBinding::loadFromConfig()
{
    QHash<QString, HotkeyBinding> hotkeys = m_configGetter();
    m_initialValue = hotkeys;
    
    for (int row = m_table->rowCount() - 1; row >= 0; --row) {
        for (int col = 0; col < m_table->columnCount(); ++col) {
            QWidget* widget = m_table->cellWidget(row, col);
            if (widget) {
                m_table->removeCellWidget(row, col);
                widget->deleteLater();
            }
        }
    }
    m_table->setRowCount(0);
    
    HotkeyManager* mgr = HotkeyManager::instance();
    QHash<QString, QVector<HotkeyBinding>> multiHotkeys = mgr ? mgr->getAllCharacterMultiHotkeys() : QHash<QString, QVector<HotkeyBinding>>();
    
    m_initialMultiHotkeys = multiHotkeys;
    
    QSet<QString> processedChars;
    
    for (auto it = multiHotkeys.constBegin(); it != multiHotkeys.constEnd(); ++it) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        
        QLineEdit* nameEdit = new QLineEdit(it.key());
        nameEdit->setStyleSheet(
            "QLineEdit {"
            "   background-color: transparent;"
            "   color: #ffffff;"
            "   border: none;"
            "   padding: 2px 4px;"
            "}"
            "QLineEdit:focus {"
            "   background-color: #353535;"
            "}"
        );
        m_table->setCellWidget(row, 0, nameEdit);
        
        QWidget *hotkeyWidget = new QWidget();
        QHBoxLayout *hotkeyLayout = new QHBoxLayout(hotkeyWidget);
        hotkeyLayout->setContentsMargins(0, 0, 4, 0);
        hotkeyLayout->setSpacing(4);
        
        HotkeyCapture *hotkeyCapture = new HotkeyCapture();
        
        QVector<HotkeyCombination> combinations;
        for (const HotkeyBinding& binding : it.value()) {
            combinations.append(HotkeyCombination(binding.keyCode, binding.ctrl, binding.alt, binding.shift));
        }
        hotkeyCapture->setHotkeys(combinations);
        
        QPushButton *clearButton = new QPushButton("×");
        clearButton->setFixedSize(24, 24);
        clearButton->setStyleSheet(
            "QPushButton {"
            "    background-color: #3a3a3a;"
            "    color: #a0a0a0;"
            "    border: 1px solid #555555;"
            "    border-radius: 3px;"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    padding: 0px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #4a4a4a;"
            "    color: #ffffff;"
            "    border: 1px solid #666666;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #2a2a2a;"
            "}"
        );
        clearButton->setToolTip("Clear hotkey");
        QObject::connect(clearButton, &QPushButton::clicked, [hotkeyCapture]() {
            hotkeyCapture->clearHotkey();
        });
        
        hotkeyLayout->addWidget(hotkeyCapture, 1);
        hotkeyLayout->addWidget(clearButton, 0);
        
        m_table->setCellWidget(row, 1, hotkeyWidget);
        
        QWidget *deleteContainer = new QWidget();
        deleteContainer->setStyleSheet("QWidget { background-color: transparent; }");
        QHBoxLayout *deleteLayout = new QHBoxLayout(deleteContainer);
        deleteLayout->setContentsMargins(0, 0, 0, 0);
        deleteLayout->setAlignment(Qt::AlignCenter);
        
        QPushButton *deleteButton = new QPushButton("×");
        deleteButton->setFixedSize(24, 24);
        deleteButton->setStyleSheet(
            "QPushButton {"
            "    background-color: #3a3a3a;"
            "    color: #e74c3c;"
            "    border: 1px solid #555555;"
            "    border-radius: 3px;"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    padding: 0px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #e74c3c;"
            "    color: #ffffff;"
            "    border: 1px solid #e74c3c;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #c0392b;"
            "}"
        );
        deleteButton->setToolTip("Delete this character hotkey");
        deleteButton->setCursor(Qt::PointingHandCursor);
        
        QObject::connect(deleteButton, &QPushButton::clicked, [this, deleteButton]() {
            for (int i = 0; i < m_table->rowCount(); ++i) {
                QWidget *widget = m_table->cellWidget(i, 2);
                if (widget && widget->findChild<QPushButton*>() == deleteButton) {
                    m_table->removeRow(i);
                    break;
                }
            }
        });
        
        deleteLayout->addWidget(deleteButton);
        m_table->setCellWidget(row, 2, deleteContainer);
        
        processedChars.insert(it.key());
    }
    
    for (auto it = hotkeys.constBegin(); it != hotkeys.constEnd(); ++it) {
        if (processedChars.contains(it.key())) {
            continue; 
        }
        
        int row = m_table->rowCount();
        m_table->insertRow(row);
        
        QLineEdit* nameEdit = new QLineEdit(it.key());
        nameEdit->setStyleSheet(
            "QLineEdit {"
            "   background-color: transparent;"
            "   color: #ffffff;"
            "   border: none;"
            "   padding: 2px 4px;"
            "}"
            "QLineEdit:focus {"
            "   background-color: #353535;"
            "}"
        );
        m_table->setCellWidget(row, 0, nameEdit);
        
        QWidget *hotkeyWidget = new QWidget();
        QHBoxLayout *hotkeyLayout = new QHBoxLayout(hotkeyWidget);
        hotkeyLayout->setContentsMargins(0, 0, 4, 0);
        hotkeyLayout->setSpacing(4);
        
        HotkeyCapture *hotkeyCapture = new HotkeyCapture();
        hotkeyCapture->setHotkey(it.value().keyCode, it.value().ctrl,
                                it.value().alt, it.value().shift);
        
        QPushButton *clearButton = new QPushButton("×");
        clearButton->setFixedSize(24, 24);
        clearButton->setStyleSheet(
            "QPushButton {"
            "    background-color: #3a3a3a;"
            "    color: #a0a0a0;"
            "    border: 1px solid #555555;"
            "    border-radius: 3px;"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    padding: 0px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #4a4a4a;"
            "    color: #ffffff;"
            "    border: 1px solid #666666;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #2a2a2a;"
            "}"
        );
        clearButton->setToolTip("Clear hotkey");
        QObject::connect(clearButton, &QPushButton::clicked, [hotkeyCapture]() {
            hotkeyCapture->clearHotkey();
        });
        
        hotkeyLayout->addWidget(hotkeyCapture, 1);
        hotkeyLayout->addWidget(clearButton, 0);
        
        m_table->setCellWidget(row, 1, hotkeyWidget);
        
        QWidget *deleteContainer = new QWidget();
        deleteContainer->setStyleSheet("QWidget { background-color: transparent; }");
        QHBoxLayout *deleteLayout = new QHBoxLayout(deleteContainer);
        deleteLayout->setContentsMargins(0, 0, 0, 0);
        deleteLayout->setAlignment(Qt::AlignCenter);
        
        QPushButton *deleteButton = new QPushButton("×");
        deleteButton->setFixedSize(24, 24);
        deleteButton->setStyleSheet(
            "QPushButton {"
            "    background-color: #3a3a3a;"
            "    color: #e74c3c;"
            "    border: 1px solid #555555;"
            "    border-radius: 3px;"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    padding: 0px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #e74c3c;"
            "    color: #ffffff;"
            "    border: 1px solid #e74c3c;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #c0392b;"
            "}"
        );
        deleteButton->setToolTip("Delete this character hotkey");
        deleteButton->setCursor(Qt::PointingHandCursor);
        
        QObject::connect(deleteButton, &QPushButton::clicked, [this, deleteButton]() {
            for (int i = 0; i < m_table->rowCount(); ++i) {
                QWidget *widget = m_table->cellWidget(i, 2);
                if (widget && widget->findChild<QPushButton*>() == deleteButton) {
                    m_table->removeRow(i);
                    break;
                }
            }
        });
        
        deleteLayout->addWidget(deleteButton);
        m_table->setCellWidget(row, 2, deleteContainer);
    }
}

void CharacterHotkeyTableBinding::saveToConfig()
{
    QHash<QString, HotkeyBinding> hotkeys;
    HotkeyManager* mgr = HotkeyManager::instance();
    
    if (mgr) {
        QHash<QString, HotkeyBinding> existing = mgr->getAllCharacterHotkeys();
        for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
            mgr->removeCharacterHotkey(it.key());
        }
    }
    
    QHash<QString, QVector<HotkeyBinding>> newMultiHotkeys;
    
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QLineEdit* nameEdit = qobject_cast<QLineEdit*>(m_table->cellWidget(row, 0));
        QWidget* hotkeyWidget = m_table->cellWidget(row, 1);
        
        HotkeyCapture* hotkeyCapture = nullptr;
        if (hotkeyWidget) {
            hotkeyCapture = hotkeyWidget->findChild<HotkeyCapture*>();
        }
        
        if (nameEdit && hotkeyCapture) {
            QString charName = nameEdit->text().trimmed();
            if (!charName.isEmpty()) {
                QVector<HotkeyCombination> combinations = hotkeyCapture->getHotkeys();
                
                if (!combinations.isEmpty()) {
                    QVector<HotkeyBinding> bindings;
                    for (const HotkeyCombination& comb : combinations) {
                        bindings.append(HotkeyBinding(comb.keyCode, comb.ctrl, comb.alt, comb.shift, true));
                    }
                    
                    if (mgr) {
                        mgr->setCharacterHotkeys(charName, bindings);
                    }
                    
                    newMultiHotkeys[charName] = bindings;
                    
                    hotkeys[charName] = bindings.first();
                }
            }
        }
    }
    
    m_configSetter(hotkeys);
    
    m_initialMultiHotkeys = newMultiHotkeys;
}

void CharacterHotkeyTableBinding::reset()
{
    for (int row = m_table->rowCount() - 1; row >= 0; --row) {
        for (int col = 0; col < m_table->columnCount(); ++col) {
            QWidget* widget = m_table->cellWidget(row, col);
            if (widget) {
                m_table->removeCellWidget(row, col);
                widget->deleteLater();
            }
        }
    }
    m_table->setRowCount(0);
}

bool CharacterHotkeyTableBinding::hasChanged() const
{
    QHash<QString, QVector<HotkeyBinding>> currentMulti;
    
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QLineEdit* nameEdit = qobject_cast<QLineEdit*>(m_table->cellWidget(row, 0));
        QWidget* hotkeyWidget = m_table->cellWidget(row, 1);
        
        HotkeyCapture* hotkeyCapture = nullptr;
        if (hotkeyWidget) {
            hotkeyCapture = hotkeyWidget->findChild<HotkeyCapture*>();
        }
        
        if (nameEdit && hotkeyCapture) {
            QString charName = nameEdit->text().trimmed();
            if (!charName.isEmpty()) {
                QVector<HotkeyCombination> combinations = hotkeyCapture->getHotkeys();
                QVector<HotkeyBinding> bindings;
                
                for (const HotkeyCombination& comb : combinations) {
                    bindings.append(HotkeyBinding(comb.keyCode, comb.ctrl, comb.alt, comb.shift, true));
                }
                
                if (!bindings.isEmpty()) {
                    currentMulti[charName] = bindings;
                }
            }
        }
    }
    
    if (currentMulti.size() != m_initialMultiHotkeys.size()) {
        return true;
    }
    
    for (auto it = currentMulti.constBegin(); it != currentMulti.constEnd(); ++it) {
        const QString& charName = it.key();
        const QVector<HotkeyBinding>& currentBindings = it.value();
        
        if (!m_initialMultiHotkeys.contains(charName)) {
            return true;
        }
        
        const QVector<HotkeyBinding>& initialBindings = m_initialMultiHotkeys[charName];
        
        if (currentBindings.size() != initialBindings.size()) {
            return true;
        }
        
        for (int i = 0; i < currentBindings.size(); ++i) {
            if (!(currentBindings[i] == initialBindings[i])) {
                return true;
            }
        }
    }
    
    return false;
}

QWidget* CharacterHotkeyTableBinding::widget() const
{
    return m_table;
}

CycleGroupTableBinding::CycleGroupTableBinding(QTableWidget* table,
                                             Getter configGetter,
                                             Setter configSetter,
                                             ButtonConnector buttonConnector)
    : m_table(table)
    , m_configGetter(configGetter)
    , m_configSetter(configSetter)
    , m_buttonConnector(buttonConnector)
{
}

void CycleGroupTableBinding::loadFromConfig()
{
    QHash<QString, CycleGroup> groups = m_configGetter();
    m_initialValue = groups;
    
    for (int row = m_table->rowCount() - 1; row >= 0; --row) {
        for (int col = 0; col < m_table->columnCount(); ++col) {
            QWidget* widget = m_table->cellWidget(row, col);
            if (widget) {
                m_table->removeCellWidget(row, col);
                widget->deleteLater();
            }
        }
    }
    m_table->setRowCount(0);
    
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        
        QString cellStyle = 
            "QLineEdit {"
            "   background-color: transparent;"
            "   color: #ffffff;"
            "   border: none;"
            "   padding: 2px 4px;"
            "}"
            "QLineEdit:focus {"
            "   background-color: #353535;"
            "}";
        
        QString buttonStyle = 
            "QPushButton {"
            "   background-color: #404040;"
            "   color: #ffffff;"
            "   border: 1px solid #555555;"
            "   padding: 6px 12px;"
            "   border-radius: 4px;"
            "   font-size: 12px;"
            "   text-align: left;"
            "}"
            "QPushButton:hover {"
            "   background-color: #4a4a4a;"
            "   border: 1px solid " + StyleSheet::colorAccent() + ";"
            "}"
            "QPushButton:pressed {"
            "   background-color: #353535;"
            "}";
        
        QLineEdit* nameEdit = new QLineEdit(it.value().groupName);
        nameEdit->setStyleSheet(cellStyle);
        m_table->setCellWidget(row, 0, nameEdit);
        
        QPushButton* charactersButton = new QPushButton();
        charactersButton->setStyleSheet(buttonStyle);
        charactersButton->setCursor(Qt::PointingHandCursor);
        
        QStringList charList;
        for (const QString& charName : it.value().characterNames) {
            charList.append(charName);
        }
        charactersButton->setProperty("characterList", charList);
        
        if (charList.isEmpty()) {
            charactersButton->setText("(No characters)");
        } else if (charList.count() == 1) {
            charactersButton->setText(charList.first());
        } else {
            charactersButton->setText(QString("%1 characters").arg(charList.count()));
        }
        
        m_table->setCellWidget(row, 1, charactersButton);
        
        m_buttonConnector(charactersButton);
        
        QWidget *forwardHotkeyWidget = new QWidget();
        QHBoxLayout *forwardLayout = new QHBoxLayout(forwardHotkeyWidget);
        forwardLayout->setContentsMargins(0, 0, 0, 0);
        forwardLayout->setSpacing(4);
        
        HotkeyCapture *forwardCapture = new HotkeyCapture();
        
        if (!it.value().forwardBindings.isEmpty()) {
            QVector<HotkeyCombination> combinations;
            for (const HotkeyBinding& binding : it.value().forwardBindings) {
                combinations.append(HotkeyCombination(binding.keyCode, binding.ctrl, binding.alt, binding.shift));
            }
            forwardCapture->setHotkeys(combinations);
        } else {
            forwardCapture->setHotkey(it.value().forwardBinding.keyCode,
                                     it.value().forwardBinding.ctrl,
                                     it.value().forwardBinding.alt,
                                     it.value().forwardBinding.shift);
        }
        
        QPushButton *clearForwardButton = new QPushButton("×");
        clearForwardButton->setFixedSize(24, 24);
        clearForwardButton->setStyleSheet(
            "QPushButton {"
            "    background-color: #3a3a3a;"
            "    color: #a0a0a0;"
            "    border: 1px solid #555555;"
            "    border-radius: 3px;"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    padding: 0px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #4a4a4a;"
            "    color: #ffffff;"
            "    border: 1px solid #666666;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #2a2a2a;"
            "}"
        );
        clearForwardButton->setToolTip("Clear hotkey");
        QObject::connect(clearForwardButton, &QPushButton::clicked, [forwardCapture]() {
            forwardCapture->clearHotkey();
        });
        
        forwardLayout->addWidget(forwardCapture, 1);
        forwardLayout->addWidget(clearForwardButton, 0);
        m_table->setCellWidget(row, 2, forwardHotkeyWidget);
        
        QWidget *backwardHotkeyWidget = new QWidget();
        QHBoxLayout *backwardLayout = new QHBoxLayout(backwardHotkeyWidget);
        backwardLayout->setContentsMargins(0, 0, 0, 0);
        backwardLayout->setSpacing(4);
        
        HotkeyCapture *backwardCapture = new HotkeyCapture();
        
        if (!it.value().backwardBindings.isEmpty()) {
            QVector<HotkeyCombination> combinations;
            for (const HotkeyBinding& binding : it.value().backwardBindings) {
                combinations.append(HotkeyCombination(binding.keyCode, binding.ctrl, binding.alt, binding.shift));
            }
            backwardCapture->setHotkeys(combinations);
        } else {
            backwardCapture->setHotkey(it.value().backwardBinding.keyCode,
                                      it.value().backwardBinding.ctrl,
                                      it.value().backwardBinding.alt,
                                      it.value().backwardBinding.shift);
        }
        
        QPushButton *clearBackwardButton = new QPushButton("×");
        clearBackwardButton->setFixedSize(24, 24);
        clearBackwardButton->setStyleSheet(
            "QPushButton {"
            "    background-color: #3a3a3a;"
            "    color: #a0a0a0;"
            "    border: 1px solid #555555;"
            "    border-radius: 3px;"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    padding: 0px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #4a4a4a;"
            "    color: #ffffff;"
            "    border: 1px solid #666666;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #2a2a2a;"
            "}"
        );
        clearBackwardButton->setToolTip("Clear hotkey");
        QObject::connect(clearBackwardButton, &QPushButton::clicked, [backwardCapture]() {
            backwardCapture->clearHotkey();
        });
        
        backwardLayout->addWidget(backwardCapture, 1);
        backwardLayout->addWidget(clearBackwardButton, 0);
        m_table->setCellWidget(row, 3, backwardHotkeyWidget);
        
        QWidget* checkboxContainer = new QWidget();
        checkboxContainer->setStyleSheet("QWidget { background-color: transparent; }");
        QHBoxLayout* checkboxLayout = new QHBoxLayout(checkboxContainer);
        checkboxLayout->setContentsMargins(0, 0, 0, 0);
        checkboxLayout->setAlignment(Qt::AlignCenter);
        
        QCheckBox* includeNotLoggedInCheck = new QCheckBox();
        includeNotLoggedInCheck->setChecked(it.value().includeNotLoggedIn);
        includeNotLoggedInCheck->setToolTip("Include not-logged-in EVE clients in this cycle group");
        
        QString checkboxStyle = QString(
            "QCheckBox {"
            "   spacing: 5px;"
            "   outline: none;"
            "}"
            "QCheckBox::indicator {"
            "   width: 18px;"
            "   height: 18px;"
            "   border: 2px solid %1;"
            "   border-radius: 4px;"
            "   background-color: #303030;"
            "}"
            "QCheckBox::indicator:hover {"
            "   border: 2px solid %2;"
            "}"
            "QCheckBox::indicator:focus {"
            "   border: 2px solid %2;"
            "}"
            "QCheckBox::indicator:checked {"
            "   background-color: %2;"
            "   border: 2px solid %2;"
            "   image: url(data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iMTIiIGhlaWdodD0iMTAiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyI+PHBhdGggZD0iTTEgNUw0IDhMMTEgMSIgc3Ryb2tlPSIjZmZmZmZmIiBzdHJva2Utd2lkdGg9IjIiIGZpbGw9Im5vbmUiLz48L3N2Zz4=);"
            "}"
            "QCheckBox::indicator:checked:hover {"
            "   background-color: %2;"
            "   border: 2px solid %2;"
            "}"
            "QCheckBox::indicator:checked:focus {"
            "   background-color: %2;"
            "   border: 2px solid %2;"
            "}"
        ).arg(StyleSheet::colorBorder())
         .arg(StyleSheet::colorAccent());
        
        includeNotLoggedInCheck->setStyleSheet(checkboxStyle);
        
        checkboxLayout->addWidget(includeNotLoggedInCheck);
        m_table->setCellWidget(row, 4, checkboxContainer);
        
        QWidget* noLoopContainer = new QWidget();
        noLoopContainer->setStyleSheet("QWidget { background-color: transparent; }");
        QHBoxLayout* noLoopLayout = new QHBoxLayout(noLoopContainer);
        noLoopLayout->setContentsMargins(0, 0, 0, 0);
        noLoopLayout->setAlignment(Qt::AlignCenter);
        
        QCheckBox* noLoopCheck = new QCheckBox();
        noLoopCheck->setChecked(it.value().noLoop);
        noLoopCheck->setToolTip("Don't loop when reaching the end of the list");
        noLoopCheck->setStyleSheet(checkboxStyle);
        
        noLoopLayout->addWidget(noLoopCheck);
        m_table->setCellWidget(row, 5, noLoopContainer);
        
        QWidget *deleteContainer = new QWidget();
        deleteContainer->setStyleSheet("QWidget { background-color: transparent; }");
        QHBoxLayout *deleteLayout = new QHBoxLayout(deleteContainer);
        deleteLayout->setContentsMargins(0, 0, 0, 0);
        deleteLayout->setAlignment(Qt::AlignCenter);
        
        QPushButton *deleteButton = new QPushButton("×");
        deleteButton->setFixedSize(24, 24);
        deleteButton->setStyleSheet(
            "QPushButton {"
            "    background-color: #3a3a3a;"
            "    color: #e74c3c;"
            "    border: 1px solid #555555;"
            "    border-radius: 3px;"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    padding: 0px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #e74c3c;"
            "    color: #ffffff;"
            "    border: 1px solid #e74c3c;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #c0392b;"
            "}"
        );
        deleteButton->setToolTip("Delete this cycle group");
        deleteButton->setCursor(Qt::PointingHandCursor);
        
        QObject::connect(deleteButton, &QPushButton::clicked, [this, deleteButton]() {
            for (int i = 0; i < m_table->rowCount(); ++i) {
                QWidget *widget = m_table->cellWidget(i, 6);
                if (widget && widget->findChild<QPushButton*>() == deleteButton) {
                    m_table->removeRow(i);
                    break;
                }
            }
        });
        
        deleteLayout->addWidget(deleteButton);
        m_table->setCellWidget(row, 6, deleteContainer);
    }
}

void CycleGroupTableBinding::saveToConfig()
{
    QHash<QString, CycleGroup> groups;
    
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QLineEdit* nameEdit = qobject_cast<QLineEdit*>(m_table->cellWidget(row, 0));
        QPushButton* charactersButton = qobject_cast<QPushButton*>(m_table->cellWidget(row, 1));
        QWidget* forwardHotkeyWidget = m_table->cellWidget(row, 2);
        QWidget* backwardHotkeyWidget = m_table->cellWidget(row, 3);
        QWidget* checkboxContainer = qobject_cast<QWidget*>(m_table->cellWidget(row, 4));
        QWidget* noLoopContainer = qobject_cast<QWidget*>(m_table->cellWidget(row, 5));
        
        HotkeyCapture* forwardCapture = nullptr;
        HotkeyCapture* backwardCapture = nullptr;
        if (forwardHotkeyWidget) {
            forwardCapture = forwardHotkeyWidget->findChild<HotkeyCapture*>();
        }
        if (backwardHotkeyWidget) {
            backwardCapture = backwardHotkeyWidget->findChild<HotkeyCapture*>();
        }
        
        if (nameEdit && charactersButton && forwardCapture && backwardCapture && checkboxContainer) {
            QString groupName = nameEdit->text().trimmed();
            QStringList charactersList = charactersButton->property("characterList").toStringList();
            
            QCheckBox* includeNotLoggedInCheck = checkboxContainer->findChild<QCheckBox*>();
            bool includeNotLoggedIn = includeNotLoggedInCheck ? includeNotLoggedInCheck->isChecked() : false;
            
            QCheckBox* noLoopCheck = noLoopContainer ? noLoopContainer->findChild<QCheckBox*>() : nullptr;
            bool noLoop = noLoopCheck ? noLoopCheck->isChecked() : false;
            
            if (!groupName.isEmpty() && !charactersList.isEmpty()) {
                QVector<QString> characters;
                for (const QString& charName : charactersList) {
                    characters.append(charName.trimmed());
                }
                
                if (!characters.isEmpty()) {
                    QVector<HotkeyCombination> forwardCombinations = forwardCapture->getHotkeys();
                    QVector<HotkeyBinding> forwardBindings;
                    for (const HotkeyCombination& comb : forwardCombinations) {
                        forwardBindings.append(HotkeyBinding(comb.keyCode, comb.ctrl, comb.alt, comb.shift, comb.keyCode != 0));
                    }
                    
                    QVector<HotkeyCombination> backwardCombinations = backwardCapture->getHotkeys();
                    QVector<HotkeyBinding> backwardBindings;
                    for (const HotkeyCombination& comb : backwardCombinations) {
                        backwardBindings.append(HotkeyBinding(comb.keyCode, comb.ctrl, comb.alt, comb.shift, comb.keyCode != 0));
                    }
                    
                    HotkeyBinding forwardBinding = !forwardBindings.isEmpty() 
                        ? forwardBindings.first() 
                        : HotkeyBinding(0, false, false, false, false);
                    
                    HotkeyBinding backwardBinding = !backwardBindings.isEmpty()
                        ? backwardBindings.first()
                        : HotkeyBinding(0, false, false, false, false);
                    
                    CycleGroup group;
                    group.groupName = groupName;
                    group.characterNames = characters;
                    group.forwardBinding = forwardBinding;
                    group.backwardBinding = backwardBinding;
                    group.forwardBindings = forwardBindings;
                    group.backwardBindings = backwardBindings;
                    group.includeNotLoggedIn = includeNotLoggedIn;
                    group.noLoop = noLoop;
                    
                    groups[groupName] = group;
                }
            }
        }
    }
    
    m_configSetter(groups);
}

void CycleGroupTableBinding::reset()
{
    for (int row = m_table->rowCount() - 1; row >= 0; --row) {
        for (int col = 0; col < m_table->columnCount(); ++col) {
            QWidget* widget = m_table->cellWidget(row, col);
            if (widget) {
                m_table->removeCellWidget(row, col);
                widget->deleteLater();
            }
        }
    }
    m_table->setRowCount(0);
}

bool CycleGroupTableBinding::hasChanged() const
{
    QHash<QString, CycleGroup> current;
    
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QLineEdit* nameEdit = qobject_cast<QLineEdit*>(m_table->cellWidget(row, 0));
        QPushButton* charactersButton = qobject_cast<QPushButton*>(m_table->cellWidget(row, 1));
        QWidget* forwardHotkeyWidget = m_table->cellWidget(row, 2);
        QWidget* backwardHotkeyWidget = m_table->cellWidget(row, 3);
        
        HotkeyCapture* forwardCapture = nullptr;
        HotkeyCapture* backwardCapture = nullptr;
        if (forwardHotkeyWidget) {
            forwardCapture = forwardHotkeyWidget->findChild<HotkeyCapture*>();
        }
        if (backwardHotkeyWidget) {
            backwardCapture = backwardHotkeyWidget->findChild<HotkeyCapture*>();
        }
        
        if (nameEdit && charactersButton && forwardCapture && backwardCapture) {
            QString groupName = nameEdit->text().trimmed();
            QStringList charactersList = charactersButton->property("characterList").toStringList();
            
            if (!groupName.isEmpty() && !charactersList.isEmpty()) {
                QVector<QString> characters;
                for (const QString& charName : charactersList) {
                    characters.append(charName.trimmed());
                }
                
                if (!characters.isEmpty()) {
                    QVector<HotkeyCombination> forwardCombinations = forwardCapture->getHotkeys();
                    QVector<HotkeyBinding> forwardBindings;
                    for (const HotkeyCombination& comb : forwardCombinations) {
                        forwardBindings.append(HotkeyBinding(comb.keyCode, comb.ctrl, comb.alt, comb.shift, comb.keyCode != 0));
                    }
                    
                    QVector<HotkeyCombination> backwardCombinations = backwardCapture->getHotkeys();
                    QVector<HotkeyBinding> backwardBindings;
                    for (const HotkeyCombination& comb : backwardCombinations) {
                        backwardBindings.append(HotkeyBinding(comb.keyCode, comb.ctrl, comb.alt, comb.shift, comb.keyCode != 0));
                    }
                    
                    HotkeyBinding forwardBinding = !forwardBindings.isEmpty() 
                        ? forwardBindings.first() 
                        : HotkeyBinding(0, false, false, false, false);
                    
                    HotkeyBinding backwardBinding = !backwardBindings.isEmpty()
                        ? backwardBindings.first()
                        : HotkeyBinding(0, false, false, false, false);
                    
                    CycleGroup group;
                    group.groupName = groupName;
                    group.characterNames = characters;
                    group.forwardBinding = forwardBinding;
                    group.backwardBinding = backwardBinding;
                    group.forwardBindings = forwardBindings;
                    group.backwardBindings = backwardBindings;
                    
                    current[groupName] = group;
                }
            }
        }
    }
    
    if (current.size() != m_initialValue.size()) {
        return true;
    }
    
    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        if (!m_initialValue.contains(it.key())) {
            return true;
        }
        const CycleGroup& currentGroup = it.value();
        const CycleGroup& initialGroup = m_initialValue[it.key()];
        
        if (currentGroup.groupName != initialGroup.groupName ||
            currentGroup.characterNames != initialGroup.characterNames ||
            currentGroup.includeNotLoggedIn != initialGroup.includeNotLoggedIn ||
            currentGroup.noLoop != initialGroup.noLoop) {
            return true;
        }
        
        if (currentGroup.forwardBindings.size() != initialGroup.forwardBindings.size()) {
            return true;
        }
        for (int i = 0; i < currentGroup.forwardBindings.size(); ++i) {
            if (!(currentGroup.forwardBindings[i] == initialGroup.forwardBindings[i])) {
                return true;
            }
        }
        
        if (currentGroup.backwardBindings.size() != initialGroup.backwardBindings.size()) {
            return true;
        }
        for (int i = 0; i < currentGroup.backwardBindings.size(); ++i) {
            if (!(currentGroup.backwardBindings[i] == initialGroup.backwardBindings[i])) {
                return true;
            }
        }
    }
    
    return false;
}

QWidget* CycleGroupTableBinding::widget() const
{
    return m_table;
}


HotkeyCaptureBinding::HotkeyCaptureBinding(HotkeyCapture* widget,
                                           Getter configGetter,
                                           Setter configSetter,
                                           HotkeyBinding defaultValue)
    : m_widget(widget)
    , m_configGetter(configGetter)
    , m_configSetter(configSetter)
    , m_defaultValue(defaultValue)
{
}

void HotkeyCaptureBinding::loadFromConfig()
{
    HotkeyBinding current = m_configGetter();
    m_initialValue = current;
    m_widget->setHotkey(current.keyCode, current.ctrl, current.alt, current.shift);
}

void HotkeyCaptureBinding::saveToConfig()
{
    QVector<HotkeyCombination> combinations = m_widget->getHotkeys();
    
    if (!combinations.isEmpty()) {
        const HotkeyCombination& first = combinations.first();
        HotkeyBinding current(first.keyCode, first.ctrl, first.alt, first.shift, first.keyCode != 0);
        m_configSetter(current);
    } else {
        HotkeyBinding current(0, false, false, false, false);
        m_configSetter(current);
    }
}

void HotkeyCaptureBinding::reset()
{
    m_widget->setHotkey(m_defaultValue.keyCode, m_defaultValue.ctrl, 
                       m_defaultValue.alt, m_defaultValue.shift);
}

bool HotkeyCaptureBinding::hasChanged() const
{
    QVector<HotkeyCombination> combinations = m_widget->getHotkeys();
    
    if (combinations.isEmpty()) {
        return m_initialValue.keyCode != 0 || m_initialValue.enabled;
    }
    
    const HotkeyCombination& first = combinations.first();
    HotkeyBinding current(first.keyCode, first.ctrl, first.alt, first.shift, first.keyCode != 0);
    
    return current != m_initialValue;
}

QWidget* HotkeyCaptureBinding::widget() const
{
    return m_widget;
}

CharacterColorTableBinding::CharacterColorTableBinding(QTableWidget* table,
                                                       ColorUpdateFunc colorUpdateFunc,
                                                       ButtonConnector buttonConnector)
    : m_table(table)
    , m_colorUpdateFunc(colorUpdateFunc)
    , m_buttonConnector(buttonConnector)
{
}

void CharacterColorTableBinding::loadFromConfig()
{
    Config& config = Config::instance();
    
    for (int row = m_table->rowCount() - 1; row >= 0; --row) {
        for (int col = 0; col < m_table->columnCount(); ++col) {
            QWidget* widget = m_table->cellWidget(row, col);
            if (widget) {
                m_table->removeCellWidget(row, col);
                widget->deleteLater();
            }
        }
    }
    m_table->setRowCount(0);
    
    QHash<QString, QColor> characterColors = config.getAllCharacterBorderColors();
    m_initialValue = characterColors;
    
    for (auto it = characterColors.constBegin(); it != characterColors.constEnd(); ++it) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        
        QLineEdit* nameEdit = new QLineEdit(it.key());
        nameEdit->setStyleSheet(
            "QLineEdit {"
            "   background-color: transparent;"
            "   color: #ffffff;"
            "   border: none;"
            "   padding: 2px 4px;"
            "}"
            "QLineEdit:focus {"
            "   background-color: #353535;"
            "}"
        );
        m_table->setCellWidget(row, 0, nameEdit);
        
        QPushButton* colorButton = new QPushButton();
        colorButton->setFixedSize(150, 28);
        colorButton->setCursor(Qt::PointingHandCursor);
        colorButton->setProperty("color", it.value());
        m_colorUpdateFunc(colorButton, it.value());
        
        m_buttonConnector(colorButton);
        
        QWidget* buttonContainer = new QWidget();
        QHBoxLayout* buttonLayout = new QHBoxLayout(buttonContainer);
        buttonLayout->setContentsMargins(3, 3, 3, 3);
        buttonLayout->addWidget(colorButton);
        buttonLayout->setAlignment(Qt::AlignCenter);
        
        m_table->setCellWidget(row, 1, buttonContainer);
        
        QWidget* deleteButtonContainer = new QWidget();
        QHBoxLayout* deleteButtonLayout = new QHBoxLayout(deleteButtonContainer);
        deleteButtonLayout->setContentsMargins(0, 0, 0, 0);
        
        QPushButton* deleteButton = new QPushButton("×");
        deleteButton->setFixedSize(24, 24);
        deleteButton->setStyleSheet(
            "QPushButton {"
            "    background-color: #3a3a3a;"
            "    color: #ffffff;"
            "    border: 1px solid #555555;"
            "    border-radius: 4px;"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    padding: 0px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #e74c3c;"
            "    border: 1px solid #c0392b;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #c0392b;"
            "}"
        );
        
        QObject::connect(deleteButton, &QPushButton::clicked, [this, row]() {
            m_table->removeRow(row);
        });
        
        deleteButtonLayout->addWidget(deleteButton, 0, Qt::AlignCenter);
        m_table->setCellWidget(row, 2, deleteButtonContainer);
    }
}

void CharacterColorTableBinding::saveToConfig()
{
    Config& config = Config::instance();
    
    QHash<QString, QColor> allColors = config.getAllCharacterBorderColors();
    for (auto it = allColors.constBegin(); it != allColors.constEnd(); ++it) {
        config.removeCharacterBorderColor(it.key());
    }
    
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QLineEdit* nameEdit = qobject_cast<QLineEdit*>(m_table->cellWidget(row, 0));
        if (!nameEdit) continue;
        
        QString charName = nameEdit->text().trimmed();
        if (charName.isEmpty()) continue;
        
        QWidget* container = m_table->cellWidget(row, 1);
        if (container) {
            QPushButton* colorButton = container->findChild<QPushButton*>();
            if (colorButton) {
                QColor color = colorButton->property("color").value<QColor>();
                if (color.isValid()) {
                    config.setCharacterBorderColor(charName, color);
                }
            }
        }
    }
    
    config.save();
}

void CharacterColorTableBinding::reset()
{
    Config& config = Config::instance();
    for (auto it = m_initialValue.constBegin(); it != m_initialValue.constEnd(); ++it) {
        config.removeCharacterBorderColor(it.key());
    }
    
    for (int row = m_table->rowCount() - 1; row >= 0; --row) {
        for (int col = 0; col < m_table->columnCount(); ++col) {
            QWidget* widget = m_table->cellWidget(row, col);
            if (widget) {
                m_table->removeCellWidget(row, col);
                widget->deleteLater();
            }
        }
    }
    m_table->setRowCount(0);
    m_initialValue.clear();
}

bool CharacterColorTableBinding::hasChanged() const
{
    QHash<QString, QColor> currentValue;
    
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QLineEdit* nameEdit = qobject_cast<QLineEdit*>(m_table->cellWidget(row, 0));
        if (!nameEdit) continue;
        
        QString charName = nameEdit->text().trimmed();
        if (charName.isEmpty()) continue;
        
        QWidget* container = m_table->cellWidget(row, 1);
        if (container) {
            QPushButton* colorButton = container->findChild<QPushButton*>();
            if (colorButton) {
                QColor color = colorButton->property("color").value<QColor>();
                if (color.isValid()) {
                    currentValue[charName] = color;
                }
            }
        }
    }
    
    if (currentValue.size() != m_initialValue.size()) {
        return true;
    }
    
    for (auto it = currentValue.constBegin(); it != currentValue.constEnd(); ++it) {
        if (!m_initialValue.contains(it.key()) || m_initialValue[it.key()] != it.value()) {
            return true;
        }
    }
    
    return false;
}

QWidget* CharacterColorTableBinding::widget() const
{
    return m_table;
}


FontBinding::FontBinding(QComboBox* fontCombo,
                        QSpinBox* sizeSpinBox,
                        Getter configGetter,
                        Setter configSetter,
                        QFont defaultValue)
    : m_fontCombo(fontCombo)
    , m_sizeSpinBox(sizeSpinBox)
    , m_configGetter(configGetter)
    , m_configSetter(configSetter)
    , m_defaultValue(defaultValue)
{
}

void FontBinding::loadFromConfig()
{
    QFont font = m_configGetter();
    m_initialValue = font;
    
    int index = m_fontCombo->findText(font.family(), Qt::MatchFixedString);
    if (index >= 0) {
        m_fontCombo->setCurrentIndex(index);
    }
    
    m_sizeSpinBox->setValue(font.pointSize());
}

void FontBinding::saveToConfig()
{
    QFont font;
    font.setFamily(m_fontCombo->currentText());
    font.setPointSize(m_sizeSpinBox->value());
    m_configSetter(font);
}

void FontBinding::reset()
{
    int index = m_fontCombo->findText(m_defaultValue.family(), Qt::MatchFixedString);
    if (index >= 0) {
        m_fontCombo->setCurrentIndex(index);
    }
    m_sizeSpinBox->setValue(m_defaultValue.pointSize());
}

bool FontBinding::hasChanged() const
{
    QString currentFamily = m_fontCombo->currentText();
    int currentSize = m_sizeSpinBox->value();
    
    return currentFamily != m_initialValue.family() || 
           currentSize != m_initialValue.pointSize();
}

QWidget* FontBinding::widget() const
{
    return m_fontCombo;
}


void SettingBindingManager::addBinding(std::unique_ptr<SettingBindingBase> binding)
{
    m_bindings.push_back(std::move(binding));
}

void SettingBindingManager::loadAll()
{
    for (auto& binding : m_bindings) {
        binding->loadFromConfig();
    }
}

void SettingBindingManager::saveAll()
{
    for (auto& binding : m_bindings) {
        binding->saveToConfig();
    }
}

void SettingBindingManager::resetAll()
{
    for (auto& binding : m_bindings) {
        binding->reset();
    }
}

bool SettingBindingManager::hasAnyChanges() const
{
    for (const auto& binding : m_bindings) {
        if (binding->hasChanged()) {
            return true;
        }
    }
    return false;
}

SettingBindingBase* SettingBindingManager::findBinding(QWidget* widget) const
{
    for (const auto& binding : m_bindings) {
        if (binding->widget() == widget) {
            return binding.get();
        }
    }
    return nullptr;
}

void SettingBindingManager::clear()
{
    m_bindings.clear();
}
