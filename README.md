# mini-social-platform

Social feed service built up across a system design assignment series.

## Progress

- [x] Assignment 7 B1: schema design, SQL vs NoSQL writeup, ER diagram
- [x] Assignment 7 B2: storage moved from flat files to SQLite, endpoints unchanged
- [x] Assignment 7 B3: indexes, with the feed query plan before and after
- [x] Assignment 7 B4: three optimized queries, see docs/08-queries.md

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
migrations/002_indexes.sql four indexes for the hot queries
mig_only_001/             copy of 001 alone, used to capture the before plan
tools/explain.cpp         seeds data and prints EXPLAIN QUERY PLAN plus timings
tools/queries.cpp         plans and timings for the three B4 queries
src/db.hpp                RAII wrapper over the SQLite C API
src/migrate.hpp           migration runner, forward only
src/repository.hpp        every query lives here, all prepared statements
src/http.hpp              small HTTP server on POSIX sockets, no dependencies
src/json.hpp              minimal JSON building and field reading
src/server.cpp            wires the endpoints to the repository
docs/08-db-choice.md      why SQL and why SQLite
docs/er-diagram.txt       ER diagram
docs/explain_before.txt   feed query plan with no indexes
docs/explain_after.txt    same query, same data, with the indexes
docs/08-queries.md        the three queries and why each is efficient
docs/explain_queries.txt  raw output from tools/queries.cpp
```

## Indexes and the feed query

`make plans` regenerates both committed outputs from an empty database.
Dataset is 2000 users, 200000 posts, 100000 follow edges and 300000 likes,
built from a fixed random seed so the numbers reproduce.

Without indexes the plan starts with `SCAN p`, meaning every one of the 200000
post rows is read:

```
SCAN p
BLOOM FILTER ON f (follower_id=? AND followee_id=?)
SEARCH f USING INDEX sqlite_autoindex_follows_1 (follower_id=? AND followee_id=?)
SEARCH u USING INTEGER PRIMARY KEY (rowid=?)
USE TEMP B-TREE FOR ORDER BY
```

With them it becomes `SEARCH p USING INDEX`, a seek into the B-tree instead:

```
SEARCH f USING COVERING INDEX sqlite_autoindex_follows_1 (follower_id=?)
SEARCH u USING INTEGER PRIMARY KEY (rowid=?)
SEARCH p USING INDEX idx_posts_user_created (user_id=?)
USE TEMP B-TREE FOR ORDER BY
```

Mean feed latency over 200 runs went from 16.6 ms to 0.92 ms, about 18 times
faster. Repeated runs land between 0.7 and 1.2 ms, so the exact millisecond
figure moves around. The plan change is the reliable result, not the timing.

`USE TEMP B-TREE FOR ORDER BY` is still there afterwards. The composite index
returns each author's posts already in time order, but merging 50 authors into
one global ordering still needs a sort. At 50 followees that costs nothing. It
would matter at 5000, and the fix for that is not an index, it is building the
feed on write instead of on read.
