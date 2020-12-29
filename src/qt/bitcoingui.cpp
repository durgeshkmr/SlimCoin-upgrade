/*
 * Qt4 slimcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2012
 * The SLIMCoin Developers 2011-2013
 */
#include "bitcoingui.h"
#include "transactiontablemodel.h"
#include "addressbookpage.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "encryptdecryptmessagedialog.h"
#include "burncoinsdialog.h"
#include "messagepage.h"
#include "multisigdialog.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "addresstablemodel.h"
#include "transactionview.h"
#include "overviewpage.h"
#include "chatwindow.h"
#include "reportview.h"
#include "inscriptiondialog.h"
#include "miningpage.h"
#include "bitcoinunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "notificator.h"
#include "guiutil.h"
#include "rpcconsole.h"
#include "wallet.h"
#include "bitcoinrpc.h"
#include "version.h"

#ifdef MAC_OSX
#include "macdockiconhandler.h"
#endif

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QIcon>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QStackedWidget>
#include <QDateTime>
#include <QMovie>
#include <QFileDialog>
#include <QDesktopServices>
#include <QTimer>

#include <QDragEnterEvent>
#include <QUrl>
#include <QSplashScreen>

#include "blockbrowser.h"

#include <iostream>

extern CWallet *pwalletMain;

static QSplashScreen *splashref;

BitcoinGUI::BitcoinGUI(QWidget *parent):
    QMainWindow(parent),
    clientModel(0),
    walletModel(0),
    trayIcon(0),
    rpcConsole(0),
    notificator(0),
    encryptWalletAction(0),
    changePassphraseAction(0),
    aboutQtAction(0)
{
    resize(850, 564);
    setWindowTitle(tr("Slimcoin") + " - " + tr("Wallet"));
#ifndef MAC_OSX
    QApplication::setWindowIcon(QIcon(":icons/slimcoin"));
    setWindowIcon(QIcon(":icons/slimcoin"));
#else
    setUnifiedTitleAndToolBarOnMac(true);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    createActions();

    // Create application menu bar
    createMenuBar();
    menuBar()->setNativeMenuBar(false);// menubar on form instead

    // Create the toolbars
    createToolBars();

    // Create the tray icon (or setup the dock icon)
    createTrayIcon();

    // Create tabs
    overviewPage = new OverviewPage();

    miningPage = new MiningPage(this);

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    transactionsPage->setLayout(vbox);

    addressBookPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab);

    receiveCoinsPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab);

    sendCoinsPage = new SendCoinsDialog(this);

    accountReportPage = new ReportView(this);

    blockBrowser = new BlockBrowser(this);

    burnCoinsPage = new BurnCoinsDialog(this);
    connect(burnCoinsAction, SIGNAL(triggered()), burnCoinsPage, SLOT(show()));

    inscriptionDialog = new InscriptionDialog(this);
    connect(inscribeAction, SIGNAL(triggered()), inscriptionDialog, SLOT(show()));

    multisigPage = new MultisigDialog(this);

    messagePage = new SignVerifyMessageDialog(this);

    dataPage = new EncryptDecryptMessageDialog(this);

    chatPage = new ChatWindow(this);

    centralWidget = new QStackedWidget(this);
    centralWidget->addWidget(overviewPage);
    centralWidget->addWidget(miningPage);
    centralWidget->addWidget(transactionsPage);
    centralWidget->addWidget(addressBookPage);
    centralWidget->addWidget(receiveCoinsPage);
    centralWidget->addWidget(sendCoinsPage);
    centralWidget->addWidget(accountReportPage);
#ifdef FIRST_CLASS_MESSAGING
    // centralWidget->addWidget(messagePage);
#endif
    setCentralWidget(centralWidget);

    // Create status bar
    statusBar();

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setMinimumWidth(73);
    frameBlocks->setMaximumWidth(73);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    labelEncryptionIcon = new QLabel();
    labelMiningIcon = new QLabel();
    labelConnectionsIcon = new QLabel();
    labelBlocksIcon = new QLabel();
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelEncryptionIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelMiningIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelConnectionsIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new QProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);

    syncIconMovie = new QMovie(":/movies/update_spinner", "mng", this);

    // Clicking on a transaction on the overview page simply sends you to transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), this, SLOT(gotoHistoryPage()));

    // Doubleclicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    rpcConsole = new RPCConsole(this);
    connect(openRPCConsoleAction, SIGNAL(triggered()), rpcConsole, SLOT(show()));

    connect(blockAction, SIGNAL(triggered()), this, SLOT(gotoBlockBrowser()));

    gotoOverviewPage();
}

BitcoinGUI::~BitcoinGUI()
{
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef MAC_OSX
    delete appMenuBar;
#endif
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);

	// Action Tabs?
    overviewAction = new QAction(QIcon(":/icons/overview"), tr("&Overview"), this);
    overviewAction->setStatusTip(tr("Show general overview of wallet"));
    overviewAction->setToolTip(overviewAction->statusTip());
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(QIcon(":/icons/send"), tr("&" SEND_COINS_DIALOG_NAME), this);
    sendCoinsAction->setStatusTip(tr("Send coins to a SLIMCoin address"));
    sendCoinsAction->setToolTip(sendCoinsAction->statusTip());
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(sendCoinsAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses"), tr("&Receive"), this);
    receiveCoinsAction->setStatusTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setToolTip(receiveCoinsAction->statusTip());
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(receiveCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history"), tr("&Transactions"), this);
    historyAction->setStatusTip(tr("Browse transaction history"));
    historyAction->setToolTip(historyAction->statusTip());
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book"), tr("&Addresses"), this);
    addressBookAction->setStatusTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setToolTip(addressBookAction->statusTip());
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(addressBookAction);

    accountReportAction = new QAction(QIcon(":/icons/account-report"), tr("&Report"), this);
    accountReportAction->setStatusTip(tr("View account reports"));
    accountReportAction->setToolTip(accountReportAction->statusTip());
    accountReportAction->setCheckable(true);
    accountReportAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
    tabGroup->addAction(accountReportAction);

    miningAction = new QAction(QIcon(":/icons/mining"), tr("&Mining"), this);
    miningAction->setStatusTip(tr("Configure mining"));
    miningAction->setToolTip(miningAction->statusTip());
    miningAction->setCheckable(true);
    miningAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_7));
    tabGroup->addAction(miningAction);

    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));
    connect(accountReportAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(accountReportAction, SIGNAL(triggered()), this, SLOT(gotoAccountReportPage()));
    connect(miningAction, SIGNAL(triggered()), this, SLOT(gotoMiningPage()));

    // Dialog items
    blockAction = new QAction(QIcon(":/icons/bex"), tr("Block/Tx &Explorer"), this);
    blockAction->setStatusTip(tr("Explore the blockchain and transactions"));
    blockAction->setToolTip(blockAction->statusTip());

    burnCoinsAction = new QAction(QIcon(":/icons/burn"), tr("&" BURN_COINS_DIALOG_NAME), this);
    burnCoinsAction->setStatusTip(tr("Burn coins from a Slimcoin address"));
    burnCoinsAction->setToolTip(burnCoinsAction->statusTip());
 
    inscribeAction = new QAction(QIcon(":/icons/inscribe"), tr("&Inscribe block"), this);
    inscribeAction->setStatusTip(tr("Inscribe a record"));
    inscribeAction->setToolTip(inscribeAction->statusTip());

    messageAction = new QAction(QIcon(":/icons/edit"), tr("&Sign/Verify"), this);
    messageAction->setStatusTip(tr("Sign/verify messages, prove you control an address"));
    messageAction->setToolTip(messageAction->statusTip());

    dataAction = new QAction(QIcon(":/icons/key"), tr("&Encrypt/Decrypt"), this);
    dataAction->setStatusTip(tr("Encrypt/decrypt data with your Slimcoin addresses"));
    dataAction->setToolTip(dataAction->statusTip());

    multisigAction = new QAction(QIcon(":/icons/send"), tr("&Multisig tools"), this);
    multisigAction->setStatusTip(tr("Multi-signature addresses and transactions."));
    multisigAction->setToolTip(multisigAction->statusTip());

    chatPageAction = new QAction(QIcon(":/icons/chat"), tr("&Chat/Social"), this);
    chatPageAction->setToolTip(tr("IRC Chat tab"));
    chatPageAction->setToolTip(chatPageAction->statusTip());

    checkWalletAction = new QAction(QIcon(":/icons/inspect"), tr("&Check Wallet..."), this);
    checkWalletAction->setStatusTip(tr("Check wallet integrity and report findings"));

    repairWalletAction = new QAction(QIcon(":/icons/repair"), tr("&Repair Wallet..."), this);
    repairWalletAction->setStatusTip(tr("Fix wallet integrity and remove orphans"));

    zapWalletAction = new QAction(QIcon(":/icons/repair"), tr("&Zap Wallet..."), this);
    zapWalletAction->setStatusTip(tr("Zaps txes from wallet then rescans (this is slow)..."));

    connect(blockAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(blockAction, SIGNAL(triggered()), this, SLOT(gotoBlockBrowser()));
    connect(messageAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(messageAction, SIGNAL(triggered()), this, SLOT(gotoMessagePage()));
    connect(dataAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(dataAction, SIGNAL(triggered()), this, SLOT(gotoDataPage()));
    connect(multisigAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(multisigAction, SIGNAL(triggered()), this, SLOT(gotoMultisigPage()));
    connect(chatPageAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(chatPageAction, SIGNAL(triggered()), this, SLOT(gotoChatPage()));

    quitAction = new QAction(QIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setStatusTip(tr("Quit application"));
    quitAction->setToolTip(quitAction->statusTip());
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(QIcon(":/icons/slimcoin"), tr("&About %1").arg(qApp->applicationName()), this);
    aboutAction->setStatusTip(tr("Show information about Slimcoin"));
    aboutAction->setToolTip(aboutAction->statusTip());
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutQtAction = new QAction(QIcon(":/icons/qt"), tr("About &Qt"), this);
    aboutQtAction->setStatusTip(tr("Show information about Qt"));
    aboutQtAction->setToolTip(aboutQtAction->statusTip());
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(QIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setStatusTip(tr("Modify configuration options for SLIMCoin"));
    optionsAction->setToolTip(optionsAction->statusTip());
    optionsAction->setMenuRole(QAction::PreferencesRole);
    toggleHideAction = new QAction(QIcon(":/icons/slimcoin"), tr("Show/Hide &SLIMCoin"), this);
    toggleHideAction->setStatusTip(tr("Show or hide the SLIMCoin window"));
    toggleHideAction->setToolTip(toggleHideAction->statusTip());
    exportAction = new QAction(QIcon(":/icons/export"), tr("&Export..."), this);
    exportAction->setStatusTip(tr("Export the data in the current tab to a file"));
    exportAction->setToolTip(exportAction->statusTip());
    encryptWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Encrypt Wallet"), this);
    encryptWalletAction->setStatusTip(tr("Encrypt or decrypt wallet"));
    encryptWalletAction->setToolTip(encryptWalletAction->statusTip());
    encryptWalletAction->setCheckable(true);
    backupWalletAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup Wallet"), this);
    backupWalletAction->setStatusTip(tr("Backup wallet to another location"));
    backupWalletAction->setToolTip(backupWalletAction->statusTip());
    changePassphraseAction = new QAction(QIcon(":/icons/key"), tr("&Change Passphrase"), this);
    changePassphraseAction->setStatusTip(tr("Change the passphrase used for wallet encryption"));
    changePassphraseAction->setToolTip(changePassphraseAction->statusTip());
    openRPCConsoleAction = new QAction(QIcon(":/icons/debugwindow"), tr("&Debug window"), this);
    openRPCConsoleAction->setStatusTip(tr("Open debugging and diagnostic console"));
    openRPCConsoleAction->setToolTip(openRPCConsoleAction->statusTip());

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(encryptWalletAction, SIGNAL(triggered(bool)), this, SLOT(encryptWallet(bool)));

    connect(checkWalletAction, SIGNAL(triggered()), this, SLOT(checkWallet()));
    connect(repairWalletAction, SIGNAL(triggered()), this, SLOT(repairWallet()));
    connect(zapWalletAction, SIGNAL(triggered()), this, SLOT(zapWallet()));

    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
}

void BitcoinGUI::createMenuBar()
{
#ifdef MAC_OSX
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    file->addAction(backupWalletAction);
    file->addAction(exportAction);
    file->addSeparator();
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    settings->addAction(encryptWalletAction);
    settings->addAction(changePassphraseAction);
    settings->addSeparator();
    settings->addAction(optionsAction);

    QMenu *tools = appMenuBar->addMenu(tr("&Tools"));
    tools->addAction(blockAction);
    tools->addAction(burnCoinsAction);
    tools->addAction(inscribeAction);
    tools->addAction(messageAction);
    tools->addAction(dataAction);
    tools->addAction(multisigAction);
    tools->addAction(chatPageAction);
    tools->addSeparator();
    tools->addAction(checkWalletAction);
    tools->addAction(repairWalletAction);
    tools->addAction(zapWalletAction);

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    help->addAction(openRPCConsoleAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
    help->addSeparator();
}

void BitcoinGUI::createToolBars()
{
    QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(overviewAction);
    toolbar->addAction(sendCoinsAction);
    toolbar->addAction(receiveCoinsAction);
    toolbar->addAction(historyAction);
    toolbar->addAction(addressBookAction);
    toolbar->addAction(accountReportAction);
    toolbar->addAction(miningAction);
#ifdef FIRST_CLASS_MESSAGING
    // toolbar->addAction(messageAction);
#endif
    QToolBar *toolbar2 = addToolBar(tr("Actions toolbar"));
    toolbar2->addAction(burnCoinsAction);
    toolbar2->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar2->addAction(exportAction);
}

void BitcoinGUI::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel)
    {
        if(clientModel->isTestNet())
        {
            QString title_testnet = windowTitle() + QString(" ") + tr("[testnet]");
            setWindowTitle(title_testnet);
#ifndef MAC_OSX
            setWindowIcon(QIcon(":icons/slimcoin_testnet"));
#else
            MacDockIconHandler::instance()->setIcon(QIcon(":icons/slimcoin_testnet"));
#endif
            if(trayIcon)
            {
                trayIcon->setToolTip(title_testnet);
                trayIcon->setIcon(QIcon(":/icons/toolbar_testnet"));
                toggleHideAction->setIcon(QIcon(":/icons/toolbar_testnet"));
            }
            aboutAction->setIcon(QIcon(":/icons/toolbar_testnet"));
        }

        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(clientModel->getNumBlocks(), clientModel->getNumBlocksOfPeers());
        connect(clientModel, SIGNAL(numBlocksChanged(int, int)), this, SLOT(setNumBlocks(int, int)));

        setMining(false, 0);
        connect(clientModel, SIGNAL(miningChanged(bool,int)), this, SLOT(setMining(bool,int)));

        // Report errors from network/worker thread
        connect(clientModel, SIGNAL(error(QString,QString, bool)), this, SLOT(error(QString,QString,bool)));

        overviewPage->setClientModel(clientModel);
        rpcConsole->setClientModel(clientModel);
        accountReportPage->setClientModel(clientModel);
        inscriptionDialog->setClientModel(clientModel);
        chatPage->setModel(clientModel);
    }
}

void BitcoinGUI::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if(walletModel)
    {
        // Report errors from wallet thread
        connect(walletModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        // Put transaction list in tabs
        transactionView->setModel(walletModel);

        overviewPage->setModel(walletModel);
        addressBookPage->setModel(walletModel->getAddressTableModel());
        receiveCoinsPage->setModel(walletModel->getAddressTableModel());
        sendCoinsPage->setModel(walletModel);
        burnCoinsPage->setModel(walletModel);
        accountReportPage->setModel(walletModel);
        messagePage->setModel(walletModel);
        dataPage->setModel(walletModel);
        inscriptionDialog->setWalletModel(walletModel);
        multisigPage->setModel(walletModel);
        miningPage->setClientModel(clientModel);

        setEncryptionStatus(walletModel->getEncryptionStatus());
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));

        // Balloon popup for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));
    }
}

void BitcoinGUI::createTrayIcon()
{
    QMenu *trayIconMenu;
#ifndef MAC_OSX
    trayIcon = new QSystemTrayIcon(this);
    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip(tr("Slimcoin client"));
    trayIcon->setIcon(QIcon(":/icons/toolbar"));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    trayIcon->show();
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(messageAction);
    trayIconMenu->addAction(dataAction);
//#ifndef FIRST_CLASS_MESSAGING
    trayIconMenu->addSeparator();
//#endif
    trayIconMenu->addAction(receiveCoinsAction);
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(burnCoinsAction);
    trayIconMenu->addAction(multisigAction);
    trayIconMenu->addAction(inscribeAction);
    trayIconMenu->addAction(blockAction);
    trayIconMenu->addAction(chatPageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef MAC_OSX // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
    notificator = new Notificator(tr("SLIMCoin-qt"), trayIcon);
}

#ifndef MAC_OSX
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers "show/hide bitcoin"
        toggleHideAction->trigger();
    }
}
#endif

void BitcoinGUI::toggleHidden()
{
    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else
        hide();
}

void BitcoinGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;
    OptionsDialog dlg(this);
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::aboutClicked()
{
    AboutDialog dlg(this);
    dlg.setModel(clientModel);
    dlg.exec();
}

void BitcoinGUI::setNumConnections(int count)
{
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }
    labelConnectionsIcon->setPixmap(QIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    labelConnectionsIcon->setToolTip(tr("%n active connection(s) to Slimcoin network", "", count));
}

void BitcoinGUI::setNumBlocks(int count, int nTotalBlocks)
{
    // don't show / hide progressBar and it's label if we have no connection(s) to the network
    if (!clientModel || clientModel->getNumConnections() == 0)
    {
        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);

        return;
    }

    QString strStatusBarWarnings = clientModel->getStatusBarWarnings();
    QString tooltip;

    if(count < nTotalBlocks)
    {
        int nRemainingBlocks = nTotalBlocks - count;
        float nPercentageDone = count / (nTotalBlocks * 0.01f);

        if (clientModel->getStatusBarWarnings() == "")
        {
            progressBarLabel->setText(tr("Synchronizing with network..."));
            progressBarLabel->setVisible(true);
            progressBar->setFormat(tr("~%n block(s) remaining", "", nRemainingBlocks));
            progressBar->setMaximum(nTotalBlocks);
            progressBar->setValue(count);
            progressBar->setVisible(true);
        }
        else
        {
            progressBarLabel->setText(clientModel->getStatusBarWarnings());
            progressBarLabel->setVisible(true);
            progressBar->setVisible(false);
        }
        tooltip = tr("Downloaded %1 of %2 blocks of transaction history (%3% done).").arg(count).arg(nTotalBlocks).arg(nPercentageDone, 0, 'f', 2);
    }
    else
    {
        if (clientModel->getStatusBarWarnings() == "")
            progressBarLabel->setVisible(false);
        else
        {
            progressBarLabel->setText(clientModel->getStatusBarWarnings());
            progressBarLabel->setVisible(true);
        }
        progressBar->setVisible(false);
        tooltip = tr("Downloaded %1 blocks of transaction history.").arg(count);
    }

    tooltip = tr("Current difficulty is %1.").arg(clientModel->GetDifficulty()) + QString("\n") + tooltip;

    QDateTime now = QDateTime::currentDateTime();
    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    int secs = lastBlockDate.secsTo(now);
    QString text;

    // Represent time from last generated block in human readable text
    if(secs <= 0)
    {
        // Fully up to date. Leave text empty.
    }
    else if(secs < 60)
    {
        text = tr("%n second(s) ago","",secs);
        /* FIXME: choose which approach to take (see below)
        if(GetBoolArg("-chart", true))
        {
            miningPage->updatePlot();
            overviewPage->updatePlot(count);
        }
        */
    }
    else if(secs < 60*60)
    {
        text = tr("%n minute(s) ago","",secs/60);
    }
    else if(secs < 24*60*60)
    {
        text = tr("%n hour(s) ago","",secs/(60*60));
    }
    else
    {
        text = tr("%n day(s) ago","",secs/(60*60*24));
    }

    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60 && count >= nTotalBlocks)
    {
        tooltip = tr("Up to date") + QString(".\n") + tooltip;
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
    }
    else
    {
        tooltip = tr("Catching up...") + QString("\n") + tooltip;
        labelBlocksIcon->setMovie(syncIconMovie);
        syncIconMovie->start();
    }

    if(!text.isEmpty())
    {
        tooltip += QString("\n");
        tooltip += tr("Last received block was generated %1.").arg(text);
    }

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);

    /* FIXME: choose which approach to take (see above) */
    if(count > 0 && nTotalBlocks > 0 && count >= nTotalBlocks)
    {
        if(GetBoolArg("-chart", true))
        {
            miningPage->updatePlot();
            overviewPage->updatePlot(count);
        }
    }
}

void BitcoinGUI::setMining(bool mining, int hashrate)
{
    if (mining)
    {
        labelMiningIcon->setPixmap(QIcon(":/icons/mining_active").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelMiningIcon->setToolTip(tr("Mining SLIMCoin at %1 hashes per second").arg(hashrate));
    }
    else
    {
        labelMiningIcon->setPixmap(QIcon(":/icons/mining_inactive").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelMiningIcon->setToolTip(tr("Not mining SLIMCoin"));
    }
}

void BitcoinGUI::error(const QString &title, const QString &message, bool modal)
{
    // Report errors from network/worker thread
    if(modal)
    {
        QMessageBox::critical(this, title, message, QMessageBox::Ok, QMessageBox::Ok);
    } else {
        notificator->notify(Notificator::Critical, title, message);
    }
}

void BitcoinGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef MAC_OSX // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
    if(clientModel)
    {
#ifndef MAC_OSX // Ignored on Mac
        if(!clientModel->getOptionsModel()->getMinimizeToTray() &&
           !clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            qApp->quit();
        }
#endif
    }
    QMainWindow::closeEvent(event);
}

void BitcoinGUI::askFee(qint64 nFeeRequired, bool *payFee)
{
    QString strMessage =
        tr("This transaction is over the size limit.  You can still send it for a fee of %1, "
          "which goes to the nodes that process your transaction and helps to support the network.  "
          "Do you want to pay the fee?").arg(
                BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nFeeRequired));
    QMessageBox::StandardButton retval = QMessageBox::question(
          this, tr("Confirm transaction fee"), strMessage,
          QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Yes);
    *payFee = (retval == QMessageBox::Yes);
}

void BitcoinGUI::incomingTransaction(const QModelIndex & parent, int start, int end)
{
    if(!walletModel || !clientModel)
        return;
    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent)
                    .data(Qt::EditRole).toULongLong();
    if(!clientModel->inInitialBlockDownload())
    {
        // On new transaction, make an info balloon
        // Unless the initial block download is in progress, to prevent balloon-spam
        QString date = ttm->index(start, TransactionTableModel::Date, parent)
                        .data().toString();
        QString type = ttm->index(start, TransactionTableModel::Type, parent)
                        .data().toString();
        QString address = ttm->index(start, TransactionTableModel::ToAddress, parent)
                        .data().toString();
        QIcon icon = qvariant_cast<QIcon>(ttm->index(start,
                            TransactionTableModel::ToAddress, parent)
                        .data(Qt::DecorationRole));

        notificator->notify(Notificator::Information,
                            (amount)<0 ? tr("Sent transaction") :
                                         tr("Incoming transaction"),
                              tr("Date: %1\n"
                                 "Amount: %2\n"
                                 "Type: %3\n"
                                 "Address: %4\n")
                              .arg(date)
                              .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), amount, true))
                              .arg(type)
                              .arg(address), icon);
    }
}

void BitcoinGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    centralWidget->setCurrentWidget(overviewPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoMiningPage()
{
    miningAction->setChecked(true);
    centralWidget->setCurrentWidget(miningPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    centralWidget->setCurrentWidget(transactionsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));
}

void BitcoinGUI::gotoAddressBookPage()
{
    addressBookAction->setChecked(true);
    centralWidget->setCurrentWidget(addressBookPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), addressBookPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    centralWidget->setCurrentWidget(receiveCoinsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), receiveCoinsPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoSendCoinsPage()
{
    sendCoinsAction->setChecked(true);
    centralWidget->setCurrentWidget(sendCoinsPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoAccountReportPage()
{
    accountReportAction->setChecked(true);
    centralWidget->setCurrentWidget(accountReportPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));
}

void BitcoinGUI::gotoBlockBrowser()
{
    blockBrowser->show();
    blockBrowser->setFocus();
}

void BitcoinGUI::gotoBurnCoinsPage()
{
  burnCoinsPage->show();
  burnCoinsPage->setFocus();
}

void BitcoinGUI::gotoInscriptionDialog()
{
    inscriptionDialog->show();
    inscriptionDialog->setFocus();
}

void BitcoinGUI::gotoMessagePage()
{
    messagePage->show();
    messagePage->setFocus();
}

void BitcoinGUI::gotoMessagePage(QString addr)
{
    gotoMessagePage();
    messagePage->setAddress_SM(addr);
}

void BitcoinGUI::gotoDataPage()
{
    dataPage->show();
    dataPage->setFocus();
}

void BitcoinGUI::gotoDataPage(QString addr)
{
    gotoDataPage();
    dataPage->setAddress_ED(addr);
}

void BitcoinGUI::gotoMultisigPage()
{
    multisigPage->show();
    multisigPage->setFocus();
}

void BitcoinGUI::gotoChatPage()
{
  chatPage->show();
  chatPage->setFocus();
}

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        gotoSendCoinsPage();
        QList<QUrl> uris = event->mimeData()->urls();
        foreach(const QUrl &uri, uris)
        {
            sendCoinsPage->handleURI(uri.toString());
        }
    }

    event->acceptProposedAction();
}

void BitcoinGUI::handleURI(QString strURI)
{
    gotoSendCoinsPage();
    sendCoinsPage->handleURI(strURI);

    if(!isActiveWindow())
        activateWindow();

    showNormalIfMinimized();
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(fWalletUnlockMintOnly? tr("Wallet is <b>encrypted</b> and currently <b>unlocked for block minting only</b>") : tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::Locked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    }
}

void BitcoinGUI::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt:
                                     AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    setEncryptionStatus(walletModel->getEncryptionStatus());
}

void BitcoinGUI::checkWallet()
{

    int nMismatchSpent;
    int64 nBalanceInQuestion;
    int nOrphansFound;

    if(!walletModel)
        return;

    // Check the wallet as requested by user
    walletModel->checkWallet(nMismatchSpent, nBalanceInQuestion, nOrphansFound);

    if (nMismatchSpent == 0 && nOrphansFound == 0)
        error(tr("Check Wallet Information"),
                tr("Wallet passed integrity test!\n"
                   "Nothing found to fix."),true);
  else
       error(tr("Check Wallet Information"),
               tr("Wallet failed integrity test!\n\n"
                  "Mismatched coin(s) found: %1.\n"
                  "Amount in question: %2.\n"
                  "Orphans found: %3.\n\n"
                  "Please backup wallet and run repair wallet.\n")
                        .arg(nMismatchSpent)
                        .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
                        .arg(nOrphansFound),true);
}

void BitcoinGUI::repairWallet()
{
    int nMismatchSpent;
    int64 nBalanceInQuestion;
    int nOrphansFound;

    if(!walletModel)
        return;

    // Repair the wallet as requested by user
    walletModel->repairWallet(nMismatchSpent, nBalanceInQuestion, nOrphansFound);

    if (nMismatchSpent == 0 && nOrphansFound == 0)
       error(tr("Repair Wallet Information"),
               tr("Wallet passed integrity test!\n"
                  "Nothing found to fix."),true);
    else
       error(tr("Repair Wallet Information"),
               tr("Wallet failed integrity test and has been repaired!\n"
                  "Mismatched coin(s) found: %1\n"
                  "Amount affected by repair: %2\n"
                  "Orphans removed: %3\n")
                        .arg(nMismatchSpent)
                        .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
                        .arg(nOrphansFound),true);
}

void BitcoinGUI::zapWallet()
{
  if(!walletModel)
    return;

  progressBarLabel->setText(tr("Starting zapwallettxes..."));
  progressBarLabel->setVisible(true);

  // bring up splash screen
  QSplashScreen splash(QPixmap(":/images/splash"), 0);
  splash.setEnabled(false);
  splash.show();
  splash.setAutoFillBackground(true);
  splashref = &splash;

  // Zap the wallet as requested by user
  // 1= save meta data
  // 2=remove meta data needed to restore wallet transaction meta data after -zapwallettxes
  std::vector<CWalletTx> vWtx;

  progressBarLabel->setText(tr("Zapping all transactions from wallet..."));
  splashMessage(_("Zapping all transactions from wallet..."));
  printf("Zapping all transactions from wallet...\n");

// clear tables

  pwalletMain = new CWallet("wallet.dat");
  int nZapWalletRet = pwalletMain->ZapWalletTx(vWtx);
  if (nZapWalletRet != 0)
  {
    progressBarLabel->setText(tr("Error loading wallet.dat: Wallet corrupted."));
    splashMessage(_("Error loading wallet.dat: Wallet corrupted"));
    printf("Error loading wallet.dat: Wallet corrupted\n");
    if (splashref)
      splash.close();
    return;
  }

  delete pwalletMain;
  pwalletMain = NULL;

  progressBarLabel->setText(tr("Loading wallet..."));
  splashMessage(_("Loading wallet..."));
  printf("Loading wallet...\n");

  int64 nStart = GetTimeMillis();
  bool fFirstRun = true;
  pwalletMain = new CWallet("wallet.dat");


  int nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
  if (nLoadWalletRet != 0)
  {
    if (nLoadWalletRet == 2)
    {
      progressBarLabel->setText(tr("Error loading wallet.dat: Wallet corrupted."));
      splashMessage(_("Error loading wallet.dat: Wallet corrupted"));
      printf("Error loading wallet.dat: Wallet corrupted\n");
    }
    else if (nLoadWalletRet == 3)
    {
      setStatusTip(tr("Warning: error reading wallet.dat! All keys read correctly, but transaction data or address book entries might be missing or incorrect."));
      progressBarLabel->setText(tr("Warning - error reading wallet."));
      printf("Warning: error reading wallet.dat! All keys read correctly, but transaction data or address book entries might be missing or incorrect.\n");
    }
    else if (nLoadWalletRet == 4)
    {
      progressBarLabel->setText(tr("Error loading wallet.dat: Please check for a newer version of BitBar."));
      setStatusTip(tr("Error loading wallet.dat: Wallet requires newer version of BitBar"));
      printf("Error loading wallet.dat: Wallet requires newer version of BitBar\n");
    }
    else if (nLoadWalletRet == 5)
    {
  progressBarLabel->setText(tr("Wallet needs to be rewriten. Please restart BitBar to complete."));
      setStatusTip(tr("Wallet needed to be rewritten: restart BitBar to complete"));
      printf("Wallet needed to be rewritten: restart BitBar to complete\n");
      if (splashref)
        splash.close();
      return;
    }
    else
    {
      progressBarLabel->setText(tr("Error laoding wallet.dat"));
      setStatusTip(tr("Error loading wallet.dat"));
      printf("Error loading wallet.dat\n");
    } 
  }
  
  progressBarLabel->setText(tr("Wallet loaded..."));
  splashMessage(_("Wallet loaded..."));
  printf(" zap wallet  load     %15lld ms\n", GetTimeMillis() - nStart);

  progressBarLabel->setText(tr("Loading lables..."));
  splashMessage(_("Loaded lables..."));
  printf(" zap wallet  loading metadata\n");

  // Restore wallet transaction metadata after -zapwallettxes=1
  BOOST_FOREACH(const CWalletTx& wtxOld, vWtx)
  {
    uint256 hash = wtxOld.GetHash();
    std::map<uint256, CWalletTx>::iterator mi = pwalletMain->mapWallet.find(hash);
    if (mi != pwalletMain->mapWallet.end())
    {
      const CWalletTx* copyFrom = &wtxOld;
      CWalletTx* copyTo = &mi->second;
      copyTo->mapValue = copyFrom->mapValue;
      copyTo->vOrderForm = copyFrom->vOrderForm;
      copyTo->nTimeReceived = copyFrom->nTimeReceived;
      copyTo->nTimeSmart = copyFrom->nTimeSmart;
      copyTo->fFromMe = copyFrom->fFromMe;
      copyTo->strFromAccount = copyFrom->strFromAccount;
      copyTo->nOrderPos = copyFrom->nOrderPos;
      copyTo->WriteToDisk();
    }
  }
  progressBarLabel->setText(tr("Scanning for transactions..."));
  splashMessage(_("scanning for transactions..."));
  printf(" zap wallet  scanning for transactions\n");

  pwalletMain->ScanForWalletTransactions(pindexGenesisBlock, true);
  pwalletMain->ReacceptWalletTransactions();
  progressBarLabel->setText(tr("Please restart your wallet."));
  splashMessage(_("Please restart your wallet."));
  printf(" zap wallet  done - please restart wallet.\n");
  //  sleep (10);
  progressBarLabel->setText(tr(""));
  progressBarLabel->setVisible(false);

  //  close splash screen
  if (splashref)
    splash.close();

  QMessageBox::warning(this, tr("Zap Wallet Finished."), tr("Please restart your wallet for changes to take effect."));
}

void BitcoinGUI::splashMessage(const std::string &message)
{
  if(splashref)
  {
    splashref->showMessage(QString::fromStdString(message), Qt::AlignBottom|Qt::AlignHCenter, QColor(120,80,25));
    QApplication::instance()->processEvents();
  }
}

void BitcoinGUI::backupWallet()
{
    QString saveDir = GetDataDir().string().c_str();
    QString filename = QFileDialog::getSaveFileName(this, tr("Backup Wallet"), saveDir, tr("Wallet Data (*.dat)"));
    if(!filename.isEmpty()) {
        if(!walletModel->backupWallet(filename)) {
            QMessageBox::warning(this, tr("Backup Failed"), tr("There was an error trying to save the wallet data to the new location."));
        }
    }
}

void BitcoinGUI::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void BitcoinGUI::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void BitcoinGUI::showNormalIfMinimized()
{
    if(!isVisible()) // Show, if hidden
        show();
    if(isMinimized()) // Unminimize, if minimized
        showNormal();
}
