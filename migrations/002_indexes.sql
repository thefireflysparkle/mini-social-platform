-- 002_indexes.sql
-- The indexes that make the three queries in B4 fast.
--
-- The assignment suggests indexing posts.user_id, follows.follower_id and
-- posts.created_at. I ended up with a slightly different set, and the reason
-- is worth writing down.

-- The feed query needs "posts by this author, newest first". A plain index on
-- user_id would find the author's posts but SQLite would still have to sort
-- them. Putting created_at in the index after user_id means the rows come back
-- already in time order, so the sort disappears for the per-author part.
--
-- This also covers plain lookups on user_id on its own, because user_id is the
-- leftmost column. So I did not need a separate index on posts.user_id.
CREATE INDEX IF NOT EXISTS idx_posts_user_created
    ON posts (user_id, created_at DESC);

-- "Posts in the last 24 hours" filters on created_at with no user in the
-- predicate. The composite index above cannot serve that, because created_at
-- is the second column and an index can only be used from the left. This is
-- the leftmost prefix rule and it is the thing I got wrong first.
CREATE INDEX IF NOT EXISTS idx_posts_created
    ON posts (created_at DESC);

-- Here is where I disagreed with the suggestion in the brief.
--
-- follows already has PRIMARY KEY (follower_id, followee_id), and SQLite builds
-- an index for that automatically. follower_id is its leftmost column, so
-- "who does this user follow" is already indexed and adding another index on
-- follower_id would just be a second copy of the same thing.
--
-- What is NOT covered is the other direction. "Who follows this user" filters
-- on followee_id, the second column, which that index cannot serve. So this is
-- the index that actually buys something.
CREATE INDEX IF NOT EXISTS idx_follows_followee
    ON follows (followee_id);

-- Counting likes per post. likes has PRIMARY KEY (user_id, post_id), so again
-- the automatic index starts with the wrong column for this. Indexing post_id
-- lets a count run entirely inside the index without reading table rows.
CREATE INDEX IF NOT EXISTS idx_likes_post
    ON likes (post_id);
