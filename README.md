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
tree and apply the following (changing the paths, obviously):

```
diff --git a/configure.ac b/configure.ac
index bd92b803f..7088ff565 100644
--- a/configure.ac
+++ b/configure.ac
@@ -1855,6 +1855,9 @@ if test "x${enable_fatal_warnings}" = xyes ; then
     AC_MSG_NOTICE(Enabling Fatal Warnings)
     CFLAGS="$CFLAGS $WERROR"
 fi
+
+CFLAGS="$CFLAGS -fplugin=/home/clumens/src/gcc-python-plugin/python.so -fplugin-arg-python-script=/home/clumens/src/fosa/find-messages.py -fplugin-arg-python-messages=/home/clumens/src/pacemaker/fns.pickle"
+
 AC_SUBST(CFLAGS)
 
 dnl This is useful for use in Makefiles that need to remove one specific flag
```

Then just build as normal.  A fns.pickle file will be written into your source directory.
This just contains type information about all the formatted output message functions.

The second part takes that type information, looks for where formatted output messages
are called, and verifies they are called with the right number and kind of arguments.
Clean the source tree, change the `-fplugin-arg-python-script` argument to be
`check-args.py`, and rebuild.  Any errors in message arguments will be detected and
cause the compiler to stop, just like any other syntax error would.
