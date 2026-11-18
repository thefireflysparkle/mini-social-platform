#pragma once
#include "db.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>

// Forward-only migration runner. Reads migrations/NNN_name.sql in
// numeric order, skips versions already recorded, and applies each
// inside a transaction so a half-applied migration cannot exist.

inline void run_migrations(Db& db, const std::string& dir) {
    db.exec("CREATE TABLE IF NOT EXISTS schema_migrations ("
            "version INTEGER PRIMARY KEY, name TEXT NOT NULL,"
            "applied_at INTEGER NOT NULL DEFAULT (unixepoch()));");

    std::map<int,int> applied;
    { auto s = db.prepare("SELECT version FROM schema_migrations;");
      while (s.step()) applied[(int)s.col_int(0)] = 1; }

    std::map<int, std::filesystem::path> files;
    for (auto& e : std::filesystem::directory_iterator(dir)) {
        auto n = e.path().filename().string();
        if (n.size() < 4 || n.substr(n.size()-4) != ".sql") continue;
        files[std::stoi(n.substr(0,3))] = e.path();
    }

    for (auto& [ver, path] : files) {
        if (applied.count(ver)) continue;
        std::ifstream f(path);
        std::string sql((std::istreambuf_iterator<char>(f)), {});

        Db::Tx tx(db);
        db.exec(sql);
        auto s = db.prepare("INSERT INTO schema_migrations(version,name) VALUES(?,?);");
        s.bind(1, (long long)ver).bind(2, path.filename().string());
        s.step();
        tx.commit();
        std::cout << "applied " << path.filename().string() << "\n";
    }
}
