/*
 * W.J. van der Laan 20011-2012
 */
#include "bitcoingui.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "optionsmodel.h"

#include "qtipcserver.h"

#include <QApplication>
#include <QMessageBox>
#include <QThread>
#include <QTextCodec>
#include <QLocale>
#include <QTranslator>
#include <QSplashScreen>
#include <QLibraryInfo>

#include <coinChain/Node.h>
#include <coinChain/NodeRPC.h>

#include <coinHTTP/Server.h>
#include <coinHTTP/Client.h>

#include <coinWallet/Wallet.h>
#include <coinWallet/WalletRPC.h>

#include <boost/thread.hpp>
#include <boost/program_options.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>

using namespace std;
using namespace boost;
using namespace boost::program_options;

// Need a global reference for the notifications to find the GUI
BitcoinGUI *guiref;
QSplashScreen *splashref;
/*
int MyMessageBox(const std::string& message, const std::string& caption, int style, wxWindow* parent, int x, int y)
{
    // Message from AppInit2(), always in main thread before main window is constructed
    QMessageBox::critical(0, QString::fromStdString(caption),
        QString::fromStdString(message),
        QMessageBox::Ok, QMessageBox::Ok);
    return 4;
}

int ThreadSafeMessageBox(const std::string& message, const std::string& caption, int style, wxWindow* parent, int x, int y)
{
    // Message from network thread
    if(guiref)
    {
        QMetaObject::invokeMethod(guiref, "error", Qt::QueuedConnection,
                                   Q_ARG(QString, QString::fromStdString(caption)),
                                   Q_ARG(QString, QString::fromStdString(message)));
    }
    else
    {
        printf("%s: %s\n", caption.c_str(), message.c_str());
        fprintf(stderr, "%s: %s\n", caption.c_str(), message.c_str());
    }
    return 4;
}

bool ThreadSafeAskFee(int64 nFeeRequired, const std::string& strCaption, wxWindow* parent)
{
    if(!guiref)
        return false;
    if(nFeeRequired < MIN_TX_FEE || nFeeRequired <= nTransactionFee || fDaemon)
        return true;
    bool payFee = false;

    // Call slot on GUI thread.
    // If called from another thread, use a blocking QueuedConnection.
    Qt::ConnectionType connectionType = Qt::DirectConnection;
    if(QThread::currentThread() != QCoreApplication::instance()->thread())
    {
        connectionType = Qt::BlockingQueuedConnection;
    }

    QMetaObject::invokeMethod(guiref, "askFee", connectionType,
                               Q_ARG(qint64, nFeeRequired),
                               Q_ARG(bool*, &payFee));

    return payFee;
}

void ThreadSafeHandleURL(const std::string& strURL)
{
    if(!guiref)
        return;

    // Call slot on GUI thread.
    // If called from another thread, use a blocking QueuedConnection.
    Qt::ConnectionType connectionType = Qt::DirectConnection;
    if(QThread::currentThread() != QCoreApplication::instance()->thread())
    {
        connectionType = Qt::BlockingQueuedConnection;
    }
    QMetaObject::invokeMethod(guiref, "handleURL", connectionType,
                               Q_ARG(QString, QString::fromStdString(strURL)));
}
*/
void CalledSetStatusBar(const std::string& strText, int nField)
{
    // Only used for built-in mining, which is disabled, simple ignore
}

void UIThreadCall(boost::function0<void> fn)
{
    // Only used for built-in mining, which is disabled, simple ignore
}

void MainFrameRepaint()
{
    if(guiref)
        QMetaObject::invokeMethod(guiref, "refreshStatusBar", Qt::QueuedConnection);
}

void InitMessage(const std::string &message)
{
    if(splashref)
    {
        splashref->showMessage(QString::fromStdString(message), Qt::AlignBottom|Qt::AlignHCenter, QColor(255,255,200));
        QApplication::instance()->processEvents();
    }
}

/*
   Translate string to current locale using Qt.
 */
std::string _(const char* psz)
{
    return QCoreApplication::translate("bitcoin-core", psz).toStdString();
}

#ifndef BITCOIN_QT_TEST
int main(int argc, char *argv[])
{
    // Do this early as we don't want to bother initializing if we are just calling IPC
    for (int i = 1; i < argc; i++)
    {
        if (strlen(argv[i]) > 7 && strncasecmp(argv[i], "bitcoin:", 8) == 0)
        {
            const char *strURL = argv[i];
            try {
                boost::interprocess::message_queue mq(boost::interprocess::open_only, "BitcoinURL");
                if(mq.try_send(strURL, strlen(strURL), 0))
                    exit(0);
                else
                    break;
            }
            catch (boost::interprocess::interprocess_exception &ex) {
                break;
            }
        }
    }

    // Internal string conversion is all UTF-8
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForTr());

    Q_INIT_RESOURCE(bitcoin);
    QApplication app(argc, argv);

    string config_file, data_dir, locale;
    unsigned short rpc_port;
    string rpc_bind, rpc_connect, rpc_user, rpc_pass;
    typedef vector<string> strings;
    strings rpc_params;
    strings connect_peers;
    strings add_peers;
    bool gen, ssl;
    string certchain, privkey;

    // Commandline options
    options_description generic("Generic options");
    generic.add_options()
        ("help,?", "Show help messages")
        ("version,v", "print version string")
        ("conf,c", value<string>(&config_file)->default_value("bitcoin.conf"), "Specify configuration file")
        ("datadir", "Specify non default data directory")
    ;

    options_description config("Config options");
    config.add_options()
        ("pid", value<string>(), "Specify pid file (default: bitcoind.pid)")
        ("nolisten", "Don't accept connections from outside")
        ("testnet", "Use the test network")
        ("addnode", value<strings>(&add_peers), "Add a node to connect to")
        ("connect", value<strings>(&connect_peers), "Connect only to the specified node")
        ("rpcuser", value<string>(&rpc_user), "Username for JSON-RPC connections")
        ("rpcpassword", value<string>(&rpc_pass), "Password for JSON-RPC connections")
        ("rpcport", value<unsigned short>(&rpc_port)->default_value(8332), "Listen for JSON-RPC connections on <arg>")
        ("rpcallowip", value<string>(&rpc_bind)->default_value(asio::ip::address_v4::loopback().to_string()), "Allow JSON-RPC connections from specified IP address")
        ("rpcconnect", value<string>(&rpc_connect)->default_value(asio::ip::address_v4::loopback().to_string()), "Send commands to node running on <arg>")
        ("keypool", value<unsigned short>(), "Set key pool size to <arg>")
        ("rescan", "Rescan the block chain for missing wallet transactions")
        ("gen", value<bool>(&gen)->default_value(false), "Generate coins")
        ("rpcssl", value<bool>(&ssl)->default_value(false), "Use OpenSSL (https) for JSON-RPC connections")
        ("rpcsslcertificatechainfile", value<string>(&certchain)->default_value("server.cert"), "Server certificate file")
        ("rpcsslprivatekeyfile", value<string>(&privkey)->default_value("server.pem"), "Server private key")
        ("lang", value<string>(&locale)->default_value(QLocale::system().name().toStdString()), "Specify locate - e.g. \"en_US\"")
    ;

    options_description hidden("Hidden options");
    hidden.add_options()("params", value<strings>(&rpc_params), "Run JSON RPC command");

    options_description cmdline_options;
    cmdline_options.add(generic).add(config).add(hidden);

    options_description config_file_options;
    config_file_options.add(config);

    options_description visible;
    visible.add(generic).add(config);

    positional_options_description pos;
    pos.add("params", -1);


    // parse the command line
    variables_map args;
    store(command_line_parser(argc, argv).options(cmdline_options).positional(pos).run(), args);
    notify(args);

    if (args.count("help")) {
        cout << "Usage: " << argv[0] << " [options] [rpc-command param1 param2 ...]\n";
        cout << visible << "\n";
        cout << "If no rpc-command is specified, " << argv[0] << " start up as a daemon, otherwise it offers commandline access to a running instance\n";
        return 1;
    }

    if (args.count("version")) {
        cout << argv[0] << " version is: " << FormatFullVersion() << "\n";
        return 1;
    }

    if(!args.count("datadir"))
        data_dir = CDB::dataDir(bitcoin.dataDirSuffix());

    // if present, parse the config file - if no data dir is specified we always assume bitcoin chain at this stage
    string config_path = data_dir + "/" + config_file;
    cout << config_path << endl;
    ifstream ifs(config_path.c_str());
    if(ifs) {
        store(parse_config_file(ifs, config_file_options, true), args);
        notify(args);
    }

    Auth auth(rpc_user, rpc_pass); // if rpc_user and rpc_pass are not set, all authenticated methods becomes disallowed.

    try {
        // If we have params on the cmdline we run as a command line client contacting a server
        if (args.count("params")) {
            string rpc_method = rpc_params[0];
            rpc_params.erase(rpc_params.begin());
            // create URL
            string url = "http://" + rpc_connect + ":" + lexical_cast<string>(rpc_port);
            if(ssl) url = "https://" + rpc_connect + ":" + lexical_cast<string>(rpc_port);
            Client client;
            // this is a blocking post!
            Reply reply = client.post(url, RPC::content(rpc_method, rpc_params), auth.headers());
            if(reply.status == Reply::ok) {
                json_spirit::Object rpc_reply = RPC::reply(reply.content);
                json_spirit::Value result = json_spirit::find_value(rpc_reply, "result");
                cout << json_spirit::write_formatted(result) << "\n";
                return 0;
            }
            else {
                cout << "HTTP error code: " << reply.status << "\n";
                json_spirit::Object rpc_reply = RPC::reply(reply.content);
                json_spirit::Object rpc_error = json_spirit::find_value(rpc_reply, "error").get_obj();
                json_spirit::Value code = json_spirit::find_value(rpc_error, "code");
                json_spirit::Value message = json_spirit::find_value(rpc_error, "message");
                cout << "JSON RPC Error code: " << code.get_int() << "\n";
                cout <<  message.get_str() << "\n";
                return 1;
            }
        }
    }
    catch(std::exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }


    // Get desired locale ("en_US") from command line or system locale
    QString lang_territory = QString::fromStdString(locale);
    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator
    QString lang = lang_territory;

    lang.truncate(lang_territory.lastIndexOf('_')); // "en"
    QTranslator qtTranslatorBase, qtTranslator, translatorBase, translator;

    qtTranslatorBase.load(QLibraryInfo::location(QLibraryInfo::TranslationsPath) + "/qt_" + lang);
    if (!qtTranslatorBase.isEmpty())
        app.installTranslator(&qtTranslatorBase);

    qtTranslator.load(QLibraryInfo::location(QLibraryInfo::TranslationsPath) + "/qt_" + lang_territory);
    if (!qtTranslator.isEmpty())
        app.installTranslator(&qtTranslator);

    translatorBase.load(":/translations/"+lang);
    if (!translatorBase.isEmpty())
        app.installTranslator(&translatorBase);

    translator.load(":/translations/"+lang_territory);
    if (!translator.isEmpty())
        app.installTranslator(&translator);

    app.setApplicationName(QApplication::translate("main", "Coin-Qt"));

    QSplashScreen splash(QPixmap(":/images/splash"), 0);
    splash.show();
    splash.setAutoFillBackground(true);
    splashref = &splash;

    app.processEvents();

    app.setQuitOnLastWindowClosed(false);

    try
    {
        // Else we start the bitcoin node and GUI!

        const Chain* chain_chooser;
        if(args.count("testnet"))
            chain_chooser = &testnet;
        else
            chain_chooser = &bitcoin;
        const Chain& chain(*chain_chooser);

        if(!args.count("datadir"))
            data_dir = CDB::dataDir(chain.dataDirSuffix());

        logfile = data_dir + "/debug.log";

        Node node(chain, data_dir, args.count("nolisten") ? "" : "0.0.0.0"); // it is also here we specify the use of a proxy!
//        PortMapper(node.get_io_service(), port); // this will use the Node call

        // use the connect and addnode options to restrict and supplement the irc and endpoint db.
        for(strings::iterator ep = add_peers.begin(); ep != add_peers.end(); ++ep) node.addPeer(*ep);
        for(strings::iterator ep = connect_peers.begin(); ep != connect_peers.end(); ++ep) node.connectPeer(*ep);

        Wallet wallet(node); // this will also register the needed callbacks

        if(args.count("rescan")) {
            wallet.ScanForWalletTransactions();
            printf("Scanned for wallet transactions");
        }

        thread nodeThread(&Node::run, &node); // run this as a background thread

        {
            // Put this in a block, so that BitcoinGUI is cleaned up properly before
            // calling Shutdown() in case of exceptions.
            BitcoinGUI window;
            splash.finish(&window);
            OptionsModel optionsModel(&wallet);
            ClientModel clientModel(node, &optionsModel);
            WalletModel walletModel(&wallet, &optionsModel);

            guiref = &window;
            window.setClientModel(&clientModel);
            window.setWalletModel(&walletModel);

            // If -min option passed, start window minimized.
            if(args.count("min"))
                window.showMinimized();
            else
                window.show();

            // Place this here as guiref has to be defined if we dont want to lose URLs
            /*
            ipcInit(); // we skip the ipc as it consumes 100% cpu ???
            // Check for URL in argv
            for (int i = 1; i < argc; i++)
            {
                if (strlen(argv[i]) > 7 && strncasecmp(argv[i], "bitcoin:", 8) == 0)
                {
                    const char *strURL = argv[i];
                    try {
                        boost::interprocess::message_queue mq(boost::interprocess::open_only, "BitcoinURL");
                        mq.try_send(strURL, strlen(strURL), 0);
                    }
                    catch (boost::interprocess::interprocess_exception &ex) {
                    }
                }
            }
            */
            app.exec();

            guiref = 0;
        }
        printf("GUI exitted, shutting down Node...\n");
        // getting here means that we have exited from the gui (e.g. by the quit method)

        node.shutdown();
        nodeThread.join();
    } catch (std::exception& e) {
        PrintException(&e, "Runaway exception");
    } catch (...) {
        PrintException(NULL, "Runaway exception");
    }
    return 0;
}
#endif // BITCOIN_QT_TEST
