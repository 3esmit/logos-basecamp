#pragma once

#include <QMetaType>
#include <QString>
#include <QVariant>

namespace DependencyMetadata {

inline QString name(const QVariant& dependency)
{
    if (dependency.metaType().id() == QMetaType::QString)
        return dependency.toString();
    return dependency.toMap().value(QStringLiteral("name")).toString();
}

} // namespace DependencyMetadata
