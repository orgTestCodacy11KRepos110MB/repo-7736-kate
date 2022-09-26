/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2001 Christoph Cullmann <cullmann@kde.org>
   SPDX-FileCopyrightText: 2001 Joseph Wenninger <jowenn@kde.org>
   SPDX-FileCopyrightText: 2001, 2005 Anders Lund <anders.lund@lund.tdcadsl.dk>

   SPDX-License-Identifier: LGPL-2.0-only
*/
#include "kateviewspace.h"

#include "diffwidget.h"
#include "kateapp.h"
#include "katedebug.h"
#include "katedocmanager.h"
#include "katefileactions.h"
#include "katemainwindow.h"
#include "katesessionmanager.h"
#include "kateupdatedisabler.h"
#include "kateurlbar.h"
#include "kateviewmanager.h"
#include "tabmimedata.h"

#include <KAcceleratorManager>
#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QHelpEvent>
#include <QMenu>
#include <QMessageBox>
#include <QRubberBand>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QWhatsThis>

#include <KTextEditor/Editor>

// BEGIN KateViewSpace
KateViewSpace::KateViewSpace(KateViewManager *viewManager, QWidget *parent, const char *name)
    : QWidget(parent)
    , m_viewManager(viewManager)
    , m_isActiveSpace(false)
{
    setObjectName(QString::fromLatin1(name));
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    // BEGIN tab bar
    QHBoxLayout *hLayout = new QHBoxLayout();
    hLayout->setSpacing(0);
    hLayout->setContentsMargins(0, 0, 0, 0);

    // add left <-> right history buttons
    m_historyBack = new QToolButton(this);
    m_historyBack->setToolTip(i18n("Go to Previous Location"));
    m_historyBack->setIcon(QIcon::fromTheme(QStringLiteral("arrow-left")));
    m_historyBack->setAutoRaise(true);
    KAcceleratorManager::setNoAccel(m_historyBack);
    m_historyBack->installEventFilter(this); // on click, active this view space
    hLayout->addWidget(m_historyBack);
    connect(m_historyBack, &QToolButton::clicked, this, [this] {
        goBack();
    });
    m_historyBack->setEnabled(false);

    m_historyForward = new QToolButton(this);
    m_historyForward->setIcon(QIcon::fromTheme(QStringLiteral("arrow-right")));
    m_historyForward->setToolTip(i18n("Go to Next Location"));
    m_historyForward->setAutoRaise(true);
    KAcceleratorManager::setNoAccel(m_historyForward);
    m_historyForward->installEventFilter(this); // on click, active this view space
    hLayout->addWidget(m_historyForward);
    connect(m_historyForward, &QToolButton::clicked, this, [this] {
        goForward();
    });
    m_historyForward->setEnabled(false);

    // add tab bar
    m_tabBar = new KateTabBar(this);
    connect(m_tabBar, &KateTabBar::currentChanged, this, &KateViewSpace::changeView);
    connect(m_tabBar, &KateTabBar::tabCloseRequested, this, &KateViewSpace::closeTabRequest, Qt::QueuedConnection);
    connect(m_tabBar, &KateTabBar::contextMenuRequest, this, &KateViewSpace::showContextMenu, Qt::QueuedConnection);
    connect(m_tabBar, &KateTabBar::newTabRequested, this, &KateViewSpace::createNewDocument);
    connect(m_tabBar, SIGNAL(activateViewSpaceRequested()), this, SLOT(makeActive()));
    hLayout->addWidget(m_tabBar);

    // add quick open
    m_quickOpen = new QToolButton(this);
    m_quickOpen->setAutoRaise(true);
    KAcceleratorManager::setNoAccel(m_quickOpen);
    m_quickOpen->installEventFilter(this); // on click, active this view space
    hLayout->addWidget(m_quickOpen);

    // forward tab bar quick open action to global quick open action
    QAction *bridge = new QAction(QIcon::fromTheme(QStringLiteral("quickopen")), i18nc("indicator for more documents", "+%1", 100), this);
    m_quickOpen->setDefaultAction(bridge);
    QAction *quickOpen = m_viewManager->mainWindow()->actionCollection()->action(QStringLiteral("view_quick_open"));
    Q_ASSERT(quickOpen);
    bridge->setToolTip(quickOpen->toolTip());
    bridge->setWhatsThis(i18n("Click here to switch to the Quick Open view."));
    connect(bridge, &QAction::triggered, quickOpen, &QAction::trigger);

    // add vertical split view space
    m_split = new QToolButton(this);
    m_split->setAutoRaise(true);
    m_split->setPopupMode(QToolButton::InstantPopup);
    m_split->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
    m_split->addAction(m_viewManager->mainWindow()->actionCollection()->action(QStringLiteral("view_split_vert")));
    m_split->addAction(m_viewManager->mainWindow()->actionCollection()->action(QStringLiteral("view_split_horiz")));
    m_split->addAction(m_viewManager->mainWindow()->actionCollection()->action(QStringLiteral("view_split_vert_move_doc")));
    m_split->addAction(m_viewManager->mainWindow()->actionCollection()->action(QStringLiteral("view_split_horiz_move_doc")));
    m_split->addAction(m_viewManager->mainWindow()->actionCollection()->action(QStringLiteral("view_close_current_space")));
    m_split->addAction(m_viewManager->mainWindow()->actionCollection()->action(QStringLiteral("view_close_others")));
    m_split->addAction(m_viewManager->mainWindow()->actionCollection()->action(QStringLiteral("view_hide_others")));
    m_split->setToolTip(i18n("Split View"));
    m_split->setWhatsThis(i18n("Control view space splitting"));
    m_split->installEventFilter(this); // on click, active this view space
    hLayout->addWidget(m_split);

    layout->addLayout(hLayout);
    // END tab bar

    m_urlBar = new KateUrlBar(this);

    // like other editors, we try to re-use documents, of not modified
    connect(m_urlBar, &KateUrlBar::openUrlRequested, this, [this](const QUrl &url, Qt::KeyboardModifiers mod) {
        // try if reuse of view make sense
        const bool shiftPress = mod == Qt::ShiftModifier;
        if (!shiftPress && !KateApp::self()->documentManager()->findDocument(url)) {
            if (auto activeView = m_viewManager->activeView()) {
                KateDocumentInfo *info = KateApp::self()->documentManager()->documentInfo(activeView->document());
                if (info && !info->wasDocumentEverModified) {
                    activeView->document()->openUrl(url);
                    return;
                }
            }
        }

        // default: open a new document or switch there
        m_viewManager->openUrl(url);
    });
    layout->addWidget(m_urlBar);

    stack = new QStackedWidget(this);
    stack->setFocus();
    stack->setSizePolicy(QSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding));
    layout->addWidget(stack);

    m_group.clear();

    // connect signal to hide/show statusbar
    connect(m_viewManager->mainWindow(), &KateMainWindow::tabBarToggled, this, &KateViewSpace::tabBarToggled);
    connect(m_viewManager, &KateViewManager::showUrlNavBarChanged, this, &KateViewSpace::urlBarToggled);

    connect(this, &KateViewSpace::viewSpaceEmptied, m_viewManager, &KateViewManager::onViewSpaceEmptied);

    // we accept drops (tabs from other viewspaces / windows)
    setAcceptDrops(true);

    m_layout.tabBarLayout = hLayout;
    m_layout.mainLayout = layout;

    // apply config, will init tabbar
    readConfig();

    // handle config changes
    connect(KateApp::self(), &KateApp::configurationChanged, this, &KateViewSpace::readConfig);

    // ensure we show/hide tabbar if needed
    connect(KateApp::self()->documentManager(), &KateDocManager::documentCreated, this, &KateViewSpace::documentCreatedOrDeleted);
    connect(KateApp::self()->documentManager(), &KateDocManager::documentDeleted, this, &KateViewSpace::documentCreatedOrDeleted);
}

KateViewSpace::~KateViewSpace() = default;

void KateViewSpace::readConfig()
{
    // get tab config
    KSharedConfig::Ptr config = KSharedConfig::openConfig();
    KConfigGroup cgGeneral = KConfigGroup(config, "General");
    m_autoHideTabBar = cgGeneral.readEntry("Auto Hide Tabs", KateApp::isKWrite());

    // init tab bar
    tabBarToggled();
}

bool KateViewSpace::eventFilter(QObject *obj, QEvent *event)
{
    QToolButton *button = qobject_cast<QToolButton *>(obj);

    // quick open button: show tool tip with shortcut
    if (button == m_quickOpen && event->type() == QEvent::ToolTip) {
        QHelpEvent *e = static_cast<QHelpEvent *>(event);
        QAction *quickOpen = m_viewManager->mainWindow()->actionCollection()->action(QStringLiteral("view_quick_open"));
        Q_ASSERT(quickOpen);
        QToolTip::showText(e->globalPos(), button->toolTip() + QStringLiteral(" (%1)").arg(quickOpen->shortcut().toString()), button);
        return true;
    }

    // quick open button: What's This
    if (button == m_quickOpen && event->type() == QEvent::WhatsThis) {
        QHelpEvent *e = static_cast<QHelpEvent *>(event);
        const int hiddenDocs = hiddenDocuments();
        QString helpText = (hiddenDocs == 0)
            ? i18n("Click here to switch to the Quick Open view.")
            : i18np("Currently, there is one more document open. To see all open documents, switch to the Quick Open view by clicking here.",
                    "Currently, there are %1 more documents open. To see all open documents, switch to the Quick Open view by clicking here.",
                    hiddenDocs);
        QWhatsThis::showText(e->globalPos(), helpText, m_quickOpen);
        return true;
    }

    // on mouse press on view space bar tool buttons: activate this space
    if (button && !isActiveSpace() && event->type() == QEvent::MouseButtonPress) {
        m_viewManager->setActiveSpace(this);
        if (currentView()) {
            m_viewManager->activateView(currentView()->document());
        }
    }
    return false;
}

void KateViewSpace::documentCreatedOrDeleted(KTextEditor::Document *)
{
    if (!m_autoHideTabBar || !m_viewManager->mainWindow()->showTabBar()) {
        return;
    }

    // toggle hide/show if state changed
    if ((KateApp::self()->documentManager()->documentList().size() > 1) != m_tabBar->isVisible()) {
        tabBarToggled();
    }
}

void KateViewSpace::tabBarToggled()
{
    KateUpdateDisabler updatesDisabled(m_viewManager->mainWindow());

    bool show = m_viewManager->mainWindow()->showTabBar();

    // we might want to auto hide if just one document is open
    if (show && m_autoHideTabBar) {
        show = KateApp::self()->documentManager()->documentList().size() > 1;
    }

    const bool urlBarVisible = m_viewManager->showUrlNavBar();

    bool showButtons = true;

    m_tabBar->setVisible(show);
    if (show) {
        m_layout.tabBarLayout->removeWidget(m_urlBar);
        int afterTabLayout = m_layout.mainLayout->indexOf(m_layout.tabBarLayout);
        m_layout.mainLayout->insertWidget(afterTabLayout + 1, m_urlBar);
        showButtons = true;
    } else if (!show && !urlBarVisible) {
        // both hidden, hide buttons as well
        showButtons = false;
    } else if (!show && urlBarVisible) {
        // UrlBar is still visible. Move it up to take the place of tabbar
        int insertAt = m_layout.tabBarLayout->indexOf(m_historyForward) + 1;
        m_layout.tabBarLayout->insertWidget(insertAt, m_urlBar);
        showButtons = true;
    }

    m_historyBack->setVisible(showButtons);
    m_historyForward->setVisible(showButtons);
    m_split->setVisible(showButtons);
    m_quickOpen->setVisible(showButtons);
}

void KateViewSpace::urlBarToggled(bool show)
{
    const bool tabBarVisible = m_viewManager->mainWindow()->showTabBar();
    bool showButtons = true;
    if (!show && !tabBarVisible) {
        // Tab bar was already hidden, now url bar is also hidden
        // so hide the buttons as well
        showButtons = false;
    } else if (show && !tabBarVisible) {
        // Tabbar is hidden, but we now need to show url bar
        // Move it up to take the place of tabbar
        int insertAt = m_layout.tabBarLayout->indexOf(m_historyForward) + 1;
        m_layout.tabBarLayout->insertWidget(insertAt, m_urlBar);
        // make sure buttons are visible
        showButtons = true;
    }

    m_historyBack->setVisible(showButtons);
    m_historyForward->setVisible(showButtons);
    m_split->setVisible(showButtons);
    m_quickOpen->setVisible(showButtons);
}

KTextEditor::View *KateViewSpace::createView(KTextEditor::Document *doc)
{
    // do nothing if we already have some view
    if (const auto it = m_docToView.find(doc); it != m_docToView.end()) {
        return it->second;
    }

    /**
     * Create a fresh view
     */
    KTextEditor::View *v = doc->createView(stack, m_viewManager->mainWindow()->wrapper());

    // our framework relies on the existence of the status bar
    v->setStatusBarEnabled(true);

    // restore the config of this view if possible
    if (!m_group.isEmpty()) {
        QString fn = v->document()->url().toString();
        if (!fn.isEmpty()) {
            QString vgroup = QStringLiteral("%1 %2").arg(m_group, fn);

            KateSession::Ptr as = KateApp::self()->sessionManager()->activeSession();
            if (as->config() && as->config()->hasGroup(vgroup)) {
                KConfigGroup cg(as->config(), vgroup);
                v->readSessionConfig(cg);
            }
        }
    }

    connect(v, &KTextEditor::View::cursorPositionChanged, this, [this](KTextEditor::View *view, const KTextEditor::Cursor &newPosition) {
        if (view && view->document())
            addPositionToHistory(view->document()->url(), newPosition);
    });

    // register document, it is shown below through showView() then
    registerDocument(doc);

    // view shall still be not registered
    Q_ASSERT(m_docToView.find(doc) == m_docToView.end());

    // insert View into stack
    stack->addWidget(v);
    m_docToView[doc] = v;
    showView(v);

    return v;
}

void KateViewSpace::removeView(KTextEditor::View *v)
{
    // remove view mappings, if we have none, this document didn't have any view here at all, just ignore it
    const auto it = m_docToView.find(v->document());
    if (it == m_docToView.end()) {
        return;
    }
    m_docToView.erase(it);

    // ...and now: remove from view space
    stack->removeWidget(v);

    // Remove the doc now!
    // Why do this now? Because otherwise it messes up the LRU
    // because we get two "currentChanged" signals
    // - First signal when we "showView" below
    // - Second comes soon after when v->document() is destroyed
    // Handling (blocking) both signals here is necessary
    m_tabBar->blockSignals(true);
    documentDestroyed(v->document());
    m_tabBar->blockSignals(false);

    // switch to most recently used rather than letting stack choose one
    // (last element could well be v->document() being removed here)
    bool shown = false;
    for (auto rit = m_registeredDocuments.rbegin(); rit != m_registeredDocuments.rend(); ++rit) {
        auto it = m_docToView.find(*rit);
        if (it != m_docToView.end()) {
            shown = showView(*rit);
            break;
        }
    }

    // This can happen if no other tab has a view in this viewspace
    if (!shown) {
        // At this point our current index has already changed
        int idx = m_tabBar->currentIndex();
        if (idx != -1) {
            m_viewManager->activateView(m_tabBar->tabDocument(idx));
        }
    }
}

bool KateViewSpace::showView(KTextEditor::Document *document)
{
    /**
     * nothing can be done if we have now view ready here
     */
    auto it = m_docToView.find(document);
    if (it == m_docToView.end()) {
        return false;
    }

    /**
     * update mru list order
     */
    const int index = m_registeredDocuments.lastIndexOf(document);
    // move view to end of list
    if (index < 0) {
        return false;
    }
    // move view to end of list
    m_registeredDocuments.removeAt(index);
    m_registeredDocuments.append(document);

    /**
     * show the wanted view
     */
    KTextEditor::View *kv = it->second;
    stack->setCurrentWidget(kv);
    kv->show();

    /**
     * we need to avoid that below's index changes will mess with current view
     */
    disconnect(m_tabBar, &KateTabBar::currentChanged, this, &KateViewSpace::changeView);

    /**
     * follow current view
     */
    m_tabBar->setCurrentDocument(document);

    // track tab changes again
    connect(m_tabBar, &KateTabBar::currentChanged, this, &KateViewSpace::changeView);
    return true;
}

void KateViewSpace::changeView(int idx)
{
    if (idx == -1) {
        return;
    }

    // make sure we open the view in this view space
    if (!isActiveSpace()) {
        m_viewManager->setActiveSpace(this);
    }

    KTextEditor::Document *doc = m_tabBar->tabDocument(idx);
    if (!doc) {
        auto w = m_tabBar->tabData(idx).value<QWidget *>();
        if (!w) {
            // can happen during widget creation if no view is there initially
            return;
        }
        stack->setCurrentWidget(w);
        m_viewManager->activateView(static_cast<KTextEditor::Document *>(nullptr));
        Q_EMIT m_viewManager->mainWindow()->widgetActivated(w);
        return;
    }

    auto currentActiveWidget = currentWidget();

    // tell the view manager to show the view
    m_viewManager->activateView(doc);

    // If we had a non-KTE::View widget as active, emit the signal
    // so that any listeners can tell that tab was changed
    if (currentActiveWidget) {
        Q_EMIT m_viewManager->viewChanged(m_viewManager->activeView());
    }
}

KTextEditor::View *KateViewSpace::currentView()
{
    // might be 0 if the stack contains no view
    return qobject_cast<KTextEditor::View *>(stack->currentWidget());
}

bool KateViewSpace::isActiveSpace() const
{
    return m_isActiveSpace;
}

void KateViewSpace::setActive(bool active)
{
    m_isActiveSpace = active;
    m_tabBar->setActive(active);
}

void KateViewSpace::makeActive(bool focusCurrentView)
{
    if (!isActiveSpace()) {
        m_viewManager->setActiveSpace(this);
        if (focusCurrentView && currentView()) {
            m_viewManager->activateView(currentView()->document());
        }
    }
    Q_ASSERT(isActiveSpace());
}

void KateViewSpace::registerDocument(KTextEditor::Document *doc)
{
    /**
     * ignore request to register something that is already known
     */
    if (m_registeredDocuments.contains(doc)) {
        return;
    }

    /**
     * remember our new document
     */
    m_registeredDocuments.insert(0, doc);

    /**
     * ensure we cleanup after document is deleted, e.g. we remove the tab bar button
     */
    connect(doc, &QObject::destroyed, this, &KateViewSpace::documentDestroyed);

    /**
     * register document is used in places that don't like view creation
     * therefore we must ensure the currentChanged doesn't trigger that
     */
    disconnect(m_tabBar, &KateTabBar::currentChanged, this, &KateViewSpace::changeView);

    /**
     * create the tab for this document, if necessary
     */
    m_tabBar->setCurrentDocument(doc);

    /**
     * handle later document state changes
     */
    connect(doc, &KTextEditor::Document::documentNameChanged, this, &KateViewSpace::updateDocumentName);
    connect(doc, &KTextEditor::Document::documentUrlChanged, this, &KateViewSpace::updateDocumentUrl);
    connect(doc, &KTextEditor::Document::modifiedChanged, this, [this](KTextEditor::Document *doc) {
        int tab = m_tabBar->documentIdx(doc);
        if (tab >= 0) {
            m_tabBar->setModifiedStateIcon(tab, doc);
        }
    });

    /**
     * allow signals again, now that the tab is there
     */
    connect(m_tabBar, &KateTabBar::currentChanged, this, &KateViewSpace::changeView);
}

void KateViewSpace::closeDocument(KTextEditor::Document *doc)
{
    // If this is the only view of the document,
    // OR the doc has no views yet
    // just close the document and it will take
    // care of removing the view + cleaning up the doc
    if (doc->views().size() <= 1) {
        m_viewManager->slotDocumentClose(doc);
    } else {
        // KTE::view for this tab has been created yet?
        auto it = m_docToView.find(doc);
        if (it != m_docToView.end()) {
            // - We have a view for this doc in this viewspace
            // - We have other views of this doc in other viewspaces
            // - Just remove the view in this viewspace
            m_viewManager->deleteView(it->second);
        } else {
            // We don't have a view for this doc in this viewspace
            // Just remove the document
            documentDestroyed(doc);
        }
    }

    /**
     * if this was the last doc, let viewManager know we are empty
     */
    if (m_registeredDocuments.isEmpty() && m_tabBar->count() == 0) {
        Q_EMIT viewSpaceEmptied(this);
    }
}

bool KateViewSpace::acceptsDroppedTab(const QMimeData *md)
{
    if (auto tabMimeData = qobject_cast<const TabMimeData *>(md)) {
        return this != tabMimeData->sourceVS && // must not be same viewspace
            viewManager() == tabMimeData->sourceVS->viewManager() && // for now we don't support dropping into different windows
            !hasDocument(tabMimeData->doc);
    }
    return TabMimeData::hasValidData(md);
}

void KateViewSpace::dragEnterEvent(QDragEnterEvent *e)
{
    if (acceptsDroppedTab(e->mimeData())) {
        m_dropIndicator.reset(new QRubberBand(QRubberBand::Rectangle, this));
        m_dropIndicator->setGeometry(rect());
        m_dropIndicator->show();
        e->acceptProposedAction();
        return;
    }

    QWidget::dragEnterEvent(e);
}

void KateViewSpace::dragLeaveEvent(QDragLeaveEvent *e)
{
    m_dropIndicator.reset();
    QWidget::dragLeaveEvent(e);
}

void KateViewSpace::dropEvent(QDropEvent *e)
{
    if (auto mimeData = qobject_cast<const TabMimeData *>(e->mimeData())) {
        m_viewManager->moveViewToViewSpace(this, mimeData->sourceVS, mimeData->doc);
        m_dropIndicator.reset();
        e->accept();
        return;
    }
    auto droppedData = TabMimeData::data(e->mimeData());
    if (droppedData.has_value()) {
        auto doc = KateApp::self()->documentManager()->openUrl(droppedData.value().url);
        auto view = m_viewManager->activateView(doc);
        if (view) {
            view->setCursorPosition({droppedData.value().line, droppedData.value().col});
            m_dropIndicator.reset();
            e->accept();
            return;
        }
    }

    QWidget::dropEvent(e);
}

bool KateViewSpace::hasDocument(KTextEditor::Document *doc) const
{
    return m_registeredDocuments.contains(doc) && (m_docToView.find(doc) != m_docToView.end());
}

KTextEditor::View *KateViewSpace::takeView(KTextEditor::Document *doc)
{
    auto it = m_docToView.find(doc);
    if (it == m_docToView.end()) {
        return nullptr;
    }
    auto *view = it->second;
    // remove it from the stack
    stack->removeWidget(view);
    // remove it from our doc->view mapping
    m_docToView.erase(it);
    documentDestroyed(doc);

    // Did we just loose our last doc?
    // Send a delayed signal. Delay is important as we want to kill
    // the viewspace after the view transfer was done
    if (m_tabBar->count() == 0 && m_registeredDocuments.empty()) {
        QMetaObject::invokeMethod(
            this,
            [this] {
                Q_EMIT viewSpaceEmptied(this);
            },
            Qt::QueuedConnection);
    }

    return view;
}

void KateViewSpace::addView(KTextEditor::View *v)
{
    registerDocument(v->document());
    m_docToView[v->document()] = v;
    // We must not already have this widget
    Q_ASSERT(stack->indexOf(v) == -1);
    stack->addWidget(v);
    showView(v);
}

void KateViewSpace::documentDestroyed(QObject *doc)
{
    /**
     * WARNING: this pointer is half destroyed
     * only good enough to check pointer value e.g. for hashes
     */
    KTextEditor::Document *invalidDoc = static_cast<KTextEditor::Document *>(doc);
    if (m_registeredDocuments.removeAll(invalidDoc) == 0) {
        // do nothing if this document wasn't registered for this viewspace
        return;
    }

    /**
     * we shall have no views for this document at this point in time!
     */
    Q_ASSERT(m_docToView.find(invalidDoc) == m_docToView.end());

    // disconnect entirely
    disconnect(doc, nullptr, this, nullptr);

    /**
     * remove the tab for this document, if existing
     */
    m_tabBar->removeDocument(invalidDoc);
}

void KateViewSpace::updateDocumentName(KTextEditor::Document *doc)
{
    // update tab button if available, might not be the case for tab limit set!wee
    const int buttonId = m_tabBar->documentIdx(doc);
    if (buttonId >= 0) {
        // BUG: 441278 We need to escape the & because it is used for accelerators/shortcut mnemonic by default
        QString tabName = doc->documentName();
        tabName.replace(QLatin1Char('&'), QLatin1String("&&"));
        m_tabBar->setTabText(buttonId, tabName);
    }
}

void KateViewSpace::updateDocumentUrl(KTextEditor::Document *doc)
{
    // update tab button if available, might not be the case for tab limit set!
    const int buttonId = m_tabBar->documentIdx(doc);
    if (buttonId >= 0) {
        m_tabBar->setTabToolTip(buttonId, doc->url().toDisplayString(QUrl::PreferLocalFile));
        m_tabBar->setModifiedStateIcon(buttonId, doc);
    }
}

void KateViewSpace::closeTabRequest(int idx)
{
    auto *doc = m_tabBar->tabDocument(idx);
    if (!doc) {
        auto widget = m_tabBar->tabData(idx).value<QWidget *>();
        if (!widget) {
            Q_ASSERT(false);
            return;
        }

        bool shouldClose = true;
        QMetaObject::invokeMethod(widget, "shouldClose", Q_RETURN_ARG(bool, shouldClose));
        if (shouldClose) {
            stack->removeWidget(widget);
            m_tabBar->removeTab(idx);
            widget->deleteLater();
            Q_EMIT m_viewManager->mainWindow()->widgetRemoved(widget);

            // if this was the last doc, let viewManager know we are empty
            if (m_registeredDocuments.isEmpty() && m_tabBar->count() == 0) {
                Q_EMIT viewSpaceEmptied(this);
            }
        }
        return;
    }

    closeDocument(doc);
}

void KateViewSpace::createNewDocument()
{
    // make sure we open the view in this view space
    if (!isActiveSpace()) {
        m_viewManager->setActiveSpace(this);
    }

    // create document
    KTextEditor::Document *doc = KateApp::self()->documentManager()->createDoc();

    // tell the view manager to show the document
    m_viewManager->activateView(doc);
}

void KateViewSpace::focusPrevTab()
{
    const int id = m_tabBar->prevTab();
    if (id >= 0) {
        changeView(id);
    }
}

void KateViewSpace::focusNextTab()
{
    const int id = m_tabBar->nextTab();
    if (id >= 0) {
        changeView(id);
    }
}

void KateViewSpace::addWidgetAsTab(QWidget *widget)
{
    stack->addWidget(widget);
    m_tabBar->setCurrentWidget(widget);
    stack->setCurrentWidget(widget);
}

bool KateViewSpace::hasWidgets() const
{
    return stack->count() > (int)m_docToView.size();
}

QWidget *KateViewSpace::currentWidget()
{
    if (auto w = stack->currentWidget()) {
        return qobject_cast<KTextEditor::View *>(w) ? nullptr : w;
    }
    return nullptr;
}

QWidgetList KateViewSpace::widgets() const
{
    QWidgetList widgets;
    for (int i = 0; i < m_tabBar->count(); ++i) {
        auto widget = m_tabBar->tabData(i).value<QWidget *>();
        if (widget) {
            widgets << widget;
        }
    }
    return widgets;
}

bool KateViewSpace::closeTabWithWidget(QWidget *widget)
{
    if (!widget) {
        return false;
    }

    for (int i = 0; i < m_tabBar->count(); ++i) {
        if (m_tabBar->tabData(i).value<QWidget *>() == widget) {
            closeTabRequest(i);
            return true;
        }
    }
    return false;
}

bool KateViewSpace::activateWidget(QWidget *widget)
{
    if (stack->indexOf(widget) == -1) {
        return false;
    }

    stack->setCurrentWidget(widget);
    for (int i = 0; i < m_tabBar->count(); ++i) {
        if (m_tabBar->tabData(i).value<QWidget *>() == widget) {
            m_tabBar->setCurrentIndex(i);
            return true;
        }
    }
    return false;
}

void KateViewSpace::focusNavigationBar()
{
    if (!m_urlBar->isHidden()) {
        m_urlBar->open();
    }
}

void KateViewSpace::addPositionToHistory(const QUrl &url, KTextEditor::Cursor c, bool calledExternally)
{
    // We don't care about invalid urls (Fixed Diff View / Untitled docs)
    if (!url.isValid()) {
        return;
    }

    // if same line, remove last entry
    // If new pos is same as "current pos", replace it with new one
    bool currPosIsInSameLine = false;
    if (currentLocation < m_locations.size()) {
        const auto &currentLoc = m_locations.at(currentLocation);
        currPosIsInSameLine = currentLoc.url == url && currentLoc.cursor.line() == c.line();
    }

    // Check if the location is at least "viewLineCount" away from the "current" position in m_locations
    if (const auto view = m_viewManager->activeView();
        view && !calledExternally && currentLocation < m_locations.size() && m_locations.at(currentLocation).url == url) {
        const int currentLine = m_locations.at(currentLocation).cursor.line();
        const int newPosLine = c.line();
        const int viewLineCount = view->lastDisplayedLine() - view->firstDisplayedLine();
        const int lowerBound = currentLine - viewLineCount;
        const int upperBound = currentLine + viewLineCount;
        if (lowerBound <= newPosLine && newPosLine <= upperBound) {
            if (currPosIsInSameLine) {
                m_locations[currentLocation].cursor = c;
            }
            return;
        }
    }

    if (currPosIsInSameLine) {
        m_locations[currentLocation].cursor.setColumn(c.column());
        return;
    }

    // we are in the middle of jumps somewhere?
    if (!m_locations.empty() && currentLocation + 1 < m_locations.size()) {
        // erase all forward history
        m_locations.erase(m_locations.begin() + currentLocation + 1, m_locations.end());
    }

    /** this is our new forward **/

    m_locations.push_back({url, c});
    // set currentLocation as last
    currentLocation = m_locations.size() - 1;
    // disable forward button as we are at the end now
    m_historyForward->setEnabled(false);
    Q_EMIT m_viewManager->historyForwardEnabled(false);

    // renable back
    if (currentLocation > 0) {
        m_historyBack->setEnabled(true);
        Q_EMIT m_viewManager->historyBackEnabled(true);
    }

    // limit size to 50, remove first 10
    int toErase = (int)m_locations.size() - 50;
    if (toErase > 0) {
        m_locations.erase(m_locations.begin(), m_locations.begin() + toErase);
        currentLocation -= toErase;
    }
}

int KateViewSpace::hiddenDocuments() const
{
    const auto hiddenDocs = KateApp::self()->documentManager()->documentList().size() - m_tabBar->count();
    Q_ASSERT(hiddenDocs >= 0);
    return hiddenDocs;
}

void KateViewSpace::showContextMenu(int idx, const QPoint &globalPos)
{
    // right now, show no context menu on empty tab bar space
    if (idx < 0) {
        return;
    }

    auto activeView = KTextEditor::Editor::instance()->application()->activeMainWindow()->activeView();
    if (!activeView) {
        return; // the welcome screen is open
    }

    auto *doc = m_tabBar->tabDocument(idx);
    auto activeDocument = activeView->document(); // used for compareUsing which is used with another
    if (!doc) {
        // This tab is holding some other widget
        // Show only "close tab" for now
        // maybe later allow adding context menu entries from the widgets
        // if needed
        QMenu menu(this);
        auto aCloseTab = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close")), i18n("Close Tab"));
        auto choice = menu.exec(globalPos);
        if (choice == aCloseTab) {
            // use single shot as this action can trigger deletion of this viewspace!
            QTimer::singleShot(0, this, [this, idx]() {
                closeTabRequest(idx);
            });
        }
        return;
    }

    auto addActionFromCollection = [this](QMenu *menu, const char *action_name) {
        QAction *action = m_viewManager->mainWindow()->action(action_name);
        return menu->addAction(action->icon(), action->text());
    };

    QMenu menu(this);
    QAction *aCloseTab = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close")), i18n("&Close Document"));
    QAction *aCloseOthers = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close-other")), i18n("Close Other &Documents"));
    QAction *aCloseAll = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close-all")), i18n("Close &All Documents"));
    menu.addSeparator();
    QAction *aDetachTab = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-detach")), i18n("D&etach Document"));
    aDetachTab->setWhatsThis(i18n("Opens the document in a new window and closes it in the current window"));
    menu.addSeparator();
    QAction *aCopyPath = addActionFromCollection(&menu, "file_copy_filepath");
    QAction *aOpenFolder = addActionFromCollection(&menu, "file_open_containing_folder");
    QAction *aFileProperties = addActionFromCollection(&menu, "file_properties");
    menu.addSeparator();
    QAction *aRenameFile = addActionFromCollection(&menu, "file_rename");
    QAction *aDeleteFile = addActionFromCollection(&menu, "file_delete");
    menu.addSeparator();
    QAction *compare = menu.addAction(i18n("Compare with Active Document"));
    compare->setIcon(QIcon::fromTheme(QStringLiteral("vcs-diff")));
    connect(compare, &QAction::triggered, this, [this, activeDocument, doc] {
        auto w = new DiffWidget({}, this);
        w->setWindowTitle(i18n("Diff %1 .. %2", activeDocument->documentName(), doc->documentName()));
        w->diffDocs(activeDocument, doc);
        m_viewManager->mainWindow()->addWidget(w);
    });

    QMenu *compareUsing = new QMenu(i18n("Compare with Active Document Using"), &menu);
    compareUsing->setIcon(QIcon::fromTheme(QStringLiteral("vcs-diff")));
    menu.addMenu(compareUsing);

    // if we have other documents, allow to close them
    aCloseOthers->setEnabled(KateApp::self()->documentManager()->documentList().size() > 1);

    // make it feasible to detach tabs if we have more then one
    aDetachTab->setEnabled(m_tabBar->count() > 1);

    if (doc->url().isEmpty()) {
        aCopyPath->setEnabled(false);
        aOpenFolder->setEnabled(false);
        aRenameFile->setEnabled(false);
        aDeleteFile->setEnabled(false);
        aFileProperties->setEnabled(false);
        compareUsing->setEnabled(false);
    }

    // both documents must have urls and must not be the same to have the compare feature enabled
    if (activeDocument->url().isEmpty() || activeDocument == doc) {
        compare->setEnabled(false);
        compareUsing->setEnabled(false);
    }

    if (compareUsing->isEnabled()) {
        for (auto &&diffTool : KateFileActions::supportedDiffTools()) {
            QAction *compareAction = compareUsing->addAction(diffTool.first);

            // we use the full path to safely execute the tool, disable action if no full path => tool not found
            compareAction->setData(diffTool.second);
            compareAction->setEnabled(!diffTool.second.isEmpty());
        }
    }

    QAction *choice = menu.exec(globalPos);

    if (!choice) {
        return;
    }

    if (choice == aCloseTab) {
        // use single shot as this action can trigger deletion of this viewspace!
        QTimer::singleShot(0, this, [this, idx]() {
            closeTabRequest(idx);
        });
    } else if (choice == aCloseOthers) {
        KateApp::self()->documentManager()->closeOtherDocuments(doc);
    } else if (choice == aCloseAll) {
        // use single shot as this action can trigger deletion of this viewspace!
        QTimer::singleShot(0, this, []() {
            KateApp::self()->documentManager()->closeAllDocuments();
        });
    } else if (choice == aCopyPath) {
        KateFileActions::copyFilePathToClipboard(doc);
    } else if (choice == aOpenFolder) {
        KateFileActions::openContainingFolder(doc);
    } else if (choice == aFileProperties) {
        KateFileActions::openFilePropertiesDialog(this, doc);
    } else if (choice == aRenameFile) {
        KateFileActions::renameDocumentFile(this, doc);
    } else if (choice == aDeleteFile) {
        KateFileActions::deleteDocumentFile(this, doc);
    } else if (choice->parent() == compareUsing) {
        QString actionData = choice->data().toString(); // name of the executable of the diff program
        if (!KateFileActions::compareWithExternalProgram(activeDocument, doc, actionData)) {
            QMessageBox::information(this,
                                     i18n("Could not start program"),
                                     i18n("The selected program could not be started. Maybe it is not installed."),
                                     QMessageBox::StandardButton::Ok);
        }
    } else if (choice == aDetachTab) {
        auto mainWindow = KateApp::self()->newMainWindow();
        mainWindow->viewManager()->openViewForDoc(doc);

        // use single shot as this action can trigger deletion of this viewspace!
        QTimer::singleShot(0, this, [this, idx]() {
            closeTabRequest(idx);
        });
    }
}

void KateViewSpace::saveConfig(KConfigBase *config, int myIndex, const QString &viewConfGrp)
{
    //   qCDebug(LOG_KATE)<<"KateViewSpace::saveConfig("<<myIndex<<", "<<viewConfGrp<<") - currentView: "<<currentView()<<")";
    QString groupname = QString(viewConfGrp + QStringLiteral("-ViewSpace %1")).arg(myIndex);

    // aggregate all views in view space (LRU ordered)
    std::vector<KTextEditor::View *> views;
    QStringList lruList;
    const auto docList = documentList();
    for (KTextEditor::Document *doc : docList) {
        lruList << doc->url().toString();
        auto it = m_docToView.find(doc);
        if (it != m_docToView.end()) {
            views.push_back(it->second);
        }
    }

    KConfigGroup group(config, groupname);
    group.writeEntry("Documents", lruList);
    group.writeEntry("Count", static_cast<int>(views.size()));

    if (currentView()) {
        group.writeEntry("Active View", currentView()->document()->url().toString());
    }

    // Save file list, including cursor position in this instance.
    int idx = 0;
    for (auto view : views) {
        const auto url = view->document()->url();
        if (!url.isEmpty()) {
            group.writeEntry(QStringLiteral("View %1").arg(idx), url.toString());

            // view config, group: "ViewSpace <n> url"
            QString vgroup = QStringLiteral("%1 %2").arg(groupname, url.toString());
            KConfigGroup viewGroup(config, vgroup);
            view->writeSessionConfig(viewGroup);
        }

        ++idx;
    }
}

void KateViewSpace::restoreConfig(KateViewManager *viewMan, const KConfigBase *config, const QString &groupname)
{
    KConfigGroup group(config, groupname);

    // workaround for the weird bug where the tabbar sometimes becomes invisible after opening a session via the session chooser dialog or the --start cmd
    // option
    // TODO: Debug the actual reason for the bug. See https://invent.kde.org/utilities/kate/-/merge_requests/189
    m_tabBar->hide();
    m_tabBar->show();

    // set back bar status to configured variant
    tabBarToggled();

    // restore Document lru list so that all tabs from the last session reappear
    const QStringList lruList = group.readEntry("Documents", QStringList());
    for (int i = 0; i < lruList.size(); ++i) {
        // ignore non-existing documents
        if (auto doc = KateApp::self()->documentManager()->findDocument(QUrl(lruList[i]))) {
            registerDocument(doc);
        }
    }

    // restore active view properties
    const QString fn = group.readEntry("Active View");
    if (!fn.isEmpty()) {
        if (auto doc = KateApp::self()->documentManager()->findDocument(QUrl(fn))) {
            if (auto view = viewMan->createView(doc, this)) {
                // view config, group: "ViewSpace <n> url"
                const QString vgroup = QStringLiteral("%1 %2").arg(groupname, fn);
                view->readSessionConfig(KConfigGroup(config, vgroup));
                m_tabBar->setCurrentDocument(doc);
            }
        }
    }

    // ensure we update the urlbar at least once
    m_urlBar->updateForDocument(currentView() ? currentView()->document() : nullptr);

    m_group = groupname; // used for restroing view configs later
}

void KateViewSpace::goBack()
{
    if (m_locations.empty() || currentLocation <= 0) {
        currentLocation = 0;
        return;
    }

    const auto &location = m_locations.at(currentLocation - 1);
    currentLocation--;

    if (currentLocation <= 0) {
        m_historyBack->setEnabled(false);
        Q_EMIT m_viewManager->historyBackEnabled(false);
    }

    if (auto v = m_viewManager->activeView()) {
        if (v->document()->url() == location.url) {
            const QSignalBlocker blocker(v);
            v->setCursorPosition(location.cursor);
            // enable forward
            m_historyForward->setEnabled(true);
            Q_EMIT m_viewManager->historyForwardEnabled(true);
            return;
        }
    }

    auto v = m_viewManager->openUrlWithView(location.url, QString());
    const QSignalBlocker blocker(v);
    v->setCursorPosition(location.cursor);
    // enable forward in viewspace + mainwindow
    m_historyForward->setEnabled(true);
    Q_EMIT m_viewManager->historyForwardEnabled(true);
}

bool KateViewSpace::isHistoryBackEnabled() const
{
    return m_historyBack->isEnabled();
}

bool KateViewSpace::isHistoryForwardEnabled() const
{
    return m_historyForward->isEnabled();
}

void KateViewSpace::goForward()
{
    if (m_locations.empty()) {
        return;
    }

    // We are already at the last position
    if (currentLocation >= m_locations.size() - 1) {
        return;
    }

    const auto &location = m_locations.at(currentLocation + 1);
    currentLocation++;

    if (currentLocation + 1 >= m_locations.size()) {
        Q_EMIT m_viewManager->historyForwardEnabled(false);
        m_historyForward->setEnabled(false);
    }

    if (!location.url.isValid() || !location.cursor.isValid()) {
        m_locations.erase(m_locations.begin() + currentLocation);
        return;
    }

    m_historyBack->setEnabled(true);
    Q_EMIT m_viewManager->historyBackEnabled(true);

    if (auto v = m_viewManager->activeView()) {
        if (v->document()->url() == location.url) {
            const QSignalBlocker blocker(v);
            v->setCursorPosition(location.cursor);
            return;
        }
    }

    auto v = m_viewManager->openUrlWithView(location.url, QString());
    const QSignalBlocker blocker(v);
    v->setCursorPosition(location.cursor);
}

// END KateViewSpace
