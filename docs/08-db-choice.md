# Why I went with SQL (SQLite) for this

## The decision

Relational database, SQLite, talked to through the C API with a small RAII
wrapper I wrote in `src/db.hpp`.

## How I got there

I started by writing down the three things the app actually has to do, because
I kept going in circles when I thought about it as "which database is better".

1. Show me the latest posts from everyone I follow.
2. Tell me how many followers someone has.
3. Tell me which post got the most likes today.

Query 1 is the one that decided it. To build a feed you first have to find out
who the user follows, then go and get posts from each of those people, then sort
the combined result by time. That is a join. If I used a document store I would
have two options: either store a pre-built feed document per user, or write the
join myself in C++ by looping over followees and merging. The first one is a
fan-out strategy, not a storage decision, and it is what Assignment 11 is about
anyway. The second is just a join, except I wrote it badly and it is not
indexed.

The second thing that pushed me to SQL is that follows and likes are both
many-to-many relationships with a rule attached. A user cannot follow the same
person twice. A user cannot like the same post twice. In SQL I get that for free
with a composite primary key. In a document model I would have to remember to
check for duplicates in every code path that writes a follow, and I know I would
eventually forget one.

Third, I do not know all the queries yet. Assignments 8 to 11 add sharding,
messaging and fan-out, and I expect to query these tables in ways I have not
thought of. A relational schema with a query planner lets me add an index later
instead of restructuring how the data is stored.

## Why SQLite and not Postgres

Mostly so the repo actually runs when someone clones it. SQLite is a single file
and needs no server, so `make seed` just works. It still gives me the things
this assignment measures: `EXPLAIN QUERY PLAN`, real transactions, foreign keys,
and WAL mode so readers do not block the writer.

The honest downside is that SQLite serialises writers at the database level, so
there is exactly one writer at a time. At the scale I am testing (200k posts)
that is fine. It would not be fine in production, and that limitation is
basically the reason the next assignment is about sharding.

## Schema

```sql
CREATE TABLE users (
    user_id      INTEGER PRIMARY KEY AUTOINCREMENT,
    handle       TEXT    NOT NULL UNIQUE,
    display_name TEXT    NOT NULL,
    created_at   INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE posts (
    post_id    INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    body       TEXT    NOT NULL,
    created_at INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE follows (
    follower_id INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    followee_id INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    PRIMARY KEY (follower_id, followee_id),
    CHECK (follower_id <> followee_id)
);

CREATE TABLE likes (
    user_id    INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    post_id    INTEGER NOT NULL REFERENCES posts(post_id) ON DELETE CASCADE,
    created_at INTEGER NOT NULL DEFAULT (unixepoch()),
    PRIMARY KEY (user_id, post_id)
);
```

This is in 3NF. Every non-key column depends on the whole key and nothing else.
The thing I had to stop myself doing was putting `author_handle` on `posts` so
the feed query would not need the join. That would have been a transitive
dependency, since the handle depends on `user_id` and not on `post_id`, and it
would mean a handle change has to rewrite every post that user ever made.

## Four decisions I had to think about

**`created_at` as INTEGER, not TEXT.** I stored epoch seconds instead of an ISO
string. Integers compare in one instruction, take 8 bytes, need no parsing, and
sort correctly as a raw index key. A text timestamp only sorts correctly if the
format is strictly fixed width, and then I am still doing string comparison on
every row of a range scan.

**Composite primary keys on `follows` and `likes` instead of an id column.** My
first version had `follow_id INTEGER PRIMARY KEY` plus the two user columns.
Then I realised I still needed a unique index on `(follower_id, followee_id)` to
stop duplicate follows, which meant storing the pair twice. Making the pair the
primary key gives me uniqueness and the lookup index in one structure.

**The direction of the `follows` columns matters more than I expected.** The
composite primary key `(follower_id, followee_id)` only helps for queries that
know `follower_id`. Asking "who follows this user" filters on the second column,
and a composite index cannot be used that way. That is the leftmost prefix rule
and it is why I need a separate index on `followee_id` in the next part.

**`CHECK (follower_id <> followee_id)`.** Stops a user following themselves. I
could have validated this in the application, but then every endpoint that
writes a follow has to remember, and the database is the one place that cannot
be bypassed.

## ON DELETE CASCADE

Every foreign key cascades. If a user is deleted, their posts, their follow
edges in both directions, and their likes all go with them. The alternative is
rows pointing at a user that no longer exists, and then every read query needs
a join to check whether the author still exists. I would rather the database
clean up than add that check to every query.
