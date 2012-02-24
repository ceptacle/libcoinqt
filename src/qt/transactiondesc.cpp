#include <transactiondesc.h>

#include "guiutil.h"
#include "bitcoinunits.h"

#include "qtui.h"

#include <QString>

using namespace std;

QString TransactionDesc::FormatTxStatus(const CWalletTx& wtx)
{
    if (!wtx.pwallet->isFinal(wtx))
    {
        if (wtx.lockTime() < LOCKTIME_THRESHOLD)
            return tr("Open for %1 blocks").arg(wtx.pwallet->getBestHeight() - wtx.lockTime());
        else
            return tr("Open until %1").arg(GUIUtil::dateTimeStr(wtx.lockTime()));
    }
    else
    {
        int nDepth = wtx.pwallet->getDepthInMainChain(wtx.getHash());
        if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
            return tr("%1/offline?").arg(nDepth);
        else if (nDepth < 6)
            return tr("%1/unconfirmed").arg(nDepth);
        else
            return tr("%1 confirmations").arg(nDepth);
    }
}

QString TransactionDesc::toHTML(Wallet *wallet, CWalletTx &wtx)
{
    QString strHTML;
    CRITICAL_BLOCK(wallet->cs_wallet)
    {
        strHTML.reserve(4000);
        strHTML += "<html><font face='verdana, arial, helvetica, sans-serif'>";

        int64 nTime = wtx.GetTxTime();
        int64 nCredit = wtx.GetCredit();
        int64 nDebit = wtx.GetDebit();
        int64 nNet = nCredit - nDebit;

        strHTML += tr("<b>Status:</b> ") + FormatTxStatus(wtx);
        int nRequests = wtx.GetRequestCount();
        if (nRequests != -1)
        {
            if (nRequests == 0)
                strHTML += tr(", has not been successfully broadcast yet");
            else if (nRequests == 1)
                strHTML += tr(", broadcast through %1 node").arg(nRequests);
            else
                strHTML += tr(", broadcast through %1 nodes").arg(nRequests);
        }
        strHTML += "<br>";

        strHTML += tr("<b>Date:</b> ") + (nTime ? GUIUtil::dateTimeStr(nTime) : QString("")) + "<br>";

        //
        // From
        //
        if (wtx.isCoinBase())
        {
            strHTML += tr("<b>Source:</b> Generated<br>");
        }
        else if (!wtx.mapValue["from"].empty())
        {
            // Online transaction
            if (!wtx.mapValue["from"].empty())
                strHTML += tr("<b>From:</b> ") + GUIUtil::HtmlEscape(wtx.mapValue["from"]) + "<br>";
        }
        else
        {
            // Offline transaction
            if (nNet > 0)
            {
                // Credit
                BOOST_FOREACH(const Output& txout, wtx.getOutputs())
                {
                    if (wallet->IsMine(txout))
                    {
                        PubKeyHash pubKeyHash;
                        ScriptHash scriptHash;
                        if (ExtractAddress(txout.script(), pubKeyHash, scriptHash) && wallet->haveKey(pubKeyHash))
                        {
                            ChainAddress address = wallet->chain().getAddress(pubKeyHash);
                            if (wallet->mapAddressBook.count(address))
                            {
                                strHTML += tr("<b>From:</b> ") + tr("unknown") + "<br>";
                                strHTML += tr("<b>To:</b> ");
                                strHTML += GUIUtil::HtmlEscape(address.toString());
                                if (!wallet->mapAddressBook[address].empty())
                                    strHTML += tr(" (yours, label: ") + GUIUtil::HtmlEscape(wallet->mapAddressBook[address]) + ")";
                                else
                                    strHTML += tr(" (yours)");
                                strHTML += "<br>";
                            }
                        }
                        break;
                    }
                }
            }
        }

        //
        // To
        //
        string strAddress;
        if (!wtx.mapValue["to"].empty())
        {
            // Online transaction
            strAddress = wtx.mapValue["to"];
            strHTML += tr("<b>To:</b> ");
            if (wallet->mapAddressBook.count(strAddress) && !wallet->mapAddressBook[strAddress].empty())
                strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[strAddress]) + " ";
            strHTML += GUIUtil::HtmlEscape(strAddress) + "<br>";
        }

        //
        // Amount
        //
        if (wtx.isCoinBase() && nCredit == 0)
        {
            //
            // Coinbase
            //
            int64 nUnmatured = 0;
            BOOST_FOREACH(const Output& txout, wtx.getOutputs())
                nUnmatured += wallet->GetCredit(txout);
            strHTML += tr("<b>Credit:</b> ");
            if (wtx.pwallet->isInMainChain(wtx.getHash()))
                strHTML += tr("(%1 matures in %2 more blocks)")
                        .arg(BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nUnmatured))
                        .arg(wtx.pwallet->GetBlocksToMaturity(wtx));
            else
                strHTML += tr("(not accepted)");
            strHTML += "<br>";
        }
        else if (nNet > 0)
        {
            //
            // Credit
            //
            strHTML += tr("<b>Credit:</b> ") + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nNet) + "<br>";
        }
        else
        {
            bool fAllFromMe = true;
            BOOST_FOREACH(const Input& txin, wtx.getInputs())
                fAllFromMe = fAllFromMe && wallet->IsMine(txin);

            bool fAllToMe = true;
            BOOST_FOREACH(const Output& txout, wtx.getOutputs())
                fAllToMe = fAllToMe && wallet->IsMine(txout);

            if (fAllFromMe)
            {
                //
                // Debit
                //
                BOOST_FOREACH(const Output& txout, wtx.getOutputs())
                {
                    if (wallet->IsMine(txout))
                        continue;

                    PubKeyHash pubKeyHash;
                    ScriptHash scriptHash;
                    if (wtx.mapValue["to"].empty())
                    {
                        // Offline transaction
                        if (ExtractAddress(txout.script(), pubKeyHash, scriptHash))
                        {
                            ChainAddress address = wallet->chain().getAddress(pubKeyHash);

                            strHTML += tr("<b>To:</b> ");
                            if (wallet->mapAddressBook.count(address) && !wallet->mapAddressBook[address].empty())
                                strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[address]) + " ";
                            strHTML += GUIUtil::HtmlEscape(address.toString());
                            strHTML += "<br>";
                        }
                    }

                    strHTML += tr("<b>Debit:</b> ") + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, -txout.value()) + "<br>";
                }

                if (fAllToMe)
                {
                    // Payment to self
                    int64 nChange = wtx.GetChange();
                    int64 nValue = nCredit - nChange;
                    strHTML += tr("<b>Debit:</b> ") + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, -nValue) + "<br>";
                    strHTML += tr("<b>Credit:</b> ") + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nValue) + "<br>";
                }

                int64 nTxFee = nDebit - wtx.getValueOut();
                if (nTxFee > 0)
                    strHTML += tr("<b>Transaction fee:</b> ") + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC,-nTxFee) + "<br>";
            }
            else
            {
                //
                // Mixed debit transaction
                //
                BOOST_FOREACH(const Input& txin, wtx.getInputs())
                    if (wallet->IsMine(txin))
                        strHTML += tr("<b>Debit:</b> ") + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC,-wallet->GetDebit(txin)) + "<br>";
                BOOST_FOREACH(const Output& txout, wtx.getOutputs())
                    if (wallet->IsMine(txout))
                        strHTML += tr("<b>Credit:</b> ") + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC,wallet->GetCredit(txout)) + "<br>";
            }
        }

        strHTML += tr("<b>Net amount:</b> ") + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC,nNet, true) + "<br>";

        //
        // Message
        //
        if (!wtx.mapValue["message"].empty())
            strHTML += QString("<br><b>") + tr("Message:") + "</b><br>" + GUIUtil::HtmlEscape(wtx.mapValue["message"], true) + "<br>";
        if (!wtx.mapValue["comment"].empty())
            strHTML += QString("<br><b>") + tr("Comment:") + "</b><br>" + GUIUtil::HtmlEscape(wtx.mapValue["comment"], true) + "<br>";

        strHTML += QString("<b>") + tr("Transaction ID:") + "</b> " + wtx.getHash().toString().c_str() + "<br>";

        if (wtx.isCoinBase())
            strHTML += QString("<br>") + tr("Generated coins must wait 120 blocks before they can be spent.  When you generated this block, it was broadcast to the network to be added to the block chain.  If it fails to get into the chain, it will change to \"not accepted\" and not be spendable.  This may occasionally happen if another node generates a block within a few seconds of yours.") + "<br>";

        //
        // Debug view
        //
        if (fDebug)
        {
            strHTML += "<hr><br>Debug information<br><br>";
            BOOST_FOREACH(const Input& txin, wtx.getInputs())
                if(wallet->IsMine(txin))
                    strHTML += "<b>Debit:</b> " + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC,-wallet->GetDebit(txin)) + "<br>";
            BOOST_FOREACH(const Output& txout, wtx.getOutputs())
                if(wallet->IsMine(txout))
                    strHTML += "<b>Credit:</b> " + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC,wallet->GetCredit(txout)) + "<br>";

            strHTML += "<br><b>Transaction:</b><br>";
            strHTML += GUIUtil::HtmlEscape(wtx.toString(), true);

            strHTML += "<br><b>Inputs:</b>";
            strHTML += "<ul>";
            CRITICAL_BLOCK(wallet->cs_wallet)
            {
                BOOST_FOREACH(const Input& txin, wtx.getInputs())
                {
                    Coin prevout = txin.prevout();

                    Transaction prev;
                    wallet->getTransaction(prevout.hash, prev);
                    {
                        if (prevout.index < prev.getNumOutputs())
                        {
                            strHTML += "<li>";
                            const Output vout = prev.getOutput(prevout.index);
                            PubKeyHash pubKeyHash;
                            ScriptHash scriptHash;
                            if (ExtractAddress(vout.script(), pubKeyHash, scriptHash))
                            {
                                ChainAddress address = wallet->chain().getAddress(pubKeyHash);
                                if (wallet->mapAddressBook.count(address) && !wallet->mapAddressBook[address].empty())
                                    strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[address]) + " ";
                                strHTML += QString::fromStdString(address.toString());
                            }
                            strHTML = strHTML + " Amount=" + BitcoinUnits::formatWithUnit(BitcoinUnits::BTC,vout.value());
                            strHTML = strHTML + " IsMine=" + (wallet->IsMine(vout) ? "true" : "false") + "</li>";
                        }
                    }
                }
            }
            strHTML += "</ul>";
        }

        strHTML += "</font></html>";
    }
    return strHTML;
}
