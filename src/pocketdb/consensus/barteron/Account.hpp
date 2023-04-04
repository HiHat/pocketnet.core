// Copyright (c) 2023 The Pocketnet developers
// Distributed under the Apache 2.0 software license, see the accompanying
// https://www.apache.org/licenses/LICENSE-2.0

#ifndef POCKETCONSENSUS_BARTERON_ACCOUNT_HPP
#define POCKETCONSENSUS_BARTERON_ACCOUNT_HPP

#include "pocketdb/consensus/Reputation.h"
#include "pocketdb/consensus/Social.h"
#include "pocketdb/models/dto/barteron/Account.h"

namespace PocketConsensus
{
    typedef shared_ptr<BarteronAccount> BarteronAccountRef;

    /*******************************************************************************************************************
    *  BarteronAccount consensus base class
    *******************************************************************************************************************/
    class BarteronAccountConsensus : public SocialConsensus<BarteronAccount>
    {
    public:
        BarteronAccountConsensus() : SocialConsensus<BarteronAccount>()
        {
            Limits.Set("payload_size", 2048, 2048, 2048);
        }

        ConsensusValidateResult Validate(const CTransactionRef& tx, const BarteronAccountRef& ptx, const PocketBlockRef& block) override
        {
            if (auto[ok, code] = SocialConsensus::Validate(tx, ptx, block); !ok)
                return {false, code};

            return Success;
        }

    protected:
    
        ConsensusValidateResult ValidateBlock(const BarteronAccountRef& ptx, const PocketBlockRef& block) override
        {
            auto blockPtxs = SocialConsensus::ExtractBlockPtxs(block, ptx, { BARTERON_ACCOUNT });
            if (blockPtxs.size() > 0)
                return {false, ConsensusResult_ManyTransactions};

            return Success;
        }
        
        ConsensusValidateResult ValidateMempool(const BarteronAccountRef& ptx) override
        {
            bool exists = false;

            // ExternalRepoInst.TryTransactionStep(__func__, [&]()
            // {
            //     auto stmt = ExternalRepoInst.SetupSqlStatement(R"sql(
            //         select
            //             1
            //         from
            //             Transactions t indexed by Transactions_Type_String1_Height_Time_Int1
            //         where
            //             t.Type in (104) and
            //             t.String1 = ? and
            //             t.Height is null
            //     )sql");
            //     ExternalRepoInst.TryBindStatementText(stmt, 1, *ptx->GetAddress());
            //     exists = (ExternalRepoInst.Step(stmt) == SQLITE_ROW);
            //     ExternalRepoInst.FinalizeSqlStatement(*stmt);
            // });

            return { !exists, ConsensusResult_ManyTransactions };
        }

    };


    // ----------------------------------------------------------------------------------------------
    // Factory for select actual rules version
    class BarteronAccountConsensusFactory : public BaseConsensusFactory<BarteronAccountConsensus>
    {
    public:
        BarteronAccountConsensusFactory()
        {
            // TODO (release): set height
            Checkpoint({ 99999999, 99999999, 0, make_shared<BarteronAccountConsensus>() });
        }
    };

    static BarteronAccountConsensusFactory ConsensusFactoryInst_BarteronAccount;
}

#endif // POCKETCONSENSUS_BARTERON_ACCOUNT_HPP
