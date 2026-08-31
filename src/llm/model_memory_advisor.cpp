#include "llm/model_memory_advisor.hpp"

#include <QRegularExpression>

#include <cmath>

namespace {

// Approximate quantized (Q4_K_M) bytes per billion parameters, plus a fixed
// context/overhead buffer. These are conservative (safe) over-estimates so we
// err on the side of not running out of RAM.
constexpr double kBytesPerBillion = 0.72 * 1024.0 * 1024.0 * 1024.0;
constexpr double kContextBufferBytes = 1.6 * 1024.0 * 1024.0 * 1024.0;

// Safety margin kept free on top of the model's own footprint.
constexpr quint64 kHeadroomBytes = 1024ULL * 1024ULL * 1024ULL; // 1 GB

double parameterBillions(const QString &modelTag)
{
    // Common tags look like "family:14b", "family:14b-q4_K_M", "family:3b".
    // Extract the first "<number>b" run, case-insensitively.
    static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+)?)b"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(modelTag);
    if (!m.hasMatch()) {
        return 0.0;
    }
    bool ok = false;
    const double params = m.captured(1).toDouble(&ok);
    return ok && params > 0.0 ? params : 0.0;
}

} // namespace

namespace ModelMemoryAdvisor {

quint64 estimatedFootprintBytes(const QString &modelTag)
{
    const double params = parameterBillions(modelTag);
    if (params <= 0.0) {
        return 0;
    }
    const double total = params * kBytesPerBillion + kContextBufferBytes;
    if (total <= 0.0) {
        return 0;
    }
    return static_cast<quint64>(total + 0.5);
}

QString footprintDisplay(const QString &modelTag)
{
    const quint64 bytes = estimatedFootprintBytes(modelTag);
    if (bytes == 0) {
        return QStringLiteral("unknown size");
    }
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    return QStringLiteral("~%1 GB").arg(QString::number(gb, 'f', 1));
}

bool fitsInMemory(const QString &modelTag, quint64 availableBytes)
{
    const quint64 footprint = estimatedFootprintBytes(modelTag);
    if (footprint == 0) {
        // Unknown size: do not block it.
        return true;
    }
    return availableBytes >= footprint + kHeadroomBytes;
}

QString recommendBest(const QStringList &installed, quint64 availableBytes)
{
    QString best;
    quint64 bestFootprint = 0;
    for (const QString &model : installed) {
        if (!fitsInMemory(model, availableBytes)) {
            continue;
        }
        const quint64 footprint = estimatedFootprintBytes(model);
        if (footprint > bestFootprint) {
            bestFootprint = footprint;
            best = model;
        }
    }
    return best;
}

QString suggestDownscale(const QStringList &installed,
                         quint64 availableBytes,
                         const QString &currentModel)
{
    const quint64 currentFootprint = estimatedFootprintBytes(currentModel);
    // If the current model's size is unknown, assume it is fine (do nothing).
    if (currentFootprint == 0) {
        return QString();
    }
    // Only downscale when the active model no longer fits comfortably.
    if (fitsInMemory(currentModel, availableBytes)) {
        return QString();
    }

    QString target;
    quint64 targetFootprint = 0;
    for (const QString &model : installed) {
        if (model == currentModel || !fitsInMemory(model, availableBytes)) {
            continue;
        }
        const quint64 footprint = estimatedFootprintBytes(model);
        // Only consider strictly smaller models as downgrade targets.
        if (footprint > 0 && footprint < currentFootprint && footprint > targetFootprint) {
            targetFootprint = footprint;
            target = model;
        }
    }
    return target;
}

} // namespace ModelMemoryAdvisor
