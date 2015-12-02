APSW stands for Another Python SQLite Wrapper.

WARNING: This is a bayeslite custom edition.  This is not the normal
apsw.  It is a temporary kludge until the `apsw' package on pypi
works.  This was customized by running::

    python setup.py fetch --sqlite --version=3.9.2

so that it includes a copy of a working version of sqlite3 instead of
relying on whatever version your system may have installed.

About
=====

APSW is a Python wrapper for the `SQLite <http://sqlite.org/>`__
embedded relational database engine. In contrast to other wrappers
such as `pysqlite <https://github.com/ghaering/pysqlite>`__ it focuses
on being a minimal layer over SQLite attempting just to translate the
complete SQLite API into Python.  The `documentation
<http://rogerbinns.github.io/apsw/pysqlite.html>`__ has a section on
the differences between APSW and pysqlite.  APSW supports CPython 2.3
onwards and CPython 3.1 onwards.

Changes
=======

`Full detailed list of changes <http://rogerbinns.github.io/apsw/changes.html>`__

Releases since 3.8.2-r1 are in `releases
<https://github.com/rogerbinns/apsw/releases>`__ (`downloads
<http://rogerbinns.github.io/apsw/download.html>`__)

Older releases are at the previous `Google Code hosting
<https://code.google.com/p/apsw/downloads/list?can=1>`__

Help/Documentation
==================

The latest documentation is at http://rogerbinns.github.io/apsw/

Mailing lists/contacts
======================

* `Python SQLite discussion group <http://groups.google.com/group/python-sqlite>`__
* You can also email the author at rogerb@rogerbinns.com

Bugs
====

You can find existing and fixed bugs by clicking on `Issues
<https://github.com/rogerbinns/apsw/issues>`__ and using "New Issue"
to report previously unknown issues.

Downloads
=========

The `download documentation
<http://rogerbinns.github.io/apsw/download.html>`__ contains a list of
binaries, source and further details including how to verify the
downloads, and packages available for other operating systems.

License
=======

See `LICENSE
<https://github.com/rogerbinns/apsw/blob/master/LICENSE>`__ - in
essence any OSI approved open source license.
