# The three queries and why each one is fast

Measured on the same seeded dataset as B3: 2000 users, 200000 posts,
100000 follow edges, 300000 likes. Plans and timings come from
`tools/queries.cpp`, full output in `docs/explain_queries.txt`.

The word to look for in every plan is SEARCH versus SCAN. SCAN reads every
row of a table. SEARCH goes down the B-tree straight to the rows it needs.

## (a) How many followers a user has

```sql
SELECT COUNT(*) FROM follows WHERE followee_id = ?;
```

Plan:

```
SEARCH follows USING COVERING INDEX idx_follows_followee (followee_id=?)
```

Mean: 0.0053 ms.

Why it is fast: "covering index" means everything the query needs is inside
the index itself, so SQLite never opens the table. It finds the first entry
for this user in `idx_follows_followee` and counts entries until the user id
changes. The cost depends on how many followers this one user has, not on how
big the follows table is.

This index exists because of the leftmost prefix rule. The primary key on
follows is `(follower_id, followee_id)`, which can answer "who do I follow"
but not "who follows me", since followee_id is the second column. Without
`idx_follows_followee` this query has to skip through the whole primary key
index.

## (b) Latest 20 posts from people a user follows

```sql
SELECT p.post_id, p.user_id, p.body, p.created_at, u.handle
FROM follows f
JOIN posts p ON p.user_id = f.followee_id
JOIN users u ON u.user_id = p.user_id
WHERE f.follower_id = ?
ORDER BY p.created_at DESC, p.post_id DESC
LIMIT 20;
```

Plan:

```
SEARCH f USING COVERING INDEX sqlite_autoindex_follows_1 (follower_id=?)
SEARCH u USING INTEGER PRIMARY KEY (rowid=?)
SEARCH p USING INDEX idx_posts_user_created (user_id=?)
USE TEMP B-TREE FOR ORDER BY
```

Mean: 1.08 ms. Before the indexes it was 16.6 ms (see B3).

Why it is fast, in three steps. The follows primary key gives the list of
people this user follows in one range read. For each of them,
`idx_posts_user_created` jumps straight to that author's posts, already in
newest-first order. Then users is looked up by `INTEGER PRIMARY KEY`, which in
SQLite is the row id itself, so there is no second index lookup.

I wrote it as a JOIN on purpose. The other way to write it is
`WHERE p.user_id IN (SELECT followee_id FROM follows WHERE follower_id = ?)`,
which makes SQLite build the followee list first before it can start on posts.

What is still not ideal: `USE TEMP B-TREE FOR ORDER BY`. Each author's posts
come out sorted, but merging 50 authors into one timeline still needs a sort.
At 50 followees that is cheap. At thousands it would not be, and the fix there
is building feeds when posts are written rather than when they are read.

## (c) Most liked post in the last 24 hours

```sql
SELECT p.post_id, p.user_id, p.body, p.created_at, u.handle,
       (SELECT COUNT(*) FROM likes l WHERE l.post_id = p.post_id) AS like_count
FROM posts p
JOIN users u ON u.user_id = p.user_id
WHERE p.created_at >= ?
ORDER BY like_count DESC, p.created_at DESC
LIMIT 1;
```

Plan:

```
SCAN u USING COVERING INDEX sqlite_autoindex_users_1
SEARCH p USING INDEX idx_posts_user_created (user_id=? AND created_at>?)
CORRELATED SCALAR SUBQUERY 1
SEARCH l USING COVERING INDEX idx_likes_post (post_id=?)
USE TEMP B-TREE FOR ORDER BY
```

Mean: 9.7 ms.

Why it is fast: it filters before it counts. Only 6601 of the 200000 posts
fall inside the 24 hour window, so the expensive part runs over about 3
percent of the table. Then each post's likes are counted inside
`idx_likes_post`, again a covering index, so no like rows are read.

The interesting bit is that the planner did not use `idx_posts_created` like I
expected. It walks the 2000 users and, for each one, seeks into
`idx_posts_user_created` on `(user_id, created_at > ?)`. Both columns of that
index get used, which gives it a tight range per user. I tried forcing
`idx_posts_created` with `INDEXED BY` and it took 16 to 18 ms against the
planner's 10 to 13, so I left the choice to the planner.

### The version I wrote first

```sql
SELECT p.post_id, ..., COUNT(l.user_id) AS like_count
FROM posts p
JOIN users u ON u.user_id = p.user_id
LEFT JOIN likes l ON l.post_id = p.post_id
WHERE p.created_at >= ?
GROUP BY p.post_id
ORDER BY like_count DESC, p.created_at DESC
LIMIT 1;
```

Plan:

```
SCAN p
SEARCH u USING INTEGER PRIMARY KEY (rowid=?)
SEARCH l USING INDEX idx_likes_post (post_id=?) LEFT-JOIN
USE TEMP B-TREE FOR ORDER BY
```

Mean: 25.2 ms, about 2.6 times slower, with the same indexes available.
Across repeated runs it landed between 2.5 and 3.5 times slower.

`SCAN p` is the reason. With the join to likes in the way, the planner stopped
using the time window to narrow posts and read all 200000 of them. It is the
same answer, but the query shape decided whether the index could be used.

What I took from this: an index only helps if the query is written so the
planner can use it, and the only way to know is to read the plan.
