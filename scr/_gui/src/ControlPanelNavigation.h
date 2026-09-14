#pragma once
#include <QPointer>
#include <QWidget>

class QLabel;
class QMenu;
class QTabWidget;
class QToolButton;
class QActionGroup;

// Compact navigation for the existing settings pages; page contents are owned
// by the tab widget and are not recreated when selecting a page.
class ControlPanelNavigation final : public QWidget
{
public:
    explicit ControlPanelNavigation(QTabWidget *pages, QWidget *parent = nullptr);
private:
    void rebuildMenu();
    void syncCurrentPage();
    QPointer<QTabWidget> m_pages;
    QLabel *m_heading = nullptr;
    QToolButton *m_button = nullptr;
    QMenu *m_menu = nullptr;
    QActionGroup *m_actions = nullptr;
};
