#pragma once
#include <QByteArray>
#include <QString>
namespace forge::core {
class PackageArchive final {
public:
    static bool extract(const QByteArray& bytes, const QString& destination, QString* error);
};
}  // namespace forge::core
