These tools are for performing extra compile-time checks on pacemaker.  In particular,
they are for checking the number and types of arguments passed to the formatted output
messages.  All those functions expect varargs, making standard compiler checks useless.

Everything requires the gcc-python-plugin.  You can try the packaged version, but I
have not had much luck with that.  Instead, build it from source:

```
$ git clone https://github.com/davidmalcolm/gcc-python-plugin
$ cd gcc-python-plugin
$ make PYTHON=python3 PYTHON_CONFIG=python3-config
```

There are two parts to this process.  The first part scans the source tree for specially
marked functions and puts those into a data file.  Go into the cloned pacemaker source
tree and apply step-1.patch.  You'll need to change my home directory to your home
directory (or wherever you have checked out the fosa repo) after applying it.

Then just build as normal.  A fns.pickle file will be written into your source directory.
This just contains type information about all the formatted output message functions.

The second part takes that type information, looks for where formatted output messages
are called, and verifies they are called with the right number and kind of arguments.
Clean the source tree, apply step-2.patch (again, changing the home directory path),
and rebuild.  Any errors in message arguments will be detected and cause the compiler
to stop, just like any other syntax error would.
