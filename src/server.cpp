// The HTTP layer. Same two endpoints the flat file version had:
//
//   GET  /feed/:userId        latest posts from people this user follows
//   POST /post                create a post
//
// Plus a few small ones I needed to seed data over HTTP so I could test the
// endpoints without a separate script.
//
// The point of this part of the assignment is that the request and response
// shapes did not change. Only what sits behind them did: the flat file is
// gone and every call now goes through Repository into SQLite.

#include "db.hpp"
#include "migrate.hpp"
#include "repository.hpp"
#include "http.hpp"
#include "json.hpp"

#include <iostream>
#include <sstream>
#include <cstdlib>

static std::string posts_to_json(long long user_id,
                                 const std::vector<Post>& posts) {
    std::ostringstream o;
    o << "{\"userId\":" << user_id
      << ",\"count\":" << posts.size()
      << ",\"items\":[";
    for (size_t i = 0; i < posts.size(); ++i) {
        const Post& p = posts[i];
        if (i) o << ",";
        o << "{\"postId\":"      << p.post_id
          << ",\"authorId\":"    << p.user_id
          << ",\"author\":\""    << json_escape(p.author_handle) << "\""
          << ",\"text\":\""      << json_escape(p.body) << "\""
          << ",\"createdAt\":"   << p.created_at << "}";
    }
    o << "]}";
    return o.str();
}

int main(int argc, char** argv) {
    std::string db_path = argc > 1 ? argv[1] : "social.db";
    std::string mig_dir = argc > 2 ? argv[2] : "migrations";
    int port = argc > 3 ? std::atoi(argv[3]) : 3000;

    try {
        Db db(db_path);
        run_migrations(db, mig_dir);      // safe to run every boot
        Repository repo(db);

        HttpServer server;

        // GET /feed/:userId?limit=20
        server.route([&](const Request& req, Response& res) {
            if (req.method != "GET") return false;
            if (req.segments.size() != 2 || req.segments[0] != "feed") return false;

            long long user_id = 0;
            try {
                user_id = std::stoll(req.segments[1]);
            } catch (...) {
                res.status = 400;
                res.body = R"({"error":"userId must be a number"})";
                return true;
            }

            int limit = 20;
            auto it = req.query.find("limit");
            if (it != req.query.end()) {
                try { limit = std::stoi(it->second); } catch (...) {}
            }
            if (limit < 1)   limit = 1;
            if (limit > 100) limit = 100;   // stop a caller asking for everything

            res.body = posts_to_json(user_id, repo.feed(user_id, limit));
            return true;
        });

        // POST /post   {"authorId": 1, "text": "hello"}
        server.route([&](const Request& req, Response& res) {
            if (req.method != "POST" || req.path != "/post") return false;

            long long author_id = 0;
            std::string text;
            if (!json_get_int(req.body, "authorId", author_id) ||
                !json_get_string(req.body, "text", text) || text.empty()) {
                res.status = 400;
                res.body = R"({"error":"authorId and text are required"})";
                return true;
            }
            if (!repo.user_exists(author_id)) {
                res.status = 400;
                res.body = R"({"error":"author does not exist"})";
                return true;
            }

            long long post_id = repo.create_post(author_id, text);
            res.status = 201;
            res.body = "{\"postId\":" + std::to_string(post_id)
                     + ",\"authorId\":" + std::to_string(author_id) + "}";
            return true;
        });

        // POST /user   {"handle": "manpreet", "displayName": "Manpreet"}
        server.route([&](const Request& req, Response& res) {
            if (req.method != "POST" || req.path != "/user") return false;

            std::string handle, name;
            if (!json_get_string(req.body, "handle", handle) || handle.empty()) {
                res.status = 400;
                res.body = R"({"error":"handle is required"})";
                return true;
            }
            if (!json_get_string(req.body, "displayName", name)) name = handle;

            try {
                long long id = repo.create_user(handle, name);
                res.status = 201;
                res.body = "{\"userId\":" + std::to_string(id)
                         + ",\"handle\":\"" + json_escape(handle) + "\"}";
            } catch (const DbError&) {
                // The UNIQUE constraint on handle is what rejects this.
                res.status = 400;
                res.body = R"({"error":"handle already taken"})";
            }
            return true;
        });

        // POST /follow   {"followerId": 1, "followeeId": 2}
        server.route([&](const Request& req, Response& res) {
            if (req.method != "POST" || req.path != "/follow") return false;

            long long a = 0, b = 0;
            if (!json_get_int(req.body, "followerId", a) ||
                !json_get_int(req.body, "followeeId", b)) {
                res.status = 400;
                res.body = R"({"error":"followerId and followeeId are required"})";
                return true;
            }
            if (a == b) {
                res.status = 400;
                res.body = R"({"error":"cannot follow yourself"})";
                return true;
            }
            repo.follow(a, b);
            res.status = 201;
            res.body = R"({"ok":true})";
            return true;
        });

        // POST /like   {"userId": 1, "postId": 2}
        server.route([&](const Request& req, Response& res) {
            if (req.method != "POST" || req.path != "/like") return false;

            long long u = 0, p = 0;
            if (!json_get_int(req.body, "userId", u) ||
                !json_get_int(req.body, "postId", p)) {
                res.status = 400;
                res.body = R"({"error":"userId and postId are required"})";
                return true;
            }
            // Checking first gives a clear 400. Without this the foreign
            // key constraint throws and the caller gets a useless 500.
            if (!repo.user_exists(u) || !repo.post_exists(p)) {
                res.status = 400;
                res.body = R"({"error":"user or post does not exist"})";
                return true;
            }
            repo.like(u, p);
            res.status = 201;
            res.body = R"({"ok":true})";
            return true;
        });

        server.listen_and_serve(port);
    } catch (const DbError& e) {
        std::cerr << "database error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
