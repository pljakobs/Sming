Clang Tools
===========

`clang-format <https://releases.llvm.org/8.0.1/tools/clang/docs/ClangFormat.html>`__
is a tool that implements automatic source code formatting.
It can be used to automatically enforce the layout rules for Sming.

`clang-tidy <https://clang.llvm.org/extra/clang-tidy/>`__
is a C++ "linter" tool to assist with diagnosing and fixing typical programming errors
including portability/readability issues, bug-prone code constructs,
interface misuse, or bugs that can be deduced via static analysis.

You can find details for the current release at https://releases.llvm.org/download.html.
Note that *clang-format* is part of the main **Clang** project, whilst *clang-tidy* can be
found in **clang-tools-extra**.


clang-format
------------

Installation
~~~~~~~~~~~~

Sming requires version 8 which is generally no longer available in the standard repositories for recent GNU/Linux distributions.
You can find standalone builds at https://github.com/muttleyxd/clang-tools-static-binaries/releases.

For example:

```
export CLANG_FORMAT=/opt/clang-format-8
wget https://github.com/muttleyxd/clang-tools-static-binaries/releases/download/master-32d3ac78/clang-format-8_linux-amd64 -O /opt/clang-format-8
chmod +x $CLANG_FORMAT
```

You should persist the definition for :envvar:`CLANG_FORMAT` as Sming uses this when running the ``make cs`` commands (see below).


.. important::

   Different versions of clang-format can produce different results,
   despite using the same configuration file.

   We are using version 8.0.1 of clang-format on our
   Continuous Integration (CI) System.

   You should install the same version on your development computer.


Rules
~~~~~

The coding rules are described in the
`.clang-format <https://github.com/SmingHub/Sming/blob/develop/.clang-format>`__
file, located in the root directory of the framework.

You should not edit this file unless it is a discussed and agreed coding
style change.

IDE integration
~~~~~~~~~~~~~~~

There are multiple existing integrations for IDEs. You can find details
in the `ClangFormat documentation <https://clang.llvm.org/docs/ClangFormat.html>`__.

For VS Code/Codium install the **clang-format** extension and configure the path with the location of the **clang-format-8** executable.

For the Eclipse IDE we recommend installing
the `CppStyle plugin <https://github.com/wangzw/CppStyle>`__. You can
configure your IDE to auto-format the code on "Save" using the
recommended coding style and/or format according to our coding style
rules using Ctrl-Shift-F (for formatting of whole file or selection of
lines). Read
`Configure CppStyle <https://github.com/wangzw/CppStyle#configure-cppstyle>`__
for details.

Command Line
~~~~~~~~~~~~

Single File

   If you want to directly apply the coding standards from the command line
   you can run the following command::

      cd $SMING_HOME
      clang-format -style=file -i Core/<modified-file>

   Where ``Core/<modified-file>`` should be replaced with the path to
   the file that you have modified.

All files

   The following command will run again the coding standards formatter over
   all C, C++ and header files inside the ``Sming/Core``, ``samples`` and 
   other key directories::

      cd $SMING_HOME
      make cs

   The command needs time to finish. So be patient. It will go over all
   files and will try to fix any coding style issues.
   
   If you wish to apply coding style to your own project, add an empty ``.cs`` marker file
   to any directory containing source code or header files. All source/header files
   in that directory and any sub-directories will be formatted when you run::
   
      make cs
   
   from your project directory.

Eclipse
~~~~~~~

If you have installed CppStyle as described above you can
configure Eclipse to auto-format your files on *Save*.

Alternatively, you can manually apply the coding style rules by selecting the source code of a
C, C++ or header file or a selection in it and run the ``Format`` command
(usually Ctrl-Shift-F).


clang-tidy
----------

Installation
~~~~~~~~~~~~

No specific version is required but generally you should aim to use the most recent version
available in your distribution. Version 17.0.6 was used at time of writing these notes.

In Ubuntu you should be able install using the following command::

   sudo apt-get install clang-tidy

See the the `download <http://releases.llvm.org/download.html>`__ page
of the Clang project for installation instructions for other operating
systems.

Configuration
~~~~~~~~~~~~~

The default tool configuration is defined in the
`.clang-tidy <https://github.com/SmingHub/Sming/blob/develop/.clang-tidy>`__
file, located in the root directory of the framework.

.. note::

   Unlike clang-format, clang-tidy has to be able to compile the target code in order to perform static analysis.
   Code must build without errors for **Host** architecture.
   This means it cannot check code modules for embedded devices, that is, anything in ``Arch/`` which isn't ``Host/``.
   It is therefore good practice to keep the device-specific modules to a minimum.

   No object code is generated by clang-tidy.

Usage
~~~~~

Only source files which haven't been built are inspected.
So, to restrict which code gets processed built the entire application normally,
then 'clean' the relevant modules before proceeding with clang-tidy.

For example::

   cd $SMING_HOME/../samples/Basic_Servo
   make -j SMING_SOC=host
   make clean Servo-clean
   make CLANG_TIDY=clang-tidy

If you want to fix a particular type of problem, it's usually best to be explicit::

   make CLANG_TIDY="clang-tidy --checks='-*,modernize-use-equals-default' --fix"

Remember to run ``make cs`` and check the output before committing!

If you want to provide a custom configuration file::

   make CLANG_TIDY="clang-tidy --config-file=myTidyConfig"


.. note::

   clang-tidy can take a long time to do its work, so it's tempting to use the `-j` option
   to speed things up.
   You may see some corrupted output though as the output from multiple clang-tidy
   instances aren't serialised correctly.
   It's usually fine to get a rough 'first-pass' indication of any problems though.

   However, if attempting to apply fixes **DO NOT** use the -j option as this will result in corrupted output.
