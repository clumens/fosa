#include <gcc-plugin.h>
#include <plugin-version.h>

#include <diagnostic-core.h>
#include <function.h>
#include <tree.h>
#include <tree-pass.h>
#include <gimple.h>
#include <gimple-iterator.h>
#include <gimple-pretty-print.h>

#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>

#include "fosa.h"

#define PLUGIN_NAME "checkargs"

/* This has to be defined for the plugin to be loaded */
int plugin_is_GPL_compatible;

/* Path to the on-disk store */
char *store = NULL;

/* The on-disk formatted output message store */
msg_map_t msg_map;

/* gcc reported type -> expected type
 *
 * Certain things gcc gets close, but not exactly what we want.  Most of the time, this
 * is some type where "struct" gets added.  Then there's cases like "gchar *" which is
 * typedef'd to "char *".  This map just allows us to fix up all the close enough cases.
 *
 * FIXME: "type alias" means something specific in compiler land, so I should probably
 * call this something else for clarity.
 */
std::unordered_map<std::string, std::string> type_aliases = {
    { "struct GList *",             "GList *" },
    { "struct GHashTable *",        "GHashTable *" },
    { "crm_exit_e",                 "crm_exit_t" },
    { "gchar *",                    "char *" },
    { "pcmk__fence_history",        "enum pcmk__fence_history" },
    { "pcmk_pacemakerd_state",      "enum pcmk_pacemakerd_state" },
    { "struct lrmd_list_t *",       "lrmd_list_t *" },
    { "struct pcmk__location_t *",  "pcmk__location_t *" },
    { "struct pcmk__op_digest_t *", "pcmk__op_digest_t *" },
    { "struct pcmk_action_t *",     "pcmk_action_t *" },
    { "struct pcmk_node_t *",       "pcmk_node_t *" },
    { "struct pcmk_resource_t *",   "pcmk_resource_t *" },
    { "struct pcmk_scheduler_t *",  "pcmk_scheduler_t *" },
    { "struct pcmk_ticket_t *",     "pcmk_ticket_t *" },
    { "struct resource_checks_t *", "resource_checks_t *" },
    { "struct stonith_history_t *", "stonith_history_t *" },
    { "struct xmlNode *",           "xmlNode *" },
    { "long long unsigned int",     "unsigned long long int" },
};

bool is_message_field(tree t) {
    tree name = DECL_NAME(t);

    if (!name) {
        return false;
    }

    return strcmp(IDENTIFIER_POINTER(name), "message") == 0;
}

bool message_from_fn_call(tree t) {
    gimple *def_stmt;
    tree fn, name;
    const char *s = NULL;

    if (TREE_CODE(t) != SSA_NAME) {
        return false;
    }

    def_stmt = SSA_NAME_DEF_STMT(t);

    if (!is_gimple_call(def_stmt)) {
        return false;
    }

    fn = gimple_call_fn(def_stmt);
    name = DECL_NAME(TREE_OPERAND(fn, 0));
    s = IDENTIFIER_POINTER(name);

    if (s == NULL) {
        return false;
    }

    return strcmp(s, "crm_element_name") == 0 || strcmp(s, "crm_map_element_name") == 0;
}

char *print_tree_to_str(tree t) {
    char *buf;
    size_t size;
    FILE *stream;

    stream = open_memstream(&buf, &size);
    print_generic_stmt(stream, t);
    fflush(stream);
    fclose(stream);

    return buf;
}

const char *string_const_from_tree(tree t) {
    tree op = TREE_OPERAND(t, 0);

    if (TREE_CODE(op) != STRING_CST) {
        return NULL;
    }

    return TREE_STRING_POINTER(op);
}

bool target_is_pcmk__output_t(tree t) {
    tree ty = TREE_TYPE(t);
    tree name;

    if (TREE_CODE(ty) != RECORD_TYPE) {
        return false;
    }

    name = TYPE_NAME(ty);
    if (!name || TREE_CODE(name) != TYPE_DECL) {
        return false;
    }

    name = DECL_NAME(name);
    if (!name) {
        return false;
    }

    return strcmp(IDENTIFIER_POINTER(name), "pcmk__output_t") == 0;
}

bool expected_bool_got_int(std::string expected, std::string got) {
    return expected == "bool" && got == "int";
}

bool expected_char_star_got_char_bracket(std::string expected, std::string got) {
    return expected == "char *" && got.starts_with("char[");
}

bool expected_const_got_not_const(std::string expected, std::string got) {
    /* FIXME: Is this really okay? */
    if (expected.starts_with("const ")) {
        return expected.substr(6) == got;
    } else {
        return false;
    }
}

bool expected_time_t_got_int(std::string expected, std::string got) {
    return expected == "time_t" && got == "long int";
}

bool integer_types_match(std::string expected, tree got) {
    if (TYPE_UNSIGNED(got)) {
        std::set<std::string> s = {"unsigned int", "unsigned long", "unsigned long long", "guint"};

        return s.contains(expected) || expected.starts_with("uint");
    } else {
        std::set<std::string> s = {"int", "long", "long long", "gint"};

        return s.contains(expected) || expected.starts_with("int");
    }
}

bool is_pointer_type(std::string t) {
    return t.ends_with("*");
}

bool is_void_pointer(tree t) {
    return TREE_CODE(t) == POINTER_TYPE && TREE_CODE(TREE_TYPE(t)) == VOID_TYPE;
}

bool weird_enums_match(std::string expected, std::string got) {
    std::set<std::string> s = {"enum shadow_disp_flags", "enum pcmk__fence_history"};

    if (got != "int" ) {
        return false;
    }

    return s.contains(expected);
}

bool types_match(std::string expected_ty, std::string got_ty) {
    if (expected_ty == got_ty) {
        return true;

    } else {
        std::string aliased_got_ty;

        if (auto search = type_aliases.find(got_ty); search != type_aliases.end()) {
            aliased_got_ty = search->second;
        } else {
            aliased_got_ty = got_ty;
        }

        if (expected_ty == aliased_got_ty) {
            return true;

        } else if (expected_bool_got_int(expected_ty, aliased_got_ty)) {
            /* Getting an int when we expect a bool is fine. */
            return true;

        } else if (expected_char_star_got_char_bracket(expected_ty, aliased_got_ty)) {
            /* Getting a "char[]" when we expect "char *" is fine. */
            return true;

        } else if (expected_const_got_not_const(expected_ty, aliased_got_ty)) {
            /* Getting a "XYZ *" when we expect "const XYZ *" is fine (I think) */
            return true;

        } else if (expected_time_t_got_int(expected_ty, aliased_got_ty)) {
            /* Getting a long int when we expect a time_t is fine. */
            return true;

        } else if (weird_enums_match(expected_ty, aliased_got_ty)) {
            /* FIXME: gcc sees certain enums as int and certain others as an actual
             * enum.  The latter seems to be ones where there's an "xyz_invalid = -1"
             * element defined.  For those, other comparisons work fine.  For the
             * former, we still need to do the comparison manually.
             *
             * This seems like something to fix, but how?
             */
            return true;
        }
    }

    return false;
}

void check_arg_types(param_list_t expected_params, gimple *stmt) {
    unsigned int n = 2;

    for (const auto& expected_ty : expected_params) {
        tree arg_tree = gimple_call_arg(stmt, n);
        tree ty = TREE_TYPE(arg_tree);
        std::string got_ty = print_tree_to_str(ty);
        bool match;

        /* Remove the trailing newline that the gcc pretty printer adds. */
        std::erase(got_ty, '\n');

        /* Some type checks we do early because we need to inspect the tree instead
         * of just comparing strings.
         */

        if (is_pointer_type(expected_ty) && is_void_pointer(ty)) {
            /* If we are expecting a pointer type and were given a "void *", that's fine. */
            n++;
            continue;

        } else if (TREE_CODE(ty) == INTEGER_TYPE) {
            /* Integer types are difficult because all the fine grained types are aliased
             * that get lost somewhere in the internals.  Plus, things like "int" and "long"
             * might mean different things on different platforms.  So we can really only
             * perform basic checks.
             */
            if (integer_types_match(expected_ty, ty)) {
                n++;
                continue;
            }
        }

        match = types_match(expected_ty, got_ty);
        if (!match) {
            /* If the types don't match, see if they both start with "const ".
             * If so, strip that off and try the comparison again.
             */
            if (expected_ty.starts_with("const ") && got_ty.starts_with("const ")) {
                std::string new_expected_ty = expected_ty.substr(6);
                std::string new_got_ty = got_ty.substr(6);

                match = types_match(new_expected_ty, new_got_ty);
            }
        }

        if (!match) {
            /* +1 is because params are zero-indexed, but users will start counting with 1 */
            error_at(stmt->location, "Expected '%s', but got '%s' in argument %d",
                     expected_ty.c_str(), got_ty.c_str(), n+1);
        }

        n++;
    }
}

bool valid_function_call(gimple *stmt) {
    tree fn, fn_called;
    tree target, field;
    gimple *def_stmt;

    /* Filter out anything that's not a function call */
    if (!is_gimple_call(stmt)) {
        return false;
    }

    /* Grab the function call statement.  In our example, that would be
     * something like:
     *
     * _15 (logger_out.120_16, "xml-patchset", patchset);
     */
    fn = gimple_call_fn(stmt);

    /* Filter out anything that's not a function pointer reference.  Basically,
     * this is anything where the reference is stored in some SSA name.
     */
    if (fn == NULL || TREE_CODE(fn) != SSA_NAME) {
        return false;
    }

    /* Grab the statement that defines the SSA name in use in the function call
     * statement.  In our example, that would be something like:
     *
     * _15 = logger_out.119_14->message;
     */
    def_stmt = SSA_NAME_DEF_STMT(fn);

    /* I think this is always going to be the case, but better to be safe. */
    if (!is_gimple_assign(def_stmt)) {
        return false;
    }

    /* Grab the first argument on the right hand side of the assignment.  In our
     * example, that would be something like:
     *
     * logger_out.119_14->message;
     */
    fn_called = gimple_assign_rhs1(def_stmt);

    if (fn_called == NULL || TREE_CODE(fn_called) != COMPONENT_REF) {
        return false;
    }

    /* The target is the object being dereferenced, and the field is the part of the
     * object being accessed.  In our example, this is:
     *
     * target = *logger_out.119_14;
     * field = message
     */
    target = TREE_OPERAND(fn_called, 0);
    field = TREE_OPERAND(fn_called, 1);

    /* Filter out anything where the target isn't a pcmk__output_t and the field
     * isn't "message".
     */
    if (!target_is_pcmk__output_t(target) || !is_message_field(field)) {
        return false;
    }

    return true;
}

void check_message(gimple *stmt, const char *msg_name) {
    unsigned int num_args = gimple_call_num_args(stmt);
    auto expected_params = msg_map.find(msg_name);

    /* Verify that the message name exists in the store. */
    if (expected_params == msg_map.end()) {
        tree t = gimple_call_arg(stmt, 1);
        error_at(EXPR_LOCATION(t), "Unknown output message: %s", msg_name);
        return;
    }

    /* Verify that enough arguments were provided to the message.  The expected length
     * does not include the first two arguments to the out->message() call, which are
     * the pcmk__output_t and the message name itself.
     */
    if (num_args != expected_params->second.size() + 2) {
        tree t = gimple_call_arg(stmt, 1);
        error_at(EXPR_LOCATION(t), "Expected %ld argument(s) to message '%s', but got %d",
                 expected_params->second.size(), msg_name, num_args - 2);
        return;
    }

    /* And then check that argument types are as expected. */
    check_arg_types(expected_params->second, stmt);
}

void find_function_calls(void *gcc_data, void *user_data) {
    opt_pass *pass = (opt_pass *) gcc_data;
    basic_block bb;
    gimple_stmt_iterator gsi;

    /* Only continue for one pass. */
    if (strcmp(pass->name, "*warn_function_return") != 0) {
        return;
    }

    /* Iterate over all the basic blocks in the current function */
    FOR_EACH_BB_FN(bb, cfun) {
        /* Iterate over all the statements in the basic block */
        for (gsi = gsi_start_bb(bb); !gsi_end_p(gsi); gsi_next(&gsi)) {
            gimple *stmt = gsi_stmt(gsi);
            tree msg_tree;
            unsigned int num_args;

            /* We are looking for a line line this:
             *
             * out->message(out, "xml-patchset", patchset);
             *
             * The return value can optionally be assigned to something (which we don't
             * care about), and the list of arguments after "xml-patchset" is optional,
             * and could be pretty long.  It depends on the message.
             */
            if (!valid_function_call(stmt)) {
                continue;
            }

            /* The first two arguments are the pcmk__output_t object and the name
             * of the message being called.  The compiler should have caught any cases
             * where this is wrong, but just in case...
             */
            num_args = gimple_call_num_args(stmt);
            if (num_args < 2) {
                continue;
            }

            /* Get the tree containing the message name.  This is typically just a
             * string constant, but it could be other things.
             */
            msg_tree = gimple_call_arg(stmt, 1);

            if (TREE_CODE(msg_tree) == SSA_NAME && message_from_fn_call(msg_tree)) {
                /* This is a call to the message function that figures out the message
                 * name by calling some other function, likely crm_map_element_name but
                 * others could show up in the future.  We can't figure out exactly
                 * which message will be called at compile time, but so far it's only
                 * ever been one of these four.  Iterate over each and check.  They
                 * should all have the same arguments.
                 */
                check_message(stmt, "bundle");
                check_message(stmt, "clone");
                check_message(stmt, "group");
                check_message(stmt, "primitive");

            } else if (TREE_CODE(msg_tree) == ADDR_EXPR) {
                /* This is a call to the message function that uses a string literal
                 * for the message name.  That's easy.
                 */
                const char *msg_name = string_const_from_tree(msg_tree);
                if (msg_name == NULL) {
                    error_at(EXPR_LOCATION(msg_tree), "Cannot figure out message name");
                }

                check_message(stmt, msg_name);

            } else {
                /* This is a call to the message function that uses some other method
                 * to determine the message name.  Error on it for now so I can try
                 * to track it down.
                 */
                error_at(EXPR_LOCATION(msg_tree), "Cannot figure out message name");
            }
        }
    }
}

int plugin_init(struct plugin_name_args *plugin_info, struct plugin_gcc_version *ver) {
    if (!plugin_default_version_check(ver, &gcc_version)) {
        return 1;
    }

    store = store_location(plugin_info->argv);

    if (!store) {
        std::cerr << "-fplugin-arg-checkargs-store= argument is missing\n";
        return 1;
    };

    read_store(store, &msg_map);
    if (msg_map.empty()) {
        std::cerr << "Output message store is empty\n";
        return 1;
    }

    /* Register a callback function for when a pass is about to be executed */
    register_callback(PLUGIN_NAME, PLUGIN_PASS_EXECUTION, find_function_calls, NULL);

    return 0;
}
