// Copyright (c) 2018-2022 The Pocketnet developers
// Distributed under the Apache 2.0 software license, see the accompanying
// https://www.apache.org/licenses/LICENSE-2.0

#ifndef POCKETCONSENSUS_USER_HPP
#define POCKETCONSENSUS_USER_HPP

#include <boost/algorithm/string.hpp>
#include "pocketdb/consensus/social/account/Account.hpp"
#include "pocketdb/models/dto/account/User.h"

namespace PocketConsensus
{
    using namespace std;
    using namespace PocketDb;
    typedef shared_ptr<User> UserRef;

    /*******************************************************************************************************************
    *  AccountUser consensus base class
    *******************************************************************************************************************/
    class AccountUserConsensus : public AccountConsensus<User>
    {
    private:
        using Base = AccountConsensus<User>;

    public:
        AccountUserConsensus() : AccountConsensus<User>()
        {
            // TODO (limits): set current limits
        }
        
        ConsensusValidateResult Validate(const CTransactionRef& tx, const UserRef& ptx, const PocketBlockRef& block) override
        {
            if (auto[ok, code] = Base::Validate(tx, ptx, block); !ok)
                return {false, code};

            // Get all the necessary data for transaction validation
            consensusData = ConsensusRepoInst.AccountUser(
                *ptx->GetAddress(),
                Height - (int)GetConsensusLimit(ConsensusLimit_depth),
                *ptx->GetPayloadName()
            );

            // Duplicate name
            if (consensusData.DuplicatesChainCount > 0 && !CheckpointRepoInst.IsSocialCheckpoint(*ptx->GetHash(), *ptx->GetType(), ConsensusResult_NicknameDouble))
                return {false, ConsensusResult_NicknameDouble};

            // The deleted account cannot be restored
            if ((TxType)consensusData.LastTxType == TxType::ACCOUNT_DELETE)
                return {false, ConsensusResult_AccountDeleted};

            // Daily change limit
            if (consensusData.EditsCount > GetConsensusLimit(edit_account_daily_count))
                return {false, ConsensusResult_ChangeInfoLimit};

            return Success;
        }

        ConsensusValidateResult Check(const CTransactionRef& tx, const UserRef& ptx) override
        {
            if (auto[ok, code] = Base::Check(tx, ptx); !ok)
                return {false, code};
             
            // Check payload
            if (!ptx->GetPayload()) return {false, ConsensusResult_Failed};

            // Self referring
            if (!IsEmpty(ptx->GetReferrerAddress()) && *ptx->GetAddress() == *ptx->GetReferrerAddress())
                return {false, ConsensusResult_ReferrerSelf};

            // Name check
            if (auto[ok, result] = CheckLogin(ptx); !ok)
                return {false, result};

            return Success;
        }

        // TODO (optimization): DEBUG! - remove after fix all checkpoints
        ConsensusValidateResult CheckOpReturnHash(const CTransactionRef& tx, const UserRef& ptx) override
        {
            auto ptxORHash = ptx->BuildHash();
            auto txORHash = TransactionHelper::ExtractOpReturnHash(tx);
            if (ptxORHash == txORHash)
                return Success;

            if (CheckpointRepoInst.IsOpReturnCheckpoint(*ptx->GetHash(), ptxORHash))
                return Success;

            LogPrintf("DEBUG - ConsensusResult_FailedOpReturn - %s\n", *ptx->GetHash());
            return Success;
            // return {false, SocialConsensusResult_FailedOpReturn};
        }

    protected:
        ConsensusData_AccountUser consensusData;

        ConsensusValidateResult ValidateBlock(const UserRef& ptx, const PocketBlockRef& block) override
        {
            // Only one transaction allowed in block
            for (auto& blockTx: *block)
            {
                if (!TransactionHelper::IsIn(*blockTx->GetType(), { ACCOUNT_USER, ACCOUNT_DELETE }) || *blockTx->GetHash() == *ptx->GetHash())
                    continue;

                auto blockPtx = static_pointer_cast<SocialTransaction>(blockTx);

                // In the early stages of the network, double transactions were allowed in blocks
                if (*ptx->GetAddress() == *blockPtx->GetAddress() && !CheckpointRepoInst.IsSocialCheckpoint(*ptx->GetHash(), *ptx->GetType(), ConsensusResult_ChangeInfoDoubleInBlock))
                    return {false, ConsensusResult_ChangeInfoDoubleInBlock};

                // We can allow "capture" another username in one block
                if (TransactionHelper::IsIn(*blockPtx->GetType(), { ACCOUNT_USER }))
                {
                    auto blockUserPtx = static_pointer_cast<User>(blockTx);
                    if (auto[ok, result] = ValidateBlockDuplicateName(ptx, blockUserPtx); !ok)
                        return {false, result};
                }
            }

            return Success;
        }

        ConsensusValidateResult ValidateMempool(const UserRef& ptx) override
        {
            if (consensusData.MempoolCount > 0)
                return {false, ConsensusResult_ChangeInfoDoubleInMempool};

            if (consensusData.DuplicatesMempoolCount > 0)
                return {false, ConsensusResult_NicknameDouble};

            return Success;
        }
        
        ConsensusValidateResult ValidatePayloadSize(const UserRef& ptx)
        {
            size_t dataSize =
                (ptx->GetPayloadName() ? ptx->GetPayloadName()->size() : 0) +
                (ptx->GetPayloadUrl() ? ptx->GetPayloadUrl()->size() : 0) +
                (ptx->GetPayloadLang() ? ptx->GetPayloadLang()->size() : 0) +
                (ptx->GetPayloadAbout() ? ptx->GetPayloadAbout()->size() : 0) +
                (ptx->GetPayloadAvatar() ? ptx->GetPayloadAvatar()->size() : 0) +
                (ptx->GetPayloadDonations() ? ptx->GetPayloadDonations()->size() : 0) +
                (ptx->GetReferrerAddress() ? ptx->GetReferrerAddress()->size() : 0) +
                (ptx->GetPayloadPubkey() ? ptx->GetPayloadPubkey()->size() : 0);

            if (dataSize > (size_t) GetConsensusLimit(ConsensusLimit_max_user_size))
                return {false, ConsensusResult_ContentSizeLimit};

            return Success;
        }
    
        // TODO (optimization): DEBUG!
        virtual ConsensusValidateResult CheckLogin(const UserRef& ptx)
        {
            if (IsEmpty(ptx->GetPayloadName()))
                // TODO (optimization): DEBUG!
                LogPrintf("DEBUG! ConsensusResult_Failed - %s\n", *ptx->GetHash());
                // return {false, ConsensusResult_Failed};

            auto name = *ptx->GetPayloadName();
            boost::algorithm::to_lower(name);

            if (name.size() > 20)
                // TODO (optimization): DEBUG!
                LogPrintf("DEBUG! ConsensusResult_NicknameLong - %s\n", *ptx->GetHash());
                // return {false, ConsensusResult_NicknameLong};
            
            if (!all_of(name.begin(), name.end(), [](unsigned char ch) { return ::isalnum(ch) || ch == '_'; }))
                // TODO (optimization): DEBUG!
                LogPrintf("DEBUG! ConsensusResult_Failed - %s\n", *ptx->GetHash());
                //return {false, ConsensusResult_Failed};

            return Success;
        }
    
        // TODO (optimization): DEBUG!
        virtual ConsensusValidateResult ValidateBlockDuplicateName(const UserRef& ptx, const UserRef& blockPtx)
        {
            auto ptxName = ptx->GetPayloadName() ? *ptx->GetPayloadName() : "";
            boost::algorithm::to_lower(ptxName);

            auto blockPtxName = blockPtx->GetPayloadName() ? *blockPtx->GetPayloadName() : "";
            boost::algorithm::to_lower(blockPtxName);

            if (ptxName == blockPtxName)
            {
                if (!CheckpointRepoInst.IsSocialCheckpoint(*ptx->GetHash(), *ptx->GetType(), ConsensusResult_NicknameDouble))
                    // TODO (optimization): DEBUG!
                    LogPrintf("DEBUG! ConsensusResult_FailedOpReturn - %s\n", *ptx->GetHash());
                    // return {false, ConsensusResult_NicknameDouble};
            }

            return Success;
        }
    };

    class AccountUserConsensusFactory : public BaseConsensusFactory<AccountUserConsensus>
    {
    public:
        AccountUserConsensusFactory()
        {
            Checkpoint({ 0, -1, -1, make_shared<AccountUserConsensus>() });
        }
    };

    static AccountUserConsensusFactory ConsensusFactoryInst_AccountUser;
}

#endif // POCKETCONSENSUS_USER_HPP
