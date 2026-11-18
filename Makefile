CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
LDLIBS   := -lsqlite3
DB       := social.db
PORT     := 3000

HEADERS  := src/db.hpp src/migrate.hpp src/repository.hpp

.PHONY: all run plans clean

all: server explain

server: src/server.cpp $(HEADERS) src/http.hpp src/json.hpp
	$(CXX) $(CXXFLAGS) src/server.cpp $(LDLIBS) -o server

explain: tools/explain.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) tools/explain.cpp $(LDLIBS) -o explain

# Migrations run on boot, so this creates the database if it is missing.
run: server
	./server $(DB) migrations $(PORT)

# Regenerates both committed query plans from scratch.
# First pass applies 001 only and seeds, so the plan has no indexes to use.
# Second pass applies 002 to the same database, so the data is identical and
# only the indexes differ.
plans: explain
	rm -f $(DB) $(DB)-wal $(DB)-shm
	./explain $(DB) mig_only_001 --seed > docs/explain_before.txt
	./explain $(DB) migrations            > docs/explain_after.txt
	@echo "wrote docs/explain_before.txt and docs/explain_after.txt"

clean:
	rm -f server explain $(DB) $(DB)-wal $(DB)-shm
