// MainWindow.h
#pragma once

#include <QMainWindow>
#include <QMap>
#include <QVector>
#include <QSet>
#include <QString>

#include "PurchaseItem.h"

class QTabWidget;
class QComboBox;
class QWidget;

struct PurchaseList {
    QString id;      // e.g. "TEAM=0|TYPE=1|NAME=Vehicles (Allied)"
    QString name;    // DEFINITION_BASE.NAME
    int team = 0;
    int type = 0;
    QVector<PurchaseItem> items;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);

private:
    QTabWidget* tabWidget= nullptr;

    // Lists chosen by user (built from master) -> used to build tabs
    QMap<QString, QVector<PurchaseItem>> categorizedLists;

    // Source data from master file
    QVector<PurchaseList> allLists;
    QSet<QString>        selectedListIds;

    // Map name -> dropdown widget (legacy)
    QMap<QString, QComboBox*> mapCamoMap;

    // Core logic
    void loadMasterJson(const QString& path);
    void rebuildFromSelection();             // <— NEW
    void buildTabs();
    QWidget* createGridPage(const QVector<PurchaseItem>& items);
    QString typeTeamToLabel(int type, int team);
    void applyCamoDefaults(const QString& mapTheme);
    QHash<QString, PurchaseItem> buildEditedMapFromTabs() const;
    void applyEditsToPurchaseList(PurchaseList& pl, const QHash<QString, PurchaseItem>& edits);
    void updateMasterFromTabs();
    void updateMasterFromTabsAllLists();
    void mergeIntoLevelDoc(QJsonDocument& levelDoc, const QMap<QString, QVector<PurchaseItem>>& tabs);
    void showLevelPickerAndRun(std::function<void(const QStringList&)> fn);
    void updateSelectedLevels();
    int computeNextUniqueDefId(const QJsonDocument* currentLevelDoc = nullptr) const;
    bool reorderCamoInLevelFile(const QString& level, const QString& theme);
    // File I/O helpers
    bool loadJsonFromFile(const QString& path, QJsonDocument& outDoc) const;
    bool writeJsonToFile(const QString& path, const QJsonDocument& doc) const;
    QJsonObject itemToJson(const PurchaseItem& item) const;

    // Export / Profile tools
    void exportMapJson();
    void exportAllMapJsons();
    void showMapTheaterWidget();
    void mergeEditsIntoMasterDoc(QJsonDocument& doc,
        const QHash<QString, PurchaseItem>& edits,
        const QSet<QString>& applyOnlyTheseListIds);


    // List picker
    void showListPickerDialog();             // < NEW
};

