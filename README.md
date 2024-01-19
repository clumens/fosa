These tools are for performing extra compile-time checks on pacemaker.  In particular,
they are for checking the number and types of arguments passed to the formatted output
messages.  All those functions expect varargs, making standard compiler checks useless.

These are implemented as GCC plugins, so a recent enough compiler and the appropriate
plugin devel package are required.  Then just build this from source with "make".

There are two steps to this process.  The first part scans the source tree for specially
marked functions and records information about them into a data file.  The second part
takes the previously recorded type information, looks for where formatted output
messages are called, and verifies they are called with the right number and type of
arguments.

To use these tools, go into your cloned pacemaker source tree and apply step-1.patch.
You'll need to change the directory paths to match your system.  Then just build as
normal.  A text file will be written out to the path you gave when you applied the
patch.

Then "make clean", apply step-2.patch (again, changing the directory paths), and
rebuild.  Any errors in message arguments will be detected and cause the compiler to
stop, just like any other build failure would.
