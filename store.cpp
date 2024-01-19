#include <gcc-plugin.h>

#include <fstream>

#include "fosa.h"

static std::list<std::string> split(std::string s, std::string delim) {
    std::list<std::string> retval;
    std::string ele;

    auto start = 0;
    auto end = s.find(delim);

    while (end != std::string::npos) {
        ele = s.substr(start, end-start);
        retval.push_back(ele);
        start = end + delim.length();
        end = s.find(delim, start);
    }

    /* Add whatever was after the last delimiter (or, the message name if it had no
     * parameters and therefore no delimiter so the above loop never ran).
     */
    ele = s.substr(start, end-start);
    retval.push_back(ele);

    return retval;
}

void read_store(char *store, msg_map_t msg_map) {
    std::string line;
    std::ifstream in(store);

    while (std::getline(in, line)) {
        /* FIXME: Handle errors from the split function */
        std::list<std::string> parts = split(line, "|");
        std::string msg_name = parts.front();

        parts.pop_front();
        msg_map.insert({msg_name, parts});
    }
}


/* Find the required -fplugin-arg-<plugin>-store= command line argument and return
 * its value, or NULL if not found.
 */
char *store_location(struct plugin_argument *argv) {
    for (struct plugin_argument *arg = argv; arg != NULL; arg++) {
        if (strcmp(arg->key, "store") == 0) {
            return arg->value;
        }
    }

    return NULL;
}

void write_store(char *store, msg_map_t msg_map) {
    std::ofstream out(store);

    /* Each formatted output message is a single line - message name, then parameters,
     * all separated by pipes.
     */
    for (const auto& [key, val] : msg_map) {
        out << key;

        for (const auto& param : val) {
            out << "|" << param;
        }

        out << "\n";
    }

    out.close();
}
