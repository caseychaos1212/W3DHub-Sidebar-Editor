#include "EditPurchaseItemDialog.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLabel>

EditPurchaseItemDialog::EditPurchaseItemDialog(const PurchaseItem& item, QWidget* parent)
    : QDialog(parent), m_working(item)
{

    setWindowTitle(QString("Edit Purchase: %1").arg(item.presetId));
    setModal(true);

    // --- widgets ---
    sbCost = new QSpinBox(this);
    sbCost->setRange(0, 1000000);
    sbCost->setValue(item.cost);

    sbTechLevel = new QSpinBox(this);
    sbTechLevel->setRange(0, 10);
    sbTechLevel->setValue(item.techLevel);

    sbSpecialTech = new QSpinBox(this);
    sbSpecialTech->setRange(0, 999);
    sbSpecialTech->setValue(item.specialTechNumber);

    sbUnitLimit = new QSpinBox(this);
    sbUnitLimit->setRange(0, 99);
    sbUnitLimit->setValue(item.unitLimit);

    cbFactory = new QComboBox(this);
    cbTechBuilding = new QComboBox(this);

    chkFactoryNotRequired = new QCheckBox("Factory Not Required", this);
    chkFactoryNotRequired->setChecked(item.factoryNotRequired);

    // --- populate combos with (label, code) pairs ---
    populateCombos();
    setComboToCode(cbFactory, item.factory, m_factoryCodes);      
    setComboToCode(cbTechBuilding, item.techBuilding, m_techBuildingCodes);

    // --- layout ---
    auto* form = new QFormLayout;
    form->addRow("COST:", sbCost);
    form->addRow("TECH_LEVEL:", sbTechLevel);
    form->addRow("SPECIAL_TECH_NUMBER:", sbSpecialTech);
    form->addRow("UNIT_LIMIT:", sbUnitLimit);
    form->addRow("FACTORY:", cbFactory);
    form->addRow("TECH_BUILDING:", cbTechBuilding);
    form->addRow("", chkFactoryNotRequired);

    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &EditPurchaseItemDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &EditPurchaseItemDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);
    setLayout(root);
}

void EditPurchaseItemDialog::populateCombos() {
    // Order & codes per your mapping
    m_factoryCodes = {
        {"POWER_PLANT", 0},
        {"SOLDIER_FACTORY", 1},
        {"VEHICLE_FACTORY", 2},
        {"REFINERY", 3},
        {"COM_CENTER", 4},
        {"REPAIR_BAY", 5},
        {"SHRINE", 6},
        {"HELIPAD", 7},
        {"CONYARD", 8},
        {"BASE_DEFENSE", 9},
        {"TECH_CENTER", 10},
        {"NAVAL_FACTORY", 11},
        {"SPECIAL", 12},
        {"SUPPLEMENTAL_AIR", 13},
    };
    m_techBuildingCodes = m_factoryCodes;          // same code table per your note
    m_techBuildingCodes.prepend({ "<None>", -1 });   // allow -1

    cbFactory->clear();
    for (const auto& p : m_factoryCodes) cbFactory->addItem(p.first, p.second);

    cbTechBuilding->clear();
    for (const auto& p : m_techBuildingCodes) cbTechBuilding->addItem(p.first, p.second);
}

void EditPurchaseItemDialog::setComboToCode(QComboBox* cb, int code,
    const QVector<QPair<QString, int>>& src) {
    int idx = -1;
    for (int i = 0; i < src.size(); ++i) if (src[i].second == code) { idx = i; break; }
    if (idx < 0) idx = 0;
    cb->setCurrentIndex(idx);
}

int EditPurchaseItemDialog::codeFromCombo(const QComboBox* cb,
    const QVector<QPair<QString, int>>& src) const {
    int idx = cb->currentIndex();
    if (idx < 0 || idx >= src.size()) return src.isEmpty() ? -1 : src.front().second;
    return src[idx].second;
}

void EditPurchaseItemDialog::onAccept() {
    m_working.cost = sbCost->value();
    m_working.techLevel = sbTechLevel->value();
    
    m_working.specialTechNumber = sbSpecialTech->value();
    m_working.unitLimit = sbUnitLimit->value();
    m_working.factory = codeFromCombo(cbFactory, m_factoryCodes);
    m_working.techBuilding = codeFromCombo(cbTechBuilding, m_techBuildingCodes);
    m_working.factoryNotRequired = chkFactoryNotRequired->isChecked();

    accept();
}

