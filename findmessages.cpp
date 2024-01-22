#include <gcc-plugin.h>
#include <plugin-version.h>

#include <diagnostic-core.h>
#include <tree.h>

#include <string.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <list>
#include <sstream>
#include <unordered_map>

#include "fosa.h"

#define PLUGIN_NAME "findmessages"

/* This has to be defined for the plugin to be loaded */
int plugin_is_GPL_compatible;

/* Path to the on-disk store */
char *store = NULL;
bool updated_store = false;

msg_map_t msg_map;

/* Convert a GCC TREE_CHAIN into a list of parameters */
param_list_t build_list_from_tree_chain(tree t) {
    param_list_t args;

    while (t) {
        tree arg_tree = TREE_VALUE(t);

        if (TREE_CODE(arg_tree) != STRING_CST) {
            error_at(EXPR_LOCATION(arg_tree), "Output message argument must be a string");

            /* I don't think the return value matters here becauase we'll bail
             * due to error_at above but just in case, clear the list.
             */
            args.clear();
            return args;
        }

        args.push_back(TREE_STRING_POINTER(arg_tree));

        t = TREE_CHAIN(t);
    }

    return args;
}

bool streq(std::string a, std::string b) {
    return a == b;
}

/* Compare two param_list_t objects for equality */
bool param_lists_identical(param_list_t a, param_list_t b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), streq);
}

std::string build_param_mismatch_err(std::string msg_name, param_list_t expected, param_list_t got) {
    std::ostringstream ret;

    ret << "Parameter list for `" << msg_name << "' is different from previous definition.\n"
        << "\tExpected:";

    for (const auto& param : expected) {
        ret << " " << param;
    }

    ret << "\n\tGot     :";

    for (const auto& param : got) {
        ret << " " << param;
    }

    ret << "\n";
    return ret.str();
}

tree output_args_attr_handler(tree *node, tree name, tree args, int flags, bool *no_add_attrs)
{
    std::string msg_name;
    param_list_t new_params;
    tree msg_tree;

    if (TREE_CODE(args) != TREE_LIST) {
        return NULL;
    }

    /* The first element of the args list is the message name */
    msg_tree = TREE_VALUE(args);

    /* Don't know why this would ever happen, either */
    if (TREE_CODE(msg_tree) != STRING_CST) {
        error_at(EXPR_LOCATION(msg_tree), "Output message must be a string");
        return NULL;
    }

    /* If the msg_map is empty, initialize it by reading in the on-disk store */
    if (msg_map.empty() && std::filesystem::exists(store)) {
        read_store(store, &msg_map);
    }

    /* Build up a list of parameter types by moving to the next argument in the tree
     * chain and going from there.
     */
    args = TREE_CHAIN(args);
    new_params = build_list_from_tree_chain(args);

    msg_name = TREE_STRING_POINTER(msg_tree);

    auto existing_params = msg_map.find(msg_name);
    if (existing_params != msg_map.end()) {
        /* This message was already stored in msg_map.  Verify its parameter list
         * is identical to what we already know.
         */
        if (!param_lists_identical(existing_params->second, new_params)) {
            std::string err_msg = build_param_mismatch_err(msg_name, existing_params->second, new_params);
            error_at(EXPR_LOCATION(msg_tree), err_msg.c_str());
            return NULL;
        }
    } else {
        /* This is a message we haven't seen before, so add it to the store. */
        msg_map.insert({msg_name, new_params});
        updated_store = true;
    }

    /* Do I actually need to return something here? */
    return NULL;
}

void fo_attr_cb(void *gcc_data, void *user_data) {
    struct attribute_spec *attr = NULL;

    /* Register an __attribute__ with a function to handle it. */
    attr = new attribute_spec;
    memset(attr, 0, sizeof(struct attribute_spec));

    attr->name = "output_args";
    attr->min_length = 1;
    attr->max_length = -1;
    attr->decl_required = false;
    attr->type_required = false;
    attr->function_type_required = false;
    attr->affects_type_identity = false;
    attr->handler = output_args_attr_handler;

    register_attribute(attr);
}

void unit_finished_cb(void *gcc_data, void *user_data) {
    if (!updated_store) {
        return;
    }

    write_store(store, msg_map);
}

int plugin_init(struct plugin_name_args *plugin_info, struct plugin_gcc_version *ver) {
    if (!plugin_default_version_check(ver, &gcc_version)) {
        return 1;
    }

    store = store_location(plugin_info->argv);

    if (!store) {
        std::cerr << "-fplugin-arg-findmessages-store= argument is missing\n";
        return 1;
    };

    /* Register a callback function for when the PCMK__OUTPUT_ARGS attribute is seen */
    register_callback(PLUGIN_NAME, PLUGIN_ATTRIBUTES, fo_attr_cb, NULL);
    /* Register a callback function for when GCC is done */
    register_callback(PLUGIN_NAME, PLUGIN_FINISH_UNIT, unit_finished_cb, NULL);

    return 0;
}
