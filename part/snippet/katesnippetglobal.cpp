/*  This file is part of the Kate project.
 *  Based on the snippet plugin from KDevelop 4.
 *
 *  Copyright (C) 2007 Robert Gruber <rgruber@users.sourceforge.net> 
 *  Copyright (C) 2012 Christoph Cullmann <cullmann@kde.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */
#include "katesnippetglobal.h"

#include <klocale.h>
#include <kpluginfactory.h>
#include <kaboutdata.h>
#include <kpluginloader.h>
#include <ktexteditor/view.h>
#include <ktexteditor/document.h>
#include <ktexteditor/codecompletioninterface.h>
#include <QMenu>

#include <KActionCollection>
#include <KAction>
#include <KToolBar>

#include <KTextEditor/HighlightInterface>

#include "snippetview.h"
#include "categorizedsnippetselector.h"

#include "editsnippet.h"
#include "repositoryview.h"
#include "completionmodel.h"
#include "katecombinedsnippetselector.h"

KateSnippetGlobal::KateSnippetGlobal(QObject *parent, const QVariantList &)
  : QObject(parent)
{
 //   qRegisterMetaType<KateSnippetGlobal::Mode>("KateSnippetGlobal::Mode");
    //m_model = new SnippetCompletionModel;
    m_snippetsMode=FileModeBasedMode;
    m_repositoryModel=new KTextEditor::CodesnippetsCore::SnippetRepositoryModel(this,KateGlobal::self());
    connect(m_repositoryModel,SIGNAL(typeChanged(QStringList)),this,SLOT(slotTypeChanged(QStringList)));

    reloadSessionConfig();
    
}

KateSnippetGlobal::~KateSnippetGlobal ()
{

}

enum KateSnippetGlobal::Mode KateSnippetGlobal::snippetsMode() {
  return m_snippetsMode;
}

void KateSnippetGlobal::setSnippetsMode(enum Mode mode) {
    if (mode!=m_snippetsMode) {
      m_snippetsMode=mode;
      emit snippetsModeChanged();
#warning update snippetviews
    }
}

KConfigGroup KateSnippetGlobal::sessionConfig() {
  return KateGlobal::self()->sessionConfig()->group("Snippets");
}

void KateSnippetGlobal::internalUpdateSessionConfig() {
  KConfigGroup group=sessionConfig();
  group.writeEntry("snippetsmode",(int)m_snippetsMode);
  m_repositoryModel->writeSessionConfig(&group,"repositorySettings");
  slotTypeChanged(QStringList()<<"*");
}

void KateSnippetGlobal::reloadSessionConfig() {
  
    KConfigGroup group=sessionConfig();
    setSnippetsMode((KateSnippetGlobal::Mode)group.readEntry("snippetsmode",(int)FileModeBasedMode));

    m_repositoryModel->readSessionConfig(&group,"repositorySettings");
    
    slotTypeChanged(QStringList()<<"*");
}

void KateSnippetGlobal::showDialog (KateView *view)
{
  if (m_activeViewForDialog) {
    kDebug()<<"There is already a dialog open -> m_activeViewForDialog would be ambigious";
    return;
  }
  
  
  KDialog dialog;
  dialog.setCaption(i18n("Snippets"));
  dialog.setButtons(KDialog::Ok);
  dialog.setDefaultButton(KDialog::Ok);
  
  QWidget *widget=snippetWidget(&dialog,view);
  dialog.setMainWidget(widget);
  m_activeViewForDialog = view;
  dialog.exec();
  m_activeViewForDialog = 0;
}

QWidget *KateSnippetGlobal::snippetWidget (QWidget *parent,KateView *initialView)
{
  if (!initialView) initialView=KateSnippetGlobal::self()->getCurrentView();
  return new KateCombinedSnippetSelector(parent,initialView);
 /*
  QTabWidget *widget=new QTabWidget(0);
  widget->addTab(new SnippetView (this,0),"KDevView");
  JoWenn::KateCategorizedSnippetSelector *tmp;
  widget->addTab(tmp=new JoWenn::KateCategorizedSnippetSelector((QWidget*)0),"KATE");
  tmp->viewChanged(currentView);
  return widget;
  //return new SnippetView (this, 0);
  */
}

KTextEditor::CodesnippetsCore::SnippetRepositoryModel *KateSnippetGlobal::repositoryModel () {
  return m_repositoryModel;
}

KateView* KateSnippetGlobal::getCurrentView() {
  KateView *view=0;
  view = m_activeViewForDialog; // a dialog is open ? use that
  if (view) return view;
  
  KTextEditor::MdiContainer *iface = qobject_cast<KTextEditor::MdiContainer*>(KateGlobal::self()->container());
  if (iface && iface->activeView())
    view = qobject_cast<KateView*>(iface->activeView());
  
  return view;
}


void KateSnippetGlobal::provideView(KTextEditor::View** pView) {
  *pView=getCurrentView();
  
}



void KateSnippetGlobal::insertSnippetFromActionData()
{
#warning FIXME
  /*
    KAction* action = dynamic_cast<KAction*>(sender());
    Q_ASSERT(action);
    Snippet* snippet = action->data().value<Snippet*>();
    Q_ASSERT(snippet);
    insertSnippet(snippet);
    */
}

void KateSnippetGlobal::createSnippet (KateView *view)
{
#warning FIXME
#if 0
   // invalid range? skip to do anything, it will fail!
   if (!view->selectionRange().isValid())
     return;

    // get mode
    QString mode = view->doc()->highlightingModeAt(view->selectionRange().start());
    if ( mode.isEmpty() )
        mode = view->doc()->mode();

    // try to look for a fitting repo
    SnippetRepository* match = 0;
    for ( int i = 0; i < SnippetStore::self()->rowCount(); ++i ) {
        SnippetRepository* repo = dynamic_cast<SnippetRepository*>( SnippetStore::self()->item(i) );
        if ( repo && repo->fileTypes().count() == 1 && repo->fileTypes().first() == mode ) {
            match = repo;
            break;
        }
    }
    bool created = !match;
    if ( created ) {
        match = SnippetRepository::createRepoFromName(
                i18nc("Autogenerated repository name for a programming language",
                      "%1 snippets", mode)
        );
        match->setFileTypes(QStringList() << mode);
    }

    EditSnippet dlg(match, 0, view);
    dlg.setSnippetText(view->selectionText());
    int status = dlg.exec();
    if ( created && status != KDialog::Accepted ) {
        // cleanup
        match->remove();
    }
#endif
}


  void KateSnippetGlobal::addDocument(KTextEditor::Document* document)
  {
    KTextEditor::HighlightInterface *hli=qobject_cast<KTextEditor::HighlightInterface*>(document);
    if (!hli) return;
    QStringList modes;
    modes<<document->mode();
    modes<<hli->embeddedHighlightingModes();
    kDebug(13040)<<modes;
    //kDebug(13040)<<"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!";
    QList<KTextEditor::CodesnippetsCore::SnippetCompletionModel> models;
    foreach (const QString& mode, modes)
    {
      QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel> completionModel;
      QHash<QString,QWeakPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel> >::iterator it=m_mode_model_hash.find(mode);
      if (it!=m_mode_model_hash.end()) {
        completionModel=it.value();
      }
      if (completionModel.isNull()) {
        completionModel=QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel>(m_repositoryModel->completionModel(mode));
        m_mode_model_hash.insert(mode,completionModel);
      }
      m_document_model_multihash.insert(document,QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel>(completionModel));
    }


    QList <QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel> >models2=m_document_model_multihash.values(document);
    QList<KTextEditor::CodesnippetsCore::SnippetSelectorModel*> list;
    foreach (const QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel>& model, models2)
    {
      list.append(model->selectorModel());
    }
    KTextEditor::CodesnippetsCore::CategorizedSnippetModel *mod;
    m_document_categorized_hash.insert(document,mod=new KTextEditor::CodesnippetsCore::CategorizedSnippetModel(list));
    connect(mod,SIGNAL(needView(KTextEditor::View**)),this,SLOT(provideView(KTextEditor::View**)));


    //Q_ASSERT(modelForDocument(document));
    const QList<KTextEditor::View*>& views=document->views();
    foreach (KTextEditor::View *view,views) {
      addView(document,view);
    }

    disconnect(document,SIGNAL(modeChanged(KTextEditor::Document*)),this,SLOT(updateDocument(KTextEditor::Document*)));
    disconnect(document,SIGNAL(viewCreated(KTextEditor::Document*,KTextEditor::View*)),this,SLOT(addView(KTextEditor::Document*,KTextEditor::View*)));

    connect(document,SIGNAL(modeChanged(KTextEditor::Document*)),this,SLOT(updateDocument(KTextEditor::Document*)));
    connect(document,SIGNAL(viewCreated(KTextEditor::Document*,KTextEditor::View*)),this,SLOT(addView(KTextEditor::Document*,KTextEditor::View*)));
  }

  
  void KateSnippetGlobal::removeDocument(KTextEditor::Document* document)
  {
    //if (!m_document_model_hash.contains(document)) return;
    delete m_document_categorized_hash.take(document);
    QList<QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel> >models=m_document_model_multihash.values(document);
    const QList<KTextEditor::View*>& views=document->views();
    foreach (const QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel>& model,models)
    {
      foreach (KTextEditor::View *view,views) {
        KTextEditor::CodeCompletionInterface *iface =
          qobject_cast<KTextEditor::CodeCompletionInterface*>( view );
        if (iface) {
          iface->unregisterCompletionModel(model.data());
        }
      }
    }
    m_document_model_multihash.remove(document);
    disconnect(document,SIGNAL(modeChanged(KTextEditor::Document*)),this,SLOT(updateDocument(KTextEditor::Document*)));
    disconnect(document,SIGNAL(viewCreated(KTextEditor::Document*,KTextEditor::View*)),this,SLOT(addView(KTextEditor::Document*,KTextEditor::View*)));
    Q_ASSERT(m_document_model_multihash.find(document)==m_document_model_multihash.end());
  }

  KTextEditor::CodesnippetsCore::CategorizedSnippetModel* KateSnippetGlobal::modelForDocument(KTextEditor::Document *document)
  {
    return m_document_categorized_hash[document];
  }

  void KateSnippetGlobal::addView(KTextEditor::Document* document,KTextEditor::View* view)
  {
    QList<QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel> > models=m_document_model_multihash.values(document);
    foreach (const QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel>& model, models) {
      KTextEditor::CodeCompletionInterface *iface =
        qobject_cast<KTextEditor::CodeCompletionInterface*>( view );
      if (iface) {
        iface->unregisterCompletionModel(model.data());
        iface->registerCompletionModel(model.data());
      }
    }
  }

  void KateSnippetGlobal::updateDocument(KTextEditor::Document* document)
  {
/*    QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel>model_d=m_document_model_multihash[document];
    QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel>model_t=m_mode_model_hash[document->mode()];
    if (model_t==model_d) return;*/
    removeDocument(document);
    addDocument(document);
    kDebug(13040)<<"invoking typeHasChanged(doc)";
    emit typeHasChanged(document);

  }

  void KateSnippetGlobal::slotTypeChanged(const QStringList& fileType)
  {
    QSet<KTextEditor::Document*> refreshList;
    if (fileType.contains("*")) {
      QList<KTextEditor::Document*> tmp_list(m_document_model_multihash.keys());
      foreach(KTextEditor::Document *doc,tmp_list) {
      refreshList.insert(doc);
      }
    } else {
      foreach(const QString& ft, fileType) {
        QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel>model_t=m_mode_model_hash[ft];
        QMultiHash<KTextEditor::Document*,QSharedPointer<KTextEditor::CodesnippetsCore::SnippetCompletionModel> >::const_iterator it;
        for(it=m_document_model_multihash.constBegin();it!=m_document_model_multihash.constEnd();++it) {
          if (it.value()==model_t) {
            refreshList<<it.key();
          }
        }
      }
    }
    foreach(KTextEditor::Document* doc,refreshList) {
      removeDocument(doc);
    }
    foreach(KTextEditor::Document* doc,refreshList) {
      addDocument(doc);
      kDebug(13040)<<"invoking typeHasChanged(doc)";
      emit typeHasChanged(doc);
    }
  }
#include "katesnippetglobal.moc"
