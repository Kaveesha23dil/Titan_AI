#ifndef TITANAI_MODEL_MEMORY_ADVISOR_HPP
#define TITANAI_MODEL_MEMORY_ADVISOR_HPP

#include <QString>
#include <QStringList>
#include <QtGlobal>

// Memory-aware model negotiation.
//
// This pure, dependency-free utility decides which local Ollama model is the
// best fit for the currently available system RAM, and whether/when to
// downscale the active model when memory is critically low. Keeping it as free
// functions makes the logic trivial to unit-test and lets both the Agent
// (automatic negotiation) and the ModelDialog (per-model "fits your RAM"
// annotations) share the same heuristics.
namespace ModelMemoryAdvisor {

// Estimated resident-memory footprint (in bytes) for an Ollama model tag,
// derived from its parameter count (e.g. "qwen2.5-coder:14b"). 0 is returned
// for tags whose size we cannot determine (treated as "unknown / assume OK").
quint64 estimatedFootprintBytes(const QString &modelTag);

// Human-readable footprint, e.g. "~12 GB".
QString footprintDisplay(const QString &modelTag);

// Whether the given model fits within the available RAM plus a safety margin.
// Unknown-size models always return true (do not block them).
bool fitsInMemory(const QString &modelTag, quint64 availableBytes);

// Returns the largest installed model that fits within available RAM, or an
// empty string if none fit. Used to suggest an upgrade when there is room.
QString recommendBest(const QStringList &installed, quint64 availableBytes);

// If `currentModel` does not fit within available RAM, returns the largest
// installed model that *does* fit and has a smaller footprint than the current
// one (the best downgrade target). Otherwise returns an empty string.
QString suggestDownscale(const QStringList &installed,
                         quint64 availableBytes,
                         const QString &currentModel);

} // namespace ModelMemoryAdvisor

#endif // TITANAI_MODEL_MEMORY_ADVISOR_HPP
