#pragma once

#include <QPushButton>
#include <QPixmap>
#include <QString>
#include <QVector>
#include "PurchaseItem.h"



class IconTileWidget : public QPushButton {
    Q_OBJECT

signals:
    void editRequested(const PurchaseItem& current, IconTileWidget* self);

public:
    explicit IconTileWidget(const PurchaseItem& item, const QString& iconDir, QWidget* parent = nullptr);
    
public:
    void applyEdits(const PurchaseItem& updated) { baseItem = updated; updateIcon(); update(); }



protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
private:
    PurchaseItem baseItem;
    int currentAltIndex = -1;
    QString iconDir;

    QPixmap loadTexture(const QString& textureName);
    void updateIcon();
};
