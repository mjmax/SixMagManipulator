#include "ControlPanelNavigation.h"
#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>

ControlPanelNavigation::ControlPanelNavigation(QTabWidget *pages, QWidget *parent)
    : QWidget(parent), m_pages(pages)
{
    setObjectName("controlPanelNavigation");
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    m_heading = new QLabel(this);
    m_heading->setObjectName("panelHeading");
    m_heading->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_heading, 1, Qt::AlignVCenter);

    m_button = new QToolButton(this);
    m_button->setObjectName("controlPageMenuButton");
    m_button->setFixedSize(28, 22);
    m_button->setToolTip("Select control panel page");
    m_button->setAccessibleName("Select control panel page");
    m_button->setCursor(Qt::PointingHandCursor);
    // Paint the three dots explicitly rather than relying on a font glyph.
    QPixmap dots(32, 32);
    dots.setDevicePixelRatio(2);
    dots.fill(Qt::transparent);
    {
        QPainter painter(&dots);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#b8c5d3"));
        for (const qreal x : {3.0, 8.0, 13.0})
            painter.drawEllipse(QPointF(x, 8), 1.5, 1.5);
    }
    m_button->setIcon(QIcon(dots));
    m_button->setIconSize(QSize(16, 16));
    layout->addWidget(m_button, 0, Qt::AlignVCenter);

    m_menu = new QMenu(this);
    m_menu->setObjectName("controlPageMenu");
    m_menu->setMinimumWidth(180);
    m_actions = new QActionGroup(this);
    m_actions->setExclusive(true);
    setStyleSheet(R"(
        QLabel#panelHeading {
            color: #b8c5d3; font-size: 11px; font-weight: 700;
            letter-spacing: 1px;
        }
        QToolButton#controlPageMenuButton {
            background: transparent; border: 1px solid transparent;
            border-radius: 4px; padding: 0px;
        }
        QToolButton#controlPageMenuButton:hover,
        QToolButton#controlPageMenuButton:focus {
            background: #263445; border-color: #344253;
        }
        QToolButton#controlPageMenuButton:pressed { background: #344253; }
        QMenu#controlPageMenu {
            background: #171e27; color: #e8edf3;
            border: 1px solid #344253; padding: 4px;
        }
        QMenu#controlPageMenu::item { padding: 7px 20px 7px 24px; }
        QMenu#controlPageMenu::item:selected {
            background: #263445; color: #f1f5f9;
        }
        QMenu#controlPageMenu::indicator {
            width: 6px; height: 6px; border: none; border-radius: 3px;
            background: transparent; image: none;
        }
        QMenu#controlPageMenu::indicator:checked { background: #b8c5d3; }
    )");
    connect(m_menu, &QMenu::aboutToShow, this, [this] { rebuildMenu(); });
    connect(m_button, &QToolButton::clicked, this, [this] {
        rebuildMenu();
        const QPoint position = m_button->mapToGlobal(
            QPoint(m_button->width() - m_menu->sizeHint().width(), m_button->height()));
        m_menu->popup(position);
    });
    if (m_pages) {
        m_pages->tabBar()->hide();
        m_pages->setCurrentIndex(0); // Always start on Control, not a saved page.
        connect(m_pages, &QTabWidget::currentChanged,
                this, [this] { syncCurrentPage(); });
    }
    rebuildMenu();
}

void ControlPanelNavigation::rebuildMenu()
{
    m_menu->clear();
    if (!m_pages) return;
    for (int index = 0; index < m_pages->count(); ++index) {
        auto *action = m_menu->addAction(m_pages->tabText(index));
        action->setData(index);
        action->setCheckable(true);
        action->setEnabled(m_pages->isTabEnabled(index));
        m_actions->addAction(action);
        connect(action, &QAction::triggered, this, [this, index] {
            if (m_pages) m_pages->setCurrentIndex(index);
            syncCurrentPage();
        });
    }
    syncCurrentPage();
}

void ControlPanelNavigation::syncCurrentPage()
{
    if (!m_pages) return;
    const int current = m_pages->currentIndex();
    const QString title = QStringLiteral("CONTROL PANEL : %1")
        .arg(m_pages->tabText(current));
    m_heading->setText(title);
    m_heading->setToolTip(title);
    for (auto *action : m_menu->actions())
        action->setChecked(action->data().toInt() == current);
}
