// Copyright (c) 2023 The Pocketnet developers
// Distributed under the Apache 2.0 software license, see the accompanying
// https://www.apache.org/licenses/LICENSE-2.0

#include <primitives/transaction.h>
#include "pocketdb/models/dto/barteron/Account.h"

namespace PocketTx
{
    BarteronAccount::BarteronAccount() : SocialTransaction()
    {
        SetType(TxType::BARTERON_ACCOUNT);
    }

    BarteronAccount::BarteronAccount(const CTransactionRef& tx) : SocialTransaction(tx)
    {
        SetType(TxType::BARTERON_ACCOUNT);
    }

} // namespace PocketTx