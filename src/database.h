#pragma once

#include <sqlite3.h>
#include <string>

namespace iris {

constexpr const char* kDatabasePath = "iris.db";

class Statement {
public:
    Statement(sqlite3* database, const char* sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() const noexcept { return statement_; }
    void execute();

private:
    sqlite3* database_{};
    sqlite3_stmt* statement_{};
};

void executeSql(sqlite3* database, const char* sql);
void createSharedSchema(sqlite3* database);

} // namespace iris
