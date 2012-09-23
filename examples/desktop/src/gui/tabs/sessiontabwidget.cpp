/*
* Copyright (C) 2008-2012 J-P Nurmi <jpnurmi@gmail.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#include "sessiontabwidget.h"
#include "messageview.h"
#include "settings.h"
#include "session.h"
#include <irccommand.h>
#include <QtGui>

SessionTabWidget::SessionTabWidget(Session* session, QWidget* parent) :
    TabWidget(parent)
{
    d.handler.setSession(session);

    connect(this, SIGNAL(currentChanged(int)), this, SLOT(tabActivated(int)));
    connect(this, SIGNAL(newTabRequested()), this, SLOT(onNewTabRequested()), Qt::QueuedConnection);
    connect(this, SIGNAL(tabMenuRequested(int,QPoint)), this, SLOT(onTabMenuRequested(int,QPoint)));

    connect(session, SIGNAL(activeChanged(bool)), this, SLOT(updateStatus()));
    connect(session, SIGNAL(connectedChanged(bool)), this, SLOT(updateStatus()));

    connect(&d.handler, SIGNAL(receiverToBeAdded(QString)), this, SLOT(openView(QString)));
    connect(&d.handler, SIGNAL(receiverToBeRemoved(QString)), this, SLOT(removeView(QString)));
    connect(&d.handler, SIGNAL(receiverToBeRenamed(QString,QString)), this, SLOT(renameView(QString,QString)));

    d.tabLeftShortcut = new QShortcut(this);
    connect(d.tabLeftShortcut, SIGNAL(activated()), this, SLOT(moveToPrevTab()));

    d.tabRightShortcut = new QShortcut(this);
    connect(d.tabRightShortcut, SIGNAL(activated()), this, SLOT(moveToNextTab()));

    QShortcut* shortcut = new QShortcut(QKeySequence::AddTab, this);
    connect(shortcut, SIGNAL(activated()), this, SLOT(onNewTabRequested()));

    shortcut = new QShortcut(QKeySequence::Close, this);
    connect(shortcut, SIGNAL(activated()), this, SLOT(closeCurrentView()));

    MessageView* view = openView(d.handler.session()->host());
    d.handler.setDefaultReceiver(view);
    updateStatus();

    applySettings(d.settings);
}

Session* SessionTabWidget::session() const
{
    return qobject_cast<Session*>(d.handler.session());
}

MessageView* SessionTabWidget::openView(const QString& receiver)
{
    MessageView* view = d.views.value(receiver.toLower());
    if (!view)
    {
        MessageView::ViewType type = MessageView::ServerView;
        if (!d.views.isEmpty())
            type = session()->isChannel(receiver) ? MessageView::ChannelView : MessageView::QueryView;
        view = new MessageView(type, d.handler.session(), this);
        view->setReceiver(receiver);
        connect(view, SIGNAL(alerted(IrcMessage*)), this, SLOT(onTabAlerted(IrcMessage*)));
        connect(view, SIGNAL(highlighted(IrcMessage*)), this, SLOT(onTabHighlighted(IrcMessage*)));
        connect(view, SIGNAL(queried(QString)), this, SLOT(openView(QString)));

        d.handler.addReceiver(receiver, view);
        d.views.insert(receiver.toLower(), view);
        addTab(view, QString(receiver).replace("&", "&&"));
        emit viewAdded(view);
    }
    setCurrentWidget(view);
    return view;
}

void SessionTabWidget::removeView(const QString& receiver)
{
    MessageView* view = d.views.take(receiver.toLower());
    if (view)
    {
        view->deleteLater();
        emit viewRemoved(view);
        if (indexOf(view) == 0)
        {
            deleteLater();
            session()->destructLater();
            emit sessionClosed(session());
        }
    }
}

void SessionTabWidget::closeCurrentView()
{
    closeView(currentIndex());
}

void SessionTabWidget::closeView(int index)
{
    MessageView* view = d.views.value(tabText(index).replace("&&", "&").toLower());
    if (view)
    {
        QString reason = tr("%1 %2").arg(QApplication::applicationName())
                                    .arg(QApplication::applicationVersion());
        if (indexOf(view) == 0)
            session()->quit(reason);
        else if (view->viewType() == MessageView::ChannelView)
            d.handler.session()->sendCommand(IrcCommand::createPart(view->receiver(), reason));

        d.handler.removeReceiver(view->receiver());
    }
}

void SessionTabWidget::renameView(const QString& from, const QString& to)
{
    MessageView* view = d.views.take(from.toLower());
    if (view)
    {
        view->setReceiver(to);
        d.views.insert(to.toLower(), view);
        int index = indexOf(view);
        if (index != -1)
            setTabText(index, view->receiver().replace("&", "&&"));
        emit viewRenamed(from, to);
    }
}

bool SessionTabWidget::event(QEvent* event)
{
    if (event->type() == QEvent::WindowActivate)
        delayedTabReset();
    return TabWidget::event(event);
}

void SessionTabWidget::updateStatus()
{
    bool inactive = !session()->isActive() && !session()->isConnected();
    setTabInactive(0, inactive);
    emit inactiveStatusChanged(inactive);
}

void SessionTabWidget::tabActivated(int index)
{
    if (index < count() - 1)
    {
        d.handler.setCurrentReceiver(qobject_cast<MessageView*>(currentWidget()));
        setTabAlert(index, false);
        setTabHighlight(index, false);
        if (isVisible())
        {
            window()->setWindowFilePath(tabText(index).replace("&&", "&"));
            if (currentWidget())
                currentWidget()->setFocus();
        }
    }
}

void SessionTabWidget::onNewTabRequested()
{
    QInputDialog dialog(this);
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog.setWindowTitle(tr("Join channel"));
    dialog.setLabelText(tr("Channel:"));
    if (dialog.exec())
    {
        QString channel = dialog.textValue();
        if (!channel.isEmpty())
        {
            if (session()->isChannel(channel))
                d.handler.session()->sendCommand(IrcCommand::createJoin(channel));
            openView(channel);
        }
    }
}

void SessionTabWidget::onTabMenuRequested(int index, const QPoint& pos)
{
    QMenu menu;
    if (index == 0)
    {
        if (session()->isActive())
            menu.addAction(tr("Disconnect"), session(), SLOT(quit()));
        else
            menu.addAction(tr("Reconnect"), session(), SLOT(reconnect()));
    }
    if (static_cast<MessageView*>(widget(index))->viewType() == MessageView::ChannelView)
        menu.addAction(tr("Part"), this, SLOT(onTabCloseRequested()))->setData(index);
    else
        menu.addAction(tr("Close"), this, SLOT(onTabCloseRequested()))->setData(index);
    menu.exec(pos);
}

void SessionTabWidget::onTabCloseRequested()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action)
        closeView(action->data().toInt());
}

void SessionTabWidget::delayedTabReset()
{
    d.delayedIndexes += currentIndex();
    QTimer::singleShot(500, this, SLOT(delayedTabResetTimeout()));
}

void SessionTabWidget::delayedTabResetTimeout()
{
    if (d.delayedIndexes.isEmpty())
        return;

    int index = d.delayedIndexes.takeFirst();
    tabActivated(index);
}

void SessionTabWidget::onTabAlerted(IrcMessage* message)
{
    int index = indexOf(static_cast<QWidget*>(sender()));
    if (index != -1)
    {
        if (!isVisible() || !isActiveWindow() || index != currentIndex())
        {
            setTabAlert(index, true);
            emit alerted(message);
        }
    }
}

void SessionTabWidget::onTabHighlighted(IrcMessage* message)
{
    int index = indexOf(static_cast<QWidget*>(sender()));
    if (index != -1)
    {
        if (!isVisible() || !isActiveWindow() || index != currentIndex())
        {
            setTabHighlight(index, true);
            emit highlighted(message);
        }
    }
}

void SessionTabWidget::applySettings(const Settings& settings)
{
    d.tabLeftShortcut->setKey(QKeySequence(settings.shortcuts.value(Settings::TabLeft)));
    d.tabRightShortcut->setKey(QKeySequence(settings.shortcuts.value(Settings::TabRight)));

    QColor color(settings.colors.value(Settings::Highlight));
    setTabTextColor(Alert, color);
    setTabTextColor(Highlight, color);

    foreach (MessageView* view, d.views)
        view->applySettings(settings);
    d.settings = settings;
}
