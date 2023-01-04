"
" SRT - Secure, Reliable, Transport
" Copyright (c) 2020 Haivision Systems Inc.
"
" This Source Code Form is subject to the terms of the Mozilla Public
" License, v. 2.0. If a copy of the MPL was not distributed with this
" file, You can obtain one at http://mozilla.org/MPL/2.0/.
"
" This file describes MAF ("manifest") file syntax used by
" SRT project.
"


if exists("b:current_syntax")
  finish
endif

" conditionals
syn match mafCondition  contained " - [!A-Za-z].*"hs=s+2

" section
syn match mafSection "^[A-Z][0-9A-Za-z_].*$" contains=mafCondition
syn match mafsection "^ .*$" contains=mafCondition

" comments
syn match  mafComment		"^\s*\zs#.*$"
syn match  mafComment		"\s\zs#.*$"
syn match  mafComment	contained	"#.*$"


" hilites

hi def link mafComment Comment
hi def link mafSection Statement
hi def link mafCondition Number


let b:current_syntax = "maf"
