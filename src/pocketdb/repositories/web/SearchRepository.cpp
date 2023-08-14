// Copyright (c) 2018-2022 The Pocketnet developers
// Distributed under the Apache 2.0 software license, see the accompanying
// https://www.apache.org/licenses/LICENSE-2.0

#include "pocketdb/repositories/web/SearchRepository.h"

namespace PocketDb
{
    // TODO (aok, api): not used in PocketRpc
    UniValue SearchRepository::SearchTags(const SearchRequest& request)
    {
        UniValue result(UniValue::VARR);

        string keyword = "%" + request.Keyword + "%";
        string sql = R"sql(
            select Value
            from Tags t indexed by Tags_Value
            where t.Value match ?
            limit ?
            offset ?
        )sql";

        SqlTransaction(__func__, [&]()
        {
            Sql(sql)
            .Bind(keyword, request.PageSize, request.PageStart)
            .Select([&](Cursor& cursor) {
                while (cursor.Step())
                {
                    if (auto[ok, value] = cursor.TryGetColumnString(0); ok)
                        result.push_back(value);
                }
            });
        });

        return result;
    }

    vector<int64_t> SearchRepository::SearchIds(const SearchRequest& request)
    {
        vector<int64_t> ids;

        if (request.Keyword.empty())
            return ids;

        SqlTransaction(__func__, [&]()
        {
            Sql(R"sql(
                with
                    keyword as (
                        select
                            cm.ContentId,
                            rank
                        from
                            web.Content c,
                            web.ContentMap cm
                        where
                            c.ROWID = cm.ROWID and
                            cm.FieldType in ( )sql" + join(request.FieldTypes | transformed(static_cast<string(*)(int)>(to_string)), ",") + R"sql( ) and
                            c.Value match )sql" + ("\"" + request.Keyword + "\"" + " OR " + request.Keyword + "*") + R"sql(
                        order by
                            rank
                    )
                select
                    ct.Uid,
                    keyword.rank
                from
                    keyword
                cross join
                    Chain ct on --indexed by Chain_Uid_Height on
                        ct.Uid = keyword.ContentId and
                        (? or ct.Height <= ?) -- 
                cross join
                    Transactions t on
                        ct.TxId = t.RowId and
                        t.Type in ( )sql" + join(request.TxTypes | transformed(static_cast<string(*)(int)>(to_string)), ",") + R"sql( ) and
                        (? or t.RegId1 = (
                            select
                                RowId as id,
                                String as hash
                            from
                                Registry
                            where
                                String = ?
                        ))
                cross join
                    Last lt on
                        lt.TxId = t.RowId
                order by
                    keyword.rank desc
                limit ?
                offset ?
            )sql")
            .Bind(
                !(request.TopBlock > 0),
                request.TopBlock,
                request.Address.empty(),
                request.Address,
                request.PageSize,
                request.PageStart
            )
            .Select([&](Cursor& cursor) {
                while (cursor.Step())
                {
                    if (auto[ok, value] = cursor.TryGetColumnInt64(0); ok)
                        ids.push_back(value);
                }
            });
        });

        return ids;
    }

    // TODO (aok, api): implement ?
    vector<int64_t> SearchRepository::SearchUsersOld(const SearchRequest& request)
    {
        vector<int64_t> result;

        string heightWhere = request.TopBlock > 0 ? " and t.Height <= ? " : "";
        string keyword = "\"" + request.Keyword + "\"" + " OR " + request.Keyword + "*";

        string sql = R"sql(
            select
                t.Id

            from web.Content f

            join web.ContentMap fm on fm.ROWID = f.ROWID

            cross join Transactions t indexed by Transactions_Last_Id_Height
                on t.Id = fm.ContentId

            cross join Payload p on p.TxHash=t.Hash

            where t.Last = 1
                and t.Type = 100
                and t.Height is not null
                )sql" + heightWhere + R"sql(
                and fm.FieldType in ( )sql" + join(vector<string>(request.FieldTypes.size(), "?"), ",") + R"sql( )
                and f.Value match ?
        )sql";

        if (request.OrderByRank)
            sql += " order by rank, t.Id ";

        sql += " limit ? ";
        sql += " offset ? ";

        SqlTransaction(__func__, [&]()
        {
            auto& stmt = Sql(sql);

            if (request.TopBlock > 0)
                stmt.Bind(request.TopBlock);
            stmt.Bind(request.FieldTypes);
            stmt.Bind(keyword);
            stmt.Bind(request.PageSize);
            stmt.Bind(request.PageStart);

            stmt.Select([&](Cursor& cursor) {
                while (cursor.Step())
                {
                    if (auto[ok, value] = cursor.TryGetColumnInt64(0); ok) result.push_back(value);
                }
            });
        });

        return result;
    }

    vector<int64_t> SearchRepository::SearchUsers(const string& keyword)
    {
        vector<int64_t> result;

        SqlTransaction(__func__, [&]()
        {
            Sql(R"sql(
                with
                    keyword as ( select '"aok" OR aok*' as value )
                select
                    names.*
                from (
                    select
                        fm.ContentId,
                        ROW_NUMBER() OVER ( ORDER BY RANK, length(f.Value) ) as ROWNUMBER,
                        RANK as RNK,
                        r.Value as Rating
                    from
                        keyword,
                        web.Content f
                    cross join
                        web.ContentMap fm on
                            fm.ROWID = f.ROWID
                    left join
                        Ratings r on
                            r.Uid = fm.ContentId and
                            r.Last = 1 and
                            r.Type = 0
                    where
                        fm.FieldType in (?) and
                        f.Value match keyword.value
                    limit ?
                ) names

                union

                select
                    about.*
                from (
                    select
                        fm.ContentId,
                        ROW_NUMBER() OVER ( ORDER BY r.Value DESC) as ROWNUMBER,
                        RANK as RNK,
                        r.Value as Rating
                    from
                        keyword,
                        web.Content f
                    join
                        web.ContentMap fm on
                            fm.ROWID = f.ROWID
                    left join
                        Ratings r on
                            r.Uid = fm.ContentId and
                            r.Last = 1 and
                            r.Type = 0
                    where
                        fm.FieldType in (?) and
                        f.Value match keyword.value
                    limit ?
                ) about

                order by ROWNUMBER, RNK, Rating desc
            )sql")
            .Bind(
                ("\"" + keyword + "\"" + " OR " + keyword + "*"),
                (int)ContentFieldType::ContentFieldType_AccountUserName,
                10,
                (int)ContentFieldType::ContentFieldType_AccountUserAbout,
                10
            )
            .Select([&](Cursor& cursor) {
                while (cursor.Step())
                {
                    if (auto[ok, value] = cursor.TryGetColumnInt64(0); ok)
                        if (find(result.begin(), result.end(), value) == result.end())
                            result.push_back(value);
                }
            });
        });

        return result;
    }

    vector<string> SearchRepository::GetRecommendedAccountByAddressSubscriptions(const string& address, string& addressExclude, const vector<int>& contentTypes, const string& lang, int cntOut, int nHeight, int depth)
    {
        vector<string> ids;

        if (address.empty())
            return ids;

        int minReputation = 300;
        int limitSubscriptions = 30;
        int limitSubscriptionsTotal = 30;
        int cntRates = 1;

        SqlTransaction(__func__, [&]()
        {
            auto& stmt = Sql(R"sql(
                with
                    addr as (
                        select
                            RowId as id,
                            String as hash
                        from
                            Registry
                        where
                            String = ?
                    ),
                    addrs as (
                        select
                            id,
                            rnk
                        from (
                            select
                                1 as rnk,
                                subscribes.RegId2 as id
                            from
                                addr
                            cross join
                                Transactions subscribes indexed by Transactions_Type_RegId1_RegId2_RegId3 on
                                    subscribes.Type in (302, 303) and
                                    subscribes.RegId1 = addr.id
                            cross join
                                Last lSubscribes on
                                    lSubscribes.TxId = subscribes.RowId
                            cross join
                                Transactions u indexed by Transactions_Type_RegId1_RegId2_RegId3 on
                                    u.Type in (100) and
                                    u.RegId1 = subscribes.RegId2
                            cross join
                                Last lu on
                                    lu.TxId = u.RowId
                            cross join
                                Chain cu on
                                    cu.TxId = u.RowId
                            cross join
                                Ratings r indexed by Ratings_Type_Uid_Last_Value on
                                    r.Uid = cu.Uid and
                                    r.Type = 0 and
                                    r.Last = 1 and
                                    r.Value > ?
                            order by
                                subscribes.RowId desc
                            limit ?
                        )

                        union

                        select
                            id,
                            rnk
                        from (
                            select
                                2 as rnk,
                                subscribers.RegId1 as id
                            from
                                addr
                            cross join
                                Transactions subscribers indexed by Transactions_Type_RegId2_RegId1 on
                                    subscribers.Type in (302, 303) and
                                    subscribers.RegId2 = addr.id
                            cross join
                                Last lSubscribers on
                                    lSubscribers.TxId = subscribers.RowId
                            cross join
                                Transactions u indexed by Transactions_Type_RegId1_RegId2_RegId3 on
                                    u.Type in (100) and
                                    u.RegId1 = subscribers.RegId1
                            cross join
                                Last lu on
                                    lu.TxId = u.RowId
                            cross join
                                Chain cu on
                                    cu.TxId = u.RowId
                            cross join
                                Ratings r indexed by Ratings_Type_Uid_Last_Value on
                                    r.Uid = cu.Uid and
                                    r.Type = 0 and
                                    r.Last = 1 and
                                    r.Value > ?
                            order by
                                random()
                            limit ?
                        )

                        order by
                            rnk
                        limit ?
                    )
                select
                    (select String from Registry r where r.RowId = Contents.RegId1)
                from
                    addrEx,
                    addrs
                cross join
                    Transactions Rates indexed by Transactions_Type_RegId1_RegId2_RegId3 on
                        Rates.Type in (300) and
                        Rates.Int1 = 5 and
                        Rates.RegId1 = addrs.id
                cross join
                    Chain cRates on
                        cRates.TxId = Rates.RowId
                cross join Transactions Contents indexed by Transactions_Type_RegId2_RegId1
                    on Contents.RegId2 = Rates.RegId2
                        and Contents.Type in ( )sql" + join(vector<string>(contentTypes.size(), "?"), ",") + R"sql( ) and
                        (1 or Contents.RegId1 not in (
                            select
                                RowId
                            from
                                Registry
                            where
                                String = ?
                        ))
                cross join
                    Last lContents on
                        lContents.TxId = Contents.RowId
                cross join
                    Transactions u indexed by Transactions_Type_RegId1_RegId2_RegId3 on
                        u.Type in (100) and
                        u.RegId1 = Contents.RegId1
                cross join
                    Last lu on
                        lu.TxId = u.RowId
                cross join
                    Payload lang on
                        lang.TxId = u.RowId and
                        (0 or lang.String1 = ?)
                group by
                    Contents.RegId1
                having count() > ?
                order by
                    count() desc
                limit ?
            )sql")
            .Bind(
                address,
                minReputation,
                limitSubscriptions,
                minReputation,
                limitSubscriptions,
                limitSubscriptionsTotal,
                contentTypes,
                addressExclude.empty(),
                addressExclude,
                lang.empty(),
                lang,
                cntRates,
                cntOut
            )
            .Select([&](Cursor& cursor) {
                while (cursor.Step())
                {
                    if (auto[ok, value] = cursor.TryGetColumnString(0); ok)
                        ids.push_back(value);
                }
            });
        });

        return ids;
    }

    // TODO (aok, api): implement
    vector<int64_t> SearchRepository::GetRecommendedContentByAddressSubscriptions(const string& contentAddress, string& address, const vector<int>& contentTypes, const string& lang, int cntOut, int nHeight, int depth)
    {
        vector<int64_t> ids;

        if (contentAddress.empty())
            return ids;

        string contentTypesFilter = join(vector<string>(contentTypes.size(), "?"), ",");

        string excludeAddressFilter = "?";
        if (!address.empty())
            excludeAddressFilter += ", ?";

        string langFilter = "";
        if (!lang.empty())
            langFilter = "cross join Payload lang indexed by Payload_String1_TxHash on lang.TxHash = Contents.Hash and lang.String1 = ?";

        int minReputation = 300;
        int limitSubscriptions = 50;
        int limitSubscriptionsTotal = 30;
        int cntRates = 1;

        string sql = R"sql(
            select recomendations.Id
            from (
                 select Contents.String1,
                        Contents.Id,
                        --Rates.String2,
                        count(*) count
                 from Transactions Rates indexed by Transactions_Type_Last_String1_Height_Id
                  cross join Transactions Contents indexed by Transactions_Type_Last_String2_Height
                     on Contents.String2 = Rates.String2
                         and Contents.Last = 1
                         and Contents.Height > 0
                         and Contents.Type in ( )sql" + contentTypesFilter + R"sql( )
                         and Contents.String1 not in ( )sql" + excludeAddressFilter + R"sql( )
                  )sql" + langFilter + R"sql(
                 where Rates.Type in (300)
                   and Rates.Int1 = 5
                   and Rates.Height > ?
                   and Rates.Last in (0, 1)
                   and Rates.String1 in
                       (
                       select address
                       from (
                            select rnk, address
                            from (
                                 select 1 as rnk, subscribes.String2 as address
                                 from Transactions subscribes indexed by Transactions_String1_Last_Height
                                  cross join Transactions u indexed by Transactions_Type_Last_String1_Height_Id
                                     on u.Type in (100)
                                         and u.Last = 1 and u.Height > 0
                                         and u.String1 = subscribes.String2
                                  cross join Ratings r indexed by Ratings_Type_Id_Last_Value
                                     on r.Id = u.Id
                                         and r.Type = 0
                                         and r.Last = 1
                                         and r.Value > ?
                                 where subscribes.Type in (302, 303)
                                   and subscribes.Last = 1
                                   and subscribes.String1 = ?
                                   and subscribes.Height is not null
                                 order by subscribes.Height desc
                                 limit ?
                                 )
                            union
                            select rnk, address
                            from (
                                 select 2 as rnk, subscribers.String1 as address
                                 from Transactions subscribers indexed by Transactions_Type_Last_String2_Height
                                  cross join Transactions u indexed by Transactions_Type_Last_String1_Height_Id
                                     on u.Type in (100)
                                         and u.Last = 1 and u.Height > 0
                                         and u.String1 = subscribers.String1
                                  cross join Ratings r indexed by Ratings_Type_Id_Last_Value
                                     on r.Id = u.Id
                                         and r.Type = 0
                                         and r.Last = 1
                                         and r.Value > ?
                                 where subscribers.Type in (302, 303)
                                   and subscribers.Last = 1
                                   and subscribers.String2 = ?
                                   and subscribers.Height is not null
                                 order by random()
                                 limit ?
                                 ))
                       order by rnk
                       limit ?
                       )
                 --group by Rates.String2
                 group by Contents.Id
                 having count(*) > ?
                 order by count(*) desc
                 ) recomendations
            group by recomendations.String1
            order by recomendations.count desc
            limit ?
        )sql";

        SqlTransaction(__func__, [&]()
        {
            auto& stmt = Sql(sql);

            stmt.Bind(contentTypes, contentAddress);

            if (!address.empty())
                stmt.Bind(address);

            if (!lang.empty())
                stmt.Bind(lang);

            stmt.Bind(
                    nHeight-depth,
                    minReputation,
                    contentAddress,
                    limitSubscriptions,
                    minReputation,
                    contentAddress,
                    limitSubscriptions,
                    limitSubscriptionsTotal,
                    cntRates,
                    cntOut
            );

            stmt.Select([&](Cursor& cursor) {
                while (cursor.Step())
                {
                    if (auto[ok, value] = cursor.TryGetColumnInt64(0); ok)
                        ids.push_back(value);
                }
            });
        });

        return ids;
    }

    // TODO (aok, api): implement
    vector<int64_t> SearchRepository::GetRandomContentByAddress(const string& contentAddress, const vector<int>& contentTypes, const string& lang, int cntOut)
    {
        vector<int64_t> ids;

        if (contentAddress.empty())
            return ids;

        string contentTypesFilter = join(vector<string>(contentTypes.size(), "?"), ",");

        string langFilter = "";
        if (!lang.empty())
            langFilter = "cross join Payload lang on lang.TxHash = Contents.Hash and lang.String1 = ?";

        string sql = R"sql(
            select Contents.Id
            from Transactions Contents indexed by Transactions_Type_Last_String1_Height_Id
            )sql" + langFilter + R"sql(
            where Contents.Type in ( )sql" + contentTypesFilter + R"sql( )
              and Contents.Last = 1
              and Contents.String1 = ?
              and Contents.Height > 0
            order by random()
            limit ?
        )sql";

        SqlTransaction(__func__, [&]()
        {
            auto& stmt = Sql(sql);

            if (!lang.empty())
                stmt.Bind(lang);

            stmt.Bind(contentTypes, contentAddress, cntOut);

            stmt.Select([&](Cursor& cursor) {
                while (cursor.Step())
                {
                    if (auto[ok, value] = cursor.TryGetColumnInt64(0); ok)
                        ids.push_back(value);
                }
            });
        });

        return ids;
    }

    // TODO (aok, api): implement
    vector<int64_t> SearchRepository::GetContentFromAddressSubscriptions(const string& address, const vector<int>& contentTypes, const string& lang, int cntOut, bool rest)
    {
        vector<int64_t> ids;

        if (address.empty())
            return ids;

        string contentTypesFilter = join(vector<string>(contentTypes.size(), "?"), ",");

        string langFilter = "";
        if (!lang.empty())
            langFilter = "cross join Payload lang on lang.TxHash = Contents.Hash and lang.String1 = ?";

        string limitFilter = "= floor(? / "
                             "(select count(s.String2) from Transactions s indexed by Transactions_Type_Last_String1_Height_Id where s.Type in (302, 303) and s.Last = 1 and s.String1 = ? and s.Height > 0)) ";
        if (rest)
            limitFilter = limitFilter + " + 1 limit ?";
        else
            limitFilter = "<" + limitFilter;

        string sql = R"sql(
            select
                result.Id
            from (
                    select
                        Contents.Id,
                        row_number() over (partition by contents.String1 order by r.Value desc ) as rowNumber
                    from Transactions Contents indexed by Transactions_Type_Last_String1_Height_Id
                    cross join Ratings r on contents.Id = r.Id and r.Last = 1 and r.Type = 2
                    )sql" + langFilter + R"sql(
                    where Contents.Type in ( )sql" + contentTypesFilter + R"sql( )
                      and Contents.Last = 1
                      and Contents.String1 in (
                        select subscribtions.String2
                        from Transactions subscribtions indexed by Transactions_Type_Last_String1_Height_Id
                        where subscribtions.Type in (302, 303)
                            and subscribtions.Last = 1
                            and subscribtions.String1 = ?
                            and subscribtions.Height > 0
                      )
                      and Contents.Height > 0
                )result
            where result.rowNumber
        )sql" + limitFilter;

        SqlTransaction(__func__, [&]()
        {
            auto& stmt = Sql(sql);

            if (!lang.empty())
                stmt.Bind(lang);

            stmt.Bind(contentTypes, address, cntOut, address);

            if (rest)
                stmt.Bind(cntOut);

            stmt.Select([&](Cursor& cursor) {
                while (cursor.Step())
                {
                    if (auto[ok, value] = cursor.TryGetColumnInt64(0); ok)
                        ids.push_back(value);
                }
            });
        });

        return ids;
    }
}