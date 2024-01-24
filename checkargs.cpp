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

#include "fosa.h"

#define PLUGIN_NAME "checkargs"

/* This has to be defined for the plugin to be loaded */
int plugin_is_GPL_compatible;

/* Path to the on-disk store */
char *store = NULL;

msg_map_t msg_map;

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

const char *type_alias(const char *ty) {
    if (strcmp(ty, "struct GList *") == 0) {
        return "GList *";
    } else if (strcmp(ty, "struct GHashTable *") == 0) {
        return "GHashTable *";
    } else if (strcmp(ty, "crm_exit_e") == 0) {
        return "crm_exit_t";
    } else if (strcmp(ty, "gchar *") == 0) {
        return "char *";
    } else if (strcmp(ty, "pcmk__fence_history") == 0) {
        return "enum pcmk__fence_history";
    } else if (strcmp(ty, "pcmk_pacemakerd_state") == 0) {
        return "enum pcmk_pacemakerd_state";
    } else if (strcmp(ty, "struct lrmd_list_t *") == 0) {
        return "lrmd_list_t *";
    } else if (strcmp(ty, "struct pcmk__location_t *") == 0) {
        return "pcmk__location_t *";
    } else if (strcmp(ty, "struct pcmk__op_digest_t *") == 0) {
        return "pcmk__op_digest_t *";
    } else if (strcmp(ty, "struct pcmk_action_t *") == 0) {
        return "pcmk_action_t *";
    } else if (strcmp(ty, "struct pcmk_node_t *") == 0) {
        return "pcmk_node_t *";
    } else if (strcmp(ty, "struct pcmk_resource_t *") == 0) {
        return "pcmk_resource_t *";
    } else if (strcmp(ty, "struct pcmk_scheduler_t *") == 0) {
        return "pcmk_scheduler_t *";
    } else if (strcmp(ty, "struct pcmk_ticket_t *") == 0) {
        return "pcmk_ticket_t *";
    } else if (strcmp(ty, "struct resource_checks_t *") == 0) {
        return "resource_checks_t *";
    } else if (strcmp(ty, "struct stonith_history_t *") == 0) {
        return "stonith_history_t *";
    } else if (strcmp(ty, "struct xmlNode *") == 0) {
        return "xmlNode *";
    } else if (strcmp(ty, "long long unsigned int") == 0) {
        return "unsigned long long int";
    } else {
        return ty;
    }
}

bool expected_bool_got_int(const char *expected, const char *got) {
    return strcmp(expected, "bool") == 0 && strcmp(got, "int") == 0;
}

bool expected_char_star_got_char_bracket(const char *expected, const char *got) {
    return strcmp(expected, "char *") == 0 && strncmp(got, "char[", 4) == 0;
}

bool expected_const_got_not_const(const char *expected, const char *got) {
    /* FIXME: Is this really okay? */
    if (strncmp(expected, "const ", 6) == 0) {
        return strcmp(expected + 6, got) == 0;
    } else {
        return false;
    }
}

bool expected_time_t_got_int(const char *expected, const char *got) {
    return strcmp(expected, "time_t") == 0 && strcmp(got, "long int") == 0;
}

bool integer_types_match(const char *expected, tree got) {
    if (TYPE_UNSIGNED(got)) {
        return strcmp(expected, "unsigned int") == 0 || strcmp(expected, "unsigned long") == 0 ||
               strcmp(expected, "unsigned long long") == 0 || strncmp(expected, "uint", 4) == 0 ||
               strcmp(expected, "guint") == 0;
    } else {
        return strcmp(expected, "int") == 0 || strcmp(expected, "long") == 0 ||
               strcmp(expected, "long long") == 0 || strncmp(expected, "int", 3) == 0 ||
               strcmp(expected, "gint") == 0;
    }
}

bool is_pointer_type(const char *t) {
    return t[strlen(t) - 1] == '*';
}

bool is_void_pointer(tree t) {
    return TREE_CODE(t) == POINTER_TYPE && TREE_CODE(TREE_TYPE(t)) == VOID_TYPE;
}

bool weird_enums_match(const char *expected, const char *got) {
    if (strcmp(got, "int") != 0) {
        return false;
    }

    if (strcmp(expected, "enum shadow_disp_flags") == 0) {
        return true;
    } else if (strcmp(expected, "enum pcmk__fence_history") == 0) {
        return true;
    }

    return false;
}

bool types_match(const char *expected_ty, const char *got_ty) {
    if (strcmp(expected_ty, got_ty) == 0) {
        return true;

    } else {
        const char *aliased_got_ty = type_alias(got_ty);

        if (strcmp(expected_ty, aliased_got_ty) == 0) {
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
    unsigned int num_args = gimple_call_num_args(stmt);
    unsigned int n = 2;

    for (const auto& param : expected_params) {
        tree arg_tree = gimple_call_arg(stmt, n);
        tree ty = TREE_TYPE(arg_tree);
        char *got_ty = print_tree_to_str(ty);
        size_t ndx;
        bool match;

        const char *expected_ty = param.c_str();

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

        /* Remove the trailing newline that the gcc pretty printer adds.  I don't care
         * about the one byte memory leak right now.
         */
        ndx = strcspn(got_ty, "\n");
        got_ty[ndx] = '\0';

        match = types_match(expected_ty, got_ty);
        if (!match) {
            /* If the types don't match, see if they both start with "const ".
             * If so, strip that off and try the comparison again.
             */
            if (strncmp(expected_ty, "const ", 6) == 0 && strncmp(got_ty, "const ", 6) == 0) {
                const char *new_expected_ty = expected_ty + 6;
                const char *new_got_ty = got_ty + 6;

                match = types_match(new_expected_ty, new_got_ty);
            }
        }

        if (!match) {
            /* +1 is because params are zero-indexed, but users will start counting with 1 */
            error_at(stmt->location, "Expected '%s', but got '%s' in argument %d",
                     expected_ty, got_ty, n+1);
        }

        free(got_ty);
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
        error_at(EXPR_LOCATION(t), "Expected %d argument(s) to message '%s', but got %d",
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
