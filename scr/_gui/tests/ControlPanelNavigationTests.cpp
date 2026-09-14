#include "ControlPanelNavigation.h"
#include <QApplication>
#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setFont(QFont("Segoe UI", 10));
    try {
        QWidget panel;
        panel.setStyleSheet("background:#171e27;color:#e8edf3;");
        auto *layout = new QVBoxLayout(&panel);
        auto *pages = new QTabWidget(&panel);
        const QStringList names{"Control", "Actuators", "Image Processing", "Pole Calibration"};
        QList<QWidget *> originalPages;
        for (const auto &name : names) {
            auto *page = new QLabel(name + " settings");
            originalPages.append(page);
            pages->addTab(page, name);
        }
        pages->setCurrentIndex(2);
        auto *navigation = new ControlPanelNavigation(pages, &panel);
        layout->addWidget(navigation);
        layout->addWidget(pages, 1);
        panel.resize(520, 280);
        panel.show();
        app.processEvents();
        auto *heading = navigation->findChild<QLabel *>("panelHeading");
        auto *button = navigation->findChild<QToolButton *>("controlPageMenuButton");
        auto *menu = navigation->findChild<QMenu *>("controlPageMenu");
        require(heading && button && menu, "Missing navigation widgets");
        require(pages->currentIndex() == 0, "Control must be the initial page");
        require(!pages->tabBar()->isVisible(), "Old tab row must be hidden");
        require(qAbs(heading->geometry().center().y() - button->geometry().center().y()) <= 1,
                "Heading and menu button must align vertically");
        for (int pass = 0; pass < 3; ++pass) {
            button->click();
            app.processEvents();
            require(menu->isVisible(), "Button must open menu");
            require(menu->actions().size() == 4, "Menu must contain four pages");
            for (int i = 0; i < names.size(); ++i) {
                require(menu->actions()[i]->text() == names[i], "Wrong page order");
                menu->actions()[i]->trigger();
                require(pages->currentIndex() == i, "Menu must select page");
                require(pages->currentWidget() == originalPages[i], "Original page must be retained");
                require(heading->text() == "CONTROL PANEL : " + names[i], "Heading must follow page");
                int checked = 0;
                for (auto *action : menu->actions()) checked += action->isChecked();
                require(checked == 1 && menu->actions()[i]->isChecked(), "Only current page must have dot");
            }
            menu->hide();
        }
        pages->setCurrentIndex(2);
        require(menu->actions()[2]->isChecked(), "Programmatic page changes must update selection");
        app.processEvents();
        panel.grab().save("navigation-panel.png");
        button->click();
        app.processEvents();
        menu->grab().save("navigation-menu.png");
        menu->hide();
        std::cout << "Navigation tests passed: default, layout, all pages, selection, title, repeated opening.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
