PLUGINS = checkargs.so findmessages.so
SUPPORT = store.cpp

CXXFLAGS = -fno-rtti -I`gcc -print-file-name=plugin`/include -fpic -shared

all: $(PLUGINS)

%.so: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(SUPPORT) $<

.PHONY: clean
clean:
	-rm -f $(PLUGINS)
