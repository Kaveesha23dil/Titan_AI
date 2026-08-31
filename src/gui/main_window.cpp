#include "gui/main_window.hpp"
#include "gui/camera_dialog.hpp"
#include "gui/model_dialog.hpp"
#include "gui/voice_settings_dialog.hpp"
#include "gui/calendar_settings_dialog.hpp"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QGraphicsDropShadowEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTextBrowser>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────
//  Colour tokens  (dark theme)
// ─────────────────────────────────────────────────────────────
namespace Col {
constexpr auto BgDeep      = "#0b0f19";
constexpr auto BgSidebar   = "#070a13";
constexpr auto BgCard      = "#111827";
constexpr auto BgCardHover = "#1a2236";
constexpr auto BgInput     = "#0f1629";
constexpr auto Border      = "#1e293b";
constexpr auto BorderLight = "#2a3a52";
constexpr auto Accent      = "#6366f1";   // Indigo
constexpr auto AccentGlow  = "#818cf8";
constexpr auto AccentCyan  = "#22d3ee";
constexpr auto TextPrimary = "#f1f5f9";
constexpr auto TextSecondary = "#94a3b8";
constexpr auto TextMuted   = "#64748b";
constexpr auto Danger      = "#ef4444";
constexpr auto Success     = "#22c55e";
constexpr auto Warning     = "#f59e0b";
constexpr auto InfoBg      = "#1e1b4b";
}

// ─────────────────────────────────────────────────────────────
//  Vector icon factory (QPainter-based, DPI-independent)
// ─────────────────────────────────────────────────────────────
QIcon MainWindow::createVectorIcon(const QString &name, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(QColor(Col::TextSecondary), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal s = size;
    const qreal m = s * 0.18;  // margin

    if (name == QLatin1String("home")) {
        // House outline
        QPainterPath path;
        path.moveTo(s / 2, m);
        path.lineTo(s - m, s * 0.45);
        path.lineTo(s - m, s - m);
        path.lineTo(m, s - m);
        path.lineTo(m, s * 0.45);
        path.closeSubpath();
        p.drawPath(path);
        // Door
        p.drawRect(QRectF(s * 0.38, s * 0.58, s * 0.24, s - m - s * 0.58));
    } else if (name == QLatin1String("chat")) {
        // Speech bubble
        QRectF r(m, m, s - 2 * m, (s - 2 * m) * 0.78);
        p.drawRoundedRect(r, 4, 4);
        // Tail
        QPainterPath tail;
        tail.moveTo(s * 0.28, r.bottom());
        tail.lineTo(s * 0.22, s - m);
        tail.lineTo(s * 0.42, r.bottom());
        p.drawPath(tail);
    } else if (name == QLatin1String("dev")) {
        // Code brackets  < / >
        p.drawLine(QPointF(s * 0.30, m), QPointF(m, s / 2));
        p.drawLine(QPointF(m, s / 2), QPointF(s * 0.30, s - m));
        p.drawLine(QPointF(s * 0.70, m), QPointF(s - m, s / 2));
        p.drawLine(QPointF(s - m, s / 2), QPointF(s * 0.70, s - m));
        p.drawLine(QPointF(s * 0.55, m * 0.7), QPointF(s * 0.45, s - m * 0.7));
    } else if (name == QLatin1String("calendar")) {
        QRectF r(m, m + 2, s - 2 * m, s - 2 * m - 2);
        p.drawRoundedRect(r, 3, 3);
        p.drawLine(QPointF(m, m + (s - 2 * m) * 0.32 + 2), QPointF(s - m, m + (s - 2 * m) * 0.32 + 2));
        p.drawLine(QPointF(s * 0.35, m), QPointF(s * 0.35, m - 2));
        p.drawLine(QPointF(s * 0.65, m), QPointF(s * 0.65, m - 2));
    } else if (name == QLatin1String("settings")) {
        // Gear
        qreal cx = s / 2, cy = s / 2, r1 = s * 0.2, r2 = s * 0.34;
        p.drawEllipse(QPointF(cx, cy), r1, r1);
        for (int i = 0; i < 8; ++i) {
            qreal angle = i * 45.0 * M_PI / 180.0;
            p.drawLine(QPointF(cx + r1 * 0.85 * std::cos(angle), cy + r1 * 0.85 * std::sin(angle)),
                       QPointF(cx + r2 * std::cos(angle), cy + r2 * std::sin(angle)));
        }
    } else if (name == QLatin1String("mic")) {
        // Microphone
        QRectF mic(s * 0.36, m, s * 0.28, s * 0.45);
        p.drawRoundedRect(mic, mic.width() / 2, mic.width() / 2);
        p.drawArc(QRectF(s * 0.24, s * 0.2, s * 0.52, s * 0.48), 0, -180 * 16);
        p.drawLine(QPointF(s / 2, s * 0.68), QPointF(s / 2, s - m));
        p.drawLine(QPointF(s * 0.35, s - m), QPointF(s * 0.65, s - m));
    } else if (name == QLatin1String("camera")) {
        QRectF body(m, s * 0.3, s - 2 * m, s * 0.5);
        p.drawRoundedRect(body, 3, 3);
        p.drawEllipse(QPointF(s / 2, s * 0.55), s * 0.13, s * 0.13);
        QPainterPath lens;
        lens.moveTo(s * 0.35, s * 0.3);
        lens.lineTo(s * 0.40, s * 0.2);
        lens.lineTo(s * 0.60, s * 0.2);
        lens.lineTo(s * 0.65, s * 0.3);
        p.drawPath(lens);
    } else if (name == QLatin1String("image")) {
        QRectF r(m, m, s - 2 * m, s - 2 * m);
        p.drawRoundedRect(r, 3, 3);
        p.drawEllipse(QPointF(s * 0.35, s * 0.35), s * 0.08, s * 0.08);
        QPainterPath mount;
        mount.moveTo(m, s * 0.72);
        mount.lineTo(s * 0.35, s * 0.48);
        mount.lineTo(s * 0.55, s * 0.62);
        mount.lineTo(s * 0.68, s * 0.52);
        mount.lineTo(s - m, s * 0.72);
        p.drawPath(mount);
    } else if (name == QLatin1String("send")) {
        pen.setColor(QColor(Col::Accent));
        p.setPen(pen);
        QPainterPath arrow;
        arrow.moveTo(m, s - m);
        arrow.lineTo(s - m, s / 2);
        arrow.lineTo(m, m);
        arrow.lineTo(s * 0.3, s / 2);
        arrow.closeSubpath();
        p.fillPath(arrow, QColor(Col::Accent));
        p.drawPath(arrow);
    } else if (name == QLatin1String("organize")) {
        QRectF r(m, m + 3, s - 2 * m, s - 2 * m - 3);
        p.drawRoundedRect(r, 3, 3);
        p.drawLine(QPointF(m, m + 3 + (s - 2 * m - 3) * 0.22), QPointF(s * 0.42, m + 3 + (s - 2 * m - 3) * 0.22));
        p.drawLine(QPointF(m, m + 3), QPointF(m + (s - 2 * m) * 0.3, m + 3));
    } else if (name == QLatin1String("cleanup")) {
        // Broom: diagonal handle plus bristle fan
        const QPointF tip(s * 0.52, s * 0.48);
        p.drawLine(QPointF(s * 0.80, s * 0.20), tip);
        QPainterPath fan;
        fan.moveTo(tip.x(), tip.y() - s * 0.08);
        fan.lineTo(s * 0.16, s - m);
        fan.quadTo(s * 0.36, s - m + 1, s * 0.68, s - m);
        fan.closeSubpath();
        p.drawPath(fan);
        p.drawLine(QPointF(s * 0.30, s * 0.74), QPointF(s * 0.25, s - m));
        p.drawLine(QPointF(s * 0.44, s * 0.66), QPointF(s * 0.41, s - m));
    } else if (name == QLatin1String("update")) {
        // Circular refresh arrows
        QRectF circle(m, m, s - 2 * m, s - 2 * m);
        p.drawArc(circle, 45 * 16, 130 * 16);
        p.drawArc(circle, 225 * 16, 130 * 16);
        const qreal cx = s / 2, cy = s / 2, r = (s - 2 * m) / 2;
        auto arrowHead = [&](qreal angleDeg) {
            const qreal a = angleDeg * M_PI / 180.0;
            // Position on the circle; Qt y-axis is flipped, hence minus sin.
            const QPointF pt(cx + r * std::cos(a), cy - r * std::sin(a));
            // Tangent direction for counter-clockwise motion.
            const QPointF tangent(-std::sin(a), -std::cos(a));
            QPen headPen(pen);
            headPen.setWidthF(1.4);
            p.setPen(headPen);
            p.drawLine(pt, pt + tangent * 3.2);
            p.drawLine(pt, pt - QPointF(-tangent.y(), tangent.x()) * 3.2);
        };
        arrowHead(180.0);   // end of first arc
        arrowHead(360.0);   // end of second arc
    } else if (name == QLatin1String("voice_settings")) {
        // Sliders icon
        for (int i = 0; i < 3; ++i) {
            qreal y = m + (s - 2 * m) * (0.2 + i * 0.3);
            p.drawLine(QPointF(m, y), QPointF(s - m, y));
            qreal kx = m + (s - 2 * m) * (0.3 + i * 0.2);
            p.setBrush(QColor(Col::TextSecondary));
            p.drawEllipse(QPointF(kx, y), 2.5, 2.5);
            p.setBrush(Qt::NoBrush);
        }
    }

    p.end();
    return QIcon(pm);
}

// ─────────────────────────────────────────────────────────────
//  Global Dark QSS
// ─────────────────────────────────────────────────────────────
void MainWindow::setupGlobalStylesheet()
{
    const QString qss = QStringLiteral(
        // --- Main Window ---
        "QMainWindow { background: %1; }"

        // --- Labels ---
        "QLabel { color: %2; }"

        // --- QLineEdit ---
        "QLineEdit { "
        "  background: %3; color: %2; border: 1px solid %4; "
        "  border-radius: 8px; padding: 10px 14px; font-size: 14px; "
        "}"
        "QLineEdit:focus { border-color: %5; }"
        "QLineEdit::placeholder { color: %6; }"

        // --- QPushButton (default) ---
        "QPushButton { "
        "  background: %7; color: %2; border: 1px solid %4; "
        "  border-radius: 8px; padding: 8px 16px; font-size: 13px; font-weight: 500; "
        "}"
        "QPushButton:hover { background: %8; border-color: %9; }"
        "QPushButton:pressed { background: %5; }"
        "QPushButton:disabled { color: %6; background: %3; border-color: %4; }"

        // --- QCheckBox ---
        "QCheckBox { color: %2; font-size: 13px; spacing: 8px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; "
        "  border: 1px solid %4; background: %3; }"
        "QCheckBox::indicator:checked { background: %5; border-color: %5; }"

        // --- QGroupBox ---
        "QGroupBox { "
        "  color: %2; font-size: 14px; font-weight: 600; "
        "  border: 1px solid %4; border-radius: 12px; "
        "  margin-top: 12px; padding: 20px 16px 16px 16px; "
        "  background: %7; "
        "}"
        "QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 6px; }"

        // --- QTextBrowser (Chat display) ---
        "QTextBrowser { "
        "  background: %1; color: %2; border: none; "
        "  font-size: 14px; padding: 12px; selection-background-color: %5; "
        "}"

        // --- QScrollBar Vertical ---
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: %9; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"

        // --- QProgressBar (mic level) ---
        "QProgressBar { "
        "  border: 1px solid %4; border-radius: 4px; background: %3; "
        "}"
        "QProgressBar::chunk { background: %5; border-radius: 4px; }"

        // --- QMessageBox ---
        "QMessageBox { background: %7; }"
        "QMessageBox QLabel { color: %2; }"
        "QMessageBox QPushButton { min-width: 80px; }"
    ).arg(
        Col::BgDeep,        // %1
        Col::TextPrimary,   // %2
        Col::BgInput,       // %3
        Col::Border,        // %4
        Col::Accent,        // %5
        Col::TextMuted,     // %6
        Col::BgCard,        // %7
        Col::BgCardHover,   // %8
        Col::BorderLight    // %9
    );
    setStyleSheet(qss);
}

// ─────────────────────────────────────────────────────────────
//  Sidebar
// ─────────────────────────────────────────────────────────────
QWidget *MainWindow::createSidebar()
{
    auto *sidebar = new QWidget(this);
    sidebar->setFixedWidth(64);
    sidebar->setStyleSheet(QStringLiteral(
        "QWidget { background: %1; } "
        "QPushButton { "
        "  background: transparent; border: none; border-radius: 12px; "
        "  padding: 10px; min-width: 40px; min-height: 40px; max-width: 40px; max-height: 40px; "
        "} "
        "QPushButton:hover { background: rgba(99,102,241,0.15); } "
        "QPushButton:checked { background: rgba(99,102,241,0.25); border: 1px solid rgba(99,102,241,0.4); } "
    ).arg(Col::BgSidebar));

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 16, 12, 16);
    layout->setSpacing(6);

    // Logo
    auto *logo = new QLabel(QStringLiteral("T"), sidebar);
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(40, 40);
    logo->setStyleSheet(QStringLiteral(
        "QLabel { "
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 %1,stop:1 %2); "
        "  color: white; font-size: 18px; font-weight: 800; "
        "  border-radius: 12px; "
        "}"
    ).arg(Col::Accent, Col::AccentCyan));
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addSpacing(20);

    // Navigation buttons
    auto makeNavBtn = [&](const QString &iconName, const QString &tooltip) {
        auto *btn = new QPushButton(sidebar);
        btn->setIcon(createVectorIcon(iconName, 22));
        btn->setIconSize(QSize(22, 22));
        btn->setToolTip(tooltip);
        btn->setCheckable(true);
        btn->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(btn, 0, Qt::AlignHCenter);
        return btn;
    };

    m_navHome = makeNavBtn(QStringLiteral("home"), QStringLiteral("Home"));
    m_navChat = makeNavBtn(QStringLiteral("chat"), QStringLiteral("Chat"));
    m_navDev  = makeNavBtn(QStringLiteral("dev"),  QStringLiteral("Developer Hub"));
    m_navCalendar = makeNavBtn(QStringLiteral("calendar"), QStringLiteral("Calendar Settings"));

    m_navHome->setChecked(true);

    layout->addStretch(1);

    // Bottom utility buttons
    m_navVoiceSettings = makeNavBtn(QStringLiteral("voice_settings"), QStringLiteral("Voice Settings"));
    m_navVoiceSettings->setCheckable(false);
    m_navSettings = makeNavBtn(QStringLiteral("settings"), QStringLiteral("Settings"));
    m_navSettings->setCheckable(false);

    // Connect navigation
    connect(m_navHome, &QPushButton::clicked, this, [this]() { navigateTo(0); });
    connect(m_navChat, &QPushButton::clicked, this, [this]() { navigateTo(1); });
    connect(m_navDev,  &QPushButton::clicked, this, [this]() { navigateTo(2); });
    connect(m_navCalendar, &QPushButton::clicked, this, &MainWindow::onOpenCalendarSettings);
    connect(m_navVoiceSettings, &QPushButton::clicked, this, &MainWindow::onVoiceSettings);
    connect(m_navSettings, &QPushButton::clicked, this, [this]() { navigateTo(3); });

    return sidebar;
}

// ─────────────────────────────────────────────────────────────
//  Welcome (Home) Page
// ─────────────────────────────────────────────────────────────
QWidget *MainWindow::createWelcomePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 30, 40, 30);
    layout->setSpacing(0);

    layout->addStretch(2);

    // Orb image
    auto *orbLabel = new QLabel(page);
    QPixmap orbPix(QStringLiteral(":/assets/orb.jpg"));
    if (!orbPix.isNull()) {
        // Create circular mask
        int dim = 160;
        QPixmap scaled = orbPix.scaled(dim, dim, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QPixmap circular(dim, dim);
        circular.fill(Qt::transparent);
        QPainter painter(&circular);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addEllipse(0, 0, dim, dim);
        painter.setClipPath(path);
        painter.drawPixmap(0, 0, scaled);
        painter.end();
        orbLabel->setPixmap(circular);
    }
    orbLabel->setAlignment(Qt::AlignCenter);
    orbLabel->setFixedHeight(170);
    layout->addWidget(orbLabel);
    layout->addSpacing(20);

    // Greeting
    m_welcomeGreeting = new QLabel(QStringLiteral("Hi there 👋"), page);
    m_welcomeGreeting->setAlignment(Qt::AlignCenter);
    m_welcomeGreeting->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 22px; font-weight: 400; }").arg(Col::TextSecondary));
    layout->addWidget(m_welcomeGreeting);
    layout->addSpacing(4);

    // Main title
    auto *title = new QLabel(QStringLiteral("How can I help today?"), page);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 32px; font-weight: 700; }").arg(Col::TextPrimary));
    layout->addWidget(title);
    layout->addSpacing(6);

    // Subtitle
    m_welcomeSubtitle = new QLabel(
        QStringLiteral("I'm here to help — from system queries\nto smart recommendations."), page);
    m_welcomeSubtitle->setAlignment(Qt::AlignCenter);
    m_welcomeSubtitle->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 14px; }").arg(Col::TextMuted));
    layout->addWidget(m_welcomeSubtitle);
    layout->addSpacing(28);

    // Input card slot
    m_welcomeInputSlot = new QVBoxLayout;
    m_welcomeInputSlot->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(m_welcomeInputSlot);
    layout->addSpacing(30);

    // Suggestion cards row
    auto *suggestionsRow = new QHBoxLayout;
    suggestionsRow->setSpacing(12);

    struct SuggestionDef {
        QString emoji;
        QString title;
        QString description;
        QString prompt;
    };

    const QList<SuggestionDef> suggestions = {
        {QStringLiteral("🖥️"), QStringLiteral("System Info"),
         QStringLiteral("Get detailed info about\nyour Arch Linux system."),
         QStringLiteral("Show me my system info")},
        {QStringLiteral("📦"), QStringLiteral("Install Package"),
         QStringLiteral("Install any package using\npacman or AUR helpers."),
         QStringLiteral("Install ")},
        {QStringLiteral("🔧"), QStringLiteral("Fix Build Errors"),
         QStringLiteral("Auto-detect and fix code\nbuild errors locally."),
         QStringLiteral("Fix my build errors")},
        {QStringLiteral("📁"), QStringLiteral("Organize Files"),
         QStringLiteral("Find duplicates and get\nfolder structure ideas."),
         QStringLiteral("Find duplicate files in my project")},
        {QStringLiteral("🧹"), QStringLiteral("Disk Cleanup"),
         QStringLiteral("Monitor disk usage and find\nsafe cleanup actions."),
         QStringLiteral("Analyze my disk usage for cleanup")},
        {QStringLiteral("🔄"), QStringLiteral("Check Updates"),
         QStringLiteral("See which packages have\nnewer versions available."),
         QStringLiteral("Check my system for updates")},
    };

    for (const auto &sg : suggestions) {
        auto *card = new QPushButton(page);
        card->setFocusPolicy(Qt::NoFocus);
        card->setCursor(Qt::PointingHandCursor);

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 16, 16, 16);
        cardLayout->setSpacing(6);

        auto *emojiLabel = new QLabel(sg.emoji, card);
        emojiLabel->setStyleSheet(QStringLiteral("font-size: 22px; background: transparent; border: none;"));
        emojiLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        cardLayout->addWidget(emojiLabel);

        auto *titleLabel = new QLabel(sg.title, card);
        titleLabel->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 14px; font-weight: 600; background: transparent; border: none; }").arg(Col::TextPrimary));
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        cardLayout->addWidget(titleLabel);

        auto *descLabel = new QLabel(sg.description, card);
        descLabel->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 12px; background: transparent; border: none; }").arg(Col::TextMuted));
        descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        cardLayout->addWidget(descLabel);
        cardLayout->addStretch();

        card->setMinimumHeight(120);
        card->setStyleSheet(QStringLiteral(
            "QPushButton { "
            "  background: %1; border: 1px solid %2; border-radius: 14px; "
            "  text-align: left; "
            "} "
            "QPushButton:hover { background: %3; border-color: %4; }"
        ).arg(Col::BgCard, Col::Border, Col::BgCardHover, Col::BorderLight));

        connect(card, &QPushButton::clicked, this, [this, prompt = sg.prompt]() {
            navigateTo(1);
            m_input->setText(prompt);
            m_input->setFocus();
            if (prompt == QStringLiteral("Install ")) {
                // Position cursor after "Install " for user to type package name
                m_input->setCursorPosition(m_input->text().length());
            } else {
                onSendClicked();
            }
        });

        suggestionsRow->addWidget(card, 1);
    }

    layout->addLayout(suggestionsRow);
    layout->addStretch(1);

    return page;
}

// ─────────────────────────────────────────────────────────────
//  Chat Page
// ─────────────────────────────────────────────────────────────
QWidget *MainWindow::createChatPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Header bar ──
    auto *headerBar = new QWidget(page);
    headerBar->setFixedHeight(56);
    headerBar->setStyleSheet(QStringLiteral(
        "QWidget { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, "
        "stop:0 %1, stop:1 %2); border-bottom: 1px solid %3; }")
        .arg(Col::BgCard, Col::BgDeep, Col::Border));

    auto *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(20, 0, 20, 0);

    // Avatar circle
    auto *avatarLabel = new QLabel(headerBar);
    QPixmap avatarPm(32, 32);
    avatarPm.fill(Qt::transparent);
    {
        QPainter ap(&avatarPm);
        ap.setRenderHint(QPainter::Antialiasing);
        QLinearGradient grad(0, 0, 32, 32);
        grad.setColorAt(0, QColor(Col::Accent));
        grad.setColorAt(1, QColor(Col::AccentCyan));
        ap.setBrush(grad);
        ap.setPen(Qt::NoPen);
        ap.drawEllipse(0, 0, 32, 32);
        ap.setPen(Qt::white);
        QFont f = ap.font();
        f.setBold(true);
        f.setPixelSize(14);
        ap.setFont(f);
        ap.drawText(QRect(0, 0, 32, 32), Qt::AlignCenter, QStringLiteral("T"));
        ap.end();
    }
    avatarLabel->setPixmap(avatarPm);
    avatarLabel->setFixedSize(32, 32);
    avatarLabel->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
    headerLayout->addWidget(avatarLabel);
    headerLayout->addSpacing(10);

    auto *headerTitle = new QLabel(QStringLiteral("TitanAI"), headerBar);
    headerTitle->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 16px; font-weight: 700; border: none; background: transparent; }").arg(Col::TextPrimary));
    headerLayout->addWidget(headerTitle);

    // Status badge
    m_statusLabel = new QLabel(QStringLiteral("● Initializing..."), headerBar);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; border: none; background: transparent; }").arg(Col::TextMuted));
    headerLayout->addWidget(m_statusLabel);
    headerLayout->addStretch(1);

    // Online indicator dot
    auto *onlineDot = new QLabel(headerBar);
    onlineDot->setFixedSize(10, 10);
    onlineDot->setStyleSheet(QStringLiteral(
        "QLabel { background: %1; border-radius: 5px; border: none; }").arg(Col::Success));
    headerLayout->addWidget(onlineDot);
    auto *onlineText = new QLabel(QStringLiteral("Local"), headerBar);
    onlineText->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; border: none; background: transparent; }").arg(Col::TextMuted));
    headerLayout->addWidget(onlineText);

    layout->addWidget(headerBar);

    // ── Chat display ──
    auto *chatContainer = new QWidget(page);
    auto *chatLayout = new QVBoxLayout(chatContainer);
    chatLayout->setContentsMargins(16, 12, 16, 8);
    chatLayout->setSpacing(6);

    m_chatDisplay = new QTextBrowser(chatContainer);
    m_chatDisplay->setReadOnly(true);
    m_chatDisplay->setOpenExternalLinks(false);
    m_chatDisplay->setFrameShape(QFrame::NoFrame);
    // Custom CSS for the chat document itself
    m_chatDisplay->document()->setDefaultStyleSheet(QStringLiteral(
        "body { margin: 0; padding: 0; } "
        "table { margin-bottom: 6px; } "
        "td { padding: 10px 14px; font-size: 14px; } "
        ".bubble-bot { background-color: %1; } "
        ".bubble-user { background-color: %2; } "
        ".bubble-error { background-color: %3; } "
        ".bubble-tool { background-color: %4; } "
    ).arg(
        QStringLiteral("#151d2e"),  // Bot bubble bg
        QStringLiteral("#1a1540"),  // User bubble bg
        QStringLiteral("#2a1215"),  // Error bubble bg
        QStringLiteral("#111520")   // Tool output bg
    ));
    chatLayout->addWidget(m_chatDisplay, 1);

    // Notification banner
    m_notificationBanner = new QLabel(chatContainer);
    m_notificationBanner->setAlignment(Qt::AlignCenter);
    m_notificationBanner->setWordWrap(true);
    m_notificationBanner->setStyleSheet(QStringLiteral(
        "QLabel { background: %1; border: 1px solid %2; border-radius: 8px; "
        "padding: 10px 14px; color: %3; font-size: 12px; }")
        .arg(Col::InfoBg, Col::Accent, Col::AccentGlow));
    m_notificationBanner->setVisible(false);
    m_notificationBanner->setTextFormat(Qt::RichText);
    chatLayout->addWidget(m_notificationBanner);

    // Voice status row
    m_voiceStatusLabel = new QLabel(QStringLiteral("Voice disabled"), chatContainer);
    m_voiceStatusLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; }").arg(Col::TextMuted));

    m_micLevelBar = new QProgressBar(chatContainer);
    m_micLevelBar->setRange(0, 100);
    m_micLevelBar->setValue(0);
    m_micLevelBar->setTextVisible(false);
    m_micLevelBar->setFixedWidth(120);
    m_micLevelBar->setFixedHeight(6);
    m_micLevelBar->hide();

    auto *voiceStatusRow = new QHBoxLayout;
    voiceStatusRow->addWidget(m_voiceStatusLabel);
    voiceStatusRow->addStretch(1);
    voiceStatusRow->addWidget(m_micLevelBar);
    chatLayout->addLayout(voiceStatusRow);

    // Chat input slot
    m_chatInputSlot = new QVBoxLayout;
    m_chatInputSlot->setContentsMargins(0, 0, 0, 0);
    chatLayout->addLayout(m_chatInputSlot);

    layout->addWidget(chatContainer, 1);

    return page;
}

// ─────────────────────────────────────────────────────────────
//  Developer Hub Page
// ─────────────────────────────────────────────────────────────
QWidget *MainWindow::createDevHubPage()
{
    // Outer wrapper — fills the page slot in QStackedWidget
    auto *outer = new QWidget(this);
    auto *outerLayout = new QVBoxLayout(outer);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // ── Header bar ──────────────────────────────────────────
    auto *headerBar = new QWidget(outer);
    headerBar->setFixedHeight(56);
    headerBar->setStyleSheet(QStringLiteral(
        "QWidget { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %1,stop:1 %2); "
        "border-bottom: 1px solid %3; }").arg(Col::BgCard, Col::BgDeep, Col::Border));

    auto *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(20, 0, 20, 0);

    auto *headerTitle = new QLabel(QStringLiteral("🛠  Developer Hub"), headerBar);
    headerTitle->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 17px; font-weight: 700; border: none; background: transparent; }"
    ).arg(Col::TextPrimary));
    headerLayout->addWidget(headerTitle);
    headerLayout->addStretch(1);

    auto *headerSub = new QLabel(
        QStringLiteral("Build · Fix · Analyze · Generate UI"), headerBar);
    headerSub->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; border: none; background: transparent; }"
    ).arg(Col::TextMuted));
    headerLayout->addWidget(headerSub);
    outerLayout->addWidget(headerBar);

    // ── Scroll area ─────────────────────────────────────────
    auto *scrollArea = new QScrollArea(outer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: %1; border: none; }").arg(Col::BgDeep));

    auto *content = new QWidget(scrollArea);
    content->setStyleSheet(QStringLiteral("background: %1;").arg(Col::BgDeep));
    scrollArea->setWidget(content);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 24, 28, 32);
    layout->setSpacing(16);

    // Helper: creates a uniform styled button
    auto makeActionBtn = [&](QWidget *parent, const QString &text,
                              const QString &iconName, const QString &tooltip) {
        auto *btn = new QPushButton(QStringLiteral("  ") + text, parent);
        if (!iconName.isEmpty()) {
            btn->setIcon(createVectorIcon(iconName, 20));
        }
        btn->setToolTip(tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: white; border: none; font-weight: 600; "
            "             padding: 10px 20px; border-radius: 8px; } "
            "QPushButton:hover { background: %2; } "
            "QPushButton:disabled { background: %3; color: %4; }"
        ).arg(Col::Accent, Col::AccentGlow, Col::BgCard, Col::TextMuted));
        return btn;
    };

    // ── ⓪ AI Performance & Memory Profile ─────────────────
    auto *perfBox = new QGroupBox(QStringLiteral("⚡  AI Memory & Performance Profile"), content);
    auto *perfLayout = new QVBoxLayout(perfBox);
    perfLayout->setSpacing(10);

    auto *perfRow = new QHBoxLayout;
    auto *perfLabel = new QLabel(QStringLiteral("Active Model:"), perfBox);
    perfLabel->setFixedWidth(100);

    m_aiModelLabel = new QLabel(m_agent.currentModel(), perfBox);
    m_aiModelLabel->setStyleSheet(QStringLiteral(
        "background: %1; border: 1px solid %2; border-radius: 6px; color: %3; padding: 6px 12px; font-weight: 600;"
    ).arg(Col::BgCard, Col::Border, Col::AccentGlow));

    m_changeModelButton = new QPushButton(QStringLiteral("⚙ Switch Model..."), perfBox);
    m_changeModelButton->setToolTip(QStringLiteral("Open the AI model manager to switch or install a more powerful model"));
    m_changeModelButton->setCursor(Qt::PointingHandCursor);
    m_changeModelButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 6px; "
        "              color: %3; padding: 6px 12px; font-weight: 600; }"
        "QPushButton:hover { background: %4; color: white; border-color: %4; }"
    ).arg(Col::BgCard, Col::Border, Col::Accent, Col::Accent));

    connect(m_changeModelButton, &QPushButton::clicked, this, &MainWindow::onManageModels);

    auto *freeRamBtn = new QPushButton(QStringLiteral("🧹 Free AI RAM"), perfBox);
    freeRamBtn->setToolTip(QStringLiteral("Immediately unload Ollama model weights from RAM"));
    freeRamBtn->setCursor(Qt::PointingHandCursor);
    freeRamBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 6px; "
        "              color: %3; padding: 6px 12px; font-weight: 600; }"
        "QPushButton:hover { background: %4; color: white; border-color: %4; }"
    ).arg(Col::BgCard, Col::Border, Col::Warning, Col::Warning));

    connect(freeRamBtn, &QPushButton::clicked, this, [this, freeRamBtn]() {
        m_agent.unloadModel();
        freeRamBtn->setText(QStringLiteral("✓ RAM Freed!"));
        QTimer::singleShot(2000, freeRamBtn, [freeRamBtn]() {
            freeRamBtn->setText(QStringLiteral("🧹 Free AI RAM"));
        });
    });

    perfRow->addWidget(perfLabel);
    perfRow->addWidget(m_aiModelLabel, 1);
    perfRow->addWidget(m_changeModelButton);
    perfRow->addWidget(freeRamBtn);
    perfLayout->addLayout(perfRow);

    auto *perfNote = new QLabel(
        QStringLiteral("Auto-sleep enabled: Ollama automatically releases RAM after 5 minutes of idle time."), perfBox);
    perfNote->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(Col::TextMuted));
    perfLayout->addWidget(perfNote);
    layout->addWidget(perfBox);

    // ── 🔋 Power Management Card ────────────────────────────
    auto *powerBox = new QGroupBox(QStringLiteral("🔋  Power Management"), content);
    auto *powerLayout = new QVBoxLayout(powerBox);
    powerLayout->setSpacing(10);

    // Battery level bar + status label row
    auto *batTopRow = new QHBoxLayout;

    m_batteryLabel = new QLabel(QStringLiteral("Battery: –"), powerBox);
    m_batteryLabel->setStyleSheet(
        QStringLiteral("color: %1; font-weight: 600; font-size: 13px;").arg(Col::TextPrimary));
    batTopRow->addWidget(m_batteryLabel);
    batTopRow->addStretch();

    m_powerStatusLabel = new QLabel(QStringLiteral("⚡ Initialising..."), powerBox);
    m_powerStatusLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;").arg(Col::TextMuted));
    batTopRow->addWidget(m_powerStatusLabel);
    powerLayout->addLayout(batTopRow);

    m_batteryBar = new QProgressBar(powerBox);
    m_batteryBar->setRange(0, 100);
    m_batteryBar->setValue(0);
    m_batteryBar->setTextVisible(false);
    m_batteryBar->setFixedHeight(10);
    m_batteryBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: %1; border-radius: 5px; border: none; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 %2, stop:0.5 %3, stop:1 %4); border-radius: 5px; }"
    ).arg(Col::BgCard, Col::Danger, Col::Warning, Col::AccentGlow));
    powerLayout->addWidget(m_batteryBar);

    // Profile selector row
    auto *profileRow = new QHBoxLayout;
    auto *profileLabel = new QLabel(QStringLiteral("Power Profile:"), powerBox);
    profileLabel->setFixedWidth(110);
    profileLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Col::TextSecondary));

    m_powerProfileCombo = new QComboBox(powerBox);
    m_powerProfileCombo->addItem(QStringLiteral("🤖 Smart Auto"),   static_cast<int>(PowerProfile::SmartAuto));
    m_powerProfileCombo->addItem(QStringLiteral("⚡ Performance"),   static_cast<int>(PowerProfile::Performance));
    m_powerProfileCombo->addItem(QStringLiteral("⚖️  Balanced"),     static_cast<int>(PowerProfile::Balanced));
    m_powerProfileCombo->addItem(QStringLiteral("🌿 Power Saver"),   static_cast<int>(PowerProfile::PowerSaver));
    m_powerProfileCombo->setCurrentIndex(0);  // SmartAuto by default
    m_powerProfileCombo->setStyleSheet(QStringLiteral(
        "QComboBox { background: %1; border: 1px solid %2; border-radius: 6px; "
        "            color: %3; padding: 6px 12px; } "
        "QComboBox::drop-down { border: none; } "
        "QComboBox QAbstractItemView { background: %1; color: %3; selection-background-color: %4; }"
    ).arg(Col::BgCard, Col::Border, Col::TextPrimary, Col::Accent));
    connect(m_powerProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPowerProfileChanged);

    profileRow->addWidget(profileLabel);
    profileRow->addWidget(m_powerProfileCombo, 1);
    powerLayout->addLayout(profileRow);

    // Brightness slider row
    auto *brightRow = new QHBoxLayout;
    auto *brightLabel = new QLabel(QStringLiteral("Brightness:"), powerBox);
    brightLabel->setFixedWidth(110);
    brightLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Col::TextSecondary));

    m_brightnessSlider = new QSlider(Qt::Horizontal, powerBox);
    m_brightnessSlider->setRange(5, 100);
    m_brightnessSlider->setValue(m_agent.powerManager().currentBrightness() > 0
                                 ? m_agent.powerManager().currentBrightness() : 80);
    m_brightnessSlider->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal { background: %1; height: 6px; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: %2; width: 16px; height: 16px; "
        "  margin: -5px 0; border-radius: 8px; }"
        "QSlider::sub-page:horizontal { background: %3; border-radius: 3px; }"
    ).arg(Col::BgCard, Col::AccentGlow, Col::Accent));
    connect(m_brightnessSlider, &QSlider::valueChanged,
            this, &MainWindow::onBrightnessSliderChanged);

    m_brightnessValueLabel = new QLabel(
        QStringLiteral("%1%").arg(m_brightnessSlider->value()), powerBox);
    m_brightnessValueLabel->setFixedWidth(36);
    m_brightnessValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_brightnessValueLabel->setStyleSheet(
        QStringLiteral("color: %1; font-weight: 600;").arg(Col::TextPrimary));

    brightRow->addWidget(brightLabel);
    brightRow->addWidget(m_brightnessSlider, 1);
    brightRow->addWidget(m_brightnessValueLabel);
    powerLayout->addLayout(brightRow);

    // Free AI RAM button (reuse from perf card concept)
    auto *powerNote = new QLabel(
        QStringLiteral("Smart Auto adapts power profile based on battery level and current activity."),
        powerBox);
    powerNote->setWordWrap(true);
    powerNote->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(Col::TextMuted));
    powerLayout->addWidget(powerNote);

    layout->addWidget(powerBox);

    // Connect PowerManager signals → GUI updates
    connect(&m_agent.powerManager(), &PowerManager::batteryInfoUpdated,
            this, &MainWindow::onBatteryInfoUpdated);
    connect(&m_agent.powerManager(), &PowerManager::lowBatteryWarning,
            this, &MainWindow::onLowBatteryWarning);
    connect(&m_agent.powerManager(), &PowerManager::criticalBatteryWarning,
            this, &MainWindow::onCriticalBatteryWarning);
    connect(&m_agent.powerManager(), &PowerManager::profileChanged,
            this, [this](PowerProfile p) {
                const int idx = m_powerProfileCombo->findData(static_cast<int>(p));
                if (idx >= 0) m_powerProfileCombo->setCurrentIndex(idx);
            });
    connect(&m_agent.powerManager(), &PowerManager::brightnessChanged,
            this, [this](int pct) {
                m_brightnessSlider->blockSignals(true);
                m_brightnessSlider->setValue(pct);
                m_brightnessSlider->blockSignals(false);
                m_brightnessValueLabel->setText(QStringLiteral("%1%").arg(pct));
            });

    // Initial battery info fill
    QTimer::singleShot(500, this, [this]() {
        onBatteryInfoUpdated(m_agent.powerManager().batteryInfo());
    });

    // ── ① Auto-Fix Code Errors ──────────────────────────────
    auto *fixerBox = new QGroupBox(QStringLiteral("🔧  Auto-Fix Code Errors"), content);
    auto *fixerLayout = new QVBoxLayout(fixerBox);
    fixerLayout->setSpacing(12);

    m_autoFixCheck = new QCheckBox(
        QStringLiteral("Enable automatic code-error fixing after build"), fixerBox);
    fixerLayout->addWidget(m_autoFixCheck);

    auto *projectRow = new QHBoxLayout;
    auto *projectLabel = new QLabel(QStringLiteral("Project:"), fixerBox);
    projectLabel->setFixedWidth(62);
    m_projectEdit = new QLineEdit(fixerBox);
    m_projectEdit->setPlaceholderText(QStringLiteral("e.g. /home/you/my-project"));
    m_browseButton = new QPushButton(QStringLiteral("Browse…"), fixerBox);
    m_browseButton->setCursor(Qt::PointingHandCursor);
    m_browseButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 6px; "
        "              color: %3; padding: 6px 14px; }"
        "QPushButton:hover { border-color: %4; color: %4; }"
    ).arg(Col::BgCard, Col::Border, Col::TextSecondary, Col::AccentGlow));
    projectRow->addWidget(projectLabel);
    projectRow->addWidget(m_projectEdit, 1);
    projectRow->addWidget(m_browseButton);
    fixerLayout->addLayout(projectRow);

    auto *buildRow = new QHBoxLayout;
    auto *buildLabel = new QLabel(QStringLiteral("Build cmd:"), fixerBox);
    buildLabel->setFixedWidth(62);
    m_buildEdit = new QLineEdit(fixerBox);
    m_buildEdit->setPlaceholderText(QStringLiteral("cmake --build build   |   npm run build"));
    m_buildFixButton = makeActionBtn(fixerBox, QStringLiteral("Build && Fix"),
                                     QString(), QStringLiteral("Run build command and auto-fix errors"));
    buildRow->addWidget(buildLabel);
    buildRow->addWidget(m_buildEdit, 1);
    buildRow->addWidget(m_buildFixButton);
    fixerLayout->addLayout(buildRow);
    layout->addWidget(fixerBox);

    // ── ② UI Design to Code ─────────────────────────────────
    auto *uiBox = new QGroupBox(QStringLiteral("🎨  UI Design to Code"), content);
    auto *uiLayout = new QVBoxLayout(uiBox);
    uiLayout->setSpacing(12);

    auto *uiDesc = new QLabel(
        QStringLiteral("Attach a UI wireframe / screenshot and describe what to build. "
                       "TitanAI will generate production-ready code and commit it on a new git branch."),
        uiBox);
    uiDesc->setWordWrap(true);
    uiDesc->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; }").arg(Col::TextSecondary));
    uiLayout->addWidget(uiDesc);

    // Design image row
    auto *designRow = new QHBoxLayout;
    designRow->setSpacing(10);

    m_uiDesignPreview = new QLabel(QStringLiteral("No design image attached"), uiBox);
    m_uiDesignPreview->setFixedSize(120, 80);
    m_uiDesignPreview->setAlignment(Qt::AlignCenter);
    m_uiDesignPreview->setWordWrap(true);
    m_uiDesignPreview->setStyleSheet(QStringLiteral(
        "QLabel { background: %1; border: 2px dashed %2; border-radius: 8px; "
        "         color: %3; font-size: 11px; }").arg(Col::BgCard, Col::Border, Col::TextMuted));
    designRow->addWidget(m_uiDesignPreview);

    auto *imgBtnCol = new QVBoxLayout;
    imgBtnCol->setSpacing(6);
    m_uiDesignPickBtn = new QPushButton(QStringLiteral("📁  Select Image"), uiBox);
    m_uiDesignPickBtn->setCursor(Qt::PointingHandCursor);
    m_uiDesignPickBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 6px; "
        "              color: %3; padding: 8px 14px; font-size: 13px; }"
        "QPushButton:hover { border-color: %4; color: %4; }"
    ).arg(Col::BgCard, Col::Border, Col::TextSecondary, Col::AccentGlow));
    imgBtnCol->addWidget(m_uiDesignPickBtn);

    m_uiDesignClearBtn = new QPushButton(QStringLiteral("✕  Clear Image"), uiBox);
    m_uiDesignClearBtn->setCursor(Qt::PointingHandCursor);
    m_uiDesignClearBtn->setEnabled(false);
    m_uiDesignClearBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: 1px solid %1; border-radius: 6px; "
        "              color: %1; padding: 8px 14px; font-size: 13px; }"
        "QPushButton:hover { border-color: %2; color: %2; }"
        "QPushButton:disabled { color: %3; border-color: %4; }"
    ).arg(Col::Danger, Col::Warning, Col::TextMuted, Col::Border));
    imgBtnCol->addWidget(m_uiDesignClearBtn);
    imgBtnCol->addStretch();
    designRow->addLayout(imgBtnCol);
    designRow->addStretch(1);
    uiLayout->addLayout(designRow);

    // Framework + Branch row
    auto *fwBranchRow = new QHBoxLayout;
    fwBranchRow->setSpacing(12);

    auto *fwLabel = new QLabel(QStringLiteral("Framework:"), uiBox);
    fwLabel->setFixedWidth(80);
    m_uiFrameworkCombo = new QComboBox(uiBox);
    m_uiFrameworkCombo->addItems({
        QStringLiteral("Auto-detect"),
        QStringLiteral("HTML / CSS / JS"),
        QStringLiteral("React"),
        QStringLiteral("Vue.js"),
        QStringLiteral("Qt 6 C++"),
        QStringLiteral("Flutter / Dart"),
        QStringLiteral("Python"),
    });
    m_uiFrameworkCombo->setMinimumWidth(160);
    fwBranchRow->addWidget(fwLabel);
    fwBranchRow->addWidget(m_uiFrameworkCombo);

    auto *branchLabel = new QLabel(QStringLiteral("Branch:"), uiBox);
    branchLabel->setFixedWidth(55);
    m_uiBranchEdit = new QLineEdit(uiBox);
    m_uiBranchEdit->setPlaceholderText(QStringLiteral("feat/ui-design  (auto-generated if blank)"));
    fwBranchRow->addWidget(branchLabel);
    fwBranchRow->addWidget(m_uiBranchEdit, 1);
    uiLayout->addLayout(fwBranchRow);

    // Requirements box
    auto *reqLabel = new QLabel(
        QStringLiteral("Requirements / Description:"), uiBox);
    reqLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: 600; }").arg(Col::TextSecondary));
    uiLayout->addWidget(reqLabel);

    m_uiRequirementsEdit = new QPlainTextEdit(uiBox);
    m_uiRequirementsEdit->setPlaceholderText(QStringLiteral(
        "Describe the UI you want to build, e.g.:\n"
        "  \"A dark-themed login page with email/password fields, a gradient hero image, "
        "animated submit button, and a footer with social links.\""));
    m_uiRequirementsEdit->setFixedHeight(100);
    m_uiRequirementsEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; "
        "                  padding: 8px; font-size: 13px; }"
        "QPlainTextEdit:focus { border-color: %4; }"
    ).arg(Col::BgCard, Col::TextPrimary, Col::Border, Col::Accent));
    uiLayout->addWidget(m_uiRequirementsEdit);

    // Progress bar + status label
    m_uiProgressBar = new QProgressBar(uiBox);
    m_uiProgressBar->setRange(0, 0);   // indeterminate
    m_uiProgressBar->setFixedHeight(4);
    m_uiProgressBar->setTextVisible(false);
    m_uiProgressBar->hide();
    m_uiProgressBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: %1; border: none; border-radius: 2px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 %2,stop:1 %3); border-radius: 2px; }"
    ).arg(Col::BgCard, Col::Accent, Col::AccentCyan));
    uiLayout->addWidget(m_uiProgressBar);

    m_uiStatusLabel = new QLabel(uiBox);
    m_uiStatusLabel->setWordWrap(true);
    m_uiStatusLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; }").arg(Col::TextMuted));
    m_uiStatusLabel->hide();
    uiLayout->addWidget(m_uiStatusLabel);

    // Generate button
    m_uiGenerateButton = makeActionBtn(uiBox,
        QStringLiteral("Generate && Implement UI"),
        QString(), QStringLiteral("Analyze the design, generate code, create git branch, and commit locally"));
    m_uiGenerateButton->setText(QStringLiteral("🚀  Generate && Implement UI"));
    m_uiGenerateButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %1,stop:1 %2); "
        "              color: white; border: none; font-weight: 700; font-size: 14px; "
        "              padding: 12px 24px; border-radius: 10px; } "
        "QPushButton:hover { background: %2; } "
        "QPushButton:disabled { background: %3; color: %4; }"
    ).arg(Col::Accent, Col::AccentGlow, Col::BgCard, Col::TextMuted));
    uiLayout->addWidget(m_uiGenerateButton, 0, Qt::AlignLeft);

    layout->addWidget(uiBox);

    // ── ③ File Organization ─────────────────────────────────
    auto *organizeBox = new QGroupBox(QStringLiteral("📁  File Organization"), content);
    auto *organizeLayout = new QVBoxLayout(organizeBox);
    organizeLayout->setSpacing(12);

    auto *organizeDesc = new QLabel(
        QStringLiteral("Scan a directory for duplicate files (SHA-256) and get folder structure suggestions."),
        organizeBox);
    organizeDesc->setWordWrap(true);
    organizeDesc->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; }").arg(Col::TextSecondary));
    organizeLayout->addWidget(organizeDesc);

    m_organizeButton = makeActionBtn(organizeBox, QStringLiteral("Organize && Find Duplicates"),
                                     QStringLiteral("organize"),
                                     QStringLiteral("Scan project for duplicate files and folder structure suggestions"));
    organizeLayout->addWidget(m_organizeButton, 0, Qt::AlignLeft);
    layout->addWidget(organizeBox);

    // ── ④ Disk Cleanup ──────────────────────────────────────
    auto *cleanupBox = new QGroupBox(QStringLiteral("🧹  Disk Cleanup"), content);
    auto *cleanupLayout = new QVBoxLayout(cleanupBox);
    cleanupLayout->setSpacing(12);

    auto *cleanupDesc = new QLabel(
        QStringLiteral("Monitor disk usage per mount point and get suggestions for safe cleanup "
                       "actions (package cache, user cache, trash, journal logs, orphan packages)."),
        cleanupBox);
    cleanupDesc->setWordWrap(true);
    cleanupDesc->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; }").arg(Col::TextSecondary));
    cleanupLayout->addWidget(cleanupDesc);

    m_diskCleanupButton = makeActionBtn(cleanupBox, QStringLiteral("Analyze Disk Usage"),
                                        QStringLiteral("cleanup"),
                                        QStringLiteral("Analyze disk usage and get safe cleanup suggestions"));
    cleanupLayout->addWidget(m_diskCleanupButton, 0, Qt::AlignLeft);
    layout->addWidget(cleanupBox);

    // ── ⑤ Update Checker ──────────────────────────────────────
    auto *updatesBox    = new QGroupBox(QStringLiteral("🔄  Update Checker"), content);
    auto *updatesLayout = new QVBoxLayout(updatesBox);
    updatesLayout->setSpacing(10);

    // Header row: description + count badge
    auto *updHeaderRow = new QHBoxLayout;
    auto *updatesDesc  = new QLabel(
        QStringLiteral("Track installed package versions and get notified when updates are available. "
                       "One-click apply — opens a terminal window for safe, supervised installation."),
        updatesBox);
    updatesDesc->setWordWrap(true);
    updatesDesc->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; }").arg(Col::TextSecondary));
    updHeaderRow->addWidget(updatesDesc, 1);

    m_updateCountBadge = new QLabel(updatesBox);
    m_updateCountBadge->setFixedSize(28, 28);
    m_updateCountBadge->setAlignment(Qt::AlignCenter);
    m_updateCountBadge->setVisible(false);
    m_updateCountBadge->setStyleSheet(QStringLiteral(
        "QLabel { background: #ef4444; color: white; border-radius: 14px; "
        "         font-weight: 700; font-size: 11px; }"));
    updHeaderRow->addWidget(m_updateCountBadge);
    updatesLayout->addLayout(updHeaderRow);

    // Progress bar (hidden when idle, pulsing during check)
    m_updateProgressBar = new QProgressBar(updatesBox);
    m_updateProgressBar->setRange(0, 0);  // indeterminate
    m_updateProgressBar->setFixedHeight(6);
    m_updateProgressBar->setTextVisible(false);
    m_updateProgressBar->setVisible(false);
    m_updateProgressBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: %1; border-radius: 3px; border: none; }"
        "QProgressBar::chunk { background: %2; border-radius: 3px; }"
    ).arg(Col::BgCard, Col::Accent));
    updatesLayout->addWidget(m_updateProgressBar);

    // Status label (last-checked + update count)
    m_updateStatusLabel = new QLabel(QStringLiteral("Not checked yet."), updatesBox);
    m_updateStatusLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 11px;").arg(Col::TextMuted));
    updatesLayout->addWidget(m_updateStatusLabel);

    // Action buttons row: Check | Apply Repo | Apply All | interval spinner
    auto *updBtnRow = new QHBoxLayout;
    updBtnRow->setSpacing(8);

    m_checkUpdatesButton = makeActionBtn(updatesBox, QStringLiteral("🔍 Check Now"),
                                         QStringLiteral("update"),
                                         QStringLiteral("Compare installed package versions with repositories"));
    updBtnRow->addWidget(m_checkUpdatesButton);

    m_applyRepoButton = new QPushButton(QStringLiteral("⬆ Apply Repo Updates"), updatesBox);
    m_applyRepoButton->setCursor(Qt::PointingHandCursor);
    m_applyRepoButton->setEnabled(false);
    m_applyRepoButton->setToolTip(QStringLiteral("Run: sudo pacman -Syu in a terminal window"));
    m_applyRepoButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 6px; "
        "              color: %3; padding: 8px 14px; font-weight: 600; }"
        "QPushButton:hover:enabled { background: %4; color: white; border-color: %4; }"
        "QPushButton:disabled { color: %5; border-color: %6; }"
    ).arg(Col::BgCard, Col::Accent, Col::Accent, Col::Accent, Col::TextMuted, Col::Border));
    updBtnRow->addWidget(m_applyRepoButton);

    m_applyAllButton = new QPushButton(QStringLiteral("🚀 Apply All (+ AUR)"), updatesBox);
    m_applyAllButton->setCursor(Qt::PointingHandCursor);
    m_applyAllButton->setEnabled(false);
    m_applyAllButton->setVisible(false);  // shown only when AUR helper exists
    m_applyAllButton->setToolTip(QStringLiteral("Run: paru/yay -Syu (repo + AUR) in a terminal window"));
    m_applyAllButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 6px; "
        "              color: %3; padding: 8px 14px; font-weight: 600; }"
        "QPushButton:hover:enabled { background: %4; color: white; border-color: %4; }"
        "QPushButton:disabled { color: %5; border-color: %6; }"
    ).arg(Col::BgCard, Col::AccentGlow, Col::AccentGlow, Col::AccentGlow, Col::TextMuted, Col::Border));
    updBtnRow->addWidget(m_applyAllButton);

    updBtnRow->addStretch(1);

    // Auto-check interval spinner
    auto *intervalLabel = new QLabel(QStringLiteral("Auto-check every"), updatesBox);
    intervalLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(Col::TextMuted));
    m_updateIntervalSpin = new QSpinBox(updatesBox);
    m_updateIntervalSpin->setRange(5, 180);
    m_updateIntervalSpin->setValue(30);
    m_updateIntervalSpin->setSuffix(QStringLiteral(" min"));
    m_updateIntervalSpin->setToolTip(QStringLiteral("Background check interval (minutes)"));
    m_updateIntervalSpin->setStyleSheet(QStringLiteral(
        "QSpinBox { background: %1; border: 1px solid %2; border-radius: 6px; "
        "           color: %3; padding: 4px 8px; }"
    ).arg(Col::BgCard, Col::Border, Col::TextPrimary));
    updBtnRow->addWidget(intervalLabel);
    updBtnRow->addWidget(m_updateIntervalSpin);
    updatesLayout->addLayout(updBtnRow);

    // Live output log (terminal-style, hidden until apply starts)
    m_updateOutputLog = new QPlainTextEdit(updatesBox);
    m_updateOutputLog->setReadOnly(true);
    m_updateOutputLog->setFixedHeight(130);
    m_updateOutputLog->setVisible(false);
    m_updateOutputLog->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #050810; color: #a3e635; border: 1px solid %1; "
        "                 border-radius: 6px; font-family: monospace; font-size: 11px; padding: 6px; }"
    ).arg(Col::Border));
    updatesLayout->addWidget(m_updateOutputLog);

    layout->addWidget(updatesBox);

    layout->addStretch(1);
    outerLayout->addWidget(scrollArea, 1);

    // ── Connect new UI Developer buttons ────────────────────
    connect(m_uiDesignPickBtn,  &QPushButton::clicked, this, &MainWindow::onUiDesignImageClicked);
    connect(m_uiDesignClearBtn, &QPushButton::clicked, this, &MainWindow::onClearUiDesignImage);
    connect(m_uiGenerateButton, &QPushButton::clicked, this, &MainWindow::onGenerateUiClicked);

    return outer;
}


// ─────────────────────────────────────────────────────────────
//  Settings Page
// ─────────────────────────────────────────────────────────────
QWidget *MainWindow::createSettingsPage()
{
    auto *outer = new QWidget(this);
    auto *outerLayout = new QVBoxLayout(outer);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // ── Header bar ──────────────────────────────────────────
    auto *headerBar = new QWidget(outer);
    headerBar->setFixedHeight(56);
    headerBar->setStyleSheet(QStringLiteral(
        "QWidget { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %1,stop:1 %2); "
        "border-bottom: 1px solid %3; }").arg(Col::BgCard, Col::BgDeep, Col::Border));
    auto *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(20, 0, 20, 0);
    auto *headerTitle = new QLabel(QStringLiteral("⚙  Settings"), headerBar);
    headerTitle->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 17px; font-weight: 700; border: none; background: transparent; }"
    ).arg(Col::TextPrimary));
    headerLayout->addWidget(headerTitle);
    headerLayout->addStretch(1);
    auto *headerSub = new QLabel(QStringLiteral("Application · Development · Updates"), headerBar);
    headerSub->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; border: none; background: transparent; }"
    ).arg(Col::TextMuted));
    headerLayout->addWidget(headerSub);
    outerLayout->addWidget(headerBar);

    // ── Scroll area ─────────────────────────────────────────
    auto *scrollArea = new QScrollArea(outer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: %1; border: none; }").arg(Col::BgDeep));
    auto *content = new QWidget(scrollArea);
    content->setStyleSheet(QStringLiteral("background: %1;").arg(Col::BgDeep));
    scrollArea->setWidget(content);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 24, 28, 32);
    layout->setSpacing(16);

    auto applyGroupStyle = [](QGroupBox *box) {
        box->setStyleSheet(QStringLiteral(
            "QGroupBox { background: %1; border: 1px solid %2; border-radius: 10px; "
            "            margin-top: 18px; padding: 12px; } "
            "QGroupBox::title { color: %3; subcontrol-origin: margin; left: 14px; "
            "                    font-weight: 700; font-size: 13px; }"
        ).arg(Col::BgCard, Col::Border, Col::TextPrimary));
    };
    auto applyEditStyle = [](QLineEdit *edit) {
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit { background: %1; border: 1px solid %2; border-radius: 6px; "
            "            color: %3; padding: 8px 10px; }"
        ).arg(Col::BgInput, Col::Border, Col::TextPrimary));
    };

    // ── Application ─────────────────────────────────────────
    auto *appBox = new QGroupBox(QStringLiteral("Application"), content);
    applyGroupStyle(appBox);
    auto *appLayout = new QVBoxLayout(appBox);
    appLayout->setSpacing(10);

    auto *modelRow = new QHBoxLayout;
    auto *modelLbl = new QLabel(QStringLiteral("Active AI model:"), appBox);
    modelLbl->setFixedWidth(140);
    modelLbl->setStyleSheet(QStringLiteral("color: %1;").arg(Col::TextSecondary));
    m_settingsModelLabel = new QLabel(m_agent.currentModel(), appBox);
    m_settingsModelLabel->setStyleSheet(QStringLiteral(
        "background: %1; border: 1px solid %2; border-radius: 6px; color: %3; "
        "padding: 6px 12px; font-weight: 600;").arg(Col::BgCard, Col::Border, Col::AccentGlow));
    auto *settingsModelBtn = new QPushButton(QStringLiteral("⚙ Switch Model..."), appBox);
    settingsModelBtn->setCursor(Qt::PointingHandCursor);
    settingsModelBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 6px; "
        "              color: %3; padding: 6px 12px; font-weight: 600; }"
        "QPushButton:hover { background: %4; color: white; border-color: %4; }"
    ).arg(Col::BgCard, Col::Border, Col::Accent, Col::Accent));
    modelRow->addWidget(modelLbl);
    modelRow->addWidget(m_settingsModelLabel, 1);
    modelRow->addWidget(settingsModelBtn);
    appLayout->addLayout(modelRow);
    connect(settingsModelBtn, &QPushButton::clicked, this, &MainWindow::onManageModels);

    // ── Development ─────────────────────────────────────────
    auto *devBox = new QGroupBox(QStringLiteral("Development"), content);
    applyGroupStyle(devBox);
    auto *devLayout = new QVBoxLayout(devBox);
    devLayout->setSpacing(10);

    auto *projectRow = new QHBoxLayout;
    auto *projectLbl = new QLabel(QStringLiteral("Project directory:"), devBox);
    projectLbl->setFixedWidth(140);
    projectLbl->setStyleSheet(QStringLiteral("color: %1;").arg(Col::TextSecondary));
    m_settingsProjectEdit = new QLineEdit(m_projectDirectory, devBox);
    m_settingsProjectEdit->setPlaceholderText(QStringLiteral("e.g. /home/you/my-project"));
    applyEditStyle(m_settingsProjectEdit);
    auto *settingsBrowseBtn = new QPushButton(QStringLiteral("Browse…"), devBox);
    settingsBrowseBtn->setCursor(Qt::PointingHandCursor);
    settingsBrowseBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 6px; color: %3; padding: 6px 12px; }"
        "QPushButton:hover { background: %4; }").arg(Col::BgCard, Col::Border, Col::TextSecondary, Col::Accent));
    projectRow->addWidget(projectLbl);
    projectRow->addWidget(m_settingsProjectEdit, 1);
    projectRow->addWidget(settingsBrowseBtn);
    devLayout->addLayout(projectRow);
    connect(settingsBrowseBtn, &QPushButton::clicked, this, &MainWindow::onSettingsBrowseProject);

    auto *buildRow = new QHBoxLayout;
    auto *buildLbl = new QLabel(QStringLiteral("Build command:"), devBox);
    buildLbl->setFixedWidth(140);
    buildLbl->setStyleSheet(QStringLiteral("color: %1;").arg(Col::TextSecondary));
    m_settingsBuildEdit = new QLineEdit(m_buildCommand, devBox);
    m_settingsBuildEdit->setPlaceholderText(QStringLiteral("cmake --build build   |   npm run build"));
    applyEditStyle(m_settingsBuildEdit);
    buildRow->addWidget(buildLbl);
    buildRow->addWidget(m_settingsBuildEdit, 1);
    devLayout->addLayout(buildRow);

    m_settingsAutoFixCheck = new QCheckBox(
        QStringLiteral("Enable Auto-Fix for build errors"), devBox);
    m_settingsAutoFixCheck->setStyleSheet(QStringLiteral(
        "QCheckBox { color: %1; } QCheckBox::indicator { width: 16px; height: 16px; }"
    ).arg(Col::TextSecondary));
    m_settingsAutoFixCheck->setChecked(m_agent.autoFixEnabled());
    devLayout->addWidget(m_settingsAutoFixCheck);

    // ── Updates ─────────────────────────────────────────────
    auto *updatesBox = new QGroupBox(QStringLiteral("Updates"), content);
    applyGroupStyle(updatesBox);
    auto *updatesLayout = new QVBoxLayout(updatesBox);
    updatesLayout->setSpacing(10);

    auto *intervalRow = new QHBoxLayout;
    auto *intervalLbl = new QLabel(QStringLiteral("Background update check:"), updatesBox);
    intervalLbl->setStyleSheet(QStringLiteral("color: %1;").arg(Col::TextSecondary));
    m_settingsIntervalSpin = new QSpinBox(updatesBox);
    m_settingsIntervalSpin->setRange(5, 180);
    m_settingsIntervalSpin->setValue(m_updateIntervalSpin ? m_updateIntervalSpin->value() : 30);
    m_settingsIntervalSpin->setSuffix(QStringLiteral(" min"));
    m_settingsIntervalSpin->setStyleSheet(QStringLiteral(
        "QSpinBox { background: %1; border: 1px solid %2; border-radius: 6px; color: %3; padding: 6px 8px; }"
    ).arg(Col::BgInput, Col::Border, Col::TextPrimary));
    intervalRow->addWidget(intervalLbl);
    intervalRow->addWidget(m_settingsIntervalSpin);
    intervalRow->addStretch(1);
    updatesLayout->addLayout(intervalRow);

    // ── Voice & Calendar launchers ──────────────────────────
    auto *accessBox = new QGroupBox(QStringLiteral("Voice & Calendar"), content);
    applyGroupStyle(accessBox);
    auto *accessLayout = new QHBoxLayout(accessBox);
    accessLayout->setSpacing(12);
    auto *voiceBtn = new QPushButton(QStringLiteral("🎙  Voice Settings"), accessBox);
    auto *calBtn = new QPushButton(QStringLiteral("📅  Calendar Settings"), accessBox);
    for (QPushButton *btn : {voiceBtn, calBtn}) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: white; border: none; border-radius: 8px; "
            "padding: 10px 18px; font-weight: 600; }"
            "QPushButton:hover { background: %2; }").arg(Col::Accent, Col::AccentGlow));
    }
    accessLayout->addWidget(voiceBtn);
    accessLayout->addWidget(calBtn);
    accessLayout->addStretch(1);
    connect(voiceBtn, &QPushButton::clicked, this, &MainWindow::onVoiceSettings);
    connect(calBtn, &QPushButton::clicked, this, &MainWindow::onOpenCalendarSettings);

    layout->addWidget(appBox);
    layout->addWidget(devBox);
    layout->addWidget(updatesBox);
    layout->addWidget(accessBox);

    // ── Save bar ────────────────────────────────────────────
    m_settingsStatusLabel = new QLabel(content);
    m_settingsStatusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(Col::TextMuted));
    auto *saveBtn = new QPushButton(QStringLiteral("💾 Save Settings"), content);
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: white; border: none; border-radius: 8px; "
        "padding: 10px 24px; font-weight: 700; font-size: 14px; }"
        "QPushButton:hover { background: %2; }").arg(Col::Accent, Col::AccentGlow));
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveSettings);
    layout->addWidget(m_settingsStatusLabel);
    layout->addStretch(1);
    layout->addWidget(saveBtn, 0, Qt::AlignRight);

    outerLayout->addWidget(scrollArea, 1);
    return outer;
}


// ─────────────────────────────────────────────────────────────
//  Shared Input Card (glassmorphism prompt bar)
// ─────────────────────────────────────────────────────────────
QWidget *MainWindow::createInputCard()
{
    m_inputCard = new QWidget(this);
    m_inputCard->setStyleSheet(QStringLiteral(
        "QWidget#inputCard { "
        "  background: %1; border: 1px solid %2; border-radius: 16px; "
        "  padding: 0px; "
        "}"
    ).arg(Col::BgCard, Col::Border));
    m_inputCard->setObjectName(QStringLiteral("inputCard"));

    auto *cardLayout = new QVBoxLayout(m_inputCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(6);

    // Pending image row
    m_pendingImageLabel = new QLabel(m_inputCard);
    m_pendingImageLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_pendingImageLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; border: none; }").arg(Col::AccentGlow));
    m_pendingImageLabel->setTextFormat(Qt::RichText);
    m_pendingImageLabel->setVisible(false);

    m_clearImageButton = new QPushButton(QStringLiteral("✕"), m_inputCard);
    m_clearImageButton->setFixedSize(24, 24);
    m_clearImageButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; font-size: 14px; padding: 0; "
        "min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; } "
        "QPushButton:hover { color: %2; }"
    ).arg(Col::TextMuted, Col::Danger));
    m_clearImageButton->setVisible(false);

    auto *pendingRow = new QHBoxLayout;
    pendingRow->setContentsMargins(0, 0, 0, 0);
    pendingRow->addWidget(m_pendingImageLabel);
    pendingRow->addStretch(1);
    pendingRow->addWidget(m_clearImageButton);
    cardLayout->addLayout(pendingRow);

    // Main input row
    auto *inputRow = new QHBoxLayout;
    inputRow->setSpacing(6);

    m_input = new QLineEdit(m_inputCard);
    m_input->setPlaceholderText(QStringLiteral("Ask me anything ..."));
    m_input->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; color: %1; font-size: 14px; padding: 8px 4px; }"
    ).arg(Col::TextPrimary));
    m_input->setMinimumHeight(36);
    inputRow->addWidget(m_input, 1);

    cardLayout->addLayout(inputRow);

    // Tools row
    auto *toolsRow = new QHBoxLayout;
    toolsRow->setSpacing(4);

    auto makeToolBtn = [&](const QString &iconName, const QString &tooltip) {
        auto *btn = new QPushButton(m_inputCard);
        btn->setIcon(createVectorIcon(iconName, 18));
        btn->setIconSize(QSize(18, 18));
        btn->setToolTip(tooltip);
        btn->setFixedSize(34, 34);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: none; border-radius: 8px; "
            "min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px; padding: 0; } "
            "QPushButton:hover { background: rgba(99,102,241,0.15); }"
        ));
        return btn;
    };

    m_cameraButton = makeToolBtn(QStringLiteral("camera"), QStringLiteral("Open camera to capture an image"));
    m_imageButton = makeToolBtn(QStringLiteral("image"), QStringLiteral("Choose an image file"));
    m_voiceButton = new QPushButton(m_inputCard);
    m_voiceButton->setIcon(createVectorIcon(QStringLiteral("mic"), 18));
    m_voiceButton->setIconSize(QSize(18, 18));
    m_voiceButton->setToolTip(QStringLiteral("Voice input (push-to-talk)"));
    m_voiceButton->setFixedSize(34, 34);
    m_voiceButton->setCheckable(true);
    m_voiceButton->setCursor(Qt::PointingHandCursor);
    m_voiceButton->setFocusPolicy(Qt::NoFocus);
    m_voiceButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 8px; "
        "min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px; padding: 0; } "
        "QPushButton:hover { background: rgba(99,102,241,0.15); } "
        "QPushButton:checked { background: rgba(99,102,241,0.3); }"
    ));
    m_voiceButton->setEnabled(false);

    toolsRow->addWidget(m_cameraButton);
    toolsRow->addWidget(m_imageButton);
    toolsRow->addWidget(m_voiceButton);
    toolsRow->addStretch(1);

    // Send button
    m_sendButton = new QPushButton(m_inputCard);
    m_sendButton->setIcon(createVectorIcon(QStringLiteral("send"), 18));
    m_sendButton->setIconSize(QSize(18, 18));
    m_sendButton->setFixedSize(38, 38);
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setFocusPolicy(Qt::NoFocus);
    m_sendButton->setStyleSheet(QStringLiteral(
        "QPushButton { "
        "  background: %1; border: none; border-radius: 19px; "
        "  min-width: 38px; max-width: 38px; min-height: 38px; max-height: 38px; padding: 0; "
        "} "
        "QPushButton:hover { background: %2; } "
        "QPushButton:disabled { background: %3; }"
    ).arg(Col::Accent, Col::AccentGlow, Col::Border));
    toolsRow->addWidget(m_sendButton);

    cardLayout->addLayout(toolsRow);

    return m_inputCard;
}

// ─────────────────────────────────────────────────────────────
//  Navigation
// ─────────────────────────────────────────────────────────────
void MainWindow::navigateTo(int pageIndex)
{
    if (pageIndex == m_currentPage && pageIndex < 3) {
        return;
    }

    // Uncheck old nav button
    QList<QPushButton *> navBtns = {m_navHome, m_navChat, m_navDev};
    for (int i = 0; i < navBtns.size(); ++i) {
        const QSignalBlocker blocker(navBtns[i]);
        navBtns[i]->setChecked(i == pageIndex);
    }

    m_currentPage = pageIndex;
    m_pageStack->setCurrentIndex(pageIndex);

    // Move input card to the active page's slot
    if (pageIndex == 0) {
        reparentInputCard(m_welcomeInputSlot);
    } else if (pageIndex == 1) {
        reparentInputCard(m_chatInputSlot);
    }
    // Dev hub page doesn't use the input card

    if (pageIndex == 1 && m_modelReady) {
        m_input->setFocus();
    }
}

void MainWindow::reparentInputCard(QVBoxLayout *targetLayout)
{
    if (!m_inputCard || !targetLayout) {
        return;
    }

    // Add to the new layout — Qt handles reparenting automatically
    if (targetLayout->indexOf(m_inputCard) == -1) {
        targetLayout->addWidget(m_inputCard);
    }
}

// ─────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("TitanAI"));
    resize(980, 680);
    setMinimumSize(780, 520);

    setupGlobalStylesheet();

    // --- Central layout: sidebar | page stack ---
    auto *central = new QWidget(this);
    auto *mainHLayout = new QHBoxLayout(central);
    mainHLayout->setContentsMargins(0, 0, 0, 0);
    mainHLayout->setSpacing(0);

    // Sidebar
    mainHLayout->addWidget(createSidebar());

    // Separator line
    auto *separator = new QWidget(central);
    separator->setFixedWidth(1);
    separator->setStyleSheet(QStringLiteral("background: %1;").arg(Col::Border));
    mainHLayout->addWidget(separator);

    // Page stack
    m_pageStack = new QStackedWidget(central);
    m_pageStack->addWidget(createWelcomePage());   // 0
    m_pageStack->addWidget(createChatPage());       // 1
    m_pageStack->addWidget(createDevHubPage());     // 2
    m_pageStack->addWidget(createSettingsPage());   // 3
    mainHLayout->addWidget(m_pageStack, 1);

    setCentralWidget(central);

    // Create shared input card and initially place on welcome page
    createInputCard();
    reparentInputCard(m_welcomeInputSlot);

    // --- Hidden helper widgets for compatibility ---
    m_voiceSettingsButton = new QPushButton(this);
    m_voiceSettingsButton->setVisible(false);
    m_calendarButton = new QPushButton(this);
    m_calendarButton->setVisible(false);

    // ═══════════════════════════════════════════════════════
    //  Connections  (preserved from original, adapted)
    // ═══════════════════════════════════════════════════════

    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_input, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    connect(&m_agent, &Agent::responseChunkReceived, this, &MainWindow::onResponseChunk);
    connect(&m_agent, &Agent::responseReceived, this, &MainWindow::onResponseReceived);
    connect(&m_agent, &Agent::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(&m_agent, &Agent::installRequested, this, &MainWindow::onInstallRequested);
    connect(&m_agent, &Agent::toolOutputReceived, this, &MainWindow::onToolOutput);
    connect(&m_agent, &Agent::modelStatusChanged, this,
            [this](OllamaManager::Status, const QString &message) {
                m_statusLabel->setText(message);
            });
    connect(&m_agent, &Agent::modelReady, this, &MainWindow::onModelReady);
    connect(&m_agent, &Agent::modelError, this, &MainWindow::onModelError);
    connect(&m_agent, &Agent::modelsChanged, this, &MainWindow::onModelsChanged);
    connect(&m_agent, &Agent::modelNegotiation, this, &MainWindow::onModelNegotiation);

    // Restore settings
    m_projectDirectory = m_settings.value(QStringLiteral("projectDir")).toString();
    m_buildCommand = m_settings.value(QStringLiteral("buildCommand")).toString();
    const bool autoFixEnabled = m_settings.value(QStringLiteral("autoFixEnabled"), false).toBool();
    const int updateInterval = m_settings.value(QStringLiteral("updateInterval"), 30).toInt();

    m_autoFixCheck->setChecked(autoFixEnabled);
    m_projectEdit->setText(m_projectDirectory);
    m_buildEdit->setText(m_buildCommand);
    m_agent.setAutoFixEnabled(autoFixEnabled);
    m_agent.setProjectDirectory(m_projectDirectory);
    m_agent.setBuildCommand(m_buildCommand);

    // Keep the Settings page in sync with the restored/Dev Hub state.
    if (m_updateIntervalSpin) {
        m_updateIntervalSpin->setValue(updateInterval);
    }
    if (m_settingsProjectEdit) {
        m_settingsProjectEdit->setText(m_projectDirectory);
    }
    if (m_settingsBuildEdit) {
        m_settingsBuildEdit->setText(m_buildCommand);
    }
    if (m_settingsAutoFixCheck) {
        m_settingsAutoFixCheck->setChecked(autoFixEnabled);
    }
    if (m_settingsIntervalSpin) {
        m_settingsIntervalSpin->setValue(updateInterval);
    }
    if (m_settingsModelLabel) {
        m_settingsModelLabel->setText(m_agent.currentModel());
    }

    connect(m_autoFixCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.setValue(QStringLiteral("autoFixEnabled"), checked);
        m_agent.setAutoFixEnabled(checked);
    });
    connect(m_projectEdit, &QLineEdit::editingFinished, this, [this]() {
        m_projectDirectory = m_projectEdit->text().trimmed();
        m_settings.setValue(QStringLiteral("projectDir"), m_projectDirectory);
        m_agent.setProjectDirectory(m_projectDirectory);
    });
    connect(m_buildEdit, &QLineEdit::editingFinished, this, [this]() {
        m_buildCommand = m_buildEdit->text().trimmed();
        m_settings.setValue(QStringLiteral("buildCommand"), m_buildCommand);
        m_agent.setBuildCommand(m_buildCommand);
    });
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseProject);
    connect(m_buildFixButton, &QPushButton::clicked, this, &MainWindow::onBuildAndFixClicked);
    connect(m_organizeButton, &QPushButton::clicked, this, &MainWindow::onOrganizeClicked);

    // File organizer
    FileOrganizer &organizer = m_agent.fileOrganizer();
    connect(&organizer, &FileOrganizer::scanStarted, this,
            [this](const QString &rootPath) {
                m_organizeButton->setEnabled(false);
                navigateTo(1); // Switch to chat to show results
                appendPlainLine(
                    QStringLiteral("Scanning '%1' for duplicates and organization ideas...")
                        .arg(rootPath),
                    Col::TextMuted);
            });
    connect(&organizer, &FileOrganizer::scanFinished, this,
            [this](int, int, quint64) {
                m_organizeButton->setEnabled(true);
                appendMessage(QStringLiteral("TitanAI"),
                              m_agent.fileOrganizer().formatFullReport(),
                              Col::AccentGlow);
            });
    connect(&organizer, &FileOrganizer::scanError, this,
            [this](const QString &error) {
                m_organizeButton->setEnabled(true);
                appendMessage(QStringLiteral("Error"), error, Col::Danger);
            });
    connect(m_diskCleanupButton, &QPushButton::clicked, this, &MainWindow::onDiskCleanupClicked);

    // Disk cleanup
    DiskCleanup &diskCleanup = m_agent.diskCleanup();
    connect(&diskCleanup, &DiskCleanup::analysisStarted, this,
            [this]() {
                m_diskCleanupButton->setEnabled(false);
                navigateTo(1); // Switch to chat to show results
                appendPlainLine(
                    QStringLiteral("Analyzing disk usage and looking for cleanup "
                                   "opportunities..."),
                    Col::TextMuted);
            });
    connect(&diskCleanup, &DiskCleanup::analysisFinished, this,
            [this](quint64) {
                m_diskCleanupButton->setEnabled(true);
                appendMessage(QStringLiteral("TitanAI"),
                              m_agent.diskCleanup().formatFullReport(),
                              Col::AccentGlow);
            });
    connect(&diskCleanup, &DiskCleanup::analysisError, this,
            [this](const QString &error) {
                m_diskCleanupButton->setEnabled(true);
                appendMessage(QStringLiteral("Error"), error, Col::Danger);
            });
    connect(m_checkUpdatesButton, &QPushButton::clicked,   this, &MainWindow::onCheckUpdatesClicked);
    connect(m_applyRepoButton,    &QPushButton::clicked,   this, &MainWindow::onApplyRepoUpdatesClicked);
    connect(m_applyAllButton,     &QPushButton::clicked,   this, &MainWindow::onApplyAllUpdatesClicked);

    // Update checker signals
    UpdateChecker &updateChecker = m_agent.updateChecker();
    connect(&updateChecker, &UpdateChecker::checkStarted,  this, [this]() {
        m_checkUpdatesButton->setEnabled(false);
        m_applyRepoButton->setEnabled(false);
        m_applyAllButton->setEnabled(false);
        m_updateProgressBar->setVisible(true);
        m_updateStatusLabel->setText(QStringLiteral("Checking..."));
        m_updateCountBadge->setVisible(false);
    });
    connect(&updateChecker, &UpdateChecker::checkProgress, this, &MainWindow::onUpdateCheckProgress);
    connect(&updateChecker, &UpdateChecker::checkFinished, this, &MainWindow::onUpdateCheckFinished);
    connect(&updateChecker, &UpdateChecker::checkError,    this, &MainWindow::onUpdateCheckError);
    connect(&updateChecker, &UpdateChecker::updatesApplyStarted, this, &MainWindow::onUpdatesApplyStarted);
    connect(&updateChecker, &UpdateChecker::updatesApplyOutput,  this, &MainWindow::onUpdatesApplyOutput);
    connect(&updateChecker, &UpdateChecker::updatesApplyFinished,this, &MainWindow::onUpdatesApplyFinished);
    connect(&updateChecker, &UpdateChecker::periodicCheckDone,   this, &MainWindow::onPeriodicUpdateCheckDone);

    // Interval spinner changes restart periodic timer
    connect(m_updateIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int minutes) {
                m_agent.updateChecker().startPeriodicCheck(minutes);
            });

    // Setup system tray icon for popup notifications
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(this);
        m_trayIcon->setIcon(QIcon::fromTheme(QStringLiteral("system-software-update"),
                                             QIcon(QStringLiteral(":/icons/logo.png"))));
        m_trayIcon->setToolTip(QStringLiteral("TitanAI"));
        m_trayIcon->show();
        connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, [this]() {
            activateWindow();
            raise();
            navigateTo(2); // jump to Dev Hub
        });
    }
    connect(&m_agent, &Agent::autoFixEnabledChanged, this, [this](bool enabled) {
        m_settings.setValue(QStringLiteral("autoFixEnabled"), enabled);
        const QSignalBlocker blocker(m_autoFixCheck);
        m_autoFixCheck->setChecked(enabled);
    });
    connect(&m_agent, &Agent::codeFixStatus, this, [this](const QString &message) {
        navigateTo(1);
        appendMessage(QStringLiteral("TitanAI"), message, Col::AccentGlow);
    });
    connect(&m_agent, &Agent::codeFixFinished, this,
            [this](const QString &summary, bool success) {
                appendMessage(success ? QStringLiteral("TitanAI") : QStringLiteral("Error"),
                              summary,
                              success ? QLatin1String(Col::AccentGlow) : QLatin1String(Col::Danger));
                setInputEnabled(true);
            });

    connect(&m_agent, &Agent::startupSuggestionsReady, this, &MainWindow::onStartupSuggestions);
    connect(&m_agent, &Agent::calendarEventsReady, this, &MainWindow::onCalendarEventsReady);
    connect(&m_agent, &Agent::calendarNotificationAlert, this, &MainWindow::onCalendarNotificationAlert);

    // UI Developer connections
    connect(&m_agent, &Agent::uiDevelopmentProgress, this, &MainWindow::onUiDevelopmentProgress);
    connect(&m_agent, &Agent::uiDevelopmentFinished, this, &MainWindow::onUiDevelopmentFinished);

    // Voice connections
    connect(m_voiceButton, &QPushButton::toggled, this, &MainWindow::onVoiceButtonToggled);
    connect(m_cameraButton, &QPushButton::clicked, this, &MainWindow::onCaptureFromCamera);
    connect(m_imageButton, &QPushButton::clicked, this, &MainWindow::onSelectImage);
    connect(m_clearImageButton, &QPushButton::clicked, this, &MainWindow::onClearPendingImage);
    connect(&m_agent, &Agent::cameraRequested, this, &MainWindow::onCaptureFromCamera);

    connect(&m_voiceEngine, &VoiceEngine::listeningChanged, this, [this](bool listening) {
        const QSignalBlocker blocker(m_voiceButton);
        m_voiceButton->setChecked(listening);
        m_micLevelBar->setVisible(listening);
        if (!listening) {
            m_voiceStatusLabel->clear();
        }
    });
    connect(&m_voiceEngine, &VoiceEngine::partialTranscript, this, &MainWindow::onVoicePartial);
    connect(&m_voiceEngine, &VoiceEngine::finalTranscript, this, &MainWindow::onVoiceFinal);
    connect(&m_voiceEngine, &VoiceEngine::wakeWordDetected, this, [this]() {
        m_voiceStatusLabel->setText(QStringLiteral("Wake word detected - listening..."));
        m_micLevelBar->setVisible(true);
    });
    connect(&m_voiceEngine, &VoiceEngine::speakingChanged, this, [this](bool speaking) {
        m_voiceStatusLabel->setText(speaking ? QStringLiteral("Speaking...") : QString());
    });
    connect(&m_voiceEngine, &VoiceEngine::micLevelChanged, this,
            [this](float level) { m_micLevelBar->setValue(qRound(level * 100.0f)); });
    connect(&m_voiceEngine, &VoiceEngine::errorOccurred, this, &MainWindow::onVoiceError);
    connect(&m_voiceEngine, &VoiceEngine::sttStatusChanged, this,
            [this](const QString &message) { m_voiceStatusLabel->setText(message); });

    m_voiceEngine.setConfig(loadVoiceSettings());
    updateVoiceUi();

    // Welcome message in chat
    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Welcome! Loading the local AI model. You can start chatting once "
                                 "it is ready."),
                  Col::AccentGlow);

    setInputEnabled(false);
    m_agent.initializeModel(Agent::kDefaultModel);
}

// ─────────────────────────────────────────────────────────────
//  Slots (mostly preserved, adapted colours)
// ─────────────────────────────────────────────────────────────
void MainWindow::onSendClicked()
{
    if (!m_modelReady) {
        return;
    }

    QString text = m_input->text().trimmed();
    if (text.isEmpty() && m_pendingImage.isNull()) {
        return;
    }

    // Switch to chat page if on welcome
    if (m_currentPage != 1) {
        navigateTo(1);
    }

    m_input->clear();
    appendMessage(QStringLiteral("You"), text, Col::AccentCyan);
    if (!m_pendingImage.isNull()) {
        appendImage(m_pendingImage);
    }
    setInputEnabled(false);

    if (m_pendingImage.isNull()) {
        m_agent.sendMessage(text);
    } else {
        const QImage image = m_pendingImage;
        m_pendingImage = QImage();
        updatePendingImageUi();
        if (text.isEmpty()) {
            text = QStringLiteral("Describe what is shown in this image.");
        }
        m_agent.sendImageMessage(image, text);
    }
}

void MainWindow::onModelReady(const QString &model)
{
    m_modelReady = true;
    m_statusLabel->setText(QStringLiteral("✦ Model ready: %1").arg(model));
    updateModelLabels(model);
    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Local AI model '%1' is ready. Ask me anything about your "
                                 "system, or just chat!")
                      .arg(model),
                  Col::AccentGlow);
    setInputEnabled(true);

    m_agent.startLearning();
    m_agent.startCalendar();
    m_agent.calendarManager().setAutoRefresh(true);

    // Start background update check (30 min interval)
    m_agent.updateChecker().startPeriodicCheck(
        m_updateIntervalSpin ? m_updateIntervalSpin->value() : 30);

    QTimer::singleShot(2000, this, [this]() {
        const QString suggestions = m_agent.getStartupSuggestions();
        if (!suggestions.isEmpty()) {
            appendMessage(QStringLiteral("TitanAI"), suggestions, Col::AccentGlow);
        }
    });
}

void MainWindow::onModelError(const QString &error)
{
    m_modelReady = false;
    m_statusLabel->setText(QStringLiteral("✕ Model unavailable"));
    appendMessage(QStringLiteral("Error"), error, Col::Danger);
    setInputEnabled(false);
}

void MainWindow::onResponseChunk(const QString &chunk)
{
    if (m_currentPage != 1) {
        navigateTo(1);
    }

    m_streamActive = true;

    if (!m_streamBlockStarted) {
        startStreamingBlock();
    }

    QTextCursor cursor = m_chatDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(chunk);
    m_chatDisplay->setTextCursor(cursor);
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

void MainWindow::onResponseReceived(const QString &response)
{
    if (m_currentPage != 1) {
        navigateTo(1);
    }

    if (!m_streamActive) {
        appendMessage(QStringLiteral("TitanAI"), response, Col::AccentGlow);
    }

    m_streamActive = false;
    m_streamBlockStarted = false;
    setInputEnabled(true);

    const VoiceEngine::Config voiceConfig = m_voiceEngine.config();
    if (voiceConfig.voiceEnabled && voiceConfig.readAloudEnabled) {
        m_voiceEngine.speak(response);
    }
}

void MainWindow::onErrorOccurred(const QString &error)
{
    if (m_currentPage != 1) {
        navigateTo(1);
    }

    m_streamActive = false;
    m_streamBlockStarted = false;
    appendMessage(QStringLiteral("Error"), error, Col::Danger);
    setInputEnabled(true);
}

void MainWindow::onInstallRequested(const QStringList &packages)
{
    QMessageBox::StandardButton answer =
        QMessageBox::question(this,
                              QStringLiteral("Confirm Installation"),
                              QStringLiteral("Install the following package(s) on your system?\n\n"
                                             "  %1\n\n"
                                             "This runs pacman with administrator privileges.")
                                  .arg(packages.join(QStringLiteral(", "))),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        setInputEnabled(true);
        return;
    }

    navigateTo(1);
    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Installing %1...").arg(packages.join(QStringLiteral(", "))),
                  Col::AccentGlow);
    m_agent.performInstall(packages);
}

void MainWindow::onToolOutput(const QString &line)
{
    if (m_currentPage != 1) {
        navigateTo(1);
    }
    appendPlainLine(line, Col::TextMuted);
}

void MainWindow::onModelNegotiation(const QString &message)
{
    // Surface memory-based model recommendations without forcing a navigation,
    // so a background suggestion never yanks the user away from what they are
    // doing. Keep the status bar in sync as well.
    if (m_statusLabel) {
        m_statusLabel->setText(message);
    }
    const bool isWarning = message.startsWith(QLatin1String("⚠"));
    appendPlainLine(message, isWarning ? Col::Warning : Col::Success);
}

void MainWindow::onBrowseProject()
{
    const QString startDir =
        m_projectDirectory.isEmpty() ? QDir::homePath() : m_projectDirectory;
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Project Directory"), startDir);
    if (directory.isEmpty()) {
        return;
    }
    applyProjectDirectory(directory);
}

void MainWindow::onSettingsBrowseProject()
{
    const QString startDir =
        m_projectDirectory.isEmpty() ? QDir::homePath() : m_projectDirectory;
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Project Directory"), startDir);
    if (directory.isEmpty()) {
        return;
    }
    applyProjectDirectory(directory);
}

// Applies a chosen project directory across the app state, Dev Hub and Settings.
void MainWindow::applyProjectDirectory(const QString &directory)
{
    m_projectDirectory = directory;
    if (m_projectEdit) {
        m_projectEdit->setText(directory);
    }
    if (m_settingsProjectEdit) {
        m_settingsProjectEdit->setText(directory);
    }
    m_settings.setValue(QStringLiteral("projectDir"), directory);
    m_agent.setProjectDirectory(directory);
}

void MainWindow::onSaveSettings()
{
    // Project directory
    if (m_settingsProjectEdit) {
        const QString dir = m_settingsProjectEdit->text().trimmed();
        applyProjectDirectory(dir);
    }

    // Build command
    if (m_settingsBuildEdit) {
        m_buildCommand = m_settingsBuildEdit->text().trimmed();
        m_settings.setValue(QStringLiteral("buildCommand"), m_buildCommand);
        m_agent.setBuildCommand(m_buildCommand);
        if (m_buildEdit) {
            m_buildEdit->setText(m_buildCommand);
        }
    }

    // Auto-fix
    const bool autoFix = m_settingsAutoFixCheck ? m_settingsAutoFixCheck->isChecked() : m_agent.autoFixEnabled();
    m_agent.setAutoFixEnabled(autoFix);
    m_settings.setValue(QStringLiteral("autoFixEnabled"), autoFix);
    if (m_autoFixCheck) {
        const QSignalBlocker blocker(m_autoFixCheck);
        m_autoFixCheck->setChecked(autoFix);
    }

    // Update check interval
    const int interval = m_settingsIntervalSpin ? m_settingsIntervalSpin->value() : 30;
    if (m_updateIntervalSpin) {
        const QSignalBlocker blocker(m_updateIntervalSpin);
        m_updateIntervalSpin->setValue(interval);
    }
    m_settings.setValue(QStringLiteral("updateInterval"), interval);
    m_agent.updateChecker().startPeriodicCheck(interval);

    // Sync model label
    updateModelLabels(m_agent.currentModel());

    if (m_settingsStatusLabel) {
        m_settingsStatusLabel->setText(QStringLiteral("✓ Settings saved at %1")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
    }

    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("⚙️ Settings saved. Active model: `%1`.").arg(m_agent.currentModel()),
                  Col::AccentGlow);
}

void MainWindow::onBuildAndFixClicked()
{
    if (!m_agent.isCodeDevelopmentEnabled()) {
        navigateTo(1);
        appendMessage(QStringLiteral("TitanAI"),
                      QStringLiteral("⏸️ Code development is currently frozen. Say 'enable code development' to resume."),
                      Col::Warning);
        return;
    }
    if (m_agent.isCodeFixBusy()) {
        return;
    }
    if (!m_modelReady) {
        navigateTo(1);
        appendMessage(QStringLiteral("Error"),
                      QStringLiteral("The model is not ready yet. Please wait."),
                      Col::Danger);
        return;
    }
    if (m_projectEdit) {
        m_projectDirectory = m_projectEdit->text().trimmed();
        m_agent.setProjectDirectory(m_projectDirectory);
    }
    if (m_buildEdit) {
        m_buildCommand = m_buildEdit->text().trimmed();
        m_agent.setBuildCommand(m_buildCommand);
    }
    setInputEnabled(false);
    m_agent.runBuildAndFix();
}

void MainWindow::onOrganizeClicked()
{
    if (m_agent.fileOrganizer().isScanning()) {
        return;
    }
    const QString directory =
        m_projectDirectory.isEmpty() ? QDir::homePath() : m_projectDirectory;
    m_agent.fileOrganizer().startScan(directory);
}

void MainWindow::onDiskCleanupClicked()
{
    if (m_agent.diskCleanup().isAnalyzing()) {
        return;
    }
    m_agent.diskCleanup().startAnalysis();
}

void MainWindow::onCheckUpdatesClicked()
{
    if (m_agent.updateChecker().isChecking()) {
        m_updateStatusLabel->setText(QStringLiteral("Already checking, please wait..."));
        return;
    }
    m_agent.updateChecker().startCheck();
}

void MainWindow::onApplyRepoUpdatesClicked()
{
    if (m_agent.updateChecker().isApplying()) return;
    m_updateOutputLog->clear();
    m_agent.updateChecker().applyUpdates(false);
}

void MainWindow::onApplyAllUpdatesClicked()
{
    if (m_agent.updateChecker().isApplying()) return;
    m_updateOutputLog->clear();
    m_agent.updateChecker().applyUpdates(true);
}

void MainWindow::onUpdateCheckProgress(const QString &stage)
{
    if (m_updateStatusLabel)
        m_updateStatusLabel->setText(stage);
}

void MainWindow::onUpdateCheckFinished(int count)
{
    m_updateProgressBar->setVisible(false);
    m_checkUpdatesButton->setEnabled(true);

    const QString lastChecked = QStringLiteral("Last checked: %1")
        .arg(m_agent.updateChecker().lastCheckTimeString());

    if (count == 0) {
        m_updateStatusLabel->setText(
            QStringLiteral("%1  |  ✅ All up to date").arg(lastChecked));
        m_updateCountBadge->setVisible(false);
        m_applyRepoButton->setEnabled(false);
        m_applyAllButton->setEnabled(false);
    } else {
        m_updateStatusLabel->setText(
            QStringLiteral("%1  |  %2 update(s) available").arg(lastChecked).arg(count));

        // Red badge
        m_updateCountBadge->setText(QString::number(qMin(count, 99)));
        m_updateCountBadge->setVisible(true);

        // Enable apply buttons
        m_applyRepoButton->setEnabled(true);
        if (m_agent.updateChecker().aurHelperAvailable()) {
            m_applyAllButton->setEnabled(true);
            m_applyAllButton->setVisible(true);
        }

        // Show in chat too
        navigateTo(1);
        appendMessage(QStringLiteral("TitanAI"),
                      m_agent.updateChecker().formatUpdateReport(),
                      Col::AccentGlow);
    }
}

void MainWindow::onUpdateCheckError(const QString &error)
{
    m_updateProgressBar->setVisible(false);
    m_checkUpdatesButton->setEnabled(true);
    m_updateStatusLabel->setText(QStringLiteral("⚠️ Error: %1").arg(error));
    appendMessage(QStringLiteral("Error"), error, Col::Danger);
}

void MainWindow::onUpdatesApplyStarted(const QString &command)
{
    m_applyRepoButton->setEnabled(false);
    m_applyAllButton->setEnabled(false);
    m_updateOutputLog->setVisible(true);
    m_updateOutputLog->appendPlainText(
        QStringLiteral("$ %1").arg(command));
}

void MainWindow::onUpdatesApplyOutput(const QString &line)
{
    if (m_updateOutputLog) {
        m_updateOutputLog->appendPlainText(line);
        // Auto-scroll to bottom
        QScrollBar *sb = m_updateOutputLog->verticalScrollBar();
        if (sb) sb->setValue(sb->maximum());
    }
}

void MainWindow::onUpdatesApplyFinished(bool success)
{
    const int count = m_agent.updateChecker().pendingUpdates().size();
    m_applyRepoButton->setEnabled(count > 0);
    if (m_agent.updateChecker().aurHelperAvailable())
        m_applyAllButton->setEnabled(count > 0);

    const QString msg = success
        ? QStringLiteral("✅ Updates applied successfully!")
        : QStringLiteral("⚠️ Update process finished (check the terminal window for details).");
    m_updateOutputLog->appendPlainText(msg);
    appendMessage(QStringLiteral("TitanAI"), msg, success ? Col::AccentGlow : Col::Warning);

    // Re-check to refresh the list
    QTimer::singleShot(2000, this, [this]() {
        m_agent.updateChecker().startCheck();
    });
}

void MainWindow::onPeriodicUpdateCheckDone(int count)
{
    if (count <= 0) return; // Nothing to notify

    // System tray popup notification
    if (m_trayIcon && m_trayIcon->supportsMessages()) {
        m_trayIcon->showMessage(
            QStringLiteral("TitanAI – Updates Available 🔄"),
            QStringLiteral("%1 package update(s) available. Click to view.").arg(count),
            QSystemTrayIcon::Information,
            8000  // show for 8 seconds
        );
    }

    // Also update the badge without switching pages (user may be mid-task)
    if (m_updateCountBadge) {
        m_updateCountBadge->setText(QString::number(qMin(count, 99)));
        m_updateCountBadge->setVisible(true);
    }
    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(
            QStringLiteral("Last checked: %1  |  %2 update(s) available")
                .arg(m_agent.updateChecker().lastCheckTimeString())
                .arg(count));
    }
    if (m_applyRepoButton) m_applyRepoButton->setEnabled(true);
    if (m_applyAllButton && m_agent.updateChecker().aurHelperAvailable())
        m_applyAllButton->setEnabled(true);
}

void MainWindow::startStreamingBlock()
{
    // Start a new bot-style bubble for streaming content
    m_chatDisplay->append(QStringLiteral(
        "<table width='100%%' cellpadding='0' cellspacing='0'><tr>"
        "<td width='8' bgcolor='%1'></td>"
        "<td bgcolor='%2' style='padding:10px 14px;'>"
        "<b style='color:%3;font-size:12px;'>✦ TitanAI</b><br/>"
    ).arg(Col::Accent, QStringLiteral("#151d2e"), Col::AccentGlow));
    m_streamBlockStarted = true;

    QTextCursor cursor = m_chatDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_chatDisplay->setTextCursor(cursor);
}

void MainWindow::setInputEnabled(bool enabled)
{
    m_input->setEnabled(enabled);
    m_sendButton->setEnabled(enabled);
    if (m_buildFixButton) {
        m_buildFixButton->setEnabled(enabled);
    }
    if (enabled) {
        m_input->setFocus();
    }
}

void MainWindow::appendMessage(const QString &sender, const QString &text, const QString &color)
{
    QString escaped = text.toHtmlEscaped();
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));

    const bool isUser = (sender == QStringLiteral("You"));
    const bool isError = (sender == QStringLiteral("Error") || sender == QStringLiteral("Voice"));

    // Pick bubble colours
    QString bubbleBg, accentBar, senderIcon;
    if (isUser) {
        bubbleBg  = QStringLiteral("#1a1540");
        accentBar = Col::AccentCyan;
        senderIcon = QStringLiteral("👤 ");
    } else if (isError) {
        bubbleBg  = QStringLiteral("#2a1215");
        accentBar = Col::Danger;
        senderIcon = QStringLiteral("⚠ ");
    } else {
        bubbleBg  = QStringLiteral("#151d2e");
        accentBar = Col::Accent;
        senderIcon = QStringLiteral("✦ ");
    }

    // Alignment: user messages align right, bot/errors align left
    const QString align = isUser ? QStringLiteral("right") : QStringLiteral("left");
    const QString width = QStringLiteral("88%%");

    const QString html = QStringLiteral(
        "<table width='%1' align='%2' cellpadding='0' cellspacing='0' style='margin-bottom:2px;'>"
        "<tr>"
        "<td width='4' bgcolor='%3'></td>"
        "<td bgcolor='%4' style='padding:10px 14px;'>"
        "<b style='color:%5;font-size:12px;'>%6%7</b><br/>"
        "<span style='color:%8;font-size:14px;'>%9</span>"
        "</td>"
        "</tr></table>"
    ).arg(width, align, accentBar, bubbleBg, color, senderIcon, sender,
          Col::TextPrimary, escaped);

    m_chatDisplay->append(html);
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

void MainWindow::appendPlainLine(const QString &text, const QString &color)
{
    const QString html = QStringLiteral(
        "<table width='88%%' cellpadding='0' cellspacing='0' style='margin-bottom:1px;'>"
        "<tr>"
        "<td width='4' bgcolor='%1'></td>"
        "<td bgcolor='%2' style='padding:6px 14px;'>"
        "<code style='color:%3;font-size:12px;'>%4</code>"
        "</td>"
        "</tr></table>"
    ).arg(Col::TextMuted, QStringLiteral("#111520"), color, text.toHtmlEscaped());

    m_chatDisplay->append(html);
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

void MainWindow::appendImage(const QImage &image)
{
    QImage thumb = image.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    thumb.save(&buffer, "PNG");

    const QString html = QStringLiteral(
        "<table width='88%%' align='right' cellpadding='0' cellspacing='0' style='margin-bottom:2px;'>"
        "<tr>"
        "<td width='4' bgcolor='%1'></td>"
        "<td bgcolor='%2' style='padding:10px 14px;'>"
        "<img src='data:image/png;base64,%3' width='300'/>"
        "</td>"
        "</tr></table>"
    ).arg(Col::AccentCyan, QStringLiteral("#1a1540"), QString::fromLatin1(bytes.toBase64()));

    m_chatDisplay->append(html);
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

// ─────────────────────────────────────────────────────────────
//  Voice slots
// ─────────────────────────────────────────────────────────────
void MainWindow::onVoicePartial(const QString &text)
{
    m_voiceStatusLabel->setText(QStringLiteral("... %1").arg(text));
}

void MainWindow::onVoiceFinal(const QString &text)
{
    m_input->setText(text);
    m_voiceStatusLabel->clear();

    if (m_voiceEngine.config().autoSendEnabled) {
        onSendClicked();
    }
}

void MainWindow::onVoiceError(const QString &error)
{
    navigateTo(1);
    appendMessage(QStringLiteral("Voice"), error, Col::Danger);
    m_voiceStatusLabel->setText(error);
}

void MainWindow::onVoiceSettings()
{
    VoiceSettingsDialog dialog(m_voiceEngine, this);
    if (dialog.exec() == QDialog::Accepted) {
        const VoiceEngine::Config config = dialog.config();
        m_voiceEngine.setConfig(config);
        if (config.voiceEnabled && config.wakeWordEnabled) {
            m_voiceEngine.startWakeWordListening();
        }
        saveVoiceSettings(config);
        updateVoiceUi();
    }
}

void MainWindow::onVoiceButtonToggled(bool enabled)
{
    if (enabled) {
        m_voiceEngine.startListening();
    } else {
        m_voiceEngine.stopListening();
    }
}

void MainWindow::onCaptureFromCamera()
{
    CameraDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        const QImage image = dialog.image();
        if (image.isNull()) {
            return;
        }
        m_pendingImage = image;
        updatePendingImageUi();
        navigateTo(1);
        appendMessage(QStringLiteral("TitanAI"),
                      QStringLiteral("Image captured. Ask your question about it and press Send."),
                      Col::AccentGlow);
    }
}

void MainWindow::onSelectImage()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select Image"),
        QDir::homePath(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*)"));
    if (file.isEmpty()) {
        return;
    }

    QImage image(file);
    if (image.isNull()) {
        navigateTo(1);
        appendMessage(QStringLiteral("Error"),
                      QStringLiteral("Could not load the selected image."),
                      Col::Danger);
        return;
    }

    m_pendingImage = image;
    updatePendingImageUi();
    navigateTo(1);
    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Image selected. Ask your question about it and press Send."),
                  Col::AccentGlow);
}

void MainWindow::onClearPendingImage()
{
    m_pendingImage = QImage();
    updatePendingImageUi();
}

void MainWindow::updatePendingImageUi()
{
    const bool hasImage = !m_pendingImage.isNull();
    m_pendingImageLabel->setVisible(hasImage);
    m_clearImageButton->setVisible(hasImage);
    if (hasImage) {
        QImage thumb = m_pendingImage.scaled(32, 32, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        thumb.save(&buffer, "PNG");
        m_pendingImageLabel->setText(
            QStringLiteral("<img src=\"data:image/png;base64,%1\"/> Image attached "
                           "&mdash; type a question and press Send.")
                .arg(QString::fromLatin1(bytes.toBase64())));
    }
}

VoiceEngine::Config MainWindow::loadVoiceSettings()
{
    VoiceEngine::Config config;
    config.voiceEnabled = m_settings.value(QStringLiteral("voiceEnabled"), true).toBool();
    config.readAloudEnabled =
        m_settings.value(QStringLiteral("readAloudEnabled"), true).toBool();
    config.wakeWordEnabled =
        m_settings.value(QStringLiteral("wakeWordEnabled"), false).toBool();
    config.autoSendEnabled =
        m_settings.value(QStringLiteral("autoSendEnabled"), false).toBool();
    config.wakeWord =
        m_settings.value(QStringLiteral("wakeWord"), QStringLiteral("hey titan")).toString();
    config.sttModelPath = m_settings.value(QStringLiteral("sttModelPath")).toString();
    config.ttsVoice = m_settings.value(QStringLiteral("ttsVoice")).toString();
    config.ttsRate = m_settings.value(QStringLiteral("ttsRate"), 0.0).toDouble();
    config.ttsPitch = m_settings.value(QStringLiteral("ttsPitch"), 0.0).toDouble();
    config.ttsVolume = m_settings.value(QStringLiteral("ttsVolume"), 1.0).toDouble();
    return config;
}

void MainWindow::saveVoiceSettings(const VoiceEngine::Config &config)
{
    m_settings.setValue(QStringLiteral("voiceEnabled"), config.voiceEnabled);
    m_settings.setValue(QStringLiteral("readAloudEnabled"), config.readAloudEnabled);
    m_settings.setValue(QStringLiteral("wakeWordEnabled"), config.wakeWordEnabled);
    m_settings.setValue(QStringLiteral("autoSendEnabled"), config.autoSendEnabled);
    m_settings.setValue(QStringLiteral("wakeWord"), config.wakeWord);
    m_settings.setValue(QStringLiteral("sttModelPath"), config.sttModelPath);
    m_settings.setValue(QStringLiteral("ttsVoice"), config.ttsVoice);
    m_settings.setValue(QStringLiteral("ttsRate"), config.ttsRate);
    m_settings.setValue(QStringLiteral("ttsPitch"), config.ttsPitch);
    m_settings.setValue(QStringLiteral("ttsVolume"), config.ttsVolume);
}

void MainWindow::updateVoiceUi()
{
    const VoiceEngine::Config config = m_voiceEngine.config();
    const bool sttAvailable = VoiceEngine::sttAvailable();
    const bool ttsAvailable = m_voiceEngine.ttsAvailable();

    m_voiceButton->setEnabled(config.voiceEnabled && sttAvailable);

    if (!ttsAvailable) {
        m_voiceStatusLabel->setText(
            QStringLiteral("No text-to-speech engine found (install 'qt6-speech' or 'espeak-ng')"));
    } else if (!config.voiceEnabled) {
        m_voiceStatusLabel->setText(QStringLiteral("Voice disabled"));
    } else if (!sttAvailable) {
        m_voiceStatusLabel->setText(
            QStringLiteral("Voice input needs Vosk (install 'vosk-api')"));
    } else if (config.wakeWordEnabled) {
        m_voiceStatusLabel->setText(
            QStringLiteral("Wake word enabled: \"%1\"").arg(config.wakeWord));
    } else {
        m_voiceStatusLabel->setText(
            QStringLiteral("Voice ready (engine: %1)").arg(m_voiceEngine.ttsEngineName()));
    }
}

void MainWindow::onStartupSuggestions(const QString &suggestions)
{
    if (!suggestions.isEmpty()) {
        appendMessage(QStringLiteral("TitanAI"), suggestions, Col::AccentGlow);
    }
}

void MainWindow::onCalendarEventsReady(const QString &eventsSummary)
{
    if (!eventsSummary.isEmpty()) {
        appendMessage(QStringLiteral("TitanAI"), eventsSummary, Col::AccentGlow);
    }
}

void MainWindow::onCalendarNotificationAlert(const QString &title, const QString &message)
{
    m_notificationBanner->setText(QStringLiteral("<b>%1</b> - %2")
                                     .arg(title.toHtmlEscaped(), message.toHtmlEscaped()));
    m_notificationBanner->setVisible(true);

    QTimer::singleShot(15000, this, [this]() {
        m_notificationBanner->setVisible(false);
    });

    appendMessage(QStringLiteral("TitanAI"), QStringLiteral("[%1] %2").arg(title, message),
                  Col::Warning);
}

void MainWindow::onOpenCalendarSettings()
{
    CalendarSettingsDialog dialog(m_agent.calendarManager(), m_agent.notificationManager(), this);
    dialog.exec();
}

// ─────────────────────────────────────────────────────────────
//  UI Design-to-Code Developer Hub slots
// ─────────────────────────────────────────────────────────────
void MainWindow::onUiDesignImageClicked()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select UI Design / Wireframe"),
        QDir::homePath(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*)"));
    if (file.isEmpty()) {
        return;
    }

    QImage image(file);
    if (image.isNull()) {
        m_uiStatusLabel->setText(QStringLiteral("⚠ Could not load image: %1").arg(file));
        m_uiStatusLabel->show();
        return;
    }

    m_uiDesignImage = image;

    // Show thumbnail
    QPixmap thumb = QPixmap::fromImage(
        image.scaled(116, 76, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_uiDesignPreview->setPixmap(thumb);
    m_uiDesignPreview->setToolTip(file);
    m_uiDesignClearBtn->setEnabled(true);
    m_uiStatusLabel->hide();
}

void MainWindow::onClearUiDesignImage()
{
    m_uiDesignImage = QImage();
    m_uiDesignPreview->clear();
    m_uiDesignPreview->setText(QStringLiteral("No design image attached"));
    m_uiDesignClearBtn->setEnabled(false);
    m_uiStatusLabel->hide();
}

void MainWindow::onGenerateUiClicked()
{
    if (!m_agent.isCodeDevelopmentEnabled()) {
        m_uiStatusLabel->setText(
            QStringLiteral("⏸️ Code development is currently frozen. Say 'enable code development' to resume."));
        m_uiStatusLabel->show();
        return;
    }

    const QString requirements = m_uiRequirementsEdit->toPlainText().trimmed();
    if (requirements.isEmpty() && m_uiDesignImage.isNull()) {
        m_uiStatusLabel->setText(
            QStringLiteral("⚠ Please attach a design image or enter requirements before generating."));
        m_uiStatusLabel->show();
        return;
    }

    // Resolve framework selection
    static const QList<UiDeveloper::Framework> kFwMap = {
        UiDeveloper::Framework::AutoDetect,
        UiDeveloper::Framework::HtmlCssJs,
        UiDeveloper::Framework::React,
        UiDeveloper::Framework::Vue,
        UiDeveloper::Framework::QtCpp,
        UiDeveloper::Framework::Flutter,
        UiDeveloper::Framework::Python,
    };
    const int idx = qBound(0, m_uiFrameworkCombo->currentIndex(),
                           static_cast<int>(kFwMap.size()) - 1);
    const UiDeveloper::Framework fw = kFwMap[idx];

    const QString branch = m_uiBranchEdit->text().trimmed();

    if (m_projectEdit) {
        m_projectDirectory = m_projectEdit->text().trimmed();
        m_agent.setProjectDirectory(m_projectDirectory);
    }

    // Disable UI while generating
    m_uiGenerateButton->setEnabled(false);
    m_uiDesignPickBtn->setEnabled(false);
    m_uiProgressBar->show();
    m_uiStatusLabel->setText(QStringLiteral("⏳ Initiating UI generation..."));
    m_uiStatusLabel->show();

    m_agent.developUi(m_uiDesignImage, requirements, branch, fw);
}

void MainWindow::onUiDevelopmentProgress(const QString &message)
{
    m_uiStatusLabel->setText(QStringLiteral("⏳ %1").arg(message));
    m_uiStatusLabel->show();

    // Also echo to chat
    navigateTo(1);
    appendMessage(QStringLiteral("TitanAI"), message, Col::AccentGlow);
}

void MainWindow::onUiDevelopmentFinished(bool success, const QString &summary, const QString &branchName)
{
    // Re-enable UI
    m_uiGenerateButton->setEnabled(true);
    m_uiDesignPickBtn->setEnabled(true);
    m_uiProgressBar->hide();

    if (success) {
        m_uiStatusLabel->setText(
            QStringLiteral("✅ Done! Branch: %1").arg(branchName));
        m_uiStatusLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 12px; }").arg(Col::Success));
    } else {
        m_uiStatusLabel->setText(QStringLiteral("❌ Failed: %1").arg(summary));
        m_uiStatusLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 12px; }").arg(Col::Danger));
    }
    m_uiStatusLabel->show();

    // Show full summary in chat
    navigateTo(1);
    appendMessage(QStringLiteral("TitanAI"), summary,
                  success ? QLatin1String(Col::AccentGlow) : QLatin1String(Col::Danger));
    setInputEnabled(true);
}

void MainWindow::onManageModels()
{
    // Refresh the installed model list from the local Ollama server first so
    // the dialog always shows the current state.
    m_agent.refreshModels();

    ModelDialog dialog(m_agent.currentModel(), m_agent.installedModels(), this);

    // Keep the dialog's installed list in sync with the live fetch/short-return.
    connect(&m_agent, &Agent::modelsChanged, &dialog, &ModelDialog::setInstalledModels);
    connect(&dialog, &ModelDialog::refreshRequested, &m_agent, &Agent::refreshModels);

    if (dialog.exec() == QDialog::Accepted) {
        const QString selectedModel = dialog.selectedModel();
        if (selectedModel.isEmpty() || selectedModel == m_agent.currentModel()) {
            return;
        }

        // Unload + re-point the client + ensure the new model is loaded in RAM.
        m_agent.switchModel(selectedModel);
        updateModelLabels(selectedModel);

        appendMessage(QStringLiteral("TitanAI"),
                      QStringLiteral("Switched active AI model to `%1`. Loading it...")
                          .arg(selectedModel),
                      Col::AccentGlow);
    }
}

void MainWindow::updateModelLabels(const QString &model)
{
    if (m_aiModelLabel) {
        m_aiModelLabel->setText(model);
    }
    if (m_settingsModelLabel) {
        m_settingsModelLabel->setText(model);
    }
}

void MainWindow::onModelsChanged(const QStringList &models)
{
    Q_UNUSED(models);
    updateModelLabels(m_agent.currentModel());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_agent.unloadModel(); // Immediately evict model weights from RAM on exit
    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------------------
// Power Management slots
// ---------------------------------------------------------------------------

void MainWindow::onPowerProfileChanged(int index)
{
    if (!m_powerProfileCombo) return;
    const auto profile = static_cast<PowerProfile>(
        m_powerProfileCombo->itemData(index).toInt());
    m_agent.applyPowerProfile(profile);
}

void MainWindow::onBrightnessSliderChanged(int value)
{
    if (m_brightnessValueLabel)
        m_brightnessValueLabel->setText(QStringLiteral("%1%").arg(value));
    m_agent.powerManager().setBrightness(value);
}

void MainWindow::onFreeAiRamClicked()
{
    m_agent.unloadModel();
}

void MainWindow::onBatteryInfoUpdated(const BatteryInfo &info)
{
    if (!m_batteryBar || !m_batteryLabel || !m_powerStatusLabel) return;

    if (!info.present) {
        m_batteryLabel->setText(QStringLiteral("Battery: N/A (Desktop)"));
        m_batteryBar->setValue(100);
        m_powerStatusLabel->setText(QStringLiteral("⚡ AC Power"));
        return;
    }

    // Update level bar
    const int pct = qBound(0, info.percent, 100);
    m_batteryBar->setValue(pct);

    // Label: percentage + AC indicator
    QString label = QStringLiteral("Battery: %1%").arg(pct);
    if (info.acOnline)  label += QStringLiteral(" ⚡");
    m_batteryLabel->setText(label);

    // Status label with power draw
    QString status = info.statusText;
    if (info.powerNowMw > 0 && !info.acOnline)
        status += QStringLiteral(" (%1 mW)").arg(info.powerNowMw);
    m_powerStatusLabel->setText(status);

    // Colour-code the bar: green > 50, orange 20-50, red <= 20
    QString chunkColor;
    if (pct > 50 || info.acOnline)      chunkColor = QStringLiteral("#22c55e");  // green
    else if (pct > 20)                  chunkColor = QStringLiteral("#f59e0b");  // amber
    else                                chunkColor = QStringLiteral("#ef4444");  // red

    m_batteryBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: #111827; border-radius: 5px; border: none; }"
        "QProgressBar::chunk { background: %1; border-radius: 5px; }"
    ).arg(chunkColor));
}

void MainWindow::onLowBatteryWarning(int percent)
{
    appendMessage(
        QStringLiteral("TitanAI"),
        QStringLiteral("⚠️ Low battery: %1%% remaining. Switching to Power Saver profile.")
            .arg(percent),
        QStringLiteral("#f59e0b"));
    // Auto-switch to Power Saver
    if (m_powerProfileCombo) {
        const int idx = m_powerProfileCombo->findData(
            static_cast<int>(PowerProfile::PowerSaver));
        if (idx >= 0) m_powerProfileCombo->setCurrentIndex(idx);
    }
}

void MainWindow::onCriticalBatteryWarning(int percent)
{
    appendMessage(
        QStringLiteral("TitanAI"),
        QStringLiteral("🔴 Critical battery: %1%% remaining! Please plug in your charger.")
            .arg(percent),
        QStringLiteral("#ef4444"));
    // Force Power Saver
    if (m_powerProfileCombo) {
        const int idx = m_powerProfileCombo->findData(
            static_cast<int>(PowerProfile::PowerSaver));
        if (idx >= 0) m_powerProfileCombo->setCurrentIndex(idx);
    }
}
