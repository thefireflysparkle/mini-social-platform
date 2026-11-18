#pragma once
// Every read and write to the database goes through this class. Nothing else
// in the project touches SQL.
//
// Two rules I am keeping to:
//   1. Every statement is prepared with bound parameters. There is no method
//      here that builds SQL by joining strings, so there is nowhere for user
//      input to become part of the query.
//   2. The caller never sees a sqlite3 handle. If I swap the database later,
//      only this file changes.

#include "db.hpp"
#include <vector>
#include <string>

struct Post {
    long long post_id = 0;
    long long user_id = 0;
    long long created_at = 0;
    std::string body;
    std::string author_handle;
};

struct User {
    long long user_id = 0;
    std::string handle;
    std::string display_name;
};

class Repository {
public:
    explicit Repository(Db& db) : db_(db) {}

    // ---- writes ---------------------------------------------------------

    long long create_user(const std::string& handle,
                          const std::string& display_name) {
        auto s = db_.prepare(
            "INSERT INTO users(handle, display_name) VALUES(?, ?);");
        s.bind(1, handle).bind(2, display_name);
        s.step();
        return db_.last_insert_id();
    }

    // INSERT OR IGNORE rather than checking first. The composite primary key
    // already rejects a duplicate, so this makes following someone twice a
    // no-op instead of an error the caller has to handle.
    void follow(long long follower_id, long long followee_id) {
        auto s = db_.prepare(
            "INSERT OR IGNORE INTO follows(follower_id, followee_id) "
            "VALUES(?, ?);");
        s.bind(1, follower_id).bind(2, followee_id);
        s.step();
    }

    void unfollow(long long follower_id, long long followee_id) {
        auto s = db_.prepare(
            "DELETE FROM follows WHERE follower_id = ? AND followee_id = ?;");
        s.bind(1, follower_id).bind(2, followee_id);
        s.step();
    }

    long long create_post(long long user_id, const std::string& body) {
        auto s = db_.prepare("INSERT INTO posts(user_id, body) VALUES(?, ?);");
        s.bind(1, user_id).bind(2, body);
        s.step();
        return db_.last_insert_id();
    }

    void like(long long user_id, long long post_id) {
        auto s = db_.prepare(
            "INSERT OR IGNORE INTO likes(user_id, post_id) VALUES(?, ?);");
        s.bind(1, user_id).bind(2, post_id);
        s.step();
    }

    // ---- reads ----------------------------------------------------------

    bool user_exists(long long user_id) {
        auto s = db_.prepare("SELECT 1 FROM users WHERE user_id = ?;");
        s.bind(1, user_id);
        return s.step();
    }

    bool post_exists(long long post_id) {
        auto s = db_.prepare("SELECT 1 FROM posts WHERE post_id = ?;");
        s.bind(1, post_id);
        return s.step();
    }

    bool get_user(long long user_id, User& out) {
        auto s = db_.prepare(
            "SELECT user_id, handle, display_name FROM users WHERE user_id = ?;");
        s.bind(1, user_id);
        if (!s.step()) return false;
        out = {s.col_int(0), s.col_text(1), s.col_text(2)};
        return true;
    }

    // The feed. This replaces what used to be a filter over an in-memory
    // array in the old flat file version.
    //
    // Written as a JOIN rather than "WHERE user_id IN (subquery)" because the
    // subquery form makes SQLite build the followee list first before it can
    // start on posts. With the join it can drive straight off the follows
    // index into posts.
    std::vector<Post> feed(long long user_id, int limit = 20) {
        auto s = db_.prepare(
            "SELECT p.post_id, p.user_id, p.body, p.created_at, u.handle "
            "FROM follows f "
            "JOIN posts p ON p.user_id = f.followee_id "
            "JOIN users u ON u.user_id = p.user_id "
            "WHERE f.follower_id = ? "
            "ORDER BY p.created_at DESC, p.post_id DESC "
            "LIMIT ?;");
        s.bind(1, user_id).bind(2, static_cast<long long>(limit));

        std::vector<Post> out;
        while (s.step()) {
            out.push_back({s.col_int(0), s.col_int(1), s.col_int(3),
                           s.col_text(2), s.col_text(4)});
        }
        return out;
    }

private:
    Db& db_;
};
