// Copyright (c) 2018-2022 The Pocketnet developers
// Distributed under the Apache 2.0 software license, see the accompanying
// https://www.apache.org/licenses/LICENSE-2.0

#include "pocketdb/helpers/ShortFormRepositoryHelper.h"
#include "pocketdb/models/shortform/ShortTxData.h"
#include "pocketdb/models/shortform/ShortTxType.h"
#include "pocketdb/models/shortform/ShortAccount.h"
#include "pocketdb/models/shortform/ShortTxOutput.h"

std::optional<std::vector<PocketDb::ShortTxOutput>> _parseOutputs(const std::string& jsonStr)
{
    UniValue json (UniValue::VOBJ);
    if (!json.read(jsonStr) || !json.isArray()) return std::nullopt;
    std::vector<PocketDb::ShortTxOutput> res;
    res.reserve(json.size());
    for (int i = 0; i < json.size(); i++) {
        const auto& elem = json[i];
        PocketDb::ShortTxOutput output;

        if (elem.exists("TxHash") && elem["TxHash"].isStr()) output.SetTxHash(elem["TxHash"].get_str());
        if (elem.exists("Value") && elem["Value"].isNum()) output.SetValue(elem["Value"].get_int64());
        if (elem.exists("SpentTxHash") && elem["SpentTxHash"].isStr()) output.SetSpentTxHash(elem["SpentTxHash"].get_str());
        if (elem.exists("AddressHash") && elem["AddressHash"].isStr()) output.SetAddressHash(elem["AddressHash"].get_str());
        if (elem.exists("Number") && elem["Number"].isNum()) output.SetNumber(elem["Number"].get_int());
        if (elem.exists("ScriptPubKey") && elem["ScriptPubKey"].isStr()) output.SetScriptPubKey(elem["ScriptPubKey"].get_str());
        if (elem.exists("Account") && elem["Account"].isStr()) {
            const auto& accJson = elem["Account"];
            PocketDb::ShortAccount accData;
            if (accJson.exists("Lang") && accJson["Lang"].isStr()) accData.SetLang(accJson["Lang"].get_str());
            if (accJson.exists("Name") && accJson["Name"].isStr()) accData.SetName(accJson["Name"].get_str());
            if (accJson.exists("Avatar") && accJson["Avatar"].isStr()) accData.SetAvatar(accJson["Avatar"].get_str());
            if (accJson.exists("Rep") && accJson["Rep"].isNum()) accData.SetReputation(accJson["Rep"].get_int64());
            output.SetAccount(accData);
        }

        res.emplace_back(std::move(output));
    }

    return res;
}

std::optional<std::vector<std::pair<std::string, std::optional<PocketDb::ShortAccount>>>> _processMultipleAddresses(const std::string& jsonStr)
{
    UniValue json;
    if (!json.read(jsonStr) || !json.isArray() || json.size() <= 0) return std::nullopt;

    std::vector<std::pair<std::string, std::optional<PocketDb::ShortAccount>>> multipleAddresses;
    for (int i = 0; i < json.size(); i++) {
        const auto& entry = json[i];
        if (!entry.isObject() || !entry.exists("address") || !entry["address"].isStr()) {
            continue;
        }

        auto address = entry["address"].get_str();
        std::optional<PocketDb::ShortAccount> accountData;
        if (entry.exists("account")) {
            const auto& account = entry["account"];
            if (account["name"].isStr()) {
                PocketDb::ShortAccount accData;
                accData.SetName(account["name"].get_str());
                if (account["avatar"].isStr()) accData.SetAvatar(account["avatar"].get_str());
                if (account["badge"].isStr()) accData.SetBadge(account["badge"].get_str());
                if (account["reputation"].isNull()) accData.SetReputation(account["reputation"].get_int64());
                accountData = std::move(accData);
            }
        }

        multipleAddresses.emplace_back(std::make_pair(std::move(address), std::move(accountData)));
    }

    return multipleAddresses;
}

bool PocketHelpers::NotificationsResult::HasData(const int64_t& blocknum)
{
    return m_txArrIndices.find(blocknum) != m_txArrIndices.end();
}

void PocketHelpers::NotificationsResult::InsertData(const PocketDb::ShortForm& shortForm)
{
    if (HasData(*shortForm.GetTxData().GetBlockNum())) return;
    m_txArrIndices.insert({*shortForm.GetTxData().GetBlockNum(), m_data.size()});
    m_data.emplace_back(shortForm.Serialize(false));
}

void PocketHelpers::NotificationsResult::InsertNotifiers(const int64_t& blocknum, PocketDb::ShortTxType contextType, std::map<std::string, std::optional<PocketDb::ShortAccount>> addresses)
{
    for (const auto& address: addresses) {
        auto& notifierEntry = m_notifiers[address.first];
        notifierEntry.notifications[contextType].emplace_back(m_txArrIndices.at(blocknum));
        if (!notifierEntry.account)
            notifierEntry.account = address.second;
    }
}

UniValue PocketHelpers::NotificationsResult::Serialize() const
{
    UniValue notifiersUni (UniValue::VOBJ);
    notifiersUni.reserveKVSize(m_notifiers.size());

    for (const auto& notifier: m_notifiers) {
        const auto& address = notifier.first;
        const auto& notifierEntry = notifier.second;

        UniValue notifierData (UniValue::VOBJ);
        notifierData.reserveKVSize(notifierEntry.notifications.size());
        for (const auto& contextTypeIndices: notifierEntry.notifications) {
            UniValue indices (UniValue::VARR);
            indices.push_backV(contextTypeIndices.second);
            notifierData.pushKV(PocketHelpers::ShortTxTypeConvertor::toString(contextTypeIndices.first), indices, false);
        }

        UniValue notifierUniObj (UniValue::VOBJ);
        if (const auto& accData = notifierEntry.account; accData.has_value()) {
            notifierUniObj.pushKV("i", accData->Serialize(), false);
        }
        notifierUniObj.pushKV("e", std::move(notifierData), false);

        notifiersUni.pushKV(address, notifierUniObj, false);
    }

    UniValue data (UniValue::VARR);
    data.push_backV(m_data);

    UniValue res (UniValue::VOBJ);
    res.pushKV("data", data, false);
    res.pushKV("notifiers", notifiersUni,false);
    return res;
}

void PocketHelpers::ShortFormParser::Reset(const int& startIndex)
{
    m_startIndex = startIndex;
}

PocketDb::ShortForm PocketHelpers::ShortFormParser::ParseFull(Cursor& cursor)
{
    int index = m_startIndex;
    auto [ok, type] = cursor.TryGetColumnString(index++);
    if (!ok) {
        throw std::runtime_error("Missing row type");
    }

    auto txData = ProcessTxData(cursor, index);
    if (!txData) {
        throw std::runtime_error("Missing required fields for tx data in a row");
    }

    auto relatedContent = ProcessTxData(cursor, index);

    return {PocketHelpers::ShortTxTypeConvertor::strToType(type), *txData, relatedContent};
}

int64_t PocketHelpers::ShortFormParser::ParseBlockNum(Cursor& cursor)
{
    auto [ok, val] = cursor.TryGetColumnInt64(m_startIndex+5);
    if (!ok) {
        throw std::runtime_error("Failed to extract blocknum from cursor");
    }

    return val;
}

PocketDb::ShortTxType PocketHelpers::ShortFormParser::ParseType(Cursor& cursor)
{
    auto [ok, val] = cursor.TryGetColumnString(m_startIndex);
    if (!ok) {
        throw std::runtime_error("Failed to extract short tx type");
    }

    auto type = PocketHelpers::ShortTxTypeConvertor::strToType(val);
    if (type == PocketDb::ShortTxType::NotSet) {
        throw std::runtime_error("Failed to parse extracted short tx type");
    }

    return type;
}

std::string PocketHelpers::ShortFormParser::ParseHash(Cursor& cursor)
{
    auto [ok, hash] = cursor.TryGetColumnString(m_startIndex+1);
    if (!ok) {
        throw std::runtime_error("Failed to extract tx hash from cursor");
    }

    return hash;
}

std::optional<std::vector<PocketDb::ShortTxOutput>> PocketHelpers::ShortFormParser::ParseOutputs(Cursor& cursor)
{
    auto [ok, str] = cursor.TryGetColumnString(m_startIndex + 11);
    if (ok) {
        return _parseOutputs(str);
    }
    return std::nullopt;
}

std::optional<PocketDb::ShortAccount> PocketHelpers::ShortFormParser::ParseAccount(Cursor& cursor, const int& index)
{
    auto [ok1, lang] = cursor.TryGetColumnString(index);
    auto [ok2, name] = cursor.TryGetColumnString(index+1);
    auto [ok3, avatar] = cursor.TryGetColumnString(index+2);
    auto [ok4, reputation] = cursor.TryGetColumnInt64(index+3);
    if (ok2 && ok4) { // TODO (losty): can there be no avatar?
        auto acc = PocketDb::ShortAccount(name, avatar, reputation);
        if (ok1) acc.SetLang(lang);
        return acc;
    }
    return std::nullopt;
}

std::optional<PocketDb::ShortTxData> PocketHelpers::ShortFormParser::ProcessTxData(Cursor& cursor, int& index)
{
    const auto i = index;

    static const auto stmtOffset = 19;
    index += stmtOffset;

    auto [ok1, hash] = cursor.TryGetColumnString(i);
    auto [ok2, txType] = cursor.TryGetColumnInt(i+1);

    if (ok1 && ok2) {
        PocketDb::ShortTxData txData(hash, (PocketTx::TxType)txType);
        if (auto [ok, val] = cursor.TryGetColumnString(i+2); ok) txData.SetAddress(val);
        if (auto [ok, val] = cursor.TryGetColumnInt64(i+3); ok) txData.SetHeight(val);
        if (auto [ok, val] = cursor.TryGetColumnInt64(i+4); ok) txData.SetBlockNum(val);
        if (auto [ok, val] = cursor.TryGetColumnInt64(i+5); ok) txData.SetTime(val);
        if (auto [ok, val] = cursor.TryGetColumnString(i+6); ok) txData.SetRootTxHash(val);
        if (auto [ok, val] = cursor.TryGetColumnString(i+7); ok) txData.SetPostHash(val);
        if (auto [ok, val] = cursor.TryGetColumnInt64(i+8); ok) txData.SetVal(val);
        if (auto [ok, val] = cursor.TryGetColumnString(i+9); ok) txData.SetInputs(_parseOutputs(val));
        if (auto [ok, val] = cursor.TryGetColumnString(i+10); ok) txData.SetOutputs(_parseOutputs(val));
        if (auto [ok, val] = cursor.TryGetColumnString(i+11); ok) txData.SetDescription(val);
        if (auto [ok, val] = cursor.TryGetColumnString(i+12); ok) txData.SetCommentParentId(val);
        if (auto [ok, val] = cursor.TryGetColumnString(i+13); ok) txData.SetCommentAnswerId(val);
        txData.SetAccount(ParseAccount(cursor, i+14));
        if (auto [ok, val] = cursor.TryGetColumnString(i+18); ok) txData.SetMultipleAddresses(_processMultipleAddresses(val));
        return txData;
    }

    return std::nullopt;
}

PocketHelpers::EventsReconstructor::EventsReconstructor()
{
    m_parser.Reset(0);
}

void PocketHelpers::EventsReconstructor::FeedRow(Cursor& cursor)
{
    m_result.emplace_back(m_parser.ParseFull(cursor));
}

std::vector<PocketDb::ShortForm>& PocketHelpers::EventsReconstructor::GetResult()
{
    return m_result;
}

PocketHelpers::NotificationsReconstructor::NotificationsReconstructor()
{
    m_parser.Reset(5);
}

void PocketHelpers::NotificationsReconstructor::FeedRow(Cursor& cursor)
{
    // Notifiers data for current row context. Possible more than one notifier for the same context
    std::map<std::string, std::optional<PocketDb::ShortAccount>> notifiers;
    // Collecting addresses and accounts for notifiers. Required data can be in 2 places:
    //  - First column in query
    //  - Outputs (e.x. for money)
    //  TODO (losty): generalize collecting account data because there could be more variants in the future
    auto [ok, addressOne] = cursor.TryGetColumnString(0);
    if (ok) {
        auto pulp = m_parser.ParseAccount(cursor, 1);
        notifiers.insert(std::make_pair(std::move(addressOne), std::move(pulp)));
    } else {
        if (auto outputs = m_parser.ParseOutputs(cursor); outputs) {
            for (const auto& output: *outputs) {
                if (output.GetAddressHash() && !output.GetAddressHash()->empty()) {
                    notifiers.insert({*output.GetAddressHash(), output.GetAccount()});
                }
            }
        }
    }
    if (notifiers.empty()) throw std::runtime_error("Missing address of notifier");

    auto blockNum = m_parser.ParseBlockNum(cursor); // blocknum is a unique key of tx because we are looking for txs in a single block
    // Do not perform parsing sql if we already has this tx
    if (!m_notifications.HasData(blockNum)) {
        m_notifications.InsertData(m_parser.ParseFull(cursor));
    }
    m_notifications.InsertNotifiers(blockNum, m_parser.ParseType(cursor), std::move(notifiers));
}

PocketHelpers::NotificationsResult PocketHelpers::NotificationsReconstructor::GetResult() const
{
    return m_notifications;
}

void PocketHelpers::NotificationSummaryReconstructor::FeedRow(Cursor& cursor)
{
    auto [ok1, typeStr] = cursor.TryGetColumnString(0);
    auto [ok2, address] = cursor.TryGetColumnString(1);
    if (!ok1 || !ok2) return;

    if (auto type = PocketHelpers::ShortTxTypeConvertor::strToType(typeStr); type != PocketDb::ShortTxType::NotSet) {
        m_result[address][type]++;
    }
}
