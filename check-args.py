import gcc
import pickle
import sys

registeredMessages = {}

basicTypeMap = {
    "long long unsigned int":   "unsigned long long int",
}

glibTypeMap = {
    "GHashTable *":             "struct GHashTable *",
    "GList *":                  "struct GList *",
    "gchar *":                  "char *",
    "guint":                    "unsigned int",
}

miscTypeMap = {
    "xmlNodePtr":               "struct xmlNode *",
}

pcmkTypeMap = {
    "cib_t *":                  "struct cib_t *",
    "crm_exit_t":               "crm_exit_e",
    "lrmd_list_t *":            "struct lrmd_list_t *",
    "op_digest_cache_t *":      "struct op_digest_cache_t *",
    "pcmk__fence_history":      "enum pcmk__fence_history",
    "pcmk_pacemakerd_state":    "enum pcmk_pacemakerd_state",
    "pe__location_t *":         "struct pe__location_t *",
    "pe_action_t *":            "struct pe_action_t *",
    "pe_node_t *":              "struct pe_node_t *",
    "pe_resource_t *":          "struct pe_resource_t *",
    "pe_ticket_t *":            "struct pe_ticket_t *",
    "pe_working_set_t *":       "struct pe_working_set_t *",
    "resource_checks_t *":      "struct resource_checks_t *",
    "stonith_history_t *":      "struct stonith_history_t *",
}

def type_alias(t):
    # FIXME: I'm not sure this is right
    if t.startswith("char["):
        return "char *"

    if t.startswith("const char["):
        return "const char *"

    if t in basicTypeMap:
        return basicTypeMap[t]

    if t in glibTypeMap:
        return glibTypeMap[t]

    if t in miscTypeMap:
        return miscTypeMap[t]

    if t in pcmkTypeMap:
        return pcmkTypeMap[t]

    if t.startswith("const "):
        # Strip off "const " from the front of the type and look up if there's
        # an alias for it in any of the maps.  But then put "const " back on
        # afterwards so we are comparing the right things in check_arg_types.
        return "const %s" % type_alias(t.removeprefix("const "))

    return t

def is_integer_type(t):
    return t in ["int", "unsigned int", "long", "unsigned long", "unsigned long long", "uint32_t"]

def is_pointer_type(t):
    return t.endswith(" *")

def message_from_fn_call(arg):
    return arg.def_stmt.fndecl.name in ["crm_element_name", "crm_map_element_name"]

def check_arg_count(stmt, messageName):
    entry = registeredMessages[messageName]

    if len(stmt.args) != len(entry) + 2:
        gcc.error(stmt.loc, "Expected %d arguments to message %s, but got %d" %
                  (len(entry), messageName, len(stmt.args)-2))
        return False

    return True

def check_arg_types(stmt, messageName):
    entry = registeredMessages[messageName]

    for i in range(0, len(entry)):
        arg = stmt.args[i+2]

        expectedTy = entry[i]
        givenTy    = str(arg.type)

        aliasedExpectedTy = type_alias(expectedTy)
        aliasedGivenTy = type_alias(givenTy)

        # If the types are the same, obviously that is fine.
        if aliasedExpectedTy == aliasedGivenTy:
            return True

        # If we were given a type but expected a const of the same type,
        # that's fine.
        if aliasedExpectedTy == "const %s" % aliasedGivenTy:
            return True

        # An integer literal works for any kind of expected integer type,
        # regardless of if we're expecting signed or not.
        if isinstance(arg, gcc.IntegerCst) and is_integer_type(expectedTy):
            return True

        # We were given a "void *" but expected some other kind of pointer.
        # That should be fine.
        # FIXME:  But really, there should be some way of seeing if there's
        # a cast involved and check that if so.
        if isinstance(arg.type, gcc.PointerType) and isinstance(arg.type.dereference, gcc.VoidType) and is_pointer_type(expectedTy):
            return True

        gcc.error(stmt.loc, "Expected '%s', but got '%s' in argument %d" % (expectedTy, givenTy, i+3))
        return False

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

            if isinstance(stmt.args[1], gcc.SsaName) and message_from_fn_call(stmt.args[1]):
                # This is a call to the message function that figures out the message
                # name by calling some function.  We can't figure out exactly which
                # message will be called at compile time, but it's almost certainly
                # going to be one of these four.  Iterate over each and check.  They
                # should all have the same arguments.
                for messageName in ["bundle", "clone", "group", "primitive"]:
                    if messageName not in registeredMessages:
                        gcc.error(stmt.loc, "Message not registered: %s" % messageName)
                        break

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

                if messageName not in registeredMessages:
                    gcc.error(stmt.loc, "Unknown format message: %s" % messageName)
                    continue

                # Check that enough arguments were provided.  The expected length does
                # not include the first two arguments, which are not for the message.
                if not check_arg_count(stmt, messageName):
                    continue

                # Check that the types are as expected.
                check_arg_types(stmt, messageName)

if "messages" not in gcc.argument_dict:
    print("-fplugin-arg-python-messages= argument is missing")
    sys.exit(1)

try:
    with open(gcc.argument_dict["messages"], "rb") as f:
        registeredMessages = pickle.load(f)
except FileNotFoundError:
    registeredMessages = {}

gcc.register_callback(gcc.PLUGIN_PASS_EXECUTION, find_function_calls)
