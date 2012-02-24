#include "transactionrecord.h"

#include <coinWallet/Wallet.h>
#include <coinWallet/WalletTx.h>

/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    if (wtx.isCoinBase())
    {
        // Don't show generated coin until confirmed by at least one block after it
        // so we don't get the user's hopes up until it looks like it's probably accepted.
        //
        // It is not an error when generated blocks are not accepted.  By design,
        // some percentage of blocks, like 10% or more, will end up not accepted.
        // This is the normal mechanism by which the network copes with latency.
        //
        // We display regular transactions right away before any confirmation
        // because they can always get into some block eventually.  Generated coins
        // are special because if their block is not accepted, they are not valid.
        //
        if (wtx.pwallet->getDepthInMainChain(wtx.getHash()) < 2)
        {
            return false;
        }
    }
    return true;
}

/*
 * Decompose Wallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const Wallet *wallet, const CWalletTx &wtx)
{
    QList<TransactionRecord> parts;
    int64 nTime = wtx.nTimeDisplayed = wtx.GetTxTime();
    int64 nCredit = wtx.GetCredit(true);
    int64 nDebit = wtx.GetDebit();
    int64 nNet = nCredit - nDebit;
    uint256 hash = wtx.getHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    if (showTransaction(wtx))
    {
        if (nNet > 0 || wtx.isCoinBase())
        {
            //
            // Credit
            //
            BOOST_FOREACH(const Output& txout, wtx.getOutputs())
            {
                if(wallet->IsMine(txout))
                {
                    TransactionRecord sub(hash, nTime);
                    PubKeyHash pubKeyHash;
                    ScriptHash scriptHash;
                    sub.idx = parts.size(); // sequence number
                    sub.credit = txout.value();
                    if (wtx.isCoinBase())
                    {
                        // Generated
                        sub.type = TransactionRecord::Generated;
                    }
                    else if (ExtractAddress(txout.script(), pubKeyHash, scriptHash) && wallet->haveKey(pubKeyHash))
                    {
                        // Received by Bitcoin Address
                        sub.type = TransactionRecord::RecvWithAddress;
                        sub.address = wallet->chain().getAddress(pubKeyHash).toString();
                    }
                    else
                    {
                        // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                        sub.type = TransactionRecord::RecvFromOther;
                        sub.address = mapValue["from"];
                    }

                    parts.append(sub);
                }
            }
        }
        else
        {
            bool fAllFromMe = true;
            BOOST_FOREACH(const Input& txin, wtx.getInputs())
                fAllFromMe = fAllFromMe && wallet->IsMine(txin);

            bool fAllToMe = true;
            BOOST_FOREACH(const Output& txout, wtx.getOutputs())
                fAllToMe = fAllToMe && wallet->IsMine(txout);

            if (fAllFromMe && fAllToMe)
            {
                // Payment to self
                int64 nChange = wtx.GetChange();

                parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToSelf, "",
                                -(nDebit - nChange), nCredit - nChange));
            }
            else if (fAllFromMe)
            {
                //
                // Debit
                //
                int64 nTxFee = nDebit - wtx.getValueOut();

                for (int nOut = 0; nOut < wtx.getNumOutputs(); nOut++)
                {
                    const Output& txout = wtx.getOutput(nOut);
                    TransactionRecord sub(hash, nTime);
                    sub.idx = parts.size();

                    if(wallet->IsMine(txout))
                    {
                        // Ignore parts sent to self, as this is usually the change
                        // from a transaction sent back to our own address.
                        continue;
                    }

                    PubKeyHash pubKeyHash;
                    ScriptHash scriptHash;
                    if (ExtractAddress(txout.script(), pubKeyHash, scriptHash))
                    {
                        // Sent to Bitcoin Address
                        sub.type = TransactionRecord::SendToAddress;
                        sub.address = wallet->chain().getAddress(pubKeyHash).toString();
                    }
                    else
                    {
                        // Sent to IP, or other non-address transaction like OP_EVAL
                        sub.type = TransactionRecord::SendToOther;
                        sub.address = mapValue["to"];
                    }

                    int64 nValue = txout.value();
                    /* Add fee to first output */
                    if (nTxFee > 0)
                    {
                        nValue += nTxFee;
                        nTxFee = 0;
                    }
                    sub.debit = -nValue;

                    parts.append(sub);
                }
            }
            else
            {
                //
                // Mixed debit transaction, can't break down payees
                //
                bool fAllMine = true;
                BOOST_FOREACH(const Output& txout, wtx.getOutputs())
                    fAllMine = fAllMine && wallet->IsMine(txout);
                BOOST_FOREACH(const Input& txin, wtx.getInputs())
                    fAllMine = fAllMine && wallet->IsMine(txin);

                parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
            }
        }
    }

    return parts;
}

void TransactionRecord::updateStatus(const CWalletTx &wtx)
{
    // Determine transaction status

    // Find the block the tx is in
    int height = wtx.pwallet->getHeight(wtx._blockHash);
    if(height < 0) height = std::numeric_limits<int>::max();
    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        height,
        (wtx.isCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    status.confirmed = wtx.pwallet->IsConfirmed(wtx);
    status.depth = wtx.pwallet->getDepthInMainChain(wtx.getHash());
    status.cur_num_blocks = wtx.pwallet->getBestHeight();

    if (!wtx.pwallet->isFinal(wtx))
    {
        if (wtx.lockTime() < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.pwallet->getBestHeight() - wtx.lockTime();
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.lockTime();
        }
    }
    else
    {
        if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
        {
            status.status = TransactionStatus::Offline;
        }
        else if (status.depth < NumConfirmations)
        {
            status.status = TransactionStatus::Unconfirmed;
        }
        else
        {
            status.status = TransactionStatus::HaveConfirmations;
        }
    }

    // For generated transactions, determine maturity
    if(type == TransactionRecord::Generated)
    {
        int64 nCredit = wtx.GetCredit(true);
        if (nCredit == 0)
        {
            status.maturity = TransactionStatus::Immature;

            if (wtx.pwallet->isInMainChain(wtx.getHash()))
            {
                status.matures_in = wtx.pwallet->GetBlocksToMaturity(wtx);

                // Check if the block was requested by anyone
                if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.maturity = TransactionStatus::MaturesWarning;
            }
            else
            {
                status.maturity = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.maturity = TransactionStatus::Mature;
        }
    }
}

bool TransactionRecord::statusUpdateNeeded(int bestHeight)
{
    return status.cur_num_blocks != bestHeight;
}

std::string TransactionRecord::getTxID()
{
    return hash.toString() + strprintf("-%03d", idx);
}

