// Runs the three B4 queries against a seeded database, prints each plan and
// times it. Also times the slower GROUP BY version of query (c) so the choice
// between the two is a measurement and not just a claim.
//
//   ./queries social.db migrations
//
// Expects the database to already be seeded by tools/explain.cpp --seed.

#include "../src/db.hpp"
#include "../src/migrate.hpp"
#include "../src/repository.hpp"

#include <iostream>
#include <chrono>
#include <string>
#include <vector>

static void show_plan(Db& db, const std::string& sql,
                      const std::vector<long long>& params) {
    auto s = db.prepare("EXPLAIN QUERY PLAN " + sql);
    for (size_t i = 0; i < params.size(); ++i)
        s.bind(static_cast<int>(i) + 1, params[i]);
    while (s.step()) std::cout << "  " << s.col_text(3) << "\n";
}

template <class F>
static double mean_ms(F f, int reps) {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
}

// Runs a query to completion and throws the rows away. Used for timing the
// two variants of (c) through the same code path.
static void drain(Db& db, const char* sql, long long param) {
    auto s = db.prepare(sql);
    s.bind(1, param);
    while (s.step()) {}
}

int main(int argc, char** argv) {
    std::string db_path = argc > 1 ? argv[1] : "social.db";
    std::string mig_dir = argc > 2 ? argv[2] : "migrations";

    try {
        Db db(db_path);
        run_migrations(db, mig_dir);
        db.exec("ANALYZE;");
        Repository repo(db);

        const long long USER  = 7;
        const long long SINCE = 1790000000LL - 86400;

        std::cout << "== (a) follower count for user 7 ==\n";
        show_plan(db, Repository::SQL_FOLLOWER_COUNT, {USER});
        std::cout << "  result: " << repo.follower_count(USER) << " followers\n";
        std::cout << "  mean over 200 runs: "
                  << mean_ms([&] { repo.follower_count(USER); }, 200) << " ms\n";

        std::cout << "\n== (b) latest 20 posts from people user 7 follows ==\n";
        show_plan(db, Repository::SQL_FEED, {USER, 20});
        std::cout << "  result: " << repo.feed(USER, 20).size() << " posts\n";
        std::cout << "  mean over 200 runs: "
                  << mean_ms([&] { repo.feed(USER, 20); }, 200) << " ms\n";

        std::cout << "\n== (c) most liked post in the last 24 hours ==\n";
        std::cout << "  posts inside the window: " << repo.posts_since(SINCE)
                  << " out of 200000\n";
        show_plan(db, Repository::SQL_TOP_POST_24H, {SINCE});
        Post p; long long likes = 0;
        if (repo.top_post_24h(SINCE, p, likes))
            std::cout << "  result: post " << p.post_id << " by "
                      << p.author_handle << " with " << likes << " likes\n";
        std::cout << "  mean over 20 runs: "
                  << mean_ms([&] {
                         drain(db, Repository::SQL_TOP_POST_24H, SINCE);
                     }, 20) << " ms\n";

        std::cout << "\n== (c) the GROUP BY version, for comparison ==\n";
        show_plan(db, Repository::SQL_TOP_POST_24H_GROUPBY, {SINCE});
        std::cout << "  mean over 3 runs: "
                  << mean_ms([&] {
                         drain(db, Repository::SQL_TOP_POST_24H_GROUPBY, SINCE);
                     }, 3) << " ms\n";
    } catch (const DbError& e) {
        std::cerr << "database error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
