#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <functional>

// Thin RAII wrapper over the SQLite C API.
// Two jobs: make every handle exception-safe, and force prepared
// statements with bound parameters so string concatenation never
// reaches SQL (that is what makes injection structurally impossible).

class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& m) : std::runtime_error(m) {}
};

class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st_, nullptr) != SQLITE_OK)
            throw DbError(std::string("prepare: ") + sqlite3_errmsg(db) + " | " + sql);
    }
    ~Stmt() { if (st_) sqlite3_finalize(st_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    Stmt(Stmt&& o) noexcept : db_(o.db_), st_(o.st_) { o.st_ = nullptr; }

    Stmt& bind(int i, long long v)        { sqlite3_bind_int64(st_, i, v); return *this; }
    Stmt& bind(int i, const std::string& v){
        sqlite3_bind_text(st_, i, v.c_str(), -1, SQLITE_TRANSIENT); return *this; }

    // Returns true while rows remain.
    bool step() {
        int rc = sqlite3_step(st_);
        if (rc == SQLITE_ROW)  return true;
        if (rc == SQLITE_DONE) return false;
        throw DbError(std::string("step: ") + sqlite3_errmsg(db_));
    }
    // Clears the bindings and rewinds so the same prepared statement can be
    // run again. I added this for the seeder in tools/explain.cpp, which
    // inserts 200k rows: preparing a fresh statement per row made seeding take
    // minutes, because every one of them re-parses the SQL.
    void reset() {
        sqlite3_reset(st_);
        sqlite3_clear_bindings(st_);
    }

    long long   col_int (int i) const { return sqlite3_column_int64(st_, i); }
    std::string col_text(int i) const {
        auto p = reinterpret_cast<const char*>(sqlite3_column_text(st_, i));
        return p ? std::string(p) : std::string();
    }
private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* st_ = nullptr;
};

class Db {
public:
    explicit Db(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
            throw DbError(std::string("open: ") + sqlite3_errmsg(db_));
        exec("PRAGMA foreign_keys = ON;");
        exec("PRAGMA journal_mode = WAL;");   // readers never block the writer
    }
    ~Db() { if (db_) sqlite3_close(db_); }
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string m = err ? err : "unknown";
            sqlite3_free(err);
            throw DbError("exec: " + m);
        }
    }
    Stmt prepare(const std::string& sql) { return Stmt(db_, sql); }
    long long last_insert_id() const { return sqlite3_last_insert_rowid(db_); }
    sqlite3* raw() { return db_; }

    // RAII transaction. Rolls back if commit() was never reached.
    class Tx {
    public:
        explicit Tx(Db& db) : db_(db) { db_.exec("BEGIN IMMEDIATE;"); }
        ~Tx() { if (!done_) try { db_.exec("ROLLBACK;"); } catch (...) {} }
        void commit() { db_.exec("COMMIT;"); done_ = true; }
    private:
        Db& db_; bool done_ = false;
    };
private:
    sqlite3* db_ = nullptr;
};
