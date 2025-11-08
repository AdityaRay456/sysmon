CXX = g++
CXXFLAGS = -std=c++17 -O2
LDFLAGS = -lncurses

SRC = src/main.cpp
OUT = sysmon

all:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: all
	./$(OUT)

clean:
	rm -f $(OUT)
	