PLUGINS = checkargs.so findmessages.so
SUPPORT = store.cpp

CXXFLAGS = -std=c++20 -fno-rtti -I`gcc -print-file-name=plugin`/include -fpic -shared

all: $(PLUGINS)

%.so: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(SUPPORT) $<

.PHONY: clean
clean:
	-rm -f $(PLUGINS)
