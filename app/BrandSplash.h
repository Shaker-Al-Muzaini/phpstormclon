#pragma once
#include <QSplashScreen>
class BrandSplash final : public QSplashScreen {
public:
    BrandSplash();
    static QPixmap artwork();

protected:
    void drawContents(QPainter* painter) override;
};
