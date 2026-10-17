-- 001_init.sql
-- Base schema. No indexes here beyond what the PK and UNIQUE constraints
-- create on their own, so that 002 can show the before/after difference.

PRAGMA foreign_keys = ON;

-- Tracks which migrations have run so the runner can skip them next time.
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     INTEGER PRIMARY KEY,
    name        TEXT    NOT NULL,
    applied_at  INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE IF NOT EXISTS users (
    user_id      INTEGER PRIMARY KEY AUTOINCREMENT,
    handle       TEXT    NOT NULL UNIQUE,   -- UNIQUE builds an index for us
    display_name TEXT    NOT NULL,
    created_at   INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE IF NOT EXISTS posts (
    post_id    INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    body       TEXT    NOT NULL,
    created_at INTEGER NOT NULL DEFAULT (unixepoch())
);

-- follower_id follows followee_id.
-- The composite PK does two jobs: it stops duplicate follows, and it is the
-- index used for "who does this user follow".
CREATE TABLE IF NOT EXISTS follows (
    follower_id INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    followee_id INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    PRIMARY KEY (follower_id, followee_id),
    CHECK (follower_id <> followee_id)       -- no following yourself
);

-- Same idea: the pair is the key, so one user can only like a post once.
CREATE TABLE IF NOT EXISTS likes (
    user_id    INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    post_id    INTEGER NOT NULL REFERENCES posts(post_id) ON DELETE CASCADE,
    created_at INTEGER NOT NULL DEFAULT (unixepoch()),
    PRIMARY KEY (user_id, post_id)
);
