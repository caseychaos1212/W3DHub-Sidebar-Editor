// IconTileWidget.cpp
#include "IconTileWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QMouseEvent>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QPixmap>
#include <QDebug>
#include <QFileInfo>
#include <QPushButton>
#include <QIcon>
#include <QSize>
#include <QCoreApplication> 

namespace {
    constexpr int kIconSize = 160;   // bump tile icon size
}

// ctor
IconTileWidget::IconTileWidget(const PurchaseItem& item, const QString& iconDir, QWidget* parent)
    : QPushButton(parent), baseItem(item), iconDir(iconDir)
{
    setFlat(true);
    setCheckable(false);
    setStyleSheet("QPushButton { padding: 0; border: 0; }");

    // give the icon room + a bit of space for the yellow corner
    setIconSize(QSize(kIconSize, kIconSize));
    setFixedSize(kIconSize + 14, kIconSize + 14);


    updateIcon();
}

QPixmap IconTileWidget::loadTexture(const QString& textureName) {
    if (textureName.trimmed().isEmpty()) return QPixmap();

    const QString ddsPath = iconDir + "/" + textureName;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cacheDir = appDir + "/cache_icons";
    QDir().mkpath(cacheDir);

    const QString baseName = QFileInfo(textureName).completeBaseName();
    const QString pngPath = cacheDir + "/" + baseName + ".png";

    // Try existing cached png first
    QPixmap pix(pngPath);
    if (!pix.isNull()) return pix;

    // Convert .dds -> .png at a larger size
    if (QFile::exists(ddsPath)) {
        QString exe = "texconv.exe";
        // -w/-h set output size; adjust if you want even bigger
        QStringList args = { "-y", "-ft", "png", "-w", "256", "-h", "256", "-o", cacheDir, ddsPath };
        QProcess::execute(exe, args);

        QPixmap converted(pngPath);
        if (!converted.isNull()) return converted;
    }

    qWarning() << "Failed to load or convert texture:" << textureName;
    return QPixmap(kIconSize, kIconSize); // blank fallback at the right size
}

void IconTileWidget::updateIcon() {
    QString tex = baseItem.texture;
    if (currentAltIndex >= 0 && currentAltIndex < baseItem.altTextures.size()) {
        const QString alt = baseItem.altTextures[currentAltIndex];
        if (!alt.trimmed().isEmpty()) tex = alt;
    }
    const QPixmap pm = loadTexture(tex);
    setIcon(QIcon(pm));
    setIconSize(QSize(192, 192));   // <<< was 88x88; adjust to taste
    update();
}


void IconTileWidget::mousePressEvent(QMouseEvent* event) {
    if (!baseItem.altTextures.isEmpty() && event->button() == Qt::LeftButton) {
        currentAltIndex = (currentAltIndex + 1) % (baseItem.altTextures.size() + 1);
        if (currentAltIndex == baseItem.altTextures.size()) currentAltIndex = -1; // back to base
        updateIcon();
    }
    QPushButton::mousePressEvent(event);
}

void IconTileWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        emit editRequested(baseItem, this);
    QPushButton::mouseDoubleClickEvent(e);
}


void IconTileWidget::paintEvent(QPaintEvent* event) {
    QPushButton::paintEvent(event);

    // Only draw the yellow corner if there’s at least one real alt texture
    bool hasAlt = std::any_of(baseItem.altTextures.begin(),
        baseItem.altTextures.end(),
        [](const QString& s) { return !s.trimmed().isEmpty(); });

    if (hasAlt) {
        QPainter p(this);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::yellow);
        const int t = 16;
        const QPoint tri[3] = { QPoint(width() - t, 0), QPoint(width(), 0), QPoint(width(), t) };
        p.drawPolygon(tri, 3);
    }
    // COST label (bottom bar)
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QRect bar(6, height() - 24, width() - 12, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(bar, 4, 4);
        p.setPen(Qt::white);
        QFont f = font(); f.setBold(true);
        p.setFont(f);
        p.drawText(bar.adjusted(6, 0, -6, 0),
            Qt::AlignVCenter | Qt::AlignLeft,
            QStringLiteral("COST: %1").arg(baseItem.cost));
    }
}



