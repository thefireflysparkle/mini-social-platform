# mini-social-platform

Social feed service built up across a system design assignment series.

## Progress

- [x] Assignment 7 B1: schema design, SQL vs NoSQL writeup, ER diagram
- [x] Assignment 7 B2: storage moved from flat files to SQLite, endpoints unchanged
- [ ] Assignment 7 B3: indexes and EXPLAIN
- [ ] Assignment 7 B4: the three queries

## Running it

Needs a C++17 compiler and the SQLite development headers.

```bash
sudo apt-get install -y build-essential libsqlite3-dev   # Debian or Ubuntu
make run                                                  # starts on :3000
```

Migrations run on boot, so the first start creates `social.db` and applies
everything in `migrations/`. Starting again applies nothing, because the runner
skips versions already recorded in `schema_migrations`.

## Endpoints

```bash
# create two users
curl -X POST localhost:3000/user -d '{"handle":"manpreet","displayName":"Manpreet Kaur"}'
curl -X POST localhost:3000/user -d '{"handle":"jai"}'

# user 1 follows user 2
curl -X POST localhost:3000/follow -d '{"followerId":1,"followeeId":2}'

# user 2 posts
curl -X POST localhost:3000/post -d '{"authorId":2,"text":"hello"}'

# user 1 reads their feed
curl localhost:3000/feed/1
curl "localhost:3000/feed/1?limit=5"

# like a post
curl -X POST localhost:3000/like -d '{"userId":1,"postId":1}'
```

`GET /feed/:userId` and `POST /post` take and return exactly what they did in
the flat file version. Only the storage behind them changed.

## Layout

```
migrations/001_init.sql   tables, foreign keys, constraints
src/db.hpp                RAII wrapper over the SQLite C API
src/migrate.hpp           migration runner, forward only
src/repository.hpp        every query lives here, all prepared statements
src/http.hpp              small HTTP server on POSIX sockets, no dependencies
src/json.hpp              minimal JSON building and field reading
src/server.cpp            wires the endpoints to the repository
docs/08-db-choice.md      why SQL and why SQLite
docs/er-diagram.txt       ER diagram
```
