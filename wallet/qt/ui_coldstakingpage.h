#ifndef UI_COLDSTAKING_H
#define UI_COLDSTAKING_H

#include <QPushButton>
#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListView>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "ClickableLabel.h"
#include <QMovie>

QT_BEGIN_NAMESPACE

class ColdStakingListView : public QListView
{
    Q_OBJECT
public:
    explicit ColdStakingListView(QWidget* parent = Q_NULLPTR) : QListView(parent) {}
    virtual ~ColdStakingListView() {}
    QModelIndexList selectedIndexesP() const { return selectedIndexes(); }
};

class Ui_ColdStaking
{
public:
    QPixmap      left_logo_pix;
    QGridLayout* main_layout;
    QVBoxLayout* left_logo_layout;
    QLabel*      left_logo_label;
    QVBoxLayout* logo_layout;
    QVBoxLayout* right_balance_layout;
    QFrame*      wallet_contents_frame;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* verticalLayoutContent;
    QHBoxLayout* horizontalLayout_2;
    QLabel*      upper_table_label;
    QLineEdit*   filter_lineEdit;

    ColdStakingListView* listColdStakingView;

    QWidget*     bottom_bar_widget;
    QLabel*      bottom_bar_logo_label;
    QGridLayout* bottom_layout;
    QPixmap      bottom_logo_pix;

    QPushButton* delegateStakeButton;

    int bottom_bar_downscale_factor;

    void setupUi(QWidget* ColdStakingPage)
    {
        if (ColdStakingPage->objectName().isEmpty())
            ColdStakingPage->setObjectName(QStringLiteral("ColdStakingPage"));
        ColdStakingPage->resize(761, 452);
        main_layout = new QGridLayout(ColdStakingPage);
        main_layout->setObjectName(QStringLiteral("horizontalLayout"));
        left_logo_layout = new QVBoxLayout();
        left_logo_layout->setObjectName(QStringLiteral("verticalLayout_2"));
        left_logo_label = new QLabel(ColdStakingPage);
        left_logo_label->setObjectName(QStringLiteral("frame"));

        //        logo_label->setFrameShadow(QFrame::Raised);
        left_logo_label->setLineWidth(0);
        //        logo_label->setFrameStyle(QFrame::StyledPanel);

        left_logo_pix = QPixmap(":images/neblio_vertical");
        left_logo_pix =
            left_logo_pix.scaledToHeight(ColdStakingPage->height() * 3. / 4., Qt::SmoothTransformation);
        left_logo_label->setPixmap(left_logo_pix);
        left_logo_label->setAlignment(Qt::AlignCenter);

        logo_layout = new QVBoxLayout(left_logo_label);
        logo_layout->setObjectName(QStringLiteral("verticalLayout_4"));

        left_logo_layout->addWidget(left_logo_label);

        main_layout->addLayout(left_logo_layout, 0, 0, 1, 1);

        bottom_logo_pix             = QPixmap(":images/neblio_horizontal");
        bottom_bar_widget           = new QWidget(ColdStakingPage);
        bottom_layout               = new QGridLayout(bottom_bar_widget);
        bottom_bar_logo_label       = new QLabel(bottom_bar_widget);
        bottom_bar_downscale_factor = 8;

        main_layout->addWidget(bottom_bar_widget, 1, 0, 1, 2);
        bottom_bar_widget->setLayout(bottom_layout);
        bottom_layout->addWidget(bottom_bar_logo_label, 0, 0, 1, 1);
        bottom_logo_pix = bottom_logo_pix.scaledToHeight(
            ColdStakingPage->height() / bottom_bar_downscale_factor, Qt::SmoothTransformation);
        bottom_bar_logo_label->setPixmap(bottom_logo_pix);
        bottom_bar_logo_label->setAlignment(Qt::AlignRight);

        right_balance_layout = new QVBoxLayout();
        right_balance_layout->setObjectName(QStringLiteral("verticalLayout_3"));
        wallet_contents_frame = new QFrame(ColdStakingPage);
        wallet_contents_frame->setObjectName(QStringLiteral("frame_2"));
        wallet_contents_frame->setFrameShape(QFrame::StyledPanel);
        // wallet_contents_frame->setFrameShadow(QFrame::Raised);
        verticalLayout        = new QVBoxLayout(wallet_contents_frame);
        verticalLayoutContent = new QHBoxLayout;
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        // showSendDialogButton = new QPushButton;
        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QStringLiteral("horizontalLayout_2"));
        upper_table_label = new QLabel(wallet_contents_frame);
        upper_table_label->setObjectName(QStringLiteral("label_4"));

        filter_lineEdit = new QLineEdit(wallet_contents_frame);
        filter_lineEdit->setPlaceholderText("Filter (Ctrl+F)");

        horizontalLayout_2->addWidget(upper_table_label);

        delegateStakeButton = new QPushButton();
        delegateStakeButton->setIcon(QIcon(":/icons/cold_delegate_add"));
        delegateStakeButton->setToolTip("Create new stake delegation");

        horizontalLayout_2->addWidget(delegateStakeButton, 0, Qt::AlignRight);

        //        labelBlockchainSyncStatus = new QLabel(wallet_contents_frame);
        //        labelBlockchainSyncStatus->setObjectName(QStringLiteral("labelBlockchainSyncStatus"));
        //        labelBlockchainSyncStatus->setStyleSheet(QStringLiteral("QLabel { color: #7F4BC8; }"));
        //        labelBlockchainSyncStatus->setText(QStringLiteral("(blockchain out of sync)"));
        //        labelBlockchainSyncStatus->setAlignment(Qt::AlignRight | Qt::AlignTrailing |
        //        Qt::AlignVCenter);

        //        horizontalLayout_2->addWidget(labelBlockchainSyncStatus);

        verticalLayout->addLayout(horizontalLayout_2);
        verticalLayout->addLayout(verticalLayoutContent);
        verticalLayoutContent->addWidget(filter_lineEdit);
        //        verticalLayoutContent->addWidget(showSendDialogButton);

        listColdStakingView = new ColdStakingListView(wallet_contents_frame);
        listColdStakingView->setObjectName(QStringLiteral("listColdStakingView"));
        listColdStakingView->setStyleSheet(QStringLiteral("QListView { background: transparent; }"));
        listColdStakingView->setFrameShape(QFrame::NoFrame);
        //        listTokens->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        //        listTokens->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        //        listTokens->setSelectionMode(QAbstractItemView::NoSelection);

        verticalLayout->addWidget(listColdStakingView);

        right_balance_layout->addWidget(wallet_contents_frame);

        main_layout->addLayout(right_balance_layout, 0, 1, 1, 1);

        retranslateUi(ColdStakingPage);

        QMetaObject::connectSlotsByName(ColdStakingPage);
    } // setupUi

    void retranslateUi(QWidget* ColdStakingPage)
    {
        ColdStakingPage->setWindowTitle(QApplication::translate("Cold-staking", "Form", Q_NULLPTR));
        upper_table_label->setText(QApplication::translate(
            "NTP1Summary", "<b>Cold Staking Addresses</b>", Q_NULLPTR));
    } // retranslateUi
};

QT_END_NAMESPACE

#endif // UI_COLDSTAKING_H
