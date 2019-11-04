import gcc
import pickle
import sys

def output_args_cb(fn, messageName, *args):
    types = [x.constant for x in args]

    try:
        with open(gcc.argument_dict["messages"], "rb") as f:
            registeredMessages = pickle.load(f)
    except FileNotFoundError:
        registeredMessages = {}

    if messageName.constant in registeredMessages:
        if types != registeredMessages[messageName.constant]:
            gcc.error(fn.location, "Argument list is different from previous definition")
    else:
        registeredMessages[messageName.constant] = types

    with open(gcc.argument_dict["messages"], "wb") as f:
        pickle.dump(registeredMessages, f, pickle.DEFAULT_PROTOCOL)

def register_attributes():
    gcc.register_attribute("output_args", 1, -1, False, False, False, output_args_cb)
    gcc.define_macro("WITH_ATTRIBUTE_OUTPUT_ARGS")

if "messages" not in gcc.argument_dict:
    print("-fplugin-arg-python-messages= argument is missing")
    sys.exit(1)

gcc.register_callback(gcc.PLUGIN_ATTRIBUTES, register_attributes)
