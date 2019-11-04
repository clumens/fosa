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

Then go into your pacemaker source tree and apply the following (changing the paths,
obviously):

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
+CFLAGS="$CFLAGS -fplugin=/home/clumens/src/gcc-python-plugin/python.so -fplugin-arg-python-script=/home/clumens/src/fosa/check-args.py"
+
 AC_SUBST(CFLAGS)
 
 dnl This is useful for use in Makefiles that need to remove one specific flag
```

Then just build as normal.  Any errors in message arguments will be detected and cause
the compiler to stop, just like any other syntax error would.
