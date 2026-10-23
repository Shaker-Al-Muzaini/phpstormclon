#include "BrandSplash.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
QPixmap BrandSplash::artwork() {
    // All brand colors are confined to this startup surface.
    QPixmap image(1280, 720);
    image.setDevicePixelRatio(2.0);
    image.fill(QColor("#051D16"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient background(0, 0, 640, 360);
    background.setColorAt(0, QColor("#051D16"));
    background.setColorAt(1, QColor("#0D3B2C"));
    painter.fillRect(QRect(0, 0, 640, 360), background);
    for (int i = 0; i < 5; ++i) {
        QPainterPath ribbon;
        ribbon.moveTo(240 + i * 35, 370);
        ribbon.cubicTo(200 + i * 38, 190, 465 + i * 22, 245, 650, 35 + i * 35);
        ribbon.lineTo(650, 390);
        ribbon.closeSubpath();
        QLinearGradient gradient(250, 170, 640, 360);
        gradient.setColorAt(0, QColor(110, 220, 160, 25 + i * 8));
        gradient.setColorAt(1, QColor(212, 160, 23, 50 + i * 10));
        painter.fillPath(ribbon, gradient);
    }
    painter.setPen(QPen(QColor("#D4A017"), 2));
    painter.setBrush(QColor("#07291F"));
    painter.drawRoundedRect(QRectF(40, 34, 46, 46), 12, 12);
    painter.setPen(QColor("#6EDCA0"));
    painter.setFont(QFont("Segoe UI", 13, QFont::Bold));
    painter.drawText(QRectF(40, 34, 46, 46), Qt::AlignCenter, "SM");
    painter.setPen(QColor("#A9BBB0"));
    painter.setFont(QFont("Segoe UI", 10));
    painter.drawText(101, 62, "SHAKER AL-MUZAYNI");
    painter.setPen(QColor("#F1F4EC"));
    painter.setFont(QFont("Segoe UI", 36, QFont::Bold));
    painter.drawText(40, 212, "Forge IDE");
    painter.setFont(QFont("Segoe UI", 12));
    painter.setPen(QColor("#A9BBB0"));
    painter.drawText(43, 244, "A native workspace for PHP & Laravel");
    painter.setPen(QColor("#D4A017"));
    painter.setFont(QFont("Segoe UI", 10));
    painter.drawText(43, 280,
                     QStringLiteral("%1  ·  Built by Shaker Al-Muzayni").arg(FORGE_VERSION));
    painter.setFont(QFont("Segoe UI", 11));
    painter.setPen(QColor("#F1F4EC"));
    painter.drawText(QRectF(400, 30, 200, 40), Qt::AlignRight | Qt::AlignVCenter,
                     QString::fromUtf8("شاكر المزيني"));
    return image;
}
BrandSplash::BrandSplash() : QSplashScreen(artwork()) {
    setObjectName("BrandSplash");
}
void BrandSplash::drawContents(QPainter* painter) {
    painter->setPen(QColor("#A9BBB0"));
    painter->setFont(QFont("Segoe UI", 9));
    painter->drawText(43, 327, "Starting workspace…");
}
