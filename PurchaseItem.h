// PurchaseItem.h
#pragma once
#include <QString>
#include <QVector>

struct PurchaseItem {
    int cost = 0;
    int presetId = 0;
    int stringId = 0;
    QString texture;
    int techLevel = 0;
    int team = 0;
    int type = 0;

    // NEW:
    int specialTechNumber = 0;
    int unitLimit = 0;
    int factory = -1;
    int techBuilding = -1;
    bool factoryNotRequired = false;

    QVector<int> altPresetIds;
    QVector<QString> altTextures;
};
