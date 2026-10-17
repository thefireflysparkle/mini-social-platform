# mini-social-platform

Social feed service built up across a system design assignment series.

## Progress

- [x] Assignment 7 B1: schema design, SQL vs NoSQL writeup, ER diagram
- [ ] Assignment 7 B2: migrate storage to the database
- [ ] Assignment 7 B3: indexes and EXPLAIN
- [ ] Assignment 7 B4: the three queries

## Layout so far

```
migrations/001_init.sql   tables, foreign keys, constraints
docs/08-db-choice.md      why SQL and why SQLite
docs/er-diagram.txt       ER diagram
```

## Applying the schema

```bash
sqlite3 social.db < migrations/001_init.sql
```
