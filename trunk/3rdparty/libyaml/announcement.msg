From: Tina Müller <post@tinita.de>
To: yaml-core@lists.sourceforge.net
Subject: [ANN] LibYAML-0.2.5: A new release

=========================
 Announcing LibYAML-0.2.5
=========================

A new release of LibYAML is now available:
https://github.com/yaml/libyaml/tree/0.2.5

The LibYAML project is now maintained by the YAML community. Planning happens
on the #yaml-dev and #libyaml IRC channels on irc.freenode.net.


Changes
=======


https://github.com/yaml/libyaml/pull/187
Add -h and --flow (on|off|keep) to run-*-test-suite

https://github.com/yaml/libyaml/pull/182
Remove unnecessary include and malloc

https://github.com/yaml/libyaml/pull/177
Add specific files back to .gitignore

https://github.com/yaml/libyaml/pull/181
Output error position in run-parser-test-suite.c

https://github.com/yaml/libyaml/pull/105
Allow question marks in plain scalars in flow collections

https://github.com/yaml/libyaml/pull/186
Emitter: Don't output trailing space for empty scalar nodes

https://github.com/yaml/libyaml/pull/185
Emitter: Output space after an alias mapping key

https://github.com/yaml/libyaml/pull/191
A couple patches to improve test suite support


Resources
=========

LibYAML IRC Channel:  #libyaml on irc.freenode.net
LibYAML homepage:     https://github.com/yaml/libyaml
Source download:      https://github.com/yaml/libyaml/archive/dist-0.2.3.zip
GitHub repository:    https://github.com/yaml/libyaml
Bug tracking:          https://github.com/yaml/libyaml/issues

YAML homepage: http://yaml.org/
YAML-core mailing list: http://lists.sourceforge.net/lists/listinfo/yaml-core


About LibYAML
=============

YAML is a data serialization format designed for human readability and
interaction with scripting languages.

LibYAML is a C library for parsing and emitting YAML. LibYAML is the underlying
parser/emitter code for YAML frameworks in many programming languages.


Maintainers
===========

The following people are responsible for maintaining LibYAML:

* Ingy döt Net

and many thanks to all who have contribributed!
See: https://github.com/yaml/libyaml/pulls


Copyright
=========

Copyright (c) 2017-2020 Ingy döt Net <ingy@ingy.net>
Copyright (c) 2006-2016 Kirill Simonov <xi@resolvent.net>

The LibYAML module was written by Kirill Simonov.
It is currently maintained by the YAML community.

LibYAML is released under the MIT license.
See the file LICENSE for more details.
