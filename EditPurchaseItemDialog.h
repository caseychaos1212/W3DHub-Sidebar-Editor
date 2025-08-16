#pragma once
#include <QDialog>
#include <QVector>
#include <QPair>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include "PurchaseItem.h"

class EditPurchaseItemDialog : public QDialog {
    Q_OBJECT
public:
    explicit EditPurchaseItemDialog(const PurchaseItem& item, QWidget* parent = nullptr);
    PurchaseItem result() const { return m_working; }

private slots:
    void onAccept();

private:
    // Widgets
    QSpinBox* sbCost = nullptr;
    QSpinBox* sbTechLevel = nullptr;
    QSpinBox* sbSpecialTech = nullptr;
    QSpinBox* sbUnitLimit = nullptr;
    QComboBox* cbFactory = nullptr;
    QComboBox* cbTechBuilding = nullptr;
    QCheckBox* chkFactoryNotRequired = nullptr;
    QDialogButtonBox* buttons = nullptr;

    // Model copy we edit
    PurchaseItem   m_working;

    // value lists (display text, code)
    QVector<QPair<QString, int>> m_factoryCodes;
    QVector<QPair<QString, int>> m_techBuildingCodes;

    void populateCombos();
    void setComboToCode(QComboBox* cb, int code, const QVector<QPair<QString, int>>& src);
    int  codeFromCombo(const QComboBox* cb, const QVector<QPair<QString, int>>& src) const;
};
