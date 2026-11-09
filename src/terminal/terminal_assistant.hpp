#ifndef TITANAI_TERMINAL_ASSISTANT_HPP
#define TITANAI_TERMINAL_ASSISTANT_HPP

#include <QObject>
#include <QString>

#include "llm/ollama_client.hpp"
#include "terminal/terminal_types.hpp"

/// Generates Linux command proposals and explanations through the local Ollama
/// integration. Model responses are always parsed as structured JSON and then
/// passed through CommandSafetyValidator before being exposed to the UI.
class TerminalAssistant : public QObject {
    Q_OBJECT

public:
    explicit TerminalAssistant(QObject *parent = nullptr);
    ~TerminalAssistant() override = default;

    void setModel(const QString &model);
    [[nodiscard]] QString model() const;
    [[nodiscard]] bool isBusy() const;

    void generateCommand(const QString &naturalLanguageRequest);
    void explainOutput(const QString &naturalLanguageRequest,
                       const QString &command,
                       const QString &output);
    void explainCommand(const QString &command, const QString &context = QString());

    [[nodiscard]] static bool isTerminalRequest(const QString &message);
    [[nodiscard]] static QString buildCommandPrompt(const QString &request);
    [[nodiscard]] static QString buildExplanationPrompt(const QString &request,
                                                        const QString &command,
                                                        const QString &output);
    [[nodiscard]] static bool parseCommandProposal(const QString &response,
                                                   TerminalCommandProposal *proposal,
                                                   QString *error);
    [[nodiscard]] static QString sanitizeOutput(const QString &output,
                                                int maxChars = 8000);

signals:
    void generationStarted(const QString &request);
    void proposalReady(const TerminalCommandProposal &proposal);
    void generationFailed(const QString &error);

    void explanationStarted();
    void explanationReady(const QString &explanation);
    void explanationFailed(const QString &error);

private slots:
    void onCompletionReceived(const QString &response);
    void onCompletionError(const QString &error);

private:
    enum class Mode { Idle, GenerateCommand, ExplainCommand, ExplainOutput };

    void finishWithError(const QString &error);

    OllamaClient m_client;
    Mode        m_mode{Mode::Idle};
    QString     m_pendingRequest;
    QString     m_pendingCommand;
};

#endif // TITANAI_TERMINAL_ASSISTANT_HPP
