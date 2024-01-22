#include <list>
#include <unordered_map>

/* The list of arguments to a message */
typedef std::list<std::string> param_list_t;

/* Map a message name to its list of parameters */
typedef std::unordered_map<std::string, param_list_t> msg_map_t;

void read_store(char *store, msg_map_t *msg_map);
char *store_location(struct plugin_argument *argv);
void write_store(char *store, msg_map_t msg_map);
