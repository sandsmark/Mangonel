/*
 * Copyright 2010-2012 Bart Kroon <bart@tarmack.eu>
 * Copyright 2012, 2013 Martin Sandsmark <martin.sandsmark@kde.org>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Mangonel.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QDesktopWidget>
#include <QDBusInterface>
#include <QIcon>
#include <QMenu>
#include <QTextDocument>
#include <QClipboard>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KWindowSystem>
#include <KNotification>
#include <KNotifyConfigWidget>
#include <KGlobalAccel>
#include <KSharedConfig>

#include "Config.h"
//Include the providers.
#include "providers/Applications.h"
#include "providers/Paths.h"
#include "providers/Shell.h"
#include "providers/Calculator.h"
#include "providers/Units.h"

#include <QDebug>

#include <unistd.h>

#define WINDOW_WIDTH 440
#define WINDOW_HEIGHT 400
#define ICON_SIZE (WINDOW_WIDTH / 1.5)

Mangonel::Mangonel()
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setContextMenuPolicy(Qt::ActionsContextMenu);
    setAttribute(Qt::WA_InputMethodEnabled);
    setAttribute(Qt::WA_MouseTracking, false);
    m_processingKey = false;
    m_apps = 0;
    QVBoxLayout* view = new QVBoxLayout(this);
    setLayout(view);
    view->setContentsMargins(0,10,0,8);
    // Setup the search feedback label.
    m_label = new Label(this);
    // Instantiate the visual feedback field.
    m_iconView = new IconView(this);
    // Add all to our layout.
    view->addWidget(m_iconView);
    view->addWidget(m_label);
    resize(WINDOW_WIDTH, WINDOW_HEIGHT);
    m_label->setMaximumWidth(WINDOW_WIDTH - 20);

    // Setup our global shortcut.
    m_actionShow = new QAction(i18n("Show Mangonel"), this);
    m_actionShow->setObjectName(QString("show"));
    QKeySequence shortcut = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Space);
    m_actionShow->setShortcut(shortcut);
    KGlobalAccel::self()->setGlobalShortcut(m_actionShow, QList<QKeySequence>() << shortcut);
    connect(m_actionShow, SIGNAL(triggered()), this, SLOT(showHide()));

    const KConfigGroup config(KSharedConfig::openConfig(), "mangonel_main");
    m_history = config.readEntry("history", QStringList());

    QString shortcutString(m_actionShow->shortcut().toString());
    QString message(i18nc("@info", "Press <shortcut>%1</shortcut> to show Mangonel.", shortcutString));

    KNotification::event(QLatin1String("startup"), message);

    // Instantiate the providers.
    m_providers["applications"] = new Applications();
    m_providers["paths"] = new Paths();
    m_providers["shell"] = new Shell();
    m_providers["Calculator"] = new Calculator();
    m_providers["Units"] = new Units();

    connect(m_label, SIGNAL(textChanged(QString)), this, SLOT(getApp(QString)));

    QAction* actionConfig = new QAction(QIcon::fromTheme("configure"), i18n("Configuration"), this);
    addAction(actionConfig);
    connect(actionConfig, SIGNAL(triggered(bool)), this, SLOT(showConfig()));

    QAction* notifyConfig = new QAction(QIcon::fromTheme("configure-notifications"), i18n("Configure notifications"), this);
    addAction(notifyConfig);
    connect(notifyConfig, SIGNAL(triggered(bool)), this, SLOT(configureNotifications()));

    QAction* quit = new QAction(QIcon::fromTheme("application-exit"), i18n("Quit"), this);
    addAction(quit);
    connect(quit, SIGNAL(triggered(bool)), qApp, SLOT(quit()));
}

Mangonel::~Mangonel()
    // Store history of session.
{
    KConfigGroup config(KSharedConfig::openConfig(), "mangonel_main");
    config.writeEntry("history", m_history);
    config.config()->sync();
}

bool Mangonel::event(QEvent* event)
{
    event->ignore();
    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*> (event);
        if (mouseEvent->button() == Qt::MiddleButton)
        {
            event->accept();
            m_label->appendText(QApplication::clipboard()->text(QClipboard::Selection));
        }
        else if (!geometry().contains(mouseEvent->globalPos()))
        {
            hide();
            event->accept();
        }
    }
    else if (event->type() == QEvent::ContextMenu)
    {
        QContextMenuEvent* menuEvent = static_cast<QContextMenuEvent*> (event);
        if (!geometry().contains(menuEvent->globalPos()))
            event->accept();
    }
    if (!event->isAccepted())
        QWidget::event(event);
    return true;
}

void Mangonel::inputMethodEvent(QInputMethodEvent* event)
{
    QString text = m_label->text();
    text.chop(event->preeditString().length());
    text = text.mid(0, text.length()+event->replacementStart());
    text.append(event->commitString());
    if (text == "~/")
        text = "";
    text.append(event->preeditString());
    m_label->setPreEdit(event->preeditString());
    m_label->setText(text);
}
void Mangonel::keyPressEvent(QKeyEvent* event)
{
    int key = event->key();
    IconView::direction direction = IconView::right;
    Application* CurrentApp;
    if (m_processingKey)
        return;
    m_processingKey = true;
    switch (event->key())
    {
    case Qt::Key_Enter:
    case Qt::Key_Return:
        launch();
    case Qt::Key_Escape:
        hide();
        break;
    case Qt::Key_Up:
        m_historyIndex += 2;
    case Qt::Key_Down:
        m_historyIndex -= 1;
        if (m_historyIndex >= 0)
        {
            if (m_historyIndex < m_history.length())
                m_label->setText(m_history[m_historyIndex]);
        }
        else
            m_historyIndex = -1;
        break;
    case Qt::Key_Left:
        direction = IconView::left;
    case Qt::Key_Right:
        m_iconView->moveItems(direction);
        CurrentApp = m_iconView->selectedApp();
        if (CurrentApp != 0)
            m_label->setCompletion(CurrentApp->completion);
        break;
    default:
        if (key == Qt::Key_Tab)
        {
            if (!m_label->completion().isEmpty())
                m_label->setText(m_label->completion());
        }
        else if (key == Qt::Key_Backspace)
        {
            QString text = m_label->text();
            text.chop(1);
            if (text == "~/")
                text = "";
            m_label->setText(text);
        }
        else if (event->matches(QKeySequence::Paste))
        {
            m_label->appendText(QApplication::clipboard()->text());
        }
        else
        {
            m_label->appendText(event->text());
        }
    }
    m_processingKey = false;
}

void Mangonel::getApp(QString query)
{
    m_iconView->clear();
    delete m_apps;
    m_apps = 0;
    if (query.length() > 0)
    {
        m_apps = new AppList();
        m_current = -1;
        foreach(Provider* provider, m_providers)
        {
            QList<Application> list = provider->getResults(query);
            foreach(const Application &app, list) {
                qDebug() << app.name << app.priority;
                m_apps->insertSorted(app);
            }
        }
        if (!m_apps->isEmpty())
        {
            for (int i = 0; i < m_apps->length(); i++)
            {
                m_iconView->addProgram(m_apps->at(i));
            }
            m_iconView->setFirst();
            Application* CurrentApp = m_iconView->selectedApp();
            if (CurrentApp != 0)
                m_label->setCompletion(CurrentApp->completion);
        }
        else
        {
            m_label->setCompletion("");
        }
    }
}

void Mangonel::launch()
{
    m_history.insert(0, m_label->text());
    Application* app = m_iconView->selectedApp();
    if (app != 0)
        app->object->launch(app->program);
}

void Mangonel::showHide()
{
    if (isVisible())
        hide();
    else
        show();
}

void Mangonel::show()
{
    resize(WINDOW_WIDTH, WINDOW_HEIGHT);
    m_historyIndex = -1;
    QRect screen = qApp->desktop()->screenGeometry(this);
    int x = (screen.width() - geometry().width()) / 2;
    int y = (screen.height() - geometry().height()) / 2;
    move(x, y);
    QWidget::show();
    KWindowSystem::forceActiveWindow(winId());
    setFocus();
}

void Mangonel::hide()
{
    m_label->setText("");
    m_iconView->clear();
    delete m_apps;
    m_apps = 0;
    QWidget::hide();
}

void Mangonel::focusInEvent(QFocusEvent* event)
{
    Q_UNUSED(event);
    grabMouse();
}

void Mangonel::focusOutEvent(QFocusEvent* event)
{
    releaseMouse();
    if (event->reason() != Qt::PopupFocusReason)
        hide();
}

bool Mangonel::eventFilter(QObject *object, QEvent *event)
{
    Q_UNUSED(object);
    if (event->type() == QEvent::FocusOut)
        return true;
    return false;
}

void Mangonel::showConfig()
{
    QKeySequence shortcut = m_actionShow->shortcut();
    ConfigDialog* dialog = new ConfigDialog(this);
    dialog->setHotkey(shortcut);
    connect(dialog, SIGNAL(hotkeyChanged(QKeySequence)), this, SLOT(setHotkey(QKeySequence)));
    installEventFilter(this);
    releaseMouse();
    dialog->exec();
    removeEventFilter(this);
    activateWindow();
    setFocus();
}

void Mangonel::setHotkey(const QKeySequence& hotkey)
{
    m_actionShow->setShortcut(hotkey);
    qDebug() << hotkey.toString();
}

void Mangonel::configureNotifications()
{
    KNotifyConfigWidget::configure();
}

IconView::IconView(QWidget* parent) : m_current(-1)
{
    Q_UNUSED(parent);
    m_scene = new QGraphicsScene(QRectF(0, 0, rect().width()*4, rect().height()), this);
    setScene(m_scene);
    setFrameStyle(QFrame::NoFrame);
    setStyleSheet("background: transparent; border: none");
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::NoFocus);
    centerOn(QPoint(rect().width()*1.5, 0));
}

IconView::~IconView()
{
    delete m_scene;
}

void IconView::clear()
{
    m_scene->clear();
    m_items.clear();
    m_current = -1;
}

void IconView::addProgram(Application application)
{
    ProgramView* program = new ProgramView(application);
    m_items.append(program);
    m_scene->addItem(program);
}

Application* IconView::selectedApp()
{
    if (m_current >= 0 and m_current < m_items.length())
    {
        return &m_items[m_current]->application;
    }
    else return 0;
}

void IconView::setFirst()
{
    if (!m_items.empty())
        m_current = 0;
    m_items[m_current]->show();
    m_items[m_current]->setPos(rect().width() + (rect().width() - ICON_SIZE) / 2, 0);
    centerOn(QPoint(rect().width()*1.5, 0));
}

void IconView::moveItems(IconView::direction direction)
{
    if (m_current < 0)
        return;
    int offset = rect().width();
    int steps =  10;
    int dx = offset / steps;
    int index = 1;
    if (direction == IconView::right)
    {
        if (m_current + 1 >= m_items.length())
            return;
        dx = -dx;
        offset *= 2;
    }
    else
    {
        if (m_current < 1)
            return;
        offset = 0;
        index = -1;
    }
    ProgramView* itemNew = m_items[m_current+index];
    ProgramView* itemOld = m_items[m_current];
    itemNew->setPos(offset + (rect().width() - ICON_SIZE) / 2, 0);
    itemNew->show();
    int startposNew = itemNew->pos().x();
    int startPosOld = itemOld->pos().x();
    for (int i = 0; i < steps / 2; i++)
    {
        itemNew->setPos(startposNew + (dx * i), 0);
        QApplication::instance()->processEvents();
        usleep(5000);
    }
    startposNew = itemNew->pos().x();
    startPosOld = itemOld->pos().x();
    for (int i = 0; i < steps / 2; i++)
    {
        itemNew->setPos(startposNew + (dx * i), 0);
        itemOld->setPos(startPosOld + (dx * i), 0);
        QApplication::instance()->processEvents();
        usleep(5000);
    }
    startposNew = itemNew->pos().x();
    startPosOld = itemOld->pos().x();
    for (int i = 0; i < steps / 2; i++)
    {
        itemOld->setPos(startPosOld + (dx * i), 0);
        QApplication::instance()->processEvents();
        usleep(5000);
    }
    itemOld->hide();
    itemNew->setPos(rect().width() + (rect().width() - ICON_SIZE) / 2, 0);
    m_current += index;
    centerOn(QPoint(rect().width()*1.5, 0));
}


ProgramView::ProgramView(Application app)
{
    hide();
    m_icon = 0;
    m_label = 0;
    m_block = 0;
    m_descriptionLabel = 0;
    application = app;
}

ProgramView::~ProgramView()
{
    delete m_icon;
    delete m_label;
    delete m_block;
    delete m_descriptionLabel;
}

void ProgramView::centerItems()
{
    m_icon->setPos(0, 0);
    QRectF iconRect = m_icon->boundingRect();
    QRectF labelRect = m_label->boundingRect();
    QRectF blockRect = m_block->boundingRect();
    QRectF descriptionRect = m_descriptionLabel->boundingRect();
    m_block->setPos(
        qreal(iconRect.width() / 2 - blockRect.width() / 2),
        qreal(iconRect.height() / 2 - blockRect.height() / 2)
    );
    m_label->setPos(
        qreal(iconRect.width() / 2 - labelRect.width() / 2),
        qreal(iconRect.height() / 2 - labelRect.height() / 2)
    );
    m_descriptionLabel->setPos(
        qreal(iconRect.width() / 2 - descriptionRect.width() / 2),
        qreal(iconRect.height() / 2 - descriptionRect.height() / 2 + labelRect.height())
    );
}

void ProgramView::show()
{
    if (!m_label) {
        m_label = new QGraphicsTextItem(application.name, this);
        if (m_label->boundingRect().width() > WINDOW_WIDTH - 40)
            m_label->adjustSize();
        m_label->document()->setDefaultTextOption(QTextOption(Qt::AlignCenter));
    }

    if (!m_icon) {
        m_icon = new QGraphicsPixmapItem(QIcon::fromTheme(application.icon).pixmap(ICON_SIZE), this);
    }

    if (!m_descriptionLabel) {
        m_descriptionLabel = new QGraphicsTextItem(i18nc("the type of the application to be launched, shown beneath the application name", "(%1)", application.type), this);
        if (m_descriptionLabel->boundingRect().width() > WINDOW_WIDTH - 40)
            m_descriptionLabel->adjustSize();
        m_descriptionLabel->document()->setDefaultTextOption(QTextOption(Qt::AlignCenter));
    }

    if (!m_block) {
        QRectF nameRect = m_label->boundingRect();
        QRectF descriptionRect = m_descriptionLabel->boundingRect();
        QRectF rect(nameRect.x(), nameRect.y() +10, qMax(nameRect.width(), descriptionRect.width()), nameRect.height() + descriptionRect.height() + 5);
        m_block = new QGraphicsRectItem(rect, this);
        m_block->setBrush(QPalette().base());
        m_block->setOpacity(0.7);
    }

    m_label->setZValue(10);
    m_descriptionLabel->setZValue(10);
    centerItems();
    QGraphicsItemGroup::show();
}


AppList::AppList()
{}

AppList::~AppList()
{}

void AppList::insertSorted(const Application &item)
{
    int index = length() / 2;
    if (length() > 0)
    {
        int span = 1 + length() / 2;
        int priority = item.priority;
        int item = value(index).priority;
        while (!(
                    priority > value(index - 1).priority and
                    priority <= item
                ))
        {
            span -= span / 2;
            if (priority > item)
                index += span;
            else if (priority <= item)
                index -= span;
            if (index < 0)
            {
                index = 0;
                break;
            }
            if (index >= length())
            {
                index = length();
                break;
            }
            item = value(index).priority;
        }
    }
    insert(index, item);
}

// kate: indent-mode cstyle; space-indent on; indent-width 4; 
