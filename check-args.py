import gcc
import gccutils

# -fplugin=/home/clumens/src/gcc-python-plugin/python.so -fplugin-arg-python-script=/home/clumens/src/check-args.py

messages = { "ban":              ["struct pe_node_t *", "struct pe__location_t *", "unsigned int"],
             "bundle":           ["unsigned int", "struct pe_resource_t *"],
             "clone":            ["unsigned int", "struct pe_resource_t *"],
             "cluster-counts":   ["unsigned int", "int", "int", "int"],
             "cluster-dc":       ["struct node_t *", "const char *", "const char *", "char *"],
             "cluster-options":  ["struct pe_working_set_t *"],
             "cluster-stack":    ["const char *"],
             "cluster-times":    ["const char *", "const char *", "const char *", "const char *"],
             "failed-action":    ["xmlNodePtr"],
             "group":            ["unsigned int", "struct pe_resource_t *"],
             "last-fenced":      ["const char *", "time_t"],
             "node":             ["struct node_t *", "unsigned int", "gboolean", "const char *"],
             "node-attribute":   ["const char *", "const char *", "gboolean", "int"],
             "op-history":       ["struct xmlNode *", "const char *", "const char *", "int", "unsigned int"],
             "primitive":        ["unsigned int", "struct pe_resource_t *"],
             "resource-history": ["struct resource_t *", "const char *", "gboolean", "int", "time_t"],
             "stonith-event":    ["struct stonith_history_t *", "unsigned int", "gboolean"],
             "ticket":           ["struct ticket_t *"],
             "validate":         ["const char *", "const char *", "char *", "char *", "int"],
           }

def type_alias(t):
    if t == "gboolean":
        return "int"
    elif t == "time_t":
        return "long int"
    elif t == "xmlNodePtr":
        return "struct xmlNode *"
    else:
        return t

def is_integer_type(t):
    return t in ["int", "unsigned int", "long", "unsigned long"]

def is_pointer_type(t):
    return t.endswith(" *")

def check_arg_count(stmt, messageName):
    entry = messages[messageName]

    if len(stmt.args) != len(entry) + 2:
        gcc.error(stmt.loc, "Expected %d arguments to message %s, but got %d" %
                  (len(entry), messageName, len(stmt.args)-2))
        return False

    return True

def check_arg_types(stmt, messageName):
    entry = messages[messageName]

    for i in range(0, len(entry)):
        arg = stmt.args[i+2]

        expectedTy = entry[i]
        givenTy    = str(arg.type)

        if type_alias(expectedTy) != type_alias(givenTy):
            # An integer literal works for any kind of expected integer type,
            # regardless of if we're expecting signed or not.
            if isinstance(arg, gcc.IntegerCst) and is_integer_type(expectedTy):
                return True

            # An integer constant of 0 where a pointer is expected is likely a
            # NULL.  That works for all pointer types, so skip the error.
            if isinstance(arg, gcc.IntegerCst) and arg.constant == 0 and is_pointer_type(expectedTy):
                return True

            gcc.error(stmt.loc, "Expected '%s', but got '%s' in argument %d" % (expectedTy, givenTy, i+3))
            return False

    return True

def find_function_calls(p, fn):
    if p.name != "*warn_function_return":
        return

    for bb in fn.cfg.basic_blocks:
        if not bb.gimple:
            continue

        for stmt in bb.gimple:
            # Filter out anything that's not a function call.
            if not isinstance(stmt, gcc.GimpleCall):
                continue

            # Filter out anything that's not a function pointer reference.  We're only
            # looking for "out->message" function calls, which go through a pointer.
            if not isinstance(stmt.fn, gcc.SsaName):
                continue

            if not isinstance(stmt.fn.def_stmt, gcc.GimpleAssign):
                continue

            if len(stmt.fn.def_stmt.rhs) != 1:
                continue

            # Filter out anything that's not a pcmk__output_t and that's not a reference
            # to the message field.
            if not hasattr(stmt.fn.def_stmt.rhs[0], "target") or not hasattr(stmt.fn.def_stmt.rhs[0], "field"):
                continue

            target = stmt.fn.def_stmt.rhs[0].target
            field  = stmt.fn.def_stmt.rhs[0].field

            if str(target.type) != "struct pcmk__output_t" or field.name != "message":
                continue

            # The first two arguments are the pcmk__output_t and the name of the message
            # being called.  The compiler should have caught any cases where this is
            # wrong, but just in case...
            if len(stmt.args) < 2:
                continue

            if isinstance(stmt.args[1], gcc.SsaName) and stmt.args[1].def_stmt.fndecl.name == "crm_element_name":
                # This is a call to the message function that figures out the message
                # name by calling crm_element_name().  We can't figure out exactly
                # which message will be called at compile time, but it's almost certainly
                # going to be one of these four.  Iterate over each and check.  They
                # should all have the same arguments.
                for messageName in ["bundle", "clone", "group", "primitive"]:
                    if not check_arg_count(stmt, messageName):
                        break

                    if not check_arg_types(stmt, messageName):
                        break
            elif not isinstance(stmt.args[1], gcc.AddrExpr):
                # This is a call to the message function that uses some other method
                # to determine the message name.  We can't figure it out, so just
                # print a note and keep going.
                gcc.inform(stmt.loc, "Cannot figure out message name")
                continue
            else:
                # This is a call to the message function that uses a string literal
                # for the message name.  That's easy.
                messageName = str(stmt.args[1]).replace('"', '')

                if messageName not in messages:
                    gcc.error(stmt.loc, "Unknown format message: %s" % messageName)
                    continue

                # Check that enough arguments were provided.  The expected length does
                # not include the first two arguments, which are not for the message.
                if not check_arg_count(stmt, messageName):
                    continue

                # Check that the types are as expected.
                check_arg_types(stmt, messageName)

gcc.register_callback(gcc.PLUGIN_PASS_EXECUTION, find_function_calls)
