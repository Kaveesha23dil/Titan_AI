#ifndef TITANAI_EXPORT_UTILS_HPP
#define TITANAI_EXPORT_UTILS_HPP

#include <QFileInfo>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QString>
#include <QTextDocument>

// ─────────────────────────────────────────────────────────────────────────────
//  Shared helpers for exporting conversations.
//
//  QTextDocument + QPdfWriter require a running QGuiApplication, so this lives
//  in the GUI layer (the plain-text/Markdown/HTML generators live in the agent
//  layer and stay testable without a GUI event loop).
// ─────────────────────────────────────────────────────────────────────────────

namespace ExportUtils {

/// Renders an HTML string to a paginated A4 PDF file using QTextDocument.
/// Returns true on success and optionally stores an error description.
inline bool writeHtmlToPdf(const QString &html,
                           const QString &title,
                           const QString &filePath,
                           QString *errorMessage = nullptr)
{
    if (filePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No destination path given.");
        }
        return false;
    }

    QPdfWriter writer(filePath);
    writer.setTitle(title.isEmpty() ? QStringLiteral("TitanAI Conversation") : title);
    writer.setCreator(QStringLiteral("TitanAI"));
    writer.setPageLayout(QPageLayout(
        QPageSize(QPageSize::A4),
        QPageLayout::Portrait,
        QMarginsF(16, 16, 16, 16),
        QPageLayout::Millimeter));

    QTextDocument document;
    document.setDocumentMargin(0);
    document.setDefaultStyleSheet(QStringLiteral(
        "code, pre { font-family: 'DejaVu Sans Mono', monospace; font-size: 9pt; "
        "background: #f1f5f9; border-radius: 3px; padding: 1px 3px; }"));

    document.setHtml(html);

    document.print(&writer);
    return QFileInfo::exists(filePath);
}

} // namespace ExportUtils

#endif // TITANAI_EXPORT_UTILS_HPP