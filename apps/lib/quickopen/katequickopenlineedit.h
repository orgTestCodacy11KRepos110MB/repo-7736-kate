/*
    SPDX-FileCopyrightText: 2020 Waqar Ahmed <waqar.17a@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef QUICKOPENLINEEDIT_H
#define QUICKOPENLINEEDIT_H

#include <QLineEdit>
#include <memory>

#include "katequickopenmodel.h"

class QuickOpenLineEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit QuickOpenLineEdit(QWidget *parent);
    ~QuickOpenLineEdit() override;

    KateQuickOpenModelList listMode() const
    {
        return m_listMode;
    }

    KateQuickOpenSearchMode sortMode() const
    {
        return m_sortMode;
    }

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void setupMenu();

private:
    std::unique_ptr<QMenu> menu;
    KateQuickOpenModelList m_listMode;
    KateQuickOpenSearchMode m_sortMode;

Q_SIGNALS:
    void listModeChanged(KateQuickOpenModelList mode);
    void searchModeChanged(KateQuickOpenSearchMode mode);
};

#endif // QUICKOPENLINEEDIT_H
