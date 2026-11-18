CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
LDLIBS   := -lsqlite3
DB       := social.db
PORT     := 3000

.PHONY: all run clean

all: server

server: src/server.cpp src/db.hpp src/migrate.hpp src/repository.hpp src/http.hpp src/json.hpp
	$(CXX) $(CXXFLAGS) src/server.cpp $(LDLIBS) -o server

# Migrations run automatically on boot, so this creates the database if it
# is not there yet and leaves it alone if it is.
run: server
	./server $(DB) migrations $(PORT)

clean:
	rm -f server $(DB) $(DB)-wal $(DB)-shm
